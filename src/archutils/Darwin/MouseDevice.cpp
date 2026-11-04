#include "MouseDevice.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

#include "InputFilter.h"
#include "RageInputDevice.h"
#include "RageLog.h"
#include "RageTimer.h"
#include "RageUtil.h"

Mouse::Mouse()
    : id_(InputDevice_Invalid),
      x_axis_(0),
      y_axis_(0),
      z_axis_(0),
      x_min_(0),
      x_max_(0),
      y_min_(0),
      y_max_(0),
      z_min_(0),
      z_max_(0) {}

bool MouseDevice::AddLogicalDevice(int usagePage, int usage) {
  if (usagePage != kHIDPage_GenericDesktop) {
    return false;
  }

  switch (usage) {
    // Mice can either be kHIDUsage_GD_Mouse or kHIDUsage_GD_Pointer.
    case kHIDUsage_GD_Mouse:
    case kHIDUsage_GD_Pointer:
      break;
    default:
      return false;
  }
  // Init only a single mouse for now.
  mouse_ = Mouse();
  return true;
}

void MouseDevice::AddElement(
    int usagePage, int usage, IOHIDElementCookie cookie,
    const CFDictionaryRef properties) {
  if (usagePage >= kHIDPage_VendorDefinedStart) {
    return;
  }

  Mouse& m = mouse_;

  switch (usagePage) {
    case kHIDPage_GenericDesktop: {  // ease scope
      int iMin = 0;
      IntValue(
          CFDictionaryGetValue(properties, CFSTR(kIOHIDElementMinKey)), iMin);
      int iMax = 0;
      IntValue(
          CFDictionaryGetValue(properties, CFSTR(kIOHIDElementMaxKey)), iMax);

      // based on usage
      switch (usage) {
        case kHIDUsage_GD_X:
          m.x_axis_ = cookie;
          m.x_min_ = iMin;
          m.x_max_ = iMax;
          break;
        case kHIDUsage_GD_Y:
          m.y_axis_ = cookie;
          m.y_min_ = iMin;
          m.y_max_ = iMax;
          break;
        case kHIDUsage_GD_Z:
        case kHIDUsage_GD_Wheel:  // also handle wheel
          m.z_axis_ = cookie;
          m.z_min_ = iMin;
          m.z_max_ = iMax;
          break;
        default:
          // LOG->Warn( "Unknown usagePage usage pair: (kHIDPage_GenericDesktop,
          // %d).", usage );
          break;
      }
    } break;
    case kHIDPage_Button: {
      const DeviceButton buttonID =
          enum_add2(MOUSE_LEFT, usage - kHIDUsage_Button_1);

      if (buttonID <= MOUSE_MIDDLE) {
        mapping_[cookie] = buttonID;
      } else {
        LOG->Warn("Button id too large: %d.", int(buttonID));
      }
      break;
    } break;
    default:
      // LOG->Warn( "Unknown usagePage usage pair: (%d, %d).", usagePage, usage
      // );
      break;
  }
}

void MouseDevice::Open() {
  const Mouse& m = mouse_;
#define ADD(x) \
  if (m.x) AddElementToQueue(m.x)
  ADD(x_axis_);
  ADD(y_axis_);
  ADD(z_axis_);
#undef ADD
  for (std::unordered_map<IOHIDElementCookie, DeviceButton>::const_iterator i =
           mapping_.begin();
       i != mapping_.end(); ++i) {
    AddElementToQueue(i->first);
  }
}

void MouseDevice::GetButtonPresses(
    std::vector<DeviceInput>& vPresses, IOHIDElementCookie cookie, int value,
    const RageTimer& now) const {
  // todo: add mouse axis stuff -aj
  const Mouse& m = mouse_;
  HRESULT result;
  IOHIDEventStruct hidEvent;
  // result =
  // (*interface_)->getElementValue(hidDeviceInterface,cookie,&hidEvent);

  if (m.x_axis_ == cookie) {
    // INPUTFILTER->UpdateCursorLocation(value,m.y_axis_);
    LOG->Trace("Mouse X: Value = %i", value);
  } else if (m.y_axis_ == cookie) {
    // INPUTFILTER->UpdateCursorLocation(m.x_axis_,value);
    LOG->Trace("Mouse Y: Value = %i", value);
  } else if (m.z_axis_ == cookie) {
    float level = SCALE(value, m.z_min_, m.z_max_, -1.0f, 1.0f);
    INPUTFILTER->ButtonPressed(
        DeviceInput(DEVICE_MOUSE, MOUSE_WHEELUP, std::max(-level, 0.0f), now));
    INPUTFILTER->ButtonPressed(DeviceInput(
        DEVICE_MOUSE, MOUSE_WHEELDOWN, std::max(+level, 0.0f), now));
  } else {
    std::unordered_map<IOHIDElementCookie, DeviceButton>::const_iterator iter =
        mapping_.find(cookie);
    if (iter != mapping_.end()) {
      vPresses.push_back(DeviceInput(DEVICE_MOUSE, iter->second, value, now));
    }
  }
}

void MouseDevice::GetDevicesAndDescriptions(
    std::vector<InputDeviceInfo>& vDevices) const {
  vDevices.push_back(InputDeviceInfo(DEVICE_MOUSE, "Mouse"));
}
