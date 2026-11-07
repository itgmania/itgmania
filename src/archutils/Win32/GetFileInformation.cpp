#include "GetFileInformation.h"

#include <sys/stat.h>

#include <cstdint>
#include <string>
#include <vector>

#include "RageUtil.h"
#include "archutils/Win32/ErrorStrings.h"
#include "global.h"

// clang-format off
#include <windows.h>
#include <tlhelp32.h>
// clang-format on

#pragma comment(lib, "version.lib")

bool GetFileVersion(const std::string& file, std::string& out) {
  do {
    DWORD ignore;
    DWORD size = GetFileVersionInfoSize(file.c_str(), &ignore);
    if (!size) {
      break;
    }

    std::vector<char> version_buffer(size);
    if (!GetFileVersionInfo(file.c_str(), 0, size, version_buffer.data())) {
      break;
    }

    WORD* trans;
    UINT trans_cnt;

    if (!VerQueryValue(
            version_buffer.data(), "\\VarFileInfo\\Translation", (void**)&trans,
            &trans_cnt)) {
      break;
    }

    if (trans_cnt == 0) {
      break;
    }

    char* str;
    UINT len;

    std::string res =
        ssprintf("\\StringFileInfo\\%04x%04x\\FileVersion", trans[0], trans[1]);
    if (!VerQueryValue(
            version_buffer.data(), res.c_str(), (void**)&str, &len) ||
        len < 1) {
      break;
    }

    out = std::string(str, len - 1);
  } while (0);

  // Get the size and date.
  struct stat st;
  if (stat(file.c_str(), &st) != -1) {
    struct tm t;
    gmtime_r(&st.st_mtime, &t);
    if (!out.empty()) {
      out += " ";
    }
    out += ssprintf(
        "[%ib, %02i-%02i-%04i]", st.st_size, t.tm_mon + 1, t.tm_mday,
        t.tm_year + 1900);
  }

  return true;
}

std::string FindSystemFile(const std::string& file) {
  char windows_path[MAX_PATH];
  GetWindowsDirectory(windows_path, MAX_PATH);

  const char* paths[] = {"\\system32\\", "\\system32\\drivers\\",
                         "\\system\\",   "\\system\\drivers\\",
                         "\\",           nullptr};

  for (int i = 0; paths[i]; ++i) {
    std::string path = ssprintf("%s%s%s", windows_path, paths[i], file.c_str());
    struct stat buf;
    if (!stat(path.c_str(), &buf)) {
      return path;
    }
  }

  return std::string();
}

/* Get the full path of the process running in process_id. On error, false is
 * returned and an error message is placed in name. */
bool GetProcessFileName(uint32_t process_id, std::string& name) {
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, process_id);
  if (snap == INVALID_HANDLE_VALUE) {
    name = werr_ssprintf(GetLastError(), "CreateToolhelp32Snapshot");
    return false;
  }

  MODULEENTRY32 me;
  me.dwSize = sizeof(MODULEENTRY32);
  if (Module32First(snap, &me)) {
    name = me.szExePath;
    CloseHandle(snap);
    return true;
  }

  CloseHandle(snap);
  name = werr_ssprintf(GetLastError(), "Module32First");
  return false;
}

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
