/*
  DEADFISH
  ===

  Copyright (c) 2018 Kanru Hua. All rights reserved.

  Redistribution and use in source and binary forms, with or without modification,
  are permitted provided that the following conditions are met:

  1. Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

  2. Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation and/or
  other materials provided with the distribution.

  3. Neither the name of the copyright holder nor the names of its contributors
  may be used to endorse or promote products derived from this software without
  specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
  ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
  ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <getopt.h>
#include "external/ciglet/ciglet.h"

static void print_usage() {
  fprintf(stderr,
    "deadfish input-wav output-wav\n"
    "  -a analyze-noise-profile\n"
    "  -d denoise-noise-profile\n"
    "  -r denoise-rate\n"
    "  -i processing-interval-samples");
  exit(1);
}

FILE* fp_analyze_noise_profile = NULL;
FILE* fp_denoise_noise_profile = NULL;
FILE* fp_wavin = NULL;
FILE* fp_wavout = NULL;
FP_TYPE denoise_rate = 1.0;
int specified_nhop = 0;
int nhop = 0; // will be sample-rate-dependent
int nfft = 0;
int pad_factor = 2;
int hop_factor = 4;

int main_analyze(FP_TYPE* x, int nx, int fs) {
  int nfrm = nx / nhop;
  int ns = nfft / 2 + 1;
  FP_TYPE** spec_magn = malloc2d(nfrm, ns, sizeof(FP_TYPE));
  stft(x, nx, nhop, nfrm, hop_factor, pad_factor, NULL, NULL,
    spec_magn, NULL);
  FP_TYPE* spec_mean = calloc(ns, sizeof(FP_TYPE));
  for(int i = 0; i < nfrm; i ++)
    for(int j = 0; j < ns; j ++)
      spec_mean[j] += spec_magn[i][j] * spec_magn[i][j];

  float tmp = fs;
  fwrite(& tmp, 4, 1, fp_analyze_noise_profile);
  for(int j = 0; j < ns; j ++) {
    spec_mean[j] = spec_mean[j] / ns;
    tmp = spec_mean[j];
    fwrite(& tmp, 4, 1, fp_analyze_noise_profile);
  }
  free(spec_mean);
  free2d(spec_magn, nfrm);
  return 1;
}

int main_denoise(FP_TYPE* x, int nx, int nbit, int fs) {
  int nfrm = nx / nhop;
  int ns = nfft / 2 + 1;

  fseek(fp_denoise_noise_profile, 0, SEEK_END);
  int fsize = ftell(fp_denoise_noise_profile);
  fseek(fp_denoise_noise_profile, 0, SEEK_SET);
  if(fsize / 4 - 1 != ns) {
    fprintf(stderr, "Error: invalid noise profile.\n");
    return 0;
  }
  float tmp;
  fread(& tmp, 4, 1, fp_denoise_noise_profile);
  if(tmp != fs) {
    fprintf(stderr, "Error: invalid noise profile.\n");
    return 0;
  }
  FP_TYPE* spec_mean = calloc(ns, sizeof(FP_TYPE));
  for(int i = 0; i < ns; i ++) {
    fread(& tmp, 4, 1, fp_denoise_noise_profile);
    spec_mean[i] = tmp;
  }

  FP_TYPE** spec_magn = malloc2d(nfrm, ns, sizeof(FP_TYPE));
  FP_TYPE** spec_phse = malloc2d(nfrm, ns, sizeof(FP_TYPE));
  FP_TYPE normfc;
  stft(x, nx, nhop, nfrm, hop_factor, pad_factor, & normfc, NULL,
    spec_magn, spec_phse);

  for(int i = 0; i < nfrm; i ++)
    for(int j = 0; j < ns; j ++) {
      FP_TYPE power = spec_magn[i][j] * spec_magn[i][j] + M_EPS;
      spec_magn[i][j] *=
        sqrt(fmax(0, power - spec_mean[j] * denoise_rate) / power);
    }
  
  int ny = nx;
  FP_TYPE* y = istft(spec_magn, spec_phse, nhop, nfrm,
    hop_factor, pad_factor, normfc, & ny);
  wavwrite_fp(y, ny, fs, nbit, fp_wavout);
  free(y);

  free(spec_mean);
  free2d(spec_magn, nfrm);
  free2d(spec_phse, nfrm);
  return 1;
}

int main_deadfish() {
  int fs, nbit, nx;
  FP_TYPE* x = wavread_fp(fp_wavin, & fs, & nbit, & nx);
  nhop = pow(2, ceil(log2(fs * 0.004)));
  if(specified_nhop != 0) nhop = specified_nhop;
  nfft = nhop * pad_factor * hop_factor;

  int ret = 1;
  if(fp_analyze_noise_profile) {
    ret = main_analyze(x, nx, fs);
  }
  if(fp_denoise_noise_profile) {
    ret = main_denoise(x, nx, nbit, fs);
  }
  free(x);
  return ret;
}

extern char* optarg;
int main(int argc, char** argv) {
  int c;
  fp_wavin = stdin;
  fp_wavout = stdout;
  while((c = getopt(argc, argv, "a:d:r:i:h")) != -1) {
    switch(c) {
    case 'a':
      fp_analyze_noise_profile = fopen(optarg, "wb");
      if(fp_analyze_noise_profile == NULL) {
        fprintf(stderr, "Cannot write to %s.\n", optarg);
        exit(1);
      }
    break;
    case 'd':
      fp_denoise_noise_profile = fopen(optarg, "rb");
      if(fp_denoise_noise_profile == NULL) {
        fprintf(stderr, "Cannot open %s.\n", optarg);
        exit(1);
      }
    break;
    case 'r':
      denoise_rate = atof(optarg);
    break;
    case 'i':
      specified_nhop = atoi(optarg);
      specified_nhop = pow(2, round(log2(specified_nhop)));
    break;
    case 'h':
      print_usage();
    break;
    }
  }
  if(optind < argc) {
    fp_wavin = fopen(argv[optind], "rb");
    if(fp_wavin == NULL) {
      fprintf(stderr, "Cannot open %s.\n", argv[optind]);
      exit(1);
    }
    if(optind < argc - 1) {
      if(! strcmp(argv[optind + 1], "-n"))
        fp_wavout = NULL;
      else {
        fp_wavout = fopen(argv[optind + 1], "wb");
        if(fp_wavout == NULL) {
          fprintf(stderr, "Cannot write to %s.\n", argv[optind + 1]);
          exit(1);
        }
      }
    }
  }

  if(! main_deadfish()) exit(1);

  if(fp_analyze_noise_profile != NULL)
    fclose(fp_analyze_noise_profile);
  if(fp_denoise_noise_profile != NULL)
    fclose(fp_denoise_noise_profile);
  if(fp_wavin != NULL)
    fclose(fp_wavin);
  if(fp_wavout != NULL)
    fclose(fp_wavout);
  return 0;
}
