#include "InputHandler_Linux_Event.h"

#include <fcntl.h>
#include <libudev.h>
#include <linux/input.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <string>
#include <vector>

#include "GamePreferences.h"  //needed for Axis Fix
#include "LinuxInputManager.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "global.h"

REGISTER_INPUT_HANDLER_CLASS2(LinuxEvent, Linux_Event);

static std::string BustypeToString(int bus) {
  switch (bus) {
      //	case BUS_ADB:
      //	case BUS_AMIGA: return "amiga input";
    case BUS_BLUETOOTH:
      return "Bluetooth";
    case BUS_GAMEPORT:
      return "gameport";
      //	case BUS_HIL:
      //	case BUS_HOST:
      //	case BUS_I2C:
    case BUS_I8042:
      return "keyboard";
    case BUS_ISA:
      return "ISA";
    case BUS_ISAPNP:
      return "ISAPNP";
    case BUS_PARPORT:
      return "parallel port";
    case BUS_PCI:
      return "PCI";
    case BUS_RS232:
      return "serial port";
    case BUS_USB:
      return "USB";
    case BUS_XTKBD:
      return "XT keyboard";
    default:
      return ssprintf("unknown bus %x", bus);
  }
}

struct EventDevice {
  EventDevice();
  ~EventDevice();
  bool Open(std::string file, InputDevice dev);
  bool IsOpen() const { return fd_ != -1; }
  void Close() {
    if (fd_ != -1) {
      close(fd_);
    }
    fd_ = -1;
  }

  int fd_;
  std::string path_;
  std::string name_;
  InputDevice dev_;

  int abs_min_[ABS_MAX];
  int abs_max_[ABS_MAX];
  DeviceButton abs_mapping_high_[ABS_MAX];
  DeviceButton abs_mapping_low_[ABS_MAX];
};

static std::vector<EventDevice*> event_devices;

static bool BitIsSet(const uint8_t* array, uint32_t bit) {
  return !!(array[bit / 8] & (1 << (bit % 8)));
}

EventDevice::EventDevice() { fd_ = -1; }

bool EventDevice::Open(std::string file, InputDevice dev) {
  path_ = file;
  dev_ = dev;
  fd_ = open(file.c_str(), O_RDWR);
  if (fd_ == -1) {
    // HACK: Let the caller handle errno.
    return false;
  }

  unsigned int clk = CLOCK_MONOTONIC;
  if (ioctl(fd_, EVIOCSCLOCKID, &clk) == -1) {
    LOG->Warn(
        "ioctl(EVIOCSCLOCKID, CLOCK_MONOTONIC) failed: %s", strerror(errno));
  }

  static bool logged = false;
  if (!logged) {
    logged = true;
    int version;
    if (ioctl(fd_, EVIOCGVERSION, &version) == -1) {
      LOG->Warn("ioctl(EVIOCGVERSION): %s", strerror(errno));
    } else {
      LOG->Info(
          "Event driver: v%i.%i.%i", (version >> 16) & 0xFF,
          (version >> 8) & 0xFF, version & 0xFF);
    }
  }

  char name[1024];
  if (ioctl(fd_, EVIOCGNAME(sizeof(name)), name) == -1) {
    LOG->Warn("ioctl(EVIOCGNAME): %s", strerror(errno));

    name_ = "(unknown)";
  } else {
    name_ = name;
  }

  input_id dev_info;
  if (ioctl(fd_, EVIOCGID, &dev_info) == -1) {
    LOG->Warn("ioctl(EVIOCGID): %s", strerror(errno));
  } else {
    LOG->Info(
        "Input device: %s: %s device, ID %04x:%04x, version %x: %s",
        file.c_str(), BustypeToString(dev_info.bustype).c_str(),
        dev_info.vendor, dev_info.product, dev_info.version, name_.c_str());
  }

  uint8_t abs_mask[ABS_MAX / 8 + 1];
  memset(abs_mask, 0, sizeof(abs_mask));
  if (ioctl(fd_, EVIOCGBIT(EV_ABS, sizeof(abs_mask)), abs_mask) < 0) {
    LOG->Warn("ioctl(EVIOCGBIT(EV_ABS)): %s", strerror(errno));
  }

  // If more exceptions need to be added, please add them here.
  // The code below will check the device name against this list
  // and apply an exception if a match is found.
  static const char* k_joystick_exceptions[] = {
      "RedOctane USB Pad", "Cobalt Flux Controller"};

  if (!BitIsSet(abs_mask, ABS_X) && !BitIsSet(abs_mask, ABS_THROTTLE) &&
      !BitIsSet(abs_mask, ABS_WHEEL)) {
    LOG->Info("    Not a joystick; ignoring [%s]", name_.c_str());
    bool is_exception = false;
    for (const char* exception : k_joystick_exceptions) {
      if (name_ == exception) {
        is_exception = true;
        break;
      }
    }
    if (is_exception) {
      LOG->Info("%s detected, making an exception.", name_.c_str());
    } else {
      Close();
      return false;
    }
  }

  uint8_t key_mask[KEY_MAX / 8 + 1];
  memset(key_mask, 0, sizeof(key_mask));
  if (ioctl(fd_, EVIOCGBIT(EV_KEY, sizeof(key_mask)), key_mask) < 0) {
    LOG->Warn("ioctl(EVIOCGBIT(EV_KEY)): %s", strerror(errno));
  }

  uint8_t event_types[EV_MAX / 8];
  memset(event_types, 0, sizeof(event_types));
  if (ioctl(fd_, EVIOCGBIT(0, EV_MAX), event_types) == -1) {
    LOG->Warn("ioctl(EV_MAX): %s", strerror(errno));
  }

  {
    std::vector<std::string> set_event_types;

    if (BitIsSet(event_types, EV_SYN)) {
      set_event_types.push_back("syn");
    }
    if (BitIsSet(event_types, EV_KEY)) {
      set_event_types.push_back("key");
    }
    if (BitIsSet(event_types, EV_REL)) {
      set_event_types.push_back("rel");
    }
    if (BitIsSet(event_types, EV_ABS)) {
      set_event_types.push_back("abs");
    }
    if (BitIsSet(event_types, EV_MSC)) {
      set_event_types.push_back("misc");
    }
    if (BitIsSet(event_types, EV_SW)) {
      set_event_types.push_back("sw");
    }
    if (BitIsSet(event_types, EV_LED)) {
      set_event_types.push_back("led");
    }
    if (BitIsSet(event_types, EV_SND)) {
      set_event_types.push_back("snd");
    }
    if (BitIsSet(event_types, EV_REP)) {
      set_event_types.push_back("rep");
    }
    if (BitIsSet(event_types, EV_FF)) {
      set_event_types.push_back("ff");
    }
    if (BitIsSet(event_types, EV_PWR)) {
      set_event_types.push_back("pwr");
    }
    if (BitIsSet(event_types, EV_FF_STATUS)) {
      set_event_types.push_back("ff_status");
    }

    LOG->Info("    Event types: %s", join(", ", set_event_types).c_str());
  }

  int total_keys = 0;
  for (int i = 0; i < KEY_MAX; ++i) {
    if (!BitIsSet(key_mask, i)) {
      continue;
    }
    ++total_keys;
  }

  int total_axes = 0;
  const DeviceButton extra_axes[] = {
      JOY_LEFT_2, JOY_UP_2, JOY_AUX_1, JOY_AUX_3};
  int next_extra_axis = 0;
  for (int i = 0; i < ABS_MAX; ++i) {
    if (!BitIsSet(abs_mask, i)) {
      continue;
    }
    struct input_absinfo absinfo;
    if (ioctl(fd_, EVIOCGABS(i), &absinfo) < 0) {
      LOG->Warn("ioctl(EVIOCGABS): %s", strerror(errno));
      continue;
    }

    // LOG->Info( "    Axis %i: min: %i; max: %i; fuzz: %i; flat: %i",
    //		i, absinfo.minimum, absinfo.maximum, absinfo.fuzz, absinfo.flat
    //);
    abs_min_[i] = absinfo.minimum;
    abs_max_[i] = absinfo.maximum;
    abs_mapping_high_[i] = enum_add2(JOY_RIGHT, 2 * i);
    abs_mapping_low_[i] = enum_add2(JOY_LEFT, 2 * i);

    if (i == ABS_X) {
      abs_mapping_high_[i] = JOY_RIGHT;
      abs_mapping_low_[i] = JOY_LEFT;
    } else if (i == ABS_Y) {
      abs_mapping_high_[i] = JOY_DOWN;
      abs_mapping_low_[i] = JOY_UP;
    } else if (i == ABS_Z) {
      abs_mapping_high_[i] = JOY_Z_DOWN;
      abs_mapping_low_[i] = JOY_Z_UP;
    } else if (i == ABS_RX) {
      abs_mapping_high_[i] = JOY_ROT_RIGHT;
      abs_mapping_low_[i] = JOY_ROT_LEFT;
    } else if (i == ABS_RY) {
      abs_mapping_high_[i] = JOY_ROT_DOWN;
      abs_mapping_low_[i] = JOY_ROT_UP;
    } else if (i == ABS_RZ) {
      abs_mapping_high_[i] = JOY_ROT_Z_DOWN;
      abs_mapping_low_[i] = JOY_ROT_Z_UP;
    } else if (i == ABS_HAT0X) {
      abs_mapping_high_[i] = JOY_HAT_RIGHT;
      abs_mapping_low_[i] = JOY_HAT_LEFT;
    } else if (i == ABS_HAT0Y) {
      abs_mapping_high_[i] = JOY_HAT_UP;
      abs_mapping_low_[i] = JOY_HAT_DOWN;
    } else {
      if (next_extra_axis < (int)ARRAYLEN(extra_axes)) {
        abs_mapping_low_[i] = extra_axes[next_extra_axis];
        abs_mapping_high_[i] = enum_add2(abs_mapping_low_[i], 1);
        ++next_extra_axis;
      }
    }

    ++total_axes;
  }
  LOG->Info("    Total keys: %i; total axes: %i", total_keys, total_axes);

  return true;
}

EventDevice::~EventDevice() { Close(); }

InputHandler_Linux_Event::InputHandler_Linux_Event()
    : input_mutex_("InputHandler_Linux"),
      next_device_(DEVICE_JOY10),
      shutdown_(true),
      devices_changed_(false),
      udev_fd_(-1) {
  if (LINUXINPUT == nullptr) {
    LINUXINPUT = new LinuxInputManager;
  }
  LINUXINPUT->InitDriver(this);

  udev_ = udev_new();
  if (!udev_) {
    LOG->Warn("Failed to create udev object");
  }

  udev_monitor_ = udev_monitor_new_from_netlink(udev_, "udev");
  if (!udev_monitor_) {
    LOG->Warn("Failed to create udev object");
    udev_unref(udev_);
  }

  udev_monitor_filter_add_match_subsystem_devtype(udev_monitor_, "input", NULL);
  udev_monitor_enable_receiving(udev_monitor_);

  udev_fd_ = udev_monitor_get_fd(udev_monitor_);

  // Always start thread, if only to monitor udev events
  StartThread();
}

InputHandler_Linux_Event::~InputHandler_Linux_Event() {
  if (input_thread_.IsCreated()) {
    StopThread();
  }

  for (int i = 0; i < (int)event_devices.size(); ++i) {
    delete event_devices[i];
  }
  event_devices.clear();

  udev_monitor_unref(udev_monitor_);
  udev_unref(udev_);
}

void InputHandler_Linux_Event::StartThread() {
  shutdown_ = false;
  input_thread_.SetName("Event input thread");
  input_thread_.Create(InputThread_Start, this);
}

void InputHandler_Linux_Event::StopThread() {
  shutdown_ = true;
  LOG->Trace("Shutting down joystick thread ...");
  input_thread_.Wait();
  LOG->Trace("Joystick thread shut down.");
}

bool InputHandler_Linux_Event::TryDevice(std::string devfile) {
  EventDevice* dev = new EventDevice;
  if (dev->Open(devfile, next_device_)) {
    bool hotplug = false;
    if (input_thread_.IsCreated()) {
      StopThread();
      hotplug = true;
    }
    /* Thread is stopped! DO NOT RETURN */
    {
      event_devices.push_back(dev);
    }
    if (hotplug) {
      StartThread();
    }

    next_device_ = enum_add2(next_device_, 1);

    input_mutex_.Lock();
    devices_changed_ = true;
    input_mutex_.Unlock();

    return true;
  } else {
    delete dev;
    // This is likely to fail; most systems still forbid ALL eventNN regardless
    // of their type. Info it anyway, it could be useful for end-user
    // troubleshooting.
    LOG->Info(
        "LinuxEvent: Couldn't open %s: %s.", devfile.c_str(), strerror(errno));
    return false;
  }
}

int InputHandler_Linux_Event::InputThread_Start(void* p) {
  ((InputHandler_Linux_Event*)p)->InputThread();
  return 0;
}

void InputHandler_Linux_Event::InputThread() {
  while (!shutdown_) {
    fd_set fdset;
    FD_ZERO(&fdset);
    int max_fd = -1;

    if (udev_fd_ >= 0) {
      FD_SET(udev_fd_, &fdset);
      max_fd = std::max(max_fd, udev_fd_);
    }

    for (int i = 0; i < (int)event_devices.size(); ++i) {
      int fd = event_devices[i]->fd_;
      if (!event_devices[i]->IsOpen()) {
        continue;
      }

      FD_SET(fd, &fdset);
      max_fd = std::max(max_fd, fd);
    }

    if (max_fd == -1) {
      break;
    }

    struct timeval zero = {0, 100000};
    if (select(max_fd + 1, &fdset, nullptr, nullptr, &zero) <= 0) {
      continue;
    }

    if (FD_ISSET(udev_fd_, &fdset)) {
      struct udev_device* device = udev_monitor_receive_device(udev_monitor_);
      if (device) {
        const char* action = udev_device_get_action(device);
        const char* devnode = udev_device_get_devnode(device);

        if (action && devnode && strcmp(action, "add") == 0) {
          // Check if the device is a joystick
          const char* devtype =
              udev_device_get_property_value(device, "ID_INPUT_JOYSTICK");
          if (devtype && strcmp(devtype, "1") == 0) {
            input_mutex_.Lock();
            devices_changed_ = true;
            input_mutex_.Unlock();
            LOG->Info("LinuxEvent: New joystick detected: %s\n", devnode);
          }
        }

        udev_device_unref(device);
      }
    }

    for (int i = 0; i < (int)event_devices.size(); ++i) {
      if (!event_devices[i]->IsOpen()) {
        continue;
      }

      if (!FD_ISSET(event_devices[i]->fd_, &fdset)) {
        continue;
      }

      input_event event;
      int ret = read(event_devices[i]->fd_, &event, sizeof(event));
      if (ret == -1) {
        LOG->Warn(
            "Error reading from %s: %s; disabled",
            event_devices[i]->path_.c_str(), strerror(errno));
        event_devices[i]->Close();
        input_mutex_.Lock();
        devices_changed_ = true;
        input_mutex_.Unlock();
        continue;
      }

      if (ret != sizeof(event)) {
        LOG->Warn(
            "Unexpected packet (size %i != %i) from joystick %i; disabled", ret,
            (int)sizeof(event), i);
        event_devices[i]->Close();
        input_mutex_.Lock();
        devices_changed_ = true;
        input_mutex_.Unlock();
        continue;
      }

      RageTimer event_time(event.time.tv_sec, event.time.tv_usec);

      switch (event.type) {
        case EV_KEY: {
          int num;
          if (event.code >= BTN_JOYSTICK && event.code <= BTN_JOYSTICK + 0xf) {
            // These guys have arbitrary names, but the kernel code in
            // hid-input.c maps exactly 0xf of them.
            num = event.code - BTN_JOYSTICK;
          } else if (
              event.code >= BTN_GAMEPAD && event.code <= BTN_GAMEPAD + 0x0f) {
            num = event.code - BTN_GAMEPAD;
          } else if (
              event.code >= BTN_TRIGGER_HAPPY1 &&
              event.code <= BTN_TRIGGER_HAPPY40) {
            // Actually, we only have 32 buttons defined.
            num = event.code - BTN_TRIGGER_HAPPY1 + 0x10;
          } else if (
              event.code >= BTN_DPAD_UP && event.code <= BTN_DPAD_RIGHT) {
            num = event.code - BTN_DPAD_UP + 0x10;
          } else {
            // If the button number is >40+0xf, it gets mapped to a code with no
            // #define. I don't know if this is appropriate at all, but what
            // else to do?
            num = event.code;
          }
          wrap(
              num,
              32);  // max number of joystick buttons.  Make this a constant?
          ButtonPressed(DeviceInput(
              event_devices[i]->dev_, enum_add2(JOY_BUTTON_1, num),
              event.value != 0, event_time));
          break;
        }

        case EV_ABS: {
          ASSERT_M(event.code < ABS_MAX, ssprintf("%i", event.code));
          DeviceButton neg = event_devices[i]->abs_mapping_low_[event.code];
          DeviceButton pos = event_devices[i]->abs_mapping_high_[event.code];

          float l = SCALE(
              int(event.value), (float)event_devices[i]->abs_min_[event.code],
              (float)event_devices[i]->abs_max_[event.code], -1.0f, 1.0f);
          if (GamePreferences::axis_fix) {
            // Up if between 0.0001 and 0.5 or if less than -0.5
            ButtonPressed(DeviceInput(
                event_devices[i]->dev_, neg,
                (l < -0.5) || ((l > 0.0001) && (l < 0.5)), event_time));
            // Down if between 0.0001 and 0.5 or if more than 0.5
            ButtonPressed(DeviceInput(
                event_devices[i]->dev_, pos,
                (l > 0.5) || ((l > 0.0001) && (l < 0.5)), event_time));
          } else {
            ButtonPressed(DeviceInput(
                event_devices[i]->dev_, neg, std::max(-l, 0.0f), event_time));
            ButtonPressed(DeviceInput(
                event_devices[i]->dev_, pos, std::max(+l, 0.0f), event_time));
          }
          break;
        }
      }
    }
  }

  InputHandler::UpdateTimer();
}

void InputHandler_Linux_Event::GetDevicesAndDescriptions(
    std::vector<InputDeviceInfo>& devices_out) {
  for (unsigned i = 0; i < event_devices.size(); ++i) {
    EventDevice* dev = event_devices[i];
    devices_out.push_back(InputDeviceInfo(dev->dev_, dev->name_));
  }

  input_mutex_.Lock();
  devices_changed_ = false;
  input_mutex_.Unlock();
}

/*
 * (c) 2003-2008 Glenn Maynard
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
