#include "DebugInfoHunt.h"

// clang-format off
#include <windows.h>
#include <mmsystem.h>
// clang-format on

#include <string>
#include <vector>

#include "ErrorStrings.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "RegistryAccess.h"
#include "VideoDriverInfo.h"
#include "global.h"

static void LogVideoDriverInfo(VideoDriverInfo info) {
  LOG->Info(
      "Video driver: %s [%s]", info.description_.c_str(),
      info.provider_.c_str());
  LOG->Info(
      "              %s, %s [%s]", info.version_.c_str(), info.date_.c_str(),
      info.device_id_.c_str());
}

static void GetMemoryDebugInfo() {
  MEMORYSTATUSEX mem;
  mem.dwLength = sizeof(mem);
  if (GlobalMemoryStatusEx(&mem)) {
    LOG->Info(
        "Memory: %lluMB total, %lluMB swap (%lluMB swap avail)",
        mem.ullTotalPhys / (1024 * 1024), mem.ullTotalPageFile / (1024 * 1024),
        mem.ullAvailPageFile / (1024 * 1024));
  } else {
    LOG->Warn(
        "GlobalMemoryStatusEx failed: %s",
        werr_ssprintf(GetLastError(), "GlobalMemoryStatusEx").c_str());
  }
}

static void GetDisplayDriverDebugInfo() {
  std::string primary_device_name = GetPrimaryVideoName();

  if (primary_device_name == "") {
    LOG->Info("Primary display driver could not be determined.");
  }

  bool logged_something = false;
  for (int i = 0; true; i++) {
    VideoDriverInfo info;
    if (!GetVideoDriverInfo(i, info)) {
      break;
    }

    if (primary_device_name == "")  // failed to get primary display name (NT4)
    {
      LogVideoDriverInfo(info);
      logged_something = true;
    } else if (info.description_ == primary_device_name) {
      LogVideoDriverInfo(info);
      logged_something = true;
      break;
    }
  }
  if (!logged_something) {
    LOG->Info("Primary display driver: %s", primary_device_name.c_str());
    LOG->Warn("Couldn't find primary display driver; logging all drivers");

    for (int i = 0; true; i++) {
      VideoDriverInfo info;
      if (!GetVideoDriverInfo(i, info)) {
        break;
      }

      LogVideoDriverInfo(info);
    }
  }
}

static std::string wo_ssprintf(MMRESULT err, const char* fmt, ...) {
  char buf[MAXERRORLENGTH];
  waveOutGetErrorText(err, buf, MAXERRORLENGTH);

  va_list va;
  va_start(va, fmt);
  std::string s = vssprintf(fmt, va);
  va_end(va);

  return s += ssprintf("(%s)", buf);
}

static void GetDriveDebugInfo() {
  /*
   * HKEY_LOCAL_MACHINE\HARDWARE\DEVICEMAP\Scsi\
   *    Scsi Port *\
   *      DMAEnabled  0 or 1
   *      Driver      "Ultra", "atapi", etc
   *      Scsi Bus *\
   *	     Target Id *\
   *	 	   Logical Unit Id *\
   *		     Identifier  "WDC WD1200JB-75CRA0"
   *			 Type        "DiskPeripheral"
   */
  std::vector<std::string> ports;
  if (!RegistryAccess::GetRegSubKeys(
          "HKEY_LOCAL_MACHINE\\HARDWARE\\DEVICEMAP\\Scsi", ports)) {
    return;
  }

  for (unsigned i = 0; i < ports.size(); ++i) {
    int dma_enabled = -1;
    RegistryAccess::GetRegValue(ports[i], "DMAEnabled", dma_enabled);

    std::string driver;
    RegistryAccess::GetRegValue(ports[i], "Driver", driver);

    std::vector<std::string> busses;
    if (!RegistryAccess::GetRegSubKeys(ports[i], busses, "Scsi Bus .*")) {
      continue;
    }

    for (unsigned bus = 0; bus < busses.size(); ++bus) {
      std::vector<std::string> target_ids;
      if (!RegistryAccess::GetRegSubKeys(
              busses[bus], target_ids, "Target Id .*")) {
        continue;
      }

      for (unsigned tid = 0; tid < target_ids.size(); ++tid) {
        std::vector<std::string> luids;
        if (!RegistryAccess::GetRegSubKeys(
                target_ids[tid], luids, "Logical Unit Id .*")) {
          continue;
        }

        for (unsigned luid = 0; luid < luids.size(); ++luid) {
          std::string identifier;
          RegistryAccess::GetRegValue(luids[luid], "Identifier", identifier);
          TrimRight(identifier);
          LOG->Info(
              "Drive: \"%s\" Driver: %s DMA: %s", identifier.c_str(),
              driver.c_str(),
              dma_enabled == 1    ? "yes"
              : dma_enabled == -1 ? "N/A"
                                  : "NO");
        }
      }
    }
  }
}

static void GetWindowsVersionDebugInfo() {
  typedef LONG(WINAPI * RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
  HMODULE mod = GetModuleHandleW(L"ntdll.dll");
  if (mod) {
    RtlGetVersionPtr rtl_get_version =
        (RtlGetVersionPtr)GetProcAddress(mod, "RtlGetVersion");
    if (rtl_get_version) {
      OSVERSIONINFOEXW osvi = {0};
      osvi.dwOSVersionInfoSize = sizeof(osvi);
      if (rtl_get_version((PRTL_OSVERSIONINFOW)&osvi) == 0) {
        std::string ver = ssprintf(
            "Windows %lu.%lu (", osvi.dwMajorVersion, osvi.dwMinorVersion);
        if (osvi.dwMajorVersion == 6 && osvi.dwMinorVersion == 1) {
          ver += "Win7";
        } else if (osvi.dwMajorVersion == 6 && osvi.dwMinorVersion == 2) {
          ver += "Win8";
        } else if (osvi.dwMajorVersion == 6 && osvi.dwMinorVersion == 3) {
          ver += "Win8.1";
        } else if (
            osvi.dwMajorVersion == 10 && osvi.dwMinorVersion == 0 &&
            osvi.dwBuildNumber < 22000) {
          ver += "Win10";
        } else if (
            osvi.dwMajorVersion == 10 && osvi.dwMinorVersion == 0 &&
            osvi.dwBuildNumber >= 22000) {
          ver += "Win11";
        } else {
          ver += "unknown";
        }

        ver +=
            ssprintf(") build %lu [%s]", osvi.dwBuildNumber, osvi.szCSDVersion);
        LOG->Info("%s", ver.c_str());
        return;
      }
    }
  }
  LOG->Info("RtlGetVersion failed!");
}

static void GetSoundDriverDebugInfo() {
  int cnt = waveOutGetNumDevs();

  for (int i = 0; i < cnt; ++i) {
    WAVEOUTCAPS caps;

    MMRESULT ret = waveOutGetDevCaps(i, &caps, sizeof(caps));
    if (ret != MMSYSERR_NOERROR) {
      LOG->Info(wo_ssprintf(ret, "waveOutGetDevCaps(%i) failed", i).c_str());
      continue;
    }
    LOG->Info(
        "Sound device %i: %s, %i.%i, MID %i, PID %i %s", i, caps.szPname,
        HIBYTE(caps.vDriverVersion), LOBYTE(caps.vDriverVersion), caps.wMid,
        caps.wPid,
        caps.dwSupport & WAVECAPS_SAMPLEACCURATE ? "" : "(INACCURATE)");
  }
}

void SearchForDebugInfo() {
  GetWindowsVersionDebugInfo();
  GetMemoryDebugInfo();
  GetDisplayDriverDebugInfo();
  GetDriveDebugInfo();
  GetSoundDriverDebugInfo();
}

/*
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
