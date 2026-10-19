#ifndef RAGE_SOUND_WASAPI_H
#define RAGE_SOUND_WASAPI_H

// clang-format off
#include <windows.h>
// clang-format on

#include <atomic>
#include <cstdint>
#include <vector>

#include "RageSoundDriver.h"
#include "RageThreads.h"

struct IAudioClient;
struct IAudioRenderClient;
struct IAudioClock;

class RageSoundDriver_WASAPI : public RageSoundDriver {
 public:
  RageSoundDriver_WASAPI();
  virtual ~RageSoundDriver_WASAPI();
  std::string Init() override;

  int64_t GetPosition() const override;
  float GetPlayLatency() const override;
  int GetSampleRate() const override;

 protected:
  void SetupDecodingThread() override;

 private:
  int m_iSampleRate;
  UINT32 m_iBufferSizeFrames;
  bool m_bFloat;  // true if we are using float, false if 16-bit PCM
  // Channels in the shared-mode mix format. The mixer makes stereo; when the
  // device runs more channels (a 5.1/7.1 speaker setup, some headsets), it
  // mixes into these buffers and spreads the result over the device's.
  int m_iChannels;
  std::vector<float> m_StereoFloat;
  std::vector<int16_t> m_StereoInt16;

  IAudioClient* m_pAudioClient;
  IAudioRenderClient* m_pRenderClient;
  IAudioClock* m_pAudioClock;
  UINT64 m_iAudioClockFreq;  // device units per second, from GetFrequency

  HANDLE m_hAudioEvent;

  std::atomic<bool> m_bShutdownMixerThread;
  bool m_bOwnsComInit;

  static int MixerThread_start(void* p);
  void MixerThread();
  RageThread m_MixingThread;

  bool InitWASAPI(std::string& sError);
  void FreeWASAPI();
};

#endif
