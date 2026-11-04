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

LightsDriver_stac2::LightsDriver_stac2()
    : devs_{{STAC2_VID, STAC2_PID_P1, STAC2_LIGHTING_INTERFACE}, {STAC2_VID, STAC2_PID_P2, STAC2_LIGHTING_INTERFACE}},
      state_changed_{false},
      output_buffer_{{0}} {}

LightsDriver_stac2::~LightsDriver_stac2() {}

void LightsDriver_stac2::SetBuffer(
    int index, bool lightState, GameController ctrlNum) {
  // the first byte is the report ID, so we offset it here to adjust.
  uint8_t index_offset = index + 1;

  // each index in the array represents a single light,
  // the light will turn on for any value that isn't 0x00
  uint8_t val = lightState ? 0xFF : 0x00;

  // ensure report ID is set.
  output_buffer_[ctrlNum][0] = STAC2_REPORT_ID;

  // ensure the index is valid and the light value has changed.
  if (index_offset < STAC2_HIDREPORT_SIZE &&
      output_buffer_[ctrlNum][index_offset] != val) {
    output_buffer_[ctrlNum][index_offset] = val;

    // signal the loop to push the new buffer to the device.
    state_changed_[ctrlNum] = true;
  }
}

void LightsDriver_stac2::HandleState(
    const LightsState* ls, GameController ctrlNum) {
  // do not create a message for an disconnected device.
  if (!devs_[ctrlNum].FoundOnce()) {
    return;
  }

  // check to see which game we are running as it can change during gameplay.
  const InputScheme* pInput = &GAMESTATE->GetCurrentGame()->input_scheme_;
  std::string sInputName = pInput->name_;

  if (EqualsNoCase(sInputName, "dance")) {
    SetBuffer(
        STAC2_LIGHTINDEX_BTN1,
        ls->game_button_lights_[ctrlNum][DANCE_BUTTON_UP], ctrlNum);
    SetBuffer(
        STAC2_LIGHTINDEX_BTN2,
        ls->game_button_lights_[ctrlNum][DANCE_BUTTON_DOWN], ctrlNum);
    SetBuffer(
        STAC2_LIGHTINDEX_BTN3,
        ls->game_button_lights_[ctrlNum][DANCE_BUTTON_LEFT], ctrlNum);
    SetBuffer(
        STAC2_LIGHTINDEX_BTN4,
        ls->game_button_lights_[ctrlNum][DANCE_BUTTON_RIGHT], ctrlNum);
  } else if (EqualsNoCase(sInputName, "pump")) {
    SetBuffer(
        STAC2_LIGHTINDEX_BTN1,
        ls->game_button_lights_[ctrlNum][PUMP_BUTTON_UPLEFT], ctrlNum);
    SetBuffer(
        STAC2_LIGHTINDEX_BTN2,
        ls->game_button_lights_[ctrlNum][PUMP_BUTTON_UPRIGHT], ctrlNum);
    SetBuffer(
        STAC2_LIGHTINDEX_BTN3,
        ls->game_button_lights_[ctrlNum][PUMP_BUTTON_CENTER], ctrlNum);
    SetBuffer(
        STAC2_LIGHTINDEX_BTN4,
        ls->game_button_lights_[ctrlNum][PUMP_BUTTON_DOWNLEFT], ctrlNum);
    SetBuffer(
        STAC2_LIGHTINDEX_BTN5,
        ls->game_button_lights_[ctrlNum][PUMP_BUTTON_DOWNRIGHT], ctrlNum);
  }

  // only push changes.
  if (state_changed_[ctrlNum]) {
    devs_[ctrlNum].Write(
        (unsigned char*)&output_buffer_[ctrlNum], STAC2_HIDREPORT_SIZE);
    state_changed_[ctrlNum] = false;
  }
}

void LightsDriver_stac2::Set(const LightsState* ls) {
  HandleState(ls, GameController_1);
  HandleState(ls, GameController_2);
}
