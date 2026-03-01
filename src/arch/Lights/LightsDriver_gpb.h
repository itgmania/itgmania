/* LightsDriver_gpb: Control lights for the gpb board by icedragon.io
 * using hidapi */

#ifndef LightsDriver_GPB_H
#define LightsDriver_GPB_H

/*
 * -------------------------- NOTE --------------------------
 *
 * This driver needs user read/write access to the icedragon.io gpb board.
 * This can be achieved by using a udev rule like this:
 *
 * SUBSYSTEMS=="usb", ATTRS{idVendor}=="04d8", ATTRS{idProduct}=="ea6a",
 * OWNER="dance", GROUP="dance", MODE="0660"
 *
 * or
 *
 * KERNEL=="hidraw*", ATTRS{idVendor}=="04d8", ATTRS{idProduct}=="ea6a",
 * OWNER="dance", GROUP="dance", MODE="0660"
 *
 * Refer to your distribution's documentation on how to properly apply a udev
 * rule.
 *
 * -------------------------- NOTE --------------------------
 */

#include <cstdint>

#include "arch/Lights/LightsDriver.h"
#include "archutils/Common/HidDevice.h"

#define GPB_VID 0x04d8
#define GPB_PID 0xea6a

#define GPB_LIGHTING_REPORTID 0x01
#define GPB_LIGHTING_INTERFACENUM 0x01
#define GPB_TOTAL_LIGHTS 16

// number of lights plus report id.
#define GPB_HID_LIGHTS_REPORT_SIZE (1 + GPB_TOTAL_LIGHTS)

enum GpbLightIndex {
  GPB_LIGHTINDEX_REPORT_ID = 0,

  GPB_LIGHTINDEX_BTN01,
  GPB_LIGHTINDEX_BTN02,
  GPB_LIGHTINDEX_BTN03,
  GPB_LIGHTINDEX_BTN04,
  GPB_LIGHTINDEX_BTN05,
  GPB_LIGHTINDEX_BTN06,
  GPB_LIGHTINDEX_BTN07,
  GPB_LIGHTINDEX_BTN08,
  GPB_LIGHTINDEX_BTN09,
  GPB_LIGHTINDEX_BTN10,
  GPB_LIGHTINDEX_BTN11,
  GPB_LIGHTINDEX_BTN12,
  GPB_LIGHTINDEX_BTN13,
  GPB_LIGHTINDEX_BTN14,
  GPB_LIGHTINDEX_BTN15,
  GPB_LIGHTINDEX_BTN16,

  GPB_LIGHTINDEX_MAX
};

static_assert(
    GPB_LIGHTINDEX_MAX == GPB_HID_LIGHTS_REPORT_SIZE,
    "Incorrect GpbLightIndex");

class LightsDriver_gpb : public LightsDriver {
 private:
  HidDevice dev;

  uint8_t outputBuffer[GPB_LIGHTINDEX_MAX];
  uint8_t prevOutputBuffer[GPB_LIGHTINDEX_MAX];

 public:
  LightsDriver_gpb();
  virtual ~LightsDriver_gpb();

  virtual void Set(const LightsState* ls);
};

#endif

/*
 * (c) 2026 din
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
