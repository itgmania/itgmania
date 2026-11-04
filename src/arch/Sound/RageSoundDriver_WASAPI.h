#ifndef RAGE_SOUND_WASAPI_H
#define RAGE_SOUND_WASAPI_H

// clang-format off
#include <windows.h>
// clang-format on

#include <atomic>

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
  int sample_rate_;
  UINT32 buffer_size_frames_;
  bool float_;  // true if we are using float, false if 16-bit PCM

  IAudioClient* audio_client_;
  IAudioRenderClient* render_client_;
  IAudioClock* audio_clock_;
  UINT64 audio_clock_freq_;  // device units per second, from GetFrequency

  HANDLE audio_event_;

  std::atomic<bool> shutdown_mixer_thread_;
  bool owns_com_init_;

  static int MixerThread_start(void* p);
  void MixerThread();
  RageThread mixing_thread_;

  bool InitWASAPI(std::string& sError);
  void FreeWASAPI();
};

#endif
