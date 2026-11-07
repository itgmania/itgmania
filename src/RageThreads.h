#ifndef RAGE_THREADS_H
#define RAGE_THREADS_H

#include <atomic>
#include <cstdint>
#include <string>

struct ThreadSlot;
class RageTimer;
/** @brief Thread, mutex, semaphore, and event classes. */
class RageThread {
 public:
  RageThread();
  RageThread(const RageThread& cpy);
  ~RageThread();

  void SetName(const std::string& n) { name_ = n; }
  std::string GetName() const { return name_; }
  void Create(int (*fn)(void*), void* data);

  void Halt(bool kill = false);
  void Resume();

  /* For crash handlers: kill or suspend all threads (except for
   * the running one) immediately. */
  static void HaltAllThreads(bool kill = false);

  /* If HaltAllThreads was called (with Kill==false), resume. */
  static void ResumeAllThreads();

  static uint64_t GetCurrentThreadID();

  static const char* GetCurrentThreadName();
  static const char* GetThreadNameByID(uint64_t id);
  static bool EnumThreadIDs(int n, uint64_t& id);
  int Wait();
  bool IsCreated() const { return slot_ != nullptr; }

  static bool GetIsShowingDialog() { return is_showing_dialog_.load(); }
  static void SetIsShowingDialog(bool b) { is_showing_dialog_.store(b); }
  static uint64_t GetInvalidThreadID();

 private:
  ThreadSlot* slot_;
  std::string name_;

  static std::atomic<bool> is_showing_dialog_;

  // Swallow up warnings. If they must be used, define them.
  RageThread& operator=(const RageThread& rhs);
};

/**
 * @brief Register a thread created outside of RageThread.
 *
 * This gives it a name for RageThread::GetCurrentThreadName,
 * and allocates a slot for checkpoints. */
class RageThreadRegister {
 public:
  RageThreadRegister(const std::string& name);
  ~RageThreadRegister();

 private:
  ThreadSlot* slot_;
  // Swallow up warnings. If they must be used, define them.
  RageThreadRegister& operator=(const RageThreadRegister& rhs);
  RageThreadRegister(const RageThreadRegister& rhs);
};

namespace Checkpoints {
void LogCheckpoints(bool yes = true);
void SetCheckpoint(const char* file, int line, const char* message);
void SetCheckpoint(const char* file, int line, const std::string& message);
void GetLogs(char* buf, int size, const char* delim);
};  // namespace Checkpoints

#define CHECKPOINT_M(m) (Checkpoints::SetCheckpoint(__FILE__, __LINE__, m))
#define CHECKPOINT (Checkpoints::SetCheckpoint(__FILE__, __LINE__, nullptr))

/* Mutex class that follows the behavior of Windows mutexes: if the same
 * thread locks the same mutex twice, we just increase a refcount; a mutex
 * is considered unlocked when the refcount reaches zero.  This is more
 * convenient, though much slower on some archs.  (We don't have any tightly-
 * coupled threads, so that's OK.) */
class MutexImpl;
class RageMutex {
 public:
  std::string GetName() const { return name_; }
  void SetName(const std::string& s) { name_ = s; }
  virtual void Lock();
  virtual bool TryLock();
  virtual void Unlock();
  virtual bool IsLockedByThisThread() const;

  RageMutex(const std::string& name);
  virtual ~RageMutex();

 protected:
  MutexImpl* mutex_;
  std::string name_;

  uint64_t locked_by_;
  int lock_cnt_;

 private:
  // Swallow up warnings. If they must be used, define them.
  RageMutex& operator=(const RageMutex& rhs);
  RageMutex(const RageMutex& rhs);
};

/**
 * @brief Lock a mutex on construction, unlock it on destruction.
 *
 * Helps for functions with more than one return path. */
class LockMutex {
  RageMutex& mutex_;

  const char* file_;
  int line_;
  uint64_t locked_at_;
  bool locked_;

 public:
  LockMutex(RageMutex& mut, const char* file, int line);
  LockMutex(RageMutex& mut)
      : mutex_(mut),
        file_(nullptr),
        line_(-1),
        locked_at_(UINT64_MAX),
        locked_(true) {
    mutex_.Lock();
  }
  ~LockMutex();
  LockMutex(LockMutex& cpy)
      : mutex_(cpy.mutex_),
        file_(nullptr),
        line_(-1),
        locked_at_(cpy.locked_at_),
        locked_(true) {
    mutex_.Lock();
  }

  /**
   * @brief Unlock the mutex (before this would normally go out of scope).
   *
   * This can only be called once. */
  void Unlock();

 private:
  // Swallow up warnings. If they must be used, define them.
  LockMutex& operator=(const LockMutex& rhs);
};

#define LockMut(m) LockMutex LocalLock(m, __FILE__, __LINE__)

class EventImpl;
class RageEvent : public RageMutex {
 public:
  RageEvent(std::string name);
  ~RageEvent();

  /*
   * If pTimeout is non-nullptr, the event will be automatically signalled at
   * the given time.  Note that implementing this timeout is optional; not all
   * archs support it. If false is returned, the wait timed out (and the mutex
   * is locked, as if the event had been signalled).
   */
  bool Wait(RageTimer* timeout = nullptr);
  void Signal();
  void Broadcast();
  bool WaitTimeoutSupported() const;
  // Swallow up warnings. If they must be used, define them.
  RageEvent& operator=(const RageEvent& rhs);
  RageEvent(const RageEvent& rhs);

 private:
  EventImpl* event_;
};

class SemaImpl;
class RageSemaphore {
 public:
  RageSemaphore(std::string name, int initial_value = 0);
  ~RageSemaphore();

  std::string GetName() const { return name_; }
  void Post();
  void Wait(bool fail_on_timeout = true);

 private:
  SemaImpl* sema_;
  std::string name_;

  // Swallow up warnings. If they must be used, define them.
  RageSemaphore& operator=(const RageSemaphore& rhs);
  RageSemaphore(const RageSemaphore& rhs);
};

#endif

/**
 * @file
 * @author Glenn Maynard (c) 2001-2004
 * @section LICENSE
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
