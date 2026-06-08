/* RageThreads helpers for threads in Linux, which are based on PIDs and TIDs.
 */
#include "PthreadHelpers.h"

#include <pthread.h>
#include <semaphore.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>

#include "RageUtil.h"
#include "archutils/Unix/Backtrace.h"  // HACK: This should be platform-agnosticized
#include "global.h"

#if defined(LINUX)
#if defined(HAVE_UNISTD_H)
#include <unistd.h>
#endif
#if defined(HAVE_FCNTL_H)
#include <fcntl.h>
#endif
#include <sys/ptrace.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#define _LINUX_PTRACE_H  // hack to prevent broken linux/ptrace.h from
                         // conflicting with sys/ptrace.h
#include <sys/user.h>

/* waitpid(); ThreadID can be a PID or (in NPTL) a TID; doesn't care if the ID
 * is a clone() or not. */
static int waittid(int ThreadID, int* status, int options) {
  static bool bSupportsWall = true;

  if (bSupportsWall) {
    int ret = waitpid(ThreadID, status, options | __WALL);
    if (ret != -1 || errno != EINVAL) {
      return ret;
    }
    bSupportsWall = false;
  }

  /* XXX: on 2.2, we need to use __WCLONE only if ThreadID isn't the main
   * thread; perhaps wait and retry without it if errno == ECHILD? */
  int ret;
  ret = waitpid(ThreadID, status, options);

  return ret;
}

/* Attempt to PTRACE_ATTACH to a thread, and wait for the SIGSTOP. */
static int PtraceAttach(int ThreadID) {
  int ret;
  ret = ptrace(PTRACE_ATTACH, ThreadID, nullptr, nullptr);
  if (ret == -1) {
    printf("ptrace failed: %s\n", strerror(errno));
    return -1;
  }

  /* Wait for the SIGSTOP from the ptrace call. */
  int status;
  ret = waittid(ThreadID, &status, 0);
  if (ret == -1) {
    return -1;
  }

  //	printf( "ret %i, exited %i, signalled %i, sig %i, stopped %i, stopsig
  //%i\n", ret, WIFEXITED(status), 			WIFSIGNALED(status),
  // WTERMSIG(status), WIFSTOPPED(status), WSTOPSIG(status));
  return 0;
}

static int PtraceDetach(int ThreadID) {
  return ptrace(PTRACE_DETACH, ThreadID, nullptr, nullptr);
}

uint64_t GetCurrentThreadId() {
  static thread_local uint64_t cached_tid = 0;
  if (!cached_tid) {
    cached_tid = gettid();
  }
  return cached_tid;
}

int SuspendThread(uint64_t ThreadID) {
  /*
   * Linux: We can't simply kill(SIGSTOP) (or tkill), since that will stop all
   * processes (grr).  We can ptrace(PTRACE_ATTACH) the process to stop it, and
   * PTRACE_DETACH to restart it.
   */
  return PtraceAttach(int(ThreadID));
  // kill( ThreadID, SIGSTOP );
}

int ResumeThread(uint64_t ThreadID) {
  return PtraceDetach(int(ThreadID));
  // kill( ThreadID, SIGSTOP );
}

/* Get a BacktraceContext for a thread.  ThreadID must not be the current
 * thread. This call leaves the given thread suspended; use ResumeThread() to
 * resume it after. */
bool GetThreadBacktraceContext(uint64_t ThreadID, BacktraceContext* ctx) {
  /* Can't GetThreadBacktraceContext the current thread. */
  ASSERT(ThreadID != GetCurrentThreadId());

  /* Attach to the thread.  This may fail with EPERM.  This can happen for at
   * least two common reasons: the process might be in a debugger already, or
   * *we* might already have attached to it via SuspendThread.
   *
   * If it's in a debugger, we won't be able to ptrace(PTRACE_GETREGS). If
   * it's us that attached, we will. */
  if (PtraceAttach(int(ThreadID)) == -1 && errno != EPERM) {
    CHECKPOINT_M(ssprintf(
        "%s (pid %i tid %i locking tid %i)", strerror(errno), getpid(),
        (int)GetCurrentThreadId(), int(ThreadID)));
    return false;
  }

#if defined(CPU_X86_64) || defined(CPU_X86)
  user_regs_struct regs;
  if (ptrace(PTRACE_GETREGS, pid_t(ThreadID), nullptr, &regs) == -1) {
    return false;
  }

  ctx->pid = pid_t(ThreadID);
#if defined(CPU_X86_64)
  ctx->ip = (void*)regs.rip;
  ctx->bp = (void*)regs.rbp;
  ctx->sp = (void*)regs.rsp;
#else
  ctx->ip = (void*)regs.eip;
  ctx->bp = (void*)regs.ebp;
  ctx->sp = (void*)regs.esp;
#endif
#elif defined(CPU_PPC)
  errno = 0;
  ctx->FramePtr = (const Frame*)ptrace(
      PTRACE_PEEKUSER, pid_t(ThreadID), (void*)(PT_R1 << 2), 0);
  if (errno) {
    return false;
  }
  ctx->PC =
      (void*)ptrace(PTRACE_PEEKUSER, pid_t(ThreadID), (void*)(PT_NIP << 2), 0);
  if (errno) {
    return false;
  }
#elif defined(CPU_AARCH64)
  // NYI
  return false;
#else
#error GetThreadBacktraceContext: which arch?
#endif

  return true;
}

#elif defined(UNIX)
#include <pthread.h>
#include <signal.h>

uint64_t GetCurrentThreadId() { return uint64_t(pthread_self()); }

int SuspendThread(uint64_t id) { return pthread_kill(pthread_t(id), SIGSTOP); }

int ResumeThread(uint64_t id) { return pthread_kill(pthread_t(id), SIGCONT); }
#endif

/*
 * (c) 2004 Glenn Maynard
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
