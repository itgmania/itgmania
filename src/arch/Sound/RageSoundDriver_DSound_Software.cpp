#include "RageSoundDriver_DSound_Software.h"

#include <cstdint>
#include <string>

#include "DSoundHelpers.h"
#include "PrefsManager.h"
#include "RageLog.h"
#include "RageSoundManager.h"
#include "RageUtil.h"
#include "archutils/Win32/ErrorStrings.h"
#include "global.h"

REGISTER_SOUND_DRIVER_CLASS2("DirectSound-sw-legacy", DSound_Software);

static const int kChannels = 2;
static const int kBytesPerFrame = kChannels * 2; /* 16-bit */
static const int kSafeWriteahead = 1024 * 4;     /* in frames */
static int max_writeahead;

/* We'll fill the buffer in chunks this big. */
static const int kNumChunks = 8;
static int chunksize() { return max_writeahead / kNumChunks; }

void RageSoundDriver_DSound_Software::MixerThread() {
  if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL)) {
    if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL)) {
      LOG->Warn(
          werr_ssprintf(GetLastError(), "Failed to set sound thread priority")
              .c_str());
    }
  }

  /* Fill a buffer before we start playing, so we don't play whatever junk is
   * in the buffer. */
  char* locked_buf_init;
  unsigned len_init;
  while (pcm_->get_output_buf(&locked_buf_init, &len_init, chunksize())) {
    memset(locked_buf_init, 0, len_init);
    pcm_->release_output_buf(locked_buf_init, len_init);
  }

  /* Start playing. */
  pcm_->Play();

  while (!shutdown_mixer_thread_) {
    char* locked_buf;
    unsigned len;
    const int64_t play_pos =
        pcm_->GetOutputPosition(); /* must be called before get_output_buf */

    if (!pcm_->get_output_buf(&locked_buf, &len, chunksize())) {
      Sleep(chunksize() * 1000 / sample_rate_);
      continue;
    }

    this->Mix(
        (int16_t*)locked_buf, len / kBytesPerFrame, play_pos,
        pcm_->GetPosition());

    pcm_->release_output_buf(locked_buf, len);
  }

  /* I'm not sure why, but if we don't stop the stream now, then the thread will
   * take 90ms (our buffer size) longer to close. */
  pcm_->Stop();
}

int64_t RageSoundDriver_DSound_Software::GetPosition() const {
  return pcm_->GetPosition();
}

int RageSoundDriver_DSound_Software::MixerThread_start(void* p) {
  ((RageSoundDriver_DSound_Software*)p)->MixerThread();
  return 0;
}

RageSoundDriver_DSound_Software::RageSoundDriver_DSound_Software()
    : pcm_(nullptr), sample_rate_(0), shutdown_mixer_thread_(false) {}

std::string RageSoundDriver_DSound_Software::Init() {
  std::string error = ds_.Init();
  if (error != "") {
    return error;
  }

  /* If we're emulated, we're better off with the WaveOut driver; DS
   * emulation tends to be desynced. */
  if (ds_.IsEmulated()) {
    return "Driver unusable (emulated device)";
  }

  max_writeahead = kSafeWriteahead;
  if (PREFSMAN->sound_write_ahead_) {
    max_writeahead = PREFSMAN->sound_write_ahead_;
  }

  /* Create a DirectSound stream, but don't force it into hardware. */
  pcm_ = new DSoundBuf;
  sample_rate_ = PREFSMAN->sound_preferred_sample_rate_;
  if (sample_rate_ == 0) {
    sample_rate_ = FALLBACK_SAMPLE_RATE;
  }
  // This sample_rate_ (driver's) is then passed as the sample_rate parameter
  // to DSoundBuf::Init()
  error = pcm_->Init(
      ds_, DSoundBuf::HW_DONT_CARE, kChannels, sample_rate_, 16,
      max_writeahead);
  if (error != "") {
    return error;
  }

  LOG->Info("Software mixing at %i hz", sample_rate_);

  StartDecodeThread();

  mixing_thread_.SetName("Mixer thread");
  mixing_thread_.Create(MixerThread_start, this);

  return std::string();
}

RageSoundDriver_DSound_Software::~RageSoundDriver_DSound_Software() {
  /* Signal the mixing thread to quit. */
  if (mixing_thread_.IsCreated()) {
    shutdown_mixer_thread_ = true;
    LOG->Trace("Shutting down mixer thread ...");
    LOG->Flush();
    mixing_thread_.Wait();
    LOG->Trace("Mixer thread shut down.");
    LOG->Flush();
  }

  delete pcm_;
}

void RageSoundDriver_DSound_Software::SetupDecodingThread() {
  if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL)) {
    LOG->Warn(
        werr_ssprintf(GetLastError(), "Failed to set decoding thread priority")
            .c_str());
  }
}

float RageSoundDriver_DSound_Software::GetPlayLatency() const {
  return (1.0f / sample_rate_) * max_writeahead;
}

int RageSoundDriver_DSound_Software::GetSampleRate() const {
  return sample_rate_;
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
