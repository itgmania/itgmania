/*
 * This implements audio resampling, using the method described at:
 *  http://www.dspguru.com/info/faqs/mrfaq.htm
 *
 * Each conversion ratio uses some memory, but the resulting table is
 * shared, so the memory overhead per stream is negligible.
 */
#include "RageSoundReader_Resample_Good.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <numeric>
#include <string>
#include <utility>

#include "RageSoundReader.h"
#include "RageSoundReader_Filter.h"
#include "RageThreads.h"
#include "RageUtil.h"
#include "global.h"

constexpr int kFilterLength = 8;  // This must be a power of 2.

namespace {
float sincf(float f) {
  if (f == 0) {
    return 1;
  }
  return std::sin(f) / f;
}

/* Modified Bessel function I0.  From Abramowitz and Stegun "Handbook of
 * Mathematical Functions", "Modified Bessel Functions I and K". */
float BesselI0(float x) {
  constexpr float kCoeffs1[] = {3.5156229f, 3.0899424f, 1.2067492f,
                                0.2659732f, 0.0360768f, 0.0045813f};
  constexpr float kCoeffs2[] = {0.39894228f,  0.01328592f,  0.00225319f,
                                -0.00157565f, 0.00916281f,  -0.02057706f,
                                0.02635537f,  -0.01647633f, 0.00392377f};
  float abs_x = std::abs(x);
  if (abs_x < 3.75f) {
    float y = (x / 3.75f) * (x / 3.75f);
    float ret = 1.0f;
    for (float coeff : kCoeffs1) {
      ret += coeff * y;
      y *= y;
    }
    return ret;
  } else {
    float y = 3.75f / abs_x;
    float ret = std::exp(abs_x) / std::sqrt(abs_x);
    for (float coeff : kCoeffs2) {
      ret += coeff * y;
      y *= y;
    }
    return ret;
  }
}

/*
 * Kaiser window:
 *
 * K(n) = I0( B*sqrt(1-(n/p)^2) )
 *        -----------------------
 *                 I0(B)
 *
 * where B is the beta parameter, p is len/2, and n is in [-len/2,+len/2].
 */
void ApplyKaiserWindow(float* buf, int len, float beta) {
  constexpr float kVeryTinyNumber =
      0.0000001f;  // a little safer than comparing against zero
  const float denom = BesselI0(beta);
  const float p = (len - 1) / 2.0f;
  for (int n = 0; n < len; ++n) {
    const float n1 = std::abs((n - p) * (1.0f / p));
    const float n1_squared = n1 * n1;
    const float num = BesselI0(
        beta * std::sqrt(std::max(1.0f - n1_squared, kVeryTinyNumber)));
    buf[n] *= num / denom;
  }
}

void MultiplyVector(float* start, float* end, float f) {
  for (; start != end; ++start) {
    *start *= f;
  }
}

void GenerateSincLowPassFilter(float* fir, int win_size, float cutoff) {
  constexpr double kTwopi = 2 * 3.141592653589793;
  const float p = (win_size - 1) / 2.0;

  for (int n = 0; n < win_size; ++n) {
    const float n1 = (n - p);
    const float val = sincf((kTwopi * cutoff * n1)) * (2.0f * cutoff);
    fir[n] = val;
  }
}

void NormalizeVector(float* buf, int size) {
  float total = std::accumulate(&buf[0], &buf[size], 0.0f);
  MultiplyVector(&buf[0], &buf[size], 1 / total);
}

int GCD(int i1, int i2) { return std::gcd(i1, i2); }
}  // namespace

template <typename T>
class AlignedBuffer {
 public:
  AlignedBuffer(int size) {
    size_ = size;
    buf_ = new T[size_];
  }

  AlignedBuffer(const AlignedBuffer& cpy) {
    size_ = cpy.size_;
    buf_ = new T[size_];
    memcpy(buf_, cpy.buf_, sizeof(T) * size_);
  }
  ~AlignedBuffer() { delete[] buf_; }
  operator T*() { return buf_; }
  operator const T*() const { return buf_; }

 private:
  T& operator=(T& rhs);
  int size_;
  T* buf_;
};

struct PolyphaseFilter {
  struct State {
    State(int up_factor) : buf_(kFilterLength * 2) {
      poly_index_ = up_factor - 1;
      filled_ = 0;
      buf_next_ = 0;
    }

    int poly_index_;
    int filled_;

    /* This buffer is duplicated.  If the circular buffer is size L, the actual
     * buffer is size L*2, and data at buf[N] is also at buf[N+L].  That way, we
     * can access up to buf[N*2-1] without having to wrap. */
    AlignedBuffer<float> buf_;
    int buf_next_;
  };
  friend struct State;

  PolyphaseFilter(int up_factor) : polyphase_(kFilterLength * up_factor) {
    up_factor_ = up_factor;
  }

  void Generate(const float* fir);
  int RunPolyphaseFilter(
      State& state, const float* in, int samples_in, int down_factor,
      float* out, int samples_out, int sample_stride) const;
  int GetLatency() const { return kFilterLength / 2; }

  int NumInputsForOutputSamples(
      const State& state, int out, int down_factor) const;

 private:
  AlignedBuffer<float> polyphase_;
  int up_factor_;
};

/*
 * Convert an FIR filter to a polyphase filter.
 *
 * pFIR is the input FIR filter, which has iL*iUpFactor values.
 * iL is the number of real samples each output sample looks at.
 * iUpFactor is the actual upsampling factor; the amount of zero-stuffing
 * between each real sample. pOutput is the 2D output polyphase filter, with
 * iL*iL values.
 *
 * With an upsampling factor (iUpFactor) of 3, and a sinc filter length of 12
 * (iL*iUpFactor),
 *
 * input     first output sample (before decimation)
 * sample          second output sample
 *                     third output sample
 *
 * 0         0
 * 0         1     0
 * 1592      2     1   0
 * 0         3     2   1
 * 0         4     3   2
 * 1623      5     4   3
 * 0         6     5   4
 * 0         7     6   5
 * 1682      8     7   6
 * 0         9     8   7
 * 0         10    9   8
 * 1730      11    10  9
 * 0               11  10
 * 0                   11
 *
 * first row: 2, 5, 8, 11
 * second: 1, 4, 7, 10
 * third: 0, 3, 6, 9
 * Read a new sample after passing the last line.
 */
void PolyphaseFilter::Generate(const float* fir) {
  float* output = polyphase_;
  const int input_size = kFilterLength * up_factor_;

  for (int row = 0; row < up_factor_; ++row) {
    int input_offset = (up_factor_ - row - 1) % up_factor_;
    for (int col = 0; col < kFilterLength; ++col) {
      *output = fir[input_offset];
      ++output;
      input_offset += up_factor_;
      input_offset %= input_size;
    }
  }
}

/*
 * We only want one boundary check when running the filter; either on the
 * number of inputs used, or the number of outputs produced.  Otherwise, we'll
 * have to maintain two counters, and check two values per iteration.
 *
 * First, call NumInputsForOutputSamples(out), to find out how many inputs to
 * supply to get the desired number of outputs.  Then, pass the data, the input
 * count and the output count to RunPolyphaseFilter.
 *
 * - When downsampling, we use the number of inputs as the boundary.  For
 * example, if the ratio is 1:3 (downsample x3), and the user gives us 10
 * samples, then we process until we've consumed all of the input.  (This will
 * result in exactly the number of samples the user asked for with
 * NumInputsForOutputSamples.)
 *
 * - When upsampling, we use the number of outputs as the boundary.  For
 * example, if the ratio is 3:1 (upsample x3), and the user wants 8 samples to
 * be output, we'll have been given 3 samples as input.  Process until we've
 * produced 8 samples.
 *
 * In both cases, we have overlap.  In the first, it's possible that we could
 * have consumed an additional input without producing an output.  In the
 * second, it's possible that we could have produced an additional output
 * without consuming an input.
 */
int PolyphaseFilter::RunPolyphaseFilter(
    State& state, const float* in, int samples_in, int down_factor, float* out,
    int samples_out, int sample_stride) const {
  ASSERT(samples_in >= 0);

  float* out_orig = out;
  const float* in_end = in + samples_in * sample_stride;
  const float* out_end = out + samples_out * sample_stride;

  int filled = state.filled_;
  int poly_index = state.poly_index_;
  while (out != out_end) {
    if (filled < kFilterLength) {
      if (in == in_end) {
        break;
      }

      state.buf_[state.buf_next_] = *in;
      state.buf_[state.buf_next_ + kFilterLength] = *in;
      ++state.buf_next_;
      state.buf_next_ &= kFilterLength - 1;

      in += sample_stride;
      ++filled;
      continue;
    }

    while (out != out_end) {
      const float* cur_poly = &polyphase_[poly_index * kFilterLength];
      const float* in_data = &state.buf_[state.buf_next_];

      float tot = 0;
      for (int j = 0; j < kFilterLength; ++j) {
        tot += in_data[j] * cur_poly[j];
      }
      *out = tot;
      out += sample_stride;

      poly_index += down_factor;
      if (poly_index >= up_factor_) {
        break;
      }
    }
    filled -= poly_index / up_factor_;
    poly_index %= up_factor_;
  }

  state.filled_ = filled;
  state.poly_index_ = poly_index;

  int ret_samples = out - out_orig;
  int ret_frames = ret_samples / sample_stride;
  return ret_frames;
}

/*
 * Return the number of input samples needed to produce the given number of
 * output samples.  This is dependent on the number of bytes in the buffer and
 * the current position of the stream.
 */
int PolyphaseFilter::NumInputsForOutputSamples(
    const State& state, int out, int down_factor) const {
  int in = 0;
  int filled = state.filled_;
  int poly_index = state.poly_index_;

#if 0
	while( iOut > 0 )
	{
		if( iFilled < FILTER_LENGTH )
		{
			int iToFill = FILTER_LENGTH-iFilled;
			iIn += iToFill;
			iFilled += iToFill;
		}

		while( iFilled == FILTER_LENGTH && iOut )
		{
			--iOut;
			iPolyIndex += iDownFactor;

			if( iPolyIndex >= up_factor_ )
				break;
		}
		iFilled -= iPolyIndex/up_factor_;
		iPolyIndex %= up_factor_;
	}
#endif

  if (out > 0) {
    if (filled < kFilterLength) {
      int to_fill = kFilterLength - filled;
      in += to_fill;
    }

    // The -1 here is because we don't refill m_fBuf after writing the last
    // output.
    poly_index += down_factor * (out - 1);
    in += poly_index / up_factor_;
  }

  return in;
}
/** @brief Utilities for working with the PolyphaseFilter cache. */
namespace PolyphaseFilterCache {
/* Cache filter data, and reuse it without copying.  All operations after
 * creation are const, so this doesn't cause thread-safety problems. */
typedef std::map<std::pair<int, float>, PolyphaseFilter*> FilterMap;
static RageMutex polyphase_filters_lock("PolyphaseFiltersLock");
static FilterMap map_polyphase_filters;

const PolyphaseFilter* MakePolyphaseFilter(
    int up_factor, float cutoff_frequency) {
  polyphase_filters_lock.Lock();
  std::pair<int, float> params(std::make_pair(up_factor, cutoff_frequency));
  FilterMap::const_iterator it = map_polyphase_filters.find(params);
  if (it != map_polyphase_filters.end()) {
    /* We already have a filter for this upsampling factor and cutoff; use it.
     */
    PolyphaseFilter* polyphase = it->second;
    polyphase_filters_lock.Unlock();
    return polyphase;
  }
  int win_size = kFilterLength * up_factor;
  float* fir = new float[win_size];
  GenerateSincLowPassFilter(fir, win_size, cutoff_frequency);
  ApplyKaiserWindow(fir, win_size, 8);
  NormalizeVector(fir, win_size);
  MultiplyVector(&fir[0], &fir[win_size], (float)up_factor);

  PolyphaseFilter* polyphase = new PolyphaseFilter(up_factor);
  polyphase->Generate(fir);
  delete[] fir;

  map_polyphase_filters[params] = polyphase;
  polyphase_filters_lock.Unlock();
  return polyphase;
}

const PolyphaseFilter* FindNearestPolyphaseFilter(
    int up_factor, float cutoff_frequency) {
  /* Find a cached filter with the same iUpFactor and a nearby cutoff frequency.
   * Round the cutoff down, if possible; it's better to filter out too much than
   * too little. */
  polyphase_filters_lock.Lock();
  std::pair<int, float> params(
      std::make_pair(up_factor, cutoff_frequency + 0.0001f));
  FilterMap::const_iterator it = map_polyphase_filters.upper_bound(params);
  if (it != map_polyphase_filters.begin()) {
    --it;
  }
  ASSERT(it->first.first == up_factor);
  PolyphaseFilter* polyphase = it->second;
  polyphase_filters_lock.Unlock();
  return polyphase;
}
}  // namespace PolyphaseFilterCache

/*
 * Interface to PolyphaseFilter, providing a simple resampling interface.  This
 * handles reuse of PolyphaseFilters.  This does not handle delay, flushing, or
 * multiple channels.
 */
class RageSoundResampler_Polyphase {
 public:
  /* Note that going outside of [iMinDownFactor,iMaxDownFactor] while resampling
   * isn't fatal.  It'll only cause aliasing, by not having a LPF that's low
   * enough, or cause too much filtering, by not having a LPF that's high
   * enough. */
  RageSoundResampler_Polyphase(
      int up_factor, int min_down_factor, int max_down_factor) {
    /* Cache filters between iMinDownFactor and iMaxDownFactor.  Do them in
     * iFilterIncrement increments; we'll round down to the closest match
     * when filtering.  This will only cause the low-pass filter to be rounded;
     * the conversion ratio will always be exact. */
    up_factor_ = up_factor;
    polyphase_ = nullptr;

    int filter_increment =
        std::max((max_down_factor - min_down_factor) / 10, 1);
    for (int down_factor = min_down_factor; down_factor <= max_down_factor;
         down_factor += filter_increment) {
      float cutoff_frequency = GetCutoffFrequency(down_factor);
      PolyphaseFilterCache::MakePolyphaseFilter(up_factor_, cutoff_frequency);
    }

    SetDownFactor(up_factor);

    state_ = new PolyphaseFilter::State(up_factor);
  }

  ~RageSoundResampler_Polyphase() { delete state_; }

  void SetDownFactor(int down_factor) {
    down_factor_ = down_factor;
    polyphase_ = GetFilter(down_factor_);
  }

  int Run(
      const float* in, int samples_in, float* out, int samples_out,
      int sample_stride) const {
    return polyphase_->RunPolyphaseFilter(
        *state_, in, samples_in, down_factor_, out, samples_out, sample_stride);
  }

  void Reset() {
    delete state_;
    state_ = new PolyphaseFilter::State(up_factor_);
  }

  int NumInputsForOutputSamples(int out) const {
    return polyphase_->NumInputsForOutputSamples(*state_, out, down_factor_);
  }
  int GetLatency() const { return polyphase_->GetLatency(); }
  int GetFilled() const { return state_->filled_; }

  RageSoundResampler_Polyphase(const RageSoundResampler_Polyphase& cpy) {
    polyphase_ = cpy.polyphase_;  // don't copy
    state_ = new PolyphaseFilter::State(*cpy.state_);
    up_factor_ = cpy.up_factor_;
    down_factor_ = cpy.down_factor_;
  }

 private:
  float GetCutoffFrequency(int down_factor) const {
    /*
     * If we're upsampling, we want the low-pass filter to cut off at the
     * nyquist frequency of the original sample.
     *
     * If we're downsampling, we want the low-pass filter to cut off at the
     * nyquist frequency of the new sample.
     */

    float cutoff_frequency;
    cutoff_frequency = 1.0f / (2 * up_factor_);
    cutoff_frequency = std::min(cutoff_frequency, 1.0f / (2 * down_factor));
    return cutoff_frequency;
  }

  const PolyphaseFilter* GetFilter(int down_factor) const {
    float cutoff_frequency = GetCutoffFrequency(down_factor);
    return PolyphaseFilterCache::FindNearestPolyphaseFilter(
        up_factor_, cutoff_frequency);
  }

  const PolyphaseFilter* polyphase_;
  PolyphaseFilter::State* state_;
  int up_factor_;
  int down_factor_;
};

int RageSoundReader_Resample_Good::GetNextSourceFrame() const {
  int64_t position = source_->GetNextSourceFrame();
  position -= resamplers_[0]->GetFilled();

  position *= sample_rate_;
  position /= source_->GetSampleRate();
  return (int)position;
}

bool RageSoundReader_Resample_Good::SetProperty(
    const std::string& property, float value) {
  if (property == "Rate") {
    SetRate(value);
    return true;
  }

  return source_->SetProperty(property, value);
}

float RageSoundReader_Resample_Good::GetStreamToSourceRatio() const {
  float ratio = source_->GetStreamToSourceRatio();
  if (rate_ != -1) {
    ratio *= rate_;
  }
  return ratio;
}

RageSoundReader_Resample_Good::RageSoundReader_Resample_Good(
    RageSoundReader* source, int sample_rate)
    : RageSoundReader_Filter(source) {
  sample_rate_ = sample_rate;
  rate_ = -1;
  ReopenResampler();
}

/* Call this if the input position is changed or reset. */
void RageSoundReader_Resample_Good::Reset() {
  for (size_t channel = 0; channel < source_->GetNumChannels(); ++channel) {
    resamplers_[channel]->Reset();
  }
}

void RageSoundReader_Resample_Good::GetFactors(
    int& down_factor, int& up_factor) const {
  down_factor = source_->GetSampleRate();
  up_factor = sample_rate_;

  {
    int gcd = GCD(up_factor, down_factor);
    up_factor /= gcd;
    down_factor /= gcd;
  }

  bool rate_changing_enabled = rate_ != -1;
  if (rate_changing_enabled) {
    up_factor *= 100;
    down_factor *= 100;
  }
}

/* Call this if the sample factor changes. */
void RageSoundReader_Resample_Good::ReopenResampler() {
  for (size_t channel = 0; channel < resamplers_.size(); ++channel) {
    delete resamplers_[channel];
  }
  resamplers_.clear();

  int down_factor, up_factor;
  GetFactors(down_factor, up_factor);

  for (size_t channel = 0; channel < source_->GetNumChannels(); ++channel) {
    int min_down_factor = down_factor;
    int max_down_factor = down_factor;
    if (rate_ != -1) {
      max_down_factor *= 5;
    }

    RageSoundResampler_Polyphase* p = new RageSoundResampler_Polyphase(
        up_factor, min_down_factor, max_down_factor);
    resamplers_.push_back(p);
  }

  if (rate_ != -1) {
    down_factor = static_cast<int>((rate_ * down_factor) + 0.5);
  }

  for (size_t channel = 0; channel < resamplers_.size(); ++channel) {
    resamplers_[channel]->SetDownFactor(down_factor);
  }
}

RageSoundReader_Resample_Good::~RageSoundReader_Resample_Good() {
  for (size_t channel = 0; channel < resamplers_.size(); ++channel) {
    delete resamplers_[channel];
  }
}

/* iFrame is in the destination rate.  Seek the source in its own sample rate.
 */
int RageSoundReader_Resample_Good::SetPosition(int frame) {
  Reset();
  frame = (int)SCALE(
      frame, 0, (int64_t)sample_rate_, 0, (int64_t)source_->GetSampleRate());
  return source_->SetPosition(frame);
}

int RageSoundReader_Resample_Good::Read(float* buf, int frames) {
  int channels = resamplers_.size();

  int frames_read = 0;

  /* If the ratio is 1:1, then we're effectively disabled, and we can read
   * directly into the buffer. */
  int down_factor, up_factor;
  GetFactors(down_factor, up_factor);

  if (resamplers_[0]->GetFilled() == 0 && down_factor == up_factor &&
      GetRate() == 1.0f) {
    return source_->Read(buf, frames);
  }

  {
    int frames_needed = resamplers_[0]->NumInputsForOutputSamples(frames);
    float* tmp_buf = (float*)alloca(frames_needed * sizeof(float) * channels);
    int frames_in = source_->Read(tmp_buf, frames_needed);
    if (frames_in < 0) {
      return frames_in;
    }

    for (int channel = 0; channel < channels; ++channel) {
      int got_frames = resamplers_[channel]->Run(
          tmp_buf + channel, frames_in, buf + channel, frames, channels);
      ASSERT(got_frames <= frames);

      if (channel == 0) {
        frames_read += got_frames;
      }
    }
  }

  return frames_read;
}

/*
 * A resampler is commonly used for two things: to change the sample rate of
 * audio, in order to give an audio driver what it wants (SetSampleRate), and to
 * change the sound of audio, changing its speed and pitch (SetRate).  These are
 * the same operation, and we do both in the same pass; the only difference is
 * that SetSampleRate causes GetSampleRate() to change, while SetRate() causes
 * GetStreamToSourceRatio() to change.
 *
 * Changing these values will take effect immediately, with a buffering latency
 * of L/4 frames.
 */
void RageSoundReader_Resample_Good::SetRate(float ratio) {
  ASSERT(ratio > 0);
  bool rate_changing_was_enabled = rate_ != -1;
  rate_ = ratio;

  if (!rate_changing_was_enabled) {
    ReopenResampler();
  }

  int down_factor, up_factor;
  GetFactors(down_factor, up_factor);
  if (rate_ != -1) {
    down_factor = static_cast<int>((rate_ * down_factor) + 0.5);
  }

  /* Set m_fRate to the actual rate, after quantization by iUpFactor. */
  rate_ = float(down_factor) / up_factor;

  for (size_t channel = 0; channel < resamplers_.size(); ++channel) {
    resamplers_[channel]->SetDownFactor(down_factor);
  }
}

float RageSoundReader_Resample_Good::GetRate() const {
  if (rate_ == -1) {
    return 1.0f;
  } else {
    return rate_;
  }
}

RageSoundReader_Resample_Good::RageSoundReader_Resample_Good(
    const RageSoundReader_Resample_Good& cpy)
    : RageSoundReader_Filter(cpy) {
  for (size_t i = 0; i < cpy.resamplers_.size(); ++i) {
    this->resamplers_.push_back(
        new RageSoundResampler_Polyphase(*cpy.resamplers_[i]));
  }
  this->sample_rate_ = cpy.sample_rate_;
  this->rate_ = cpy.rate_;
}

RageSoundReader_Resample_Good* RageSoundReader_Resample_Good::Copy() const {
  return new RageSoundReader_Resample_Good(*this);
}

/*
 * (c) 2006 Glenn Maynard
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, provided that the above
 * copyright notice(s) and this permission notice appear in all copies of
 * the Software and that both the above copyright notice(s) and this
 * permission notice appear in supporting documentation.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
 * INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
