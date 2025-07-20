#ifndef INPUT_HANDLER_PUMPHID
#define INPUT_HANDLER_PUMPHID

/*
 * -------------------------- NOTE --------------------------
 *
 * This driver needs user read/write access the device.
 * This can be achieved by using a udev rule like this:
 *
 * SUBSYSTEMS=="usb", ATTRS{idVendor}=="0d2f", ATTRS{idProduct}=="1020",
 * OWNER="dance", GROUP="dance", MODE="0660"
 *
 * or
 *
 * KERNEL=="hidraw*", ATTRS{idVendor}=="0d2f", ATTRS{idProduct}=="1020",
 * OWNER="dance", GROUP="dance", MODE="0660"
 *
 * Refer to your distribution's documentation on how to properly apply a udev
 * rule.
 *
 * -------------------------- NOTE --------------------------
 */

#include <vector>

#include "InputHandler.h"
#include "RageThreads.h"
#include "arch/Lights/LightsDriver_Export.h"
#include "archutils/Common/HidDevice.h"

#define PUMPHID_DEVICEID DEVICE_JOY1
#define PUMPHID_PAYLOADSIZE_FROMDEV 16

// hidapi wants a blank report ID, but since this device doesn't use that
// we have to add a byte just to compensate.
#define PUMPHID_PAYLOADSIZE_TODEV (16 + 1)

// active high lights, active low input.
#define PUMPHID_DEF_LIGHTS 0x00
#define PUMPHID_DEF_INPUT 0xFF

// two known device IDs that have shipped.
#define PUMPHID_VID 0x0D2F
#define PUMPHID_PID_V1 0x1020
#define PUMPHID_PID_V2 0x1040

#define PUMPHID_INTERFACE_NUM 0

#pragma pack(push, 1)

typedef union {
  struct {
    bool btn_UL_U : 1;
    bool btn_UR_D : 1;
    bool btn_CN_L : 1;
    bool btn_LL_R : 1;
    bool btn_LR_START : 1;
    bool btn_SELECT : 1;
    bool btn_MENU_LEFT : 1;
    bool btn_MENU_RIGHT : 1;
  };
  uint8_t raw;
} pumphid_player_byte_t;

typedef union {
  struct {
    bool pad0 : 1;
    bool btn_TEST : 1;
    bool btn_COIN : 1;
    bool pad3 : 1;
    bool pad4 : 1;
    bool pad5 : 1;
    bool btn_SERVICE : 1;
    bool btn_CLR : 1;
  };
  uint8_t raw;
} pumphid_cabinet_byte_t;

typedef union {
  struct {
    bool btn_MENU_UL : 1;
    bool btn_MENU_UR : 1;
    bool btn_MENU_CN : 1;
    bool btn_MENU_LL : 1;
    bool btn_MENU_LR : 1;
    bool pad5 : 1;
    bool pad6 : 1;
    bool pad7 : 1;
  };
  uint8_t raw;
} pumphid_menu_byte_t;

typedef union {
  struct {
    pumphid_player_byte_t p1_sensor0;
    pumphid_player_byte_t p1_sensor1;
    pumphid_player_byte_t p1_sensor2;
    pumphid_player_byte_t p1_sensor3;

    pumphid_player_byte_t p2_sensor0;
    pumphid_player_byte_t p2_sensor1;
    pumphid_player_byte_t p2_sensor2;
    pumphid_player_byte_t p2_sensor3;

    pumphid_cabinet_byte_t cab0;
    pumphid_cabinet_byte_t cab1;

    pumphid_menu_byte_t p1_menu;
    pumphid_menu_byte_t p2_menu;

    uint8_t byte12;
    uint8_t byte13;
    // TODO: Figure out what this counter does. Increments on every packet?
    uint16_t counter;
  };
  uint8_t raw_buff[PUMPHID_PAYLOADSIZE_FROMDEV];
} pumphid_output_state_t;

typedef union {
  struct {
    // fake padding byte for hidapi
    uint8_t report_id_pad = 0;

    // Byte 0: P1 lamps
    uint8_t mux_setting_p1 : 2;
    bool lamp_p1_ul : 1;
    bool lamp_p1_ur : 1;
    bool lamp_p1_cn : 1;
    bool lamp_p1_ll : 1;
    bool lamp_p1_lr : 1;
    bool lamp_p1_pad7 : 1;

    // Byte 1: Neon lights
    bool pad0 : 1;
    bool pad1 : 1;
    bool lamp_neon : 1;
    bool pad3 : 1;
    bool pad4 : 1;
    bool pad5 : 1;
    bool pad6 : 1;
    bool pad7 : 1;

    // Byte 2: P2 lamps
    uint8_t mux_setting_p2 : 2;
    bool lamp_p2_ul : 1;
    bool lamp_p2_ur : 1;
    bool lamp_p2_cn : 1;
    bool lamp_p2_ll : 1;
    bool lamp_p2_lr : 1;
    // yes this is a cab lamp in a player byte... not a mistake, it's the
    // hardware.
    bool lamp_mar_ul : 1;

    // Byte 3: Cabinet lamps
    bool lamp_maq_lr : 1;
    bool lamp_maq_ll : 1;
    bool lamp_maq_ur : 1;
    bool lamp_coin_pulse : 1;
    bool lamp_usb_en : 1;
    bool lamp_alwayson0 : 1;
    bool lamp_alwayson1 : 1;
    bool lamp_alwaysoff0 : 1;

    // Byte 4: P1 menu lamps
    bool menu_p1_ul : 1;
    bool menu_p1_ur : 1;
    bool menu_p1_cn : 1;
    bool menu_p1_ll : 1;
    bool menu_p1_lr : 1;
    bool menu_p1_pad5 : 1;
    bool menu_p1_pad6 : 1;
    bool menu_p1_pad7 : 1;

    // Byte 5: P2 menu lamps
    bool menu_p2_ul : 1;
    bool menu_p2_ur : 1;
    bool menu_p2_cn : 1;
    bool menu_p2_ll : 1;
    bool menu_p2_lr : 1;
    bool menu_p2_pad5 : 1;
    bool menu_p2_pad6 : 1;
    bool menu_p2_pad7 : 1;

    // Byte 6–15: Reserved/Raw
    uint8_t byte6;
    uint8_t byte7;
    uint8_t byte8;
    uint8_t byte9;
    uint8_t byte10;
    uint8_t byte11;
    uint8_t byte12;
    uint8_t byte13;
    uint8_t byte14;
    uint8_t byte15;
  };
  uint8_t raw_buff[PUMPHID_PAYLOADSIZE_TODEV];
} pumphid_input_state_t;

#pragma pack(pop)

// ensure the sizes are correct
static_assert(
    sizeof(pumphid_input_state_t) == PUMPHID_PAYLOADSIZE_TODEV,
    "pumphid_input_state_t != PUMPHID_PAYLOADSIZE_TODEV");

static_assert(
    sizeof(pumphid_output_state_t) == PUMPHID_PAYLOADSIZE_FROMDEV,
    "pumphid_output_state_t != PUMPHID_PAYLOADSIZE_FROMDEV");

class InputHandler_PumpHID : public InputHandler {
 public:
  InputHandler_PumpHID();
  ~InputHandler_PumpHID();

  RString GetDeviceSpecificInputString(const DeviceInput& di);
  void GetDevicesAndDescriptions(std::vector<InputDeviceInfo>& vDevicesOut);

  bool IsConnected() { return dev != nullptr && dev->IsConnected(); }

 private:
  HidDevice* dev;
  static const std::vector<int> devPIDS;

  pumphid_output_state_t msg_from_device;
  pumphid_input_state_t msg_to_device;

  bool m_bShutdown;
  RageThread InputThread;

  uint32_t PumpHIDToLocalState();

  void PushInputStateToEngine(std::uint32_t newInput);

  static int InputThread_Start(void* p);
  void InputThreadMain();

  bool IsLightChange(LightsState prevLS, LightsState newLS);
  void CreateLightingMessage(LightsState newLS);
};

#endif

/*
 * (c) 2025 din
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
