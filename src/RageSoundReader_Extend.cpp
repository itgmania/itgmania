#include "RageSoundReader_Extend.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "RageSoundReader.h"
#include "RageSoundReader_Filter.h"
#include "RageSoundUtil.h"
#include "RageUtil.h"
#include "global.h"

/*
 * Add support for negative seeks (adding a delay), extending a sound
 * beyond its end (m_LengthSeconds and M_CONTINUE), looping and fading.
 * This filter is normally inserted before extended buffering, implementing
 * properties that can seek the sound; this results in buffered seeks, but
 * changes to these properties are delayed.
 */

RageSoundReader_Extend::RageSoundReader_Extend(RageSoundReader* source)
    : RageSoundReader_Filter(source) {
  ASSERT_M(
      source != nullptr,
      "The music file was not found! Was it deleted or moved while the game "
      "was on?");
  position_frames_ = source->GetNextSourceFrame();

  stop_mode_ = M_STOP;
  start_frames_ = 0;
  length_frames_ = -1;
  fade_out_frames_ = 0;
  fade_in_frames_ = 0;
  ignore_fade_in_frames_ = false;
}

int RageSoundReader_Extend::SetPosition(int frame) {
  ignore_fade_in_frames_ = false;

  position_frames_ = frame;
  int ret = source_->SetPosition(std::max(frame, 0));
  if (ret < 0) {
    return ret;
  }

  if (length_frames_ != -1) {
    return position_frames_ < GetEndFrame();
  }

  /* If we're in CONTINUE and we seek past the end of the file, don't return
   * EOF. */
  if (stop_mode_ == M_CONTINUE) {
    return 1;
  }

  return ret;
}

int RageSoundReader_Extend::GetEndFrame() const {
  if (length_frames_ == -1) {
    return -1;
  }

  return start_frames_ + length_frames_;
}

int RageSoundReader_Extend::GetData(float* buffer, int frames) {
  int frames_to_read = frames;
  if (length_frames_ != -1) {
    int frames_left = GetEndFrame() - position_frames_;
    frames_left = std::max(0, frames_left);
    frames_to_read = std::min(frames_to_read, frames_left);
  }

  if (frames && !frames_to_read) {
    return RageSoundReader::END_OF_FILE;
  }

  if (position_frames_ < 0) {
    frames_to_read = std::min(frames_to_read, -position_frames_);
    memset(buffer, 0, frames_to_read * sizeof(float) * this->GetNumChannels());
    return frames_to_read;
  }

  int new_position_frames = source_->GetNextSourceFrame();
  int ret = RageSoundReader_Filter::Read(buffer, frames_to_read);

  /* Update the position from the source.  If the source is at EOF, skip this,
   * so we'll extrapolate in M_CONTINUE. */
  if (ret != RageSoundReader::END_OF_FILE) {
    position_frames_ = new_position_frames;
  }
  return ret;
}

int RageSoundReader_Extend::Read(float* buffer, int frames) {
  int frames_read = GetData(buffer, frames);
  if (frames_read == RageSoundReader::END_OF_FILE) {
    if ((length_frames_ != -1 && position_frames_ < GetEndFrame()) ||
        stop_mode_ == M_CONTINUE) {
      frames_read = frames;
      if (stop_mode_ != M_CONTINUE) {
        frames_read = std::min(GetEndFrame() - position_frames_, frames_read);
      }
      memset(buffer, 0, frames_read * sizeof(float) * this->GetNumChannels());
    }
  }

  if (frames_read > 0) {
    int full_volume_position_frames = 0;
    int silence_position_frames = 0;
    if (fade_in_frames_ != 0 && !ignore_fade_in_frames_) {
      silence_position_frames = 0;
      full_volume_position_frames = fade_in_frames_;
    }

    /* We want to fade when there's m_iFadeFrames frames left, but if
     * m_LengthFrames is -1, we don't know the length we're playing.
     * (m_LengthFrames is the length to play, not the length of the
     * source.)  If we don't know the length, don't fade. */
    if (fade_out_frames_ != 0 && length_frames_ != -1) {
      silence_position_frames = GetEndFrame();
      full_volume_position_frames = silence_position_frames - fade_out_frames_;
    }

    if (silence_position_frames != full_volume_position_frames) {
      const int start_second = position_frames_;
      const int end_second = position_frames_ + frames_read;
      const float start_volume = SCALE(
          start_second, full_volume_position_frames, silence_position_frames,
          1.0f, 0.0f);
      const float end_volume = SCALE(
          end_second, full_volume_position_frames, silence_position_frames,
          1.0f, 0.0f);
      RageSoundUtil::Fade(
          buffer, frames_read, this->GetNumChannels(), start_volume,
          end_volume);
    }

    position_frames_ += frames_read;
  }

  if (frames_read == RageSoundReader::END_OF_FILE && stop_mode_ == M_LOOP) {
    this->SetPosition(start_frames_);

    /* If we're not fading out at the end, then only fade in once.  Ignore
     * m_iFadeInFrames until seeked, so we only fade in once. */
    if (fade_out_frames_ == 0) {
      ignore_fade_in_frames_ = true;
    }
    return STREAM_LOOPED;
  }

  return frames_read;
}

int RageSoundReader_Extend::GetNextSourceFrame() const {
  return position_frames_;
}

bool RageSoundReader_Extend::SetProperty(
    const std::string& property, float value) {
  if (property == "StartSecond") {
    start_frames_ = static_cast<int>((value * this->GetSampleRate()) + 0.5);
    return true;
  }

  if (property == "LengthSeconds") {
    if (value == -1) {
      length_frames_ = -1;
    } else {
      length_frames_ = static_cast<int>((value * this->GetSampleRate()) + 0.5);
    }
    return true;
  }

  if (property == "Loop") {
    stop_mode_ = M_LOOP;
    return true;
  }

  if (property == "Stop") {
    stop_mode_ = M_STOP;
    return true;
  }

  if (property == "Continue") {
    stop_mode_ = M_CONTINUE;
    return true;
  }

  if (property == "FadeInSeconds") {
    fade_in_frames_ = static_cast<int>((value * this->GetSampleRate()) + 0.5);
    return true;
  }

  if (property == "FadeSeconds" || property == "FadeOutSeconds") {
    fade_out_frames_ = static_cast<int>((value * this->GetSampleRate()) + 0.5);
    return true;
  }

  return false;
}

/*
 * Copyright (c) 2003-2006 Glenn Maynard
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
