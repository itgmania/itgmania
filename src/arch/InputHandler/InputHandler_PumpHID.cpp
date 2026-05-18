#include "InputHandler_PumpHID.h"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "EnumHelper.h"
#include "Game.h"
#include "GameInput.h"
#include "GameState.h"
#include "InputMapper.h"
#include "LightsManager.h"
#include "PrefsManager.h"
#include "RageInputDevice.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "StdString.h"
#include "arch/InputHandler/InputHandler.h"
#include "arch/Lights/LightsDriver_Export.h"
#include "archutils/Common/HidDevice.h"

#if defined(HAVE_FCNTL_H)
#include <fcntl.h>
#endif

// all of the known device pid's that use this communication protocol.
const std::vector<int> InputHandler_PumpHID::devPIDS = {
    PUMPHID_PID_V1, PUMPHID_PID_V2};

REGISTER_INPUT_HANDLER_CLASS(PumpHID);

InputHandler_PumpHID::InputHandler_PumpHID() {
  // clear buffers etc.

  memset(msg_to_device.raw_buff, PUMPHID_DEF_LIGHTS, sizeof(msg_to_device));
  memset(msg_from_device.raw_buff, PUMPHID_DEF_INPUT, sizeof(msg_from_device));

  m_bShutdown = false;

  // ensure auto reconnect and blocking reads (since the device wants write/read
  // cycles properly.)
  dev = new HidDevice(PUMPHID_VID, devPIDS, PUMPHID_INTERFACE_NUM, true, false);

  if (IsConnected() && PREFSMAN->m_bThreadedInput) {
    InputThread.SetName("PumpHID thread");
    InputThread.Create(InputThread_Start, this);
  }
}

InputHandler_PumpHID::~InputHandler_PumpHID() {
  // disconnect cleanup etc
  if (InputThread.IsCreated()) {
    m_bShutdown = true;
    LOG->Trace("Shutting down PumpHID thread ...");
    InputThread.Wait();
    LOG->Trace("PumpHID thread shut down.");

    // turn off all of the lights manually.
    memset(msg_to_device.raw_buff, PUMPHID_DEF_LIGHTS, sizeof(msg_to_device));
    dev->Write(msg_to_device.raw_buff, PUMPHID_PAYLOADSIZE_TODEV);
  }
}

std::string InputHandler_PumpHID::GetDeviceSpecificInputString(
    const DeviceInput& di) {
  return InputHandler::GetDeviceSpecificInputString(di);
}

void InputHandler_PumpHID::GetDevicesAndDescriptions(
    std::vector<InputDeviceInfo>& vDevicesOut) {
  vDevicesOut.push_back(
      InputDeviceInfo(InputDevice(DEVICE_PUMPHID), "PumpHID"));
}

int InputHandler_PumpHID::InputThread_Start(void* p) {
  ((InputHandler_PumpHID*)p)->InputThreadMain();
  return 0;
}

void InputHandler_PumpHID::InputThreadMain() {
  uint32_t newInput = 0;
  LightsState newLS;

  uint32_t prevInput = 0;
  LightsState prevLS = {};

  while (!m_bShutdown) {
    newLS = LightsDriver_Export::GetState();

    // even though we need to write to the PumpHID every cycle (otherwise it
    // will lock up) we don't need to make a new message every time because you
    // know it hasn't changed.
    if (prevLS != newLS) {
      CreateLightingMessage(newLS);
    }

    // hidapi always wants a report id for the device
    // yet the PumpHID device doesn't have one, so make sure it's always zero
    msg_to_device.report_id_pad = 0;

    // push lighting state
    HidResults writeRtn =
        dev->Write(msg_to_device.raw_buff, PUMPHID_PAYLOADSIZE_TODEV);

    if (writeRtn != HidResults::Success) {
      LOG->Warn("PumpHID write fail %d", writeRtn);
      continue;
    }

    // pull input state
    int readRtn =
        dev->Read(msg_from_device.raw_buff, PUMPHID_PAYLOADSIZE_FROMDEV);

    if (readRtn != PUMPHID_PAYLOADSIZE_FROMDEV) {
      LOG->Warn("PumpHID read fail %d", readRtn);
      continue;
    }

    // convert it to a local value.
    newInput = PumpHIDToLocalState(msg_from_device);

    // don't flood the engine with states that are not different.
    if (prevInput != newInput) {
      PushInputStateToEngine(newInput);

      // debugging pay no attention
      // LOG->Info("%d | %08x", readRtn, newInput);
    }

    prevLS = newLS;
    prevInput = newInput;
  }
}

void InputHandler_PumpHID::CreateLightingMessage(LightsState newLS) {
  // clear the state and recreate.
  memset(msg_to_device.raw_buff, PUMPHID_DEF_LIGHTS, sizeof(msg_to_device));

  // lights are active high, so we can just set them as booleans.

  // check to see which game we are running as it can change during gameplay.
  const InputScheme* pInput = &GAMESTATE->GetCurrentGame()->m_InputScheme;
  std::string sInputName = pInput->m_szName;

  if (EqualsNoCase(sInputName, "dance")) {
    msg_to_device.lamp_p1_ul =
        newLS.m_bGameButtonLights[GameController_1][DANCE_BUTTON_UP];
    msg_to_device.lamp_p1_ur =
        newLS.m_bGameButtonLights[GameController_1][DANCE_BUTTON_DOWN];
    msg_to_device.lamp_p1_cn =
        newLS.m_bGameButtonLights[GameController_1][DANCE_BUTTON_LEFT];
    msg_to_device.lamp_p1_ll =
        newLS.m_bGameButtonLights[GameController_1][DANCE_BUTTON_RIGHT];

    msg_to_device.lamp_p2_ul =
        newLS.m_bGameButtonLights[GameController_2][DANCE_BUTTON_UP];
    msg_to_device.lamp_p2_ur =
        newLS.m_bGameButtonLights[GameController_2][DANCE_BUTTON_DOWN];
    msg_to_device.lamp_p2_cn =
        newLS.m_bGameButtonLights[GameController_2][DANCE_BUTTON_LEFT];
    msg_to_device.lamp_p2_ll =
        newLS.m_bGameButtonLights[GameController_2][DANCE_BUTTON_RIGHT];
  } else if (EqualsNoCase(sInputName, "pump")) {
    msg_to_device.lamp_p1_ul =
        newLS.m_bGameButtonLights[GameController_1][PUMP_BUTTON_UPLEFT];
    msg_to_device.lamp_p1_ur =
        newLS.m_bGameButtonLights[GameController_1][PUMP_BUTTON_UPRIGHT];
    msg_to_device.lamp_p1_cn =
        newLS.m_bGameButtonLights[GameController_1][PUMP_BUTTON_CENTER];
    msg_to_device.lamp_p1_ll =
        newLS.m_bGameButtonLights[GameController_1][PUMP_BUTTON_DOWNLEFT];
    msg_to_device.lamp_p1_lr =
        newLS.m_bGameButtonLights[GameController_1][PUMP_BUTTON_DOWNRIGHT];

    msg_to_device.lamp_p2_ul =
        newLS.m_bGameButtonLights[GameController_2][PUMP_BUTTON_UPLEFT];
    msg_to_device.lamp_p2_ur =
        newLS.m_bGameButtonLights[GameController_2][PUMP_BUTTON_UPRIGHT];
    msg_to_device.lamp_p2_cn =
        newLS.m_bGameButtonLights[GameController_2][PUMP_BUTTON_CENTER];
    msg_to_device.lamp_p2_ll =
        newLS.m_bGameButtonLights[GameController_2][PUMP_BUTTON_DOWNLEFT];
    msg_to_device.lamp_p2_lr =
        newLS.m_bGameButtonLights[GameController_2][PUMP_BUTTON_DOWNRIGHT];
  }

  // TODO how to handle Menu U/D/L/R vs lx cabs UR/UL/C etc
  // menu buttons.
  msg_to_device.menu_p1_cn =
      newLS.m_bGameButtonLights[GameController_1][GAME_BUTTON_START];

  msg_to_device.menu_p2_cn =
      newLS.m_bGameButtonLights[GameController_1][GAME_BUTTON_START];

  // cabinet lights
  msg_to_device.lamp_mar_ul = newLS.m_bCabinetLights[LIGHT_MARQUEE_UP_LEFT];
  msg_to_device.lamp_maq_ur = newLS.m_bCabinetLights[LIGHT_MARQUEE_UP_RIGHT];
  msg_to_device.lamp_maq_ll = newLS.m_bCabinetLights[LIGHT_MARQUEE_LR_LEFT];
  msg_to_device.lamp_maq_lr = newLS.m_bCabinetLights[LIGHT_MARQUEE_LR_RIGHT];

  msg_to_device.lamp_neon = newLS.m_bCabinetLights[LIGHT_BASS_LEFT] ||
                            newLS.m_bCabinetLights[LIGHT_BASS_RIGHT];

  msg_to_device.lamp_coin_pulse = newLS.m_bCoinCounter;

  // static values to match.
  msg_to_device.lamp_alwayson0 = true;
  msg_to_device.lamp_alwayson1 = true;
  msg_to_device.lamp_alwaysoff0 = false;
}

uint32_t InputHandler_PumpHID::PumpHIDToLocalState(
    pumphid_output_state_t from_dev) {
  // active low state to stepmania
  pumphid_to_stepmania_t state;
  state.raw = 0;

  // the device is active low, but active high easier to understand.
  // bit flip to make the message active high.
  for (int i = 0; i < PUMPHID_PAYLOADSIZE_FROMDEV; i++) {
    from_dev.raw_buff[i] = ~from_dev.raw_buff[i];
  }

  // TODO: expose the raw individual sensor values to lua to allow someone to
  // write a nice test input screen for debugging. See ITG3's theme and oITG for
  // inspiration.
  uint8_t p1 = from_dev.p1_sensor0.raw | from_dev.p1_sensor1.raw |
               from_dev.p1_sensor2.raw | from_dev.p1_sensor3.raw;

  uint8_t p2 = from_dev.p2_sensor0.raw | from_dev.p2_sensor1.raw |
               from_dev.p2_sensor2.raw | from_dev.p2_sensor3.raw;

  // newer firmwares of the pump hid are WEIRD about this, so just pull out the
  // bits we want test, coin, service, and clear from p1, and just coin from p2.
  uint8_t cab = 0;
  cab |= from_dev.cab0.btn_TEST << 0;
  cab |= from_dev.cab0.btn_SERVICE << 1;
  cab |= from_dev.cab0.btn_CLR << 2;
  cab |= from_dev.cab0.btn_COIN << 3;
  cab |= from_dev.cab1.btn_COIN << 4;

  // so let's jam this all of this info into 32bits.
  // we are reserving 8bits per player (which covers an itg cab with pumphid
  // upgrade, since buttons are in there.) five bits per menu (matches the lx
  // cab) and five for the cab state (test/service/clear/coin1/coin2)

  state.p1 = p1;
  state.p2 = p2;
  state.p1_menu = (from_dev.p1_menu.raw & 0x1F);
  state.p2_menu = (from_dev.p2_menu.raw & 0x1F);
  state.cab = (cab & 0x1F);

  // we have exactly one bit left over in this 32bit word.
  // don't spend it all in one place.

  return state.raw;
}

void InputHandler_PumpHID::PushInputStateToEngine(std::uint32_t newInput) {
  for (int i = 0; i < 32; i++) {
    bool pressed = (newInput & (1 << i));

    DeviceInput di(DEVICE_PUMPHID, enum_add2(JOY_BUTTON_1, i), pressed);

    // If we're in a thread, our timestamp is accurate.
    if (InputThread.IsCreated()) {
      di.ts.Touch();
    }

    ButtonPressed(di);
  }
}
