#include "InputHandler_PumpHID.h"

#include <fcntl.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "EnumHelper.h"
#include "Game.h"
#include "GameInput.h"
#include "GameState.h"
#include "InputFilter.h"
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

REGISTER_INPUT_HANDLER_CLASS(PumpHID);
REGISTER_AUTODETECT_INPUT_HANDLER_CLASS(PumpHID);

// auto load the device if the device is plugged in.
bool InputHandler_PumpHID::ShouldAutoLoad() {
  return HidDevice::IsDevicePresent(
      PUMPHID_VID, kDevPids, PUMPHID_INTERFACE_NUM);
}

InputHandler_PumpHID::InputHandler_PumpHID() {
  // clear buffers etc.

  memset(msg_to_device_.raw_buff_, PUMPHID_DEF_LIGHTS, sizeof(msg_to_device_));
  memset(
      msg_from_device_.raw_buff_, PUMPHID_DEF_INPUT, sizeof(msg_from_device_));

  shutdown_ = false;

  if (IsConnected() && PREFSMAN->threaded_input_) {
    input_thread_.SetName("PumpHID thread");
    input_thread_.Create(InputThread_Start, this);
  }
}

InputHandler_PumpHID::~InputHandler_PumpHID() {
  // disconnect cleanup etc
  if (input_thread_.IsCreated()) {
    shutdown_ = true;
    LOG->Trace("Shutting down PumpHID thread ...");
    input_thread_.Wait();
    LOG->Trace("PumpHID thread shut down.");

    // turn off all of the lights manually.
    memset(
        msg_to_device_.raw_buff_, PUMPHID_DEF_LIGHTS, sizeof(msg_to_device_));
    dev_.Write(msg_to_device_.raw_buff_, PUMPHID_PAYLOADSIZE_TODEV);
  }
}

std::string InputHandler_PumpHID::GetDeviceSpecificInputString(
    const DeviceInput& di) {
  return InputHandler::GetDeviceSpecificInputString(di);
}

void InputHandler_PumpHID::GetDevicesAndDescriptions(
    std::vector<InputDeviceInfo>& devices_out) {
  devices_out.push_back(
      InputDeviceInfo(InputDevice(DEVICE_PUMPHID), "PumpHID"));
}

int InputHandler_PumpHID::InputThread_Start(void* p) {
  ((InputHandler_PumpHID*)p)->InputThreadMain();
  return 0;
}

void InputHandler_PumpHID::BroadcastFullSensorStateHelper(
    PlayerNumber pn, uint8_t sensor_index, pumphid_player_byte_t state) {
  PadSensor curr_sensor = PadSensor_TopCenter;

  switch (sensor_index) {
    case 0:
      curr_sensor = PadSensor_RightCenter;
      break;
    case 1:
      curr_sensor = PadSensor_LeftCenter;
      break;
    case 2:
      curr_sensor = PadSensor_BottomCenter;
      break;
    case 3:
      curr_sensor = PadSensor_TopCenter;
      break;
    default:
      LOG->Warn("Invalid sensor position %d", sensor_index);
      break;
  }

  // check to see which game we are running as it can change during gameplay.
  const InputScheme* input = &GAMESTATE->GetCurrentGame()->input_scheme_;
  std::string input_name = input->name_;

  // all buttons here are active low, so set to zero when "true"
  if (EqualsNoCase(input_name, "dance")) {
    INPUTFILTER->setFullSensorStateBinary(
        pn, PadPanel_Up, curr_sensor, !(state.btn_ul_u_));
    INPUTFILTER->setFullSensorStateBinary(
        pn, PadPanel_Down, curr_sensor, !(state.btn_ur_d_));
    INPUTFILTER->setFullSensorStateBinary(
        pn, PadPanel_Left, curr_sensor, !(state.btn_cn_l_));
    INPUTFILTER->setFullSensorStateBinary(
        pn, PadPanel_Right, curr_sensor, !(state.btn_ll_r_));

  } else if (EqualsNoCase(input_name, "pump")) {
    INPUTFILTER->setFullSensorStateBinary(
        pn, PadPanel_UpLeft, curr_sensor, !(state.btn_ul_u_));
    INPUTFILTER->setFullSensorStateBinary(
        pn, PadPanel_UpRight, curr_sensor, !(state.btn_ur_d_));
    INPUTFILTER->setFullSensorStateBinary(
        pn, PadPanel_Center, curr_sensor, !(state.btn_cn_l_));
    INPUTFILTER->setFullSensorStateBinary(
        pn, PadPanel_DownLeft, curr_sensor, !(state.btn_ll_r_));
    INPUTFILTER->setFullSensorStateBinary(
        pn, PadPanel_DownRight, curr_sensor, !(state.btn_lr_start_));
  }
}

void InputHandler_PumpHID::InputThreadMain() {
  uint32_t new_input = 0;
  LightsState new_ls = {};

  uint32_t prev_input = 0;
  LightsState prev_ls = {};

  pumphid_player_byte_t prev_p1_sensor[PUMP_HID_NUMOFSENSORS] = {};
  pumphid_player_byte_t prev_p2_sensor[PUMP_HID_NUMOFSENSORS] = {};

  while (!shutdown_) {
    new_ls = LightsDriver_Export::GetState();

    // even though we need to write to the PumpHID every cycle (otherwise it
    // will lock up) we don't need to make a new message every time because you
    // know it hasn't changed.
    if (prev_ls != new_ls) {
      CreateLightingMessage(new_ls);
      prev_ls = new_ls;
    }

    // hidapi always wants a report id for the device
    // yet the PumpHID device doesn't have one, so make sure it's always zero
    msg_to_device_.report_id_pad_ = 0;

    // push lighting state
    HidResults write_rtn =
        dev_.Write(msg_to_device_.raw_buff_, PUMPHID_PAYLOADSIZE_TODEV);

    if (write_rtn != HidResults::Success) {
      LOG->Warn("PumpHID write fail %d", write_rtn);
      continue;
    }

    // pull input state
    int read_rtn =
        dev_.Read(msg_from_device_.raw_buff_, PUMPHID_PAYLOADSIZE_FROMDEV);

    if (read_rtn != PUMPHID_PAYLOADSIZE_FROMDEV) {
      LOG->Warn("PumpHID read fail %d", read_rtn);
      continue;
    }

    // convert it to a local value.
    new_input = PumpHIDToLocalState(msg_from_device_);

    // don't flood the engine with states that are not different.
    if (prev_input != new_input) {
      PushInputStateToEngine(new_input);
      prev_input = new_input;
    }

    // we have to separately compare the sensor states in the event the whole
    // panel is being pressed, but the sensors in them have swapped.
    // we can't rely on PumpHIDToLocalState changes to fire off this event since
    // that decimates the sensor information into a button state.
    for (int i = 0; i < PUMP_HID_NUMOFSENSORS; i++) {
      if (msg_from_device_.p1_sensor_[i].raw_ != prev_p1_sensor[i].raw_) {
        BroadcastFullSensorStateHelper(
            PLAYER_1, i, msg_from_device_.p1_sensor_[i]);
        prev_p1_sensor[i] = msg_from_device_.p1_sensor_[i];
      }
      if (msg_from_device_.p2_sensor_[i].raw_ != prev_p2_sensor[i].raw_) {
        BroadcastFullSensorStateHelper(
            PLAYER_2, i, msg_from_device_.p2_sensor_[i]);
        prev_p2_sensor[i] = msg_from_device_.p2_sensor_[i];
      }
    }
  }
}

void InputHandler_PumpHID::CreateLightingMessage(LightsState new_ls) {
  // clear the state and recreate.
  memset(msg_to_device_.raw_buff_, PUMPHID_DEF_LIGHTS, sizeof(msg_to_device_));

  // lights are active high, so we can just set them as booleans.

  // check to see which game we are running as it can change during gameplay.
  const InputScheme* input = &GAMESTATE->GetCurrentGame()->input_scheme_;
  std::string input_name = input->name_;

  if (EqualsNoCase(input_name, "dance")) {
    msg_to_device_.lamp_p1_ul_ =
        new_ls.game_button_lights_[GameController_1][DANCE_BUTTON_UP];
    msg_to_device_.lamp_p1_ur_ =
        new_ls.game_button_lights_[GameController_1][DANCE_BUTTON_DOWN];
    msg_to_device_.lamp_p1_cn_ =
        new_ls.game_button_lights_[GameController_1][DANCE_BUTTON_LEFT];
    msg_to_device_.lamp_p1_ll_ =
        new_ls.game_button_lights_[GameController_1][DANCE_BUTTON_RIGHT];

    msg_to_device_.lamp_p2_ul_ =
        new_ls.game_button_lights_[GameController_2][DANCE_BUTTON_UP];
    msg_to_device_.lamp_p2_ur_ =
        new_ls.game_button_lights_[GameController_2][DANCE_BUTTON_DOWN];
    msg_to_device_.lamp_p2_cn_ =
        new_ls.game_button_lights_[GameController_2][DANCE_BUTTON_LEFT];
    msg_to_device_.lamp_p2_ll_ =
        new_ls.game_button_lights_[GameController_2][DANCE_BUTTON_RIGHT];
  } else if (EqualsNoCase(input_name, "pump")) {
    msg_to_device_.lamp_p1_ul_ =
        new_ls.game_button_lights_[GameController_1][PUMP_BUTTON_UPLEFT];
    msg_to_device_.lamp_p1_ur_ =
        new_ls.game_button_lights_[GameController_1][PUMP_BUTTON_UPRIGHT];
    msg_to_device_.lamp_p1_cn_ =
        new_ls.game_button_lights_[GameController_1][PUMP_BUTTON_CENTER];
    msg_to_device_.lamp_p1_ll_ =
        new_ls.game_button_lights_[GameController_1][PUMP_BUTTON_DOWNLEFT];
    msg_to_device_.lamp_p1_lr_ =
        new_ls.game_button_lights_[GameController_1][PUMP_BUTTON_DOWNRIGHT];

    msg_to_device_.lamp_p2_ul_ =
        new_ls.game_button_lights_[GameController_2][PUMP_BUTTON_UPLEFT];
    msg_to_device_.lamp_p2_ur_ =
        new_ls.game_button_lights_[GameController_2][PUMP_BUTTON_UPRIGHT];
    msg_to_device_.lamp_p2_cn_ =
        new_ls.game_button_lights_[GameController_2][PUMP_BUTTON_CENTER];
    msg_to_device_.lamp_p2_ll_ =
        new_ls.game_button_lights_[GameController_2][PUMP_BUTTON_DOWNLEFT];
    msg_to_device_.lamp_p2_lr_ =
        new_ls.game_button_lights_[GameController_2][PUMP_BUTTON_DOWNRIGHT];
  }

  // TODO how to handle Menu U/D/L/R vs lx cabs UR/UL/C etc
  // menu buttons.
  msg_to_device_.menu_p1_cn_ =
      new_ls.game_button_lights_[GameController_1][GAME_BUTTON_START];

  msg_to_device_.menu_p2_cn_ =
      new_ls.game_button_lights_[GameController_1][GAME_BUTTON_START];

  // cabinet lights
  msg_to_device_.lamp_mar_ul_ = new_ls.cabinet_lights_[LIGHT_MARQUEE_UP_LEFT];
  msg_to_device_.lamp_maq_ur_ = new_ls.cabinet_lights_[LIGHT_MARQUEE_UP_RIGHT];
  msg_to_device_.lamp_maq_ll_ = new_ls.cabinet_lights_[LIGHT_MARQUEE_LR_LEFT];
  msg_to_device_.lamp_maq_lr_ = new_ls.cabinet_lights_[LIGHT_MARQUEE_LR_RIGHT];

  msg_to_device_.lamp_neon_ = new_ls.cabinet_lights_[LIGHT_BASS_LEFT] ||
                              new_ls.cabinet_lights_[LIGHT_BASS_RIGHT];

  msg_to_device_.lamp_coin_pulse_ = new_ls.coin_counter_;

  // static values to match.
  msg_to_device_.lamp_alwayson0_ = true;
  msg_to_device_.lamp_alwayson1_ = true;
  msg_to_device_.lamp_alwaysoff0_ = false;
}

uint32_t InputHandler_PumpHID::PumpHIDToLocalState(
    pumphid_output_state_t from_dev) {
  // active low state to stepmania
  pumphid_to_stepmania_t state;
  state.raw_ = 0;

  // the device is active low, but active high easier to understand.
  // bit flip to make the message active high.
  for (int i = 0; i < PUMPHID_PAYLOADSIZE_FROMDEV; i++) {
    from_dev.raw_buff_[i] = ~from_dev.raw_buff_[i];
  }

  uint8_t p1 = from_dev.p1_sensor_[0].raw_ | from_dev.p1_sensor_[1].raw_ |
               from_dev.p1_sensor_[2].raw_ | from_dev.p1_sensor_[3].raw_;

  uint8_t p2 = from_dev.p2_sensor_[0].raw_ | from_dev.p2_sensor_[1].raw_ |
               from_dev.p2_sensor_[2].raw_ | from_dev.p2_sensor_[3].raw_;

  // newer firmwares of the pump hid are WEIRD about this, so just pull out the
  // bits we want test, coin, service, and clear from p1, and just coin from p2.
  uint8_t cab = 0;
  cab |= from_dev.cab0_.btn_test_ << 0;
  cab |= from_dev.cab0_.btn_service_ << 1;
  cab |= from_dev.cab0_.btn_clr_ << 2;
  cab |= from_dev.cab0_.btn_coin_ << 3;
  cab |= from_dev.cab1_.btn_coin_ << 4;

  // so let's jam this all of this info into 32bits.
  // we are reserving 8bits per player (which covers an itg cab with pumphid
  // upgrade, since buttons are in there.) five bits per menu (matches the lx
  // cab) and five for the cab state (test/service/clear/coin1/coin2)

  state.p1_ = p1;
  state.p2_ = p2;
  state.p1_menu_ = (from_dev.p1_menu_.raw_ & 0x1F);
  state.p2_menu_ = (from_dev.p2_menu_.raw_ & 0x1F);
  state.cab_ = (cab & 0x1F);

  // we have exactly one bit left over in this 32bit word.
  // don't spend it all in one place.

  return state.raw_;
}

void InputHandler_PumpHID::PushInputStateToEngine(std::uint32_t new_input) {
  for (int i = 0; i < 32; i++) {
    bool pressed = (new_input & (1 << i));

    DeviceInput di(DEVICE_PUMPHID, enum_add2(JOY_BUTTON_1, i), pressed);

    // If we're in a thread, our timestamp is accurate.
    if (input_thread_.IsCreated()) {
      di.ts_.Touch();
    }

    ButtonPressed(di);
  }
}
