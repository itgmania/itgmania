#include "arch/Threads/Threads.h"

#include <pthread.h>
#include <sys/time.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "RageLog.h"
#include "RageThreads.h"
#include "RageTimer.h"
#include "RageUtil.h"
#include "Threads_Pthreads.h"
#include "global.h"

#if defined(MACOSX)
#include "archutils/Darwin/DarwinThreadHelpers.h"
#else
#include "archutils/Common/PthreadHelpers.h"
#endif

void ThreadImpl_Pthreads::Halt(bool kill) {
  (void)kill;
  /* Linux:
   * Send a SIGSTOP to the thread. If we send a SIGKILL, pthreads will
   * "helpfully" propagate it to the other threads, and we'll get killed, too.
   * This isn't ideal, since it can cause the process to background as far as
   * the shell is concerned, so the shell prompt can display before the crash
   * handler actually displays a message. */
  SuspendThread(thread_handle_);
}

void ThreadImpl_Pthreads::Resume() {
  // Linux: Send a SIGCONT to the thread.
  ResumeThread(thread_handle_);
}

uint64_t ThreadImpl_Pthreads::GetThreadId() const { return thread_handle_; }

int ThreadImpl_Pthreads::Wait() {
  int* val;
  int ret = pthread_join(thread_, (void**)&val);
  ASSERT_M(ret == 0, ssprintf("pthread_join: %s", strerror(ret)));

  int i_ret = *val;
  delete val;
  return i_ret;
}

ThreadImpl* MakeThisThread() {
  ThreadImpl_Pthreads* thread = new ThreadImpl_Pthreads;

  thread->thread_ = pthread_self();
  thread->thread_handle_ = GetCurrentThreadId();

  return thread;
}

static void* StartThread(void* data) {
  ThreadImpl_Pthreads* p_this = (ThreadImpl_Pthreads*)data;

  p_this->thread_handle_ = GetCurrentThreadId();
  *p_this->thread_id_ = p_this->thread_handle_;

  // Tell MakeThread that we've set thread_id_, so it's safe to return.
  p_this->start_finished_sem_->Post();

  int ret = p_this->func_(p_this->data_);

  return new int(ret);
}

ThreadImpl* MakeThread(
    int (*func)(void* data), void* data, uint64_t* thread_id) {
  ThreadImpl_Pthreads* thread = new ThreadImpl_Pthreads;
  thread->func_ = func;
  thread->data_ = data;
  thread->thread_id_ = thread_id;

  thread->start_finished_sem_ = new SemaImpl_Pthreads(0);

  int ret = pthread_create(&thread->thread_, nullptr, StartThread, thread);
  ASSERT_M(
      ret == 0, ssprintf("MakeThread: pthread_create: %s", strerror(errno)));

  // Don't return until StartThread sets thread_id_.
  thread->start_finished_sem_->Wait();
  delete thread->start_finished_sem_;

  // Copy the thread name.
  const char* rawname = RageThread::GetThreadNameByID(*thread_id);
  const size_t max_name_len = sizeof(thread->name_);
  if (strlen(rawname) < max_name_len) {
    // If it fits, I sits^H^H^H^Hcopy.
    strncpy(thread->name_, rawname, max_name_len);
  } else {
    if (strstr(rawname, "Worker thread") && strchr(rawname, '(')) {
      // Special case for RageUtil_WorkerThread.cpp
      // "Worker thread (name)", e.g.
      // "Worker thread (/@mc1int/)" => "(/@mc1int/)".
      const char* workername = strchr(rawname, '(');
      strncpy(thread->name_, workername, max_name_len);
    } else {
      // Abbreviate the name by taking the first 6, last 7
      // characters and adding '..' in the middle.
      LOG->Trace(
          "Truncated thread name due to size limit of %zu: %s", max_name_len,
          rawname);
      snprintf(
          thread->name_, max_name_len, "%.6s..%s", rawname,
          &rawname[strlen(rawname) - 7]);
    }
  }
  // Ensure there is always a terminating NUL character.
  thread->name_[max_name_len - 1] = '\0';

#ifndef MACOSX
  // macOS/BSD can only set the name of the calling thread
  ret = pthread_setname_np(thread->thread_, thread->name_);
  if (ret != 0 && LOG) {
    LOG->Trace("pthead_setname_np: %s", strerror(ret));
  }
#endif

  return thread;
}

MutexImpl_Pthreads::MutexImpl_Pthreads() {
  pthread_mutex_init(&mutex_, nullptr);
}

MutexImpl_Pthreads::~MutexImpl_Pthreads() {
  int ret = pthread_mutex_destroy(&mutex_) == -1;
  ASSERT_M(ret == 0, ssprintf("Error deleting mutex: %s", strerror(errno)));
}

bool MutexImpl_Pthreads::Lock() {
#if defined(MACOSX)
  int ret;
  do {
    ret = pthread_mutex_lock(&mutex_);
  } while (ret == -1 && ret == EINTR);

  ASSERT_M(ret == 0, ssprintf("pthread_mutex_lock: %s", strerror(errno)));

  return true;
#else
  int len = 10;  // seconds
  int tries = 2;

  while (tries--) {
    /* Wait for ten seconds. If it takes longer than that, we're
     * probably deadlocked. */
    timeval tv;
    gettimeofday(&tv, nullptr);

    timespec ts;
    ts.tv_sec = tv.tv_sec + len;
    ts.tv_nsec = tv.tv_usec * 1000;
    int ret = pthread_mutex_timedlock(&mutex_, &ts);
    switch (ret) {
      case 0:
        return true;

      case EINTR:
        /* Ignore it. */
        ++tries;
        continue;

      case ETIMEDOUT:
        /* Timed out. Probably deadlocked. Try again one more time,
         * with a smaller timeout, just in case we're debugging
         * and happened to stop while waiting on the mutex. */
        len = 1;
        break;

      default:
        FAIL_M(ssprintf("pthread_mutex_timedlock: %s", strerror(errno)));
    }
  }

  return false;
#endif
}

bool MutexImpl_Pthreads::TryLock() {
  int ret = pthread_mutex_trylock(&mutex_);
  if (ret == EBUSY) {
    return false;
  }
  ASSERT_M(
      ret == 0, ssprintf("pthread_mutex_trylock failed: %s", strerror(errno)));
  return true;
}

void MutexImpl_Pthreads::Unlock() { pthread_mutex_unlock(&mutex_); }

uint64_t GetThisThreadId() { return GetCurrentThreadId(); }

uint64_t GetInvalidThreadId() { return 0; }

MutexImpl* MakeMutex() { return new MutexImpl_Pthreads(); }

/* Check if condattr_setclock is supported, and supports the clock that
 * RageTimer selected. */
#if defined(UNIX)
#include <dlfcn.h>

#include "arch/ArchHooks/ArchHooks_Unix.h"
#endif
namespace {
typedef int (*CONDATTR_SET_CLOCK)(pthread_condattr_t* attr, clockid_t clock_id);
CONDATTR_SET_CLOCK condattr_setclock = nullptr;
bool initialized = false;

#if defined(UNIX)
clockid_t GetClock() { return ArchHooks_Unix::GetClock(); }

void InitMonotonic() {
  if (initialized) {
    return;
  }
  initialized = true;

  void* lib = nullptr;

  do {
    {
      lib = dlopen(nullptr, RTLD_LAZY);
      if (lib == nullptr) {
        break;
      }

      condattr_setclock =
          (CONDATTR_SET_CLOCK)dlsym(lib, "pthread_condattr_setclock");

      if (condattr_setclock == nullptr) {
        break;
      }
    }

    // Make sure that we can set up the clock attribute.
    pthread_condattr_t condattr;
    pthread_condattr_init(&condattr);

    if (condattr_setclock(&condattr, GetClock()) != 0) {
      printf("pthread_condattr_setclock failed\n");
      pthread_condattr_destroy(&condattr);
      break;
    }
    pthread_condattr_destroy(&condattr);

    /* Everything seems to work. */
    return;
  } while (0);

  condattr_setclock = nullptr;
  if (lib != nullptr) {
    dlclose(lib);
  }
  lib = nullptr;
}
#elif defined(MACOSX)
void InitMonotonic() { initialized = true; }
clockid_t GetClock() { return CLOCK_MONOTONIC; }
#else
void InitMonotonic() { initialized = true; }

clockid_t GetClock() { return CLOCK_REALTIME; }
#endif
};  // namespace

EventImpl_Pthreads::EventImpl_Pthreads(MutexImpl_Pthreads* parent) {
  parent_ = parent;

  InitMonotonic();

  pthread_condattr_t condattr;
  pthread_condattr_init(&condattr);

  if (condattr_setclock != nullptr) {
    condattr_setclock(&condattr, GetClock());
  }

  pthread_cond_init(&cond_, &condattr);
  pthread_condattr_destroy(&condattr);
}

EventImpl_Pthreads::~EventImpl_Pthreads() { pthread_cond_destroy(&cond_); }

bool EventImpl_Pthreads::Wait(RageTimer* timeout) {
  if (timeout == nullptr) {
    pthread_cond_wait(&cond_, &parent_->mutex_);
    return true;
  }

  /* If the clock is not CLOCK_MONOTONIC, or we can't change the wait clock
   * (no condattr_setclock), pthread_cond_timedwait has an inherent race
   * condition: the system clock may change before we call it. */
  timespec abstime;
  if (condattr_setclock != nullptr || GetClock() == CLOCK_REALTIME) {
    /* If we support condattr_setclock, we'll set the condition to use
     * the same clock as RageTimer and can use it directly. If the
     * clock is CLOCK_REALTIME, that's the default anyway. */
    abstime.tv_sec = timeout->secs_;
    abstime.tv_nsec = timeout->us_ * 1000;
  } else {
    // The RageTimer clock is different than the wait clock; convert it.
    timeval tv;
    gettimeofday(&tv, nullptr);

    RageTimer timeofday(tv.tv_sec, tv.tv_usec);

    float seconds_in_future = -timeout->Ago();
    timeofday += seconds_in_future;

    abstime.tv_sec = timeofday.secs_;
    abstime.tv_nsec = timeofday.us_ * 1000;
  }

  int ret = pthread_cond_timedwait(&cond_, &parent_->mutex_, &abstime);
  return ret != ETIMEDOUT;
}

bool EventImpl_Pthreads::WaitTimeoutSupported() const { return true; }

void EventImpl_Pthreads::Signal() { pthread_cond_signal(&cond_); }

void EventImpl_Pthreads::Broadcast() { pthread_cond_broadcast(&cond_); }

EventImpl* MakeEvent(MutexImpl* mutex) {
  MutexImpl_Pthreads* pthreads_mutex = (MutexImpl_Pthreads*)mutex;

  return new EventImpl_Pthreads(pthreads_mutex);
}

// Use conditions, to work around macOS "forgetting" to implement semaphores.
SemaImpl_Pthreads::SemaImpl_Pthreads(int initial_value) {
  int ret = pthread_cond_init(&cond_, nullptr);
  ASSERT_M(
      ret == 0,
      ssprintf("SemaImpl_Pthreads: pthread_cond_init: %s", strerror(errno)));
  ret = pthread_mutex_init(&mutex_, nullptr);
  ASSERT_M(
      ret == 0,
      ssprintf("SemaImpl_Pthreads: pthread_mutex_init: %s", strerror(errno)));

  value_ = initial_value;
}

SemaImpl_Pthreads::~SemaImpl_Pthreads() {
  pthread_cond_destroy(&cond_);
  pthread_mutex_destroy(&mutex_);
}

void SemaImpl_Pthreads::Post() {
  pthread_mutex_lock(&mutex_);
  ++value_;
  if (value_ == 1) {
    pthread_cond_signal(&cond_);
  }
  pthread_mutex_unlock(&mutex_);
}

bool SemaImpl_Pthreads::Wait() {
  timeval tv;
  gettimeofday(&tv, nullptr);

  /* Wait for ten seconds.  If it takes longer than that, we're probably
   * deadlocked. */
  timespec ts;
  ts.tv_sec = tv.tv_sec + 10;
  ts.tv_nsec = tv.tv_usec * 1000;

  pthread_mutex_lock(&mutex_);

  int tries = 2;
  while (!value_ && tries) {
    int ret = pthread_cond_timedwait(&cond_, &mutex_, &ts);

    switch (ret) {
      case 0:
      case EINTR:
        break;

      case ETIMEDOUT:
        /* Timed out. Probably deadlocked. Try again one more time,
         * with a smaller timeout, just in case we're debugging and
         * happened to stop while waiting on the mutex. */
        ++ts.tv_sec;
        tries--;
        break;

      default:
        FAIL_M(ssprintf("pthread_cond_timedwait: %s", strerror(errno)));
    }
  }

  if (!value_) {
    /* Timed out. */
    pthread_mutex_unlock(&mutex_);
    return false;
  } else {
    --value_;
    pthread_mutex_unlock(&mutex_);
    return true;
  }
}

SemaImpl* MakeSemaphore(int initial_value) {
  return new SemaImpl_Pthreads(initial_value);
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
