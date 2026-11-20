#include "LightsDriver_snek.h"

#include <cstdint>

#include "GameInput.h"
#include "LightsManager.h"
#include "arch/Lights/LightsDriver.h"

REGISTER_LIGHTS_DRIVER_CLASS(snek);
REGISTER_AUTODETECT_LIGHTS_DRIVER_CLASS(snek);

bool LightsDriver_snek::ShouldAutoLoad() {
  return HidDevice::IsDevicePresent(
      SNEK_VID, SNEK_PID, SNEK_LIGHTING_INTERFACENUM);
}

LightsDriver_snek::LightsDriver_snek()
    : dev_{SNEK_VID, SNEK_PID, SNEK_LIGHTING_INTERFACENUM},
      state_changed_{false},
      output_buffer_{0} {}

LightsDriver_snek::~LightsDriver_snek() {}

void LightsDriver_snek::SetBuffer(int index, bool light_state) {
  // the first byte is the report ID, so we offset it here to adjust.
  uint8_t index_offset = index + 1;

  // each index in the array represents a single light,
  // the light will turn on for any value that isn't 0x00
  uint8_t val = light_state ? 0xFF : 0x00;

  // ensure report ID is set.
  output_buffer_[0] = SNEK_REPORT_ID;

  // ensure the index is valid and the light value has changed.
  if (index_offset < SNEK_HIDREPORT_SIZE &&
      output_buffer_[index_offset] != val) {
    output_buffer_[index_offset] = val;

    // signal the loop to push the new buffer to the device.
    state_changed_ = true;
  }
}

void LightsDriver_snek::Set(const LightsState* ls) {
  if (!dev_.FoundOnce()) {
    return;
  }

  SetBuffer(SNEK_INDEX_DANCE_M_UL, ls->cabinet_lights_[LIGHT_MARQUEE_UP_LEFT]);
  SetBuffer(SNEK_INDEX_DANCE_M_UR, ls->cabinet_lights_[LIGHT_MARQUEE_UP_RIGHT]);
  SetBuffer(SNEK_INDEX_DANCE_M_LL, ls->cabinet_lights_[LIGHT_MARQUEE_LR_LEFT]);
  SetBuffer(SNEK_INDEX_DANCE_M_LR, ls->cabinet_lights_[LIGHT_MARQUEE_LR_RIGHT]);

  SetBuffer(
      SNEK_INDEX_DANCE_P1_START,
      ls->game_button_lights_[GameController_1][GAME_BUTTON_START]);
  SetBuffer(
      SNEK_INDEX_DANCE_P2_START,
      ls->game_button_lights_[GameController_2][GAME_BUTTON_START]);

  SetBuffer(
      SNEK_INDEX_DANCE_NEON, ls->cabinet_lights_[LIGHT_BASS_LEFT] ||
                                 ls->cabinet_lights_[LIGHT_BASS_RIGHT]);

  SetBuffer(
      SNEK_INDEX_DANCE_P1_UP,
      ls->game_button_lights_[GameController_1][DANCE_BUTTON_UP]);
  SetBuffer(
      SNEK_INDEX_DANCE_P1_DOWN,
      ls->game_button_lights_[GameController_1][DANCE_BUTTON_DOWN]);
  SetBuffer(
      SNEK_INDEX_DANCE_P1_LEFT,
      ls->game_button_lights_[GameController_1][DANCE_BUTTON_LEFT]);
  SetBuffer(
      SNEK_INDEX_DANCE_P1_RIGHT,
      ls->game_button_lights_[GameController_1][DANCE_BUTTON_RIGHT]);

  SetBuffer(
      SNEK_INDEX_DANCE_P2_UP,
      ls->game_button_lights_[GameController_2][DANCE_BUTTON_UP]);
  SetBuffer(
      SNEK_INDEX_DANCE_P2_DOWN,
      ls->game_button_lights_[GameController_2][DANCE_BUTTON_DOWN]);
  SetBuffer(
      SNEK_INDEX_DANCE_P2_LEFT,
      ls->game_button_lights_[GameController_2][DANCE_BUTTON_LEFT]);
  SetBuffer(
      SNEK_INDEX_DANCE_P2_RIGHT,
      ls->game_button_lights_[GameController_2][DANCE_BUTTON_RIGHT]);

  // more lights for custom applications
  SetBuffer(
      SNEK_LIGHTINDEX_CN14_FL1,
      ls->game_button_lights_[GameController_1][GAME_BUTTON_MENULEFT]);
  SetBuffer(
      SNEK_LIGHTINDEX_CN14_FL2,
      ls->game_button_lights_[GameController_2][GAME_BUTTON_MENULEFT]);
  SetBuffer(
      SNEK_LIGHTINDEX_CN14_FL3,
      ls->game_button_lights_[GameController_1][GAME_BUTTON_SELECT]);
  SetBuffer(
      SNEK_LIGHTINDEX_CN14_FL4,
      ls->game_button_lights_[GameController_2][GAME_BUTTON_SELECT]);

  // only push on changes.
  if (state_changed_) {
    // TODO: Check for error/reconnect.
    dev_.Write((unsigned char*)&output_buffer_, SNEK_HIDREPORT_SIZE);
    state_changed_ = false;
  }
}
