#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "Platform.h"
#include "RageLog.h"
#include "RageSound.h"
#include "RageSoundDriver.h"
#include "RageSoundMixBuffer.h"
#include "RageSoundReader.h"
#include "RageThreads.h"
#include "RageUtil.h"
#include "global.h"

#if !defined(_WIN32)
#include <unistd.h>
#endif

#if defined(_MSC_VER) && !defined(PRIu64)
#define PRIu64 "I64u"
#endif
/* Note(sukibaby): If you need to activate a log method
 * below, and it's casting a variable to int instead of
 * using the correct format specifier, please modify it
 * to use PRId64 for int64_t, or PRIu64 for uint64_t */

static const int channels = 2;

static int frames_to_buffer;

/* 512 is about 10ms, which is big enough for the tolerance of most schedulers.
 */
static int chunksize() { return 512; }

static int underruns = 0, logged_underruns = 0;

// Maps stream (hardware) frames to the equivalent number of source frames.
// In other words, this converts an offset measured in hardware frames into
// the matching offset matched in original source (stream) frames.
static int64_t StreamFramesToSourceFrames(
    int iFrames, float fSourceToStreamRatio) {
  return static_cast<int64_t>(std::llround(
      static_cast<double>(iFrames) *
      static_cast<double>(fSourceToStreamRatio)));
}

RageSoundDriver::Sound::Sound() {
  sound_ = nullptr;
  state_ = AVAILABLE;
  paused_ = false;
}

void RageSoundDriver::Sound::Allocate(int iFrames) {
  /* Reserve enough blocks in the buffer to hold the buffer.  Add one, to
   * account for the fact that we may have a partial block due to a previous
   * Mix() call. */
  const int iFramesPerBlock = samples_per_block / channels;
  const int iBlocksToPrebuffer = iFrames / iFramesPerBlock;
  buffer_.reserve(iBlocksToPrebuffer + 1);
  mixed_position_queue_.reserve(64);
  playback_history_.clear();
}

void RageSoundDriver::Sound::Deallocate() {
  buffer_.reserve(0);
  mixed_position_queue_.reserve(0);
  playback_history_.clear();
}

int RageSoundDriver::DecodeThread_start(void* p) {
  ((RageSoundDriver*)p)->DecodeThread();
  return 0;
}

RageSoundMixBuffer& RageSoundDriver::MixIntoBuffer(
    int iFrames, int64_t iFrameNumber, int64_t iCurrentFrame) {
  ASSERT_M(
      decode_thread_.IsCreated(),
      "RageSoundDriver::StartDecodeThread() was never called");

  static RageSoundMixBuffer mix;

  for (unsigned i = 0; i < ARRAYLEN(sounds_); ++i) {
    /* s.sound_ can not safely be accessed from here. */
    Sound& s = sounds_[i];
    if (s.state_ == Sound::HALTING) {
      /* This indicates that this stream can be reused. */
      s.paused_ = false;
      s.state_ = Sound::STOPPED;

      //			LOG->Trace("set %p from HALTING to STOPPED",
      // sounds_[i].sound_);
      continue;
    }

    if (s.state_ != Sound::STOPPING && s.state_ != Sound::PLAYING) {
      continue;
    }

    /* STOPPING or PLAYING.  Read sound data. */
    if (sounds_[i].paused_) {
      continue;
    }

    int iGotFrames = 0;
    int iFramesLeft = iFrames;

    /* Does the sound have a start time? */
    if (!s.start_time_.IsZero() && iCurrentFrame != -1) {
      /* If the sound is supposed to start at a time past this buffer, insert
       * silence. */
      const int64_t iFramesUntilThisBuffer = iFrameNumber - iCurrentFrame;
      const float fSecondsBeforeStart = -s.start_time_.Ago();
      const int64_t iFramesBeforeStart =
          int64_t(fSecondsBeforeStart * GetSampleRate());
      const int iSilentFramesInThisBuffer = std::clamp(
          int(iFramesBeforeStart - iFramesUntilThisBuffer), 0, iFramesLeft);

      iGotFrames += iSilentFramesInThisBuffer;
      iFramesLeft -= iSilentFramesInThisBuffer;

      /* If we didn't completely fill the buffer, then we've written all of the
       * silence. */
      if (iFramesLeft) {
        s.start_time_.SetZero();
      }
    }

    /* Fill actual data. */
    sound_block* p[2];
    unsigned pSize[2];
    s.buffer_.get_read_pointers(p, pSize);

    while (iFramesLeft && pSize[0]) {
      if (!p[0]->frames_in_buffer_) {
        /* We've processed all of the sound in this block.  Mark it read. */
        s.buffer_.advance_read_pointer(1);
        ++p[0];
        --pSize[0];

        /* If we have more data in p[0], keep going. */
        if (pSize[0]) {
          continue;  // more data
        }

        /* We've used up p[0].  Try p[1]. */
        std::swap(p[0], p[1]);
        std::swap(pSize[0], pSize[1]);
        continue;
      }

      /* Note that, until we call advance_read_pointer, we can safely write to
       * p[0]. */
      ASSERT(p[0]->current_position_span_ < p[0]->position_span_count_);
      MixingPositionInfo& pSpan =
          p[0]->position_spans_[p[0]->current_position_span_];
      const int remainingFrames = pSpan.frames_ - pSpan.frames_consumed_;
      ASSERT(remainingFrames > 0);

      const int frames_to_read = std::min(
          iFramesLeft, std::min(p[0]->frames_in_buffer_, remainingFrames));
      mix.SetWriteOffset(iGotFrames * channels);
      mix.write(p[0]->buffer_next_, frames_to_read * channels);

      {
        Sound::PlaybackPositionInfo pos;
        pos.hardware_frame_ = iFrameNumber + iGotFrames;
        pos.source_frame_ =
            pSpan.source_frame_ +
            StreamFramesToSourceFrames(
                pSpan.frames_consumed_, pSpan.source_to_stream_ratio_);
        pos.frames_ = frames_to_read;
        pos.source_to_stream_ratio_ = pSpan.source_to_stream_ratio_;

        s.mixed_position_queue_.write(&pos, 1);
      }

      p[0]->buffer_next_ += frames_to_read * channels;
      p[0]->frames_in_buffer_ -= frames_to_read;
      pSpan.frames_consumed_ += frames_to_read;
      if (pSpan.frames_consumed_ == pSpan.frames_) {
        ++p[0]->current_position_span_;
      }

      //			LOG->Trace( "incr fr rd += %i (state %i) (%p)",
      //				(int) frames_to_read, s.state_,
      // s.sound_ );

      iGotFrames += frames_to_read;
      iFramesLeft -= frames_to_read;
    }

    /* If we don't have enough to fill the buffer, we've underrun. */
    if (iGotFrames < iFrames && s.state_ == Sound::PLAYING) {
      ++underruns;
    }
  }

  return mix;
}

void RageSoundDriver::Mix(
    int16_t* pBuf, int iFrames, int64_t iFrameNumber, int64_t iCurrentFrame) {
  memset(pBuf, 0, iFrames * channels * sizeof(int16_t));
  MixIntoBuffer(iFrames, iFrameNumber, iCurrentFrame).read(pBuf);
}

void RageSoundDriver::Mix(
    float* pBuf, int iFrames, int64_t iFrameNumber, int64_t iCurrentFrame) {
  memset(pBuf, 0, iFrames * channels * sizeof(float));
  MixIntoBuffer(iFrames, iFrameNumber, iCurrentFrame).read(pBuf);
}

void RageSoundDriver::MixDeinterlaced(
    float** pBufs, int iChannels, int iFrames, int64_t iFrameNumber,
    int64_t iCurrentFrame) {
  for (int i = 0; i < iChannels; ++i) {
    memset(pBufs[i], 0, iFrames * sizeof(float));
  }
  MixIntoBuffer(iFrames, iFrameNumber, iCurrentFrame)
      .read_deinterlace(pBufs, iChannels);
}

void RageSoundDriver::DecodeThread() {
  SetupDecodingThread();

  while (!shutdown_decode_thread_) {
    /* Fill each playing sound, round-robin. */
    {
      int iSampleRate = GetSampleRate();
      ASSERT_M(iSampleRate > 0, ssprintf("%i", iSampleRate));
      int iUsecs = 1000000 * chunksize() / iSampleRate;
      usleep(iUsecs);
    }

    LockMut(mutex_);
    //		LOG->Trace("begin mix");

    for (unsigned i = 0; i < ARRAYLEN(sounds_); ++i) {
      if (sounds_[i].state_ != Sound::PLAYING) {
        continue;
      }

      Sound* pSound = &sounds_[i];

      CHECKPOINT_M("Processing the sound while buffers are available.");
      while (pSound->buffer_.num_writable()) {
        int iWrote = GetDataForSound(*pSound);
        if (iWrote == RageSoundReader::WOULD_BLOCK) {
          break;
        }
        if (iWrote < 0) {
          /* This sound is finishing. */
          pSound->state_ = Sound::STOPPING;
          break;
          //					LOG->Trace("mixer: (#%i) eof
          //(%p)", i, pSound->sound_ );
        }
      }
    }
    //		LOG->Trace("end mix");
  }
}

/* Buffer a block of sound data for the given sound.  Return the number of
 * frames buffered, or a RageSoundReader return code. */
int RageSoundDriver::GetDataForSound(Sound& s) {
  sound_block* p[2];
  unsigned psize[2];
  s.buffer_.get_write_pointers(p, psize);

  /* If we have no open buffer slot, we have a buffer overflow. */
  ASSERT(psize[0] > 0);

  sound_block* pBlock = p[0];
  int size = ARRAYLEN(pBlock->buffer_) / channels;
  RageSoundMixPosition positions[samples_per_block];
  int iPositionCount = 0;
  int iRet = s.sound_->GetDataToPlay(
      pBlock->buffer_, size, positions, ARRAYLEN(positions), iPositionCount,
      pBlock->frames_in_buffer_);
  if (iRet > 0) {
    ASSERT(iPositionCount > 0);
    pBlock->buffer_next_ = pBlock->buffer_;
    pBlock->position_span_count_ = iPositionCount;
    pBlock->current_position_span_ = 0;
    for (int i = 0; i < iPositionCount; ++i) {
      pBlock->position_spans_[i].source_frame_ = positions[i].source_frame_;
      pBlock->position_spans_[i].frames_ = positions[i].frames_;
      pBlock->position_spans_[i].source_to_stream_ratio_ =
          positions[i].source_to_stream_ratio_;
      pBlock->position_spans_[i].frames_consumed_ = 0;
    }
    s.buffer_.advance_write_pointer(1);
  } else {
    pBlock->position_span_count_ = 0;
    pBlock->current_position_span_ = 0;
  }

  //	LOG->Trace( "incr fr wr %i (state %i) (%p)",
  //		(int) pBlock->frames_in_buffer_, s.state_, s.sound_ );

  return iRet;
}

void RageSoundDriver::PlaybackQueueDrain(Sound& s) {
  Sound::PlaybackPositionInfo position;
  while (s.mixed_position_queue_.read(&position, 1)) {
    if (!s.playback_history_.empty()) {
      Sound::PlaybackPositionInfo& previous = s.playback_history_.back();
      const int64_t iExpectedHardwareFrame =
          previous.hardware_frame_ + previous.frames_;
      const int64_t iExpectedSourceFrame =
          previous.source_frame_ +
          StreamFramesToSourceFrames(
              previous.frames_, previous.source_to_stream_ratio_);
      if (previous.source_to_stream_ratio_ ==
              position.source_to_stream_ratio_ &&
          iExpectedHardwareFrame == position.hardware_frame_ &&
          iExpectedSourceFrame == position.source_frame_) {
        previous.frames_ += position.frames_;
        continue;
      }
    }

    s.playback_history_.push_back(position);
  }
}

void RageSoundDriver::PlaybackHistoryCleanup(
    Sound& s, int64_t iCurrentHardwareFrame) {
  while (s.playback_history_.size() > 1) {
    if (s.playback_history_[1].hardware_frame_ > iCurrentHardwareFrame) {
      break;
    }
    s.playback_history_.pop_front();
  }
}

bool RageSoundDriver::GetSourceFrameForHardwareFrame(
    const Sound& s, int64_t iHardwareFrame, int& iSourceFrame) const {
  if (s.playback_history_.empty()) {
    return false;
  }

  const Sound::PlaybackPositionInfo* pClosest = nullptr;
  for (const Sound::PlaybackPositionInfo& position : s.playback_history_) {
    if (iHardwareFrame < position.hardware_frame_) {
      break;
    }

    pClosest = &position;
    if (iHardwareFrame < position.hardware_frame_ + position.frames_) {
      iSourceFrame = static_cast<int>(
          position.source_frame_ +
          StreamFramesToSourceFrames(
              static_cast<int>(iHardwareFrame - position.hardware_frame_),
              position.source_to_stream_ratio_));
      return true;
    }
  }

  if (pClosest == nullptr) {
    return false;
  }

  iSourceFrame = static_cast<int>(
      pClosest->source_frame_ +
      StreamFramesToSourceFrames(
          pClosest->frames_, pClosest->source_to_stream_ratio_));
  return true;
}

bool RageSoundDriver::GetPlayingPosition(
    const RageSoundBase* pSound, int& iSourceFrame, RageTimer* pTimer) {
  const int64_t iCurrentHardwareFrame = GetHardwareFrame(pTimer);

  LockMut(mutex_);
  for (unsigned i = 0; i < ARRAYLEN(sounds_); ++i) {
    Sound& sound = sounds_[i];
    if (sound.state_ == Sound::AVAILABLE || sound.sound_ != pSound) {
      continue;
    }

    PlaybackQueueDrain(sound);
    PlaybackHistoryCleanup(sound, iCurrentHardwareFrame);
    return GetSourceFrameForHardwareFrame(
        sound, iCurrentHardwareFrame, iSourceFrame);
  }

  return false;
}

void RageSoundDriver::Update() {
  const int64_t iCurrentHardwareFrame = GetHardwareFrame(nullptr);

  mutex_.Lock();
  for (unsigned i = 0; i < ARRAYLEN(sounds_); ++i) {
    PlaybackQueueDrain(sounds_[i]);
    PlaybackHistoryCleanup(sounds_[i], iCurrentHardwareFrame);

    switch (sounds_[i].state_) {
      case Sound::STOPPED:
        sounds_[i].Deallocate();
        sounds_[i].state_ = Sound::AVAILABLE;
        continue;
      case Sound::STOPPING:
        break;
      default:
        continue;
    }

    if (sounds_[i].buffer_.num_readable() != 0) {
      continue;
    }

    //		LOG->Trace("finishing sound %i", i);

    int iSourceFrame = -1;
    GetSourceFrameForHardwareFrame(
        sounds_[i], iCurrentHardwareFrame, iSourceFrame);
    sounds_[i].sound_->SoundIsFinishedPlaying(iSourceFrame);
    sounds_[i].sound_ = nullptr;

    /* This sound is done.  Set it to HALTING, since the mixer thread might
     * be accessing it; it'll change it back to STOPPED once it's ready to
     * be used again. */
    sounds_[i].state_ = Sound::HALTING;
    //		LOG->Trace("set (#%i) %p from STOPPING to HALTING", i,
    // sounds_[i].sound_);
  }

  constexpr uint64_t iUsecs = 1000000;
  static uint64_t fNextUsecs = 0;
  if (RageTimer::GetTimeSinceStartMicroseconds() >= fNextUsecs) {
    /* Lockless: only Mix() can write to underruns. */
    int current_underruns = underruns;
    if (current_underruns > logged_underruns) {
      LOG->MapLog(
          "GenericMixingUnderruns", "Mixing underruns: %i",
          current_underruns - logged_underruns);
      LOG->Trace("Mixing underruns: %i", current_underruns - logged_underruns);
      logged_underruns = current_underruns;

      /* Don't log again for at least a second, or we'll burst output
       * and possibly cause more underruns. */
      fNextUsecs = RageTimer::GetTimeSinceStartMicroseconds() + iUsecs;
    }
  }

  mutex_.Unlock();
}

void RageSoundDriver::StartMixing(RageSoundBase* pSound) {
  /* Lock available sounds_[], and reserve a slot. */
  sound_list_mutex_.Lock();

  unsigned i;
  for (i = 0; i < ARRAYLEN(sounds_); ++i) {
    if (sounds_[i].state_ == Sound::AVAILABLE) {
      break;
    }
  }
  if (i == ARRAYLEN(sounds_)) {
    sound_list_mutex_.Unlock();
    return;
  }

  Sound& s = sounds_[i];
  s.state_ = Sound::BUFFERING;

  /* We've reserved our slot; we can safely unlock now.  Don't hold onto it
   * longer than needed, since prebuffering might take some time. */
  sound_list_mutex_.Unlock();

  s.sound_ = pSound;
  s.start_time_ = pSound->GetStartTime();
  s.buffer_.clear();
  s.mixed_position_queue_.clear();
  s.playback_history_.clear();

  /* Initialize the sound buffer. */
  int BufferSize = frames_to_buffer;

  s.Allocate(BufferSize);

  //	LOG->Trace("StartMixing(%s) (%p)",
  // s.sound_->GetLoadedFilePath().c_str(), s.sound_ );

  /* Prebuffer some frames before changing the sound to PLAYING. */
  while (s.buffer_.num_writable()) {
    //		LOG->Trace("StartMixing: (#%i) buffering %i (%i writable) (%p)",
    // i, (int) frames_to_buffer, s.buffer.num_writable(), s.sound_ );
    int iWrote = GetDataForSound(s);
    if (iWrote < 0) {
      break;
    }
  }

  s.state_ = Sound::PLAYING;

  //	LOG->Trace("StartMixing: (#%i) finished prebuffering(%s) (%p)", i,
  // s.sound_->GetLoadedFilePath().c_str(), s.sound_ );
}

void RageSoundDriver::StopMixing(RageSoundBase* pSound) {
  /* Lock, to make sure the decoder thread isn't running on this sound while we
   * do this. */
  mutex_.Lock();

  const int64_t iCurrentHardwareFrame = GetHardwareFrame(nullptr);

  /* Find the sound. */
  unsigned i;
  for (i = 0; i < ARRAYLEN(sounds_); ++i) {
    if (sounds_[i].state_ != Sound::AVAILABLE && sounds_[i].sound_ == pSound) {
      break;
    }
  }
  if (i == ARRAYLEN(sounds_)) {
    mutex_.Unlock();
    LOG->Trace("not stopping a sound because it's not playing");
    return;
  }

  /* If we're already in STOPPED, there's nothing to do. */
  if (sounds_[i].state_ == Sound::STOPPED) {
    mutex_.Unlock();
    LOG->Trace("not stopping a sound because it's already in STOPPED");
    return;
  }

  //	LOG->Trace("StopMixing: set %p (%s) to HALTING", sounds_[i].sound_,
  // sounds_[i].sound_->GetLoadedFilePath().c_str());

  PlaybackQueueDrain(sounds_[i]);
  PlaybackHistoryCleanup(sounds_[i], iCurrentHardwareFrame);
  int iSourceFrame = -1;
  GetSourceFrameForHardwareFrame(
      sounds_[i], iCurrentHardwareFrame, iSourceFrame);

  /* Tell the mixing thread to flush the buffer.  We don't have to worry about
   * the decoding thread, since we've locked mutex_. */
  sounds_[i].state_ = Sound::HALTING;

  /* Invalidate the sound_ pointer to guarantee we don't make any further
   * references to it.  Once this call returns, the sound may no longer exist.
   */
  sounds_[i].sound_ = nullptr;
  //	LOG->Trace("end StopMixing");

  mutex_.Unlock();

  pSound->SoundIsFinishedPlaying(iSourceFrame);
}

bool RageSoundDriver::PauseMixing(RageSoundBase* pSound, bool bStop) {
  LockMut(mutex_);

  /* Find the sound. */
  unsigned i;
  for (i = 0; i < ARRAYLEN(sounds_); ++i) {
    if (sounds_[i].state_ != Sound::AVAILABLE && sounds_[i].sound_ == pSound) {
      break;
    }
  }

  /* A sound can be paused in PLAYING or STOPPING.  (STOPPING means the sound
   * has been decoded to the end, and we're waiting for that data to finish, so
   * externally it looks and acts like PLAYING.) */
  if (i == ARRAYLEN(sounds_) || (sounds_[i].state_ != Sound::PLAYING &&
                                 sounds_[i].state_ != Sound::STOPPING)) {
    LOG->Trace("not pausing a sound because it's not playing");
    return false;
  }

  sounds_[i].paused_ = bStop;

  return true;
}

void RageSoundDriver::StartDecodeThread() {
  ASSERT(!decode_thread_.IsCreated());

  decode_thread_.Create(DecodeThread_start, this);
}

void RageSoundDriver::SetDecodeBufferSize(int iFrames) {
  ASSERT(!decode_thread_.IsCreated());

  frames_to_buffer = iFrames;
}

RageSoundDriver::RageSoundDriver()
    : mutex_("RageSoundDriver"), sound_list_mutex_("SoundListMutex") {
  shutdown_decode_thread_ = false;
  max_hardware_frame_ = 0;
  SetDecodeBufferSize(4096);
  decode_thread_.SetName("Decode thread");
}

RageSoundDriver::~RageSoundDriver() {
  /* Signal the decoding thread to quit. */
  if (decode_thread_.IsCreated()) {
    shutdown_decode_thread_ = true;
    LOG->Trace("Shutting down decode thread ...");
    LOG->Flush();
    decode_thread_.Wait();
    LOG->Trace("Decode thread shut down.");
    LOG->Flush();
  }
}

int64_t RageSoundDriver::ClampHardwareFrame(int64_t iHardwareFrame) const {
  /* It's sometimes possible for the hardware position to move backwards,
   * usually on underrun.  We can try to prevent this in each driver, but it's
   * an obscure error, so let's clamp the result here instead. */
  if (iHardwareFrame < max_hardware_frame_) {
    /* Clamp the output to one per second, so one underruns don't cascade due to
     * output spam. */
    static int64_t lastTime = 0;
    int64_t currentTime = RageTimer::GetTimeSinceStartMicroseconds();
    if (lastTime == 0 || (currentTime - lastTime) > 1000000) {
      LOG->Trace(
          "RageSoundDriver: driver returned a lesser position (%" PRId64
          " < %" PRId64 ")",
          iHardwareFrame, max_hardware_frame_);
      lastTime = currentTime;
    }
    return max_hardware_frame_;
  }
  if (iHardwareFrame > max_hardware_frame_) {
    max_hardware_frame_ = iHardwareFrame;
  }
  return max_hardware_frame_;
}

int64_t RageSoundDriver::GetHardwareFrame(
    RageTimer* pTimestamp = nullptr) const {
  if (pTimestamp == nullptr) {
    return ClampHardwareFrame(GetPosition());
  }

  /*
   * We may have unpredictable scheduling delays between updating the timestamp
   * and reading the sound position.  If we're preempted while doing this and
   * it may have caused the timestamp to not match the returned time, retry.
   *
   * As a failsafe, only allow a few attempts.  If this has to try more than
   * a few times, then probably we have thread contention that's causing more
   * severe performance problems, anyway.
   */
  int iTries = 3;
  int64_t iPositionFrames;
  uint64_t iStartTime;
  const uint64_t iThreshold = 2000ULL;

  do {
    iStartTime = RageTimer::GetTimeSinceStartMicroseconds();
    iPositionFrames = GetPosition();
    pTimestamp->Touch();
    uint64_t elapsedTime =
        RageTimer::GetTimeSinceStartMicroseconds() - iStartTime;
    if (elapsedTime <= iThreshold) {
      break;
    }
  } while (--iTries);

  if (iTries == 0) {
    static bool bLogged = false;
    if (!bLogged) {
      bLogged = true;
      LOG->Warn("RageSoundDriver::GetHardwareFrame: too many tries");
    }
  }

  return ClampHardwareFrame(iPositionFrames);
}

/*
 * (c) 2002-2004 Glenn Maynard
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
