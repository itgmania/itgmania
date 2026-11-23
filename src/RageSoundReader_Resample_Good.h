/* RageSoundReader_Resample_Good - libsamplerate wrapper. */

#ifndef RAGE_SOUND_READER_RESAMPLE_GOOD_H
#define RAGE_SOUND_READER_RESAMPLE_GOOD_H

#include <string>
#include <vector>

#include "RageSoundReader.h"
#include "RageSoundReader_Filter.h"

// Forward declaration of libsamplerate's opaque state, so that including this
// header does not require samplerate.h.
struct SRC_STATE_tag;

class RageSoundReader_Resample_Good : public RageSoundReader_Filter {
 public:
  /* We own source. */
  RageSoundReader_Resample_Good(RageSoundReader* source, int sample_rate);
  RageSoundReader_Resample_Good(const RageSoundReader_Resample_Good& cpy);
  RageSoundReader_Resample_Good& operator=(
      const RageSoundReader_Resample_Good&) = delete;
  int SetPosition(int frame);
  int Read(float* buf, int frames);
  virtual ~RageSoundReader_Resample_Good();
  RageSoundReader_Resample_Good* Copy() const;
  bool SetProperty(const std::string& property, float value);
  int GetNextSourceFrame() const;
  float GetStreamToSourceRatio() const;

  /**
   * @brief Change the rate of a sound without changing the sample rate.
   * @param ratio the ratio for changing. */
  void SetRate(float ratio);

  /**
   * @brief Retrieve the exact rate.
   * @return the exact rate. */
  float GetRate() const;

  int GetSampleRate() const { return sample_rate_; }

 private:
  // Call if the input position is changed or reset.
  void Reset();
  // Output frames per input frame, including any rate change.
  double GetConversionRatio() const;
  bool IsPristine() const;

  SRC_STATE_tag* state_;  // libsamplerate converter (all channels interleaved)
  int channels_;
  int sample_rate_;
  float rate_;  // -1 when SetRate() has never been called

  // Input frames read from the source but not yet taken by libsamplerate.
  std::vector<float> pending_;
  bool source_eof_;

  // Bookkeeping used to report the stream position: how many input frames
  // libsamplerate has taken, versus how much input time its output covers. The
  // difference is what it is holding internally (its filter lookahead).
  double consumed_input_frames_;
  double emitted_input_time_;
  double last_ratio_;
};

#endif

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
