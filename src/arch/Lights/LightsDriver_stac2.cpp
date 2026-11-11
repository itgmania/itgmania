#include "LightsDriver_stac2.h"

#include <cstdint>
#include <string>

#include "Game.h"
#include "GameInput.h"
#include "GameState.h"
#include "InputMapper.h"
#include "LightsManager.h"
#include "StdString.h"
#include "arch/Lights/LightsDriver.h"

REGISTER_LIGHTS_DRIVER_CLASS(stac2);
REGISTER_AUTODETECT_LIGHTS_DRIVER_CLASS(stac2);

bool LightsDriver_stac2::ShouldAutoLoad() {
  return HidDevice::IsDevicePresent(
      STAC2_VID, {STAC2_PID_P1, STAC2_PID_P2}, STAC2_LIGHTING_INTERFACE);
}

LightsDriver_stac2::LightsDriver_stac2()
    : devs_{
          {STAC2_VID, STAC2_PID_P1, STAC2_LIGHTING_INTERFACE},
          {STAC2_VID, STAC2_PID_P2, STAC2_LIGHTING_INTERFACE}},
      state_changed_{false},
      output_buffer_{{0}} {}

LightsDriver_stac2::~LightsDriver_stac2() {}

void LightsDriver_stac2::SetBuffer(
    int index, bool light_state, GameController ctrl_num) {
  // the first byte is the report ID, so we offset it here to adjust.
  uint8_t index_offset = index + 1;

  // each index in the array represents a single light,
  // the light will turn on for any value that isn't 0x00
  uint8_t val = light_state ? 0xFF : 0x00;

  // ensure report ID is set.
  output_buffer_[ctrl_num][0] = STAC2_REPORT_ID;

  // ensure the index is valid and the light value has changed.
  if (index_offset < STAC2_HIDREPORT_SIZE &&
      output_buffer_[ctrl_num][index_offset] != val) {
    output_buffer_[ctrl_num][index_offset] = val;

    // signal the loop to push the new buffer to the device.
    state_changed_[ctrl_num] = true;
  }
}

void LightsDriver_stac2::HandleState(
    const LightsState* ls, GameController ctrl_num) {
  // do not create a message for an disconnected device.
  if (!devs_[ctrl_num].FoundOnce()) {
    return;
  }

  // check to see which game we are running as it can change during gameplay.
  const InputScheme* input = &GAMESTATE->GetCurrentGame()->input_scheme_;
  std::string input_name = input->name_;

  if (EqualsNoCase(input_name, "dance")) {
    SetBuffer(
        STAC2_LIGHTINDEX_BTN1,
        ls->game_button_lights_[ctrl_num][DANCE_BUTTON_UP], ctrl_num);
    SetBuffer(
        STAC2_LIGHTINDEX_BTN2,
        ls->game_button_lights_[ctrl_num][DANCE_BUTTON_DOWN], ctrl_num);
    SetBuffer(
        STAC2_LIGHTINDEX_BTN3,
        ls->game_button_lights_[ctrl_num][DANCE_BUTTON_LEFT], ctrl_num);
    SetBuffer(
        STAC2_LIGHTINDEX_BTN4,
        ls->game_button_lights_[ctrl_num][DANCE_BUTTON_RIGHT], ctrl_num);
  } else if (EqualsNoCase(input_name, "pump")) {
    SetBuffer(
        STAC2_LIGHTINDEX_BTN1,
        ls->game_button_lights_[ctrl_num][PUMP_BUTTON_UPLEFT], ctrl_num);
    SetBuffer(
        STAC2_LIGHTINDEX_BTN2,
        ls->game_button_lights_[ctrl_num][PUMP_BUTTON_UPRIGHT], ctrl_num);
    SetBuffer(
        STAC2_LIGHTINDEX_BTN3,
        ls->game_button_lights_[ctrl_num][PUMP_BUTTON_CENTER], ctrl_num);
    SetBuffer(
        STAC2_LIGHTINDEX_BTN4,
        ls->game_button_lights_[ctrl_num][PUMP_BUTTON_DOWNLEFT], ctrl_num);
    SetBuffer(
        STAC2_LIGHTINDEX_BTN5,
        ls->game_button_lights_[ctrl_num][PUMP_BUTTON_DOWNRIGHT], ctrl_num);
  }

  // only push changes.
  if (state_changed_[ctrl_num]) {
    devs_[ctrl_num].Write(
        (unsigned char*)&output_buffer_[ctrl_num], STAC2_HIDREPORT_SIZE);
    state_changed_[ctrl_num] = false;
  }
}

void LightsDriver_stac2::Set(const LightsState* ls) {
  HandleState(ls, GameController_1);
  HandleState(ls, GameController_2);
}
