/* LightsDriver_snek: Control lights for the snek board by icedragon.io using hidapi */

#ifndef LightsDriver_snek_H
#define LightsDriver_snek_H

/*
 * -------------------------- NOTE --------------------------
 *
 * This driver needs user read/write access to the icedragon.io snek board.
 * This can be achieved by using a udev rule like this:
 *
 * SUBSYSTEMS=="usb", ATTRS{idVendor}=="2e8a", ATTRS{idProduct}=="10a8", OWNER="dance", GROUP="dance", MODE="0660"
 *
 * or
 *
 * KERNEL=="hidraw*", ATTRS{idVendor}=="2e8a", ATTRS{idProduct}=="10a8", OWNER="dance", GROUP="dance", MODE="0660"
 *
 * Refer to your distribution's documentation on how to properly apply a udev rule.
 *
 * -------------------------- NOTE --------------------------
 */

#include "arch/Lights/LightsDriver.h"

#include <cstdint>
#include "archutils/Common/HidDevice.h"

// static information about the device in question.
#define SNEK_VID 0x2e8a
#define SNEK_PID 0x10a8
#define SNEK_NUMOF_LIGHTS 32

// the first byte of the buffer is a static report id.
#define SNEK_HIDREPORT_SIZE (SNEK_NUMOF_LIGHTS + 1)
#define SNEK_REPORT_ID 0x01
#define SNEK_LIGHTING_INTERFACENUM 0x02

// all indicies contain their respective 573 pinouts
enum SnekLightIndex
{
	SNEK_LIGHTINDEX_CN10_FL1 = 0,
	SNEK_LIGHTINDEX_CN10_FL2,
	SNEK_LIGHTINDEX_CN10_FL3,
	SNEK_LIGHTINDEX_CN10_FL4,
	SNEK_LIGHTINDEX_CN10_FL5,
	SNEK_LIGHTINDEX_CN10_FL6,
	SNEK_LIGHTINDEX_CN10_FL7,
	SNEK_LIGHTINDEX_CN10_FL8,

	SNEK_LIGHTINDEX_CN13_FL1,
	SNEK_LIGHTINDEX_CN13_FL2,
	SNEK_LIGHTINDEX_CN13_FL3,
	SNEK_LIGHTINDEX_CN13_FL4,

	SNEK_LIGHTINDEX_CN14_FL1,
	SNEK_LIGHTINDEX_CN14_FL2,
	SNEK_LIGHTINDEX_CN14_FL3,
	SNEK_LIGHTINDEX_CN14_FL4,

	SNEK_LIGHTINDEX_CN12_FL1,
	SNEK_LIGHTINDEX_CN12_FL2,
	SNEK_LIGHTINDEX_CN12_FL3,
	SNEK_LIGHTINDEX_CN12_FL4,
	SNEK_LIGHTINDEX_CN12_FL5,
	SNEK_LIGHTINDEX_CN12_FL6,
	SNEK_LIGHTINDEX_CN12_FL7,
	SNEK_LIGHTINDEX_CN12_FL8,

	SNEK_LIGHTINDEX_CN11_FL1,
	SNEK_LIGHTINDEX_CN11_FL2,
	SNEK_LIGHTINDEX_CN11_FL3,
	SNEK_LIGHTINDEX_CN11_FL4,
	SNEK_LIGHTINDEX_CN11_FL5,
	SNEK_LIGHTINDEX_CN11_FL6,
	SNEK_LIGHTINDEX_CN11_FL7,
	SNEK_LIGHTINDEX_CN11_FL8,

	SNEK_LIGHTINDEX_MAX
};

// helper defines for dance mode.
#define SNEK_INDEX_DANCE_P1_START SNEK_LIGHTINDEX_CN10_FL3
#define SNEK_INDEX_DANCE_P2_START SNEK_LIGHTINDEX_CN10_FL4

#define SNEK_INDEX_DANCE_M_LR SNEK_LIGHTINDEX_CN10_FL5
#define SNEK_INDEX_DANCE_M_UR SNEK_LIGHTINDEX_CN10_FL6
#define SNEK_INDEX_DANCE_M_LL SNEK_LIGHTINDEX_CN10_FL7
#define SNEK_INDEX_DANCE_M_UL SNEK_LIGHTINDEX_CN10_FL8

#define SNEK_INDEX_DANCE_NEON SNEK_LIGHTINDEX_CN13_FL1

#define SNEK_INDEX_DANCE_P1_UP SNEK_LIGHTINDEX_CN12_FL1
#define SNEK_INDEX_DANCE_P1_DOWN SNEK_LIGHTINDEX_CN12_FL2
#define SNEK_INDEX_DANCE_P1_LEFT SNEK_LIGHTINDEX_CN12_FL3
#define SNEK_INDEX_DANCE_P1_RIGHT SNEK_LIGHTINDEX_CN12_FL4

#define SNEK_INDEX_DANCE_P2_UP SNEK_LIGHTINDEX_CN11_FL1
#define SNEK_INDEX_DANCE_P2_DOWN SNEK_LIGHTINDEX_CN11_FL2
#define SNEK_INDEX_DANCE_P2_LEFT SNEK_LIGHTINDEX_CN11_FL3
#define SNEK_INDEX_DANCE_P2_RIGHT SNEK_LIGHTINDEX_CN11_FL4

class LightsDriver_snek : public LightsDriver
{
private:
	HidDevice dev;

	bool stateChanged = false;
	uint8_t outputBuffer[SNEK_HIDREPORT_SIZE];

	void SetBuffer(int index, bool lightState);

public:
	LightsDriver_snek();
	virtual ~LightsDriver_snek();

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
