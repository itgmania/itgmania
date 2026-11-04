#ifndef RAGE_SOUND_PULSEAUDIO_H
#define RAGE_SOUND_PULSEAUDIO_H

#include <pulse/pulseaudio.h>

#include <cstddef>
#include <cstdint>
#include <string>

#include "RageSound.h"
#include "RageSoundDriver.h"
#include "RageThreads.h"

class RageSoundDriver_PulseAudio : public RageSoundDriver {
 public:
  RageSoundDriver_PulseAudio();
  virtual ~RageSoundDriver_PulseAudio();

  std::string Init();

  int64_t GetPosition() const;
  inline int GetSampleRate() const { return ss_.rate; };

 protected:
  int64_t GetPositionUnlocked() const;

  int64_t last_position_;
  pa_sample_spec ss_;
  char* error_;

  void m_InitStream();
  RageSemaphore sem_;

  pa_threaded_mainloop* pulse_main_loop_;
  pa_context* pulse_ctx_;
  pa_stream* pulse_stream_;

 public:
  void CtxStateCb(pa_context* c);
  void StreamStateCb(pa_stream* s);
  void StreamWriteCb(pa_stream* s, size_t length);

  static void StaticCtxStateCb(pa_context* c, void* user);
  static void StaticStreamStateCb(pa_stream* s, void* user);
  static void StaticStreamWriteCb(pa_stream* s, size_t length, void* user);
};

#endif /* RAGE_SOUND_PULSEAUDIO_H */

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
