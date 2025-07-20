#ifndef INPUT_HANDLER_PUMPHID
#define INPUT_HANDLER_PUMPHID

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
    bool lamp_p1_c : 1;
    bool lamp_p1_ll : 1;
    bool lamp_p1_lr : 1;
    bool lamp_pad0 : 1;

    // Byte 1: Neon lights
    bool pad0 : 1;
    bool pad1 : 1;
    bool lamp_neon : 1;
    bool pad3 : 1;
    bool pad4 : 1;
    bool pad5 : 1;
    bool lamp_led : 1;
    bool pad7 : 1;

    // Byte 2: P2 lamps
    uint8_t mux_setting_p2 : 2;
    bool lamp_p2_ul : 1;
    bool lamp_p2_ur : 1;
    bool lamp_p2_c : 1;
    bool lamp_p2_ll : 1;
    bool lamp_p2_lr : 1;
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
    bool pad5_p1 : 1;
    bool pad6_p1 : 1;
    bool pad7_p1 : 1;

    // Byte 5: P2 menu lamps
    bool menu_p2_ul : 1;
    bool menu_p2_ur : 1;
    bool menu_p2_cn : 1;
    bool menu_p2_ll : 1;
    bool menu_p2_lr : 1;
    bool pad5_p2 : 1;
    bool pad6_p2 : 1;
    bool pad7_p2 : 1;

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
