#include "RageSoundDriver_WASAPI.h"
#include "RageSoundDriver_WASAPI_SubDriver.h"

// clang-format off
#include <windows.h>
#include <audioclient.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
// clang-format on

#include "PrefsManager.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "archutils/Win32/DirectXHelpers.h"
#include "archutils/Win32/ErrorStrings.h"
#include "global.h"

REGISTER_SOUND_DRIVER_CLASS2("WASAPI", WASAPI);

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

bool RageSoundDriver_WASAPI::TrySubDriver(
    std::unique_ptr<WasapiSubDriver> pCandidateSubDriver,
    const WasapiInitParams& params, HRESULT& hrInitialize) {

  LOG->Info("WASAPI: Attempting to initialize `%s` mode",
            pCandidateSubDriver->GetModeName());

  HRESULT hrSub = pCandidateSubDriver->InitializeStream(params);
  if (SUCCEEDED(hrSub)) {
    m_pSubDriver = std::move(pCandidateSubDriver);
    hrInitialize = hrSub;
    return true;
  }

  LOG->Warn(hr_ssprintf(
                hrSub, "WASAPI: %s mode unsuccessful; trying fallback mode",
                pCandidateSubDriver->GetModeName())
                .c_str());
  return false;
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
  hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
  pEnumerator->Release();
  if (FAILED(hr)) {
    sError = hr_ssprintf(hr, "GetDefaultAudioEndpoint failed");
    FreeWASAPI();
    return false;
  }

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

  WasapiInitParams params = {
      m_pAudioClient,
      pwfx,
      (int)pwfx->nSamplesPerSec,
      PREFSMAN->m_iSoundWriteAhead,
  };
  m_pSubDriver.reset();

  if (!TrySubDriver(CreateWasapiSharedLLSubDriver(), params, hr) &&
      !TrySubDriver(CreateWasapiSharedSubDriver(), params, hr) &&
      !TrySubDriver(CreateWasapiExclusiveSubDriver(), params, hr)) {
    sError = "Failed to initialize WASAPI stream mode";
    CoTaskMemFree(pwfx);
    FreeWASAPI();
    return false;
  }

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

  m_iSampleRate = pwfx->nSamplesPerSec;
  int iChannels = pwfx->nChannels;

  CoTaskMemFree(pwfx);

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

  float reportedPlayLatency = GetPlayLatency();
  LOG->Info("WASAPI: Reported audio play latency: %f", reportedPlayLatency);
  LOG->Info(
      "WASAPI: %s mode, %d channels, %d Hz, %s, buffer size %d frames",
      m_pSubDriver ? m_pSubDriver->GetModeName() : "Unknown",
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
  UINT32 decodeBufferSizeFrames = m_iBufferSizeFrames * 3 / 2;
  if (decodeBufferSizeFrames < 768) {
    decodeBufferSizeFrames = 768;
  }

  SetDecodeBufferSize(decodeBufferSizeFrames);
  StartDecodeThread();

  m_MixingThread.SetName("WASAPI Mixer Thread");
  m_MixingThread.Create(MixerThread_start, this);

  return "";
}

int RageSoundDriver_WASAPI::MixerThread_start(void* p) {
  ((RageSoundDriver_WASAPI*)p)->MixerThread();
  return 0;
}

bool RageSoundDriver_WASAPI::WriteFrames(
    UINT32 iFrames, int64_t iHardwareFrame, int64_t iCurrentFrame,
    const char* sPhase) {
  HRESULT hr = S_OK;
  BYTE* pData = nullptr;
  hr = m_pRenderClient->GetBuffer(iFrames, &pData);
  if (FAILED(hr)) {
    LOG->Warn(hr_ssprintf(hr, "GetBuffer (%s) failed", sPhase).c_str());
    return false;
  }

  if (m_bFloat) {
    this->Mix((float*)pData, iFrames, iHardwareFrame, iCurrentFrame);
  } else {
    this->Mix((int16_t*)pData, iFrames, iHardwareFrame, iCurrentFrame);
  }

  hr = m_pRenderClient->ReleaseBuffer(iFrames, 0);
  if (FAILED(hr)) {
    LOG->Warn(hr_ssprintf(hr, "ReleaseBuffer (%s) failed", sPhase).c_str());
    return false;
  }

  return true;
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

  HRESULT hr = S_OK;
  int64_t iHardwareFrame = 0;  // Total frames played/mixed so far

  if (m_pSubDriver && m_pSubDriver->RequiresInitialFill()) {
    if (!WriteFrames(m_iBufferSizeFrames, iHardwareFrame, 0, "initial fill")) {
      return;
    }
    iHardwareFrame += m_iBufferSizeFrames;
  }

  hr = m_pAudioClient->Start();
  if (FAILED(hr)) {
    LOG->Warn(hr_ssprintf(hr, "Failed to start IAudioClient").c_str());
    return;
  }

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

    UINT32 framesAvailable = m_iBufferSizeFrames;
    if (!m_pSubDriver || m_pSubDriver->UsesPaddingFrames()) {
      UINT32 padding = 0;
      hr = m_pAudioClient->GetCurrentPadding(&padding);
      if (FAILED(hr)) {
        LOG->Warn(hr_ssprintf(hr, "GetCurrentPadding failed").c_str());
        continue;
      }

      framesAvailable = m_iBufferSizeFrames - padding;
    }

    if (framesAvailable == 0) {
      continue;
    }

    int64_t iCurrentFrame = GetPosition();
    if (!WriteFrames(framesAvailable, iHardwareFrame, iCurrentFrame, "streaming")) {
      continue;
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
  float result = latency / 10000000.0f;
  return result;
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
