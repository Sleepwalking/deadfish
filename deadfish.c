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
    "  -c compression-threshold,damping-ratio,(unit)\n"
    "  -n normalize-absolute-maximum,(unit)\n"
    "  -I inverse-filter-order,window-size\n"
    "  -l (measure-lkfs-loudness)\n"
    "  -t detect-threshold\n"
    "  -i processing-interval\n");
  exit(1);
}

#define OPERATION_NOISE_PROFILE   0
#define OPERATION_DENOISE         1
#define OPERATION_COMPRESS        2
#define OPERATION_COMPRESS_LKFS   3
#define OPERATION_NORMALIZE_ABS   4
#define OPERATION_NORMALIZE_LKFS  5
#define OPERATION_MEASURE_LKFS    6
#define OPERATION_INVERSE_FILER   7
#define OPERATION_DETECT_THOLD    8

typedef struct {
  int type;
  int order;
  FP_TYPE compression_threshold;
  FP_TYPE compression_damping;
  FP_TYPE normalization_max;
  FP_TYPE window_size;
} operation;

FILE* fp_analyze_noise_profile = NULL;
FILE* fp_denoise_noise_profile = NULL;
FILE* fp_wavin = NULL;
FILE* fp_wavout = NULL;

FP_TYPE denoise_rate = 1.0;
FP_TYPE smoothing_bandwidth = 500.0;

FP_TYPE specified_thop = 0;
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

#define db2mag(x) (pow(10.0, (x) / 20.0))

typedef struct {
  FP_TYPE loudness_total;
  FP_TYPE* loudness_inst;
  int size;
  FP_TYPE interval;
} loudness;

static void delete_loudness(loudness* dst) {
  if(dst == NULL) return;
  free(dst -> loudness_inst);
  free(dst);
}

static loudness* lkfs_loudness(FP_TYPE* x, int nx, int fs, FP_TYPE thop) {
  // Resample to 48 kHz.
  FP_TYPE* x0 = rresample(x, nx, 48000.0f / fs, & nx);
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
  int framesize = round(thop * 4 * fs);
  int nfrm = max(1, (nx - framesize) / (thop * fs));
  FP_TYPE* z = calloc(nfrm, sizeof(FP_TYPE));
  FP_TYPE* l = calloc(nfrm, sizeof(FP_TYPE));
  for(int i = 0; i < nfrm; i ++) {
    int center = (i + 2) * thop * fs;
    FP_TYPE* xfrm = fetch_frame(x2, nx, center, framesize);
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

  loudness* ret = malloc(sizeof(loudness));
  ret -> loudness_total = power_count > 0 ?
    -0.691 + 10.0 * log10(power_sum / power_count) : -70.0;
  ret -> loudness_inst = l;
  ret -> size = nfrm;
  ret -> interval = thop;
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

int main_compress(int use_lkfs) {
  int nfrm = nx / nhop;
  FP_TYPE* w = hanning(nhop * 2);
  loudness* loudness_measure = NULL;
  if(use_lkfs)
    loudness_measure = lkfs_loudness(x, nx, fs, (FP_TYPE)nhop / fs);
  
  FP_TYPE* y = calloc(nx, sizeof(FP_TYPE));
  for(int i = 0; i < nfrm; i ++) {
    int center = i * nhop;
    FP_TYPE* xfrm = fetch_frame(x, nx, center, nhop * 2);
    FP_TYPE level = 0;
    FP_TYPE gain = 0;
    FP_TYPE threshold = op.compression_threshold;
    if(loudness_measure != NULL) {
      int loudness_idx = min(max(0, i - 2), loudness_measure -> size - 1);
      level = loudness_measure -> loudness_inst[loudness_idx];
    } else {
      for(int j = 0; j < nhop * 2; j ++)
        level = fmax(level, fabs(xfrm[j]));
    }
    if(level > threshold) {
      gain = (threshold - level) * (1.0 - op.compression_damping);
      if(loudness_measure != NULL) {
        gain = db2mag(gain) - 1.0;
      } else {
        gain = gain / (level + M_EPS);
      }
    }
    for(int j = 0; j < nhop * 2; j ++) {
      int idx = center + j - nhop;
      if(idx >= 0 && idx < nx)
        y[idx] += xfrm[j] * w[j] * gain;
    }
    free(xfrm);
  }
  for(int i = 0; i < nx; i ++)
    x[i] += y[i];
  if(loudness_measure != NULL)
    delete_loudness(loudness_measure);
  free(y);
  free(w);
  return 1;
}

int main_normalize_abs() {
  FP_TYPE absmax = 0;
  for(int i = 0; i < nx; i ++)
    absmax = fmax(absmax, fabs(x[i]));
  FP_TYPE gain = op.normalization_max / absmax;
  for(int i = 0; i < nx; i ++)
    x[i] *= gain;
  return 1;
}

int main_normalize_lkfs() {
  loudness* measure = lkfs_loudness(x, nx, fs, 0.1);
  FP_TYPE gain = db2mag(op.normalization_max - measure -> loudness_total);
  for(int i = 0; i < nx; i ++) x[i] *= gain;
  delete_loudness(measure);
  return 1;
}

int main_lkfs(FP_TYPE thop) {
  loudness* measure = lkfs_loudness(x, nx, fs, thop);
  printf("Total = %f LKFS\n", measure -> loudness_total);
  for(int i = 0; i < measure -> size; i ++) {
    FP_TYPE t = (FP_TYPE)(i + 2) * measure -> interval;
    printf("%f, %f LKFS\n", t, measure -> loudness_inst[i]);
  }
  delete_loudness(measure);
  return 1;
}

int main_inverse_filter(FP_TYPE thop) {
  int window_size = round(op.window_size * fs);
  int order = op.order;
  int nfrm = (FP_TYPE)nx / fs / thop;
  FP_TYPE* y = calloc(nx, sizeof(FP_TYPE));
  FP_TYPE* xfrm = calloc(window_size + order, sizeof(FP_TYPE));
  FP_TYPE* yfrm = calloc(window_size, sizeof(FP_TYPE));
  FP_TYPE* w = hanning(window_size);
  FP_TYPE normalize_factor = 0;
  for(int i = 0; i < window_size; i += thop * fs)
    normalize_factor += w[i];

  for(int i = 0; i < nfrm; i ++) {
    int center = floor(i * thop * fs);
    int left = center - window_size / 2 - order;
    for(int j = 0; j < window_size + order; j ++) {
      int idx = left + j;
      xfrm[j] = (idx >= 0 && idx < nx) ? x[idx] : 0.0;
      xfrm[j] += randu() * 1e-8;
    }
    FP_TYPE* a = lpc(xfrm, window_size + order, order, NULL);
    for(int j = 0; j < window_size; j ++) {
      yfrm[j] = xfrm[j + order];
      for(int k = 1; k <= order; k ++)
        yfrm[j] += xfrm[j + order - k] * a[k];
      yfrm[j] *= w[j] / normalize_factor;
      int idx = center - window_size / 2 + j;
      if(idx >= 0 && idx < nx)
        y[idx] += yfrm[j];
    }
    free(a);
  }
  free(xfrm); free(yfrm); free(w);
  free(x); x = y;
  return 1;
}

int main_detect_threshold() {
  FP_TYPE threshold = op.normalization_max;
  int idx_begin = 0;
  int hold = 0;
  for(int i = 0; i < nx; i ++) {
    if(fabs(x[i]) >= threshold) {
      if(! hold) {
        idx_begin = i;
        hold = 1;
      }
    } else {
      if(hold) {
        printf("%f\t%f\n", (FP_TYPE)idx_begin / fs, (FP_TYPE)(i - 1) / fs);
        hold = 0;
      }
    }
  }
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
      if(! main_compress(0)) return 0;
    } else
    if(op.type == OPERATION_COMPRESS_LKFS) {
      nhop = round(fs * 0.1);
      if(specified_thop != 0) nhop = round(specified_thop * fs);
      if(! main_compress(1)) return 0;
    } else
    if(op.type == OPERATION_NORMALIZE_ABS) {
      if(! main_normalize_abs()) return 0;
    } else
    if(op.type == OPERATION_NORMALIZE_LKFS) {
      if(! main_normalize_lkfs()) return 0;
    } else
    if(op.type == OPERATION_MEASURE_LKFS) {
      FP_TYPE thop = 0.1;
      if(specified_thop != 0) thop = specified_thop;
      if(! main_lkfs(thop)) return 0;
    } else
    if(op.type == OPERATION_INVERSE_FILER) {
      FP_TYPE thop = op.window_size / 4;
      if(specified_thop != 0) thop = specified_thop;
      if(! main_inverse_filter(thop)) return 0;
    } else
    if(op.type == OPERATION_DETECT_THOLD) {
      if(! main_detect_threshold()) return 0;
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
  while((c = getopt(argc, argv, "a:d:r:s:c:n:I:lt:i:h")) != -1) {
    int i = 0;
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
      i ++;
      while(optarg[i] != 0) {
        if(optarg[i] == ',') {
          if(! strcmp(optarg + i + 1, "k"))
            top_op.type = OPERATION_COMPRESS_LKFS;
          break;
        }
        i ++;
      }
      operation_chain[num_operation ++] = top_op;
    break;
    case 'n':
      top_op.type = OPERATION_NORMALIZE_ABS;
      while(optarg[i] != 0) {
        if(optarg[i] == ',') {
          if(! strcmp(optarg + i + 1, "k"))
            top_op.type = OPERATION_NORMALIZE_LKFS;
          optarg[i] = 0;
          break;
        }
        i ++;
      }
      top_op.normalization_max = atof(optarg);
      operation_chain[num_operation ++] = top_op;
    break;
    case 'I':
      while(optarg[i] != ',') {
        if(optarg[i] == 0) {
          fprintf(stderr, "-I option requires two comma-separated "
            "parameters.\n");
          exit(1);
        }
        i ++;
      }
      optarg[i]= 0;
      top_op.type = OPERATION_INVERSE_FILER;
      top_op.order = atoi(optarg);
      top_op.window_size = atof(optarg + i + 1);
      operation_chain[num_operation ++] = top_op;
    break;
    case 'l':
      top_op.type = OPERATION_MEASURE_LKFS;
      operation_chain[num_operation ++] = top_op;
    break;
    case 't':
      top_op.type = OPERATION_DETECT_THOLD;
      top_op.normalization_max = atof(optarg);
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
