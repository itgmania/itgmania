#include "MemoryCardDriverThreaded_Linux.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "RageLog.h"
#include "RageTimer.h"
#include "RageUtil.h"
#include "StdString.h"
#include "arch/MemoryCard/MemoryCardDriver.h"
#include "global.h"

bool MemoryCardDriverThreaded_Linux::TestWrite(UsbStorageDevice* device) {
  if (access(device->os_mount_dir_.c_str(), W_OK) == -1) {
    device->SetError("TestFailed");
    return false;
  }

  return true;
}

static bool ExecuteCommand(const std::string& command) {
  LOG->Trace("executing '%s'", command.c_str());
  int ret = system(command.c_str());
  LOG->Trace("done executing '%s'", command.c_str());
  if (ret != 0) {
    std::string error =
        ssprintf("failed to execute '%s' with error %d", command.c_str(), ret);
    if (ret == -1) {
      error += ssprintf(": %s", command.c_str());
    }
    LOG->Warn("%s", error.c_str());
  }
  return ret == 0;
}

static bool ReadFile(const std::string& path, std::string& s_buf) {
  s_buf.clear();

  int fd = open(path.c_str(), O_RDONLY);
  if (fd == -1) {
    // "No such file or directory" is understandable
    if (errno != ENOENT) {
      LOG->Warn("Error opening \"%s\": %s", path.c_str(), strerror(errno));
    }
    return false;
  }

  while (1) {
    char buf[1024];
    int got = read(fd, buf, sizeof(buf));
    if (got == -1) {
      close(fd);
      LOG->Warn("Error reading \"%s\": %s", path.c_str(), strerror(errno));
      return false;
    }

    s_buf.append(buf, got);
    if (got < (int)sizeof(buf)) {
      break;
    }
  }

  close(fd);
  return true;
}

static void GetFileList(
    const std::string& path, std::vector<std::string>& out) {
  out.clear();

  DIR* dp = opendir(path.c_str());
  if (dp == nullptr) {
    return;  // false; // XXX warn
  }

  while (const struct dirent* ent = readdir(dp)) {
    out.push_back(ent->d_name);
  }

  closedir(dp);
}

bool MemoryCardDriverThreaded_Linux::USBStorageDevicesChanged() {
  std::string this_devices;

  /* If a device is removed and reinserted, the inode of the /sys/block entry
   * will change. */
  std::string device_path = "/sys/block/";

  std::vector<std::string> devices;
  GetFileList(device_path, devices);

  for (unsigned i = 0; i < devices.size(); ++i) {
    struct stat buf;
    if (stat((device_path + devices[i]).c_str(), &buf) == -1) {
      continue;  // XXX warn
    }

    this_devices += ssprintf("%i,", (int)buf.st_ino);
  }

  bool changed = this_devices != last_devices_;
  last_devices_ = this_devices;
  if (changed) {
    LOG->Trace("Change in USB storage devices detected.");
  }
  return changed;
}

void MemoryCardDriverThreaded_Linux::GetUSBStorageDevices(
    std::vector<UsbStorageDevice>& devices_out) {
  LOG->Trace("GetUSBStorageDevices");

  devices_out.clear();

  {
    std::vector<std::string> devices;
    std::string block_device_path = "/sys/block/";
    GetFileList(block_device_path, devices);

    for (unsigned i = 0; i < devices.size(); ++i) {
      const std::string& device = devices[i];
      if (device == "." || device == "..") {
        continue;
      }

      UsbStorageDevice usbd;

      std::string path = block_device_path + device + "/";
      usbd.sys_path_ = path;

      /* Ignore non-removable devices. */
      std::string buf;
      if (!ReadFile(path + "removable", buf)) {
        continue;  // already warned
      }
      if (atoi(buf.c_str()) != 1) {
        continue;
      }

      /*
       * The kernel isn't exposing all of /sys atomically, so we end up missing
       * the partition due to it not being shown yet.  It won't show up until
       * the kernel has scanned the partition table, which can take a variable
       * amount of time, sometimes over a second.  Watch for the "queue" sysfs
       * directory, which is created after this, to tell when partition
       * directories are created.
       */
      RageTimer wait_until;
      wait_until += 5;
      std::string queue_file_path = usbd.sys_path_ + "queue";
      while (1) {
        if (wait_until.Ago() >= 0) {
          LOG->Warn("Timed out waiting for %s", queue_file_path.c_str());
          break;
        }

        if (access(usbd.sys_path_.c_str(), F_OK) == -1) {
          LOG->Warn(
              "Block directory %s went away while we were waiting for %s",
              usbd.sys_path_.c_str(), queue_file_path.c_str());
          break;
        }

        if (access(queue_file_path.c_str(), F_OK) != -1) {
          break;
        }

        usleep(10000);
      }

      /* Wait for udev to finish handling device node creation */
      ExecuteCommand("udevadm settle");

      /* If the first partition device exists, eg. /sys/block/uba/uba1, use it.
       */
      if (access((usbd.sys_path_ + device + "1").c_str(), F_OK) != -1) {
        LOG->Trace("OK");
        usbd.device_ = "/dev/" + device + "1";
      } else {
        LOG->Trace("error %s", strerror(errno));
        usbd.device_ = "/dev/" + device;
      }

      /*
       * sPath/device should be a symlink to the actual device.  For USB
       * devices, it looks like this:
       *
       * device -> ../../devices/pci0000:00/0000:00:02.1/usb2/2-1/2-1:1.0
       *
       * "2-1" is "bus-port".
       */
      char link[256];
      ssize_t ret = readlink((path + "device").c_str(), link, sizeof(link));
      if (ret == -1) {
        LOG->Warn(
            "readlink(\"%s\"): %s", (path + "device").c_str(), strerror(errno));
      } else {
        /*
         * The full path looks like
         *
         *   ../../devices/pci0000:00/0000:00:02.1/usb2/2-2/2-2.1/2-2.1:1.0
         *
         * In newer kernels, it looks like:
         *
         * ../../../3-2.1:1.0
         *
         * Each path element refers to a new hop in the chain.
         *  "usb2" = second USB host
         *  2-            second USB host,
         *   -2           port 1 on the host,
         *     .1         port 1 on an attached hub
         *       .2       ... port 2 on the next hub ...
         *
         * We want the bus number and the port of the last hop.  The level is
         * the number of hops.
         */
        link[ret] = 0;
        std::vector<std::string> bits;
        split(link, "/", bits);

        std::string host_port = bits[bits.size() - 1];
        if (!host_port.empty()) {
          /* Strip off the endpoint information after the colon. */
          size_t pos = host_port.find(':');
          if (pos != std::string::npos) {
            host_port.erase(pos);
          }

          /* sHostPort is eg. 2-2.1. */
          Replace(host_port, "-", ".");
          bits.clear();
          split(host_port, ".", bits);
          if (bits.size() > 1) {
            usbd.bus_ = atoi(bits[0].c_str());
            usbd.port_ = atoi(bits[bits.size() - 1].c_str());
            usbd.level_ = bits.size() - 1;
          }
        }
      }

      if (ReadFile(path + "device/../idVendor", buf)) {
        sscanf(buf.c_str(), "%x", &usbd.id_vendor_);
      }

      if (ReadFile(path + "device/../idProduct", buf)) {
        sscanf(buf.c_str(), "%x", &usbd.id_product_);
      }

      if (ReadFile(path + "device/../serial", buf)) {
        usbd.serial_ = buf;
        TrimRight(usbd.serial_);
      }
      if (ReadFile(path + "device/../product", buf)) {
        usbd.product_ = buf;
        TrimRight(usbd.product_);
      }
      if (ReadFile(path + "device/../manufacturer", buf)) {
        usbd.vendor_ = buf;
        TrimRight(usbd.vendor_);
      }

      devices_out.push_back(usbd);
    }
  }

  {
    // Find where each device is mounted. Output looks like:

    // /dev/sda1               /mnt/flash1             auto    noauto,owner 0 0
    // /dev/sdb1               /mnt/flash2             auto    noauto,owner 0 0
    // /dev/sdc1               /mnt/flash3             auto    noauto,owner 0 0

    std::ifstream f("/etc/fstab");
    if (f.fail()) {
      LOG->Warn("can't open '/etc/fstab': %s", strerror(errno));
      return;
    }

    std::string line;
    while (!f.eof()) {
      std::getline(f, line);
      if (f.eof()) {
        continue;
      } else if (f.fail()) {
        LOG->Warn("error reading '/etc/fstab': %s", strerror(errno));
        return;
      }

      char scsi_device[1024];
      char sz_mount_point[1024];
      int ret = sscanf(line.c_str(), "%s %s", scsi_device, sz_mount_point);
      if (ret != 2 || scsi_device[0] == '#') {
        continue;  // don't process this line
      }

      /* Get the real kernel device name, which should match
       * the name from /sys/block, by following symlinks in
       * /dev.  This allows us to specify persistent names in
       * /etc/fstab using things like /dev/device/by-path. */
      char underlying_device[PATH_MAX];
      if (realpath(scsi_device, underlying_device) == nullptr) {
        // "No such file or directory" is understandable
        if (errno != ENOENT) {
          LOG->Warn("realpath(\"%s\"): %s", scsi_device, strerror(errno));
        }
        continue;
      }

      std::string s_mount_point = sz_mount_point;
      TrimLeft(s_mount_point);
      TrimRight(s_mount_point);

      // search for the mountpoint corresponding to the device
      for (unsigned i = 0; i < devices_out.size(); i++) {
        UsbStorageDevice& usbd = devices_out[i];
        if (usbd.device_ == underlying_device)  // found our match
        {
          // Use the device entry from fstab so the mount command works
          usbd.device_ = scsi_device;
          usbd.os_mount_dir_ = s_mount_point;
          break;  // stop looking for a match
        }
      }
    }
  }

  for (unsigned i = 0; i < devices_out.size(); i++) {
    UsbStorageDevice& usbd = devices_out[i];
    LOG->Trace(
        "    sDevice: %s, iBus: %d, iLevel: %d, iPort: %d, id: %04X:%04X, "
        "Vendor: '%s', Product: '%s', sSerial: \"%s\", sOsMountDir: %s",
        usbd.device_.c_str(), usbd.bus_, usbd.level_, usbd.port_,
        usbd.id_vendor_, usbd.id_product_, usbd.vendor_.c_str(),
        usbd.product_.c_str(), usbd.serial_.c_str(),
        usbd.os_mount_dir_.c_str());
  }

  /* Remove any devices that we couldn't find a mountpoint for. */
  for (unsigned i = 0; i < devices_out.size(); i++) {
    UsbStorageDevice& usbd = devices_out[i];
    if (usbd.os_mount_dir_.empty()) {
      LOG->Trace(
          "Ignoring %s (couldn't find in /etc/fstab)", usbd.device_.c_str());

      devices_out.erase(devices_out.begin() + i);
      --i;
    }
  }

  LOG->Trace("Done with GetUSBStorageDevices");
}

bool MemoryCardDriverThreaded_Linux::Mount(UsbStorageDevice* device) {
  ASSERT(!device->device_.empty());

  std::string command = "mount " + device->device_;
  bool mounted_successfully = ExecuteCommand(command);

  return mounted_successfully;
}

void MemoryCardDriverThreaded_Linux::Unmount(UsbStorageDevice* device) {
  if (device->device_.empty()) {
    return;
  }

  /* Use umount -l, so we unmount the device even if it's in use.  Open
   * files remain usable, and the device (eg. /dev/sda) won't be reused
   * by new devices until those are closed.  Without this, if something
   * causes the device to not unmount here, we'll never unmount it; that
   * causes a device name leak, eventually running us out of mountpoints. */
  std::string command = "sync; umount -l \"" + device->device_ + "\"";
  ExecuteCommand(command);
}

/*
 * (c) 2003-2005 Chris Danford, Glenn Maynard
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
