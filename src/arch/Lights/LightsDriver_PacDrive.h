/* LightsDriver_PacDrive: Control lights for the PacDrive by Ultimarc using hidapi */

#ifndef LightsDriver_PacDrive_H
#define LightsDriver_PacDrive_H

/*
 * -------------------------- NOTE --------------------------
 *
 * This driver needs user read/write access to PacDrive.
 * This can be achieved by using a udev rule like this:
 *
 * SUBSYSTEMS=="usb", ATTRS{idVendor}=="D209", ATTRS{idProduct}=="150[0-9]", OWNER="dance", GROUP="dance", MODE="0660"
 *
 * or
 *
 * KERNEL=="hidraw*", ATTRS{idVendor}=="D209", ATTRS{idProduct}=="150[0-9]", OWNER="dance", GROUP="dance", MODE="0660"
 *
 * Refer to your distribution's documentation on how to properly apply a udev rule.
 *
 * -------------------------- NOTE --------------------------
 */

#include "arch/Lights/LightsDriver.h"

#include <cstdint>
#include "archutils/Common/HidDevice.h"

// static information about the device in question.
#define PACDRIVE_VID 0xD209

// PacDrive PIDs range from 0x1500->0x1507
#define PACDRIVE_PID 0x1500
#define PACDRIVE_PID_MAX 8

#define PACDRIVE_INTERFACE 0

// the first byte of the buffer is a static report id.
// and I have no idea why ultimarc's report is 5 bytes wide...
#define PACDRIVE_HIDREPORT_SIZE 5
#define PACDRIVE_HIDREPORT_ID 0x00

#pragma pack(push, 1)

typedef union
{
	struct
	{
		bool led01 : 1;
		bool led02 : 1;
		bool led03 : 1;
		bool led04 : 1;
		bool led05 : 1;
		bool led06 : 1;
		bool led07 : 1;
		bool led08 : 1;
		bool led09 : 1;
		bool led10 : 1;
		bool led11 : 1;
		bool led12 : 1;
		bool led13 : 1;
		bool led14 : 1;
		bool led15 : 1;
		bool led16 : 1;
	};
	uint16_t raw;
} pacdrive_leds_t;

#pragma pack(pop)

typedef union
{
	struct
	{
		uint8_t report_id;
		uint8_t pad0;
		uint8_t pad1;
		pacdrive_leds_t leds;
	};
	uint8_t raw_state[PACDRIVE_HIDREPORT_SIZE];
} pacdrive_state_t;

class LightsDriver_PacDrive : public LightsDriver
{
private:
	HidDevice dev;

	pacdrive_state_t state;
	pacdrive_leds_t prev_led_state;

public:
	LightsDriver_PacDrive();
	virtual ~LightsDriver_PacDrive();

	virtual void Set(const LightsState *ls);
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
