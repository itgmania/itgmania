#include "MemoryCardDriverThreaded_Folder.h"

#include <sys/stat.h>

#include <cerrno>
#include <cstdlib>
#include <string>
#include <vector>

#include "MemoryCardManager.h"
#include "PlayerNumber.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "arch/MemoryCard/MemoryCardDriver.h"

static int current_serial = 0;

MemoryCardDriverThreaded_Folder::MemoryCardDriverThreaded_Folder() {
  last_devices_ = 0;
}

MemoryCardDriverThreaded_Folder::~MemoryCardDriverThreaded_Folder() {}

bool MemoryCardDriverThreaded_Folder::FolderExists(std::string path) {
  if (path.empty()) {
    return false;
  }

  const char* pathname = path.c_str();
  struct stat info;

  int stat_rc = stat(pathname, &info);
  if (stat_rc != 0) {
    if (errno == ENOENT) {
      return false;
    }  // something along the path does not exist
    if (errno == ENOTDIR) {
      return false;
    }  // something in path prefix is not a dir
    return false;
  }

  if (info.st_mode & S_IFDIR) {
    return true;
  }

  return false;
}

bool MemoryCardDriverThreaded_Folder::TestWrite(UsbStorageDevice* device) {
  // TODO

  return true;
}

int MemoryCardDriverThreaded_Folder::GetActivePlayerMask() {
  int ret = 0;

  FOREACH_PlayerNumber(p) {
    const std::string folder = MEMCARDMAN->memory_card_os_mount_point_[p];

    if (FolderExists(folder)) {
      ret |= 1 << p;
    }
  }

  return ret;
}

bool MemoryCardDriverThreaded_Folder::USBStorageDevicesChanged() {
  return GetActivePlayerMask() != last_devices_;
}

void MemoryCardDriverThreaded_Folder::GetUSBStorageDevices(
    std::vector<UsbStorageDevice>& devices_out) {
  LOG->Trace("GetUSBStorageDevices");

  devices_out.clear();
  last_devices_ = GetActivePlayerMask();

  FOREACH_PlayerNumber(p) {
    if ((last_devices_ & (1 << p)) > 0) {
      UsbStorageDevice usbd;
      usbd.serial_ = StringConversion::ToString(current_serial++);
      usbd.sys_path_ = MEMCARDMAN->memory_card_os_mount_point_[p];
      usbd.os_mount_dir_ = MEMCARDMAN->memory_card_os_mount_point_[p];

      devices_out.push_back(usbd);
    }
  }
}

bool MemoryCardDriverThreaded_Folder::Mount(UsbStorageDevice* device) {
  return true;
}

void MemoryCardDriverThreaded_Folder::Unmount(UsbStorageDevice* device) {
  return;
}

/*
 * (c) 2018-2019 Electromuis
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
