/*
 * Implements properties:
 *   "Speed" - cause the sound to play faster or slower
 *   "Pitch" - raise or lower the pitch of the sound
 *
 * Pitch changing is implemented by combining speed changing and rate changing
 * (via resampling).  The resampler needs to be changed later than the speed
 * changer; this class just controls parameters on the other two real filters.
 */

#include "RageSoundReader_PitchChange.h"

#include <string>

#include "Preference.h"
#include "RageSoundReader.h"
#include "RageSoundReader_Filter.h"
#include "RageSoundReader_Resample_Good.h"
#include "RageSoundReader_SpeedChange.h"

static Preference<bool> rate_mod_preserves_pitch("RateModPreservesPitch", true);

RageSoundReader_PitchChange::RageSoundReader_PitchChange(
    RageSoundReader* source)
    : RageSoundReader_Filter(nullptr) {
  bool preserve_pitch = rate_mod_preserves_pitch.Get();

  speed_change_ = new RageSoundReader_SpeedChange(source);
  if (preserve_pitch) {
    resample_ = new RageSoundReader_Resample_Good(
        speed_change_, speed_change_->GetSampleRate());
  } else {
    resample_ =
        new RageSoundReader_Resample_Good(source, source->GetSampleRate());
  }
  source_ = resample_;
  speed_ratio_ = 1.0f;
  pitch_ratio_ = 1.0f;
  last_set_speed_ratio_ = speed_ratio_;
  last_set_pitch_ratio_ = pitch_ratio_;
}

RageSoundReader_PitchChange::RageSoundReader_PitchChange(
    const RageSoundReader_PitchChange& cpy)
    : RageSoundReader_Filter(cpy) {
  /* The source tree has already been copied.  Our source is m_pResample; its
   * source is m_pSpeedChange (and its source is a copy of the pSource we were
   * initialized with). */
  resample_ = dynamic_cast<RageSoundReader_Resample_Good*>(&*source_);
  speed_change_ =
      dynamic_cast<RageSoundReader_SpeedChange*>(resample_->GetSource());
  speed_ratio_ = cpy.speed_ratio_;
  pitch_ratio_ = cpy.pitch_ratio_;
  last_set_speed_ratio_ = cpy.last_set_speed_ratio_;
  last_set_pitch_ratio_ = cpy.last_set_pitch_ratio_;
}

int RageSoundReader_PitchChange::Read(float* buf, int frames) {
  /* m_pSpeedChange->NextReadWillStep is true if speed changes will be applied
   * immediately on the next Read().  When this is true, apply the ratio to the
   * resampler and the speed changer simultaneously, so they take effect as
   * closely together as possible. */
  bool preserve_pitch = rate_mod_preserves_pitch.Get();
  if ((!preserve_pitch) || ((last_set_speed_ratio_ != speed_ratio_ ||
                             last_set_pitch_ratio_ != pitch_ratio_) &&
                            speed_change_->NextReadWillStep())) {
    float rate = GetStreamToSourceRatio();

    /* This is the simple way: */
    // m_pResample->SetRate( m_fPitchRatio );
    // m_pSpeedChange->SetSpeedRatio( m_fSpeedRatio / m_fPitchRatio );

    /* However, the resampler has a limited granularity due to internal fixed-
     * point math, and the actual ratio will be slightly different than what
     * we tell it to use.  The actual ratio used is fActualPitchRatio. */

    if (preserve_pitch) {
      resample_->SetRate(pitch_ratio_);
      float actual_pitch_ratio = resample_->GetRate();
      float requested_speed_ratio = speed_ratio_ / actual_pitch_ratio;
      speed_change_->SetSpeedRatio(requested_speed_ratio);
      last_set_pitch_ratio_ = pitch_ratio_;
      last_set_speed_ratio_ = speed_ratio_;
    } else {
      resample_->SetRate(speed_ratio_);
      last_set_speed_ratio_ = speed_ratio_;
    }

    /* If we just applied a new speed and it caused the ratio to change, return
     * no data, so the caller can see the new ratio. */
    if (rate != GetStreamToSourceRatio()) {
      return 0;
    }
  }

  return RageSoundReader_Filter::Read(buf, frames);
}

bool RageSoundReader_PitchChange::SetProperty(
    const std::string& property, float value) {
  if (property == "Rate") {
    /* Don't propagate this.  m_pResample will take it, but it's under
     * our control. */
    return false;
  }
  if (property == "Speed") {
    SetSpeedRatio(value);
    return true;
  }

  if (property == "Pitch") {
    SetPitchRatio(value);
    return true;
  }

  return RageSoundReader_Filter::SetProperty(property, value);
}

/*
 * Copyright (c) 2006 Glenn Maynard
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
