#include "global.h"

// DO NOT USE stdio.h!  printf() calls malloc()!
// #include <stdio.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
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

static void SpliceProgramPath(char* buf, int bufsiz, const char* fn) {
  char tbuf[MAX_PATH];
  char* pszFile;

  GetModuleFileName(nullptr, tbuf, sizeof tbuf);
  GetFullPathName(tbuf, bufsiz, buf, &pszFile);
  strcpy(pszFile, fn);
}

///////////////////////////////////////////////////////////////////////////

static const struct ExceptionLookup {
  DWORD code;
  const char* name;
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
  for (int i = 0; exceptions[i].code; ++i) {
    if (exceptions[i].code == code) {
      return exceptions[i].name;
    }
  }

  return nullptr;
}

static CrashInfo g_CrashInfo;
static void GetReason(const EXCEPTION_RECORD* pRecord, CrashInfo* crash) {
  // fill out bomb reason
  const char* reason = LookupException(pRecord->ExceptionCode);

  if (reason == nullptr) {
    wsprintf(
        crash->m_CrashReason, "unknown exception 0x%08lx",
        pRecord->ExceptionCode);
  } else {
    strcpy(crash->m_CrashReason, reason);
  }
}

static HWND g_hForegroundWnd = nullptr;
void CrashHandler::SetForegroundWindow(HWND hWnd) { g_hForegroundWnd = hWnd; }

void WriteToChild(HANDLE hPipe, const void* pData, size_t iSize) {
  while (iSize) {
    DWORD iActual;
    if (!WriteFile(
            hPipe, pData, static_cast<DWORD>(iSize), &iActual, nullptr)) {
      return;
    }
    iSize -= iActual;
  }
}

/* Execute the child process. Return a handle to the process, a writable handle
 * to its stdin, and a readable handle to its stdout. */
bool StartChild(HANDLE& hProcess, HANDLE& hToStdin, HANDLE& hFromStdout) {
  char cwd[MAX_PATH];
  SpliceProgramPath(cwd, MAX_PATH, "");

  STARTUPINFO si;
  ZeroMemory(&si, sizeof(si));
  si.dwFlags |= STARTF_USESTDHANDLES;

  {
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = true;
    sa.lpSecurityDescriptor = nullptr;

    CreatePipe(&si.hStdInput, &hToStdin, &sa, 0);
    CreatePipe(&hFromStdout, &si.hStdOutput, &sa, 0);
    SetHandleInformation(hToStdin, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hFromStdout, HANDLE_FLAG_INHERIT, 0);
  }

  char szBuf[MAX_PATH] = "";
  GetModuleFileName(nullptr, szBuf, MAX_PATH);
  strcat(szBuf, " ");
  strcat(szBuf, CHILD_MAGIC_PARAMETER);

  PROCESS_INFORMATION pi;
  int iRet = CreateProcess(
      nullptr,  // pointer to name of executable module
      szBuf,    // pointer to command line string
      nullptr,  // process security attributes
      nullptr,  // thread security attributes
      true,     // handle inheritance flag
      0,        // creation flags
      nullptr,  // pointer to new environment block
      cwd,      // pointer to current directory name
      &si,      // pointer to STARTUPINFO
      &pi       // pointer to PROCESS_INFORMATION
  );

  CloseHandle(si.hStdInput);
  CloseHandle(si.hStdOutput);

  if (!iRet) {
    CloseHandle(hToStdin);
    CloseHandle(hFromStdout);
    return false;
  }

  hProcess = pi.hProcess;
  CloseHandle(pi.hThread);

  return true;
}

static const char* CrashGetModuleBaseName(HMODULE hmod, char* pszBaseName) {
  char szPath1[MAX_PATH];
  char szPath2[MAX_PATH];

  // XXX: It looks like nothing in here COULD throw an exception. Need to verify
  // that.
  //	__try {
  if (!GetModuleFileName(hmod, szPath1, sizeof(szPath1))) {
    return nullptr;
  }

  char* pszFile;
  DWORD dw = GetFullPathName(szPath1, sizeof(szPath2), szPath2, &pszFile);

  if (!dw || dw > sizeof(szPath2)) {
    return nullptr;
  }

  strcpy(pszBaseName, pszFile);

  pszFile = pszBaseName;

  char* period = nullptr;
  while (*pszFile++) {
    if (pszFile[-1] == '.') {
      period = pszFile - 1;
    }
  }

  if (period) {
    *period = 0;
  }
  //	} __except(1) {
  //		return nullptr;
  //	}

  return pszBaseName;
}

void RunChild() {
  HANDLE hProcess = nullptr, hToStdin = nullptr, hFromStdout = nullptr;
  if (!StartChild(hProcess, hToStdin, hFromStdout)) {
    ASSERT_M(0, "Failed to start child process");
  }

  // 0. Send a handle of this process to the crash handling process, which it
  // can use to handle symbol lookups.
  {
    HANDLE hTargetHandle;
    DuplicateHandle(
        GetCurrentProcess(), GetCurrentProcess(), hProcess, &hTargetHandle, 0,
        FALSE, DUPLICATE_SAME_ACCESS);

    WriteToChild(hToStdin, &hTargetHandle, sizeof(hTargetHandle));
  }

  // 1. Write the CrashData.
  WriteToChild(hToStdin, &g_CrashInfo, sizeof(g_CrashInfo));

  // 2. Write info.
  const TCHAR* p = RageLog::GetInfo();
  int iSize = static_cast<int>(strlen(p));
  WriteToChild(hToStdin, &iSize, sizeof(iSize));
  WriteToChild(hToStdin, p, iSize);

  // 3. Write AdditionalLog.
  p = RageLog::GetAdditionalLog();
  iSize = static_cast<int>(strlen(p));
  WriteToChild(hToStdin, &iSize, sizeof(iSize));
  WriteToChild(hToStdin, p, iSize);

  // 4. Write RecentLogs.
  int cnt = 0;
  const TCHAR* ps[1024];
  while (cnt < 1024 && (ps[cnt] = RageLog::GetRecentLog(cnt)) != nullptr) {
    ++cnt;
  }

  WriteToChild(hToStdin, &cnt, sizeof(cnt));
  for (int i = 0; i < cnt; ++i) {
    iSize = static_cast<int>(strlen(ps[i])) + 1;
    WriteToChild(hToStdin, &iSize, sizeof(iSize));
    WriteToChild(hToStdin, ps[i], iSize);
  }

  // 5. Write CHECKPOINTs.
  static TCHAR buf[1024 * 32];
  Checkpoints::GetLogs(buf, sizeof(buf), "$$");
  iSize = static_cast<int>(strlen(buf)) + 1;
  WriteToChild(hToStdin, &iSize, sizeof(iSize));
  WriteToChild(hToStdin, buf, iSize);

  // 6. Write the crashed thread's name.
  p = RageThread::GetCurrentThreadName();
  iSize = static_cast<int>(strlen(p)) + 1;
  WriteToChild(hToStdin, &iSize, sizeof(iSize));
  WriteToChild(hToStdin, p, iSize);

  /* The parent process needs to access this process briefly. When it's done,
   * it'll close the handle. Wait until we see that before exiting. */
  while (1) {
    /* Ugly: the new process can't execute GetModuleFileName on this process,
     * since GetModuleFileNameEx might not be available. Run the requests here.
     */
    HMODULE hMod;
    DWORD iActual;
    if (!ReadFile(hFromStdout, &hMod, sizeof(hMod), &iActual, nullptr)) {
      break;
    }

    TCHAR szName[MAX_PATH];
    if (!CrashGetModuleBaseName(hMod, szName)) {
      strcpy(szName, "???");
    }
    iSize = static_cast<int>(strlen(szName));
    WriteToChild(hToStdin, &iSize, sizeof(iSize));
    WriteToChild(hToStdin, szName, iSize);
  }
}

static DWORD WINAPI MainExceptionHandler(LPVOID lpParameter) {
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
  PEXCEPTION_POINTERS pExc = reinterpret_cast<PEXCEPTION_POINTERS>(lpParameter);
  switch (pExc->ExceptionRecord->ExceptionCode) {
    case EXCEPTION_FLT_INVALID_OPERATION:
    case EXCEPTION_FLT_DENORMAL_OPERAND:
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
    case EXCEPTION_FLT_OVERFLOW:
    case EXCEPTION_FLT_UNDERFLOW:
    case EXCEPTION_FLT_INEXACT_RESULT:
      pExc->ContextRecord->FltSave.ControlWord |= 0x3F;
      return static_cast<DWORD>(EXCEPTION_CONTINUE_EXECUTION);
  }

  static int InHere = 0;
  if (InHere > 0) {
    /* If we get here, then we've been called recursively, which means we
     * crashed. If InHere is greater than 1, then we crashed after writing
     * the crash dump; say so. */
    SetUnhandledExceptionFilter(nullptr);
    MessageBox(
        nullptr,
        InHere == 1 ? "The error reporting interface has crashed.\n"
                    : "The error reporting interface has crashed. However, "
                      "crashinfo.txt was written successfully to the program "
                      "directory.\n",
        "Fatal Error", MB_OK);
#ifdef DEBUG
    DebugBreak();
#endif

    return EXCEPTION_EXECUTE_HANDLER;
  }
  ++InHere;
  /////////////////////////

  RageThread::HaltAllThreads(false);

  if (!g_CrashInfo.m_CrashReason[0]) {
    GetReason(pExc->ExceptionRecord, &g_CrashInfo);
  }
  CrashHandler::do_backtrace(
      (void**)g_CrashInfo.m_BacktracePointers, BACKTRACE_MAX_SIZE,
      GetCurrentProcess(), GetCurrentThread(), pExc->ContextRecord);

  RunChild();

  ++InHere;

  if (g_bAutoRestart) {
    Win32RestartProgram();
  }

  /* Now things get more risky. If we're fullscreen, the window will obscure
   * the crash dialog. Try to hide the window. Things might blow up here; do
   * this after DoSave, so we always write a crash dump. */
  if (GetWindowThreadProcessId(g_hForegroundWnd, nullptr) ==
      GetCurrentThreadId()) {
    /* The thread that crashed was the thread that created the main window.
     * Hide the window. This will also restore the video mode, if necessary. */
    ShowWindow(g_hForegroundWnd, SW_HIDE);
  } else {
    /* A different thread crashed. Simply kill all other windows. We can't
     * safely call ShowWindow; the main thread might be deadlocked. */
    RageThread::HaltAllThreads(true);
    ChangeDisplaySettings(nullptr, 0);
  }

  InHere = false;

  SetUnhandledExceptionFilter(nullptr);

  /* Forcibly terminate; if we keep going, we'll try to shut down threads and
   * do other things that may deadlock, which is confusing for users. */
  TerminateProcess(GetCurrentProcess(), 0);

  return EXCEPTION_EXECUTE_HANDLER;
}

long __stdcall CrashHandler::ExceptionHandler(EXCEPTION_POINTERS* pExc) {
  /* If the stack overflowed, we have a very limited amount of stack space.
   * In this case, we will allocate a new 32KB stack, and run the exception
   * handler in it, to increase the chances of success. */
  if (pExc->ExceptionRecord->ExceptionCode == EXCEPTION_STACK_OVERFLOW) {
    HANDLE hExceptionHandler = CreateThread(
        nullptr, 1024 * 32, MainExceptionHandler,
        reinterpret_cast<LPVOID>(pExc), 0, nullptr);
    if (hExceptionHandler == NULL) {
      TerminateProcess(GetCurrentProcess(), 0);
      return EXCEPTION_EXECUTE_HANDLER;
    }
    WaitForSingleObject(hExceptionHandler, INFINITE);

    DWORD ret;
    GetExitCodeThread(hExceptionHandler, &ret);
    CloseHandle(hExceptionHandler);

    return static_cast<long>(ret);
  }

  // Common case.
  return static_cast<long>(MainExceptionHandler(pExc));
}

//////////////////////////////////////////////////////////////////////////////

// Walks the stack and fills buf with up to 'size' return addresses.
// buf must be a writable array of void* of at least 'size' elements.
// If fewer than 'size' frames are found, the next element is set to nullptr.
// Only works on x86_64.
void CrashHandler::do_backtrace(
    void** buf, size_t size, HANDLE hProcess, HANDLE hThread,
    const CONTEXT* pContext) {
  if (!buf || !pContext || size == 0) {
    return;
  }

  size_t count = 0;

#if defined(_M_X64) || defined(__x86_64__)
  uintptr_t ip = static_cast<uintptr_t>(pContext->Rip);
  uintptr_t frame = static_cast<uintptr_t>(pContext->Rbp);
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
 * If iID == GetInvalidThreadId(), then output a stack trace for every thread.
 */
void CrashHandler::ForceDeadlock(std::string reason, uint64_t iID) {
  strncpy(
      g_CrashInfo.m_CrashReason, reason.c_str(),
      sizeof(g_CrashInfo.m_CrashReason));
  g_CrashInfo.m_CrashReason[sizeof(g_CrashInfo.m_CrashReason) - 1] = 0;

  /* Suspend the other thread we're going to backtrace. (We need to at least
   * suspend hThread, for GetThreadContext to work.) */
  RageThread::HaltAllThreads(false);

  if (iID == GetInvalidThreadId()) {
    // Backtrace all threads.
    int iCnt = 0;
    for (int i = 0; RageThread::EnumThreadIDs(i, iID); ++i) {
      if (iID == GetInvalidThreadId()) {
        continue;
      }

      if (iID == GetCurrentThreadId()) {
        continue;
      }

      const HANDLE hThread = Win32ThreadIdToHandle(iID);

      CONTEXT context;
      context.ContextFlags = CONTEXT_FULL;
      if (!GetThreadContext(hThread, &context)) {
        wsprintf(
            g_CrashInfo.m_CrashReason + strlen(g_CrashInfo.m_CrashReason),
            "; GetThreadContext(%Ix) failed",
            reinterpret_cast<uintptr_t>(hThread));
      } else {
        static const void* BacktracePointers[BACKTRACE_MAX_SIZE];
        do_backtrace(
            (void**)g_CrashInfo.m_AlternateThreadBacktrace[iCnt],
            BACKTRACE_MAX_SIZE, GetCurrentProcess(), hThread, &context);

        const char* pName = RageThread::GetThreadNameByID(iID);
        strncpy(
            g_CrashInfo.m_AlternateThreadName[iCnt], pName ? pName : "???",
            sizeof(g_CrashInfo.m_AlternateThreadName[iCnt]) - 1);

        ++iCnt;
      }

      if (iCnt == CrashInfo::MAX_BACKTRACE_THREADS) {
        break;
      }
    }
  } else {
    const HANDLE hThread = Win32ThreadIdToHandle(iID);

    CONTEXT context;
    context.ContextFlags = CONTEXT_FULL;
    if (!GetThreadContext(hThread, &context)) {
      strcat(g_CrashInfo.m_CrashReason, "(GetThreadContext failed)");
    } else {
      static const void* BacktracePointers[BACKTRACE_MAX_SIZE];
      do_backtrace(
          (void**)g_CrashInfo.m_AlternateThreadBacktrace[0], BACKTRACE_MAX_SIZE,
          GetCurrentProcess(), hThread, &context);

      const char* pName = RageThread::GetThreadNameByID(iID);
      strncpy(
          g_CrashInfo.m_AlternateThreadName[0], pName ? pName : "???",
          sizeof(g_CrashInfo.m_AlternateThreadName[0]) - 1);
    }
  }

  debug_crash();
}

void CrashHandler::ForceCrash(const char* reason) {
  strncpy(g_CrashInfo.m_CrashReason, reason, sizeof(g_CrashInfo.m_CrashReason));
  g_CrashInfo.m_CrashReason[sizeof(g_CrashInfo.m_CrashReason) - 1] = 0;

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
