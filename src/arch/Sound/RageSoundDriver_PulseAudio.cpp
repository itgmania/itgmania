#include "RageSoundDriver_PulseAudio.h"

#include <pulse/error.h>
#include <sys/resource.h>
#include <sys/time.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "PrefsManager.h"
#include "ProductInfo.h"
#include "RageException.h"
#include "RageLog.h"
#include "RageSound.h"
#include "RageSoundManager.h"
#include "RageTimer.h"
#include "RageUtil.h"
#include "global.h"
#include "ver.h"

/* Register the RageSoundDriver_Pulseaudio class as sound driver "Pulse" */
REGISTER_SOUND_DRIVER_CLASS2("Pulse", PulseAudio);

/* Constructor */
RageSoundDriver_PulseAudio::RageSoundDriver_PulseAudio()
    : RageSoundDriver(),
      last_position_(0),
      error_(nullptr),
      sem_("Pulseaudio Synchronization Semaphore"),
      pulse_main_loop_(nullptr),
      pulse_ctx_(nullptr),
      pulse_stream_(nullptr) {
  ss_.rate = PREFSMAN->sound_preferred_sample_rate_;
  if (ss_.rate == 0) {
    ss_.rate = FALLBACK_SAMPLE_RATE;
  }
}

RageSoundDriver_PulseAudio::~RageSoundDriver_PulseAudio() {
  pa_context_disconnect(pulse_ctx_);
  pa_context_unref(pulse_ctx_);
  pa_threaded_mainloop_stop(pulse_main_loop_);
  pa_threaded_mainloop_free(pulse_main_loop_);

  if (error_ != nullptr) {
    free(error_);
  }
}

/* Initialization */
std::string RageSoundDriver_PulseAudio::Init() {
  int error = 0;

  LOG->Trace("Pulse: pa_threaded_mainloop_new()...");
  pulse_main_loop_ = pa_threaded_mainloop_new();
  if (pulse_main_loop_ == nullptr) {
    return "pa_threaded_mainloop_new() failed!";
  }

#ifdef PA_PROP_APPLICATION_NAME /* proplist available only since 0.9.11 */
  pa_proplist* plist = pa_proplist_new();
  pa_proplist_sets(plist, PA_PROP_APPLICATION_NAME, PRODUCT_FAMILY);
  pa_proplist_sets(plist, PA_PROP_APPLICATION_VERSION, kProductVersion);
  pa_proplist_sets(plist, PA_PROP_MEDIA_ROLE, "game");

  LOG->Trace("Pulse: pa_context_new_with_proplist()...");

  pulse_ctx_ = pa_context_new_with_proplist(
      pa_threaded_mainloop_get_api(pulse_main_loop_), PRODUCT_FAMILY, plist);
  pa_proplist_free(plist);

  if (pulse_ctx_ == nullptr) {
    return "pa_context_new_with_proplist() failed!";
  }
#else
  LOG->Trace("Pulse: pa_context_new()...");
  pulse_ctx_ = pa_context_new(
      pa_threaded_mainloop_get_api(pulse_main_loop_), PRODUCT_FAMILY);
  if (pulse_ctx_ == nullptr) {
    return "pa_context_new() failed!";
  }
#endif

  pa_context_set_state_callback(pulse_ctx_, StaticCtxStateCb, this);

  LOG->Trace("Pulse: pa_context_connect()...");
  error =
      pa_context_connect(pulse_ctx_, nullptr, (pa_context_flags_t)0, nullptr);

  if (error < 0) {
    return ssprintf(
        "pa_contect_connect(): %s", pa_strerror(pa_context_errno(pulse_ctx_)));
  }

  LOG->Trace("Pulse: pa_threaded_mainloop_start()...");
  error = pa_threaded_mainloop_start(pulse_main_loop_);
  if (error < 0) {
    return ssprintf("pa_threaded_mainloop_start() returned %i", error);
  }

  /* Create the decode thread, this will be needed for Mix(), that we
   * will use as soon as a stream is ready. */
  StartDecodeThread();

  /* Wait for the pulseaudio stream to be ready before returning.
   * An error may occur, if it appends, error_ becomes non-nullptr. */
  sem_.Wait();

  if (error_ == nullptr) {
    return "";
  } else {
    return error_;
  }
}

void RageSoundDriver_PulseAudio::m_InitStream(void) {
  int error;
  pa_sample_spec ss_local;  // Use a local pa_sample_spec for setup
  pa_channel_map map;

  /* init sample spec */
  ss_local.format = PA_SAMPLE_S16LE;
  ss_local.channels = 2;
  ss_local.rate =
      ss_.rate;  // Use the rate initialized in the constructor's ss_

  /* init channel map */
  pa_channel_map_init_stereo(&map);

  /* check sample spec */
  if (!pa_sample_spec_valid(&ss_local)) {
    if (asprintf(&error_, "invalid sample spec!") == -1) {
      error_ = nullptr;  // asprintf failed to allocate memory
    }
    sem_.Post();
    return;
  }

  /* log the used sample spec */
  char specstring[PA_SAMPLE_SPEC_SNPRINT_MAX];
  pa_sample_spec_snprint(specstring, sizeof(specstring), &ss_local);
  LOG->Trace("Pulse: using sample spec: %s", specstring);

  /* create the stream */
  LOG->Trace("Pulse: pa_stream_new()...");
  pulse_stream_ =
      pa_stream_new(pulse_ctx_, PRODUCT_FAMILY " Audio", &ss_local, &map);
  if (pulse_stream_ == nullptr) {
    if (asprintf(
            &error_, "pa_stream_new(): %s",
            pa_strerror(pa_context_errno(pulse_ctx_))) == -1) {
      error_ = nullptr;  // asprintf failed to allocate memory
    }
    sem_.Post();
    return;
  }

  /* set the write callback, it will be called when the sound server
   * needs data */
  pa_stream_set_write_callback(pulse_stream_, StaticStreamWriteCb, this);

  /* set the state callback, it will be called when the stream state will
   * change */
  pa_stream_set_state_callback(pulse_stream_, StaticStreamStateCb, this);

  /* configure attributes of the stream */
  pa_buffer_attr attr;
  memset(&attr, 0x00, sizeof(attr));  // Initialize all members to 0/nullptr

  /* tlength: Target length of the buffer.
   *
   * "The server tries to assure that at least tlength bytes are always
   *  available in the per-stream server-side playback buffer. It is
   *  recommended to set this to (uint32_t) -1, which will initialize
   *  this to a value that is deemed sensible by the server. However,
   *  this value will default to something like 2s, i.e. for applications
   *  that have specific latency requirements this value should be set to
   *  the maximum latency that the application can deal with."
   *
   * We don't want the default here, we want a small latency.
   * We use pa_usec_to_bytes() to convert a latency to a buffer size.
   */
  attr.tlength =
      pa_usec_to_bytes(20 * PA_USEC_PER_MSEC, &ss_local);  // Use local ss_local

  /* maxlength: Maximum length of the buffer
   *
   * "Setting this to (uint32_t) -1 will initialize this to the maximum
   *  value supported by server, which is recommended."
   *
   * (uint32_t)-1 is NOT working here, setting it to tlength*2, like
   * openal-soft-pulseaudio does.
   */
  attr.maxlength = attr.tlength * 2;

  /* minreq: Minimum request
   *
   * "The server does not request less than minreq bytes from the client,
   *  instead waits until the buffer is free enough to request more bytes
   *  at once. It is recommended to set this to (uint32_t) -1, which will
   *  initialize this to a value that is deemed sensible by the server."
   *
   * (uint32_t)-1 is NOT working here, setting it to 0, like
   * openal-soft-pulseaudio does.
   */
  attr.minreq = 0;  // Setting to (uint32_t)-1 caused issues in some
                    // environments. 0 is safer.

  /* prebuf: Pre-buffering
   *
   * "The server does not start with playback before at least prebuf
   *  bytes are available in the buffer. It is recommended to set this
   *  to (uint32_t) -1, which will initialize this to the same value as
   *  tlength"
   */
  attr.prebuf = (uint32_t)-1;

  /* fragsize: Deprecated. Use tlength, prebuf, minreq, maxlength instead.
   * Ensure it's not used by setting to (uint32_t)-1, as per PA documentation.
   */
  attr.fragsize = (uint32_t)-1;

  /* log the used target buffer length */
  LOG->Trace("Pulse: using target buffer length of %i bytes", attr.tlength);

  /* connect the stream for playback */
  LOG->Trace("Pulse: pa_stream_connect_playback()...");
  const int flags =
      PA_STREAM_INTERPOLATE_TIMING |
      PA_STREAM_NOT_MONOTONIC  // mHostTime may not be monotonic in some cases
      | PA_STREAM_AUTO_TIMING_UPDATE |
      PA_STREAM_ADJUST_LATENCY;  // Allow server to adjust latency based on our
                                 // buffer_attr
  error = pa_stream_connect_playback(
      pulse_stream_, nullptr, &attr, static_cast<pa_stream_flags_t>(flags),
      nullptr, nullptr);
  if (error < 0) {
    if (asprintf(
            &error_, "pa_stream_connect_playback(): %s",
            pa_strerror(pa_context_errno(pulse_ctx_))) == -1) {
      error_ = nullptr;  // asprintf failed to allocate memory
    }
    sem_.Post();
    return;
  }

  // ss_ is the member variable. After successfully connecting the stream,
  // we can be more confident about the sample spec being used.
  // It's generally good practice to update ss_ with the spec that was
  // actually used to create the stream, in case the server negotiated
  // something slightly different (though with PA_SAMPLE_S16LE, 2ch, and
  // specific rates, it's less likely to change).
  ss_ = ss_local;

  // Semaphore is posted in StreamStateCb when stream becomes PA_STREAM_READY
}

void RageSoundDriver_PulseAudio::CtxStateCb(pa_context* c) {
  switch (pa_context_get_state(pulse_ctx_)) {
    case PA_CONTEXT_CONNECTING:
      LOG->Trace("Pulse: Context connecting...");
      break;
    case PA_CONTEXT_AUTHORIZING:
      LOG->Trace("Pulse: Context authorizing...");
      break;
    case PA_CONTEXT_SETTING_NAME:
      LOG->Trace("Pulse: Context setting name...");
      break;
    case PA_CONTEXT_READY:
      LOG->Trace("Pulse: Context ready now.");
      m_InitStream();
      break;
    case PA_CONTEXT_TERMINATED:
    case PA_CONTEXT_FAILED:
      if (asprintf(
              &error_, "context connection failed: %s",
              pa_strerror(pa_context_errno(pulse_ctx_))) == -1) {
        error_ = nullptr;
      }
      sem_.Post();
      return;
      break;
    default:
      break;
  }
}

void RageSoundDriver_PulseAudio::StreamStateCb(pa_stream* s) {
  switch (pa_stream_get_state(pulse_stream_)) {
    case PA_STREAM_CREATING:
      LOG->Trace("Pulse: Stream creating...");
      break;
    case PA_STREAM_READY:
      LOG->Trace("Pulse: Stream ready now/");
      sem_.Post();
      return;
      break;
    case PA_STREAM_UNCONNECTED:
    case PA_STREAM_TERMINATED:
    case PA_STREAM_FAILED:
      if (asprintf(
              &error_, "stream connection failed: %s",
              pa_strerror(pa_context_errno(pulse_ctx_))) == -1) {
      }
      sem_.Post();
      return;
      break;
  }
}

int64_t RageSoundDriver_PulseAudio::GetPosition() const {
  pa_threaded_mainloop_lock(pulse_main_loop_);
  int64_t position = GetPositionUnlocked();
  pa_threaded_mainloop_unlock(pulse_main_loop_);
  return position;
}

int64_t RageSoundDriver_PulseAudio::GetPositionUnlocked() const {
  pa_usec_t usec;
  if (pa_stream_get_time(pulse_stream_, &usec) < 0) {
    int pa_errno = pa_context_errno(pulse_ctx_);

    // We might get no data error if the stream has just been started and hasn't
    // received any timing data yet
    if (pa_errno == PA_ERR_NODATA) {
      return 0;
    } else {
      RageException::Throw(
          "Pulse: pa_stream_get_time() failed: %s", pa_strerror(pa_errno));
    }
  }

  size_t length = pa_usec_to_bytes(usec, &ss_);
  return length /
         (sizeof(int16_t) * 2); /* we use 16-bit frames and 2 channels */
}

void RageSoundDriver_PulseAudio::StreamWriteCb(pa_stream* s, size_t length) {
  int64_t cur_pos = GetPositionUnlocked();
  while (length > 0) {
    void* buf;
    size_t bufsize = length;
    if (pa_stream_begin_write(pulse_stream_, &buf, &bufsize) < 0) {
      RageException::Throw(
          "Pulse: pa_stream_begin_write() failed: %s",
          pa_strerror(pa_context_errno(pulse_ctx_)));
    }

    const size_t nbframes =
        bufsize / sizeof(int16_t); /* we use 16-bit frames */
    int64_t pos1 = last_position_;
    int64_t pos2 = pos1 + nbframes / 2; /* Mix() position in stereo frames */
    this->Mix(reinterpret_cast<int16_t*>(buf), pos2 - pos1, pos1, cur_pos);

    if (pa_stream_write(
            pulse_stream_, buf, bufsize, nullptr, 0, PA_SEEK_RELATIVE) < 0) {
      RageException::Throw(
          "Pulse: pa_stream_write() failed: %s",
          pa_strerror(pa_context_errno(pulse_ctx_)));
    }

    last_position_ = pos2;
    length -= bufsize;
  }
}

/* Static wrappers, because pulseaudio is a C API, it uses callbacks.
 * So we have to write wrappers that will call our objects callbacks. */
void RageSoundDriver_PulseAudio::StaticCtxStateCb(pa_context* c, void* user) {
  RageSoundDriver_PulseAudio* obj = (RageSoundDriver_PulseAudio*)user;
  obj->CtxStateCb(c);
}
void RageSoundDriver_PulseAudio::StaticStreamStateCb(pa_stream* s, void* user) {
  RageSoundDriver_PulseAudio* obj = (RageSoundDriver_PulseAudio*)user;
  obj->StreamStateCb(s);
}
void RageSoundDriver_PulseAudio::StaticStreamWriteCb(
    pa_stream* s, size_t length, void* user) {
  RageSoundDriver_PulseAudio* obj = (RageSoundDriver_PulseAudio*)user;
  obj->StreamWriteCb(s, length);
}

/*
 * (c) 2009 Damien Thebault
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
