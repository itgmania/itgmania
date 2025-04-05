#include "global.h"
#include "RageLog.h"
#include "LightsDriver_snek.h"
#include "GameState.h"
#include "Game.h"

REGISTER_LIGHTS_DRIVER_CLASS(snek);


LightsDriver_snek::LightsDriver_snek() :
	dev{ SNEK_VID , SNEK_PID, SNEK_LIGHTING_INTERFACENUM },
	stateChanged{ false },
	outputBuffer{0}
{
}

LightsDriver_snek::~LightsDriver_snek()
{

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
	if (!dev.FoundOnce())
		return;

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
		dev.Write((unsigned char *)&outputBuffer, SNEK_HIDREPORT_SIZE);
		stateChanged = false;
	}
}
