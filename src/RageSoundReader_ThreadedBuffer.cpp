#include "RageSoundReader_ThreadedBuffer.h"

#include <algorithm>
#include <chrono>
#include <list>
#include <string>
#include <thread>

#include "RageSoundReader.h"
#include "RageSoundReader_Filter.h"
#include "RageTimer.h"
#include "global.h"

/* Implement threaded read-ahead buffering.
 *
 * If a buffer is low on data, keep filling until it has a g_iMinFillFrames.
 * Once beyond that, fill at a rate relative to realtime.
 *
 * This allows a stream to have a large buffer, for higher reliability, without
 * causing major CPU bursts when the stream starts or underruns. Filling 32k
 * takes more CPU than filling 4k frames, and may cause a skip. */

// The amount of data to read at once:
static const unsigned g_iReadBlockSizeFrames = 4096;  // 4K frames

// The maximum number of frames to buffer:
static const int g_iStreamingBufferFrames = 1024 * 128;  // 128K frames

/* When a sound has fewer than g_iMinFillFrames buffered, buffer at maximum
 * speed. Once beyond that, fill at a limited rate. */
static const int g_iMinFillFrames = 1024 * 16;  // 16K frames

RageSoundReader_ThreadedBuffer::RageSoundReader_ThreadedBuffer(
    RageSoundReader* pSource)
    : RageSoundReader_Filter(pSource), event_("ThreadedBuffer") {
  sample_rate_ = pSource->GetSampleRate();
  channels_ = pSource->GetNumChannels();

  int iFrameSize = sizeof(float) * this->GetNumChannels();
  data_buffer_.reserve(g_iStreamingBufferFrames * iFrameSize, iFrameSize);

  eof_ = false;
  shutdown_thread_ = false;
  enabled_ = false;
  filling_ = false;

  stream_position_.push_back(Mapping());
  stream_position_.back().position_of_first_frame_ =
      pSource->GetNextSourceFrame();
  stream_position_.back().rate_ = pSource->GetStreamToSourceRatio();

  thread_.SetName("Sound buffering");
  thread_.Create(StartBufferingThread, this);
}

RageSoundReader_ThreadedBuffer::RageSoundReader_ThreadedBuffer(
    const RageSoundReader_ThreadedBuffer& cpy)
    : RageSoundReader_Filter(
          nullptr),  // don't touch m_pSource before DisableBuffering
      event_("ThreadedBuffer") {
  bool bWasEnabled = cpy.DisableBuffering();

  source_ = cpy.source_->Copy();
  sample_rate_ = cpy.sample_rate_;
  channels_ = cpy.channels_;
  data_buffer_ = cpy.data_buffer_;
  eof_ = cpy.eof_;
  shutdown_thread_ = cpy.shutdown_thread_;
  enabled_ = cpy.enabled_;
  filling_ = cpy.filling_;

  stream_position_ = cpy.stream_position_;

  thread_.Create(StartBufferingThread, this);

  if (bWasEnabled) {
    cpy.EnableBuffering();
    EnableBuffering();
  }
}

RageSoundReader_ThreadedBuffer::~RageSoundReader_ThreadedBuffer() {
  DisableBuffering();
  event_.Lock();
  shutdown_thread_ = true;
  event_.Broadcast();
  event_.Unlock();

  thread_.Wait();
}

void RageSoundReader_ThreadedBuffer::EnableBuffering() {
  event_.Lock();
  enabled_ = true;
  event_.Broadcast();
  event_.Unlock();
}

bool RageSoundReader_ThreadedBuffer::DisableBuffering() {
  event_.Lock();
  bool bRet = enabled_;
  enabled_ = false;
  event_.Broadcast();

  while (filling_) {
    event_.Wait();
  }

  event_.Unlock();

  return bRet;
}

int RageSoundReader_ThreadedBuffer::SetPosition(int iFrame) {
  bool bWasEnabled = DisableBuffering();

  data_buffer_.clear();

  int iRet = RageSoundReader_Filter::SetPosition(iFrame);

  stream_position_.clear();
  stream_position_.push_back(Mapping());
  stream_position_.back().position_of_first_frame_ = iFrame;

  eof_ = iRet == 0;

  if (bWasEnabled) {
    EnableBuffering();
  }

  return iRet;
}

int RageSoundReader_ThreadedBuffer::GetEmptyFrames() const {
  int iSamplesPerFrame = this->GetNumChannels();
  if (g_iReadBlockSizeFrames * iSamplesPerFrame > data_buffer_.num_writable()) {
    return 0;
  }
  return data_buffer_.num_writable() / iSamplesPerFrame;
}

int RageSoundReader_ThreadedBuffer::GetFilledFrames() const {
  int iSamplesPerFrame = this->GetNumChannels();
  return data_buffer_.num_readable() / iSamplesPerFrame;
}

int RageSoundReader_ThreadedBuffer::GetNextSourceFrame() const {
  event_.Lock();
  int iRet = stream_position_.front().position_of_first_frame_;
  event_.Unlock();
  return iRet;
}

float RageSoundReader_ThreadedBuffer::GetStreamToSourceRatio() const {
  event_.Lock();
  float fRet = stream_position_.front().rate_;
  event_.Unlock();
  return fRet;
}

int RageSoundReader_ThreadedBuffer::GetLength() const {
  bool bWasEnabled = DisableBuffering();
  int iRet = source_->GetLength();
  if (bWasEnabled) {
    EnableBuffering();
  }
  return iRet;
}
int RageSoundReader_ThreadedBuffer::GetLength_Fast() const {
  bool bWasEnabled = DisableBuffering();
  int iRet = source_->GetLength_Fast();
  if (bWasEnabled) {
    EnableBuffering();
  }
  return iRet;
}

bool RageSoundReader_ThreadedBuffer::SetProperty(
    const std::string& sProperty, float fValue) {
  return source_->SetProperty(sProperty, fValue);
}

void RageSoundReader_ThreadedBuffer::BufferingThread() {
  event_.Lock();
  while (!shutdown_thread_) {
    if (!enabled_) {
      event_.Wait();
      continue;
    }

    // Fill some data.
    filling_ = true;

    int iFramesToFill = g_iReadBlockSizeFrames;
    if (GetFilledFrames() < g_iMinFillFrames) {
      iFramesToFill =
          std::max(iFramesToFill, g_iMinFillFrames - GetFilledFrames());
    }

    int iRet = FillFrames(iFramesToFill);

    // Release m_bFilling, and signal the event to wake anyone waiting for it.
    filling_ = false;
    event_.Broadcast();

    // On error or end of file, stop buffering the sound.
    if (iRet < 0) {
      enabled_ = false;
      continue;
    }

    /* Sleep proportionately to the amount of data we buffered, so we
     * fill at a reasonable pace. */
    const float fTimeToSleep =
        static_cast<float>(g_iReadBlockSizeFrames) / (sample_rate_ * 2.0f);

    if (event_.WaitTimeoutSupported()) {
      RageTimer time;
      time.Touch();
      time += fTimeToSleep;
      event_.Wait(&time);
    } else {
      event_.Unlock();
      const auto sleep_duration_us =
          std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::duration<float>(fTimeToSleep));
      std::this_thread::sleep_for(sleep_duration_us);
      event_.Lock();
    }
  }
  event_.Unlock();
}

int RageSoundReader_ThreadedBuffer::FillFrames(int iFrames) {
  int iFramesFilled = 0;
  while (iFramesFilled < iFrames) {
    int iRet = FillBlock();

    if (iRet == 0) {
      break;
    }
    // On error or end of file, stop buffering the sound.
    if (iRet < 0) {
      return iRet;
    }

    iFramesFilled += iRet;
  }
  return iFramesFilled;
}

int RageSoundReader_ThreadedBuffer::FillBlock() {
  if (GetEmptyFrames() == 0) {
    return 0;
  }

  int iSamplesPerFrame = this->GetNumChannels();
  ASSERT(
      g_iReadBlockSizeFrames * iSamplesPerFrame <= data_buffer_.num_writable());

  int iGotFrames;
  int iNextSourceFrame = 0;
  float fRate = 0;

  event_.Unlock();

  {
    // We own m_pSource, even after unlocking, because m_bFilling is true.
    unsigned iBufSize;
    float* pBuf = data_buffer_.get_write_pointer(&iBufSize);
    ASSERT((iBufSize % iSamplesPerFrame) == 0);
    iGotFrames = source_->RetriedRead(
        pBuf, std::min(g_iReadBlockSizeFrames, iBufSize / iSamplesPerFrame),
        &iNextSourceFrame, &fRate);
  }

  event_.Lock();

  if (iGotFrames > 0) {
    // Add the data to the buffer.
    data_buffer_.advance_write_pointer(iGotFrames * iSamplesPerFrame);
    if (iNextSourceFrame != stream_position_.back().position_of_first_frame_ +
                                stream_position_.back().frames_buffered_ ||
        fRate != stream_position_.back().rate_) {
      stream_position_.push_back(Mapping());
      stream_position_.back().position_of_first_frame_ = iNextSourceFrame;
      stream_position_.back().rate_ = fRate;
    }

    stream_position_.back().frames_buffered_ += iGotFrames;
  }

  eof_ = (iGotFrames == END_OF_FILE);

  return iGotFrames;
}

int RageSoundReader_ThreadedBuffer::Read(float* pBuffer, int iFrames) {
  if (!eof_) {
    EnableBuffering();
  }

  event_.Lock();

  {
    /* Delete any empty mappings from the beginning, but don't empty the list,
     * so we always have the current position and rate. If we delete an item,
     * the rate or position has probably changed, so return. */
    std::list<Mapping>::iterator it = stream_position_.begin();
    ++it;
    if (it != stream_position_.end() &&
        !stream_position_.front().frames_buffered_) {
      ++it;
      stream_position_.pop_front();
      event_.Unlock();
      return 0;
    }
  }

  int iRet;
  if (stream_position_.front().frames_buffered_) {
    Mapping& pos = stream_position_.front();
    int iFramesToRead = std::min(iFrames, pos.frames_buffered_);
    int iSamplesPerFrame = this->GetNumChannels();
    data_buffer_.read(pBuffer, iFramesToRead * iSamplesPerFrame);
    pos.position_of_first_frame_ += iFramesToRead;
    pos.frames_buffered_ -= iFramesToRead;
    iRet = iFramesToRead;
  } else if (eof_) {
    iRet = END_OF_FILE;
  } else {
    iRet = WOULD_BLOCK;
  }
  event_.Unlock();

  return iRet;
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
