/* LightsDriver_stac2: Control lights for the stac2 by icedragon.io using hidapi
 */

#ifndef LightsDriver_stac2_H
#define LightsDriver_stac2_H

/*
 * -------------------------- NOTE --------------------------
 *
 * This driver needs user read/write access to the icedragon.io stac2.
 * This can be achieved by using a udev rule like this:
 *
 * (player 1 then player 2)
 * SUBSYSTEMS=="usb", ATTRS{idVendor}=="02ea", ATTRS{idProduct}=="10d9",
 * OWNER="dance", GROUP="dance", MODE="0660" SUBSYSTEMS=="usb",
 * ATTRS{idVendor}=="02ea", ATTRS{idProduct}=="10e9", OWNER="dance",
 * GROUP="dance", MODE="0660"
 *
 *  or
 *
 * KERNEL=="hidraw*", ATTRS{idVendor}=="02ea", ATTRS{idProduct}=="10d9",
 * OWNER="dance", GROUP="dance", MODE="0660" KERNEL=="hidraw*",
 * ATTRS{idVendor}=="02ea", ATTRS{idProduct}=="10e9", OWNER="dance",
 * GROUP="dance", MODE="0660"
 *
 * Refer to your distribution's documentation on how to properly apply a udev
 * rule.
 *
 * -------------------------- NOTE --------------------------
 */

#include <cstdint>

#include "GameInput.h"
#include "arch/Lights/LightsDriver.h"
#include "archutils/Common/HidDevice.h"

// static information about the device(s) in question.
#define STAC2_VID 0x2E8A
#define STAC2_PID_P1 0x10D9
#define STAC2_PID_P2 0x10E9
#define STAC2_NUMOF_LIGHTS 8

// the first byte of the buffer is a static report id.
#define STAC2_HIDREPORT_SIZE (STAC2_NUMOF_LIGHTS + 1)
#define STAC2_REPORT_ID 0x01
#define STAC2_LIGHTING_INTERFACE 0x02

// total number of supported devices.
#define STAC2_MAX_NUMBER 2

enum StacLightIndex {
  STAC2_LIGHTINDEX_BTN1 = 0,
  STAC2_LIGHTINDEX_BTN2 = 1,
  STAC2_LIGHTINDEX_BTN3 = 2,
  STAC2_LIGHTINDEX_BTN4 = 3,
  STAC2_LIGHTINDEX_BTN5 = 4,
  STAC2_LIGHTINDEX_BTN6 = 5,
  STAC2_LIGHTINDEX_BTN7 = 6,
  STAC2_LIGHTINDEX_BTN8 = 7,
  STAC2_LIGHTINDEX_MAX
};

class LightsDriver_stac2 : public LightsDriver {
 private:
  HidDevice devs[STAC2_MAX_NUMBER];

  bool stateChanged[STAC2_MAX_NUMBER];
  uint8_t outputBuffer[STAC2_MAX_NUMBER][STAC2_HIDREPORT_SIZE];

  void HandleState(const LightsState* ls, GameController ctrlNum);
  void SetBuffer(int index, bool lightState, GameController ctrlNum);

 public:
  LightsDriver_stac2();
  virtual ~LightsDriver_stac2();

  virtual void Set(const LightsState* ls);
};

#endif

/*
 * (c) 2025 din
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
