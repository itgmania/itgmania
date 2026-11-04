#include "SampleHistory.h"

#include <algorithm>
#include <cmath>

#include "RageUtil.h"

inline float sample_step_size(int samples_per_second) {
  return 1.0f / samples_per_second;
}

SampleHistory::SampleHistory() {
  last_history_ = 0;
  history_samples_per_second_ = 60;
  history_seconds_ = 0.0f;
  to_sample_ = sample_step_size(history_samples_per_second_);
  history_seconds_ = 10.0f;
  int iSamples = std::lrint(history_samples_per_second_ * history_seconds_);
  history_.resize(iSamples);
}

float SampleHistory::GetSampleNum(float fSamplesAgo) const {
  fSamplesAgo = std::min(fSamplesAgo, (float)history_.size() - 1);
  if (fSamplesAgo < 0) {
    fSamplesAgo = 0;
  }
  if (history_.size() == 0) {
    return 0.0f;
  }

  float fSample = last_history_ - fSamplesAgo - 1;

  float f = std::floor(fSample);
  int iSample = std::lrint(f);
  int iNextSample = iSample + 1;
  wrap(iSample, history_.size());
  wrap(iNextSample, history_.size());

  float p = fSample - f;
  float fRet = lerp(p, history_[iSample], history_[iNextSample]);
  //	LOG->Trace( "%.3f: %i, %i, %.3f (f %.3f, %.3f)", fSample, iSample,
  // iNextSample, fRet, f, p );
  return fRet;
}

float SampleHistory::GetSample(float fSecondsAgo) const {
  float fSamplesAgo = fSecondsAgo * history_samples_per_second_;
  return GetSampleNum(fSamplesAgo);
}

void SampleHistory::AddSample(float fSample, float fDeltaTime) {
  while (fDeltaTime > 0.0001f) {
    float fTime = std::min(to_sample_, fDeltaTime);
    to_sample_ -= fTime;
    fDeltaTime -= fTime;

    if (to_sample_ < 0.0001f) {
      ++last_history_;
      last_history_ %= history_.size();
      to_sample_ += sample_step_size(history_samples_per_second_);
    }

    history_[last_history_] = fSample;
  }
}

/*
 * (c) 2006-2007 Glenn Maynard
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
