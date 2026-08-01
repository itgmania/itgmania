/* LightsDriver_brokeio: Control lights for the brokeio board (in gamepad mode)
 * using hidapi */

#ifndef LightsDriver_brokeio_H
#define LightsDriver_brokeio_H

/*
 * -------------------------- NOTE --------------------------
 *
 * This driver needs user read/write access to the brokeio board.
 * This can be achieved by using a udev rule like this:
 *
 * SUBSYSTEMS=="usb", ATTRS{idVendor}=="1D50", ATTRS{idProduct}=="6181",
 * OWNER="dance", GROUP="dance", MODE="0660"
 *
 * or
 *
 * KERNEL=="hidraw*", ATTRS{idVendor}=="1D50", ATTRS{idProduct}=="6181",
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

#define BROKEIO_VID 0x1D50
#define BROKEIO_PID 0x6181

#define BROKEIO_LIGHTING_REPORTID 0x00

#define BROKEIO_TOTAL_LIGHTS (5 + 5 + 1 + 4 + 1)
#define BROKEIO_HID_LIGHTS_REPORT_SIZE (1 + BROKEIO_TOTAL_LIGHTS)

enum BrokeIOReportIndex {
  BROKEIO_REPORT_ID = 0,

  BROKEIO_P1_UL,
  BROKEIO_P1_UR,
  BROKEIO_P1_CN,
  BROKEIO_P1_LL,
  BROKEIO_P1_LR,

  BROKEIO_P2_UL,
  BROKEIO_P2_UR,
  BROKEIO_P2_CN,
  BROKEIO_P2_LL,
  BROKEIO_P2_LR,

  BROKEIO_NEON,

  BROKEIO_MAR_UL,
  BROKEIO_MAR_UR,
  BROKEIO_MAR_LL,
  BROKEIO_MAR_LR,

  BROKEIO_COIN_PULSE,
  
  BROKEIO_REPORT_MAX
};

static_assert(
    BROKEIO_REPORT_MAX == BROKEIO_HID_LIGHTS_REPORT_SIZE,
    "Incorrect BrokeIOReportIndex");

class LightsDriver_brokeio : public LightsDriver {
 private:
  HidDevice dev;
  uint8_t outputBuffer[BROKEIO_TOTAL_LIGHTS];
  uint8_t prevOutputBuffer[BROKEIO_TOTAL_LIGHTS];

 public:
  LightsDriver_brokeio();
  virtual ~LightsDriver_brokeio();

  virtual void Set(const LightsState* ls);
};

#endif

/*
 * (c) 2026 navaroli
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
