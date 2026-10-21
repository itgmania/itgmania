#include "ALSA9Helpers.h"

#include <unistd.h>

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

  if (dsnd_pcm_state(pcm) == SND_PCM_STATE_PREPARED) {
    dsnd_pcm_drop(pcm);
  }

  if (dsnd_pcm_state(pcm) != SND_PCM_STATE_OPEN) {
    /* Reset the stream to SND_PCM_STATE_OPEN. */
    err = dsnd_pcm_hw_free(pcm);
    ALSA_ASSERT("dsnd_pcm_hw_free");
  }
  //	ASSERT_M( dsnd_pcm_state(pcm) == SND_PCM_STATE_OPEN, ssprintf("(%s)",
  // dsnd_pcm_state_name(dsnd_pcm_state(pcm))) );

  /* allocate the hardware parameters structure */
  snd_pcm_hw_params_t* hwparams;
  dsnd_pcm_hw_params_alloca(&hwparams);

  err = dsnd_pcm_hw_params_any(pcm, hwparams);
  ALSA_CHECK("dsnd_pcm_hw_params_any");

  /* Set to interleaved mmap mode. */
  err = dsnd_pcm_hw_params_set_access(
      pcm, hwparams, SND_PCM_ACCESS_MMAP_INTERLEAVED);
  ALSA_CHECK("dsnd_pcm_hw_params_set_access");

  /* Set the PCM format: signed 16bit, native endian. */
  err = dsnd_pcm_hw_params_set_format(pcm, hwparams, SND_PCM_FORMAT_S16);
  ALSA_CHECK("dsnd_pcm_hw_params_set_format");

  /* Set the number of channels. */
  err = dsnd_pcm_hw_params_set_channels(pcm, hwparams, 2);
  ALSA_CHECK("dsnd_pcm_hw_params_set_channels");

  /* Set the sample rate. */
  err = dsnd_pcm_hw_params_set_rate_near(pcm, hwparams, &samplerate, 0);
  ALSA_CHECK("dsnd_pcm_hw_params_set_rate_near");

  /* Set the buffersize to the writeahead, and then copy back the actual value
   * we got. */
  writeahead = preferred_writeahead;
  err = dsnd_pcm_hw_params_set_buffer_size_near(pcm, hwparams, &writeahead);
  ALSA_CHECK("dsnd_pcm_hw_params_set_buffer_size_near");

  /* The period size is roughly equivalent to what we call the chunksize. */
  int dir = 0;
  chunksize = preferred_chunksize;
  err =
      dsnd_pcm_hw_params_set_period_size_near(pcm, hwparams, &chunksize, &dir);
  ALSA_CHECK("dsnd_pcm_hw_params_set_period_size_near");

  //	LOG->Info("asked for %i period, got %i", chunksize, period_size);

  /* write the hardware parameters to the device */
  err = dsnd_pcm_hw_params(pcm, hwparams);
  ALSA_CHECK("dsnd_pcm_hw_params");

  return true;
}

bool Alsa9Buf::SetSWParams() {
  snd_pcm_sw_params_t* swparams;
  dsnd_pcm_sw_params_alloca(&swparams);
  dsnd_pcm_sw_params_current(pcm, swparams);

  int err = dsnd_pcm_sw_params_set_xfer_align(pcm, swparams, 1);
  ALSA_ASSERT("dsnd_pcm_sw_params_set_xfer_align");

  /* chunksize has been set to the period size.  Set avail_min to the period
   * size, too, so poll() wakes up once per chunk. */
  err = dsnd_pcm_sw_params_set_avail_min(pcm, swparams, chunksize);
  ALSA_ASSERT("dsnd_pcm_sw_params_set_avail_min");

  /* If this fails, we might have bound dsnd_pcm_sw_params_set_avail_min to
   * the old SW API. */
  //	ASSERT( err <= 0 );

  /* Disable SND_PCM_STATE_XRUN. */
  snd_pcm_uframes_t boundary = 0;
  err = dsnd_pcm_sw_params_get_boundary(swparams, &boundary);
  ALSA_ASSERT("dsnd_pcm_sw_params_get_boundary");

  err = dsnd_pcm_sw_params_set_stop_threshold(pcm, swparams, boundary);
  ALSA_ASSERT("dsnd_pcm_sw_params_set_stop_threshold");

  err = dsnd_pcm_sw_params(pcm, swparams);
  ALSA_ASSERT("dsnd_pcm_sw_params");

  /* Timestamp pointer updates, so GetPosition knows when the position it reads
   * was reached. Separate from the above, so a refusal costs only that. */
  tstamp_clock = -1;
  snd_pcm_tstamp_type_t tstamp_type;
  dsnd_pcm_sw_params_current(pcm, swparams);
  if (dsnd_pcm_sw_params_get_tstamp_type != nullptr &&
      dsnd_pcm_sw_params_set_tstamp_mode(
          pcm, swparams, SND_PCM_TSTAMP_ENABLE) >= 0 &&
      dsnd_pcm_sw_params(pcm, swparams) >= 0 &&
      dsnd_pcm_sw_params_get_tstamp_type(swparams, &tstamp_type) >= 0) {
    switch (tstamp_type) {
      case SND_PCM_TSTAMP_TYPE_GETTIMEOFDAY:
        tstamp_clock = CLOCK_REALTIME;
        break;
      case SND_PCM_TSTAMP_TYPE_MONOTONIC:
        tstamp_clock = CLOCK_MONOTONIC;
        break;
      case SND_PCM_TSTAMP_TYPE_MONOTONIC_RAW:
        tstamp_clock = CLOCK_MONOTONIC_RAW;
        break;
    }
  }

  err = dsnd_pcm_prepare(pcm);
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
  if (!PREFSMAN->m_iSoundDevice.Get().empty()) {
    return PREFSMAN->m_iSoundDevice;
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

  if (!PREFSMAN->m_iSoundDevice.Get().empty()) {
    LOG->Info(
        "ALSA device overridden to \"%s\"",
        PREFSMAN->m_iSoundDevice.Get().c_str());
  }
}

Alsa9Buf::Alsa9Buf() {
  samplerate = 44100;
  last_cursor_pos = 0;
  preferred_writeahead = 8192;
  preferred_chunksize = 1024;
  pcm = nullptr;
  tstamp_clock = -1;
  max_extrapolation_ns = 0;
}

std::string Alsa9Buf::Init(int iWriteahead, int iChunkSize, int iSampleRate) {
  preferred_writeahead = iWriteahead;
  preferred_chunksize = iChunkSize;
  if (iSampleRate == 0) {
    samplerate = FALLBACK_SAMPLE_RATE;
  } else {
    samplerate = iSampleRate;
  }

  GetSoundCardDebugInfo();

  InitializeErrorHandler();

  /* Open the device. */
  int err;
  err = dsnd_pcm_open(
      &pcm, DeviceName().c_str(), SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
  if (err < 0) {
    return ssprintf(
        "dsnd_pcm_open(%s): %s", DeviceName().c_str(), dsnd_strerror(err));
  }

  if (!SetHWParams()) {
    CHECKPOINT;
    return "SetHWParams failed";
  }

  SetSWParams();

  /* The pointer moves at least once a period; past two, the device is stuck
   * and the position should stop too. */
  max_extrapolation_ns =
      std::max<int64_t>(2000000000LL * chunksize / samplerate, 5000000);

  LOG->Info("ALSA: Mixing at %ihz", samplerate);

  if (preferred_writeahead != writeahead) {
    LOG->Info(
        "ALSA: writeahead adjusted from %u to %u",
        (unsigned)preferred_writeahead, (unsigned)writeahead);
  }
  if (preferred_chunksize != chunksize) {
    LOG->Info(
        "ALSA: chunksize adjusted from %u to %u", (unsigned)preferred_chunksize,
        (unsigned)chunksize);
  }

  return "";
}

Alsa9Buf::~Alsa9Buf() {
  if (pcm != nullptr) {
    dsnd_pcm_close(pcm);
  }
}

/* Don't fill the buffer any more than than "writeahead" frames.  Prefer to
 * write "chunksize" frames at a time.  (These numbers are hints; if the
 * hardware parameters require it, they can be ignored.) */
int Alsa9Buf::GetNumFramesToFill() {
  /* Make sure we can write ahead at least two chunks.  Otherwise, we'll only
   * fill one chunk ahead, and underrun. */
  int ActualWriteahead = std::max(writeahead, chunksize * 2);

  snd_pcm_sframes_t avail_frames = dsnd_pcm_avail_update(pcm);

  if (avail_frames < 0 && Recover(avail_frames)) {
    avail_frames = dsnd_pcm_avail_update(pcm);
  }

  int total_frames = writeahead;
  if (avail_frames > total_frames) {
    /* underrun */
    const int size = avail_frames - total_frames;
    LOG->Trace("underrun (%i frames)", size);
    int large_skip_threshold = 2 * samplerate;

    /* For small underruns, ignore them.  We'll return the maximum writeahead
     * and ALSA will just discard the data.  GetPosition will return consistent
     * values during this time, so arrows will continue to scroll smoothly until
     * the music catches up. */
    if (size >= large_skip_threshold) {
      /* It's a large skip.  Catch up.  If we fall too far behind, the sound
       * thread will be decoding as fast as it can, which will steal too many
       * cycles from the rendering thread. */
      std::lock_guard<std::mutex> lock(position_lock);
      const snd_pcm_sframes_t skipped = dsnd_pcm_forward(pcm, size);
      if (skipped > 0) {
        /* The skipped frames count as written, or every later position would
         * read that far behind the frames Mix() was told it is writing. */
        last_cursor_pos += skipped;
        avail_frames -= skipped;
      }
    }
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
  if (unfilled_frames < (int)chunksize) {
    return 0;
  }

  return chunksize;
}

bool Alsa9Buf::WaitUntilFramesCanBeFilled(int timeout_ms) {
  int err = dsnd_pcm_wait(pcm, timeout_ms);
  /* EINTR is normal; don't warn. */
  if (err == -EINTR) {
    return false;
  }
  if (err < 0 && !Recover(err)) {
    /* Unrecoverable (unplugged?): snd_pcm_wait returns at once, so wait out
     * the timeout here rather than spin the mixer thread. */
    static bool warned = false;
    if (!warned) {
      warned = true;
      ALSA_ASSERT("snd_pcm_wait");
    }
    usleep(timeout_ms * 1000);
    return false;
  }

  return err == 1;
}

/* Restart the stream after an underrun stopped it (XRUN) or a system suspend
 * (SUSPENDED). Afterwards both device pointers restart at 0, which leaves
 * positions consistent: they're taken relative to the write pointer. */
bool Alsa9Buf::Recover(int err) {
  if (err != -EPIPE && err != -ESTRPIPE) {
    return false;
  }
  LOG->Trace(
      "ALSA: recovering from %s", err == -EPIPE ? "underrun" : "suspend");
  err = dsnd_pcm_recover(pcm, err, 1);
  ALSA_ASSERT("dsnd_pcm_recover");
  return err >= 0;
}

void Alsa9Buf::Write(const int16_t* buffer, int frames) {
  /* We should be able to write it all.  Mix() has already labelled these
   * frames with the positions at which they'll play, so if writing fails,
   * restart the stream and write them anyway. */
  bool recovered = false;
  while (frames > 0) {
    snd_pcm_sframes_t wrote;
    {
      std::lock_guard<std::mutex> lock(position_lock);
      wrote = dsnd_pcm_mmap_writei(pcm, (const char*)buffer, frames);
      if (wrote > 0) {
        last_cursor_pos += wrote;
      }
    }

    if (wrote == -EAGAIN) {
      dsnd_pcm_wait(pcm, 1);
      continue;
    }
    if (wrote < 0 && !recovered && Recover(wrote)) {
      recovered = true;
      continue;
    }
    if (wrote <= 0) {
      LOG->Trace(
          "RageSoundDriver_ALSA9::GetData: dsnd_pcm_mmap_writei: %s (%i)",
          dsnd_strerror(wrote), (int)wrote);
      return;
    }

    buffer += wrote * 2;
    frames -= wrote;
  }
}

static int64_t TimestampNs(const snd_htimestamp_t& ts) {
  return int64_t(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

static int64_t ClockNs(clockid_t id) {
  timespec ts;
  clock_gettime(id, &ts);
  return int64_t(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

/* GetPosition takes the delay to be live after this many new positions
 * without a pointer move. */
static const int kLiveDelayStrikes = 3;

int64_t Alsa9Buf::GetPosition() const {
  std::lock_guard<std::mutex> lock(position_lock);
  PositionClock& clock = position_clock;

  /* Read before the status: a pointer update that lands while the status is
   * read must not look older than this look. */
  const clockid_t clock_id =
      tstamp_clock != -1 ? tstamp_clock : CLOCK_MONOTONIC;
  const int64_t before = ClockNs(clock_id);

  snd_pcm_status_t* status;
  dsnd_pcm_status_alloca(&status);
  if (dsnd_pcm_status(pcm, status) < 0) {
    return clock.last_returned;
  }

  const snd_pcm_state_t state = dsnd_pcm_status_get_state(status);
  if (state != SND_PCM_STATE_RUNNING && state != SND_PCM_STATE_DRAINING) {
    clock.valid = false;
    if (state == SND_PCM_STATE_PREPARED) {
      /* Not started: what has been written is still to play. */
      const snd_pcm_uframes_t avail = dsnd_pcm_status_get_avail(status);
      const int64_t queued = avail < writeahead ? writeahead - avail : 0;
      clock.last_returned =
          std::max(clock.last_returned, last_cursor_pos - queued);
    }
    /* Stopped (XRUN, SUSPENDED, ...): nothing plays, so the position holds. */
    return clock.last_returned;
  }

  /* The device's position as of its last pointer update. */
  const int64_t frame = last_cursor_pos - dsnd_pcm_status_get_delay(status);
  const int64_t now = ClockNs(clock_id);

  /* When the pointer last moved (or the stream started). */
  int64_t moved = 0;
  if (tstamp_clock != -1) {
    snd_htimestamp_t updated, triggered;
    dsnd_pcm_status_get_htstamp(status, &updated);
    dsnd_pcm_status_get_trigger_htstamp(status, &triggered);
    moved = std::max(TimestampNs(updated), TimestampNs(triggered));
  }

  if (!clock.valid || frame != clock.frame) {
    /* A new position. If the pointer has moved since our last look, it was
     * reached when the pointer moved. If not, it's current as read, and a
     * driver doing that repeatedly reports a delay that changes on its own,
     * so its pointer moves say nothing about its positions. */
    int64_t reached = now;
    if (tstamp_clock != -1) {
      if (moved != clock.last_moved && moved > clock.last_query_ns &&
          moved <= now) {
        if (!clock.live_delay) {
          reached = moved;
        }
      } else if (clock.valid) {
        clock.strikes = std::min(clock.strikes + 1, kLiveDelayStrikes);
        if (clock.strikes == kLiveDelayStrikes) {
          clock.live_delay = true;
        }
      }
    }
    clock.valid = true;
    clock.frame = frame;
    clock.time_ns = reached;
  }
  clock.last_query_ns = before;
  clock.last_moved = moved;

  const int64_t elapsed =
      std::clamp<int64_t>(now - clock.time_ns, 0, max_extrapolation_ns);
  int64_t position = frame + elapsed * samplerate / 1000000000LL;

  /* Pointer updates land a little late or early; don't let that jitter step
   * the position backwards. Real jumps back are left to the caller. */
  const int64_t max_extrapolation_frames =
      max_extrapolation_ns * samplerate / 1000000000LL;
  if (position < clock.last_returned &&
      position >= clock.last_returned - max_extrapolation_frames) {
    position = clock.last_returned;
  }
  clock.last_returned = position;
  return position;
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
