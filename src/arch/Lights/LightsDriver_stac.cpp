#include "global.h"
#include "RageLog.h"
#include "LightsDriver_stac.h"
#include "GameState.h"
#include "Game.h"

REGISTER_LIGHTS_DRIVER_CLASS(stac);

LightsDriver_stac::LightsDriver_stac()
{
	struct hid_device_info *devs, *cur_dev;

	memset(outputBuffer[GameController_1], 0x00, STAC_HIDREPORT_SIZE);
	memset(outputBuffer[GameController_2], 0x00, STAC_HIDREPORT_SIZE);

	// Enumerate through to find the lighting interface.
	devs = hid_enumerate(STAC_VID, 0);
	cur_dev = devs;

	if (devs && cur_dev)
	{
		while (cur_dev)
		{
			if (cur_dev->vendor_id == STAC_VID &&
				cur_dev->interface_number == STAC_LIGHTING_INTERFACE)
			{
				if (cur_dev->product_id == STAC_PID_P1)
				{
					handle[0] = hid_open_path(cur_dev->path);
				}
				else if (cur_dev->product_id == STAC_PID_P2)
				{
					handle[1] = hid_open_path(cur_dev->path);
				}
			}

			cur_dev = cur_dev->next;
		}
	}

	if (!handle[0])
	{
		LOG->Warn("stac P1 device not found.");
	}

	if (!handle[1])
	{
		LOG->Warn("stac P2 device not found.");
	}
}

LightsDriver_stac::~LightsDriver_stac()
{
	for (int i = 0; i < STAC_MAX_NUMBER; i++)
	{
		if (handle[i])
		{
			hid_close(handle[i]);
		}
	}

	// Finalize the hidapi library
	hid_exit();
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
	if (!handle[ctrlNum])
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
		// TODO: Check for error/reconnect.
		hid_write(handle[ctrlNum], (unsigned char *)&outputBuffer[ctrlNum], STAC_HIDREPORT_SIZE);
		stateChanged[ctrlNum] = false;
	}
}

void LightsDriver_stac::Set(const LightsState *ls)
{
	HandleState(ls, GameController_1);
	HandleState(ls, GameController_2);
}
