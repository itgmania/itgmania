#include "RageThreads.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "RageException.h"
#include "RageLog.h"
#include "RageTimer.h"
#include "RageUtil.h"
#include "arch/Threads/Threads.h"
#include "global.h"

#if defined(_WIN32)
#include "archutils/Win32/crash.h"
#elif defined(LINUX) || defined(MACOSX)
#include "archutils/Unix/CrashHandler.h"
#endif

// Set in Dialog.cpp via SetIsShowingDialog, but read in RageThread.cpp.
std::atomic<bool> RageThread::is_showing_dialog_(false);

#define MAX_THREADS 128
// static std::vector<RageMutex*> *g_MutexList = nullptr; /* watch out for
// static initialization order problems */

struct ThreadSlot {
  mutable char name_[1024]; /* mutable so we can force nul-termination */

  /* Format this beforehand, since it's easier to do that than to do it under
   * crash conditions. */
  char thread_formatted_output_[1024];

  std::atomic<bool> used_;
  uint64_t id_;

  ThreadImpl* impl_;

#undef CHECKPOINT_COUNT
#define CHECKPOINT_COUNT 5
  struct ThreadCheckpoint {
    const char *file_, *message_;
    int line_;
    char formatted_buf_[1024];

    ThreadCheckpoint() { Set(nullptr, 0, nullptr); }
    void Set(const char* file, int line, const char* message = nullptr);
    const char* GetFormattedCheckpoint();
  };
  ThreadCheckpoint checkpoints_[CHECKPOINT_COUNT];
  int cur_checkpoint_, num_checkpoints_;
  const char* GetFormattedCheckpoint(int lineno);

  ThreadSlot()
      : used_{false},
        id_(GetInvalidThreadId()),
        impl_(nullptr),
        cur_checkpoint_(0),
        num_checkpoints_(0) {}
  void Init() {
    id_ = GetInvalidThreadId();
    cur_checkpoint_ = num_checkpoints_ = 0;
    impl_ = nullptr;

    /* Reset used last; otherwise, a thread creation might pick up the slot. */
    used_.store(false, std::memory_order_release);
  }

  void Release() {
    RageUtil::SafeDelete(impl_);
    Init();
  }

  const char* GetThreadName() const;
};

void ThreadSlot::ThreadCheckpoint::Set(
    const char* file, int line, const char* message) {
  file_ = file;
  line_ = line;
  message_ = message;

  /* Skip any path components. */
  if (file_ != nullptr) {
    const char* p = strrchr(file_, '/');
    if (p == nullptr) {
      p = strrchr(file_, '\\');
    }
    if (p != nullptr && p[1] != '\0') {
      file_ = p + 1;
    }
  }

  snprintf(
      formatted_buf_, sizeof(formatted_buf_), "        %s:%i %s", file_, line_,
      message_ ? message_ : "");
}

const char* ThreadSlot::ThreadCheckpoint::GetFormattedCheckpoint() {
  if (file_ == nullptr) {
    return nullptr;
  }

  /* Make sure it's terminated: */
  formatted_buf_[sizeof(formatted_buf_) - 1] = 0;

  return formatted_buf_;
}

const char* ThreadSlot::GetFormattedCheckpoint(int lineno) {
  if (lineno >= CHECKPOINT_COUNT || lineno >= num_checkpoints_) {
    return nullptr;
  }

  if (num_checkpoints_ == CHECKPOINT_COUNT) {
    lineno += cur_checkpoint_;
    lineno %= CHECKPOINT_COUNT;
  }

  return checkpoints_[lineno].GetFormattedCheckpoint();
}

static ThreadSlot g_ThreadSlots[MAX_THREADS];
struct ThreadSlot* g_pUnknownThreadSlot = nullptr;

/* Lock this mutex before using or modifying m_pImpl.  Other values are just
 * identifiers, so possibly racing over them is harmless (simply using a stale
 * thread ID, etc). */
static RageMutex& GetThreadSlotsLock() {
  static RageMutex* lock = new RageMutex("ThreadSlots");
  return *lock;
}

/* All unkown threads are put into the same slot in g_ThreadSlots. This
 * mutex synchronizes access to that slot. */
static RageMutex& GetUnknownThreadSlotLock() {
  static RageMutex* lock = new RageMutex("UnknownThreadSlot");
  return *lock;
}

static int FindEmptyThreadSlot() {
  LockMut(GetThreadSlotsLock());
  for (int entry = 0; entry < MAX_THREADS; ++entry) {
    if (g_ThreadSlots[entry].used_.load(std::memory_order_acquire)) {
      continue;
    }

    g_ThreadSlots[entry].used_.store(true, std::memory_order_release);
    return entry;
  }

  RageException::Throw("Out of thread slots!");
}

static void InitThreads() {
  /* We don't have to worry about two threads calling this at once, since it's
   * called when we create a thread. */
  static bool initialized = false;
  if (initialized) {
    return;
  }

  LockMut(GetThreadSlotsLock());

  /* Libraries might start threads on their own, which might call user
   * callbacks, which could come back here.  Make sure we don't accidentally
   * initialize twice. */
  if (initialized) {
    return;
  }

  initialized = true;

  /* Register the "unknown thread" slot. */
  int slot = FindEmptyThreadSlot();
  strcpy(g_ThreadSlots[slot].name_, "Unknown thread");
  g_ThreadSlots[slot].id_ = GetInvalidThreadId();
  sprintf(g_ThreadSlots[slot].thread_formatted_output_, "Unknown thread");
  g_pUnknownThreadSlot = &g_ThreadSlots[slot];
}

static ThreadSlot* GetThreadSlotFromID(uint64_t id) {
  InitThreads();

  for (int entry = 0; entry < MAX_THREADS; ++entry) {
    if (!g_ThreadSlots[entry].used_.load(std::memory_order_acquire)) {
      continue;
    }
    if (g_ThreadSlots[entry].id_ == id) {
      return &g_ThreadSlots[entry];
    }
  }
  return nullptr;
}

static ThreadSlot* GetCurThreadSlot() {
  return GetThreadSlotFromID(RageThread::GetCurrentThreadID());
}

static ThreadSlot* GetUnknownThreadSlot() { return g_pUnknownThreadSlot; }

RageThread::RageThread() : slot_(nullptr), name_("unnamed") {}

/* Copying a thread does not start the copy. */
RageThread::RageThread(const RageThread& cpy)
    : slot_(nullptr), name_(cpy.name_) {}

RageThread::~RageThread() {
  if (slot_ != nullptr) {
    Wait();
  }
}

const char* ThreadSlot::GetThreadName() const {
  /* This function may be called in crash conditions, so guarantee the string
   * is null-terminated. */
  name_[sizeof(name_) - 1] = 0;

  return name_;
}

void RageThread::Create(int (*fn)(void*), void* data) {
  /* Don't create a thread that's already running: */
  ASSERT(slot_ == nullptr);

  InitThreads();

  /* Lock unused slots, so nothing else uses our slot before we mark it used. */
  LockMut(GetThreadSlotsLock());

  int slotno = FindEmptyThreadSlot();
  slot_ = &g_ThreadSlots[slotno];

  strcpy(slot_->name_, name_.c_str());

  if (LOG) {
    LOG->Trace("Starting thread: %s", name_.c_str());
  }
  sprintf(slot_->thread_formatted_output_, "Thread: %s", name_.c_str());

  /* Start a thread using our own startup function.  We pass the id to fill in,
   * to make sure it's set before the thread actually starts.  (Otherwise, early
   * checkpoints might not have a completely set-up thread slot.) */
  slot_->impl_ = MakeThread(fn, data, &slot_->id_);
}

RageThreadRegister::RageThreadRegister(const std::string& name) {
  InitThreads();
  LockMut(GetThreadSlotsLock());

  int slot = FindEmptyThreadSlot();

  slot_ = &g_ThreadSlots[slot];

  strcpy(slot_->name_, name.c_str());
  sprintf(slot_->thread_formatted_output_, "Thread: %s", name.c_str());

  slot_->id_ = GetThisThreadId();
  slot_->impl_ = MakeThisThread();
}

RageThreadRegister::~RageThreadRegister() {
  LockMut(GetThreadSlotsLock());

  slot_->Release();
  slot_ = nullptr;
}

const char* RageThread::GetCurrentThreadName() {
  return GetThreadNameByID(GetCurrentThreadID());
}

const char* RageThread::GetThreadNameByID(uint64_t id) {
  ThreadSlot* slot = GetThreadSlotFromID(id);
  if (slot == nullptr) {
    return "???";
  }

  return slot->GetThreadName();
}

bool RageThread::EnumThreadIDs(int n, uint64_t& id) {
  if (n >= MAX_THREADS) {
    return false;
  }

  LockMut(GetThreadSlotsLock());
  const ThreadSlot* slot = &g_ThreadSlots[n];

  if (slot->used_.load(std::memory_order_acquire)) {
    id = slot->id_;
  } else {
    id = GetInvalidThreadId();
  }

  return true;
}

int RageThread::Wait() {
  ASSERT(slot_ != nullptr);
  ASSERT(slot_->impl_ != nullptr);
  int ret = slot_->impl_->Wait();

  LockMut(GetThreadSlotsLock());

  slot_->Release();
  slot_ = nullptr;

  return ret;
}

void RageThread::Halt(bool kill) {
  ASSERT(slot_ != nullptr);
  ASSERT(slot_->impl_ != nullptr);
  slot_->impl_->Halt(kill);
}

void RageThread::Resume() {
  ASSERT(slot_ != nullptr);
  ASSERT(slot_->impl_ != nullptr);
  slot_->impl_->Resume();
}

void RageThread::HaltAllThreads(bool kill) {
  const uint64_t this_thread_id = GetThisThreadId();
  for (int entry = 0; entry < MAX_THREADS; ++entry) {
    if (!g_ThreadSlots[entry].used_.load(std::memory_order_acquire)) {
      continue;
    }
    if (this_thread_id == g_ThreadSlots[entry].id_ ||
        g_ThreadSlots[entry].impl_ == nullptr) {
      continue;
    }
    g_ThreadSlots[entry].impl_->Halt(kill);
  }
}

void RageThread::ResumeAllThreads() {
  const uint64_t this_thread_id = GetThisThreadId();
  for (int entry = 0; entry < MAX_THREADS; ++entry) {
    if (!g_ThreadSlots[entry].used_.load(std::memory_order_acquire)) {
      continue;
    }
    if (this_thread_id == g_ThreadSlots[entry].id_ ||
        g_ThreadSlots[entry].impl_ == nullptr) {
      continue;
    }

    g_ThreadSlots[entry].impl_->Resume();
  }
}

uint64_t RageThread::GetCurrentThreadID() { return GetThisThreadId(); }
uint64_t RageThread::GetInvalidThreadID() { return GetInvalidThreadId(); }

/* Normally, checkpoints are only seen in crash logs.  It's occasionally useful
 * to see them in logs, but this outputs a huge amount of text. */
static bool g_LogCheckpoints = false;
void Checkpoints::LogCheckpoints(bool on) { g_LogCheckpoints = on; }

void Checkpoints::SetCheckpoint(
    const char* file, int line, const std::string& message) {
  SetCheckpoint(file, line, message.c_str());
}

void Checkpoints::SetCheckpoint(
    const char* file, int line, const char* message) {
  bool unknown_thread = false;
  ThreadSlot* slot = GetCurThreadSlot();
  if (slot == nullptr) {
    unknown_thread = true;
    slot = GetUnknownThreadSlot();
    /* We can't ASSERT here, since that uses checkpoints. */
    if (slot == nullptr) {
      sm_crash("GetUnknownThreadSlot() returned nullptr");
    }
  }

  /* Ignore everything up to and including the first "src/". */
  const char* temp = strstr(file, "src/");
  if (temp) {
    file = temp + 4;
  }

  if (unknown_thread) {
    GetUnknownThreadSlotLock().Lock();
  }

  slot->checkpoints_[slot->cur_checkpoint_].Set(file, line, message);

  if (g_LogCheckpoints) {
    LOG->Trace("%s", slot->checkpoints_[slot->cur_checkpoint_].formatted_buf_);
  }

  ++slot->cur_checkpoint_;
  slot->num_checkpoints_ =
      std::max(slot->num_checkpoints_, slot->cur_checkpoint_);
  slot->cur_checkpoint_ %= CHECKPOINT_COUNT;

  if (unknown_thread) {
    GetUnknownThreadSlotLock().Unlock();
  }
}

/* This is called under crash conditions.  Be careful. */
static const char* GetCheckpointLog(int slotno, int lineno) {
  ThreadSlot& slot = g_ThreadSlots[slotno];
  if (!slot.used_.load(std::memory_order_acquire)) {
    return nullptr;
  }

  /* Only show the "Unknown thread" entry if it has at least one checkpoint. */
  if (&slot == g_pUnknownThreadSlot &&
      slot.GetFormattedCheckpoint(0) == nullptr) {
    return nullptr;
  }

  if (lineno != 0) {
    return slot.GetFormattedCheckpoint(lineno - 1);
  }

  slot.thread_formatted_output_[sizeof(slot.thread_formatted_output_) - 1] = 0;
  return slot.thread_formatted_output_;
}

/* XXX: iSize check unimplemented */
void Checkpoints::GetLogs(char* p_buf, int size, const char* delim) {
  p_buf[0] = 0;

  for (int slotno = 0; slotno < MAX_THREADS; ++slotno) {
    const char* buf = GetCheckpointLog(slotno, 0);
    if (buf == nullptr) {
      continue;
    }
    strcat(p_buf, buf);
    strcat(p_buf, delim);

    for (int line = 1; (buf = GetCheckpointLog(slotno, line)) != nullptr;
         ++line) {
      strcat(p_buf, buf);
      strcat(p_buf, delim);
    }
  }
}

/*
 * "Safe" mutexes: locking the same mutex more than once from the same thread
 * is refcounted and does not deadlock.
 *
 * Only actually lock the mutex once; when we do so, remember which thread
 * locked it. Then, when we lock in the future, only increment a counter, with
 * no locks.
 *
 * We must be holding the real mutex to write to LockedBy and LockCnt.  However,
 * we can look at LockedBy to see if it's us that owns it (in which case, we
 * already hold the mutex).
 *
 * In Windows, this helps smooth out performance: for some reason, Windows likes
 * to yank the scheduler away from a thread that locks a mutex that it already
 * owns.
 */

RageMutex::RageMutex(const std::string& name)
    : mutex_(MakeMutex()),
      name_(name),
      locked_by_(GetInvalidThreadId()),
      lock_cnt_(0) {
  /*	if( g_FreeMutexIDs == nullptr )
          {
                  g_FreeMutexIDs = new std::set<int>;
                  for( int i = 0; i < MAX_MUTEXES; ++i )
                          g_FreeMutexIDs->insert( i );
          }

          if( g_FreeMutexIDs->empty() )
          {
                  ASSERT_M( g_MutexList, "!g_FreeMutexIDs but !g_MutexList?" );
     // doesn't make sense to be out of mutexes yet never created any
                  std::string s;
                  for( unsigned i = 0; i < g_MutexList->size(); ++i )
                  {
                          if( i )
                                  s += ", ";
                          s += ssprintf( "\"%s\"",
     (*g_MutexList)[i]->GetName().c_str() );
                  }
                  LOG->Trace( "%s", s.c_str() );
                  FAIL_M( ssprintf("MAX_MUTEXES exceeded creating \"%s\"",
     name.c_str() ) );
          }

          m_UniqueID = *g_FreeMutexIDs->begin();

          g_FreeMutexIDs->erase( g_FreeMutexIDs->begin() );

          if( g_MutexList == nullptr )
                  g_MutexList = new std::vector<RageMutex*>;

          g_MutexList->push_back( this );
  */
}

RageMutex::~RageMutex() {
  delete mutex_;
  /*
          std::vector<RageMutex*>::iterator it = find( g_MutexList->begin(),
     g_MutexList->end(), this ); ASSERT( it != g_MutexList->end() );
          g_MutexList->erase( it );
          if( g_MutexList->empty() )
          {
                  delete g_MutexList;
                  g_MutexList = nullptr;
          }

          delete m_pMutex;

          g_FreeMutexIDs->insert( m_UniqueID );
  */
}

void RageMutex::Lock() {
  uint64_t this_thread_id = GetThisThreadId();
  if (locked_by_ == this_thread_id) {
    ++lock_cnt_;
    return;
  }

  if (!mutex_->Lock()) {
    const ThreadSlot* this_slot = GetThreadSlotFromID(GetThisThreadId());
    const ThreadSlot* other_slot = GetThreadSlotFromID(locked_by_);

    std::string this_slot_name =
        "(???"
        ")";  // stupid trigraph warnings
    std::string other_slot_name =
        "(???"
        ")";  // stupid trigraph warnings
    if (this_slot) {
      this_slot_name =
          ssprintf("%s (%i)", this_slot->GetThreadName(), (int)this_slot->id_);
    }
    if (other_slot) {
      other_slot_name = ssprintf(
          "%s (%i)", other_slot->GetThreadName(), (int)other_slot->id_);
    }
    const std::string reason = ssprintf(
        "Thread deadlock on mutex %s between %s and %s", GetName().c_str(),
        this_slot_name.c_str(), other_slot_name.c_str());

    /* Don't leave GetThreadSlotsLock() locked when we call
     * ForceCrashHandlerDeadlock. */
    GetThreadSlotsLock().Lock();
    uint64_t crash_handle = other_slot ? other_slot->id_ : 0;
    GetThreadSlotsLock().Unlock();

    /* Pass the crash handle of the other thread, so it can backtrace that
     * thread. */
    CrashHandler::ForceDeadlock(reason, crash_handle);
  }

  locked_by_ = this_thread_id;
}

bool RageMutex::TryLock() {
  if (locked_by_ == GetThisThreadId()) {
    ++lock_cnt_;
    return true;
  }

  if (!mutex_->TryLock()) {
    return false;
  }

  locked_by_ = GetThisThreadId();

  return true;
}

void RageMutex::Unlock() {
  if (lock_cnt_) {
    --lock_cnt_;
    return;
  }

  locked_by_ = GetInvalidThreadId();

  mutex_->Unlock();
}

bool RageMutex::IsLockedByThisThread() const {
  return locked_by_ == GetThisThreadId();
}

LockMutex::LockMutex(RageMutex& mutex, const char* file, int line)
    : mutex_(mutex),
      file_(file),
      line_(line),
      locked_at_(RageTimer::GetTimeSinceStartMicroseconds()),
      locked_(false)  // ensure it gets locked inside.
{
  mutex_.Lock();
  locked_ = true;
}

LockMutex::~LockMutex() {
  if (locked_) {
    mutex_.Unlock();
  }
}

void LockMutex::Unlock() {
  ASSERT(locked_);
  locked_ = false;

  mutex_.Unlock();

  constexpr uint64_t kThresholdUsec = 15000;

  if (file_ && locked_at_ != UINT64_MAX) {
    const uint64_t current_usecs = RageTimer::GetTimeSinceStartMicroseconds();
    const uint64_t dur_usecs = current_usecs - locked_at_;

    if (dur_usecs > kThresholdUsec) {
      LOG->Trace(
          "Lock at %s:%i took %" PRIu64 " microseconds", file_, line_,
          dur_usecs);
    }
  }
}

RageEvent::RageEvent(std::string name)
    : RageMutex(name), event_(MakeEvent(mutex_)) {}

RageEvent::~RageEvent() { delete event_; }

/* For each of these calls, the mutex must be locked, and must not be locked
 * recursively. */
bool RageEvent::Wait(RageTimer* timeout) {
  ASSERT(IsLockedByThisThread());
  ASSERT(lock_cnt_ == 0);

  /* A zero RageTimer also means no timeout. */
  if (timeout != nullptr && timeout->IsZero()) {
    timeout = nullptr;
  }
  bool ret = event_->Wait(timeout);

  locked_by_ = GetThisThreadId();
  return ret;
}

void RageEvent::Signal() {
  ASSERT(IsLockedByThisThread());
  ASSERT(lock_cnt_ == 0);
  event_->Signal();
}

void RageEvent::Broadcast() {
  ASSERT(IsLockedByThisThread());
  ASSERT(lock_cnt_ == 0);
  event_->Broadcast();
}

bool RageEvent::WaitTimeoutSupported() const {
  return event_->WaitTimeoutSupported();
}

RageSemaphore::RageSemaphore(std::string name, int initial_value)
    : sema_(MakeSemaphore(initial_value)), name_(name) {}

RageSemaphore::~RageSemaphore() { delete sema_; }

void RageSemaphore::Post() { sema_->Post(); }

void RageSemaphore::Wait(bool fail_on_timeout) {
  do {
    if (sema_->Wait()) {
      return;
    }
  } while (!fail_on_timeout || RageThread::GetIsShowingDialog());

  /* We waited too long.  We're probably deadlocked, though unlike mutexes, we
   * can't tell which thread we're stuck on. */
  const ThreadSlot* this_slot = GetThreadSlotFromID(GetThisThreadId());
  const std::string reason = ssprintf(
      "Semaphore timeout on mutex %s on thread %s", GetName().c_str(),
      this_slot ? this_slot->GetThreadName()
                : "(???"
                  ")");  // stupid trigraph warnings
  CrashHandler::ForceDeadlock(reason, GetInvalidThreadId());
}

/*
 * Copyright (c) 2001-2004 Glenn Maynard
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
