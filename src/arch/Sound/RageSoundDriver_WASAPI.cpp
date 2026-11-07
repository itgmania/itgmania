#include "RageSoundDriver_WASAPI.h"

// clang-format off
#include <windows.h>
#include <audioclient.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
// clang-format on

#include <algorithm>
#include <cmath>
#include <limits>

#include "PrefsManager.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "archutils/Win32/DirectXHelpers.h"
#include "archutils/Win32/ErrorStrings.h"
#include "global.h"

REGISTER_SOUND_DRIVER_CLASS2("WASAPI", WASAPI);

namespace {
// The performance counter in microseconds: the time base of the timestamps
// IAudioClock::GetPosition returns (in 100 ns units).
int64_t NowQpcUs() {
  static const int64_t iFreq = []() {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return static_cast<int64_t>(f.QuadPart);
  }();
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  const int64_t iTicks = now.QuadPart;
  return (iTicks / iFreq) * 1000000 + (iTicks % iFreq) * 1000000 / iFreq;
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
      m_iPeriodFrames(0),
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

  REFERENCE_TIME hnsPeriod = 0;
  if (FAILED(m_pAudioClient->GetDevicePeriod(&hnsPeriod, nullptr)) ||
      hnsPeriod <= 0) {
    hnsPeriod = 100000;  // the usual 10 ms
  }
  m_iPeriodFrames = static_cast<int>(hnsPeriod * m_iSampleRate / 10000000);
  m_Clock.Reset(m_iSampleRate, m_iPeriodFrames);

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

    // Stamp the consumption as soon as it is read.
    const int64_t iEventUs = NowQpcUs();
    UINT64 iClockPosition = 0, iClockQpc = 0;
    const bool bClock =
        SUCCEEDED(m_pAudioClock->GetPosition(&iClockPosition, &iClockQpc));
    m_Clock.AddEvent(
        iEventUs, iHardwareFrame - padding, bClock,
        static_cast<int64_t>(
            iClockPosition * (UINT64)m_iSampleRate / m_iAudioClockFreq),
        static_cast<int64_t>(iClockQpc / 10));

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

void RageSoundDriver_WASAPI::ConsumedClock::Reset(
    int iSampleRate, int iPeriodFrames) {
  std::lock_guard<std::mutex> lock(m_Mutex);
  m_iSampleRate = iSampleRate;
  m_iPeriodFrames = std::max(iPeriodFrames, 1);
  m_iCalibrationSeen = 0;
  m_iCalibrationTries = 0;
  m_iLatencyFrames = 0;
  m_bCalibrated = false;
  m_iHistoryCount = 0;
  m_iHistoryNext = 0;
  m_iLowEvents = 0;
}

void RageSoundDriver_WASAPI::ConsumedClock::AddEvent(
    int64_t iEventUs, int64_t iConsumedFrames, bool bClockValid,
    int64_t iClockFrames, int64_t iClockUs) {
  std::lock_guard<std::mutex> lock(m_Mutex);
  const double fFramesPerUs = m_iSampleRate / 1000000.0;
  if (!m_bCalibrated) {
    // The first events fill the engine's buffers.
    constexpr int kSkipEvents = 20;
    if (++m_iCalibrationSeen <= kSkipEvents) {
      return;
    }
    // Compare like with like: positions follow the most prompt event, so
    // take the most prompt consumption reading and the most advanced clock
    // reading (a clock that moves in steps lags between them), each against
    // its own timestamp.
    if (m_iCalibrationSeen == kSkipEvents + 1) {
      m_fBestConsumed = -std::numeric_limits<double>::infinity();
      m_fBestClock = -std::numeric_limits<double>::infinity();
    }
    m_fBestConsumed = std::max(
        m_fBestConsumed, static_cast<double>(iConsumedFrames) -
                             fFramesPerUs * static_cast<double>(iEventUs));
    if (bClockValid) {
      m_fBestClock = std::max(
          m_fBestClock, static_cast<double>(iClockFrames) -
                            fFramesPerUs * static_cast<double>(iClockUs));
    }
    if (m_iCalibrationSeen < kSkipEvents + kCalibrationEvents) {
      return;
    }
    const double fLatency = m_fBestConsumed - m_fBestClock;
    // Audio can't be heard before the engine takes it, and no engine holds
    // a second of it; outside that the clock was already off, so try again.
    constexpr int kMaxTries = 5;
    const bool bSane = fLatency >= 0 && fLatency <= m_iSampleRate;
    if (!bSane && ++m_iCalibrationTries < kMaxTries) {
      m_iCalibrationSeen = kSkipEvents;
      return;
    }
    m_iLatencyFrames = bSane ? std::llround(fLatency)
                             : 2 * static_cast<int64_t>(m_iPeriodFrames);
    m_bCalibrated = true;
  }

  const double fValue =
      static_cast<double>(iConsumedFrames - m_iLatencyFrames) -
      fFramesPerUs * static_cast<double>(iEventUs);
  if (m_iHistoryCount > 0) {
    const double fBest =
        *std::max_element(m_History, m_History + m_iHistoryCount);
    // A late event reads low once; when the engine stopped taking audio for a
    // while, every event after it reads low, and the older events no longer
    // describe playback.
    if (fValue < fBest - 1.5 * m_iPeriodFrames) {
      if (++m_iLowEvents >= 2) {
        m_History[0] = m_fLastValue;
        m_iHistoryNext = 1;
        m_iHistoryCount = 1;
        m_iLowEvents = 0;
      }
    } else {
      m_iLowEvents = 0;
    }
  }
  m_fLastValue = fValue;
  m_History[m_iHistoryNext] = fValue;
  m_iHistoryNext = (m_iHistoryNext + 1) % kHistory;
  m_iHistoryCount = std::min(m_iHistoryCount + 1, kHistory);
  m_iLastConsumed = iConsumedFrames;
}

int64_t RageSoundDriver_WASAPI::ConsumedClock::GetPosition(
    int64_t iNowUs) const {
  std::lock_guard<std::mutex> lock(m_Mutex);
  if (!m_bCalibrated || m_iHistoryCount == 0) {
    return -1;
  }
  const double fFramesPerUs = m_iSampleRate / 1000000.0;
  const double fBest =
      *std::max_element(m_History, m_History + m_iHistoryCount);
  double fPosition = fBest + fFramesPerUs * static_cast<double>(iNowUs);
  // Until the next event the engine has only what it already took, so if it
  // stalls, playback stops at the end of that.
  const double fLimit =
      static_cast<double>(m_iLastConsumed - m_iLatencyFrames) +
      static_cast<double>(
          std::max<int64_t>(m_iLatencyFrames, 2 * m_iPeriodFrames));
  fPosition = std::min(fPosition, fLimit);
  return std::max<int64_t>(std::llround(std::floor(fPosition)), 0);
}

int64_t RageSoundDriver_WASAPI::GetPosition() const {
  const int64_t iConsumedPosition = m_Clock.GetPosition(NowQpcUs());
  if (iConsumedPosition >= 0) {
    return iConsumedPosition;
  }

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
