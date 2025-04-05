#include "global.h"
#include "RageLog.h"
#include "LightsDriver_stac.h"
#include "GameState.h"
#include "Game.h"

REGISTER_LIGHTS_DRIVER_CLASS(stac);

LightsDriver_stac::LightsDriver_stac() :
	devs{ {STAC_VID , STAC_PID_P1, STAC_LIGHTING_INTERFACE}, {STAC_VID , STAC_PID_P2, STAC_LIGHTING_INTERFACE } },
	stateChanged{ false },
	outputBuffer{ {0} }
{
}

LightsDriver_stac::~LightsDriver_stac()
{
}

void LightsDriver_stac::SetBuffer(int index, bool lightState, GameController ctrlNum)
{
	// the first byte is the report ID, so we offset it here to adjust.
	uint8_t index_offset = index + 1;

	// each index in the array represents a single light,
	// the light will turn on for any value that isn't 0x00
	uint8_t val = lightState ? 0xFF : 0x00;

	// ensure report ID is set.
	outputBuffer[ctrlNum][0] = STAC_REPORT_ID;

	// ensure the index is valid and the light value has changed.
	if (index_offset < STAC_HIDREPORT_SIZE && outputBuffer[ctrlNum][index_offset] != val)
	{
		outputBuffer[ctrlNum][index_offset] = val;

		// signal the loop to push the new buffer to the device.
		stateChanged[ctrlNum] = true;
	}
}

void LightsDriver_stac::HandleState(const LightsState *ls, GameController ctrlNum)
{
	// do not create a message for an disconnected device.
	if (!devs[ctrlNum].FoundOnce())
		return;

	// check to see which game we are running as it can change during gameplay.
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

	// only push changes.
	if (stateChanged[ctrlNum])
	{
		devs[ctrlNum].Write((unsigned char *)&outputBuffer[ctrlNum], STAC_HIDREPORT_SIZE);
		stateChanged[ctrlNum] = false;
	}
}

void LightsDriver_stac::Set(const LightsState *ls)
{
	HandleState(ls, GameController_1);
	HandleState(ls, GameController_2);
}
