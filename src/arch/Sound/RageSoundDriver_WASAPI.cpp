#include "RageSoundDriver_WASAPI.h"

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
    : sample_rate_(0),
      buffer_size_frames_(0),
      float_(true),
      audio_client_(nullptr),
      render_client_(nullptr),
      audio_clock_(nullptr),
      audio_clock_freq_(0),
      audio_event_(INVALID_HANDLE_VALUE),
      shutdown_mixer_thread_(false),
      owns_com_init_(false) {}

RageSoundDriver_WASAPI::~RageSoundDriver_WASAPI() {
  if (mixing_thread_.IsCreated()) {
    shutdown_mixer_thread_ = true;
    if (audio_event_ != INVALID_HANDLE_VALUE) {
      SetEvent(audio_event_);  // Wake up thread
    }
    mixing_thread_.Wait();
  }

  FreeWASAPI();

  if (audio_event_ != INVALID_HANDLE_VALUE) {
    CloseHandle(audio_event_);
  }
}

void RageSoundDriver_WASAPI::FreeWASAPI() {
  if (audio_client_) {
    audio_client_->Stop();
  }
  if (audio_clock_) {
    audio_clock_->Release();
    audio_clock_ = nullptr;
  }
  if (render_client_) {
    render_client_->Release();
    render_client_ = nullptr;
  }
  if (audio_client_) {
    audio_client_->Release();
    audio_client_ = nullptr;
  }
  if (owns_com_init_) {
    CoUninitialize();
    owns_com_init_ = false;
  }
}

bool RageSoundDriver_WASAPI::InitWASAPI(std::string& sError) {
  HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
    sError = hr_ssprintf(hr, "CoInitializeEx failed");
    return false;
  }
  owns_com_init_ = SUCCEEDED(hr);

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
      __uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&audio_client_);
  pDevice->Release();
  if (FAILED(hr)) {
    sError = hr_ssprintf(hr, "Activate(IAudioClient) failed");
    FreeWASAPI();
    return false;
  }

  WAVEFORMATEX* pwfx = nullptr;
  hr = audio_client_->GetMixFormat(&pwfx);
  if (FAILED(hr)) {
    sError = hr_ssprintf(hr, "GetMixFormat failed");
    FreeWASAPI();
    return false;
  }

  sample_rate_ = pwfx->nSamplesPerSec;
  int iChannels = pwfx->nChannels;

  if (pwfx->wFormatTag != WAVE_FORMAT_EXTENSIBLE) {
    sError = ssprintf("Unsupported format tag: %d", pwfx->wFormatTag);
    CoTaskMemFree(pwfx);
    FreeWASAPI();
    return false;
  }

  WAVEFORMATEXTENSIBLE* pEx = (WAVEFORMATEXTENSIBLE*)pwfx;
  if (pEx->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) {
    float_ = true;
  } else if (
      pEx->SubFormat == KSDATAFORMAT_SUBTYPE_PCM &&
      pEx->Format.wBitsPerSample == 16) {
    float_ = false;
  } else {
    sError = "Unsupported format (expected float or 16-bit PCM)";
    CoTaskMemFree(pwfx);
    FreeWASAPI();
    return false;
  }

  REFERENCE_TIME hnsRequestedDuration = 0;
  if (PREFSMAN->sound_write_ahead_) {
    // WriteAhead is in frames. Convert to 100-nanosecond units.
    hnsRequestedDuration = (REFERENCE_TIME)PREFSMAN->sound_write_ahead_ *
                           10000000ULL / sample_rate_;
  }

  hr = audio_client_->Initialize(
      AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
      hnsRequestedDuration, 0, pwfx, nullptr);

  CoTaskMemFree(pwfx);

  if (FAILED(hr)) {
    sError = hr_ssprintf(hr, "Initialize(IAudioClient) failed");
    FreeWASAPI();
    return false;
  }

  audio_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
  if (audio_event_ == NULL) {
    sError = werr_ssprintf(GetLastError(), "CreateEvent failed");
    FreeWASAPI();
    return false;
  }

  hr = audio_client_->SetEventHandle(audio_event_);
  if (FAILED(hr)) {
    sError = hr_ssprintf(hr, "SetEventHandle failed");
    FreeWASAPI();
    return false;
  }

  hr = audio_client_->GetBufferSize(&buffer_size_frames_);
  if (FAILED(hr)) {
    sError = hr_ssprintf(hr, "GetBufferSize failed");
    FreeWASAPI();
    return false;
  }

  hr = audio_client_->GetService(IID_PPV_ARGS(&render_client_));
  if (FAILED(hr)) {
    sError = hr_ssprintf(hr, "GetService(IAudioRenderClient) failed");
    FreeWASAPI();
    return false;
  }

  hr = audio_client_->GetService(IID_PPV_ARGS(&audio_clock_));
  if (FAILED(hr)) {
    sError = hr_ssprintf(hr, "GetService(IAudioClock) failed");
    FreeWASAPI();
    return false;
  }

  hr = audio_clock_->GetFrequency(&audio_clock_freq_);
  if (FAILED(hr)) {
    sError = hr_ssprintf(hr, "GetFrequency failed");
    FreeWASAPI();
    return false;
  }

  LOG->Info(
      "WASAPI: Shared mode, %d channels, %d Hz, %s, buffer size %d frames",
      iChannels, sample_rate_, float_ ? "Float" : "Int16", buffer_size_frames_);

  return true;
}

std::string RageSoundDriver_WASAPI::Init() {
  std::string sError;
  if (!InitWASAPI(sError)) {
    return sError;
  }

  // Set decode buffer size.
  // We want it to be at least as big as the WASAPI buffer.
  SetDecodeBufferSize(buffer_size_frames_ * 3 / 2);
  StartDecodeThread();

  mixing_thread_.SetName("WASAPI Mixer Thread");
  mixing_thread_.Create(MixerThread_start, this);

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

  HRESULT hr = audio_client_->Start();
  if (FAILED(hr)) {
    LOG->Warn(hr_ssprintf(hr, "Failed to start IAudioClient").c_str());
    return;
  }

  int64_t iHardwareFrame = 0;  // Total frames played/mixed so far

  while (!shutdown_mixer_thread_) {
    DWORD waitResult =
        WaitForSingleObject(audio_event_, 2000);  // 2 second timeout safety
    if (waitResult == WAIT_TIMEOUT) {
      LOG->Warn("WASAPI: Timeout waiting for audio event");
      audio_client_->Stop();
      audio_client_->Start();
      continue;
    }

    if (shutdown_mixer_thread_) {
      break;
    }

    UINT32 padding = 0;
    hr = audio_client_->GetCurrentPadding(&padding);
    if (FAILED(hr)) {
      LOG->Warn(hr_ssprintf(hr, "GetCurrentPadding failed").c_str());
      continue;
    }

    UINT32 framesAvailable = buffer_size_frames_ - padding;
    if (framesAvailable == 0) {
      continue;
    }

    BYTE* pData = nullptr;
    hr = render_client_->GetBuffer(framesAvailable, &pData);
    if (FAILED(hr)) {
      LOG->Warn(hr_ssprintf(hr, "GetBuffer failed").c_str());
      continue;
    }

    int64_t iCurrentFrame = GetPosition();

    if (float_) {
      this->Mix((float*)pData, framesAvailable, iHardwareFrame, iCurrentFrame);
    } else {
      this->Mix(
          (int16_t*)pData, framesAvailable, iHardwareFrame, iCurrentFrame);
    }

    hr = render_client_->ReleaseBuffer(framesAvailable, 0);
    if (FAILED(hr)) {
      LOG->Warn(hr_ssprintf(hr, "ReleaseBuffer failed").c_str());
    }

    iHardwareFrame += framesAvailable;
  }

  audio_client_->Stop();
}

int64_t RageSoundDriver_WASAPI::GetPosition() const {
  UINT64 position = 0;
  if (FAILED(audio_clock_->GetPosition(&position, nullptr))) {
    return 0;
  }
  return (int64_t)(position * (UINT64)sample_rate_ / audio_clock_freq_);
}

float RageSoundDriver_WASAPI::GetPlayLatency() const {
  REFERENCE_TIME latency = 0;
  if (FAILED(audio_client_->GetStreamLatency(&latency))) {
    return 0.0f;
  }
  return (float)latency / 10000000.0f;
}

int RageSoundDriver_WASAPI::GetSampleRate() const { return sample_rate_; }

void RageSoundDriver_WASAPI::SetupDecodingThread() {
  if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL)) {
    LOG->Warn(
        werr_ssprintf(
            GetLastError(), "Failed to set WASAPI decoding thread priority")
            .c_str());
  }
}
