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
    "  -s smoothing-bandwidth\n"
    "  -c compression-threshold,damping-ratio\n"
    "  -n normalize-absolute-maximum\n"
    "  -l (measure-lkfs-loudness)\n"
    "  -i processing-interval\n");
  exit(1);
}

#define OPERATION_NOISE_PROFILE   0
#define OPERATION_DENOISE         1
#define OPERATION_COMPRESS        2
#define OPERATION_ABSNORMALIZE    3
#define OPERATION_MEASURE_LKFS    4

typedef struct {
  int type;
  FP_TYPE compression_threshold;
  FP_TYPE compression_damping;
  FP_TYPE normalization_max;
} operation;

FILE* fp_analyze_noise_profile = NULL;
FILE* fp_denoise_noise_profile = NULL;
FILE* fp_wavin = NULL;
FILE* fp_wavout = NULL;

FP_TYPE denoise_rate = 1.0;
FP_TYPE smoothing_bandwidth = 500.0;

int specified_thop = 0;
int nhop = 0; // will be sample-rate-dependent
int nfft = 0;
int pad_factor = 2;
int hop_factor = 4;

// the current audio
FP_TYPE* x = NULL;
int nx = 0;
int fs = 0;
int nbit = 0;

operation operation_chain[128];
int num_operation = 0;
operation op; // the current operation

typedef struct {
  FP_TYPE loudness_total;
  FP_TYPE* loudness_inst;
  int size, interval_48k;
} lkfs;

static void delete_lkfs(lkfs* dst) {
  if(dst == NULL) return;
  free(dst -> loudness_inst);
  free(dst);
}

static lkfs* lkfs_loudness(FP_TYPE* x, int nx, int fs, int nhop) {
  // Resample to 48 kHz.
  FP_TYPE* x0 = rresample(x, nx, 48000.0f / fs, & nx);
  nhop = nhop * 48000 / fs;
  fs = 48000;
  // 1st stage filtering
  FP_TYPE B1[3] = {1.53512485958697, -2.69169618940638, 1.19839281085285};
  FP_TYPE A1[3] = {1.0, -1.69065929318241, 0.73248077421585};
  FP_TYPE* x1 = filter(B1, 3, A1, 3, x0, nx);
  free(x0);
  // 2nd stage filtering
  FP_TYPE B2[3] = {1.0, -2.0, 1.0};
  FP_TYPE A2[3] = {1.0, -1.99004745483398, 0.99007225036621};
  FP_TYPE* x2 = filter(B2, 3, A2, 3, x1, nx);
  free(x1);
  // Measure the short-time power.
  int framesize = nhop * 4;
  int nfrm = max(1, (nx - framesize) / nhop);
  FP_TYPE* z = calloc(nfrm, sizeof(FP_TYPE));
  FP_TYPE* l = calloc(nfrm, sizeof(FP_TYPE));
  for(int i = 0; i < nfrm; i ++) {
    FP_TYPE* xfrm = fetch_frame(x2, nx, (i + 2) * nhop, framesize);
    for(int j = 0; j < framesize; j ++) z[i] += xfrm[j] * xfrm[j];
    z[i] /= framesize;
    l[i] = -0.691 + 10.0 * log10(z[i]);
    free(xfrm);
  }
  // Compute the relative threshold.
  FP_TYPE power_sum = 0; int power_count = 0;
  for(int i = 0; i < nfrm; i ++) {
    if(l[i] > -70) {
      power_sum += z[i];
      power_count ++;
    }
  }
  FP_TYPE threshold = -70;
  if(power_count > 0) {
    power_sum /= power_count;
    threshold = -0.691 + 10.0 * log10(power_sum) - 10.0;
  }
  // Compute the total loudness;
  power_sum = 0; power_count = 0;
  for(int i = 0; i < nfrm; i ++) {
    if(l[i] > threshold) {
      power_sum += z[i];
      power_count ++;
    }
  }
  free(z);
  free(x2);

  lkfs* ret = malloc(sizeof(lkfs));
  ret -> loudness_total = power_count > 0 ?
    -0.691 + 10.0 * log10(power_sum / power_count) : -70.0;
  ret -> loudness_inst = l;
  ret -> size = nfrm;
  ret -> interval_48k = nhop;
  return ret;
}

int main_analyze() {
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

  nx = 0;

  return 1;
}

int main_denoise() {
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

  FP_TYPE* gain = calloc(ns, sizeof(FP_TYPE));
  FP_TYPE smoothing_bins = smoothing_bandwidth / fs * nfft / 2.0;
  for(int i = 0; i < nfrm; i ++) {
    for(int j = 0; j < ns; j ++) {
      FP_TYPE power = spec_magn[i][j] * spec_magn[i][j] + M_EPS;
      gain[j] = fmax(0, 1.0 - spec_mean[j] * denoise_rate / power);
    }
    FP_TYPE* smooth_gain = moving_avg(gain, ns, smoothing_bins);
    for(int j = 0; j < ns; j ++)
      spec_magn[i][j] *= fmax(gain[j], sqrt(smooth_gain[j]));
    free(smooth_gain);
  }
  free(gain);
  
  int ny = nx;
  FP_TYPE* y = istft(spec_magn, spec_phse, nhop, nfrm,
    hop_factor, pad_factor, normfc, & ny);
  free(x);
  x = y;
  nx = ny;

  free(spec_mean);
  free2d(spec_magn, nfrm);
  free2d(spec_phse, nfrm);
  return 1;
}

int main_compress() {
  int nfrm = nx / nhop;
  FP_TYPE* w = hanning(nhop * 2);
  for(int i = 0; i < nfrm; i ++) {
    int center = i * nhop;
    FP_TYPE* xfrm = fetch_frame(x, nx, center, nhop * 2);
    FP_TYPE level = 0;
    for(int j = 0; j < nhop * 2; j ++)
      level = fmax(level, fabs(xfrm[j]));
    FP_TYPE gain = 1.0;
    if(level > op.compression_threshold) {
      gain = ((level - op.compression_threshold) * op.compression_damping +
        op.compression_threshold) / (level + M_EPS);
    }
    for(int j = 0; j < nhop * 2; j ++) {
      int idx = center + j - nhop;
      if(idx >= 0 && idx < nx)
        x[idx] += xfrm[j] * w[j] * (gain - 1.0);
    }
    free(xfrm);
  }
  free(w);
  return 1;
}

int main_absnormalize() {
  FP_TYPE absmax = 0;
  for(int i = 0; i < nx; i ++)
    absmax = fmax(absmax, fabs(x[i]));
  FP_TYPE gain = op.normalization_max / absmax;
  for(int i = 0; i < nx; i ++)
    x[i] *= gain;
  return 1;
}

int main_lkfs() {
  lkfs* measure = lkfs_loudness(x, nx, fs, nhop);
  printf("Total = %f LKFS\n", measure -> loudness_total);
  for(int i = 0; i < measure -> size; i ++) {
    FP_TYPE t = (FP_TYPE)(i + 2) * measure -> interval_48k / 48000;
    printf("%f, %f LKFS\n", t, measure -> loudness_inst[i]);
  }
  delete_lkfs(measure);
  return 1;
}

int main_deadfish() {
  x = wavread_fp(fp_wavin, & fs, & nbit, & nx);
  
  for(int i = 0; i < num_operation; i ++) {
    op = operation_chain[i];
    if(op.type == OPERATION_NOISE_PROFILE) {
      nhop = pow(2, ceil(log2(fs * 0.004)));
      if(specified_thop != 0) nhop = pow(2, round(log2(specified_thop * fs)));
      nfft = nhop * pad_factor * hop_factor;
      if(! main_analyze()) return 0;
      free(x); return 1;
    } else
    if(op.type == OPERATION_DENOISE) {
      nhop = pow(2, ceil(log2(fs * 0.004)));
      if(specified_thop != 0) nhop = pow(2, round(log2(specified_thop * fs)));
      nfft = nhop * pad_factor * hop_factor;
      if(! main_denoise()) return 0;
    } else
    if(op.type == OPERATION_COMPRESS) {
      nhop = round(fs * 0.03);
      if(specified_thop != 0) nhop = round(specified_thop * fs);
      if(! main_compress()) return 0;
    } else
    if(op.type == OPERATION_ABSNORMALIZE) {
      if(! main_absnormalize()) return 0;
    } else
    if(op.type == OPERATION_MEASURE_LKFS) {
      nhop = round(fs * 0.1);
      if(specified_thop != 0) nhop = round(specified_thop * fs);
      if(! main_lkfs()) return 0;
    }
  }

  if(fp_wavout != NULL)
    wavwrite_fp(x, nx, fs, nbit, fp_wavout);
  free(x);
  return 1;
}

extern char* optarg;
int main(int argc, char** argv) {
  int c;
  fp_wavin = stdin;
  fp_wavout = stdout;
  while((c = getopt(argc, argv, "a:d:r:s:c:n:li:h")) != -1) {
    int i;
    operation top_op;
    switch(c) {
    case 'a':
      fp_analyze_noise_profile = fopen(optarg, "wb");
      if(fp_analyze_noise_profile == NULL) {
        fprintf(stderr, "Cannot write to %s.\n", optarg);
        exit(1);
      }
      top_op.type = OPERATION_NOISE_PROFILE;
      operation_chain[num_operation ++] = top_op;
    break;
    case 'd':
      fp_denoise_noise_profile = fopen(optarg, "rb");
      if(fp_denoise_noise_profile == NULL) {
        fprintf(stderr, "Cannot open %s.\n", optarg);
        exit(1);
      }
      top_op.type = OPERATION_DENOISE;
      operation_chain[num_operation ++] = top_op;
    break;
    case 'r':
      denoise_rate = atof(optarg);
    break;
    case 's':
      smoothing_bandwidth = atof(optarg);
    break;
    case 'c':
      i = 0;
      while(optarg[i] != ',') {
        if(optarg[i] == 0) {
          fprintf(stderr, "-c option requires two comma-separated "
            "parameters.\n");
          exit(1);
        }
        i ++;
      }
      optarg[i] = 0;
      top_op.type = OPERATION_COMPRESS;
      top_op.compression_threshold = atof(optarg);
      top_op.compression_damping = atof(optarg + i + 1);
      operation_chain[num_operation ++] = top_op;
    break;
    case 'n':
      top_op.type = OPERATION_ABSNORMALIZE;
      top_op.normalization_max = atof(optarg);
      operation_chain[num_operation ++] = top_op;
    break;
    case 'l':
      top_op.type = OPERATION_MEASURE_LKFS;
      operation_chain[num_operation ++] = top_op;
    break;
    case 'i':
      specified_thop = atof(optarg);
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
      fp_wavout = fopen(argv[optind + 1], "wb");
      if(fp_wavout == NULL) {
        fprintf(stderr, "Cannot write to %s.\n", argv[optind + 1]);
        exit(1);
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
