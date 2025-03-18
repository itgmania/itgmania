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
		m_Marquee_UpLeft = 0x01,
		m_Marquee_UpRight = 0x02,
		m_Marquee_LwLeft = 0x04,
		m_Marquee_LwRight = 0x08,
		m_Buttons_Left = 0x10,
		m_Buttons_Right = 0x20,
		m_Bass = 0x40,
	};

	enum PadLightIndex
	{
		m_PadP1_Up = 0x01,
		m_PadP1_Down = 0x02,
		m_PadP1_Left = 0x04,
		m_PadP1_Right = 0x08,
		m_PadP2_Up = 0x10,
		m_PadP2_Down = 0x20,
		m_PadP2_Left = 0x40,
		m_PadP2_Right = 0x80
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
