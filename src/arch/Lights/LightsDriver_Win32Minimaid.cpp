// LightsDriver_Win32Minimaid - driver for lights on the Minimaid with
// libmmmagic
/* To use, StepMania must be run with root permissions, or udev rule for the
 * device must be made something like this (but it's not quite right):
 * echo SUBSYSTEM==\"usb\", ATTR{idVendor}==\"beef\", ATTR{idProduct}==\"5730\",
 * MODE=\"0666\" > /etc/udev/rules.d/50-minimaid
 */

#include "LightsDriver_Win32Minimaid.h"

#include "global.h"
#include "windows.h"

REGISTER_LIGHTS_DRIVER_CLASS(Win32Minimaid);

HINSTANCE mmmagic_dll = nullptr;

int minimaid_filter(unsigned int, struct _EXCEPTION_POINTERS*) {
  return EXCEPTION_EXECUTE_HANDLER;
}

void setup_driver() {
  __try {
    // Get the function pointers
    mm_connect_minimaid =
        (MM_CONNECT_MINIMAID)GetProcAddress(mmmagic_dll, "mm_connect_minimaid");
    mm_set_kb = (MM_SETKB)GetProcAddress(mmmagic_dll, "mm_setKB");
    mm_set_ddr_pad1_light =
        (MM_SETDDRPAD1LIGHT)GetProcAddress(mmmagic_dll, "mm_setDDRPad1Light");
    mm_set_ddr_pad2_light =
        (MM_SETDDRPAD2LIGHT)GetProcAddress(mmmagic_dll, "mm_setDDRPad2Light");
    mm_set_ddr_cabinet_light = (MM_SETCABINETLIGHT)GetProcAddress(
        mmmagic_dll, "mm_setDDRCabinetLight");
    mm_set_ddr_bass_light =
        (MM_SETDDRBASSLIGHT)GetProcAddress(mmmagic_dll, "mm_setDDRBassLight");
    mm_set_ddr_all_on =
        (MM_SETDDRALLON)GetProcAddress(mmmagic_dll, "mm_setDDRAllOn");
    mm_set_ddr_all_off =
        (MM_SETDDRALLOFF)GetProcAddress(mmmagic_dll, "mm_setDDRAllOff");
    mm_set_blue_led =
        (MM_SETBLUELED)GetProcAddress(mmmagic_dll, "mm_setBlueLED");
    mm_set_mm_output_reports = (MM_SETMMOUTPUTREPORTS)GetProcAddress(
        mmmagic_dll, "mm_setMMOutputReports");
    mm_send_ddr_mini_maid_update = (MM_SENDDDRMINIMAIDUPDATE)GetProcAddress(
        mmmagic_dll, "mm_sendDDRMiniMaidUpdate");
    mm_connect_minimaid();
    mm_set_kb(true);

    mmmagic_loaded = true;
  } __except (minimaid_filter(GetExceptionCode(), GetExceptionInformation())) {
    MessageBox(
        nullptr,
        "Could not connect to the Mimimaid device. Freeing the library now.",
        "ERROR", MB_OK);
    FreeLibrary(mmmagic_dll);
  }
}

LightsDriver_Win32Minimaid::LightsDriver_Win32Minimaid() {
  mmmagic_loaded = false;
  mmmagic_dll = LoadLibraryW(L"mmmagic64.dll");
  if (mmmagic_dll == nullptr) {
    MessageBox(
        nullptr, "Could not LoadLibrary( mmmagic64.dll ).", "ERROR", MB_OK);
    return;
  }
  setup_driver();
}

LightsDriver_Win32Minimaid::~LightsDriver_Win32Minimaid() {
  if (mmmagic_loaded) {
    mm_set_ddr_all_off();
    FreeLibrary(mmmagic_dll);
  }
}

void LightsDriver_Win32Minimaid::Set(const LightsState* ls) {
  if (mmmagic_loaded) {
    mm_set_ddr_all_off();

    // Set the cabinet light values
    if (ls->cabinet_lights_[LIGHT_MARQUEE_UP_LEFT]) {
      mm_set_ddr_cabinet_light(DDR_DOUBLE_MARQUEE_UPPER_LEFT, 1);
    }
    if (ls->cabinet_lights_[LIGHT_MARQUEE_UP_RIGHT]) {
      mm_set_ddr_cabinet_light(DDR_DOUBLE_MARQUEE_UPPER_RIGHT, 1);
    }
    if (ls->cabinet_lights_[LIGHT_MARQUEE_LR_LEFT]) {
      mm_set_ddr_cabinet_light(DDR_DOUBLE_MARQUEE_LOWER_LEFT, 1);
    }
    if (ls->cabinet_lights_[LIGHT_MARQUEE_LR_RIGHT]) {
      mm_set_ddr_cabinet_light(DDR_DOUBLE_MARQUEE_LOWER_RIGHT, 1);
    }
    if (ls->cabinet_lights_[LIGHT_BASS_LEFT] ||
        ls->cabinet_lights_[LIGHT_BASS_RIGHT]) {
      mm_set_ddr_bass_light(DDR_DOUBLE_BASS_LIGHTS, 1);
    }

    if (ls->game_button_lights_[GameController_1][DANCE_BUTTON_LEFT]) {
      mm_set_ddr_pad1_light(DDR_DOUBLE_PAD_LEFT, 1);
    }
    if (ls->game_button_lights_[GameController_1][DANCE_BUTTON_RIGHT]) {
      mm_set_ddr_pad1_light(DDR_DOUBLE_PAD_RIGHT, 1);
    }
    if (ls->game_button_lights_[GameController_1][DANCE_BUTTON_UP]) {
      mm_set_ddr_pad1_light(DDR_DOUBLE_PAD_UP, 1);
    }
    if (ls->game_button_lights_[GameController_1][DANCE_BUTTON_DOWN]) {
      mm_set_ddr_pad1_light(DDR_DOUBLE_PAD_DOWN, 1);
    }
    if (ls->game_button_lights_[GameController_1][GAME_BUTTON_START]) {
      mm_set_ddr_cabinet_light(DDR_DOUBLE_PLAYER1_PANEL, 1);
    }
    if (ls->game_button_lights_[GameController_2][DANCE_BUTTON_LEFT]) {
      mm_set_ddr_pad2_light(DDR_DOUBLE_PAD_LEFT, 1);
    }
    if (ls->game_button_lights_[GameController_2][DANCE_BUTTON_RIGHT]) {
      mm_set_ddr_pad2_light(DDR_DOUBLE_PAD_RIGHT, 1);
    }
    if (ls->game_button_lights_[GameController_2][DANCE_BUTTON_UP]) {
      mm_set_ddr_pad2_light(DDR_DOUBLE_PAD_UP, 1);
    }
    if (ls->game_button_lights_[GameController_2][DANCE_BUTTON_DOWN]) {
      mm_set_ddr_pad2_light(DDR_DOUBLE_PAD_DOWN, 1);
    }
    if (ls->game_button_lights_[GameController_2][GAME_BUTTON_START]) {
      mm_set_ddr_cabinet_light(DDR_DOUBLE_PLAYER2_PANEL, 1);
    }

    // Output the information
    mm_send_ddr_mini_maid_update();
  }
}

/*
 * (c) 2013 pkgingo
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
