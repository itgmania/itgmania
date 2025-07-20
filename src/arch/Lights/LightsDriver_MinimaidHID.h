/* LightsDriver_MinimaidHID: Control lights minimaid using hidapi */

#ifndef LightsDriver_MinimaidHID_H
#define LightsDriver_MinimaidHID_H

/*
 * -------------------------- NOTE --------------------------
 *
 * This driver needs user read/write access to the minimaid board.
 * This can be achieved by using a udev rule like this:
 *
 * SUBSYSTEMS=="usb", ATTRS{idVendor}=="beef", ATTRS{idProduct}=="10a8",
 * OWNER="dance", GROUP="dance", MODE="5730"
 *
 * or
 *
 * KERNEL=="hidraw*", ATTRS{idVendor}=="beef", ATTRS{idProduct}=="10a8",
 * OWNER="dance", GROUP="dance", MODE="5730"
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

#define MM_INTERFACE_NUM 0
#define MM_REPORT_ID 0

#define MM_OUTPUTREPORT_SIZE 9

#pragma pack(push, 1)

typedef union {
  struct {
    // byte 0
    uint8_t b0_unused0 : 1;    // bit 0
    uint8_t b0_unused1 : 1;    // bit 0
    uint8_t p1_menu : 1;       // bit 2
    uint8_t p2_menu : 1;       // bit 3
    uint8_t mar_p2_lower : 1;  // bit 4
    uint8_t mar_p2_upper : 1;  // bit 5
    uint8_t mar_p1_lower : 1;  // bit 6
    uint8_t mar_p1_upper : 1;  // bit 7

    // byte 1
    uint8_t p1_up : 1;       // bit 8
    uint8_t p1_down : 1;     // bit 9
    uint8_t p1_left : 1;     // bit 10
    uint8_t p1_right : 1;    // bit 11
    uint8_t p1_pad_en : 1;   // bit 12
    uint8_t b1_unused5 : 1;  // bit 13
    uint8_t b1_unused6 : 1;  // bit 14
    uint8_t b1_unused7 : 1;  // bit 15

    // byte 2
    uint8_t p2_up : 1;       // bit 16
    uint8_t p2_down : 1;     // bit 17
    uint8_t p2_left : 1;     // bit 18
    uint8_t p2_right : 1;    // bit 19
    uint8_t p2_pad_en : 1;   // bit 20
    uint8_t b2_unused5 : 1;  // bit 21
    uint8_t b2_unused6 : 1;  // bit 22
    uint8_t b2_unused7 : 1;  // bit 23

    // byte 3
    uint8_t neons : 1;    // bit 24
    uint8_t unused3 : 7;  // bits 25–31
  };
  uint32_t raw;
} mm_lighting_state_ddr_t;

typedef union {
  struct {
    uint8_t report_id;
    uint8_t extout;

    mm_lighting_state_ddr_t lights;

    uint8_t blueled;
    uint8_t kb_enable;
    uint8_t aux_flags;
  };
  uint8_t raw[MM_OUTPUTREPORT_SIZE];
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
  HidDevice dev;

  mm_output_report_t outputReport;

 public:
  LightsDriver_MinimaidHID();
  virtual ~LightsDriver_MinimaidHID();

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
