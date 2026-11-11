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

static const int kWindowSizeMs = 30;

RageSoundReader_SpeedChange::RageSoundReader_SpeedChange(
    RageSoundReader* source)
    : RageSoundReader_Filter(source) {
  channels_.resize(source->GetNumChannels());
  speed_ratio_ = trailing_speed_ratio_ = 1.0f;
  data_buffer_avail_frames_ = 0;
  SetSpeedRatio(1.0f);
  Reset();
}

void RageSoundReader_SpeedChange::SetSpeedRatio(float ratio) {
  speed_ratio_ = ratio;

  /* If we haven't read any data yet, put the new delta into effect immediately.
   */
  if (data_buffer_avail_frames_ == 0) {
    trailing_speed_ratio_ = speed_ratio_;
  }
}

int RageSoundReader_SpeedChange::GetWindowSizeFrames() const {
  return (kWindowSizeMs * GetSampleRate()) / 1000;
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
    const float* buffer, int buffer_size, const float* correlate_buffer,
    int correlate_buffer_size, int stride) {
  if (buffer_size <= correlate_buffer_size) {
    return 0;
  }

  /* Optimization: if the buffers are the same, they always corellate at 0. */
  if (buffer == correlate_buffer) {
    return 0;
  }

  int buffer_distance_to_search = buffer_size - correlate_buffer_size;
  int best_offset = 0;
  float best_score = 0;
  for (int i = 0; i < buffer_distance_to_search; i += stride) {
    float score = 0;
    const float* frames = buffer + i;
    for (int j = 0; j < correlate_buffer_size; j += stride) {
      float diff = frames[j] - correlate_buffer[j];
      score += std::abs(diff);
    }

    if (i == 0 || score < best_score) {
      best_score = score;
      best_offset = i;
    }
  }
  return best_offset;
}

int RageSoundReader_SpeedChange::FillData(int max_frames) {
  /* XXX: If the rate or source frame offset changes in the source, we should
   * stop at that point, so the changes propagate upward.  That's tricky, since
   * we want to process a block at a time.  We could just flush what we have,
   * and maybe tweak m_iDeltaFrames for the next block based on how much we
   * processed, so we average out the short block. */
  while (max_frames > 0) {
    int frames_to_read = max_frames - data_buffer_avail_frames_;
    int bytes_to_read = frames_to_read * channels_.size() * sizeof(float);
    if (bytes_to_read <= 0) {
      return data_buffer_avail_frames_;
    }

    float* temp_buffer = new float[bytes_to_read / sizeof(float)];
    int got_frames = source_->Read(temp_buffer, frames_to_read);
    if (got_frames < 0) {
      delete[] temp_buffer;
      if (got_frames == END_OF_FILE && data_buffer_avail_frames_) {
        return data_buffer_avail_frames_;
      }
      return got_frames;
    }

    for (size_t i = 0; i < channels_.size(); ++i) {
      ChannelInfo& c = channels_[i];

      if ((int)c.data_buffer_.size() < max_frames) {
        c.data_buffer_.resize(max_frames);
      }

      const float* in = temp_buffer + i;
      float* out = &c.data_buffer_[data_buffer_avail_frames_];
      for (int j = 0; j < got_frames; ++j) {
        *out = *in;
        in += channels_.size();
        ++out;
      }
    }
    delete[] temp_buffer;

    data_buffer_avail_frames_ += got_frames;
  }
  return data_buffer_avail_frames_;
}

void RageSoundReader_SpeedChange::EraseData(int frames_to_delete) {
  ASSERT(frames_to_delete <= data_buffer_avail_frames_);
  ASSERT(data_buffer_avail_frames_ >= frames_to_delete);
  ASSERT(uncorrelated_pos_ >= frames_to_delete);

  int frames_to_move = data_buffer_avail_frames_ - frames_to_delete;
  data_buffer_avail_frames_ -= frames_to_delete;
  uncorrelated_pos_ -= frames_to_delete;
  for (size_t i = 0; i < channels_.size(); ++i) {
    ChannelInfo& c = channels_[i];
    if (frames_to_move) {
      memmove(
          &c.data_buffer_[0], &c.data_buffer_[frames_to_delete],
          frames_to_move * sizeof(float));
    }
    ASSERT(c.correlated_pos_ >= frames_to_delete);
    c.correlated_pos_ -= frames_to_delete;
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
    float advance_frames = GetWindowSizeFrames() * trailing_speed_ratio_;
    advance_frames += error_frames_;
    int trailing_delta_frames = static_cast<int>((advance_frames) + 0.5);
    error_frames_ = advance_frames - trailing_delta_frames;
    uncorrelated_pos_ += trailing_delta_frames;

    pos_ = 0;
  }

  /* Update m_fTrailingSpeedRatio.  Do this after advancing m_iUncorrelatedPos,
   * so changes to m_iUncorrelatedPos line up with GetNextSourceFrame. */
  trailing_speed_ratio_ = speed_ratio_;

  /* We don't need any data before the earlier of m_iUncorrelatedPos or
   * m_iCorrelatedPos. */
  int to_delete = uncorrelated_pos_;
  for (size_t i = 0; i < channels_.size(); ++i) {
    ChannelInfo& c = channels_[i];
    ASSERT(c.correlated_pos_ <= data_buffer_avail_frames_);
    to_delete = std::min(to_delete, c.correlated_pos_);
    // iToDelete = std::min( iToDelete, m_iDataBufferAvailFrames );
  }
  EraseData(to_delete);

  /* Fill as much data as we might need to do the search and use the result. */
  {
    int max_position_needed =
        uncorrelated_pos_ + GetToleranceFrames() + GetWindowSizeFrames();
    for (size_t i = 0; i < channels_.size(); ++i) {
      max_position_needed = std::max(
          max_position_needed,
          channels_[i].correlated_pos_ + GetWindowSizeFrames());
    }

    int got = FillData(max_position_needed);
    if (got < 0) {
      return got;
    }

    if (max_position_needed > data_buffer_avail_frames_) {
      /* We're at EOF.  Flush the remaining data, if any. */
      uncorrelated_pos_ = channels_[0].correlated_pos_;
      return data_buffer_avail_frames_;
    }
  }

  /* Starting at our preferred position (m_iUncorrelatedPos), within
   * GetToleranceFrames(), search for data that looks like the sound we would
   * have continued playing if we kept going from the old position
   * (m_iCorrelatedPos). */
  int correlated_to_match = GetWindowSizeFrames() / 4;
  int uncorrelated_to_match =
      GetToleranceFrames() + correlated_to_match;  // maximum distance to search

  for (size_t i = 0; i < channels_.size(); ++i) {
    ChannelInfo& c = channels_[i];
    ASSERT(c.correlated_pos_ >= 0);
    ASSERT(c.correlated_pos_ < data_buffer_avail_frames_);

    int best = FindClosestMatch(
        &c.data_buffer_[uncorrelated_pos_], uncorrelated_to_match,
        &c.data_buffer_[c.correlated_pos_], correlated_to_match,
        channels_.size());
    c.last_correlated_pos_ = c.correlated_pos_;
    c.correlated_pos_ = best + uncorrelated_pos_;
    ASSERT(
        channels_[i].correlated_pos_ + GetWindowSizeFrames() <=
        data_buffer_avail_frames_);
  }
  return data_buffer_avail_frames_;
}

int RageSoundReader_SpeedChange::GetCursorAvail() const {
  int cursor_avail = GetWindowSizeFrames() - pos_;
  for (size_t i = 0; i < channels_.size(); ++i) {
    int cursor_avail_for_channel =
        (data_buffer_avail_frames_ - channels_[i].correlated_pos_) - pos_;
    cursor_avail = std::min(cursor_avail, cursor_avail_for_channel);
  }

  return cursor_avail;
}

int RageSoundReader_SpeedChange::Read(float* buf, int frames) {
  for (;;) {
    if (data_buffer_avail_frames_ == 0 &&
        trailing_speed_ratio_ == speed_ratio_ && speed_ratio_ == 1.0f) {
      /* Fast path: the buffer is empty, and we're not scaling the audio.  Read
       * directly into the output buffer, to eliminate memory and copying
       * overhead. */
      return source_->Read(buf, frames);
    }

    int cursor_avail = GetCursorAvail();

    if (cursor_avail == 0) {
      int ret = Step();
      if (ret < 0) {
        return ret;
      }
      if (!GetCursorAvail()) {
        return END_OF_FILE;
      }
      continue;
    }

    /* copy GetWindowSizeFrames() from iCorrelatedPos */
    int frames_len = frames;
    int frames_avail = std::min(cursor_avail, frames_len);

    frames -= frames_avail;
    int frames_read = frames_avail;

    int window_size_frames = GetWindowSizeFrames();
    while (frames_avail--) {
      for (size_t i = 0; i < channels_.size(); ++i) {
        ChannelInfo& c = channels_[i];
        float i1 = c.data_buffer_[c.correlated_pos_ + pos_];
        float i2 = c.data_buffer_[c.last_correlated_pos_ + pos_];
        *buf++ = SCALE(pos_, 0, window_size_frames, i2, i1);
      }

      ++pos_;
    }

    return frames_read;
  }
}

/* We prefer to be able to seek precisely, so seeking to a position produces
 * data equal to what you'd get if you read data up to that point.  This filter
 * can't do that, because the exact selection of slices is dependent on the
 * previous selection. */
int RageSoundReader_SpeedChange::SetPosition(int frame) {
  Reset();
  return RageSoundReader_Filter::SetPosition(frame);
}

bool RageSoundReader_SpeedChange::SetProperty(
    const std::string& property, float value) {
  if (property == "Speed") {
    SetSpeedRatio(value);
    return true;
  }

  return RageSoundReader_Filter::SetProperty(property, value);
}

int RageSoundReader_SpeedChange::GetNextSourceFrame() const {
  float ratio = trailing_speed_ratio_;

  int source_frame = RageSoundReader_Filter::GetNextSourceFrame();
  int pos = static_cast<int>((pos_ * ratio) + 0.5);

  source_frame -= data_buffer_avail_frames_;
  source_frame += uncorrelated_pos_ + pos;
  return source_frame;
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
