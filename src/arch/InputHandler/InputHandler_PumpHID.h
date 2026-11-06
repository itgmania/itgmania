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

#include <cstdint>
#include <string>
#include <vector>

#include "InputHandler.h"
#include "LightsManager.h"
#include "PlayerNumber.h"
#include "RageInputDevice.h"
#include "RageThreads.h"
#include "archutils/Common/HidDevice.h"

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

#define PUMP_HID_NUMOFSENSORS 4

#pragma pack(push, 1)

typedef union {
  struct {
    bool btn_ul_u_ : 1;
    bool btn_ur_d_ : 1;
    bool btn_cn_l_ : 1;
    bool btn_ll_r_ : 1;
    bool btn_lr_start_ : 1;
    bool btn_select_ : 1;
    bool btn_menu_left_ : 1;
    bool btn_menu_right_ : 1;
  };
  uint8_t raw_;
} pumphid_player_byte_t;

typedef union {
  struct {
    bool pad0_ : 1;
    bool btn_test_ : 1;
    bool btn_coin_ : 1;
    bool pad3_ : 1;
    bool pad4_ : 1;
    bool pad5_ : 1;
    bool btn_service_ : 1;
    bool btn_clr_ : 1;
  };
  uint8_t raw_;
} pumphid_cabinet_byte_t;

typedef union {
  struct {
    bool btn_menu_ul_ : 1;
    bool btn_menu_ur_ : 1;
    bool btn_menu_cn_ : 1;
    bool btn_menu_ll_ : 1;
    bool btn_menu_lr_ : 1;
    bool pad5_ : 1;
    bool pad6_ : 1;
    bool pad7_ : 1;
  };
  uint8_t raw_;
} pumphid_menu_byte_t;

typedef union {
  struct {
    pumphid_player_byte_t p1_sensor_[PUMP_HID_NUMOFSENSORS];
    pumphid_player_byte_t p2_sensor_[PUMP_HID_NUMOFSENSORS];

    pumphid_cabinet_byte_t cab0_;
    pumphid_cabinet_byte_t cab1_;

    pumphid_menu_byte_t p1_menu_;
    pumphid_menu_byte_t p2_menu_;

    uint8_t byte12_;
    uint8_t byte13_;
    // TODO: Figure out what this counter does. Increments on every packet?
    uint16_t counter_;
  };
  uint8_t raw_buff_[PUMPHID_PAYLOADSIZE_FROMDEV];
} pumphid_output_state_t;

typedef union {
  struct {
    // fake padding byte for hidapi
    uint8_t report_id_pad_;

    // Byte 0: P1 lamps
    uint8_t mux_setting_p1_ : 2;
    bool lamp_p1_ul_ : 1;
    bool lamp_p1_ur_ : 1;
    bool lamp_p1_cn_ : 1;
    bool lamp_p1_ll_ : 1;
    bool lamp_p1_lr_ : 1;
    bool lamp_p1_pad7_ : 1;

    // Byte 1: Neon lights
    bool pad0_ : 1;
    bool pad1_ : 1;
    bool lamp_neon_ : 1;
    bool pad3_ : 1;
    bool pad4_ : 1;
    bool pad5_ : 1;
    bool pad6_ : 1;
    bool pad7_ : 1;

    // Byte 2: P2 lamps
    uint8_t mux_setting_p2_ : 2;
    bool lamp_p2_ul_ : 1;
    bool lamp_p2_ur_ : 1;
    bool lamp_p2_cn_ : 1;
    bool lamp_p2_ll_ : 1;
    bool lamp_p2_lr_ : 1;
    // yes this is a cab lamp in a player byte... not a mistake, it's the
    // hardware.
    bool lamp_mar_ul_ : 1;

    // Byte 3: Cabinet lamps
    bool lamp_maq_lr_ : 1;
    bool lamp_maq_ll_ : 1;
    bool lamp_maq_ur_ : 1;
    bool lamp_coin_pulse_ : 1;
    bool lamp_usb_en_ : 1;
    bool lamp_alwayson0_ : 1;
    bool lamp_alwayson1_ : 1;
    bool lamp_alwaysoff0_ : 1;

    // Byte 4: P1 menu lamps
    bool menu_p1_ul_ : 1;
    bool menu_p1_ur_ : 1;
    bool menu_p1_cn_ : 1;
    bool menu_p1_ll_ : 1;
    bool menu_p1_lr_ : 1;
    bool menu_p1_pad5_ : 1;
    bool menu_p1_pad6_ : 1;
    bool menu_p1_pad7_ : 1;

    // Byte 5: P2 menu lamps
    bool menu_p2_ul_ : 1;
    bool menu_p2_ur_ : 1;
    bool menu_p2_cn_ : 1;
    bool menu_p2_ll_ : 1;
    bool menu_p2_lr_ : 1;
    bool menu_p2_pad5_ : 1;
    bool menu_p2_pad6_ : 1;
    bool menu_p2_pad7_ : 1;

    // Byte 6–15: Reserved/Raw
    uint8_t byte6_;
    uint8_t byte7_;
    uint8_t byte8_;
    uint8_t byte9_;
    uint8_t byte10_;
    uint8_t byte11_;
    uint8_t byte12_;
    uint8_t byte13_;
    uint8_t byte14_;
    uint8_t byte15_;
  };
  uint8_t raw_buff_[PUMPHID_PAYLOADSIZE_TODEV];
} pumphid_input_state_t;

// used to convert all of this info to 31 buttons for stepmania.
typedef union {
  struct {
    uint16_t p1_ : 8;
    uint16_t p2_ : 8;

    uint16_t p1_menu_ : 5;
    uint16_t p2_menu_ : 5;

    uint16_t cab_ : 5;

    // one extra bit in question.
    uint16_t padding_ : 1;
  };
  uint32_t raw_;
} pumphid_to_stepmania_t;

#pragma pack(pop)

// ensure the sizes are correct
static_assert(
    sizeof(pumphid_input_state_t) == PUMPHID_PAYLOADSIZE_TODEV,
    "pumphid_input_state_t != PUMPHID_PAYLOADSIZE_TODEV");

static_assert(
    sizeof(pumphid_output_state_t) == PUMPHID_PAYLOADSIZE_FROMDEV,
    "pumphid_output_state_t != PUMPHID_PAYLOADSIZE_FROMDEV");

// ensure that we are making a 32bit word for stepmania, 32=8*4
static_assert(
    sizeof(pumphid_to_stepmania_t) == 4, "pumphid_to_stepmania_t != 4");

class InputHandler_PumpHID : public InputHandler {
 public:
  InputHandler_PumpHID();
  ~InputHandler_PumpHID();

  std::string GetDeviceSpecificInputString(const DeviceInput& di);
  void GetDevicesAndDescriptions(std::vector<InputDeviceInfo>& vDevicesOut);

  bool IsConnected() { return dev_.IsConnected(); }

 private:
  // all of the known device pid's that use this communication protocol.
  inline static const std::vector<int> kDevPids = {
      PUMPHID_PID_V1, PUMPHID_PID_V2};

  // ensure auto reconnect and blocking reads (since the device wants write/read
  // cycles properly.)
  HidDevice dev_ =
      HidDevice(PUMPHID_VID, kDevPids, PUMPHID_INTERFACE_NUM, true, false);

  pumphid_output_state_t msg_from_device_;
  pumphid_input_state_t msg_to_device_;

  bool shutdown_;
  RageThread input_thread_;

  void BroadcastFullSensorStateHelper(
      PlayerNumber pn, uint8_t sensor_index, pumphid_player_byte_t state);

  uint32_t PumpHIDToLocalState(pumphid_output_state_t from_dev);

  void PushInputStateToEngine(std::uint32_t newInput);

  static int InputThread_Start(void* p);
  void InputThreadMain();

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
