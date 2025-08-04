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
	if (CompareNoCase(lightOrder, "lumenar") == 0 || CompareNoCase(lightOrder, "openitg") == 0)
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
		/*
		 * OpenITG (LumenAR) Order:
		 * http://solid-orange.com/wp-content/uploads/2014/02/ddr_oitg_pacdrive_pins.gif
		 * Note: The bit order in OpenITG is byte swapped as PacDrive's vendor library byte
		 * swaps the values.
		 *
		 * This listing mirrors the PHYSICAL pins each of the lights comes out.
		 *
		 * 01: P1 Left
		 * 02: P1 Right
		 * 03: P1 Up
		 * 04: P1 Down
		 * 05: P2 Left
		 * 06: P2 Right
		 * 07: P2 Up
		 * 08: P2 Down
		 * 09: Marquee UL
		 * 10: Marquee UR
		 * 11: Marquee LL
		 * 12: Marquee LR
		 * 13: P1 Start
		 * 14: P2 Start
		 * 15: Bass Left
		 * 16: Bass Right
		 */

		state.leds.led01 = ls->m_bGameButtonLights[GameController_1][DANCE_BUTTON_LEFT];
		state.leds.led02 = ls->m_bGameButtonLights[GameController_1][DANCE_BUTTON_RIGHT];
		state.leds.led03 = ls->m_bGameButtonLights[GameController_1][DANCE_BUTTON_UP];
		state.leds.led04 = ls->m_bGameButtonLights[GameController_1][DANCE_BUTTON_DOWN];

		state.leds.led05 = ls->m_bGameButtonLights[GameController_2][DANCE_BUTTON_LEFT];
		state.leds.led06 = ls->m_bGameButtonLights[GameController_2][DANCE_BUTTON_RIGHT];
		state.leds.led07 = ls->m_bGameButtonLights[GameController_2][DANCE_BUTTON_UP];
		state.leds.led08 = ls->m_bGameButtonLights[GameController_2][DANCE_BUTTON_DOWN];

		state.leds.led09 = ls->m_bCabinetLights[LIGHT_MARQUEE_UP_LEFT];
		state.leds.led10 = ls->m_bCabinetLights[LIGHT_MARQUEE_UP_RIGHT];
		state.leds.led11 = ls->m_bCabinetLights[LIGHT_MARQUEE_LR_LEFT];
		state.leds.led12 = ls->m_bCabinetLights[LIGHT_MARQUEE_LR_RIGHT];

		state.leds.led13 = ls->m_bGameButtonLights[GameController_1][GAME_BUTTON_START];
		state.leds.led14 = ls->m_bGameButtonLights[GameController_2][GAME_BUTTON_START];

		state.leds.led15 = ls->m_bCabinetLights[LIGHT_BASS_LEFT] || ls->m_bCabinetLights[LIGHT_BASS_RIGHT];
		state.leds.led16 = ls->m_bCabinetLights[LIGHT_BASS_LEFT] || ls->m_bCabinetLights[LIGHT_BASS_RIGHT];

		break;

	case 0:
	default:
		/*
		* SM5 order.
		* see pg7 of http://www.peeweepower.com/stepmania/sm509pacdriveinfo.pdf
		*
		* 01: Marquee UL
		* 02: Marquee UR
		* 03: Marquee LL
		* 04: Marquee LR
		* 05: Bass (both)
		* 06: P1 Left
		* 07: P1 Right
		* 08: P1 Up
		* 09: P1 Down
		* 10: P1 Start
		* 11: P2 Left
		* 12: P2 Right
		* 13: P2 Up
		* 14: P2 Down
		* 15: P2 Start
		* 16: (unused)
		*/

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
