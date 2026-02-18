/* LightsDriver_fusion: Control lights for the fusion board by icedragon.io
 * using hidapi */

#ifndef LightsDriver_fusion_H
#define LightsDriver_fusion_H

/*
 * -------------------------- NOTE --------------------------
 *
 * This driver needs user read/write access to the icedragon.io fusion board.
 * This can be achieved by using a udev rule like this:
 *
 * SUBSYSTEMS=="usb", ATTRS{idVendor}=="0547", ATTRS{idProduct}=="1337",
 * OWNER="dance", GROUP="dance", MODE="0660"
 *
 * or
 *
 * KERNEL=="hidraw*", ATTRS{idVendor}=="0547", ATTRS{idProduct}=="1337",
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

#define FUSION_VID 0x0547
#define FUSION_PID 0x1337

#define FUSION_LIGHTING_REPORTID 0x02

// five player, five player, neon, four marquee, coin, led
#define FUSION_TOTAL_LIGHTS (5 + 5 + 1 + 4 + 1 + 1)

// number of lights plus report id.
#define FUSION_HID_LIGHTS_REPORT_SIZE (1 + FUSION_TOTAL_LIGHTS)

enum FusionReportIndex {
  FUSION_REPORT_ID = 0,

  FUSION_P1_UL,
  FUSION_P1_UR,
  FUSION_P1_CN,
  FUSION_P1_LL,
  FUSION_P1_LR,

  FUSION_P2_UL,
  FUSION_P2_UR,
  FUSION_P2_CN,
  FUSION_P2_LL,
  FUSION_P2_LR,

  FUSION_NEON,

  FUSION_MAR_UL,
  FUSION_MAR_UR,
  FUSION_MAR_LL,
  FUSION_MAR_LR,

  FUSION_COIN_PULSE,
  FUSION_LED,

  FUISON_REPORT_MAX
};

static_assert(
    FUISON_REPORT_MAX == FUSION_HID_LIGHTS_REPORT_SIZE,
    "Incorrect FusionReportIndex");

class LightsDriver_fusion : public LightsDriver {
 private:
  HidDevice dev;

  uint8_t outputBuffer[FUISON_REPORT_MAX];
  uint8_t prevOutputBuffer[FUISON_REPORT_MAX];

 public:
  LightsDriver_fusion();
  virtual ~LightsDriver_fusion();

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
