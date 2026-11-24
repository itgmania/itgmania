#include "Threads_Win32.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>

#include "RageThreads.h"
#include "RageTimer.h"
#include "RageUtil.h"
#include "archutils/Win32/ErrorStrings.h"
#include "global.h"

const int MAX_THREADS = 128;

static std::unique_ptr<MutexImpl_Win32> g_pThreadIdMutex;
static bool g_ThreadIdMutexInitialized = false;
static std::mutex g_ThreadIdMutexInitLock;

static void InitThreadIdMutex() {
  std::lock_guard<std::mutex> lock(g_ThreadIdMutexInitLock);
  if (g_ThreadIdMutexInitialized) {
    return;
  }

  std::unique_ptr<MutexImpl_Win32> temp = std::make_unique<MutexImpl_Win32>();
  g_ThreadIdMutexInitialized = true;

  g_pThreadIdMutex = std::move(temp);
}

static uint64_t g_ThreadIds[MAX_THREADS];
static HANDLE g_ThreadHandles[MAX_THREADS];
static std::mutex g_ThreadDataMutex;

HANDLE Win32ThreadIdToHandle(uint64_t id) {
  std::lock_guard<std::mutex> lock(g_ThreadDataMutex);
  for (int i = 0; i < MAX_THREADS; ++i) {
    if (g_ThreadIds[i] == id) {
      return g_ThreadHandles[i];
    }
  }

  return nullptr;
}

void ThreadImpl_Win32::Halt(bool kill) {
  if (kill) {
    ExitThread(0);  // 0 indicates the thread was terminated
  } else {
    SuspendThread(thread_handle_);
  }
}

void ThreadImpl_Win32::Resume() { ResumeThread(thread_handle_); }

uint64_t ThreadImpl_Win32::GetThreadId() const {
  return static_cast<uint64_t>(thread_id_);
}

int ThreadImpl_Win32::Wait() {
  WaitForSingleObject(thread_handle_, INFINITE);

  DWORD ret;
  GetExitCodeThread(thread_handle_, &ret);

  CloseHandle(thread_handle_);
  thread_handle_ = nullptr;

  return ret;
}

// SetThreadName magic comes from VirtualDub.
#define MS_VC_EXCEPTION 0x406d1388

typedef struct tagTHREADNAME_INFO {
  DWORD type_;       // must be 0x1000
  LPCSTR name_;      // pointer to name (in same addr space)
  DWORD thread_id_;  // thread ID (-1 caller thread)
  DWORD flags_;      // reserved for future use, must be zero
} THREADNAME_INFO;

static void SetThreadName(DWORD thread_id, LPCTSTR thread_name) {
#if defined(_MSC_VER)
  THREADNAME_INFO info;
  info.type_ = 0x1000;
  info.name_ = thread_name;
  info.thread_id_ = thread_id;
  info.flags_ = 0;

#ifdef _DEBUG
  __try {
    RaiseException(
        MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(DWORD), (ULONG_PTR*)&info);
  } __except (
      EXCEPTION_CONTINUE_EXECUTION)  // NOTE(sukibaby): compiler suggests trying
                                     // EXCEPTION_EXECUTE_HANDLER to avoid
                                     // possible infinite loop.
  {
  }
#endif

#elif defined(__GNUC__)
  pthread_setname_np(pthread_self(), thread_name);
#endif
}

static DWORD WINAPI StartThread(LPVOID data) {
  ThreadImpl_Win32* self = static_cast<ThreadImpl_Win32*>(data);

  SetThreadName(GetCurrentThreadId(), RageThread::GetCurrentThreadName());

  DWORD ret = static_cast<DWORD>(self->func_(self->data_));

  std::lock_guard<std::mutex> lock(g_ThreadDataMutex);
  for (int i = 0; i < MAX_THREADS; ++i) {
    if (g_ThreadIds[i] == RageThread::GetCurrentThreadID()) {
      g_ThreadHandles[i] = nullptr;
      g_ThreadIds[i] = 0;
      break;
    }
  }

  return ret;
}

static int GetOpenSlot(uint64_t id) {
  InitThreadIdMutex();

  g_pThreadIdMutex->Lock();

  // Find an open slot in g_ThreadIds.
  int slot = 0;
  while (slot < MAX_THREADS && g_ThreadIds[slot] != 0) {
    ++slot;
  }
  ASSERT(slot < MAX_THREADS);

  g_ThreadIds[slot] = id;

  g_pThreadIdMutex->Unlock();

  return slot;
}

ThreadImpl* MakeThisThread() {
  std::unique_ptr<ThreadImpl_Win32> thread =
      std::make_unique<ThreadImpl_Win32>();

  SetThreadName(GetCurrentThreadId(), RageThread::GetCurrentThreadName());

  const HANDLE cur_proc = GetCurrentProcess();
  int ret = DuplicateHandle(
      cur_proc, GetCurrentThread(), cur_proc, &thread->thread_handle_, 0, false,
      DUPLICATE_SAME_ACCESS);

  if (!ret) {
    thread->thread_handle_ = nullptr;
  }

  thread->thread_id_ = GetCurrentThreadId();

  int slot = GetOpenSlot(GetCurrentThreadId());
  g_ThreadHandles[slot] = thread->thread_handle_;

  return thread.release();
}

ThreadImpl* MakeThread(
    int (*func)(void* data), void* data, uint64_t* thread_id) {
  std::unique_ptr<ThreadImpl_Win32> thread =
      std::make_unique<ThreadImpl_Win32>();
  thread->func_ = func;
  thread->data_ = data;

  thread->thread_handle_ = CreateThread(
      nullptr, 0, &StartThread, thread.get(), CREATE_SUSPENDED,
      &thread->thread_id_);
  *thread_id = static_cast<uint64_t>(thread->thread_id_);
  ASSERT_M(
      thread->thread_handle_ != nullptr,
      ssprintf("%s", werr_ssprintf(GetLastError(), "CreateThread").c_str()));

  int slot = GetOpenSlot(thread->thread_id_);
  g_ThreadHandles[slot] = thread->thread_handle_;

  int ret = ResumeThread(thread->thread_handle_);
  ASSERT_M(
      ret == 1,
      ssprintf("%s", werr_ssprintf(GetLastError(), "ResumeThread").c_str()));

  return thread.release();
}

MutexImpl_Win32::MutexImpl_Win32() {
  if (!InitializeCriticalSectionAndSpinCount(&mutex, kSpinCount)) {
    FAIL_M(werr_ssprintf(
        GetLastError(),
        "Unexpected failure in InitializeCriticalSectionAndSpinCount which "
        "should have been impossible!"));
  }
}

MutexImpl_Win32::~MutexImpl_Win32() { DeleteCriticalSection(&mutex); }

bool MutexImpl_Win32::Lock() {
  EnterCriticalSection(&mutex);  // guaranteed
  return true;
}

bool MutexImpl_Win32::TryLock() { return TryEnterCriticalSection(&mutex) != 0; }
void MutexImpl_Win32::Unlock() { LeaveCriticalSection(&mutex); }

uint64_t GetThisThreadId() { return GetCurrentThreadId(); }

uint64_t GetInvalidThreadId() { return 0; }

MutexImpl* MakeMutex() { return new MutexImpl_Win32(); }

EventImpl_Win32::EventImpl_Win32(MutexImpl_Win32* pParent) {
  m_pParent = pParent;
  InitializeConditionVariable(&m_ThreadConditionVariable);
}

EventImpl_Win32::~EventImpl_Win32() {
  // Nothing to free
}

bool EventImpl_Win32::Wait(RageTimer* pTimeout) {
  DWORD iMilliseconds = INFINITE;
  if (pTimeout != nullptr) {
    float fSecondsInFuture = -pTimeout->Ago();
    iMilliseconds = static_cast<DWORD>(
        std::max(0, static_cast<int>(fSecondsInFuture * 1000)));
  }

  bool success =
      SleepConditionVariableCS(
          &m_ThreadConditionVariable, &m_pParent->mutex, iMilliseconds) != 0;

  if (!success) {
    ASSERT_M(
        GetLastError() == ERROR_TIMEOUT,
        ssprintf(
            "%s", werr_ssprintf(
                      GetLastError(),
                      "Waited as instructed, but received a timeout signal!")
                      .c_str()));
  }

  return success;
}

void EventImpl_Win32::Signal() {
  WakeConditionVariable(&m_ThreadConditionVariable);
}

void EventImpl_Win32::Broadcast() {
  WakeAllConditionVariable(&m_ThreadConditionVariable);
}

EventImpl* MakeEvent(MutexImpl* mutex) {
  MutexImpl_Win32* win32_mutex = static_cast<MutexImpl_Win32*>(mutex);

  return new EventImpl_Win32(win32_mutex);
}

SemaImpl_Win32::SemaImpl_Win32(int initial_value) {
  sem_ = CreateSemaphore(nullptr, initial_value, 999999999, nullptr);
}

SemaImpl_Win32::~SemaImpl_Win32() { CloseHandle(sem_); }

void SemaImpl_Win32::Post() { ReleaseSemaphore(sem_, 1, nullptr); }

bool SemaImpl_Win32::Wait() {
  DWORD result = WaitForSingleObject(sem_, INFINITE);

  switch (result) {
    case WAIT_OBJECT_0:
      return true;
    case WAIT_FAILED:
      FAIL_M(werr_ssprintf(GetLastError(), "WaitForSingleObject"));
    default:
      FAIL_M("Unexpected WaitForSingleObject return");
  }
  return false;
}

SemaImpl* MakeSemaphore(int initial_value) {
  return new SemaImpl_Win32(initial_value);
}

/*
 * (c) 2001-2004 Glenn Maynard
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, provided that the above
 * copyright notice(s) and this permission notice appear in all copies of
 * the Software and that both the above copyright notice(s) and this
 * permission notice appear in supporting documentation.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
 * INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
