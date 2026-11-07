#include "MemoryCardDriverThreaded_Windows.h"

#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include "MemoryCardManager.h"
#include "PlayerNumber.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "RageUtil/RandomNumbers.h"
#include "archutils/Win32/ErrorStrings.h"
#include "global.h"

MemoryCardDriverThreaded_Windows::MemoryCardDriverThreaded_Windows() {
  last_logical_drives_ = 0;
}

MemoryCardDriverThreaded_Windows::~MemoryCardDriverThreaded_Windows() {}

static bool TestReady(const std::string& drive, std::string& volume_label_out) {
  TCHAR volume_name_buffer[MAX_PATH];
  DWORD volume_serial_number;
  DWORD maximum_component_length;
  DWORD file_system_flags;
  TCHAR file_system_name_buffer[MAX_PATH];

  if (!GetVolumeInformation(
          drive.c_str(), volume_name_buffer, sizeof(volume_name_buffer),
          &volume_serial_number, &maximum_component_length, &file_system_flags,
          file_system_name_buffer, sizeof(file_system_name_buffer))) {
    return false;
  }

  volume_label_out = volume_name_buffer;
  return true;
}

bool MemoryCardDriverThreaded_Windows::TestWrite(UsbStorageDevice* device) {
  /* Try to write a file, to check if the device is writable and that we have
   * write permission. Use FILE_ATTRIBUTE_TEMPORARY to try to avoid actually
   * writing to the device.  This reduces the chance of corruption if the user
   * removes the device immediately, without doing anything. */
  for (int i = 0; i < 10; ++i) {
    HANDLE file = CreateFile(
        ssprintf("%stmp%i", device->os_mount_dir_.c_str(), RandomInt(100000))
            .c_str(),
        GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);

    if (file == INVALID_HANDLE_VALUE) {
      DWORD error = GetLastError();
      LOG->Warn(
          werr_ssprintf(
              error, "Couldn't write to %s", device->os_mount_dir_.c_str())
              .c_str());

      if (error == ERROR_FILE_EXISTS) {
        continue;
      }
      break;
    }

    CloseHandle(file);
    return true;
  }

  device->SetError("TestFailed");
  return false;
}

static bool IsFloppyDrive(const std::string& drive) {
  char buf[1024];

  int ret = QueryDosDevice(drive.c_str(), buf, 1024);
  if (ret == 0) {
    LOG->Warn(werr_ssprintf(GetLastError(), "QueryDosDevice(%s)", drive.c_str())
                  .c_str());
    return false;
  }

  // Make sure buf is terminated with two nulls.  This only may be needed if
  // the buffer filled.
  buf[ret - 2] = 0;
  buf[ret - 1] = 0;

  const char* p = buf;
  while (*p) {
    if (BeginsWith(p, "\\Device\\Floppy")) {
      return true;
    }

    p += strlen(p) + 1;
  }
  return false;
}

void MemoryCardDriverThreaded_Windows::GetUSBStorageDevices(
    std::vector<UsbStorageDevice>& devices_out) {
  LOG->Trace("MemoryCardDriverThreaded_Windows::GetUSBStorageDevices");

  DWORD logical_drives = ::GetLogicalDrives();
  last_logical_drives_ = logical_drives;

  const int kMaxDrives = 26;
  for (int i = 0; i < kMaxDrives; ++i) {
    DWORD mask = (1 << i);
    if (!(last_logical_drives_ & mask)) {
      continue;  // drive letter is invalid
    }

    std::string drive = ssprintf("%c:", 'A' + i % 26);

    LOG->Trace(drive.c_str());

    // we definitely don't need this lol
    if (IsFloppyDrive(drive.c_str())) {
      LOG->Trace("IsFloppyDrive");
      continue;
    }

    // Testing hack:  Allow non-removable drive letters to be used if that
    // driver letter is specified as a memory_card_os_mount_point_.

    bool is_specified_mount_point = false;
    FOREACH_ENUM(PlayerNumber, p)
    is_specified_mount_point |= EqualsNoCase(
        MEMCARDMAN->memory_card_os_mount_point_[p].Get(), drive.c_str());

    std::string drive_path = drive + "\\";

    if (is_specified_mount_point) {
      LOG->Trace(
          "'%s' is a specified mount point.  Allowing...", drive.c_str());
    } else {
      if (GetDriveType(drive_path.c_str()) != DRIVE_REMOVABLE) {
        LOG->Trace("not DRIVE_REMOVABLE");
        continue;
      }
    }

    std::string volume_label;
    if (!TestReady(drive_path.c_str(), volume_label)) {
      LOG->Trace("not TestReady");
      continue;
    }

    devices_out.push_back(UsbStorageDevice());
    UsbStorageDevice& usbd = devices_out.back();
    usbd.SetOsMountDir(drive.c_str());
    usbd.device_ = std::string("\\\\.\\") + drive.c_str();
    usbd.volume_label_ = volume_label;
  }

  for (size_t i = 0; i < devices_out.size(); ++i) {
    UsbStorageDevice& usbd = devices_out[i];

    // TODO: fill in bus/level/port with this:
    // http://www.codeproject.com/system/EnumDeviceProperties.asp

    // find volume size
    DWORD sectors_per_cluster;
    DWORD bytes_per_sector;
    DWORD number_of_free_clusters;
    DWORD total_number_of_clusters;
    if (GetDiskFreeSpace(
            usbd.os_mount_dir_.c_str(), &sectors_per_cluster, &bytes_per_sector,
            &number_of_free_clusters, &total_number_of_clusters)) {
      usbd.volume_size_mb_ = std::round(
          total_number_of_clusters * (float)sectors_per_cluster *
          bytes_per_sector / (1024 * 1024));
    }
  }
}

bool MemoryCardDriverThreaded_Windows::USBStorageDevicesChanged() {
  return ::GetLogicalDrives() != last_logical_drives_;
}

bool MemoryCardDriverThreaded_Windows::Mount(UsbStorageDevice* device) {
  // nothing to do here...
  return true;
}

void MemoryCardDriverThreaded_Windows::Unmount(UsbStorageDevice* device) {
  /* Try to flush the device before returning.  This requires administrator
   * priviliges. */
  HANDLE h_device = CreateFile(
      device->device_.c_str(), GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL, nullptr);

  if (h_device == INVALID_HANDLE_VALUE) {
    LOG->Warn(werr_ssprintf(
                  GetLastError(),
                  "Couldn't open memory card device to flush (%s): CreateFile",
                  device->device_.c_str())
                  .c_str());
    return;
  }

  if (!FlushFileBuffers(h_device)) {
    LOG->Warn(werr_ssprintf(
                  GetLastError(),
                  "Couldn't flush memory card device (%s): FlushFileBuffers",
                  device->device_.c_str())
                  .c_str());
  }
  CloseHandle(h_device);
}

/*
 * (c) 2003-2004 Chris Danford
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
