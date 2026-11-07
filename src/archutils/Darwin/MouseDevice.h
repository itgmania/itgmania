#ifndef MOUSE_DEVICE_H
#define MOUSE_DEVICE_H

#include <unordered_map>
#include <vector>

#include "HIDDevice.h"
#include "RageInputDevice.h"
#include "RageTimer.h"

struct Mouse {
  InputDevice id_;
  IOHIDElementCookie x_axis_, y_axis_, z_axis_;
  int x_min_, x_max_;
  int y_min_, y_max_;
  int z_min_, z_max_;

  Mouse();
};

class MouseDevice : public HIDDevice {
 private:
  std::unordered_map<IOHIDElementCookie, DeviceButton> mapping_;
  Mouse mouse_;

 protected:
  bool AddLogicalDevice(int usage_page, int usage);
  void AddElement(
      int usage_page, int usage, IOHIDElementCookie cookie,
      const CFDictionaryRef properties);
  void Open();

  // just in case -aj
  Mouse GetMouse() { return mouse_; }

 public:
  void GetButtonPresses(
      std::vector<DeviceInput>& presses, IOHIDElementCookie cookie, int value,
      const RageTimer& now) const;
  void GetDevicesAndDescriptions(std::vector<InputDeviceInfo>& devices) const;
};

#endif

/*
 * (c) 2011 AJ Kelly
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
