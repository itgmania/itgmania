#include "RageSoundReader_SpeedChange.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <string>

#include "RageSoundReader.h"
#include "RageSoundReader_Filter.h"
#include "RageUtil.h"
#include "global.h"

static const int WINDOW_SIZE_MS = 30;

RageSoundReader_SpeedChange::RageSoundReader_SpeedChange(
    RageSoundReader* pSource)
    : RageSoundReader_Filter(pSource) {
  channels_.resize(pSource->GetNumChannels());
  speed_ratio_ = trailing_speed_ratio_ = 1.0f;
  data_buffer_avail_frames_ = 0;
  SetSpeedRatio(1.0f);
  Reset();
}

void RageSoundReader_SpeedChange::SetSpeedRatio(float fRatio) {
  speed_ratio_ = fRatio;

  /* If we haven't read any data yet, put the new delta into effect immediately.
   */
  if (data_buffer_avail_frames_ == 0) {
    trailing_speed_ratio_ = speed_ratio_;
  }
}

int RageSoundReader_SpeedChange::GetWindowSizeFrames() const {
  return (WINDOW_SIZE_MS * GetSampleRate()) / 1000;
}

void RageSoundReader_SpeedChange::Reset() {
  trailing_speed_ratio_ = speed_ratio_;
  data_buffer_avail_frames_ = 0;
  for (size_t i = 0; i < channels_.size(); ++i) {
    ChannelInfo& c = channels_[i];
    c.correlated_pos_ = 0;
    c.last_correlated_pos_ = 0;
  }
  uncorrelated_pos_ = 0;
  pos_ = 0;
  error_frames_ = 0;
}

static int FindClosestMatch(
    const float* pBuffer, int iBufferSize, const float* pCorrelateBuffer,
    int iCorrelateBufferSize, int iStride) {
  if (iBufferSize <= iCorrelateBufferSize) {
    return 0;
  }

  /* Optimization: if the buffers are the same, they always corellate at 0. */
  if (pBuffer == pCorrelateBuffer) {
    return 0;
  }

  int iBufferDistanceToSearch = iBufferSize - iCorrelateBufferSize;
  int iBestOffset = 0;
  float fBestScore = 0;
  for (int i = 0; i < iBufferDistanceToSearch; i += iStride) {
    float fScore = 0;
    const float* pFrames = pBuffer + i;
    for (int j = 0; j < iCorrelateBufferSize; j += iStride) {
      float fDiff = pFrames[j] - pCorrelateBuffer[j];
      fScore += std::abs(fDiff);
    }

    if (i == 0 || fScore < fBestScore) {
      fBestScore = fScore;
      iBestOffset = i;
    }
  }
  return iBestOffset;
}

int RageSoundReader_SpeedChange::FillData(int iMaxFrames) {
  /* XXX: If the rate or source frame offset changes in the source, we should
   * stop at that point, so the changes propagate upward.  That's tricky, since
   * we want to process a block at a time.  We could just flush what we have,
   * and maybe tweak m_iDeltaFrames for the next block based on how much we
   * processed, so we average out the short block. */
  while (iMaxFrames > 0) {
    int iFramesToRead = iMaxFrames - data_buffer_avail_frames_;
    int iBytesToRead = iFramesToRead * channels_.size() * sizeof(float);
    if (iBytesToRead <= 0) {
      return data_buffer_avail_frames_;
    }

    float* pTempBuffer = new float[iBytesToRead / sizeof(float)];
    int iGotFrames = source_->Read(pTempBuffer, iFramesToRead);
    if (iGotFrames < 0) {
      delete[] pTempBuffer;
      if (iGotFrames == END_OF_FILE && data_buffer_avail_frames_) {
        return data_buffer_avail_frames_;
      }
      return iGotFrames;
    }

    for (size_t i = 0; i < channels_.size(); ++i) {
      ChannelInfo& c = channels_[i];

      if ((int)c.data_buffer_.size() < iMaxFrames) {
        c.data_buffer_.resize(iMaxFrames);
      }

      const float* pIn = pTempBuffer + i;
      float* pOut = &c.data_buffer_[data_buffer_avail_frames_];
      for (int j = 0; j < iGotFrames; ++j) {
        *pOut = *pIn;
        pIn += channels_.size();
        ++pOut;
      }
    }
    delete[] pTempBuffer;

    data_buffer_avail_frames_ += iGotFrames;
  }
  return data_buffer_avail_frames_;
}

void RageSoundReader_SpeedChange::EraseData(int iFramesToDelete) {
  ASSERT(iFramesToDelete <= data_buffer_avail_frames_);
  ASSERT(data_buffer_avail_frames_ >= iFramesToDelete);
  ASSERT(uncorrelated_pos_ >= iFramesToDelete);

  int iFramesToMove = data_buffer_avail_frames_ - iFramesToDelete;
  data_buffer_avail_frames_ -= iFramesToDelete;
  uncorrelated_pos_ -= iFramesToDelete;
  for (size_t i = 0; i < channels_.size(); ++i) {
    ChannelInfo& c = channels_[i];
    if (iFramesToMove) {
      memmove(
          &c.data_buffer_[0], &c.data_buffer_[iFramesToDelete],
          iFramesToMove * sizeof(float));
    }
    ASSERT(c.correlated_pos_ >= iFramesToDelete);
    c.correlated_pos_ -= iFramesToDelete;
  }
}

int RageSoundReader_SpeedChange::Step() {
  if (data_buffer_avail_frames_ == 0) {
    return FillData(GetWindowSizeFrames());
  }

  /* If m_iPos is non-zero, we just finished playing a previous block, so
   * advance forward. */
  if (pos_) {
    /* Advance m_iCorrelatedPos past the data that was just copied, to point to
     * the sound that we would have played if we had continued copying at that
     * point. */
    for (size_t i = 0; i < channels_.size(); ++i) {
      ASSERT(channels_[i].correlated_pos_ + pos_ <= data_buffer_avail_frames_);
      channels_[i].correlated_pos_ += pos_;
    }

    /* Advance m_iUncorrelatedPos to the position we'd prefer to continue
     * playing from. This rounds the position, making the ratio imprecise.
     * Record the amount of error introduced here.  If we want to advance by 2.3
     * frames, we'll advance by 2.0 frames, and advance by 0.3 more the next
     * time around. */
    float fAdvanceFrames = GetWindowSizeFrames() * trailing_speed_ratio_;
    fAdvanceFrames += error_frames_;
    int iTrailingDeltaFrames = static_cast<int>((fAdvanceFrames) + 0.5);
    error_frames_ = fAdvanceFrames - iTrailingDeltaFrames;
    uncorrelated_pos_ += iTrailingDeltaFrames;

    pos_ = 0;
  }

  /* Update m_fTrailingSpeedRatio.  Do this after advancing m_iUncorrelatedPos,
   * so changes to m_iUncorrelatedPos line up with GetNextSourceFrame. */
  trailing_speed_ratio_ = speed_ratio_;

  /* We don't need any data before the earlier of m_iUncorrelatedPos or
   * m_iCorrelatedPos. */
  int iToDelete = uncorrelated_pos_;
  for (size_t i = 0; i < channels_.size(); ++i) {
    ChannelInfo& c = channels_[i];
    ASSERT(c.correlated_pos_ <= data_buffer_avail_frames_);
    iToDelete = std::min(iToDelete, c.correlated_pos_);
    // iToDelete = std::min( iToDelete, m_iDataBufferAvailFrames );
  }
  EraseData(iToDelete);

  /* Fill as much data as we might need to do the search and use the result. */
  {
    int iMaxPositionNeeded =
        uncorrelated_pos_ + GetToleranceFrames() + GetWindowSizeFrames();
    for (size_t i = 0; i < channels_.size(); ++i) {
      iMaxPositionNeeded = std::max(
          iMaxPositionNeeded,
          channels_[i].correlated_pos_ + GetWindowSizeFrames());
    }

    int iGot = FillData(iMaxPositionNeeded);
    if (iGot < 0) {
      return iGot;
    }

    if (iMaxPositionNeeded > data_buffer_avail_frames_) {
      /* We're at EOF.  Flush the remaining data, if any. */
      uncorrelated_pos_ = channels_[0].correlated_pos_;
      return data_buffer_avail_frames_;
    }
  }

  /* Starting at our preferred position (m_iUncorrelatedPos), within
   * GetToleranceFrames(), search for data that looks like the sound we would
   * have continued playing if we kept going from the old position
   * (m_iCorrelatedPos). */
  int iCorrelatedToMatch = GetWindowSizeFrames() / 4;
  int iUncorrelatedToMatch =
      GetToleranceFrames() + iCorrelatedToMatch;  // maximum distance to search

  for (size_t i = 0; i < channels_.size(); ++i) {
    ChannelInfo& c = channels_[i];
    ASSERT(c.correlated_pos_ >= 0);
    ASSERT(c.correlated_pos_ < data_buffer_avail_frames_);

    int iBest = FindClosestMatch(
        &c.data_buffer_[uncorrelated_pos_], iUncorrelatedToMatch,
        &c.data_buffer_[c.correlated_pos_], iCorrelatedToMatch,
        channels_.size());
    c.last_correlated_pos_ = c.correlated_pos_;
    c.correlated_pos_ = iBest + uncorrelated_pos_;
    ASSERT(
        channels_[i].correlated_pos_ + GetWindowSizeFrames() <=
        data_buffer_avail_frames_);
  }
  return data_buffer_avail_frames_;
}

int RageSoundReader_SpeedChange::GetCursorAvail() const {
  int iCursorAvail = GetWindowSizeFrames() - pos_;
  for (size_t i = 0; i < channels_.size(); ++i) {
    int iCursorAvailForChannel =
        (data_buffer_avail_frames_ - channels_[i].correlated_pos_) - pos_;
    iCursorAvail = std::min(iCursorAvail, iCursorAvailForChannel);
  }

  return iCursorAvail;
}

int RageSoundReader_SpeedChange::Read(float* pBuf, int iFrames) {
  for (;;) {
    if (data_buffer_avail_frames_ == 0 &&
        trailing_speed_ratio_ == speed_ratio_ && speed_ratio_ == 1.0f) {
      /* Fast path: the buffer is empty, and we're not scaling the audio.  Read
       * directly into the output buffer, to eliminate memory and copying
       * overhead. */
      return source_->Read(pBuf, iFrames);
    }

    int iCursorAvail = GetCursorAvail();

    if (iCursorAvail == 0) {
      int iRet = Step();
      if (iRet < 0) {
        return iRet;
      }
      if (!GetCursorAvail()) {
        return END_OF_FILE;
      }
      continue;
    }

    /* copy GetWindowSizeFrames() from iCorrelatedPos */
    int iFramesLen = iFrames;
    int iFramesAvail = std::min(iCursorAvail, iFramesLen);

    iFrames -= iFramesAvail;
    int iFramesRead = iFramesAvail;

    int iWindowSizeFrames = GetWindowSizeFrames();
    while (iFramesAvail--) {
      for (size_t i = 0; i < channels_.size(); ++i) {
        ChannelInfo& c = channels_[i];
        float i1 = c.data_buffer_[c.correlated_pos_ + pos_];
        float i2 = c.data_buffer_[c.last_correlated_pos_ + pos_];
        *pBuf++ = SCALE(pos_, 0, iWindowSizeFrames, i2, i1);
      }

      ++pos_;
    }

    return iFramesRead;
  }
}

/* We prefer to be able to seek precisely, so seeking to a position produces
 * data equal to what you'd get if you read data up to that point.  This filter
 * can't do that, because the exact selection of slices is dependent on the
 * previous selection. */
int RageSoundReader_SpeedChange::SetPosition(int iFrame) {
  Reset();
  return RageSoundReader_Filter::SetPosition(iFrame);
}

bool RageSoundReader_SpeedChange::SetProperty(
    const std::string& sProperty, float fValue) {
  if (sProperty == "Speed") {
    SetSpeedRatio(fValue);
    return true;
  }

  return RageSoundReader_Filter::SetProperty(sProperty, fValue);
}

int RageSoundReader_SpeedChange::GetNextSourceFrame() const {
  float fRatio = trailing_speed_ratio_;

  int iSourceFrame = RageSoundReader_Filter::GetNextSourceFrame();
  int iPos = static_cast<int>((pos_ * fRatio) + 0.5);

  iSourceFrame -= data_buffer_avail_frames_;
  iSourceFrame += uncorrelated_pos_ + iPos;
  return iSourceFrame;
}

float RageSoundReader_SpeedChange::GetStreamToSourceRatio() const {
  /* If the ratio has changed and GetCursorAvail() is 0, the new ratio will
   * become the real ratio on the next read.  Otherwise, we'll continue using
   * our old ratio for a bit longer. */
  if (GetCursorAvail() == 0) {
    return speed_ratio_ * RageSoundReader_Filter::GetStreamToSourceRatio();
  } else {
    return trailing_speed_ratio_ *
           RageSoundReader_Filter::GetStreamToSourceRatio();
  }
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
