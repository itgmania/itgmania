#include "LinuxInputManager.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <string>
#include <vector>

#include "InputHandler_Linux_Event.h"
#include "InputHandler_Linux_Joystick.h"
#include "RageInput.h"  // g_sInputDrivers g_sInputDeviceOrder
#include "RageLog.h"

// Our DeviceButton KEY_* conflicts with linux/input.h KEY_*
// clang-format off
#include <linux/input.h>
// clang-format on

std::string getDevice(std::string inputDir, std::string type) {
  std::string result = "";
  DIR* dir = opendir(inputDir.c_str());
  if (dir == nullptr) {
    LOG->Warn(
        "LinuxInputManager: Couldn't open %s: %s.", inputDir.c_str(),
        strerror(errno));
    return "";
  }

  struct dirent* d;
  while ((d = readdir(dir)) != nullptr) {
    if (strncmp(type.c_str(), d->d_name, type.size()) == 0) {
      result = std::string("/dev/input/") + d->d_name;
      break;
    }
  }

  closedir(dir);
  return result;
}

LinuxInputManager::LinuxInputManager() {
  event_enabled_ =
      g_sInputDrivers.Get().find("LinuxEvent") != std::string::npos;
  joystick_enabled_ =
      g_sInputDrivers.Get().find("LinuxJoystick") != std::string::npos;
  // HACK: If empty, assume both are enabled
  if (g_sInputDrivers.Get() == "") {
    event_enabled_ = true;
    joystick_enabled_ = true;
  }

  joystick_driver_ = nullptr;

  // XXX: Can I use RageFile for this?
  DIR* sysClassInput = opendir("/sys/class/input");
  if (sysClassInput == nullptr) {
    // XXX: Probably should throw a Dialog. But Linux doesn't have a
    // DialogDriver yet so eh.
    LOG->Warn(
        "Couldn't open /sys/class/input: %s. Joysticks will not work!",
        strerror(errno));
    return;
  }

  struct dirent* d;
  while ((d = readdir(sysClassInput)) != nullptr) {
    if (strncmp("input", d->d_name, 5) != 0) {
      continue;
    }

    std::string dName = std::string("/sys/class/input/") + d->d_name;

    bool bEventPresent = getDevice(dName, "event") != "";
    if (event_enabled_ && bEventPresent) {
      pending_event_devices_.push_back(dName);
      continue;
    }

    bool bJoystickPresent = getDevice(dName, "js") != "";
    if (joystick_enabled_ && bJoystickPresent) {
      pending_joystick_devices_.push_back(dName);
      continue;
    }

    if (!bEventPresent && !bJoystickPresent) {
      LOG->Info(
          "LinuxInputManager: %s seems to have no eventNN or jsNN.",
          dName.c_str());
    }
  }

  // use Presort to sort the devices by unique location (like USB port/hub
  // number.)
  PresortPhysical(pending_event_devices_, "event");
  PresortPhysical(pending_joystick_devices_, "js");

  closedir(sysClassInput);
}

LinuxInputManager::~LinuxInputManager() {}

static bool presort_cmpDevices(LinuxInputSort a, LinuxInputSort b) {
  return a.unique_string_ < b.unique_string_;
}

void LinuxInputManager::PresortPhysical(
    std::vector<std::string>& sortingArray, std::string sortBy) {
  pre_sort_.clear();

  for (std::string& dev : sortingArray) {
    LinuxInputSort entry;

    entry.device_name_ = dev;
    entry.unique_string_ = "";

    int m_iFD = open(getDevice(dev, sortBy.c_str()).c_str(), O_RDWR);

    if (m_iFD >= 0) {
      char szLocation[1024];

      // EVIOCGPHYS can return values like "usb-0000:0d:00.3-4"
      // telling us where the device is located physically on the computer.
      if (ioctl(m_iFD, EVIOCGPHYS(sizeof(szLocation)), szLocation) != -1) {
        entry.unique_string_ += szLocation;
      }

      close(m_iFD);
    }

    // in the event we didn't get a unique location from ioctl
    // default to the name so that sorting by number still works as intended.
    entry.unique_string_ += entry.device_name_;

    pre_sort_.push_back(entry);
  }

  std::sort(pre_sort_.begin(), pre_sort_.end(), presort_cmpDevices);

  sortingArray.clear();

  for (LinuxInputSort& dev : pre_sort_) {
    // LOG->Info("%s -> %s", dev.DeviceName.c_str(), dev.UniqueString.c_str());
    sortingArray.push_back(dev.device_name_);
  }
}

void LinuxInputManager::InitDriver(InputHandler_Linux_Event* driver) {
  for (std::string& dev : pending_event_devices_) {
    std::string devFile = getDevice(dev, "event");
    ASSERT(devFile != "");

    if (!driver->TryDevice(devFile) && joystick_enabled_ &&
        getDevice(dev, "js") != "") {
      pending_joystick_devices_.push_back(dev);
    }
  }
  if (joystick_driver_ != nullptr) {
    InitDriver(joystick_driver_);
  }

  pending_event_devices_.clear();
}

void LinuxInputManager::InitDriver(InputHandler_Linux_Joystick* driver) {
  joystick_driver_ = driver;
  // Discard all the joystick devices if they were assigned manually via
  // 	InputDeviceOrder
  if (g_sInputDeviceOrder.Get() != "") {
    pending_joystick_devices_.clear();
  }

  for (std::string& dev : pending_joystick_devices_) {
    std::string devFile = getDevice(dev, "js");
    ASSERT(devFile != "");

    driver->TryDevice(devFile);
  }

  // If any, add the manually specified devices via InputDeviceOrder
  std::vector<std::string> fixedDevices;
  split(g_sInputDeviceOrder, ",", fixedDevices, true);

  for (std::string dev : fixedDevices) {
    std::string devFile = dev;
    driver->TryDevice(devFile);
  }
}

LinuxInputManager* LINUXINPUT =
    nullptr;  // global and accessible anywhere in our program

/*
 * (c) 2013 Ben "root" Anderson
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
