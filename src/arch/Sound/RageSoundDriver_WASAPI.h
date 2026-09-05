#ifndef RAGE_SOUND_WASAPI_H
#define RAGE_SOUND_WASAPI_H

// clang-format off
#include <windows.h>
// clang-format on

#include <atomic>
#include <memory>
#include <vector>

#include "RageSoundDriver.h"
#include "RageThreads.h"

struct IAudioClient;
struct IAudioRenderClient;
struct IAudioClock;
class WasapiSubDriver;
struct WasapiInitParams;

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

  IAudioClient* m_pAudioClient;
  IAudioRenderClient* m_pRenderClient;
  IAudioClock* m_pAudioClock;
  UINT64 m_iAudioClockFreq;  // device units per second, from GetFrequency

  HANDLE m_hAudioEvent;

  std::atomic<bool> m_bShutdownMixerThread;
  bool m_bOwnsComInit;
  std::unique_ptr<WasapiSubDriver> m_pSubDriver;

  static int MixerThread_start(void* p);
  bool TrySubDriver(const std::string& sSubDriverName,
                    const WasapiInitParams& params,
                    HRESULT& hrInitialize);
  bool HasSubDriverName(const std::vector<std::string>& asSubDriverNames,
                        const std::string& sSubDriverName) const;
  void BuildSubDriverTryOrder(std::vector<std::string>& asSubDriverNames) const;
  bool WriteFrames(UINT32 iFrames, int64_t iHardwareFrame, int64_t iCurrentFrame,
                   const char* sPhase);
  void MixerThread();
  RageThread m_MixingThread;

  bool InitWASAPI(std::string& sError);
  void FreeWASAPI();
};

#endif
