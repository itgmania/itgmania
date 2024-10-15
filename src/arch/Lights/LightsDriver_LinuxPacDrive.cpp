// Cross-platform, libusb-based driver for outputting lights
// via a PacDrive (http://www.ultimarc.com/pacdrive.html)

#include "global.h"
#include "RageLog.h"
#include "LightsDriver_LinuxPacDrive.h"

#include <cstdint>
#include <libusb.h>


REGISTER_LIGHTS_DRIVER_CLASS( LinuxPacDrive );

// HID Class-Specific Requests values. See section 7.2 of the HID specifications
#define HID_GET_REPORT                0x01
#define HID_GET_IDLE                  0x02
#define HID_GET_PROTOCOL              0x03
#define HID_SET_REPORT                0x09
#define HID_SET_IDLE                  0x0A
#define HID_SET_PROTOCOL              0x0B
#define HID_REPORT_TYPE_INPUT         0x01
#define HID_REPORT_TYPE_OUTPUT        0x02
#define HID_REPORT_TYPE_FEATURE       0x03

#define HID_IFACE_IN    256
#define HID_IFACE_OUT   512

// PacDrives have PIDs 1500 - 1507, but we'll handle that later.
const unsigned PACDRIVE_TIMEOUT = 10000;
const int PACDRIVE_VENDOR_ID = 0xD209;
const int PACDRIVE_PRODUCT_ID = 0x1500;

//Adds new preference to allow for different light wiring setups
static Preference<RString> g_sPacDriveLightOrdering("PacDriveLightOrdering", "openitg");
int iLightingOrder = 0;

LightsDriver_LinuxPacDrive::LightsDriver_LinuxPacDrive()
{
    DeviceHandle = NULL;

    OpenDevice();

    // clear all lights
    WriteDevice( 0 );

    RString lightOrder = g_sPacDriveLightOrdering.Get();
    if (lightOrder.CompareNoCase("lumenar") == 0 || lightOrder.CompareNoCase("openitg") == 0) {
        iLightingOrder = 1;
    }
}

LightsDriver_LinuxPacDrive::~LightsDriver_LinuxPacDrive()
{
    // clear all lights and close the connection
    WriteDevice( 0 );
    CloseDevice();
}

void LightsDriver_LinuxPacDrive::Set( const LightsState *ls )
{
    if ( !DeviceHandle ) return;

	uint16_t outb = 0;

    switch (iLightingOrder) {
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
        //If all else fails, falls back to original order
        //reference page 7
        //http://www.peeweepower.com/stepmania/sm509pacdriveinfo.pdf

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
        //Bit index 15 is unused.

        break;
    }

    WriteDevice(outb);
}

void LightsDriver_LinuxPacDrive::OpenDevice()
{
    libusb_device **devs;
    libusb_device *dev = NULL;
    ssize_t result;
    int i = 0;

    CloseDevice(); // Ensure any previously opened device is closed first to prevent conflicts

    libusb_context* context = USBContext::getInstance().getContext();
    if (!context)
    {
        LOG->Warn("libusb: Failed to initialize context");
        return;
    }

    libusb_set_option(context, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_INFO);

    result = libusb_get_device_list(context, &devs);
    if (result < 0)
    {
        LOG->Warn("libusb_get_device_list failed: %s", libusb_error_name(result));
        return;
    }

    while ((dev = devs[i++]) != NULL)
    {
        libusb_device_descriptor desc;
        // Note since libusb-1.0.16, LIBUSBX_API_VERSION >= 0x01000102, this function always succeeds:
        libusb_get_device_descriptor(dev, &desc);

        if (PACDRIVE_VENDOR_ID == desc.idVendor &&
            PACDRIVE_PRODUCT_ID <= desc.idProduct &&
            PACDRIVE_PRODUCT_ID + 8 > desc.idProduct)
        {
            LOG->Info("PacDrive device was found vid: 0x%04x pid: 0x%04x", desc.idVendor, desc.idProduct);
            break;
        }
    }

    if (!dev)
    {
        LOG->Warn("PacDrive was not found");
        libusb_free_device_list(devs, 1);
        return;
    }

    result = libusb_open(dev, &DeviceHandle);
    if (result < 0)
    {
        LOG->Warn("libusb_open failed: %s", libusb_error_name(result));
        DeviceHandle = NULL;
        libusb_free_device_list(devs, 1);
        return;
    }

    libusb_free_device_list(devs, 1);

    if (libusb_kernel_driver_active(DeviceHandle, 0) == 1)
    {
        LOG->Warn("Kernel Driver Active");
        if (libusb_detach_kernel_driver(DeviceHandle, 0) == 0)
            LOG->Warn("Kernel Driver Detached!");
    }

    result = libusb_claim_interface(DeviceHandle, 0);
    if (result < 0)
    {
        LOG->Warn("libusb_claim_interface: cannot claim interface: %s", libusb_error_name(result));
        libusb_close(DeviceHandle);
        return;
    }

    LOG->Info("Device claimed");
}

void LightsDriver_LinuxPacDrive::CloseDevice()
{
    if (!DeviceHandle)
        return;

    ssize_t result;

    result = libusb_release_interface(DeviceHandle, 0);
    if (result != 0)
    {
        LOG->Warn("libusb_release_interface: %s", libusb_error_name(result));
    }

    libusb_close(DeviceHandle);
    DeviceHandle = NULL;
}

void LightsDriver_LinuxPacDrive::WriteDevice(uint16_t out)
{
    if (!DeviceHandle) return;

    // output is within the first 16 bits - accept a
    // 16-bit arg and cast it, for simplicity's sake.
	uint32_t data = (out << 16);
    int expected = sizeof(data);

    ssize_t result = libusb_control_transfer(DeviceHandle,
                                             LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
                                             HID_SET_REPORT,
                                             HID_IFACE_OUT,
                                             0,
                                             (unsigned char*)&data,
                                             expected,
                                             PACDRIVE_TIMEOUT);

    if(result != expected) {
        LOG->Warn("PacDrive writing failed: %li (%s)\n", result, libusb_error_name(result));
        CloseDevice();
    }
}

/*
 * Rewritten for libusb 1.0 2024 sirex
 * Rewritten for libusb 1.0 2024 sukibaby
 * Original libusb 0.1 file Copyright (c) 2008 BoXoRRoXoRs
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
