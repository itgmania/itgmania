#ifndef RAGE_SOUND_WASAPI_H
#define RAGE_SOUND_WASAPI_H

// clang-format off
#include <windows.h>
// clang-format on

#include <atomic>
#include <cstdint>
#include <mutex>

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

  IAudioClient* m_pAudioClient;
  IAudioRenderClient* m_pRenderClient;
  IAudioClock* m_pAudioClock;
  UINT64 m_iAudioClockFreq;  // device units per second, from GetFrequency
  int m_iPeriodFrames;       // the audio engine's processing period

  // Where playback is, worked out from what the audio engine has taken from
  // our buffer rather than from IAudioClock. After the engine glitches,
  // IAudioClock can move in whole 10 ms steps and run ahead of the audio for
  // the rest of the session; what the engine has consumed always matches what
  // it plays. At every buffer event the mixer thread records frames written
  // minus padding with the time; positions in between run on the system clock
  // from the least delayed recent event.
  class ConsumedClock {
   public:
    void Reset(int iSampleRate, int iPeriodFrames);
    // Times are performance-counter microseconds. Before it has enough
    // events, compares consumption with IAudioClock (iClockFrames read at
    // iClockUs) so that steady-state positions come out where IAudioClock
    // puts them.
    void AddEvent(
        int64_t iEventUs, int64_t iConsumedFrames, bool bClockValid,
        int64_t iClockFrames, int64_t iClockUs);
    // -1 until calibrated.
    int64_t GetPosition(int64_t iNowUs) const;

   private:
    static constexpr int kCalibrationEvents = 50;
    static constexpr int kHistory = 64;
    mutable std::mutex m_Mutex;
    int m_iSampleRate = 0;
    int m_iPeriodFrames = 0;
    int m_iCalibrationSeen = 0;
    int m_iCalibrationTries = 0;
    double m_fBestConsumed = 0;
    double m_fBestClock = 0;
    int64_t m_iLatencyFrames = 0;  // consumed but not yet heard
    bool m_bCalibrated = false;
    // For each recent event, heard frame minus elapsed frames: the largest is
    // the event the engine handled most promptly.
    double m_History[kHistory] = {};
    int m_iHistoryCount = 0;
    int m_iHistoryNext = 0;
    int m_iLowEvents = 0;  // consecutive events well behind the best
    double m_fLastValue = 0;
    int64_t m_iLastConsumed = 0;
  };
  ConsumedClock m_Clock;

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
