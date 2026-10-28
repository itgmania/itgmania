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

#define REGISTER_WASAPI_SUBDRIVER(name, x) \
  static WasapiSubDriverRegistration<x> register_wasapi_subdriver_##x(name)

class WasapiSubDriver {
 public:
  virtual ~WasapiSubDriver() = default;

  virtual const char* GetModeName() const = 0;
  virtual HRESULT InitializeStream(const WasapiInitParams& params) = 0;
  virtual bool UsesPaddingFrames() const = 0;
  virtual bool RequiresInitialFill() const = 0;
};

using WasapiSubDriverFactory = std::unique_ptr<WasapiSubDriver> (*)();
void RegisterWasapiSubDriver(
    const std::string& sName, WasapiSubDriverFactory pFactory);

template <class T>
class WasapiSubDriverRegistration {
 public:
  explicit WasapiSubDriverRegistration(const char* sName) {
    RegisterWasapiSubDriver(sName, []() -> std::unique_ptr<WasapiSubDriver> {
      return std::make_unique<T>();
    });
  }
};

std::unique_ptr<WasapiSubDriver> CreateWasapiSubDriverByName(
    const std::string& sName);
#endif
