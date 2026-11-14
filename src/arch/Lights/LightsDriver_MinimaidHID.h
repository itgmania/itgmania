/* LightsDriver_MinimaidHID: Control lights minimaid using hidapi */

#ifndef LightsDriver_MinimaidHID_H
#define LightsDriver_MinimaidHID_H

/*
 * -------------------------- NOTE --------------------------
 *
 * This driver needs user read/write access to the minimaid board.
 * This can be achieved by using a udev rule like this:
 *
 * SUBSYSTEMS=="usb", ATTRS{idVendor}=="beef", ATTRS{idProduct}=="5730",
 * OWNER="dance", GROUP="dance", MODE="0660"
 *
 * or
 *
 * KERNEL=="hidraw*", ATTRS{idVendor}=="beef", ATTRS{idProduct}=="5730",
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

// static information about the device in question.
#define MM_VID 0xbeef
#define MM_PID 0x5730

// input isn't used here, we are using the keyboard on the firmware
// we are only talking to the lighting interface.
#define MM_INPUT_INTERFACE_NUM 0
#define MM_LIGHTS_INTERFACE_NUM 1

#define MM_REPORT_ID 0

#define MM_OUTPUTREPORT_SIZE 9

#pragma pack(push, 1)

typedef union {
  struct {
    // byte 0
    bool b0_unused0_ : 1;    // bit 0
    bool b0_unused1_ : 1;    // bit 1
    bool p1_menu_ : 1;       // bit 2
    bool p2_menu_ : 1;       // bit 3
    bool mar_p2_lower_ : 1;  // bit 4
    bool mar_p2_upper_ : 1;  // bit 5
    bool mar_p1_lower_ : 1;  // bit 6
    bool mar_p1_upper_ : 1;  // bit 7

    // byte 1
    bool p1_up_ : 1;       // bit 8
    bool p1_down_ : 1;     // bit 9
    bool p1_left_ : 1;     // bit 10
    bool p1_right_ : 1;    // bit 11
    bool p1_pad_en_ : 1;   // bit 12
    bool b1_unused5_ : 1;  // bit 13
    bool b1_unused6_ : 1;  // bit 14
    bool b1_unused7_ : 1;  // bit 15

    // byte 2
    bool p2_up_ : 1;       // bit 16
    bool p2_down_ : 1;     // bit 17
    bool p2_left_ : 1;     // bit 18
    bool p2_right_ : 1;    // bit 19
    bool p2_pad_en_ : 1;   // bit 20
    bool b2_unused5_ : 1;  // bit 21
    bool b2_unused6_ : 1;  // bit 22
    bool b2_unused7_ : 1;  // bit 23

    // byte 3
    bool neons_ : 1;       // bit 24
    bool b3_unused1_ : 1;  // bit 25
    bool b3_unused2_ : 1;  // bit 26
    bool b3_unused3_ : 1;  // bit 27
    bool b3_unused4_ : 1;  // bit 28
    bool b3_unused5_ : 1;  // bit 29
    bool b3_unused6_ : 1;  // bit 30
    bool b3_unused7_ : 1;  // bit 31
  };
  uint32_t raw_;
} mm_lighting_state_ddr_t;

typedef union {
  struct {
    uint8_t report_id_;
    uint8_t extout_;
    mm_lighting_state_ddr_t lights_;
    uint8_t blueled_;
    uint8_t kb_enable_;
    uint8_t aux_flags_;
  };
  uint8_t raw_[MM_OUTPUTREPORT_SIZE];
} mm_output_report_t;

#pragma pack(pop)

// ensure the sizes are correct
static_assert(
    sizeof(mm_output_report_t) == MM_OUTPUTREPORT_SIZE,
    "mm_output_report_t != MM_OUTPUTREPORT_SIZE");

// ensure that we are making a 32bit word for the lights
static_assert(
    sizeof(mm_lighting_state_ddr_t) == 4, "mm_lighting_state_ddr_t != 4");

class LightsDriver_MinimaidHID : public LightsDriver {
 private:
  HidDevice dev_;

  mm_lighting_state_ddr_t prev_lights_;

  void sendReport(mm_lighting_state_ddr_t lights);

 public:
  LightsDriver_MinimaidHID();
  virtual ~LightsDriver_MinimaidHID();

  static bool ShouldAutoLoad();

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
