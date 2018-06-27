/*
 * Copyright (C) 2018 Arm Limited or its affiliates. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the License); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Description: MFCC feature extraction to match with TensorFlow MFCC Op
 */

#include <string.h>

#include "mfcc-official.h"
#include "float.h"


void FFT(const unsigned long & fftlen, vector<complex<float> >& vec)
{
  unsigned long ulPower = 0;
  unsigned long fftlen1 = fftlen - 1;
  while(fftlen1 > 0)
  {
    ulPower++;
    fftlen1=fftlen1/2;
  }


  bitset<sizeof(unsigned long) * 8> bsIndex;
  unsigned long ulIndex;
  unsigned long ulK;
  for(unsigned long p = 0; p < fftlen; p++)
  {
    ulIndex = 0;
    ulK = 1;
    bsIndex = bitset<sizeof(unsigned long) * 8>(p);
    for(unsigned long j = 0; j < ulPower; j++)
      {
        ulIndex += bsIndex.test(ulPower - j - 1) ? ulK : 0;
        ulK *= 2;
      }

    if(ulIndex > p)
      {
        complex<float> c = vec[p];
        vec[p] = vec[ulIndex];
        vec[ulIndex] = c;
      }
  }


  vector<complex<float> > vecW;
  for(unsigned long i = 0; i < fftlen / 2; i++)
    {
      vecW.push_back(complex<float>(cos(M_2PI * i / fftlen) , -1 * sin(M_2PI * i / fftlen)));
    }



  unsigned long ulGroupLength = 1;
  unsigned long ulHalfLength = 0;
  unsigned long ulGroupCount = 0;
  complex<float> cw;
  complex<float> c1;
  complex<float> c2;
  for(unsigned long b = 0; b < ulPower; b++)
    {
      ulHalfLength = ulGroupLength;
      ulGroupLength *= 2;
      for(unsigned long j = 0; j < fftlen; j += ulGroupLength)
        {
          for(unsigned long k = 0; k < ulHalfLength; k++)
            {
              cw = vecW[k * fftlen / ulGroupLength] * vec[j + k + ulHalfLength];
              c1 = vec[j + k] + cw;
              c2 = vec[j + k] - cw;
              vec[j + k] = c1;
              vec[j + k + ulHalfLength] = c2;
            }
        }
    }
}

MFCC::MFCC(int num_mfcc_features, int frame_len, int mfcc_dec_bits)
:num_mfcc_features(num_mfcc_features),
 frame_len(frame_len),
 mfcc_dec_bits(mfcc_dec_bits)
{

  // Round-up to nearest power of 2.
  filter_len = pow(2,ceil((log(frame_len)/log(2))));

  frame = new float[filter_len];
  buffer = new float[filter_len];
  mel_energies = new float[NUM_FBANK_BINS];

  //create window function
  window_func = new float[frame_len];
  for (int i = 0; i < frame_len; i++)
    window_func[i] = 0.5 - 0.5*cos(M_2PI * ((float)i) / (frame_len));

  //create mel filterbank
  fbank_filter_first = new int32_t[NUM_FBANK_BINS];
  fbank_filter_last = new int32_t[NUM_FBANK_BINS];;
  mel_fbank = create_mel_fbank();

  //create DCT matrix
  dct_matrix = create_dct_matrix(NUM_FBANK_BINS, num_mfcc_features);

  //initialize FFT

}

MFCC::~MFCC() {
  delete []frame;
  delete [] buffer;
  delete []mel_energies;
  delete []window_func;
  delete []fbank_filter_first;
  delete []fbank_filter_last;
  delete []dct_matrix;
  for(int i=0;i<NUM_FBANK_BINS;i++)
    delete mel_fbank[i];
  delete mel_fbank;
}

float * MFCC::create_dct_matrix(int32_t input_length, int32_t coefficient_count) {
  int32_t k, n;
  float * M = new float[input_length*coefficient_count];
  float normalizer = sqrt(2.0/(float)input_length);
  for (k = 0; k < coefficient_count; k++) {
    for (n = 0; n < input_length; n++) {
      M[k*input_length+n] = normalizer * cos( ((double)M_PI)/input_length * (n + 0.5) * k );
    }
  }
  return M;
}

float ** MFCC::create_mel_fbank() {

  int32_t bin, i;

  int32_t num_fft_bins = filter_len/2;
  float fft_bin_width = ((float)SAMP_FREQ) / filter_len;
  float mel_low_freq = MelScale(MEL_LOW_FREQ);
  float mel_high_freq = MelScale(MEL_HIGH_FREQ);
  float mel_freq_delta = (mel_high_freq - mel_low_freq) / (NUM_FBANK_BINS+1);

  float *this_bin = new float[num_fft_bins];

  float ** mel_fbank =  new float*[NUM_FBANK_BINS];

  for (bin = 0; bin < NUM_FBANK_BINS; bin++) {

    float left_mel = mel_low_freq + bin * mel_freq_delta;
    float center_mel = mel_low_freq + (bin + 1) * mel_freq_delta;
    float right_mel = mel_low_freq + (bin + 2) * mel_freq_delta;

    int32_t first_index = -1, last_index = -1;

    for (i = 0; i < num_fft_bins; i++) {

      float freq = (fft_bin_width * i);  // center freq of this fft bin.
      float mel = MelScale(freq);
      this_bin[i] = 0.0;

      if (mel > left_mel && mel < right_mel) {
        float weight;
        if (mel <= center_mel) {
          weight = (mel - left_mel) / (center_mel - left_mel);
        } else {
          weight = (right_mel-mel) / (right_mel-center_mel);
        }
        this_bin[i] = weight;
        if (first_index == -1)
          first_index = i;
        last_index = i;
      }
    }

    fbank_filter_first[bin] = first_index;
    fbank_filter_last[bin] = last_index;
    mel_fbank[bin] = new float[last_index-first_index+1];

    int32_t j = 0;
    //copy the part we care about
    for (i = first_index; i <= last_index; i++) {
      mel_fbank[bin][j++] = this_bin[i];
    }
  }
  delete []this_bin;
  return mel_fbank;
}

void MFCC::mfcc_compute(const int16_t * audio_data, float* mfcc_out) {

  int32_t i, j, bin;

  //TensorFlow way of normalizing .wav data to (-1,1)
  for (i = 0; i < frame_len; i++) {
    frame[i] = (float)audio_data[i]/(1<<15);
  }
  //Fill up remaining with zeros
  memset(&frame[frame_len], 0, sizeof(float) * (filter_len-frame_len));

  for (i = 0; i < frame_len; i++) {
    frame[i] *= window_func[i];
  }

  //Compute FFT
  // arm_rfft_fast_f32(rfft, frame, buffer, 0);
  vector<complex<float> > zero_padded;
  zero_padded.assign(frame, frame+frame_len);
  zero_padded.resize(filter_len, 0);
  FFT(filter_len, zero_padded);
  //Convert to power spectrum
  //frame is stored as [real0, realN/2-1, real1, im1, real2, im2, ...]
  int32_t half_dim = filter_len/2;
  float first_energy = zero_padded[0].real() * zero_padded[0].real(),
        last_energy =  zero_padded[half_dim-1].real() * zero_padded[half_dim-1].real();  // handle this special case
  for (i = 1; i < half_dim; i++) {
    float real = zero_padded[i].real(), im = zero_padded[i].imag();
    buffer[i] = real*real + im*im;
  }
  buffer[0] = first_energy;
  buffer[half_dim] = last_energy;

  float sqrt_data;
  //Apply mel filterbanks
  for (bin = 0; bin < NUM_FBANK_BINS; bin++) {
    j = 0;
    float mel_energy = 0;
    int32_t first_index = fbank_filter_first[bin];
    int32_t last_index = fbank_filter_last[bin];
    for (i = first_index; i <= last_index; i++) {
      sqrt_data = sqrt(buffer[i]);
      mel_energy += (sqrt_data) * mel_fbank[bin][j++];
    }
    mel_energies[bin] = mel_energy;

    //avoid log of zero
    if (mel_energy == 0.0)
      mel_energies[bin] = FLT_MIN;
  }
  //Take log
  for (bin = 0; bin < NUM_FBANK_BINS; bin++)
    mel_energies[bin] = logf(mel_energies[bin]);
  //Take DCT. Uses matrix mul.
  for (i = 0; i < num_mfcc_features; i++) {
    float sum = 0.0;
    for (j = 0; j < NUM_FBANK_BINS; j++) {
      sum += dct_matrix[i*NUM_FBANK_BINS+j] * mel_energies[j];
    }
    mfcc_out[i] = sum;
    //Input is Qx.mfcc_dec_bits (from quantization step)
    // sum *= (0x1<<mfcc_dec_bits);
    // sum = round(sum);
    // if(sum >= 127)
    //   mfcc_out[i] = 127;
    // else if(sum <= -128)
    //   mfcc_out[i] = -128;
    // else
    //   mfcc_out[i] = sum;
  }

}

