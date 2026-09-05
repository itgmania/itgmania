#include "RageSoundDriver_WASAPI_SubDriver.h"

#include "RageLog.h"

namespace {
class WasapiSharedLLSubDriver : public WasapiSubDriver {
 public:
  const char* GetModeName() const override { return "Shared LowLatency"; }

  HRESULT InitializeStream(const WasapiInitParams& params) override {
#ifdef __IAudioClient3_INTERFACE_DEFINED__
    IAudioClient3* pAudioClient3 = nullptr;
    HRESULT hr =
        params.pAudioClient->QueryInterface(IID_PPV_ARGS(&pAudioClient3));
    if (FAILED(hr) || pAudioClient3 == nullptr) {
      LOG->Info("WASAPI: Shared LowLatency - IAudioClient3 not available");
      return S_FALSE;
    }

    UINT32 iDefaultPeriodFrames = 0;
    UINT32 iFundamentalPeriodFrames = 0;
    UINT32 iMinPeriodFrames = 0;
    UINT32 iMaxPeriodFrames = 0;
    hr = pAudioClient3->GetSharedModeEnginePeriod(
        params.pWaveFormat, &iDefaultPeriodFrames, &iFundamentalPeriodFrames,
        &iMinPeriodFrames, &iMaxPeriodFrames);
    if (FAILED(hr)) {
      LOG->Warn(
          "WASAPI: Shared LowLatency - GetSharedModeEnginePeriod failed");
      pAudioClient3->Release();
      return S_FALSE;
    }

    UINT32 iPeriodFrames = params.iWriteAheadFrames;
    if (iPeriodFrames < iMinPeriodFrames) {
      iPeriodFrames = iMinPeriodFrames;
    }
    if (iPeriodFrames > iMaxPeriodFrames) {
      iPeriodFrames = iMaxPeriodFrames;
    }

    if (iFundamentalPeriodFrames > 0) {
      UINT32 iRemainder = iPeriodFrames % iFundamentalPeriodFrames;
      if (iRemainder != 0) {
        iPeriodFrames += iFundamentalPeriodFrames - iRemainder;
        if (iPeriodFrames > iMaxPeriodFrames) {
          iPeriodFrames = iMaxPeriodFrames;
        }
      }
    }

    LOG->Info(
        "WASAPI: Shared LowLatency attempting to initialize using parameters "
        "(period=%u, min=%u, default=%u, max=%u, fundamental=%u)",
        iPeriodFrames, iMinPeriodFrames, iDefaultPeriodFrames,
        iMaxPeriodFrames, iFundamentalPeriodFrames);

    hr = pAudioClient3->InitializeSharedAudioStream(
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK, iPeriodFrames, params.pWaveFormat,
        nullptr);

    pAudioClient3->Release();

    if (FAILED(hr)) {
      LOG->Warn("WASAPI: Shared LowLatency InitializeSharedAudioStream failed");
      return hr;
    }

    LOG->Info("WASAPI: Shared LowLatency mode initialized successfully");
    return S_OK;
#else
    LOG->Info(
        "WASAPI: Low-Latency IAudioClient3 headers unavailable in this SDK; "
        "skipping shared low-latency pathway");
    (void)params;
    return S_FALSE;
#endif
  }

  bool UsesPaddingFrames() const override { return true; }
  bool RequiresInitialFill() const override { return false; }
};
}  // namespace

std::unique_ptr<WasapiSubDriver> CreateWasapiSharedLLSubDriver() {
  return std::make_unique<WasapiSharedLLSubDriver>();
}
