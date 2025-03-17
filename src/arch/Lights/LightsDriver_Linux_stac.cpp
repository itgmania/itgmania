#include "global.h"
#include "LightsDriver_Linux_stac.h"
#include "GameState.h"
#include "Game.h"
#include "RageLog.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>

#if defined(HAVE_UNISTD_H)
#include <unistd.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>

#if defined(HAVE_FCNTL_H)
#include <fcntl.h>
#endif

#include <libudev.h>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <sys/ioctl.h>

REGISTER_LIGHTS_DRIVER_CLASS2(stac, Linux_stac);

LightsDriver_Linux_stac::LightsDriver_Linux_stac()
{
	memset(outputBuffer[GameController_1], 0x00, STAC_HIDREPORT_SIZE);
	memset(outputBuffer[GameController_2], 0x00, STAC_HIDREPORT_SIZE);

	// Open the device using the VID, PID,
	// and optionally the Serial number.
	handleP1 = hid_open(STAC_VID, STAC_PID_P1, NULL);
	handleP2 = hid_open(STAC_VID, STAC_PID_P2, NULL);
	
	if (!handleP1) {
		LOG->Warn("Stac P1 device not found.");
		hid_exit();
		return;
	}
}

LightsDriver_Linux_stac::~LightsDriver_Linux_stac()
{
	if(handleP1)
		hid_close(handleP1);
	if (handleP2)
		hid_close(handleP2);

	// Finalize the hidapi library
	hid_exit();
}

void LightsDriver_Linux_stac::SetBuffer(int index, bool lightState, GameController ctrlNum)
{
	//the first byte is the report ID, so we offset it here to adjust.
	uint8_t index_offset = index + 1;

	//each index in the array represents a single light,
	//the light will turn on for any value that isn't 0x00
	uint8_t val = lightState ? 0xFF : 0x00;

	//ensure the index is valid and the light value has changed.
	if (index_offset < STAC_HIDREPORT_SIZE && outputBuffer[ctrlNum][index_offset] != val)
	{
		outputBuffer[ctrlNum][index_offset] = val;

		//signal the loop to push the new buffer to the device.
		stateChanged = true;
	}
}

void LightsDriver_Linux_stac::HandleState(const LightsState *ls, GameController ctrlNum)
{
	if (!handle[ctrlNum])
		return;

    //check to see which game we are running as it can change during gameplay.
    const InputScheme *pInput = &GAMESTATE->GetCurrentGame()->m_InputScheme;
    RString sInputName = pInput->m_szName;

    if (sInputName.EqualsNoCase("dance"))
    {
		SetBuffer(STAC_LIGHTINDEX_BTN1, ls->m_bGameButtonLights[ctrlNum][DANCE_BUTTON_UP], ctrlNum);
		SetBuffer(STAC_LIGHTINDEX_BTN2, ls->m_bGameButtonLights[ctrlNum][DANCE_BUTTON_DOWN], ctrlNum);
		SetBuffer(STAC_LIGHTINDEX_BTN3, ls->m_bGameButtonLights[ctrlNum][DANCE_BUTTON_LEFT], ctrlNum);
		SetBuffer(STAC_LIGHTINDEX_BTN4, ls->m_bGameButtonLights[ctrlNum][DANCE_BUTTON_RIGHT], ctrlNum);
	}
	else if (sInputName.EqualsNoCase("pump"))
	{
		SetBuffer(STAC_LIGHTINDEX_BTN1, ls->m_bGameButtonLights[ctrlNum][PUMP_BUTTON_UPLEFT], ctrlNum);
		SetBuffer(STAC_LIGHTINDEX_BTN2, ls->m_bGameButtonLights[ctrlNum][PUMP_BUTTON_UPRIGHT], ctrlNum);
		SetBuffer(STAC_LIGHTINDEX_BTN3, ls->m_bGameButtonLights[ctrlNum][PUMP_BUTTON_CENTER], ctrlNum);
		SetBuffer(STAC_LIGHTINDEX_BTN4, ls->m_bGameButtonLights[ctrlNum][PUMP_BUTTON_DOWNLEFT], ctrlNum);
		SetBuffer(STAC_LIGHTINDEX_BTN5, ls->m_bGameButtonLights[ctrlNum][PUMP_BUTTON_DOWNRIGHT], ctrlNum);
    }

	if (!stateChanged)
		return;

	hid_write(handle[ctrlNum], (unsigned char*)&outputBuffer[ctrlNum], STAC_HIDREPORT_SIZE);
	stateChanged = false;
}

void LightsDriver_Linux_stac::Set(const LightsState *ls)
{
	HandleState(ls, GameController_1);
	HandleState(ls, GameController_2);
}
