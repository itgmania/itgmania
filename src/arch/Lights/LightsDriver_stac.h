/* LightsDriver_stac: Control lights for the stac by icedragon.io using hidapi */

#ifndef LightsDriver_stac_H
#define LightsDriver_stac_H

/*
 * -------------------------- NOTE --------------------------
 *
 * This driver needs user read/write access to the icedragon.io stac.
 * This can be achieved by using a udev rule like this:
 *
 * (player 1 then player 2)
 * SUBSYSTEMS=="usb", ATTRS{idVendor}=="04d8", ATTRS{idProduct}=="eb5b", OWNER="dance", GROUP="dance", MODE="0660"
 * SUBSYSTEMS=="usb", ATTRS{idVendor}=="04d8", ATTRS{idProduct}=="eb5a", OWNER="dance", GROUP="dance", MODE="0660"
 *
 *  or
 *
 * KERNEL=="hidraw*", ATTRS{idVendor}=="04d8", ATTRS{idProduct}=="eb5b", OWNER="dance", GROUP="dance", MODE="0660"
 * KERNEL=="hidraw*", ATTRS{idVendor}=="04d8", ATTRS{idProduct}=="eb5a", OWNER="dance", GROUP="dance", MODE="0660"
 *
 * Refer to your distribution's documentation on how to properly apply a udev rule.
 *
 * -------------------------- NOTE --------------------------
 */

#include "arch/Lights/LightsDriver.h"

#include <cstdint>
#include "archutils/Common/HidDevice.h"

// static information about the device(s) in question.
#define STAC_VID 0x04d8
#define STAC_PID_P1 0xea4b
#define STAC_PID_P2 0xea4a
#define STAC_NUMOF_LIGHTS 5

// the first byte of the buffer is a static report id.
#define STAC_HIDREPORT_SIZE (STAC_NUMOF_LIGHTS + 1)
#define STAC_REPORT_ID 0x01
#define STAC_LIGHTING_INTERFACE 0x01

// total number of supported devices.
#define STAC_MAX_NUMBER 2

enum StacLightIndex
{
	STAC_LIGHTINDEX_BTN1 = 0,
	STAC_LIGHTINDEX_BTN2 = 1,
	STAC_LIGHTINDEX_BTN3 = 2,
	STAC_LIGHTINDEX_BTN4 = 3,
	STAC_LIGHTINDEX_BTN5 = 4,
	STAC_LIGHTINDEX_MAX
};

class LightsDriver_stac : public LightsDriver
{
private:
	HidDevice devs[STAC_MAX_NUMBER];

	bool stateChanged[STAC_MAX_NUMBER];
	uint8_t outputBuffer[STAC_MAX_NUMBER][STAC_HIDREPORT_SIZE];

	void HandleState(const LightsState *ls, GameController ctrlNum);
	void SetBuffer(int index, bool lightState, GameController ctrlNum);

public:
	LightsDriver_stac();
	virtual ~LightsDriver_stac();

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
