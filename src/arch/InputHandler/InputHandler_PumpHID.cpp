// do NOT let clang reposition these includes, because it will mess up the
// compile

// clang-format off
#include "global.h"
#include "InputHandler_PumpHID.h"
#include "PrefsManager.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "arch/Lights/LightsDriver_Export.h"
#include "GameState.h"
#include "Game.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <set>
#include <vector>

#if defined(HAVE_UNISTD_H)
#include <unistd.h>
#endif
#if defined(HAVE_FCNTL_H)
#include <fcntl.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>

#include "hidapi.h"
// clang-format on

const std::vector<int> InputHandler_PumpHID::devPIDS = {
    PUMPHID_PID_V1, PUMPHID_PID_V2};

REGISTER_INPUT_HANDLER_CLASS(PumpHID);

InputHandler_PumpHID::InputHandler_PumpHID() {
  // clear buffers etc.

  dev = new HidDevice(PUMPHID_VID, devPIDS, 0);

  memset(msg_to_device.raw_buff, PUMPHID_DEF_LIGHTS, sizeof(msg_to_device));
  memset(msg_from_device.raw_buff, PUMPHID_DEF_INPUT, sizeof(msg_from_device));

  m_bShutdown = false;

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

RString InputHandler_PumpHID::GetDeviceSpecificInputString(
    const DeviceInput& di) {
  return InputHandler::GetDeviceSpecificInputString(di);
}

void InputHandler_PumpHID::GetDevicesAndDescriptions(
    std::vector<InputDeviceInfo>& vDevicesOut) {
  // We use a joystick device so we can get automatic input mapping
  vDevicesOut.push_back(
      InputDeviceInfo(InputDevice(PUMPHID_DEVICEID), "PumpHID"));
}

int InputHandler_PumpHID::InputThread_Start(void* p) {
  ((InputHandler_PumpHID*)p)->InputThreadMain();
  return 0;
}

void InputHandler_PumpHID::InputThreadMain() {
  uint32_t newInput = 0;
  LightsState newLS;

  uint32_t prevInput = 0;
  LightsState prevLS;

  while (!m_bShutdown) {
    newLS = LightsDriver_Export::GetState();

    // even though we need to write to the PumpHID every cycle (otherwise it
    // will lock up) we don't need to make a new message every time because you
    // know it hasn't changed.
    if (IsLightChange(prevLS, newLS)) {
      CreateLightingMessage(newLS);
    }

    // hidapi always wants a report id for the device
    // yet the PumpHID device doesn't have one, so make sure it's always zero
    msg_to_device.report_id_pad = 0;

    // push lighting state
    dev->Write(msg_to_device.raw_buff, PUMPHID_PAYLOADSIZE_TODEV);

    // pull input state
    int rtn = dev->Read(msg_from_device.raw_buff, PUMPHID_PAYLOADSIZE_FROMDEV);
    newInput = PumpHIDToLocalState();

    // debugging pay no attention
    // LOG->Info("%d | %08x", rtn, newInput);

    // don't flood the engine with states that are not different.
    if (prevInput != newInput) {
      PushInputStateToEngine(newInput);
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
  RString sInputName = pInput->m_szName;

  if (EqualsNoCase(sInputName, "dance")) {
    msg_to_device.lamp_p1_ul =
        newLS.m_bGameButtonLights[GameController_1][DANCE_BUTTON_UP];
    msg_to_device.lamp_p1_ur =
        newLS.m_bGameButtonLights[GameController_1][DANCE_BUTTON_DOWN];
    msg_to_device.lamp_p1_c =
        newLS.m_bGameButtonLights[GameController_1][DANCE_BUTTON_LEFT];
    msg_to_device.lamp_p1_ll =
        newLS.m_bGameButtonLights[GameController_1][DANCE_BUTTON_RIGHT];

    msg_to_device.lamp_p2_ul =
        newLS.m_bGameButtonLights[GameController_2][DANCE_BUTTON_UP];
    msg_to_device.lamp_p2_ur =
        newLS.m_bGameButtonLights[GameController_2][DANCE_BUTTON_DOWN];
    msg_to_device.lamp_p2_c =
        newLS.m_bGameButtonLights[GameController_2][DANCE_BUTTON_LEFT];
    msg_to_device.lamp_p2_ll =
        newLS.m_bGameButtonLights[GameController_2][DANCE_BUTTON_RIGHT];
  } else if (EqualsNoCase(sInputName, "pump")) {
    msg_to_device.lamp_p1_ul =
        newLS.m_bGameButtonLights[GameController_1][PUMP_BUTTON_UPLEFT];
    msg_to_device.lamp_p1_ur =
        newLS.m_bGameButtonLights[GameController_1][PUMP_BUTTON_UPRIGHT];
    msg_to_device.lamp_p1_c =
        newLS.m_bGameButtonLights[GameController_1][PUMP_BUTTON_CENTER];
    msg_to_device.lamp_p1_ll =
        newLS.m_bGameButtonLights[GameController_1][PUMP_BUTTON_DOWNLEFT];
    msg_to_device.lamp_p1_lr =
        newLS.m_bGameButtonLights[GameController_1][PUMP_BUTTON_DOWNRIGHT];

    msg_to_device.lamp_p2_ul =
        newLS.m_bGameButtonLights[GameController_2][PUMP_BUTTON_UPLEFT];
    msg_to_device.lamp_p2_ur =
        newLS.m_bGameButtonLights[GameController_2][PUMP_BUTTON_UPRIGHT];
    msg_to_device.lamp_p2_c =
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

uint32_t InputHandler_PumpHID::PumpHIDToLocalState() {
  // device is active low, but stepmania is active high.
  for (int i = 0; i < PUMPHID_PAYLOADSIZE_FROMDEV; i++) {
    msg_from_device.raw_buff[i] = ~msg_from_device.raw_buff[i];
  }

  // TODO: expose the raw individual sensor values to lua to allow someone to
  // write a nice test input screen for debugging. See ITG3's theme and oITG for
  // inspiration.
  uint8_t p1 = msg_from_device.p1_sensor0.raw | msg_from_device.p1_sensor1.raw |
               msg_from_device.p1_sensor2.raw | msg_from_device.p1_sensor3.raw;

  uint8_t p2 = msg_from_device.p2_sensor0.raw | msg_from_device.p2_sensor1.raw |
               msg_from_device.p2_sensor2.raw | msg_from_device.p2_sensor3.raw;

  // newer firmwares of the pump hid are WEIRD about this, so just pull out the
  // bits we want test, coin, service, and clear from p1, and just coin from p2.
  uint8_t cab = 0;
  cab |= msg_from_device.cab0.btn_TEST << 0;
  cab |= msg_from_device.cab0.btn_SERVICE << 1;
  cab |= msg_from_device.cab0.btn_CLR << 2;
  cab |= msg_from_device.cab0.btn_COIN << 3;
  cab |= msg_from_device.cab1.btn_COIN << 4;

  // we should now have a clean active high message,
  // so let's concat it into a
  // local message.
  // squish it into bit packs of five.
  return (p1 << 0) | (p2 << (5)) | (msg_from_device.p1_menu.raw << (10)) |
         (msg_from_device.p2_menu.raw << (15)) | (cab << (20));
}

void InputHandler_PumpHID::PushInputStateToEngine(std::uint32_t newInput) {
  for (int i = 0; i < 32; i++) {
    bool pressed = (newInput & (1 << i));

    DeviceInput di(PUMPHID_DEVICEID, enum_add2(JOY_BUTTON_1, i), pressed);

    // If we're in a thread, our timestamp is accurate.
    if (InputThread.IsCreated()) {
      di.ts.Touch();
    }

    ButtonPressed(di);
  }
}

bool InputHandler_PumpHID::IsLightChange(
    LightsState prevLS, LightsState newLS) {
  FOREACH_CabinetLight(light) {
    if (prevLS.m_bCabinetLights[light] != newLS.m_bCabinetLights[light]) {
      return true;
    }
  }

  for (int gc = 0; gc < NUM_GameController; gc++) {
    FOREACH_ENUM(GameButton, gb) {
      if (prevLS.m_bGameButtonLights[gc][gb] !=
          newLS.m_bGameButtonLights[gc][gb]) {
        return true;
      }
    }
  }

  return false;
}
