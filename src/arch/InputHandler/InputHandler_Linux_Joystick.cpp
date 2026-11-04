#include "InputHandler_Linux_Joystick.h"

#include <fcntl.h>
#include <linux/joystick.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <set>
#include <string>
#include <vector>

#include "LinuxInputManager.h"
#include "RageInputDevice.h"  // NUM_JOYSTICKS
#include "RageLog.h"
#include "RageUtil.h"
#include "global.h"

REGISTER_INPUT_HANDLER_CLASS2(LinuxJoystick, Linux_Joystick);

InputHandler_Linux_Joystick::InputHandler_Linux_Joystick() {
  devices_changed_ = false;

  LOG->Trace("InputHandler_Linux_Joystick::InputHandler_Linux_Joystick");

  if (LINUXINPUT == nullptr) {
    LINUXINPUT = new LinuxInputManager;
  }
  LINUXINPUT->InitDriver(this);

  if (!files_.empty()) {  // LinuxInputManager found at least one valid
                          // joystick for us
    StartThread();
  }
}

InputHandler_Linux_Joystick::~InputHandler_Linux_Joystick() {
  if (input_thread_.IsCreated()) {
    StopThread();
  }

  for (auto& f : files_) {
    if (f.fd_ != -1) {
      close(f.fd_);
    }
  }
  files_.clear();
}

void InputHandler_Linux_Joystick::StartThread() {
  shutdown_ = false;
  input_thread_.SetName("Joystick thread");
  input_thread_.Create(InputThread_Start, this);
}

void InputHandler_Linux_Joystick::StopThread() {
  shutdown_ = true;
  LOG->Trace("Shutting down joystick thread ...");
  input_thread_.Wait();
  LOG->Trace("Joystick thread shut down.");
}

bool InputHandler_Linux_Joystick::TryDevice(std::string dev) {
  struct stat st;
  if (stat(dev.c_str(), &st) == -1) {
    LOG->Warn(
        "LinuxJoystick: Couldn't stat %s: %s", dev.c_str(), strerror(errno));
    return false;
  }

  if (!S_ISCHR(st.st_mode)) {
    LOG->Warn(
        "LinuxJoystick: Ignoring %s: not a character device", dev.c_str());
    return false;
  }

  bool ret = false;
  bool hotplug = false;
  if (input_thread_.IsCreated()) {
    StopThread();
    hotplug = true;
  }
  /* Thread is stopped! DO NOT RETURN */
  {
    FileDescriptor f;
    f.fd_ = open(dev.c_str(), O_RDONLY);
    if (f.fd_ != -1) {
      char szName[1024];
      ZERO(szName);
      if (ioctl(f.fd_, JSIOCGNAME(sizeof(szName)), szName) < 0) {
        f.description_ = ssprintf("Unknown joystick at %s", dev.c_str());
      } else {
        f.description_ = szName;
      }

      LOG->Info("LinuxJoystick: Opened %s", dev.c_str());
      devices_changed_ = true;
      ret = true;

      files_.push_back(f);
    } else {
      LOG->Warn(
          "LinuxJoystick: Failed to open %s: %s", dev.c_str(), strerror(errno));
    }
  }
  if (hotplug) {
    StartThread();
  }

  return ret;
}

int InputHandler_Linux_Joystick::InputThread_Start(void* p) {
  ((InputHandler_Linux_Joystick*)p)->InputThread();
  return 0;
}

void InputHandler_Linux_Joystick::InputThread() {
  while (!shutdown_) {
    fd_set fdset;
    FD_ZERO(&fdset);
    int max_fd = -1;

    for (size_t i = 0; i < files_.size(); ++i) {
      if (files_[i].fd_ < 0) {
        continue;
      }

      FD_SET(files_[i].fd_, &fdset);
      max_fd = std::max(max_fd, files_[i].fd_);
    }

    if (max_fd == -1) {
      break;
    }

    struct timeval zero = {0, 100000};
    if (select(max_fd + 1, &fdset, nullptr, nullptr, &zero) <= 0) {
      continue;
    }
    // js_event.time is jiffies_to_msecs(jiffies): a 32-bit counter on the
    // kernel's jiffies clock, whose epoch differs from RageTimer's
    // CLOCK_MONOTONIC and can't be anchored from userspace. Use poll time.
    RageTimer now;

    for (size_t i = 0; i < files_.size(); ++i) {
      if (files_[i].fd_ == -1) {
        continue;
      }

      if (!FD_ISSET(files_[i].fd_, &fdset)) {
        continue;
      }

      js_event event;
      int ret = read(files_[i].fd_, &event, sizeof(event));

      if (ret == -1) {
        LOG->Warn(
            "Error reading from joystick %zu: %s; disabled", i,
            strerror(errno));
        close(files_[i].fd_);
        files_[i].fd_ = -1;
        continue;
      }

      if (ret != sizeof(event)) {
        LOG->Warn(
            "Unexpected packet (size %i != %i) from joystick %zu; disabled",
            ret, (int)sizeof(event), i);
        close(files_[i].fd_);
        files_[i].fd_ = -1;
        continue;
      }

      InputDevice id = InputDevice(DEVICE_JOY1 + i);

      event.type &= ~JS_EVENT_INIT;
      switch (event.type) {
        case JS_EVENT_BUTTON: {
          int iNum = event.number;
          // In 2.6.11 using an EMS USB2, the event number for P1 Tri (the first
          // button) is being reported as 32 instead of 0.  Correct for this.
          wrap(
              iNum,
              32);  // max number of joystick buttons.  Make this a constant?
          ButtonPressed(
              DeviceInput(id, enum_add2(JOY_BUTTON_1, iNum), event.value, now));
          break;
        }

        case JS_EVENT_AXIS: {
          DeviceButton neg = enum_add2(JOY_LEFT, 2 * event.number);
          DeviceButton pos = enum_add2(JOY_RIGHT, 2 * event.number);
          float l = SCALE(int(event.value), 0.0f, 32767, 0.0f, 1.0f);
          ButtonPressed(DeviceInput(id, neg, std::max(-l, 0.0f), now));
          ButtonPressed(DeviceInput(id, pos, std::max(+l, 0.0f), now));
          break;
        }

        default:
          LOG->Warn(
              "Unexpected packet (type %i) from joystick %zu; disabled",
              event.type, i);
          close(files_[i].fd_);
          files_[i].fd_ = -1;
          continue;
      }
    }
  }

  InputHandler::UpdateTimer();
}

void InputHandler_Linux_Joystick::GetDevicesAndDescriptions(
    std::vector<InputDeviceInfo>& vDevicesOut) {
  // HACK: If IH_Linux_Joystick is constructed before IH_Linux_Event, our thread
  // won't be started as part of the constructor. This isn't called until all
  // InputHandlers have been constructed, and is (hopefully) in the same thread
  // as TryDevice... so doublecheck our thread now.
  if (files_.empty() && !input_thread_.IsCreated()) {
    StartThread();
  }

  for (size_t i = 0; i < files_.size(); ++i) {
    if (files_[i].fd_ < 0) {
      continue;
    }

    vDevicesOut.push_back(
        InputDeviceInfo(InputDevice(DEVICE_JOY1 + i), files_[i].description_));
  }
  devices_changed_ = false;
}

/*
 * (c) 2003-2004 Glenn Maynard
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
