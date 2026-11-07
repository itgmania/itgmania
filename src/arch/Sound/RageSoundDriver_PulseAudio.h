#ifndef RAGE_SOUND_PULSEAUDIO_H
#define RAGE_SOUND_PULSEAUDIO_H

#include <pulse/pulseaudio.h>

#include <cstddef>
#include <cstdint>
#include <limits>
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
  inline int GetSampleRate() const { return m_ss.rate; };

 protected:
  int64_t GetPositionUnlocked() const;

  int64_t m_LastPosition;
  pa_sample_spec m_ss;
  char* m_Error;

  void m_InitStream();
  RageSemaphore m_Sem;

  pa_threaded_mainloop* m_PulseMainLoop;
  pa_context* m_PulseCtx;
  pa_stream* m_PulseStream;

  // Each timing reply gives the stream frame being heard at the time of the
  // reply. While the stream plays, frame - rate * time is a constant offset;
  // this keeps the offsets of recent replies and the edges of their spread.
  class OffsetWindow {
   public:
    void Clear();
    bool Empty() const { return m_iCount == 0; }
    // jump: how far outside the current spread an offset must fall, a few
    // times running, to be taken as the stream moving rather than noise.
    void Add(double fOffset, double fJump);
    // False until there are enough offsets, or a spread from before, to put
    // a lower edge that isn't ahead of the true one.
    bool HaveLowEdge() const {
      return m_iCount >= kMinSamples || (m_iCount > 0 && m_fSpread > 0);
    }
    double LowEdge() const { return m_fLowEdge; }
    double HighEdge() const { return m_fHigh; }

   private:
    static constexpr int kSize = 128;
    static constexpr int kMinSamples = 64;
    double m_fSamples[kSize];
    int m_iCount = 0;
    int m_iNext = 0;
    int m_iLowRun = 0;
    int m_iHighRun = 0;
    double m_fLow = 0;
    double m_fHigh = 0;
    double m_fLowEdge = 0;
    double m_fSpread = 0;  // of the last full enough window; Clear keeps it
    // Highest lower edge allowed while refilling after a rising restart.
    double m_fCap = std::numeric_limits<double>::infinity();
  };
  OffsetWindow m_Offsets;
  bool m_bPipeWire = false;
  bool m_bHaveTiming = false;
  bool m_bPlaying = false;
  double m_fHeldFrame = 0;

  // Our own timing requests, at irregular intervals; see TimingTimerCb.
  pa_time_event* m_TimingEvent = nullptr;
  bool m_bTimingRequested = false;
  uint32_t m_iJitter = 0x9e3779b9u;

 public:
  void CtxStateCb(pa_context* c);
  void StreamStateCb(pa_stream* s);
  void StreamWriteCb(pa_stream* s, size_t length);
  void LatencyUpdateCb();
  void TimingTimerCb(pa_time_event* e);

  static void StaticCtxStateCb(pa_context* c, void* user);
  static void StaticStreamStateCb(pa_stream* s, void* user);
  static void StaticStreamWriteCb(pa_stream* s, size_t length, void* user);
  static void StaticLatencyUpdateCb(pa_stream* s, void* user);
  static void StaticTimingTimerCb(
      pa_mainloop_api* api, pa_time_event* e, const struct timeval* tv,
      void* user);
  static void StaticTimingDoneCb(pa_stream* s, int success, void* user);
  static void StaticServerInfoCb(
      pa_context* c, const pa_server_info* i, void* user);
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
