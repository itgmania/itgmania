#include "global.h"
#include "RageLog.h"
#include "LightsDriver_PacDrive.h"
#include "GameState.h"
#include "Game.h"

REGISTER_LIGHTS_DRIVER_CLASS(PacDrive);

static Preference<RString> g_sPacDriveLightOrdering("PacDriveLightOrdering", "openitg");
int iPacDriveLightOrder = 0;

LightsDriver_PacDrive::LightsDriver_PacDrive() : dev{PACDRIVE_VID, make_pids(PACDRIVE_PID, PACDRIVE_PID_MAX), PACDRIVE_INTERFACE}
{
	prev_led_state.raw = 0;
	memset(state.raw_state, 0x00, sizeof(state.raw_state));

	RString lightOrder = g_sPacDriveLightOrdering.Get();
	if (lightOrder.CompareNoCase("lumenar") == 0 || lightOrder.CompareNoCase("openitg") == 0)
	{
		iPacDriveLightOrder = 1;
	}
}

LightsDriver_PacDrive::~LightsDriver_PacDrive()
{
}

void LightsDriver_PacDrive::Set(const LightsState *ls)
{
	if (!dev.FoundOnce())
		return;

	switch (iPacDriveLightOrder)
	{
	case 1:
		// Sets the cabinet light values to follow LumenAR/OpenITG wiring standards

		/*
		 * OpenITG PacDrive Order:
		 * Taken from LightsDriver_PacDrive::SetLightsMappings() in openitg.
		 * (index of 1 as the PacDrive labels them as index 1)
		 *
		 * 1: Marquee UL
		 * 2: Marquee UR
		 * 3: Marquee DL
		 * 4: Marquee DR
		 *
		 * 5: P1 Button
		 * 6: P2 Button
		 *
		 * 7: Bass Left
		 * 8: Bass Right
		 *
		 * 9,19,11,12: P1 L R U D
		 * 13,14,15,16: P2 L R U D
		 */

		state.leds.led01 = ls->m_bCabinetLights[LIGHT_MARQUEE_UP_LEFT];
		state.leds.led02 = ls->m_bCabinetLights[LIGHT_MARQUEE_UP_RIGHT];
		state.leds.led03 = ls->m_bCabinetLights[LIGHT_MARQUEE_LR_LEFT];
		state.leds.led04 = ls->m_bCabinetLights[LIGHT_MARQUEE_LR_RIGHT];

		state.leds.led05 = ls->m_bGameButtonLights[GameController_1][GAME_BUTTON_START];
		state.leds.led06 = ls->m_bGameButtonLights[GameController_2][GAME_BUTTON_START];

		state.leds.led07 = ls->m_bCabinetLights[LIGHT_BASS_LEFT] || ls->m_bCabinetLights[LIGHT_BASS_RIGHT];
		state.leds.led08 = ls->m_bCabinetLights[LIGHT_BASS_LEFT] || ls->m_bCabinetLights[LIGHT_BASS_RIGHT];

		state.leds.led09 = ls->m_bGameButtonLights[GameController_1][DANCE_BUTTON_LEFT];
		state.leds.led10 = ls->m_bGameButtonLights[GameController_1][DANCE_BUTTON_RIGHT];
		state.leds.led11 = ls->m_bGameButtonLights[GameController_1][DANCE_BUTTON_UP];
		state.leds.led12 = ls->m_bGameButtonLights[GameController_1][DANCE_BUTTON_DOWN];

		state.leds.led13 = ls->m_bGameButtonLights[GameController_2][DANCE_BUTTON_LEFT];
		state.leds.led14 = ls->m_bGameButtonLights[GameController_2][DANCE_BUTTON_RIGHT];
		state.leds.led15 = ls->m_bGameButtonLights[GameController_2][DANCE_BUTTON_UP];
		state.leds.led16 = ls->m_bGameButtonLights[GameController_2][DANCE_BUTTON_DOWN];
		break;

	case 0:
	default:
		// If all else fails, falls back to original order
		// reference page 7
		// http://www.peeweepower.com/stepmania/sm509pacdriveinfo.pdf

		state.leds.led01 = ls->m_bCabinetLights[LIGHT_MARQUEE_UP_LEFT];
		state.leds.led02 = ls->m_bCabinetLights[LIGHT_MARQUEE_UP_RIGHT];
		state.leds.led03 = ls->m_bCabinetLights[LIGHT_MARQUEE_LR_LEFT];
		state.leds.led04 = ls->m_bCabinetLights[LIGHT_MARQUEE_LR_RIGHT];

		state.leds.led05 = ls->m_bCabinetLights[LIGHT_BASS_LEFT] || ls->m_bCabinetLights[LIGHT_BASS_RIGHT];

		state.leds.led06 = ls->m_bGameButtonLights[GameController_1][DANCE_BUTTON_LEFT];
		state.leds.led07 = ls->m_bGameButtonLights[GameController_1][DANCE_BUTTON_RIGHT];
		state.leds.led08 = ls->m_bGameButtonLights[GameController_1][DANCE_BUTTON_UP];
		state.leds.led09 = ls->m_bGameButtonLights[GameController_1][DANCE_BUTTON_DOWN];
		state.leds.led10 = ls->m_bGameButtonLights[GameController_1][GAME_BUTTON_START];

		state.leds.led11 = ls->m_bGameButtonLights[GameController_2][DANCE_BUTTON_LEFT];
		state.leds.led12 = ls->m_bGameButtonLights[GameController_2][DANCE_BUTTON_RIGHT];
		state.leds.led13 = ls->m_bGameButtonLights[GameController_2][DANCE_BUTTON_UP];
		state.leds.led14 = ls->m_bGameButtonLights[GameController_2][DANCE_BUTTON_DOWN];
		state.leds.led15 = ls->m_bGameButtonLights[GameController_2][GAME_BUTTON_START];

		// led16 is not used.
		state.leds.led16 = false;
		break;
	}

	// only push on changes.
	if (state.leds.raw != prev_led_state.raw)
	{
		state.report_id = PACDRIVE_HIDREPORT_ID;
		state.pad0 = 0;
		state.pad1 = 0;

		dev.Write((unsigned char *)&state.raw_state, sizeof(state.raw_state));
		prev_led_state = state.leds;
	}
}
