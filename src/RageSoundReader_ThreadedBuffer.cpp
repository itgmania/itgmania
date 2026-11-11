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
static const unsigned kReadBlockSizeFrames = 4096;  // 4K frames

// The maximum number of frames to buffer:
static const int kStreamingBufferFrames = 1024 * 128;  // 128K frames

/* When a sound has fewer than g_iMinFillFrames buffered, buffer at maximum
 * speed. Once beyond that, fill at a limited rate. */
static const int kMinFillFrames = 1024 * 16;  // 16K frames

RageSoundReader_ThreadedBuffer::RageSoundReader_ThreadedBuffer(
    RageSoundReader* source)
    : RageSoundReader_Filter(source), event_("ThreadedBuffer") {
  sample_rate_ = source->GetSampleRate();
  channels_ = source->GetNumChannels();

  int frame_size = sizeof(float) * this->GetNumChannels();
  data_buffer_.reserve(kStreamingBufferFrames * frame_size, frame_size);

  eof_ = false;
  shutdown_thread_ = false;
  enabled_ = false;
  filling_ = false;

  stream_position_.push_back(Mapping());
  stream_position_.back().position_of_first_frame_ =
      source->GetNextSourceFrame();
  stream_position_.back().rate_ = source->GetStreamToSourceRatio();

  thread_.SetName("Sound buffering");
  thread_.Create(StartBufferingThread, this);
}

RageSoundReader_ThreadedBuffer::RageSoundReader_ThreadedBuffer(
    const RageSoundReader_ThreadedBuffer& cpy)
    : RageSoundReader_Filter(
          nullptr),  // don't touch m_pSource before DisableBuffering
      event_("ThreadedBuffer") {
  bool was_enabled = cpy.DisableBuffering();

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

  if (was_enabled) {
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
  bool ret = enabled_;
  enabled_ = false;
  event_.Broadcast();

  while (filling_) {
    event_.Wait();
  }

  event_.Unlock();

  return ret;
}

int RageSoundReader_ThreadedBuffer::SetPosition(int frame) {
  bool was_enabled = DisableBuffering();

  data_buffer_.clear();

  int ret = RageSoundReader_Filter::SetPosition(frame);

  stream_position_.clear();
  stream_position_.push_back(Mapping());
  stream_position_.back().position_of_first_frame_ = frame;

  eof_ = ret == 0;

  if (was_enabled) {
    EnableBuffering();
  }

  return ret;
}

int RageSoundReader_ThreadedBuffer::GetEmptyFrames() const {
  int samples_per_frame = this->GetNumChannels();
  if (kReadBlockSizeFrames * samples_per_frame > data_buffer_.num_writable()) {
    return 0;
  }
  return data_buffer_.num_writable() / samples_per_frame;
}

int RageSoundReader_ThreadedBuffer::GetFilledFrames() const {
  int samples_per_frame = this->GetNumChannels();
  return data_buffer_.num_readable() / samples_per_frame;
}

int RageSoundReader_ThreadedBuffer::GetNextSourceFrame() const {
  event_.Lock();
  int ret = stream_position_.front().position_of_first_frame_;
  event_.Unlock();
  return ret;
}

float RageSoundReader_ThreadedBuffer::GetStreamToSourceRatio() const {
  event_.Lock();
  float ret = stream_position_.front().rate_;
  event_.Unlock();
  return ret;
}

int RageSoundReader_ThreadedBuffer::GetLength() const {
  bool was_enabled = DisableBuffering();
  int ret = source_->GetLength();
  if (was_enabled) {
    EnableBuffering();
  }
  return ret;
}
int RageSoundReader_ThreadedBuffer::GetLength_Fast() const {
  bool was_enabled = DisableBuffering();
  int ret = source_->GetLength_Fast();
  if (was_enabled) {
    EnableBuffering();
  }
  return ret;
}

bool RageSoundReader_ThreadedBuffer::SetProperty(
    const std::string& property, float value) {
  return source_->SetProperty(property, value);
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

    int frames_to_fill = kReadBlockSizeFrames;
    if (GetFilledFrames() < kMinFillFrames) {
      frames_to_fill =
          std::max(frames_to_fill, kMinFillFrames - GetFilledFrames());
    }

    int ret = FillFrames(frames_to_fill);

    // Release m_bFilling, and signal the event to wake anyone waiting for it.
    filling_ = false;
    event_.Broadcast();

    // On error or end of file, stop buffering the sound.
    if (ret < 0) {
      enabled_ = false;
      continue;
    }

    /* Sleep proportionately to the amount of data we buffered, so we
     * fill at a reasonable pace. */
    const float time_to_sleep =
        static_cast<float>(kReadBlockSizeFrames) / (sample_rate_ * 2.0f);

    if (event_.WaitTimeoutSupported()) {
      RageTimer time;
      time.Touch();
      time += time_to_sleep;
      event_.Wait(&time);
    } else {
      event_.Unlock();
      const auto sleep_duration_us =
          std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::duration<float>(time_to_sleep));
      std::this_thread::sleep_for(sleep_duration_us);
      event_.Lock();
    }
  }
  event_.Unlock();
}

int RageSoundReader_ThreadedBuffer::FillFrames(int frames) {
  int frames_filled = 0;
  while (frames_filled < frames) {
    int ret = FillBlock();

    if (ret == 0) {
      break;
    }
    // On error or end of file, stop buffering the sound.
    if (ret < 0) {
      return ret;
    }

    frames_filled += ret;
  }
  return frames_filled;
}

int RageSoundReader_ThreadedBuffer::FillBlock() {
  if (GetEmptyFrames() == 0) {
    return 0;
  }

  int samples_per_frame = this->GetNumChannels();
  ASSERT(
      kReadBlockSizeFrames * samples_per_frame <= data_buffer_.num_writable());

  int got_frames;
  int next_source_frame = 0;
  float rate = 0;

  event_.Unlock();

  {
    // We own m_pSource, even after unlocking, because m_bFilling is true.
    unsigned buf_size;
    float* buf = data_buffer_.get_write_pointer(&buf_size);
    ASSERT((buf_size % samples_per_frame) == 0);
    got_frames = source_->RetriedRead(
        buf, std::min(kReadBlockSizeFrames, buf_size / samples_per_frame),
        &next_source_frame, &rate);
  }

  event_.Lock();

  if (got_frames > 0) {
    // Add the data to the buffer.
    data_buffer_.advance_write_pointer(got_frames * samples_per_frame);
    if (next_source_frame != stream_position_.back().position_of_first_frame_ +
                                 stream_position_.back().frames_buffered_ ||
        rate != stream_position_.back().rate_) {
      stream_position_.push_back(Mapping());
      stream_position_.back().position_of_first_frame_ = next_source_frame;
      stream_position_.back().rate_ = rate;
    }

    stream_position_.back().frames_buffered_ += got_frames;
  }

  eof_ = (got_frames == END_OF_FILE);

  return got_frames;
}

int RageSoundReader_ThreadedBuffer::Read(float* buffer, int frames) {
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

  int ret;
  if (stream_position_.front().frames_buffered_) {
    Mapping& pos = stream_position_.front();
    int frames_to_read = std::min(frames, pos.frames_buffered_);
    int samples_per_frame = this->GetNumChannels();
    data_buffer_.read(buffer, frames_to_read * samples_per_frame);
    pos.position_of_first_frame_ += frames_to_read;
    pos.frames_buffered_ -= frames_to_read;
    ret = frames_to_read;
  } else if (eof_) {
    ret = END_OF_FILE;
  } else {
    ret = WOULD_BLOCK;
  }
  event_.Unlock();

  return ret;
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
