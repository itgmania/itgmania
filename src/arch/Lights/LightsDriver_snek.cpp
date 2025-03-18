#include "global.h"
#include "RageLog.h"
#include "LightsDriver_snek.h"
#include "GameState.h"
#include "Game.h"

REGISTER_LIGHTS_DRIVER_CLASS(snek);

LightsDriver_snek::LightsDriver_snek()
{
	struct hid_device_info *devs, *cur_dev;

	memset(outputBuffer, 0x00, SNEK_HIDREPORT_SIZE);

	// Enumerate through the snek device to find the lighting interface.
	devs = hid_enumerate(SNEK_VID, SNEK_PID);
	cur_dev = devs;

	if (devs && cur_dev)
	{
		// Look for the desired interface number for lighting.
		while (cur_dev)
		{
			if (cur_dev->vendor_id == SNEK_VID &&
				cur_dev->product_id == SNEK_PID &&
				cur_dev->interface_number == SNEK_LIGHTING_INTERFACENUM)
			{
				// Open the device via its path (only way to get interface)
				handle = hid_open_path(cur_dev->path);

				break;
			}

			cur_dev = cur_dev->next;
		}
	}

	if (!handle)
	{
		LOG->Warn("snek board lighting not found.");
	}
}

LightsDriver_snek::~LightsDriver_snek()
{
	if (handle)
	{
		hid_close(handle);
	}

	// Finalize the hidapi library
	hid_exit();
}

void LightsDriver_snek::SetBuffer(int index, bool lightState)
{
	// the first byte is the report ID, so we offset it here to adjust.
	uint8_t index_offset = index + 1;

	// each index in the array represents a single light,
	// the light will turn on for any value that isn't 0x00
	uint8_t val = lightState ? 0xFF : 0x00;

	// ensure report ID is set.
	outputBuffer[0] = SNEK_REPORT_ID;

	// ensure the index is valid and the light value has changed.
	if (index_offset < SNEK_HIDREPORT_SIZE && outputBuffer[index_offset] != val)
	{
		outputBuffer[index_offset] = val;

		// signal the loop to push the new buffer to the device.
		stateChanged = true;
	}
}

void LightsDriver_snek::Set(const LightsState *ls)
{
	// do not make a message for a non-connected device.
	if (!handle)
	{
		return;
	}

	SetBuffer(SNEK_INDEX_DANCE_M_UL, ls->m_bCabinetLights[LIGHT_MARQUEE_UP_LEFT]);
	SetBuffer(SNEK_INDEX_DANCE_M_UR, ls->m_bCabinetLights[LIGHT_MARQUEE_UP_RIGHT]);
	SetBuffer(SNEK_INDEX_DANCE_M_LL, ls->m_bCabinetLights[LIGHT_MARQUEE_LR_LEFT]);
	SetBuffer(SNEK_INDEX_DANCE_M_LR, ls->m_bCabinetLights[LIGHT_MARQUEE_LR_RIGHT]);

	SetBuffer(SNEK_INDEX_DANCE_P1_START, ls->m_bGameButtonLights[GameController_1][GAME_BUTTON_START]);
	SetBuffer(SNEK_INDEX_DANCE_P2_START, ls->m_bGameButtonLights[GameController_2][GAME_BUTTON_START]);

	SetBuffer(SNEK_INDEX_DANCE_NEON, ls->m_bCabinetLights[LIGHT_BASS_LEFT] || ls->m_bCabinetLights[LIGHT_BASS_RIGHT]);

	SetBuffer(SNEK_INDEX_DANCE_P1_UP, ls->m_bGameButtonLights[GameController_1][DANCE_BUTTON_UP]);
	SetBuffer(SNEK_INDEX_DANCE_P1_DOWN, ls->m_bGameButtonLights[GameController_1][DANCE_BUTTON_DOWN]);
	SetBuffer(SNEK_INDEX_DANCE_P1_LEFT, ls->m_bGameButtonLights[GameController_1][DANCE_BUTTON_LEFT]);
	SetBuffer(SNEK_INDEX_DANCE_P1_RIGHT, ls->m_bGameButtonLights[GameController_1][DANCE_BUTTON_RIGHT]);

	SetBuffer(SNEK_INDEX_DANCE_P2_UP, ls->m_bGameButtonLights[GameController_2][DANCE_BUTTON_UP]);
	SetBuffer(SNEK_INDEX_DANCE_P2_DOWN, ls->m_bGameButtonLights[GameController_2][DANCE_BUTTON_DOWN]);
	SetBuffer(SNEK_INDEX_DANCE_P2_LEFT, ls->m_bGameButtonLights[GameController_2][DANCE_BUTTON_LEFT]);
	SetBuffer(SNEK_INDEX_DANCE_P2_RIGHT, ls->m_bGameButtonLights[GameController_2][DANCE_BUTTON_RIGHT]);

	// only push on changes.
	if (stateChanged)
	{
		// TODO: Check for error/reconnect.
		hid_write(handle, (unsigned char *)&outputBuffer, SNEK_HIDREPORT_SIZE);
		stateChanged = false;
	}
}
