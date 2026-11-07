#include "MemoryCardDriver.h"

#include <algorithm>
#include <string>
#include <vector>

#include "EnumHelper.h"
#include "MemoryCardDriverThreaded_Folder.h"
#include "MemoryCardDriver_Null.h"
#include "Preference.h"
#include "ProfileManager.h"
#include "RageFileManager.h"
#include "RageLog.h"
// Provides platform-specific ARCH_MEMORY_CARD_DRIVER selection.
#include "arch/arch_default.h"

static const std::string TEMP_MOUNT_POINT = "/@mctemptimeout/";

enum MemoryCardDriverType {
  MemoryCardDriverType_USB,
  MemoryCardDriverType_Directory,
  NUM_MemoryCardDriverType,
  MemoryCardDriverType_Invalid
};

static const char* MemoryCardDriverTypeNames[] = {"USB", "Directory"};
XToString(MemoryCardDriverType);
StringToX(MemoryCardDriverType);
LuaXType(MemoryCardDriverType);

Preference<MemoryCardDriverType> g_MemoryCardDriver(
    "MemoryCardDriver", MemoryCardDriverType_USB, nullptr,
    PreferenceType::Immutable);

bool UsbStorageDevice::operator==(const UsbStorageDevice& other) const {
  if (bus_ != other.bus_) {
    return false;
  }
  if (port_ != other.port_) {
    return false;
  }
  if (level_ != other.level_) {
    return false;
  }
  if (os_mount_dir_ != other.os_mount_dir_) {
    return false;
  }
  return true;
}

void UsbStorageDevice::SetOsMountDir(const std::string& s) {
  os_mount_dir_ = s;
}

bool MemoryCardDriver::NeedUpdate(bool mount) {
  if (mount) {
    /* Check if any devices need a write test. */
    for (unsigned i = 0; i < devices_last_seen_.size(); i++) {
      const UsbStorageDevice& d = devices_last_seen_[i];
      if (d.state_ == UsbStorageDevice::STATE_CHECKING) {
        return true;
      }
    }
  }

  return USBStorageDevicesChanged();
}

bool MemoryCardDriver::DoOneUpdate(
    bool mount, std::vector<UsbStorageDevice>& storage_devices_out) {
  if (!NeedUpdate(mount)) {
    return false;
  }

  std::vector<UsbStorageDevice> old = devices_last_seen_;  // copy
  GetUSBStorageDevices(storage_devices_out);

  // log connects
  for (UsbStorageDevice& newd : storage_devices_out) {
    std::vector<UsbStorageDevice>::iterator iter =
        find(old.begin(), old.end(), newd);
    if (iter == old.end()) {  // didn't find
      LOG->Trace("New device connected: %s", newd.device_.c_str());
    }
  }

  /* When we first see a device, regardless of bMount, just return it as
   * CHECKING, so the main thread knows about the device.  On the next call
   * where bMount is true, check it. */
  for (unsigned i = 0; i < storage_devices_out.size(); i++) {
    UsbStorageDevice& d = storage_devices_out[i];

    /* If this device was just connected (it wasn't here last time), set it to
     * CHECKING and return it, to let the main thread know about the device
     * before we start checking. */
    std::vector<UsbStorageDevice>::iterator iter =
        find(old.begin(), old.end(), d);
    if (iter == old.end())  // didn't find
    {
      LOG->Trace("New device entering CHECKING: %s", d.device_.c_str());
      d.state_ = UsbStorageDevice::STATE_CHECKING;
      continue;
    }

    /* Preserve the state of the device, and any data loaded from previous
     * checks. */
    d.state_ = iter->state_;
    d.is_name_available_ = iter->is_name_available_;
    d.name_ = iter->name_;

    /* The device was here last time.  If CHECKING, check the device now, if
     * we're allowed to. */
    if (d.state_ == UsbStorageDevice::STATE_CHECKING) {
      if (!mount) {
        /* We can't check it now.  Keep STATE_CHECKING, and check it when we
         * can. */
        d.state_ = UsbStorageDevice::STATE_CHECKING;
        continue;
      }

      if (!this->Mount(&d)) {
        d.SetError("MountFailed");
        continue;
      }

      if (TestWrite(&d)) {
        /* We've successfully mounted and tested the device.  Read the
         * profile name (by mounting a temporary, private mountpoint),
         * and then unmount it until Mount() is called. */
        d.state_ = UsbStorageDevice::STATE_READY;

        FILEMAN->Mount("dirro", d.os_mount_dir_, TEMP_MOUNT_POINT);
        d.is_name_available_ = PROFILEMAN->FastLoadProfileNameFromMemoryCard(
            TEMP_MOUNT_POINT, d.name_);
        FILEMAN->Unmount("dirro", d.os_mount_dir_, TEMP_MOUNT_POINT);
      }

      this->Unmount(&d);

      LOG->Trace(
          "WriteTest: %s, Name: %s",
          d.state_ == UsbStorageDevice::STATE_ERROR ? "failed" : "succeeded",
          d.name_.c_str());
    }
  }

  devices_last_seen_ = storage_devices_out;

  return true;
}

MemoryCardDriver* MemoryCardDriver::Create() {
  MemoryCardDriver* ret = nullptr;

  switch (g_MemoryCardDriver) {
    case MemoryCardDriverType_Directory:
      ret = new MemoryCardDriverThreaded_Folder;
      break;
    case MemoryCardDriverType_USB:
#ifdef ARCH_MEMORY_CARD_DRIVER
      ret = new ARCH_MEMORY_CARD_DRIVER;
#else
      LOG->Warn(
          "MemoryCardDriverType_USB was selected, "
          "but no driver was compiled in.");
#endif
      break;
    default:
      break;
  }

  if (!ret) {
    LOG->Info("MemoryCardDriver_Null is loaded.");
    ret = new MemoryCardDriver_Null;
  }

  return ret;
}

/*
 * (c) 2002-2004 Glenn Maynard
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
