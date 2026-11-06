#ifndef RAGE_SOUND_DRIVER_AU_H
#define RAGE_SOUND_DRIVER_AU_H

#include <AudioUnit/AudioUnit.h>
#include <CoreFoundation/CoreFoundation.h>

#include <cstdint>
#include <string>

#include "RageSoundDriver.h"
#include "RageThreads.h"

class RageSoundDriver_AU : public RageSoundDriver {
 public:
  RageSoundDriver_AU();
  std::string Init();
  ~RageSoundDriver_AU();
  float GetPlayLatency() const;
  int GetSampleRate() const { return sample_rate_; }
  int64_t GetPosition() const;

 protected:
  void SetupDecodingThread();

 private:
  static OSStatus Render(
      void* inRefCon, AudioUnitRenderActionFlags* ioActionFlags,
      const AudioTimeStamp* inTimeStamp, UInt32 inBusNumber,
      UInt32 inNumberFrames, AudioBufferList* ioData);
  static void NameHALThread(
      CFRunLoopObserverRef, CFRunLoopActivity activity, void* inRefCon);

  double time_scale_;
  AudioUnit output_unit_;
  int sample_rate_;
  bool done_;
  bool started_;
  RageThreadRegister* io_thread_;
  RageThreadRegister* notification_thread_;
  RageSemaphore semaphore_;
};

#endif
/*
 * (c) 2004-2006 Steve Checkoway
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
