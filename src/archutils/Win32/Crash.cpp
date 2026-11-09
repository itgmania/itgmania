#include "global.h"

// DO NOT USE stdio.h!  printf() calls malloc()!
// #include <stdio.h>

#include <strsafe.h>
#include <windows.h>

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "CrashHandlerInternal.h"
#include "PrefsManager.h"  // for g_bAutoRestart
#include "RageLog.h"       // for RageLog::GetAdditionalLog and Flush
#include "RageThreads.h"   // for GetCheckpointLogs
#include "RestartProgram.h"
#include "arch/Threads/Threads_Win32.h"
#include "crash.h"
#include "global.h"

// WARNING: This is called from crash-time conditions!  No malloc() or new!!!

#define malloc not_allowed_here
#define new not_allowed_here

static void BufferedFormatString(
    char* buf, size_t bufsiz, const char* fmt, ...) {
  if (!buf || bufsiz == 0) {
    return;
  }

  va_list args;
  va_start(args, fmt);
  HRESULT hr = StringCchVPrintfA(buf, bufsiz, fmt, args);
  va_end(args);

  if (FAILED(hr)) {
    buf[0] = '\0';
  }
}

static void SpliceProgramPath(char* buf, int bufsiz, const char* fn) {
  char tbuf[MAX_PATH];
  char* file;

  GetModuleFileNameA(nullptr, tbuf, sizeof tbuf);
  GetFullPathNameA(tbuf, bufsiz, buf, &file);
  strcpy(file, fn);
}

///////////////////////////////////////////////////////////////////////////

static const struct ExceptionLookup {
  DWORD code_;
  const char* name_;
} exceptions[] = {
    {EXCEPTION_ACCESS_VIOLATION, "Access Violation"},
    {EXCEPTION_BREAKPOINT, "Breakpoint"},
    {EXCEPTION_FLT_DENORMAL_OPERAND, "FP Denormal Operand"},
    {EXCEPTION_FLT_DIVIDE_BY_ZERO, "FP Divide-by-Zero"},
    {EXCEPTION_FLT_INEXACT_RESULT, "FP Inexact Result"},
    {EXCEPTION_FLT_INVALID_OPERATION, "FP Invalid Operation"},
    {
        EXCEPTION_FLT_OVERFLOW,
        "FP Overflow",
    },
    {
        EXCEPTION_FLT_STACK_CHECK,
        "FP Stack Check",
    },
    {
        EXCEPTION_FLT_UNDERFLOW,
        "FP Underflow",
    },
    {
        EXCEPTION_INT_DIVIDE_BY_ZERO,
        "Integer Divide-by-Zero",
    },
    {
        EXCEPTION_INT_OVERFLOW,
        "Integer Overflow",
    },
    {
        EXCEPTION_PRIV_INSTRUCTION,
        "Privileged Instruction",
    },
    {EXCEPTION_ILLEGAL_INSTRUCTION, "Illegal instruction"},
    {EXCEPTION_INVALID_HANDLE, "Invalid handle"},
    {EXCEPTION_STACK_OVERFLOW, "Stack overflow"},
    {
        0xe06d7363,
        "Unhandled Microsoft C++ Exception",
    },
    {0},
};

static const char* LookupException(DWORD code) {
  for (int i = 0; exceptions[i].code_; ++i) {
    if (exceptions[i].code_ == code) {
      return exceptions[i].name_;
    }
  }

  return nullptr;
}

static CrashInfo crash_info;
static void GetReason(const EXCEPTION_RECORD* record, CrashInfo* crash) {
  // fill out bomb reason
  const char* reason = LookupException(record->ExceptionCode);

  if (reason == nullptr) {
    BufferedFormatString(
        crash->crash_reason_, sizeof(crash->crash_reason_),
        "unknown exception 0x%08lx", record->ExceptionCode);
  } else {
    strcpy(crash->crash_reason_, reason);
  }
}

static HWND foreground_wnd = nullptr;
void CrashHandler::SetForegroundWindow(HWND wnd) { foreground_wnd = wnd; }

void WriteToChild(HANDLE pipe, const void* data, size_t size) {
  while (size) {
    DWORD actual;
    if (!WriteFile(pipe, data, static_cast<DWORD>(size), &actual, nullptr)) {
      return;
    }
    size -= actual;
  }
}

/* Execute the child process. Return a handle to the process, a writable handle
 * to its stdin, and a readable handle to its stdout. */
bool StartChild(HANDLE& process, HANDLE& to_stdin, HANDLE& from_stdout) {
  char cwd[MAX_PATH];
  SpliceProgramPath(cwd, MAX_PATH, "");

  STARTUPINFOA si;
  ZeroMemory(&si, sizeof(si));
  si.dwFlags |= STARTF_USESTDHANDLES;

  {
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = true;
    sa.lpSecurityDescriptor = nullptr;

    CreatePipe(&si.hStdInput, &to_stdin, &sa, 0);
    CreatePipe(&from_stdout, &si.hStdOutput, &sa, 0);
    SetHandleInformation(to_stdin, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(from_stdout, HANDLE_FLAG_INHERIT, 0);
  }

  char buf[MAX_PATH] = "";
  GetModuleFileNameA(nullptr, buf, MAX_PATH);
  strcat(buf, " ");
  strcat(buf, CHILD_MAGIC_PARAMETER);

  PROCESS_INFORMATION pi;
  int ret = CreateProcessA(
      nullptr,  // pointer to name of executable module
      buf,      // pointer to command line string
      nullptr,  // process security attributes
      nullptr,  // thread security attributes
      true,     // handle inheritance flag
      0,        // creation flags
      nullptr,  // pointer to new environment block
      cwd,      // pointer to current directory name
      &si,      // pointer to STARTUPINFOA
      &pi       // pointer to PROCESS_INFORMATION
  );

  CloseHandle(si.hStdInput);
  CloseHandle(si.hStdOutput);

  if (!ret) {
    CloseHandle(to_stdin);
    CloseHandle(from_stdout);
    return false;
  }

  process = pi.hProcess;
  CloseHandle(pi.hThread);

  return true;
}

static const char* CrashGetModuleBaseName(HMODULE hmod, char* base_name) {
  char path1[MAX_PATH];
  char path2[MAX_PATH];

  // XXX: It looks like nothing in here COULD throw an exception. Need to verify
  // that.
  //	__try {
  if (!GetModuleFileNameA(hmod, path1, sizeof(path1))) {
    return nullptr;
  }

  char* file;
  DWORD dw = GetFullPathNameA(path1, sizeof(path2), path2, &file);

  if (!dw || dw > sizeof(path2)) {
    return nullptr;
  }

  strcpy(base_name, file);

  file = base_name;

  char* period = nullptr;
  while (*file++) {
    if (file[-1] == '.') {
      period = file - 1;
    }
  }

  if (period) {
    *period = 0;
  }
  //	} __except(1) {
  //		return nullptr;
  //	}

  return base_name;
}

void RunChild() {
  HANDLE process = nullptr, to_stdin = nullptr, from_stdout = nullptr;
  if (!StartChild(process, to_stdin, from_stdout)) {
    ASSERT_M(0, "Failed to start child process");
  }

  // 0. Send a handle of this process to the crash handling process, which it
  // can use to handle symbol lookups.
  {
    HANDLE target_handle;
    DuplicateHandle(
        GetCurrentProcess(), GetCurrentProcess(), process, &target_handle, 0,
        FALSE, DUPLICATE_SAME_ACCESS);

    WriteToChild(to_stdin, &target_handle, sizeof(target_handle));
  }

  // 1. Write the CrashData.
  WriteToChild(to_stdin, &crash_info, sizeof(crash_info));

  // 2. Write info.
  const TCHAR* p = RageLog::GetInfo();
  int size = static_cast<int>(strlen(p));
  WriteToChild(to_stdin, &size, sizeof(size));
  WriteToChild(to_stdin, p, size);

  // 3. Write AdditionalLog.
  p = RageLog::GetAdditionalLog();
  size = static_cast<int>(strlen(p));
  WriteToChild(to_stdin, &size, sizeof(size));
  WriteToChild(to_stdin, p, size);

  // 4. Write RecentLogs.
  int cnt = 0;
  const TCHAR* ps[1024];
  while (cnt < 1024 && (ps[cnt] = RageLog::GetRecentLog(cnt)) != nullptr) {
    ++cnt;
  }

  WriteToChild(to_stdin, &cnt, sizeof(cnt));
  for (int i = 0; i < cnt; ++i) {
    size = static_cast<int>(strlen(ps[i])) + 1;
    WriteToChild(to_stdin, &size, sizeof(size));
    WriteToChild(to_stdin, ps[i], size);
  }

  // 5. Write CHECKPOINTs.
  static TCHAR buf[1024 * 32];
  Checkpoints::GetLogs(buf, sizeof(buf), "$$");
  size = static_cast<int>(strlen(buf)) + 1;
  WriteToChild(to_stdin, &size, sizeof(size));
  WriteToChild(to_stdin, buf, size);

  // 6. Write the crashed thread's name.
  p = RageThread::GetCurrentThreadName();
  size = static_cast<int>(strlen(p)) + 1;
  WriteToChild(to_stdin, &size, sizeof(size));
  WriteToChild(to_stdin, p, size);

  /* The parent process needs to access this process briefly. When it's done,
   * it'll close the handle. Wait until we see that before exiting. */
  while (1) {
    /* Ugly: the new process can't execute GetModuleFileName on this process,
     * since GetModuleFileNameEx might not be available. Run the requests here.
     */
    HMODULE mod;
    DWORD actual;
    if (!ReadFile(from_stdout, &mod, sizeof(mod), &actual, nullptr)) {
      break;
    }

    TCHAR name[MAX_PATH];
    if (!CrashGetModuleBaseName(mod, name)) {
      strcpy(name, "???");
    }
    size = static_cast<int>(strlen(name));
    WriteToChild(to_stdin, &size, sizeof(size));
    WriteToChild(to_stdin, name, size);
  }
}

static DWORD WINAPI MainExceptionHandler(LPVOID parameter) {
  // Flush the log so it isn't cut off at the end.
  /* 1. We can't do regular file access in the crash handler.
   * 2. We can't access LOG itself at all, since it may not be set up or the
   * pointer might be munged. We must only ever use the RageLog:: methods that
   * access static data, that we're being very careful to null-terminate as
   * needed. Logs are rarely important, anyway. Only info.txt and crashinfo.txt
   * are needed 99% of the time. */
  //	LOG->Flush();

  /* We aren't supposed to receive these exceptions. For example, if you do
   * a floating point divide by zero, you should receive a result of #INF.
   * Only if the floating point exception for _EM_ZERODIVIDE is unmasked
   * does this exception occur, and we never unmask it.
   * However, once in a while some driver or library turns evil and unmasks an
   * exception flag on us. If this happens, re-mask it and continue execution.
   */
  PEXCEPTION_POINTERS exc = reinterpret_cast<PEXCEPTION_POINTERS>(parameter);
  switch (exc->ExceptionRecord->ExceptionCode) {
    case EXCEPTION_FLT_INVALID_OPERATION:
    case EXCEPTION_FLT_DENORMAL_OPERAND:
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
    case EXCEPTION_FLT_OVERFLOW:
    case EXCEPTION_FLT_UNDERFLOW:
    case EXCEPTION_FLT_INEXACT_RESULT:
      exc->ContextRecord->FltSave.ControlWord |= 0x3F;
      return static_cast<DWORD>(EXCEPTION_CONTINUE_EXECUTION);
  }

  static int in_here = 0;
  if (in_here > 0) {
    /* If we get here, then we've been called recursively, which means we
     * crashed. If in_here is greater than 1, then we crashed after writing
     * the crash dump; say so. */
    SetUnhandledExceptionFilter(nullptr);
    MessageBoxA(
        nullptr,
        in_here == 1 ? "The error reporting interface has crashed.\n"
                     : "The error reporting interface has crashed. However, "
                       "crashinfo.txt was written successfully to the program "
                       "directory.\n",
        "Fatal Error", MB_OK);
#ifdef DEBUG
    DebugBreak();
#endif

    return EXCEPTION_EXECUTE_HANDLER;
  }
  ++in_here;
  /////////////////////////

  RageThread::HaltAllThreads(false);

  if (!crash_info.crash_reason_[0]) {
    GetReason(exc->ExceptionRecord, &crash_info);
  }
  CrashHandler::do_backtrace(
      (void**)crash_info.backtrace_pointers_, BACKTRACE_MAX_SIZE,
      GetCurrentProcess(), GetCurrentThread(), exc->ContextRecord);

  RunChild();

  ++in_here;

  if (auto_restart) {
    Win32RestartProgram();
  }

  /* Now things get more risky. If we're fullscreen, the window will obscure
   * the crash dialog. Try to hide the window. Things might blow up here; do
   * this after DoSave, so we always write a crash dump. */
  if (GetWindowThreadProcessId(foreground_wnd, nullptr) ==
      GetCurrentThreadId()) {
    /* The thread that crashed was the thread that created the main window.
     * Hide the window. This will also restore the video mode, if necessary. */
    ShowWindow(foreground_wnd, SW_HIDE);
  } else {
    /* A different thread crashed. Simply kill all other windows. We can't
     * safely call ShowWindow; the main thread might be deadlocked. */
    RageThread::HaltAllThreads(true);
    ChangeDisplaySettings(nullptr, 0);
  }

  in_here = false;

  SetUnhandledExceptionFilter(nullptr);

  /* Forcibly terminate; if we keep going, we'll try to shut down threads and
   * do other things that may deadlock, which is confusing for users. */
  TerminateProcess(GetCurrentProcess(), 0);

  return EXCEPTION_EXECUTE_HANDLER;
}

long __stdcall CrashHandler::ExceptionHandler(EXCEPTION_POINTERS* exc) {
  /* If the stack overflowed, we have a very limited amount of stack space.
   * In this case, we will allocate a new 32KB stack, and run the exception
   * handler in it, to increase the chances of success. */
  if (exc->ExceptionRecord->ExceptionCode == EXCEPTION_STACK_OVERFLOW) {
    HANDLE exception_handler = CreateThread(
        nullptr, 1024 * 32, MainExceptionHandler, reinterpret_cast<LPVOID>(exc),
        0, nullptr);
    if (exception_handler == NULL) {
      TerminateProcess(GetCurrentProcess(), 0);
      return EXCEPTION_EXECUTE_HANDLER;
    }
    WaitForSingleObject(exception_handler, INFINITE);

    DWORD ret;
    GetExitCodeThread(exception_handler, &ret);
    CloseHandle(exception_handler);

    return static_cast<long>(ret);
  }

  // Common case.
  return static_cast<long>(MainExceptionHandler(exc));
}

//////////////////////////////////////////////////////////////////////////////

// Walks the stack and fills buf with up to 'size' return addresses.
// buf must be a writable array of void* of at least 'size' elements.
// If fewer than 'size' frames are found, the next element is set to nullptr.
// Only works on x86_64.
void CrashHandler::do_backtrace(
    void** buf, size_t size, HANDLE process, HANDLE thread,
    const CONTEXT* context) {
  if (!buf || !context || size == 0) {
    return;
  }

  size_t count = 0;

#if defined(_M_X64) || defined(__x86_64__)
  uintptr_t ip = static_cast<uintptr_t>(context->Rip);
  uintptr_t frame = static_cast<uintptr_t>(context->Rbp);
#else
  uintptr_t ip = 0, frame = 0;
#endif

  if (ip) {
    buf[count++] = reinterpret_cast<void*>(ip);
  }

  while (count < size - 1 && frame != 0) {
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(reinterpret_cast<LPCVOID>(frame), &mbi, sizeof(mbi)) ==
        0) {
      break;
    }

    DWORD protect = mbi.Protect & 0xff;
    if (!(protect == PAGE_READWRITE || protect == PAGE_READONLY ||
          protect == PAGE_EXECUTE_READ || protect == PAGE_EXECUTE_READWRITE)) {
      break;
    }

    size_t min_size = 2 * sizeof(uintptr_t);
    uintptr_t frame_end = frame + min_size;
    uintptr_t mbi_end =
        reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    if (frame < reinterpret_cast<uintptr_t>(mbi.BaseAddress) ||
        frame_end > mbi_end) {
      break;
    }

    // Read next frame pointer and return address
    const uintptr_t* frame_ptr = reinterpret_cast<const uintptr_t*>(frame);
    uintptr_t next_frame = frame_ptr[0];
    uintptr_t ret_addr = frame_ptr[1];

    if (next_frame <= frame || next_frame - frame > 1024 * 1024 ||
        ret_addr == 0) {
      break;
    }

    buf[count++] = reinterpret_cast<void*>(ret_addr);
    frame = next_frame;
  }

  if (count < size) {
    buf[count] = nullptr;
  }
}

// Dereferencing NULL pointer on purpose.
// This works even in the debugger.
#pragma warning(push)
#pragma warning(disable : 6011)
[[noreturn]]
static void debug_crash() {
  volatile int* p = nullptr;
  *p = 0;
}
#pragma warning(pop)

/* Get a stack trace of the current thread and the specified thread.
 * If id == GetInvalidThreadId(), then output a stack trace for every thread.
 */
void CrashHandler::ForceDeadlock(std::string reason, uint64_t id) {
  strncpy(
      crash_info.crash_reason_, reason.c_str(),
      sizeof(crash_info.crash_reason_));
  crash_info.crash_reason_[sizeof(crash_info.crash_reason_) - 1] = 0;

  /* Suspend the other thread we're going to backtrace. (We need to at least
   * suspend thread, for GetThreadContext to work.) */
  RageThread::HaltAllThreads(false);

  if (id == GetInvalidThreadId()) {
    // Backtrace all threads.
    int cnt = 0;
    for (int i = 0; RageThread::EnumThreadIDs(i, id); ++i) {
      if (id == GetInvalidThreadId()) {
        continue;
      }

      if (id == GetCurrentThreadId()) {
        continue;
      }

      const HANDLE thread = Win32ThreadIdToHandle(id);

      CONTEXT context;
      context.ContextFlags = CONTEXT_FULL;
      if (!GetThreadContext(thread, &context)) {
        size_t used = strlen(crash_info.crash_reason_);
        BufferedFormatString(
            crash_info.crash_reason_ + used,
            sizeof(crash_info.crash_reason_) - used,
            "; GetThreadContext(%Ix) failed",
            reinterpret_cast<uintptr_t>(thread));
      } else {
        static const void* kBacktracePointers[BACKTRACE_MAX_SIZE];
        do_backtrace(
            (void**)crash_info.alternate_thread_backtrace_[cnt],
            BACKTRACE_MAX_SIZE, GetCurrentProcess(), thread, &context);

        const char* name = RageThread::GetThreadNameByID(id);
        strncpy(
            crash_info.alternate_thread_name_[cnt], name ? name : "???",
            sizeof(crash_info.alternate_thread_name_[cnt]) - 1);

        ++cnt;
      }

      if (cnt == CrashInfo::kMaxBacktraceThreads) {
        break;
      }
    }
  } else {
    const HANDLE thread = Win32ThreadIdToHandle(id);

    CONTEXT context;
    context.ContextFlags = CONTEXT_FULL;
    if (!GetThreadContext(thread, &context)) {
      strcat(crash_info.crash_reason_, "(GetThreadContext failed)");
    } else {
      static const void* kBacktracePointers[BACKTRACE_MAX_SIZE];
      do_backtrace(
          (void**)crash_info.alternate_thread_backtrace_[0], BACKTRACE_MAX_SIZE,
          GetCurrentProcess(), thread, &context);

      const char* name = RageThread::GetThreadNameByID(id);
      strncpy(
          crash_info.alternate_thread_name_[0], name ? name : "???",
          sizeof(crash_info.alternate_thread_name_[0]) - 1);
    }
  }

  debug_crash();
}

void CrashHandler::ForceCrash(const char* reason) {
  strncpy(crash_info.crash_reason_, reason, sizeof(crash_info.crash_reason_));
  crash_info.crash_reason_[sizeof(crash_info.crash_reason_) - 1] = 0;

  debug_crash();
}

/*
 * (c) 1998-2001 Avery Lee
 * (c) 2003-2004 Glenn Maynard
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
