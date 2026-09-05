#include "RageSoundDriver_WASAPI_SubDriver.h"

// clang-format off
#include <ksmedia.h>
// clang-format on

namespace {
REFERENCE_TIME FramesToHnsDuration(UINT32 iFrames, int iSampleRate) {
  if (iFrames == 0 || iSampleRate <= 0) {
    return 0;
  }

  return (REFERENCE_TIME)iFrames * 10000000ULL / iSampleRate;
}

class WasapiExclusiveSubDriver : public WasapiSubDriver {
 public:
  const char* GetModeName() const override { return "Exclusive"; }

  HRESULT InitializeStream(const WasapiInitParams& params) override {
    if (params.pWaveFormat->wFormatTag != WAVE_FORMAT_EXTENSIBLE) {
      return AUDCLNT_E_UNSUPPORTED_FORMAT;
    }

    // create a copy of the original format to modify if needed
    const WAVEFORMATEXTENSIBLE* pOriginalFormat =
        reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(params.pWaveFormat);
    WAVEFORMATEXTENSIBLE selectedFormat = *pOriginalFormat;
    WAVEFORMATEX* pSelectedFormat = (WAVEFORMATEX*)&selectedFormat;

    HRESULT hr = params.pAudioClient->IsFormatSupported(
        AUDCLNT_SHAREMODE_EXCLUSIVE, pSelectedFormat, nullptr);
    if (FAILED(hr)) {
      selectedFormat.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
      selectedFormat.Format.wBitsPerSample = 16;
      selectedFormat.Samples.wValidBitsPerSample = 16;
      selectedFormat.Format.nBlockAlign =
          selectedFormat.Format.nChannels *
          selectedFormat.Format.wBitsPerSample / 8;
      selectedFormat.Format.nAvgBytesPerSec =
          selectedFormat.Format.nSamplesPerSec * selectedFormat.Format.nBlockAlign;

      hr = params.pAudioClient->IsFormatSupported(
          AUDCLNT_SHAREMODE_EXCLUSIVE, pSelectedFormat, nullptr);
      if (FAILED(hr)) {
        return hr;
      }
    }

    REFERENCE_TIME hnsRequestedDuration =
        FramesToHnsDuration(params.iWriteAheadFrames, params.iSampleRate);
    if (hnsRequestedDuration == 0) {
      REFERENCE_TIME hnsDefaultPeriod = 0;
      REFERENCE_TIME hnsMinimumPeriod = 0;
      hr = params.pAudioClient->GetDevicePeriod(
          &hnsDefaultPeriod, &hnsMinimumPeriod);
      if (FAILED(hr)) {
        return hr;
      }
      hnsRequestedDuration =
          hnsMinimumPeriod ? hnsMinimumPeriod : hnsDefaultPeriod;
    }

    hr = params.pAudioClient->Initialize(
        AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        hnsRequestedDuration, hnsRequestedDuration, pSelectedFormat, nullptr);
    if (FAILED(hr)) {
      return hr;
    }

    // set params.pWaveFormat to the actual format we are using, since we succeeded.
    *reinterpret_cast<WAVEFORMATEXTENSIBLE*>(params.pWaveFormat) =
        selectedFormat;
    return S_OK;
  }

  bool UsesPaddingFrames() const override { return false; }
  bool RequiresInitialFill() const override { return true; }
};
}  // namespace

std::unique_ptr<WasapiSubDriver> CreateWasapiExclusiveSubDriver() {
  return std::make_unique<WasapiExclusiveSubDriver>();
}
