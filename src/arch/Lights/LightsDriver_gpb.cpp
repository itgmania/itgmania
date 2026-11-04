#include "LightsDriver_gpb.h"

#include "Game.h"
#include "GameState.h"
#include "RageLog.h"
#include "global.h"

REGISTER_LIGHTS_DRIVER_CLASS(gpb);

LightsDriver_gpb::LightsDriver_gpb()
    : dev_{GPB_VID, GPB_PID, GPB_LIGHTING_INTERFACENUM}, output_buffer_{0} {}

LightsDriver_gpb::~LightsDriver_gpb() {}

void LightsDriver_gpb::Set(const LightsState* ls) {
  if (!dev_.FoundOnce()) {
    return;
  }

  output_buffer_[GPB_LIGHTINDEX_REPORT_ID] = GPB_LIGHTING_REPORTID;

  // btns are:
  // p1: red, left, right, green
  // p2: red, left, right, green
  // p1 up, down
  // p2 up, down

  output_buffer_[GPB_LIGHTINDEX_BTN01] =
      ls->game_button_lights_[GameController_1][GAME_BUTTON_SELECT];
  output_buffer_[GPB_LIGHTINDEX_BTN02] =
      ls->game_button_lights_[GameController_1][GAME_BUTTON_LEFT];
  output_buffer_[GPB_LIGHTINDEX_BTN03] =
      ls->game_button_lights_[GameController_1][GAME_BUTTON_RIGHT];
  output_buffer_[GPB_LIGHTINDEX_BTN04] =
      ls->game_button_lights_[GameController_1][GAME_BUTTON_START];

  output_buffer_[GPB_LIGHTINDEX_BTN05] =
      ls->game_button_lights_[GameController_2][GAME_BUTTON_SELECT];
  output_buffer_[GPB_LIGHTINDEX_BTN06] =
      ls->game_button_lights_[GameController_2][GAME_BUTTON_LEFT];
  output_buffer_[GPB_LIGHTINDEX_BTN07] =
      ls->game_button_lights_[GameController_2][GAME_BUTTON_RIGHT];
  output_buffer_[GPB_LIGHTINDEX_BTN08] =
      ls->game_button_lights_[GameController_2][GAME_BUTTON_START];

  output_buffer_[GPB_LIGHTINDEX_BTN09] =
      ls->game_button_lights_[GameController_1][GAME_BUTTON_UP];
  output_buffer_[GPB_LIGHTINDEX_BTN10] =
      ls->game_button_lights_[GameController_1][GAME_BUTTON_DOWN];

  output_buffer_[GPB_LIGHTINDEX_BTN11] =
      ls->game_button_lights_[GameController_2][GAME_BUTTON_UP];
  output_buffer_[GPB_LIGHTINDEX_BTN12] =
      ls->game_button_lights_[GameController_2][GAME_BUTTON_DOWN];

  if (memcmp(output_buffer_, prev_output_buffer_, sizeof(output_buffer_)) !=
      0) {
    dev_.Write((unsigned char*)&output_buffer_, GPB_HID_LIGHTS_REPORT_SIZE);
    memcpy(prev_output_buffer_, output_buffer_, sizeof(prev_output_buffer_));
  }
}
