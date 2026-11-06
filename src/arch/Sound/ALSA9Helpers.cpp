#include "ALSA9Helpers.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <string>

#include "ALSA9Dynamic.h"
#include "PrefsManager.h"
#include "RageLog.h"
#include "RageSound.h"
#include "RageSoundConstants.h"
#include "RageUtil.h"
#include "global.h"

/* int err; must be defined before using this macro */
#define ALSA_CHECK(x)                                 \
  if (err < 0) {                                      \
    LOG->Info("ALSA: %s: %s", x, dsnd_strerror(err)); \
    return false;                                     \
  }
#define ALSA_ASSERT(x)                                \
  if (err < 0) {                                      \
    LOG->Warn("ALSA: %s: %s", x, dsnd_strerror(err)); \
  }

bool Alsa9Buf::SetHWParams() {
  int err;

  if (dsnd_pcm_state(pcm_) == SND_PCM_STATE_PREPARED) {
    dsnd_pcm_drop(pcm_);
  }

  if (dsnd_pcm_state(pcm_) != SND_PCM_STATE_OPEN) {
    /* Reset the stream to SND_PCM_STATE_OPEN. */
    err = dsnd_pcm_hw_free(pcm_);
    ALSA_ASSERT("dsnd_pcm_hw_free");
  }
  //	ASSERT_M( dsnd_pcm_state(pcm) == SND_PCM_STATE_OPEN, ssprintf("(%s)",
  // dsnd_pcm_state_name(dsnd_pcm_state(pcm))) );

  /* allocate the hardware parameters structure */
  snd_pcm_hw_params_t* hwparams;
  dsnd_pcm_hw_params_alloca(&hwparams);

  err = dsnd_pcm_hw_params_any(pcm_, hwparams);
  ALSA_CHECK("dsnd_pcm_hw_params_any");

  /* Set to interleaved mmap mode. */
  err = dsnd_pcm_hw_params_set_access(
      pcm_, hwparams, SND_PCM_ACCESS_MMAP_INTERLEAVED);
  ALSA_CHECK("dsnd_pcm_hw_params_set_access");

  /* Set the PCM format: signed 16bit, native endian. */
  err = dsnd_pcm_hw_params_set_format(pcm_, hwparams, SND_PCM_FORMAT_S16);
  ALSA_CHECK("dsnd_pcm_hw_params_set_format");

  /* Set the number of channels. */
  err = dsnd_pcm_hw_params_set_channels(pcm_, hwparams, 2);
  ALSA_CHECK("dsnd_pcm_hw_params_set_channels");

  /* Set the sample rate. */
  err = dsnd_pcm_hw_params_set_rate_near(pcm_, hwparams, &samplerate_, 0);
  ALSA_CHECK("dsnd_pcm_hw_params_set_rate_near");

  /* Set the buffersize to the writeahead, and then copy back the actual value
   * we got. */
  writeahead_ = preferred_writeahead_;
  err = dsnd_pcm_hw_params_set_buffer_size_near(pcm_, hwparams, &writeahead_);
  ALSA_CHECK("dsnd_pcm_hw_params_set_buffer_size_near");

  /* The period size is roughly equivalent to what we call the chunksize. */
  int dir = 0;
  chunksize_ = preferred_chunksize_;
  err = dsnd_pcm_hw_params_set_period_size_near(
      pcm_, hwparams, &chunksize_, &dir);
  ALSA_CHECK("dsnd_pcm_hw_params_set_period_size_near");

  //	LOG->Info("asked for %i period, got %i", chunksize, period_size);

  /* write the hardware parameters to the device */
  err = dsnd_pcm_hw_params(pcm_, hwparams);
  ALSA_CHECK("dsnd_pcm_hw_params");

  return true;
}

bool Alsa9Buf::SetSWParams() {
  snd_pcm_sw_params_t* swparams;
  dsnd_pcm_sw_params_alloca(&swparams);
  dsnd_pcm_sw_params_current(pcm_, swparams);

  int err = dsnd_pcm_sw_params_set_xfer_align(pcm_, swparams, 1);
  ALSA_ASSERT("dsnd_pcm_sw_params_set_xfer_align");

  /* chunksize has been set to the period size.  Set avail_min to the period
   * size, too, so poll() wakes up once per chunk. */
  err = dsnd_pcm_sw_params_set_avail_min(pcm_, swparams, chunksize_);
  ALSA_ASSERT("dsnd_pcm_sw_params_set_avail_min");

  /* If this fails, we might have bound dsnd_pcm_sw_params_set_avail_min to
   * the old SW API. */
  //	ASSERT( err <= 0 );

  /* Disable SND_PCM_STATE_XRUN. */
  snd_pcm_uframes_t boundary = 0;
  err = dsnd_pcm_sw_params_get_boundary(swparams, &boundary);
  ALSA_ASSERT("dsnd_pcm_sw_params_get_boundary");

  err = dsnd_pcm_sw_params_set_stop_threshold(pcm_, swparams, boundary);
  ALSA_ASSERT("dsnd_pcm_sw_params_set_stop_threshold");

  err = dsnd_pcm_sw_params(pcm_, swparams);
  ALSA_ASSERT("dsnd_pcm_sw_params");

  err = dsnd_pcm_prepare(pcm_);
  ALSA_ASSERT("dsnd_pcm_prepare");

  return true;
}

void Alsa9Buf::ErrorHandler(
    const char* file, int line, const char* function, int err, const char* fmt,
    ...) {
  va_list va;
  va_start(va, fmt);
  std::string str = vssprintf(fmt, va);
  va_end(va);

  if (err) {
    str += ssprintf(" (%s)", dsnd_strerror(err));
  }

  /* Annoying: these happen both normally (eg. "out of memory" when allocating
   * too many PCM slots) and abnormally, and there's no way to tell which is
   * which.  I don't want to pollute the warning output. */
  LOG->Trace("ALSA error: %s:%i %s: %s", file, line, function, str.c_str());
}

void Alsa9Buf::InitializeErrorHandler() {
  dsnd_lib_error_set_handler(ErrorHandler);
}

static std::string DeviceName() {
  if (!PREFSMAN->sound_device_.Get().empty()) {
    return PREFSMAN->sound_device_;
  }
  return "default";
}

void Alsa9Buf::GetSoundCardDebugInfo() {
  static bool done = false;
  if (done) {
    return;
  }
  done = true;

  std::ifstream f("/proc/asound/version");
  if (f.good()) {
    std::string version;
    std::getline(f, version);
    LOG->Info("ALSA: %s", version.c_str());
  }

  InitializeErrorHandler();

  int card = -1;
  while (dsnd_card_next(&card) >= 0 && card >= 0) {
    const std::string id = ssprintf("hw:%d", card);
    snd_ctl_t* handle;
    int err;
    err = dsnd_ctl_open(&handle, id.c_str(), 0);
    if (err < 0) {
      LOG->Info(
          "Couldn't open card #%i (\"%s\") to probe: %s", card, id.c_str(),
          dsnd_strerror(err));
      continue;
    }

    snd_ctl_card_info_t* info;
    dsnd_ctl_card_info_alloca(&info);
    err = dsnd_ctl_card_info(handle, info);
    if (err < 0) {
      LOG->Info(
          "Couldn't get card info for card #%i (\"%s\"): %s", card, id.c_str(),
          dsnd_strerror(err));
      dsnd_ctl_close(handle);
      continue;
    }

    int dev = -1;
    while (dsnd_ctl_pcm_next_device(handle, &dev) >= 0 && dev >= 0) {
      snd_pcm_info_t* pcminfo;
      dsnd_pcm_info_alloca(&pcminfo);
      dsnd_pcm_info_set_device(pcminfo, dev);
      dsnd_pcm_info_set_stream(pcminfo, SND_PCM_STREAM_PLAYBACK);

      err = dsnd_ctl_pcm_info(handle, pcminfo);
      if (err < 0) {
        if (err != -ENOENT) {
          LOG->Info(
              "dsnd_ctl_pcm_info(%i) (%s) failed: %s", card, id.c_str(),
              dsnd_strerror(err));
        }
        continue;
      }

      LOG->Info(
          "ALSA Driver: %i: %s [%s], device %i: %s [%s], %i/%i subdevices "
          "avail",
          card, dsnd_ctl_card_info_get_name(info),
          dsnd_ctl_card_info_get_id(info), dev, dsnd_pcm_info_get_id(pcminfo),
          dsnd_pcm_info_get_name(pcminfo),
          dsnd_pcm_info_get_subdevices_avail(pcminfo),
          dsnd_pcm_info_get_subdevices_count(pcminfo));
    }
    dsnd_ctl_close(handle);
  }

  if (card == 0) {
    LOG->Info("No ALSA sound cards were found.");
  }

  if (!PREFSMAN->sound_device_.Get().empty()) {
    LOG->Info(
        "ALSA device overridden to \"%s\"",
        PREFSMAN->sound_device_.Get().c_str());
  }
}

Alsa9Buf::Alsa9Buf() {
  samplerate_ = 44100;
  last_cursor_pos_ = 0;
  preferred_writeahead_ = 8192;
  preferred_chunksize_ = 1024;
  pcm_ = nullptr;
}

std::string Alsa9Buf::Init(int iWriteahead, int iChunkSize, int iSampleRate) {
  preferred_writeahead_ = iWriteahead;
  preferred_chunksize_ = iChunkSize;
  if (iSampleRate == 0) {
    samplerate_ = FALLBACK_SAMPLE_RATE;
  } else {
    samplerate_ = iSampleRate;
  }

  GetSoundCardDebugInfo();

  InitializeErrorHandler();

  /* Open the device. */
  int err;
  err = dsnd_pcm_open(
      &pcm_, DeviceName().c_str(), SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
  if (err < 0) {
    return ssprintf(
        "dsnd_pcm_open(%s): %s", DeviceName().c_str(), dsnd_strerror(err));
  }

  if (!SetHWParams()) {
    CHECKPOINT;
    return "SetHWParams failed";
  }

  SetSWParams();

  LOG->Info("ALSA: Mixing at %ihz", samplerate_);

  if (preferred_writeahead_ != writeahead_) {
    LOG->Info(
        "ALSA: writeahead adjusted from %u to %u",
        (unsigned)preferred_writeahead_, (unsigned)writeahead_);
  }
  if (preferred_chunksize_ != chunksize_) {
    LOG->Info(
        "ALSA: chunksize adjusted from %u to %u",
        (unsigned)preferred_chunksize_, (unsigned)chunksize_);
  }

  return "";
}

Alsa9Buf::~Alsa9Buf() {
  if (pcm_ != nullptr) {
    dsnd_pcm_close(pcm_);
  }
}

/* Don't fill the buffer any more than than "writeahead" frames.  Prefer to
 * write "chunksize" frames at a time.  (These numbers are hints; if the
 * hardware parameters require it, they can be ignored.) */
int Alsa9Buf::GetNumFramesToFill() {
  /* Make sure we can write ahead at least two chunks.  Otherwise, we'll only
   * fill one chunk ahead, and underrun. */
  int ActualWriteahead = std::max(writeahead_, chunksize_ * 2);

  snd_pcm_sframes_t avail_frames = dsnd_pcm_avail_update(pcm_);

  int total_frames = writeahead_;
  if (avail_frames > total_frames) {
    /* underrun */
    const int size = avail_frames - total_frames;
    LOG->Trace("underrun (%i frames)", size);
    int large_skip_threshold = 2 * samplerate_;

    /* For small underruns, ignore them.  We'll return the maximum writeahead
     * and ALSA will just discard the data.  GetPosition will return consistent
     * values during this time, so arrows will continue to scroll smoothly until
     * the music catches up. */
    if (size >= large_skip_threshold) {
      /* It's a large skip.  Catch up.  If we fall too far behind, the sound
       * thread will be decoding as fast as it can, which will steal too many
       * cycles from the rendering thread. */
      dsnd_pcm_forward(pcm_, size);
    }
  }

  if (avail_frames < 0) {
    avail_frames = dsnd_pcm_avail_update(pcm_);
  }

  if (avail_frames < 0) {
    LOG->Trace(
        "RageSoundDriver_ALSA9::GetData: dsnd_pcm_avail_update: %s",
        dsnd_strerror(avail_frames));
    return 0;
  }

  /* Number of frames that have data: */
  const snd_pcm_sframes_t filled_frames =
      std::max(0l, total_frames - avail_frames);

  /* Number of frames that don't have data, that are within the writeahead: */
  snd_pcm_sframes_t unfilled_frames = std::clamp(
      ActualWriteahead - filled_frames, 0l,
      (snd_pcm_sframes_t)ActualWriteahead);

  //	LOG->Trace( "total_fr: %i; avail_fr: %i; filled_fr: %i; ActualWr %i;
  // chunksize %i; unfilled_frames %i ", 			total_frames,
  // avail_frames, filled_frames, ActualWriteahead, chunksize, unfilled_frames
  // );

  /* If we have less than a chunk empty, don't fill at all.  Otherwise, we'll
   * spend a lot of CPU filling in partial chunks, instead of waiting for some
   * sound to play and then filling a whole chunk at once. */
  if (unfilled_frames < (int)chunksize_) {
    return 0;
  }

  return chunksize_;
}

bool Alsa9Buf::WaitUntilFramesCanBeFilled(int timeout_ms) {
  int err = dsnd_pcm_wait(pcm_, timeout_ms);
  /* EINTR is normal; don't warn. */
  if (err == -EINTR) {
    return false;
  }
  ALSA_ASSERT("snd_pcm_wait");

  return err == 1;
}

void Alsa9Buf::Write(const int16_t* buffer, int frames) {
  /* We should be able to write it all.  If we don't, treat it as an error. */
  int wrote;
  do {
    wrote = dsnd_pcm_mmap_writei(pcm_, (const char*)buffer, frames);
  } while (wrote == -EAGAIN);

  if (wrote < 0) {
    LOG->Trace(
        "RageSoundDriver_ALSA9::GetData: dsnd_pcm_mmap_writei: %s (%i)",
        dsnd_strerror(wrote), wrote);
    return;
  }

  last_cursor_pos_ += wrote;
  if (wrote < frames) {
    LOG->Trace("Couldn't write whole buffer? (%i < %i)", wrote, frames);
  }
}

int64_t Alsa9Buf::GetPosition() const {
  if (dsnd_pcm_state(pcm_) == SND_PCM_STATE_PREPARED) {
    return last_cursor_pos_;
  }

  dsnd_pcm_hwsync(pcm_);

  /* delay is returned in frames */
  snd_pcm_sframes_t delay;
  int err = dsnd_pcm_delay(pcm_, &delay);
  ALSA_ASSERT("dsnd_pcm_delay");

  return last_cursor_pos_ - delay;
}

/*
 * (c) 2002-2004 Glenn Maynard, Aaron VonderHaar
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
