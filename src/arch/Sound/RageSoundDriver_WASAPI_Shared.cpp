#include "RageSoundDriver_WASAPI_SubDriver.h"

#include "PrefsManager.h"

namespace {
REFERENCE_TIME FramesToHnsDuration(UINT32 iFrames, int iSampleRate) {
  if (iFrames == 0 || iSampleRate <= 0) {
    return 0;
  }

  return (REFERENCE_TIME)iFrames * 10000000ULL / iSampleRate;
}

class WasapiSharedSubDriver : public WasapiSubDriver {
 public:
  const char* GetModeName() const override { return "Shared"; }

  HRESULT InitializeStream(const WasapiInitParams& params) override {
    REFERENCE_TIME hnsRequestedDuration =
        FramesToHnsDuration(params.iWriteAheadFrames, params.iSampleRate);

    return params.pAudioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        hnsRequestedDuration, 0, params.pWaveFormat, nullptr);
  }

  bool UsesPaddingFrames() const override { return true; }
  bool RequiresInitialFill() const override { return false; }
};
}  // namespace

std::unique_ptr<WasapiSubDriver> CreateWasapiSharedSubDriver() {
  return std::make_unique<WasapiSharedSubDriver>();
}
