#ifndef ALSA9_HELPERS_H
#define ALSA9_HELPERS_H

#include <time.h>

#include <cstdint>
#include <mutex>
#include <string>

#define ALSA_PCM_NEW_HW_PARAMS_API
#define ALSA_PCM_NEW_SW_PARAMS_API
#include <alsa/asoundlib.h>

class Alsa9Buf {
 private:
  unsigned samplerate;
  /* Frames written so far. GetPosition reports positions in this count, so it
   * must move with the device's write pointer: anything that moves that
   * pointer without writing (forward) moves this too. */
  int64_t last_cursor_pos;

  snd_pcm_uframes_t preferred_writeahead, preferred_chunksize;
  snd_pcm_uframes_t writeahead, chunksize;

  snd_pcm_t* pcm;

  /* GetPosition runs on the mixer and main threads. This keeps
   * last_cursor_pos in step with the write pointer and guards the clock. */
  mutable std::mutex position_lock;

  /* Clock of the PCM's status timestamps, or -1 if they can't be used. */
  clockid_t tstamp_clock;

  /* The device reports its position only when its pointer moves, which on
   * some hardware is once a period. GetPosition extrapolates from the time
   * the current position was reached, up to max_extrapolation_ns. */
  struct PositionClock {
    bool valid = false;
    int64_t frame = 0;          // position, in last_cursor_pos frames
    int64_t time_ns = 0;        // when the device reached it
    int64_t last_query_ns = 0;  // clock just before the last status read
    int64_t last_moved = 0;     // that status's pointer timestamp
    int64_t last_returned = 0;
    /* Positions keep moving without the pointer moving: the driver reports a
     * delay that changes on its own (USB), so they are current as read. Once
     * seen, that stays: such a driver also moves its pointer every period. */
    bool live_delay = false;
    int strikes = 0;  // new positions without a pointer move
  };
  mutable PositionClock position_clock;
  int64_t max_extrapolation_ns;

  bool SetHWParams();
  bool SetSWParams();
  bool Recover(int err);

  static void ErrorHandler(
      const char* file, int line, const char* function, int err,
      const char* fmt, ...);

 public:
  static void InitializeErrorHandler();
  static void GetSoundCardDebugInfo();
  Alsa9Buf();
  std::string Init(int iWriteahead, int iChunkSize, int iSampleRate);
  ~Alsa9Buf();

  int GetNumFramesToFill();
  bool WaitUntilFramesCanBeFilled(int timeout_ms);
  void Write(const int16_t* buffer, int frames);

  void SetVolume(float vol);
  int GetSampleRate() const { return samplerate; }

  int64_t GetPosition() const;
  int64_t GetPlayPos() const { return last_cursor_pos; }
};
#endif

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
