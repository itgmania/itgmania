#include "RestartProgram.h"

#include <windows.h>

#include <string>

#include "global.h"

void Win32RestartProgram() {
  TCHAR szFullAppPath[MAX_PATH];
  const size_t pathLength = GetModuleFileName(nullptr, szFullAppPath, MAX_PATH);

  std::basic_string<TCHAR> cmdLine;
  cmdLine.reserve(pathLength + 3);
  cmdLine += TEXT('"');
  cmdLine += szFullAppPath;
  cmdLine += TEXT('"');

  // Relaunch
  PROCESS_INFORMATION pi = {};
  STARTUPINFO si = {};
  si.cb = sizeof(si);
  if (!CreateProcess(
          nullptr,         // module name
          cmdLine.data(),  // full command line string
          nullptr,         // process security attrs
          nullptr,         // thread security attrs
          FALSE,           // handle inheritance flag
          0,               // creation flags
          nullptr,         // environment
          nullptr,         // current directory
          &si,             // startup info
          &pi)) {          // process info
    return;
  }

  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);

  ExitProcess(0);

  /* not reached */
}

/*
 * (c) 2002-2004 Chris Danford
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
