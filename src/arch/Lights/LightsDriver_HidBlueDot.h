#ifndef LightsDriver_HidBlueDot_H
#define LightsDriver_HidBlueDot_H

#include "arch/Lights/LightsDriver.h"
#include "archutils/Common/HidDevice.h"

#define VID 0x04BD
#define PID 0xBD

class LightsDriver_HidBlueDot : public LightsDriver
{
private:
	enum CabinetLightIndex
	{
		m_Marquee_UpLeft = 0,
		m_Marquee_UpRight,
		m_Marquee_LwLeft,
		m_Marquee_LwRight,
		m_Buttons_Left,
		m_Buttons_Right,
		m_Bass
	};

	enum PadLightIndex
	{
		m_PadP1_Up = 0,
		m_PadP1_Down,
		m_PadP1_Left,
		m_PadP1_Right,
		m_PadP2_Up,
		m_PadP2_Down,
		m_PadP2_Left,
		m_PadP2_Right
	};

	uint8_t m_iCabData[3];
	uint8_t m_iPadData[3];
	HidDevice dev;

	void SetLight(unsigned char* buffer, int index, bool value);
	void SetPadLight(PadLightIndex index, bool value);
	void SetCabinetLight(CabinetLightIndex index, bool value);
public:
	LightsDriver_HidBlueDot();
	virtual ~LightsDriver_HidBlueDot();

	virtual void Set( const LightsState *ls );
};

#endif
