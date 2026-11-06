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

RageSoundReader_Extend::RageSoundReader_Extend(RageSoundReader* pSource)
    : RageSoundReader_Filter(pSource) {
  ASSERT_M(
      pSource != nullptr,
      "The music file was not found! Was it deleted or moved while the game "
      "was on?");
  position_frames_ = pSource->GetNextSourceFrame();

  stop_mode_ = M_STOP;
  start_frames_ = 0;
  length_frames_ = -1;
  fade_out_frames_ = 0;
  fade_in_frames_ = 0;
  ignore_fade_in_frames_ = false;
}

int RageSoundReader_Extend::SetPosition(int iFrame) {
  ignore_fade_in_frames_ = false;

  position_frames_ = iFrame;
  int iRet = source_->SetPosition(std::max(iFrame, 0));
  if (iRet < 0) {
    return iRet;
  }

  if (length_frames_ != -1) {
    return position_frames_ < GetEndFrame();
  }

  /* If we're in CONTINUE and we seek past the end of the file, don't return
   * EOF. */
  if (stop_mode_ == M_CONTINUE) {
    return 1;
  }

  return iRet;
}

int RageSoundReader_Extend::GetEndFrame() const {
  if (length_frames_ == -1) {
    return -1;
  }

  return start_frames_ + length_frames_;
}

int RageSoundReader_Extend::GetData(float* pBuffer, int iFrames) {
  int iFramesToRead = iFrames;
  if (length_frames_ != -1) {
    int iFramesLeft = GetEndFrame() - position_frames_;
    iFramesLeft = std::max(0, iFramesLeft);
    iFramesToRead = std::min(iFramesToRead, iFramesLeft);
  }

  if (iFrames && !iFramesToRead) {
    return RageSoundReader::END_OF_FILE;
  }

  if (position_frames_ < 0) {
    iFramesToRead = std::min(iFramesToRead, -position_frames_);
    memset(pBuffer, 0, iFramesToRead * sizeof(float) * this->GetNumChannels());
    return iFramesToRead;
  }

  int iNewPositionFrames = source_->GetNextSourceFrame();
  int iRet = RageSoundReader_Filter::Read(pBuffer, iFramesToRead);

  /* Update the position from the source.  If the source is at EOF, skip this,
   * so we'll extrapolate in M_CONTINUE. */
  if (iRet != RageSoundReader::END_OF_FILE) {
    position_frames_ = iNewPositionFrames;
  }
  return iRet;
}

int RageSoundReader_Extend::Read(float* pBuffer, int iFrames) {
  int iFramesRead = GetData(pBuffer, iFrames);
  if (iFramesRead == RageSoundReader::END_OF_FILE) {
    if ((length_frames_ != -1 && position_frames_ < GetEndFrame()) ||
        stop_mode_ == M_CONTINUE) {
      iFramesRead = iFrames;
      if (stop_mode_ != M_CONTINUE) {
        iFramesRead = std::min(GetEndFrame() - position_frames_, iFramesRead);
      }
      memset(pBuffer, 0, iFramesRead * sizeof(float) * this->GetNumChannels());
    }
  }

  if (iFramesRead > 0) {
    int iFullVolumePositionFrames = 0;
    int iSilencePositionFrames = 0;
    if (fade_in_frames_ != 0 && !ignore_fade_in_frames_) {
      iSilencePositionFrames = 0;
      iFullVolumePositionFrames = fade_in_frames_;
    }

    /* We want to fade when there's m_iFadeFrames frames left, but if
     * m_LengthFrames is -1, we don't know the length we're playing.
     * (m_LengthFrames is the length to play, not the length of the
     * source.)  If we don't know the length, don't fade. */
    if (fade_out_frames_ != 0 && length_frames_ != -1) {
      iSilencePositionFrames = GetEndFrame();
      iFullVolumePositionFrames = iSilencePositionFrames - fade_out_frames_;
    }

    if (iSilencePositionFrames != iFullVolumePositionFrames) {
      const int iStartSecond = position_frames_;
      const int iEndSecond = position_frames_ + iFramesRead;
      const float fStartVolume = SCALE(
          iStartSecond, iFullVolumePositionFrames, iSilencePositionFrames, 1.0f,
          0.0f);
      const float fEndVolume = SCALE(
          iEndSecond, iFullVolumePositionFrames, iSilencePositionFrames, 1.0f,
          0.0f);
      RageSoundUtil::Fade(
          pBuffer, iFramesRead, this->GetNumChannels(), fStartVolume,
          fEndVolume);
    }

    position_frames_ += iFramesRead;
  }

  if (iFramesRead == RageSoundReader::END_OF_FILE && stop_mode_ == M_LOOP) {
    this->SetPosition(start_frames_);

    /* If we're not fading out at the end, then only fade in once.  Ignore
     * m_iFadeInFrames until seeked, so we only fade in once. */
    if (fade_out_frames_ == 0) {
      ignore_fade_in_frames_ = true;
    }
    return STREAM_LOOPED;
  }

  return iFramesRead;
}

int RageSoundReader_Extend::GetNextSourceFrame() const {
  return position_frames_;
}

bool RageSoundReader_Extend::SetProperty(
    const std::string& sProperty, float fValue) {
  if (sProperty == "StartSecond") {
    start_frames_ = static_cast<int>((fValue * this->GetSampleRate()) + 0.5);
    return true;
  }

  if (sProperty == "LengthSeconds") {
    if (fValue == -1) {
      length_frames_ = -1;
    } else {
      length_frames_ = static_cast<int>((fValue * this->GetSampleRate()) + 0.5);
    }
    return true;
  }

  if (sProperty == "Loop") {
    stop_mode_ = M_LOOP;
    return true;
  }

  if (sProperty == "Stop") {
    stop_mode_ = M_STOP;
    return true;
  }

  if (sProperty == "Continue") {
    stop_mode_ = M_CONTINUE;
    return true;
  }

  if (sProperty == "FadeInSeconds") {
    fade_in_frames_ = static_cast<int>((fValue * this->GetSampleRate()) + 0.5);
    return true;
  }

  if (sProperty == "FadeSeconds" || sProperty == "FadeOutSeconds") {
    fade_out_frames_ = static_cast<int>((fValue * this->GetSampleRate()) + 0.5);
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
