// Cross-platform, libusb-based driver for outputting lights
// via a PacDrive (http://www.ultimarc.com/pacdrive.html)

#include "global.h"
#include "RageLog.h"
#include "io/PacDrive.h"
#include "LightsDriver_LinuxPacDrive.h"
#include "Preference.h"

REGISTER_LIGHTS_DRIVER_CLASS( LinuxPacDrive );

// Adds new preference to allow for different light wiring setups
// This is the same preference as LightsDriver_PacDrive.cpp but that only gets
// loaded on Windows builds.
static Preference<RString> g_sPacDriveLightOrdering("PacDriveLightOrdering", "minimaid");

LightsDriver_LinuxPacDrive::LightsDriver_LinuxPacDrive()
{
	m_bHasDevice = Board.Open();

	RString lightOrder = g_sPacDriveLightOrdering.Get();
	if (lightOrder.CompareNoCase("lumenar") == 0 || lightOrder.CompareNoCase("openitg") == 0) {
		m_iLightingOrder = 1;
		LOG->Info( "Setting LinuxPacDrive order to: OpenITG" );
	}
	else
	{
		m_iLightingOrder = 0;
		LOG->Info( "Setting LinuxPacDrive order to: SM5" );
	}


	if( m_bHasDevice == false )
	{
		LOG->Warn( "Could not establish a connection with PacDrive." );
		return;
	}

	// clear all lights
	Board.Write( 0 );
}

LightsDriver_LinuxPacDrive::~LightsDriver_LinuxPacDrive()
{
	if( !m_bHasDevice )
		return;

	// clear all lights and close the connection
	Board.Write( 0 );
	Board.Close();
}

void LightsDriver_LinuxPacDrive::Set( const LightsState *ls )
{
	if( !m_bHasDevice )
		return;

	uint16_t outb = 0;

	switch (m_iLightingOrder) {
		case 1:
			//Sets the cabinet light values to follow LumenAR/OpenITG wiring standards

			/*
			 * OpenITG PacDrive Order:
			 * Taken from LightsDriver_PacDrive::SetLightsMappings() in openitg.
			 *
			 * 0: Marquee UL
			 * 1: Marquee UR
			 * 2: Marquee DL
			 * 3: Marquee DR
			 *
			 * 4: P1 Button
			 * 5: P2 Button
			 *
			 * 6: Bass Left
			 * 7: Bass Right
			 *
			 * 8,9,10,11: P1 L R U D
			 * 12,13,14,15: P2 L R U D
			 */

			if (ls->m_bCabinetLights[LIGHT_MARQUEE_UP_LEFT]) outb |= BIT(0);
			if (ls->m_bCabinetLights[LIGHT_MARQUEE_UP_RIGHT]) outb |= BIT(1);
			if (ls->m_bCabinetLights[LIGHT_MARQUEE_LR_LEFT]) outb |= BIT(2);
			if (ls->m_bCabinetLights[LIGHT_MARQUEE_LR_RIGHT]) outb |= BIT(3);

			if (ls->m_bGameButtonLights[GameController_1][GAME_BUTTON_START]) outb |= BIT(4);
			if (ls->m_bGameButtonLights[GameController_2][GAME_BUTTON_START]) outb |= BIT(5);

			//Most PacDrive/Cabinet setups only have *one* bass light, so mux them together here.
			if (ls->m_bCabinetLights[LIGHT_BASS_LEFT] || ls->m_bCabinetLights[LIGHT_BASS_RIGHT]) outb |= BIT(6);
			if (ls->m_bCabinetLights[LIGHT_BASS_LEFT] || ls->m_bCabinetLights[LIGHT_BASS_RIGHT]) outb |= BIT(7);

			if (ls->m_bGameButtonLights[GameController_1][DANCE_BUTTON_LEFT]) outb |= BIT(8);
			if (ls->m_bGameButtonLights[GameController_1][DANCE_BUTTON_RIGHT]) outb |= BIT(9);
			if (ls->m_bGameButtonLights[GameController_1][DANCE_BUTTON_UP]) outb |= BIT(10);
			if (ls->m_bGameButtonLights[GameController_1][DANCE_BUTTON_DOWN]) outb |= BIT(11);

			if (ls->m_bGameButtonLights[GameController_2][DANCE_BUTTON_LEFT]) outb |= BIT(12);
			if (ls->m_bGameButtonLights[GameController_2][DANCE_BUTTON_RIGHT]) outb |= BIT(13);
			if (ls->m_bGameButtonLights[GameController_2][DANCE_BUTTON_UP]) outb |= BIT(14);
			if (ls->m_bGameButtonLights[GameController_2][DANCE_BUTTON_DOWN]) outb |= BIT(15);

			break;

		case 0:
		default:
			//If all else fails, falls back to Minimaid order

			if (ls->m_bCabinetLights[LIGHT_MARQUEE_UP_LEFT]) outb |= BIT(0);
			if (ls->m_bCabinetLights[LIGHT_MARQUEE_UP_RIGHT]) outb |= BIT(1);
			if (ls->m_bCabinetLights[LIGHT_MARQUEE_LR_LEFT]) outb |= BIT(2);
			if (ls->m_bCabinetLights[LIGHT_MARQUEE_LR_RIGHT]) outb |= BIT(3);

			if (ls->m_bCabinetLights[LIGHT_BASS_LEFT] || ls->m_bCabinetLights[LIGHT_BASS_RIGHT]) outb |= BIT(4);

			if (ls->m_bGameButtonLights[GameController_1][DANCE_BUTTON_LEFT]) outb |= BIT(5);
			if (ls->m_bGameButtonLights[GameController_1][DANCE_BUTTON_RIGHT]) outb |= BIT(6);
			if (ls->m_bGameButtonLights[GameController_1][DANCE_BUTTON_UP]) outb |= BIT(7);
			if (ls->m_bGameButtonLights[GameController_1][DANCE_BUTTON_DOWN]) outb |= BIT(8);
			if (ls->m_bGameButtonLights[GameController_1][GAME_BUTTON_START]) outb |= BIT(9);

			if (ls->m_bGameButtonLights[GameController_2][DANCE_BUTTON_LEFT]) outb |= BIT(10);
			if (ls->m_bGameButtonLights[GameController_2][DANCE_BUTTON_RIGHT]) outb |= BIT(11);
			if (ls->m_bGameButtonLights[GameController_2][DANCE_BUTTON_UP]) outb |= BIT(12);
			if (ls->m_bGameButtonLights[GameController_2][DANCE_BUTTON_DOWN]) outb |= BIT(13);
			if (ls->m_bGameButtonLights[GameController_2][GAME_BUTTON_START]) outb |= BIT(14);

			break;
	}

	// write the data - if it fails, stop updating
	if( !Board.Write(outb) )
	{
		LOG->Warn( "Lost connection with PacDrive." );
		m_bHasDevice = false;
	}
}

/*
 * Copyright (c) 2008 BoXoRRoXoRs
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
