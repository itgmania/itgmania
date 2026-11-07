#include "VideoDriverInfo.h"

#include <windows.h>

#include <string>
#include <vector>

#include "RageLog.h"
#include "RageUtil.h"
#include "RegistryAccess.h"
#include "global.h"

// this will not work on 95 and NT because of EnumDisplayDevices
std::string GetPrimaryVideoName() {
  typedef BOOL(WINAPI * pfnEnumDisplayDevices)(
      PVOID, DWORD, PDISPLAY_DEVICE, DWORD);
  pfnEnumDisplayDevices enum_display_devices;
  HINSTANCE inst_user32;

  inst_user32 = LoadLibrary("User32.DLL");
  if (!inst_user32) {
    return std::string();
  }

  // VC6 don't have a stub to static link with, so link dynamically.
  enum_display_devices =
      (pfnEnumDisplayDevices)GetProcAddress(inst_user32, "EnumDisplayDevicesA");
  if (enum_display_devices == nullptr) {
    FreeLibrary(inst_user32);
    return std::string();
  }

  std::string primary_device_name;
  for (int i = 0; true; ++i) {
    DISPLAY_DEVICE dd;
    ZERO(dd);
    dd.cb = sizeof(dd);
    if (!enum_display_devices(nullptr, i, &dd, 0)) {
      break;
    }
    if (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) {
      primary_device_name = (char*)dd.DeviceString;
      break;
    }
  }

  FreeLibrary(inst_user32);
  TrimRight(primary_device_name);
  return primary_device_name;
}

std::string GetPrimaryVideoDriverName() {
  std::string primary_device_name = GetPrimaryVideoName();
  if (primary_device_name != "") {
    return primary_device_name;
  }

  LOG->Warn("GetPrimaryVideoName failed; renderer selection may be wrong");

  VideoDriverInfo info;
  if (!GetVideoDriverInfo(0, info)) {
    return "(ERROR DETECTING VIDEO DRIVER)";
  }

  return info.description_;
}

/* Get info for the given card number.  Return false if that card doesn't exist.
 */
bool GetVideoDriverInfo(int cardno, VideoDriverInfo& info) {
  static bool initialized = false;
  static std::vector<std::string> lst;
  if (!initialized) {
    initialized = true;

    const std::string kTopKey =
        "HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Class\\{"
        "4D36E968-E325-11CE-BFC1-08002BE10318}";

    RegistryAccess::GetRegSubKeys(kTopKey, lst, ".*", false);

    for (int i = lst.size() - 1; i >= 0; --i) {
      /* Remove all keys that aren't four characters long ("Properties"). */
      if (lst[i].size() != 4) {
        lst.erase(lst.begin() + i);
        continue;
      }

      lst[i] = kTopKey + "\\" + lst[i];
    }

    if (lst.size() == 0) {
      LOG->Warn("GetVideoDriverInfo error: no cards found!");
      return false;
    }
  }

  while (cardno < (int)lst.size()) {
    const std::string key = lst[cardno];

    if (!RegistryAccess::GetRegValue(key, "DriverDesc", info.description_)) {
      /* Remove this one from the list and ignore it, */
      lst.erase(lst.begin() + cardno);
      continue;
    }
    TrimRight(info.description_);

    RegistryAccess::GetRegValue(key, "DriverDate", info.date_);
    RegistryAccess::GetRegValue(key, "MatchingDeviceId", info.device_id_);
    RegistryAccess::GetRegValue(key, "ProviderName", info.provider_);
    RegistryAccess::GetRegValue(key, "DriverVersion", info.version_);

    return true;
  }

  return false;
}

/*
 * (c) 2002-2004 Chris Danford, Glenn Maynard
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
