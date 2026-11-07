#include "JoystickDevice.h"

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "RageInputDevice.h"
#include "RageLog.h"
#include "RageTimer.h"
#include "RageUtil.h"
#include "global.h"

Joystick::Joystick()
    : id_(InputDevice_Invalid),
      x_axis_(0),
      y_axis_(0),
      z_axis_(0),
      x_rot_(0),
      y_rot_(0),
      z_rot_(0),
      hat_(0),
      x_min_(0),
      x_max_(0),
      y_min_(0),
      y_max_(0),
      z_min_(0),
      z_max_(0),
      rx_min_(0),
      rx_max_(0),
      ry_min_(0),
      ry_max_(0),
      rz_min_(0),
      rz_max_(0),
      hat_min_(0),
      hat_max_(0) {}

bool JoystickDevice::AddLogicalDevice(int usage_page, int usage) {
  if (usage_page != kHIDPage_GenericDesktop) {
    return false;
  }
  switch (usage) {
    case kHIDUsage_GD_Joystick:
    case kHIDUsage_GD_GamePad:
      break;
    default:
      return false;
  }
  sticks_.push_back(Joystick());
  return true;
}

void JoystickDevice::AddElement(
    int usage_page, int usage, IOHIDElementCookie cookie,
    const CFDictionaryRef properties) {
  if (usage_page >= kHIDPage_VendorDefinedStart) {
    return;
  }

  ASSERT(sticks_.size());
  Joystick& js = sticks_.back();

  switch (usage_page) {
    case kHIDPage_GenericDesktop: {
      int min_val = 0;
      int max_val = 0;

      IntValue(
          CFDictionaryGetValue(properties, CFSTR(kIOHIDElementMinKey)),
          min_val);
      IntValue(
          CFDictionaryGetValue(properties, CFSTR(kIOHIDElementMaxKey)),
          max_val);

      switch (usage) {
        case kHIDUsage_GD_X:
          js.x_axis_ = cookie;
          js.x_min_ = min_val;
          js.x_max_ = max_val;
          break;
        case kHIDUsage_GD_Y:
          js.y_axis_ = cookie;
          js.y_min_ = min_val;
          js.y_max_ = max_val;
          break;
        case kHIDUsage_GD_Z:
          js.z_axis_ = cookie;
          js.z_min_ = min_val;
          js.z_max_ = max_val;
          break;
        case kHIDUsage_GD_Rx:
          js.x_rot_ = cookie;
          js.rx_min_ = min_val;
          js.rx_max_ = max_val;
          break;
        case kHIDUsage_GD_Ry:
          js.y_rot_ = cookie;
          js.ry_min_ = min_val;
          js.ry_max_ = max_val;
          break;
        case kHIDUsage_GD_Rz:
          js.z_rot_ = cookie;
          js.rz_min_ = min_val;
          js.rz_max_ = max_val;
          break;
        case kHIDUsage_GD_DPadUp:
          js.mapping_[cookie] = JOY_UP;
          break;
        case kHIDUsage_GD_DPadDown:
          js.mapping_[cookie] = JOY_DOWN;
          break;
        case kHIDUsage_GD_DPadRight:
          js.mapping_[cookie] = JOY_RIGHT;
          break;
        case kHIDUsage_GD_DPadLeft:
          js.mapping_[cookie] = JOY_LEFT;
          break;
        case kHIDUsage_GD_Hatswitch: {
          if (max_val - min_val != 7 && max_val - min_val != 3) {
            break;
          }
          js.hat_ = cookie;
          js.hat_min_ = min_val;
          js.hat_max_ = max_val;
          break;
        }
        default:
          // LOG->Warn( "Unknown usagePage usage pair: (kHIDPage_GenericDesktop,
          // %d).", usage );
          break;
      }
      break;
    }
    case kHIDPage_Button: {
      const DeviceButton button_id =
          enum_add2(JOY_BUTTON_1, usage - kHIDUsage_Button_1);

      if (button_id <= JOY_BUTTON_32) {
        js.mapping_[cookie] = button_id;
      } else {
        LOG->Warn("Button id too large: %d.", int(button_id));
      }
      break;
    }
    default:
      // LOG->Warn( "Unknown usagePage usage pair: (%d, %d).", usage_page, usage
      // );
      break;
  }  // end switch (usage_page)
}

void JoystickDevice::Open() {
  // Add elements to the queue for each Joystick
  for (const Joystick& js : sticks_) {
#define ADD(x) \
  if (js.x) AddElementToQueue(js.x)
    ADD(x_axis_);
    ADD(y_axis_);
    ADD(z_axis_);
    ADD(x_rot_);
    ADD(y_rot_);
    ADD(z_rot_);
    ADD(hat_);
#undef ADD
    for (std::unordered_map<IOHIDElementCookie, DeviceButton>::const_iterator
             j = js.mapping_.begin();
         j != js.mapping_.end(); ++j) {
      AddElementToQueue(j->first);
    }
  }
}

bool JoystickDevice::InitDevice(int vid, int pid) {
  if (vid != 0x0507 || pid != 0x0011) {
    return true;
  }
  // It's a Para controller, so try to power it on.
  uint8_t power_on = 1;
  IOReturn ret = SetReport(kIOHIDReportTypeFeature, 0, &power_on, 1, 10);

  if (ret) {
    LOG->Warn("Failed to power on the Para controller: %#08x", ret);
  }
  return ret == kIOReturnSuccess;
}

void JoystickDevice::GetButtonPresses(
    std::vector<DeviceInput>& presses, IOHIDElementCookie cookie, int value,
    const RageTimer& now) const {
  for (const Joystick& js : sticks_) {
    if (js.x_axis_ == cookie) {
      float level = SCALE(value, js.x_min_, js.x_max_, -1.0f, 1.0f);

      presses.push_back(
          DeviceInput(js.id_, JOY_LEFT, std::max(-level, 0.0f), now));
      presses.push_back(
          DeviceInput(js.id_, JOY_RIGHT, std::max(level, 0.0f), now));
      break;
    } else if (js.y_axis_ == cookie) {
      float level = SCALE(value, js.y_min_, js.y_max_, -1.0f, 1.0f);

      presses.push_back(
          DeviceInput(js.id_, JOY_UP, std::max(-level, 0.0f), now));
      presses.push_back(
          DeviceInput(js.id_, JOY_DOWN, std::max(level, 0.0f), now));
      break;
    } else if (js.z_axis_ == cookie) {
      float level = SCALE(value, js.z_min_, js.z_max_, -1.0f, 1.0f);

      presses.push_back(
          DeviceInput(js.id_, JOY_Z_UP, std::max(-level, 0.0f), now));
      presses.push_back(
          DeviceInput(js.id_, JOY_Z_DOWN, std::max(level, 0.0f), now));
      break;
    } else if (js.x_rot_ == cookie) {
      float level = SCALE(value, js.rx_min_, js.rx_max_, -1.0f, 1.0f);

      presses.push_back(
          DeviceInput(js.id_, JOY_ROT_LEFT, std::max(-level, 0.0f), now));
      presses.push_back(
          DeviceInput(js.id_, JOY_ROT_RIGHT, std::max(level, 0.0f), now));
      break;
    } else if (js.y_rot_ == cookie) {
      float level = SCALE(value, js.ry_min_, js.ry_max_, -1.0f, 1.0f);

      presses.push_back(
          DeviceInput(js.id_, JOY_ROT_UP, std::max(-level, 0.0f), now));
      presses.push_back(
          DeviceInput(js.id_, JOY_ROT_DOWN, std::max(level, 0.0f), now));
      break;
    } else if (js.z_rot_ == cookie) {
      float level = SCALE(value, js.rz_min_, js.rz_max_, -1.0f, 1.0f);

      presses.push_back(
          DeviceInput(js.id_, JOY_ROT_Z_UP, std::max(-level, 0.0f), now));
      presses.push_back(
          DeviceInput(js.id_, JOY_ROT_Z_DOWN, std::max(level, 0.0f), now));
      break;
    } else if (js.hat_ == cookie) {
      float level_up = 0.f, level_right = 0.f, level_down = 0.f,
            level_left = 0.f;

      value -= js.hat_min_;  // Probably just subtracting 0.
      if (js.hat_max_ - js.hat_min_ == 3) {
        value *= 2;
      }
      switch (value) {
        case 0:
          level_up = 1.f;
          break;  // U
        case 1:
          level_up = 1.f;
          level_right = 1.f;
          break;  // UR
        case 2:
          level_right = 1.f;
          break;  // R
        case 3:
          level_down = 1.f;
          level_right = 1.f;
          break;  // DR
        case 4:
          level_down = 1.f;
          break;  // D
        case 5:
          level_down = 1.f;
          level_left = 1.f;
          break;  // DL
        case 6:
          level_left = 1.f;
          break;  // L
        case 7:
          level_up = 1.f;
          level_left = 1.f;
          break;  // UL
      }
      presses.push_back(DeviceInput(js.id_, JOY_HAT_UP, level_up, now));
      presses.push_back(DeviceInput(js.id_, JOY_HAT_RIGHT, level_right, now));
      presses.push_back(DeviceInput(js.id_, JOY_HAT_DOWN, level_down, now));
      presses.push_back(DeviceInput(js.id_, JOY_HAT_LEFT, level_left, now));
      break;
    } else {
      // hash_map<T,U>::operator[] is not const
      std::unordered_map<IOHIDElementCookie, DeviceButton>::const_iterator iter;

      iter = js.mapping_.find(cookie);
      if (iter != js.mapping_.end()) {
        presses.push_back(DeviceInput(js.id_, iter->second, value, now));
        break;
      }
    }
  }
}

int JoystickDevice::AssignIDs(InputDevice start_id) {
  if (!IsJoystick(start_id)) {
    return -1;
  }
  for (auto i = sticks_.begin(); i != sticks_.end(); ++i) {
    if (!IsJoystick(start_id)) {
      sticks_.erase(i, sticks_.end());
      break;
    }
    i->id_ = start_id;
    enum_add(start_id, 1);
  }
  return sticks_.size();
}

void JoystickDevice::GetDevicesAndDescriptions(
    std::vector<InputDeviceInfo>& devices) const {
  for (auto& i : sticks_) {
    devices.push_back(InputDeviceInfo(i.id_, GetDescription()));
  }
}

/*
 * (c) 2005-2007 Steve Checkoway
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
