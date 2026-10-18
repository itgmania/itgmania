#include "RageSoundDriver_WASAPI.h"

// clang-format off
#include <windows.h>
#include <audioclient.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
// clang-format on

#include <algorithm>
#include <cwctype>
#include <string>

#include "PrefsManager.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "archutils/Win32/DirectXHelpers.h"
#include "archutils/Win32/ErrorStrings.h"
#include "global.h"

REGISTER_SOUND_DRIVER_CLASS2("WASAPI", WASAPI);

namespace {
bool ContainsNoCase(
    const std::wstring& sHaystack, const std::wstring& sNeedle) {
  auto it = std::search(
      sHaystack.begin(), sHaystack.end(), sNeedle.begin(), sNeedle.end(),
      [](wchar_t a, wchar_t b) {
        return std::towlower(a) == std::towlower(b);
      });
  return it != sHaystack.end();
}

// The first active render endpoint whose friendly name contains sWant, or
// whose id is sWant (case insensitive), so the game can play to a particular
// output without it being the Windows default: e.g. "Speakers (Realtek" to go
// around an audio enhancer that is the default device. Null if none matches.
IMMDevice* FindRenderEndpoint(
    IMMDeviceEnumerator* pEnumerator, const std::string& sWant) {
  IMMDeviceCollection* pCollection = nullptr;
  if (FAILED(pEnumerator->EnumAudioEndpoints(
          eRender, DEVICE_STATE_ACTIVE, &pCollection))) {
    return nullptr;
  }
  const std::wstring sWantW = RStringToWstring(sWant);
  IMMDevice* pFound = nullptr;
  UINT iCount = 0;
  pCollection->GetCount(&iCount);
  for (UINT i = 0; i < iCount && pFound == nullptr; ++i) {
    IMMDevice* pDevice = nullptr;
    if (FAILED(pCollection->Item(i, &pDevice))) {
      continue;
    }
    std::wstring sName, sId;
    LPWSTR pId = nullptr;
    if (SUCCEEDED(pDevice->GetId(&pId))) {
      sId = pId;
      CoTaskMemFree(pId);
    }
    IPropertyStore* pProps = nullptr;
    if (SUCCEEDED(pDevice->OpenPropertyStore(STGM_READ, &pProps))) {
      PROPVARIANT name;
      PropVariantInit(&name);
      if (SUCCEEDED(pProps->GetValue(PKEY_Device_FriendlyName, &name)) &&
          name.vt == VT_LPWSTR) {
        sName = name.pwszVal;
      }
      PropVariantClear(&name);
      pProps->Release();
    }
    // The name by substring; the id only whole, since every id contains
    // digits a short value would match.
    const bool bIdMatch =
        sId.size() == sWantW.size() && ContainsNoCase(sId, sWantW);
    if (ContainsNoCase(sName, sWantW) || bIdMatch) {
      pFound = pDevice;
      LOG->Info("WASAPI: using \"%s\"", WStringToRString(sName).c_str());
    } else {
      pDevice->Release();
    }
  }
  pCollection->Release();
  return pFound;
}
}  // namespace

RageSoundDriver_WASAPI::RageSoundDriver_WASAPI()
    : m_iSampleRate(0),
      m_iBufferSizeFrames(0),
      m_bFloat(true),
      m_pAudioClient(nullptr),
      m_pRenderClient(nullptr),
      m_pAudioClock(nullptr),
      m_iAudioClockFreq(0),
      m_hAudioEvent(INVALID_HANDLE_VALUE),
      m_bShutdownMixerThread(false),
      m_bOwnsComInit(false) {}

RageSoundDriver_WASAPI::~RageSoundDriver_WASAPI() {
  if (m_MixingThread.IsCreated()) {
    m_bShutdownMixerThread = true;
    if (m_hAudioEvent != INVALID_HANDLE_VALUE) {
      SetEvent(m_hAudioEvent);  // Wake up thread
    }
    m_MixingThread.Wait();
  }

  FreeWASAPI();

  if (m_hAudioEvent != INVALID_HANDLE_VALUE) {
    CloseHandle(m_hAudioEvent);
  }
}

void RageSoundDriver_WASAPI::FreeWASAPI() {
  if (m_pAudioClient) {
    m_pAudioClient->Stop();
  }
  if (m_pAudioClock) {
    m_pAudioClock->Release();
    m_pAudioClock = nullptr;
  }
  if (m_pRenderClient) {
    m_pRenderClient->Release();
    m_pRenderClient = nullptr;
  }
  if (m_pAudioClient) {
    m_pAudioClient->Release();
    m_pAudioClient = nullptr;
  }
  if (m_bOwnsComInit) {
    CoUninitialize();
    m_bOwnsComInit = false;
  }
}

bool RageSoundDriver_WASAPI::InitWASAPI(std::string& sError) {
  HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
    sError = hr_ssprintf(hr, "CoInitializeEx failed");
    return false;
  }
  m_bOwnsComInit = SUCCEEDED(hr);

  IMMDeviceEnumerator* pEnumerator = nullptr;
  hr = CoCreateInstance(
      __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
      IID_PPV_ARGS(&pEnumerator));
  if (FAILED(hr)) {
    sError = hr_ssprintf(hr, "CoCreateInstance(MMDeviceEnumerator) failed");
    FreeWASAPI();
    return false;
  }

  IMMDevice* pDevice = nullptr;
  const std::string sDevice = PREFSMAN->m_iSoundDevice.Get();
  // WaveOut reads SoundDevice as device numbers ("2" or "1,3"); a value like
  // that left over from WaveOut isn't an endpoint name.
  const bool bDeviceNumbers =
      sDevice.find_first_not_of("0123456789, ") == std::string::npos;
  if (!sDevice.empty() && !bDeviceNumbers) {
    pDevice = FindRenderEndpoint(pEnumerator, sDevice);
    if (pDevice == nullptr) {
      LOG->Warn(
          "WASAPI: no output matches SoundDevice \"%s\"; using the default",
          sDevice.c_str());
    }
  }
  hr = pDevice != nullptr
           ? S_OK
           : pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
  if (FAILED(hr)) {
    pEnumerator->Release();
    sError = hr_ssprintf(hr, "GetDefaultAudioEndpoint failed");
    FreeWASAPI();
    return false;
  }
  pEnumerator->Release();

  hr = pDevice->Activate(
      __uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&m_pAudioClient);
  pDevice->Release();
  if (FAILED(hr)) {
    sError = hr_ssprintf(hr, "Activate(IAudioClient) failed");
    FreeWASAPI();
    return false;
  }

  WAVEFORMATEX* pwfx = nullptr;
  hr = m_pAudioClient->GetMixFormat(&pwfx);
  if (FAILED(hr)) {
    sError = hr_ssprintf(hr, "GetMixFormat failed");
    FreeWASAPI();
    return false;
  }

  m_iSampleRate = pwfx->nSamplesPerSec;
  int iChannels = pwfx->nChannels;

  if (pwfx->wFormatTag != WAVE_FORMAT_EXTENSIBLE) {
    sError = ssprintf("Unsupported format tag: %d", pwfx->wFormatTag);
    CoTaskMemFree(pwfx);
    FreeWASAPI();
    return false;
  }

  WAVEFORMATEXTENSIBLE* pEx = (WAVEFORMATEXTENSIBLE*)pwfx;
  if (pEx->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) {
    m_bFloat = true;
  } else if (
      pEx->SubFormat == KSDATAFORMAT_SUBTYPE_PCM &&
      pEx->Format.wBitsPerSample == 16) {
    m_bFloat = false;
  } else {
    sError = "Unsupported format (expected float or 16-bit PCM)";
    CoTaskMemFree(pwfx);
    FreeWASAPI();
    return false;
  }

  REFERENCE_TIME hnsRequestedDuration = 0;
  if (PREFSMAN->m_iSoundWriteAhead) {
    // WriteAhead is in frames. Convert to 100-nanosecond units.
    hnsRequestedDuration = (REFERENCE_TIME)PREFSMAN->m_iSoundWriteAhead *
                           10000000ULL / m_iSampleRate;
  }

  hr = m_pAudioClient->Initialize(
      AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
      hnsRequestedDuration, 0, pwfx, nullptr);

  CoTaskMemFree(pwfx);

  if (FAILED(hr)) {
    sError = hr_ssprintf(hr, "Initialize(IAudioClient) failed");
    FreeWASAPI();
    return false;
  }

  m_hAudioEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
  if (m_hAudioEvent == NULL) {
    sError = werr_ssprintf(GetLastError(), "CreateEvent failed");
    FreeWASAPI();
    return false;
  }

  hr = m_pAudioClient->SetEventHandle(m_hAudioEvent);
  if (FAILED(hr)) {
    sError = hr_ssprintf(hr, "SetEventHandle failed");
    FreeWASAPI();
    return false;
  }

  hr = m_pAudioClient->GetBufferSize(&m_iBufferSizeFrames);
  if (FAILED(hr)) {
    sError = hr_ssprintf(hr, "GetBufferSize failed");
    FreeWASAPI();
    return false;
  }

  hr = m_pAudioClient->GetService(IID_PPV_ARGS(&m_pRenderClient));
  if (FAILED(hr)) {
    sError = hr_ssprintf(hr, "GetService(IAudioRenderClient) failed");
    FreeWASAPI();
    return false;
  }

  hr = m_pAudioClient->GetService(IID_PPV_ARGS(&m_pAudioClock));
  if (FAILED(hr)) {
    sError = hr_ssprintf(hr, "GetService(IAudioClock) failed");
    FreeWASAPI();
    return false;
  }

  hr = m_pAudioClock->GetFrequency(&m_iAudioClockFreq);
  if (FAILED(hr)) {
    sError = hr_ssprintf(hr, "GetFrequency failed");
    FreeWASAPI();
    return false;
  }

  LOG->Info(
      "WASAPI: Shared mode, %d channels, %d Hz, %s, buffer size %d frames",
      iChannels, m_iSampleRate, m_bFloat ? "Float" : "Int16",
      m_iBufferSizeFrames);

  return true;
}

std::string RageSoundDriver_WASAPI::Init() {
  std::string sError;
  if (!InitWASAPI(sError)) {
    return sError;
  }

  // Set decode buffer size.
  // We want it to be at least as big as the WASAPI buffer.
  SetDecodeBufferSize(m_iBufferSizeFrames * 3 / 2);
  StartDecodeThread();

  m_MixingThread.SetName("WASAPI Mixer Thread");
  m_MixingThread.Create(MixerThread_start, this);

  return "";
}

int RageSoundDriver_WASAPI::MixerThread_start(void* p) {
  ((RageSoundDriver_WASAPI*)p)->MixerThread();
  return 0;
}

void RageSoundDriver_WASAPI::MixerThread() {
  if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL)) {
    if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL)) {
      LOG->Warn(
          werr_ssprintf(
              GetLastError(), "Failed to set WASAPI sound thread priority")
              .c_str());
    }
  }

  HRESULT hr = m_pAudioClient->Start();
  if (FAILED(hr)) {
    LOG->Warn(hr_ssprintf(hr, "Failed to start IAudioClient").c_str());
    return;
  }

  int64_t iHardwareFrame = 0;  // Total frames played/mixed so far

  while (!m_bShutdownMixerThread) {
    DWORD waitResult =
        WaitForSingleObject(m_hAudioEvent, 2000);  // 2 second timeout safety
    if (waitResult == WAIT_TIMEOUT) {
      LOG->Warn("WASAPI: Timeout waiting for audio event");
      m_pAudioClient->Stop();
      m_pAudioClient->Start();
      continue;
    }

    if (m_bShutdownMixerThread) {
      break;
    }

    UINT32 padding = 0;
    hr = m_pAudioClient->GetCurrentPadding(&padding);
    if (FAILED(hr)) {
      LOG->Warn(hr_ssprintf(hr, "GetCurrentPadding failed").c_str());
      continue;
    }

    UINT32 framesAvailable = m_iBufferSizeFrames - padding;
    if (framesAvailable == 0) {
      continue;
    }

    BYTE* pData = nullptr;
    hr = m_pRenderClient->GetBuffer(framesAvailable, &pData);
    if (FAILED(hr)) {
      LOG->Warn(hr_ssprintf(hr, "GetBuffer failed").c_str());
      continue;
    }

    int64_t iCurrentFrame = GetPosition();

    if (m_bFloat) {
      this->Mix((float*)pData, framesAvailable, iHardwareFrame, iCurrentFrame);
    } else {
      this->Mix(
          (int16_t*)pData, framesAvailable, iHardwareFrame, iCurrentFrame);
    }

    hr = m_pRenderClient->ReleaseBuffer(framesAvailable, 0);
    if (FAILED(hr)) {
      LOG->Warn(hr_ssprintf(hr, "ReleaseBuffer failed").c_str());
    }

    iHardwareFrame += framesAvailable;
  }

  m_pAudioClient->Stop();
}

int64_t RageSoundDriver_WASAPI::GetPosition() const {
  UINT64 position = 0;
  if (FAILED(m_pAudioClock->GetPosition(&position, nullptr))) {
    return 0;
  }
  return (int64_t)(position * (UINT64)m_iSampleRate / m_iAudioClockFreq);
}

float RageSoundDriver_WASAPI::GetPlayLatency() const {
  REFERENCE_TIME latency = 0;
  if (FAILED(m_pAudioClient->GetStreamLatency(&latency))) {
    return 0.0f;
  }
  return (float)latency / 10000000.0f;
}

int RageSoundDriver_WASAPI::GetSampleRate() const { return m_iSampleRate; }

void RageSoundDriver_WASAPI::SetupDecodingThread() {
  if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL)) {
    LOG->Warn(
        werr_ssprintf(
            GetLastError(), "Failed to set WASAPI decoding thread priority")
            .c_str());
  }
}
