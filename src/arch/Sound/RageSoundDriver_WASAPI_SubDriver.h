#ifndef RAGE_SOUND_WASAPI_SUB_DRIVER_H
#define RAGE_SOUND_WASAPI_SUB_DRIVER_H

// clang-format off
#include <windows.h>
#include <audioclient.h>
// clang-format on

#include <memory>
#include <string>

struct WasapiInitParams {
  IAudioClient* pAudioClient;
  WAVEFORMATEX* pWaveFormat;
  int iSampleRate;
  int iWriteAheadFrames;
};

class WasapiSubDriver {
 public:
  virtual ~WasapiSubDriver() = default;

  virtual const char* GetModeName() const = 0;
  virtual HRESULT InitializeStream(const WasapiInitParams& params) = 0;
  virtual bool UsesPaddingFrames() const = 0;
  virtual bool RequiresInitialFill() const = 0;
};

std::unique_ptr<WasapiSubDriver> CreateWasapiSharedSubDriver();
std::unique_ptr<WasapiSubDriver> CreateWasapiSharedLLSubDriver();
std::unique_ptr<WasapiSubDriver> CreateWasapiExclusiveSubDriver();
std::unique_ptr<WasapiSubDriver> CreateWasapiSubDriverByName(
    const std::string& sName);
const char* GetWasapiSubDriverValidNames();

#endif
