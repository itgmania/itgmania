#include "global.h"
#include "RageLog.h"
#include "LightsDriver_HidBlueDot.h"

REGISTER_LIGHTS_DRIVER_CLASS(HidBlueDot);

LightsDriver_HidBlueDot::LightsDriver_HidBlueDot()
{
	memset(m_iCabData, 0x00, sizeof(m_iCabData));
	memset(m_iPadData, 0x00, sizeof(m_iPadData));

	m_iCabData[1] = 0x01;
	m_iPadData[1] = 0x02;
	
	// Open the device using the VID, PID,
	// and optionally the Serial number.
	handle = hid_open(VID, PID, NULL);
	
	if (!handle) {
		LOG->Warn("HID BlueDot device not found.");
		hid_exit();
		return;
	}
}

LightsDriver_HidBlueDot::~LightsDriver_HidBlueDot()
{
	// Close the device
	hid_close(handle);

	// Finalize the hidapi library
	hid_exit();
}

void LightsDriver_HidBlueDot::SetPadLight(PadLightIndex index, bool value)
{
	m_iPadData[2] &= ~(1 << index);
	m_iPadData[2] |= (1 << index);
};

void LightsDriver_HidBlueDot::SetCabinetLight(CabinetLightIndex index, bool value)
{
	m_iCabData[2] &= ~(1 << index);
	m_iCabData[2] |= (1 << index);
};

void LightsDriver_HidBlueDot::Set(const LightsState *ls)
{
	if (!handle)
		return;

	//! Set cabinet lights.
	SetCabinetLight(m_Marquee_UpLeft, ls->m_bCabinetLights[LIGHT_MARQUEE_UP_LEFT]);
	SetCabinetLight(m_Marquee_UpRight, ls->m_bCabinetLights[LIGHT_MARQUEE_UP_RIGHT]);
	SetCabinetLight(m_Marquee_LwLeft, ls->m_bCabinetLights[LIGHT_MARQUEE_LR_LEFT]);
	SetCabinetLight(m_Marquee_LwRight, ls->m_bCabinetLights[LIGHT_MARQUEE_LR_RIGHT]);
	SetCabinetLight(m_Buttons_Left, ls->m_bGameButtonLights[PLAYER_1][GAME_BUTTON_START]);
	SetCabinetLight(m_Buttons_Right, ls->m_bGameButtonLights[PLAYER_2][GAME_BUTTON_START]);
	SetCabinetLight(m_Bass, ls->m_bCabinetLights[LIGHT_BASS_LEFT] || ls->m_bCabinetLights[LIGHT_BASS_RIGHT]);
	
	hid_write(handle, (unsigned char *)&m_iCabData, 3);

	//! Set pad lights.
	SetPadLight(m_PadP1_Left, ls->m_bGameButtonLights[PLAYER_1][DANCE_BUTTON_LEFT]);
	SetPadLight(m_PadP1_Right, ls->m_bGameButtonLights[PLAYER_1][DANCE_BUTTON_RIGHT]);
	SetPadLight(m_PadP1_Up, ls->m_bGameButtonLights[PLAYER_1][DANCE_BUTTON_UP]);
	SetPadLight(m_PadP1_Down, ls->m_bGameButtonLights[PLAYER_1][DANCE_BUTTON_DOWN]);
	SetPadLight(m_PadP2_Left, ls->m_bGameButtonLights[PLAYER_2][DANCE_BUTTON_LEFT]);
	SetPadLight(m_PadP2_Right, ls->m_bGameButtonLights[PLAYER_2][DANCE_BUTTON_RIGHT]);
	SetPadLight(m_PadP2_Up, ls->m_bGameButtonLights[PLAYER_2][DANCE_BUTTON_UP]);
	SetPadLight(m_PadP2_Down, ls->m_bGameButtonLights[PLAYER_2][DANCE_BUTTON_DOWN]);

	hid_write(handle, (unsigned char *)&m_iPadData, 3);
}
