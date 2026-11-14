#include "LightsDriver_fusion.h"

#include <cstring>
#include <string>

#include "Game.h"
#include "GameInput.h"
#include "GameState.h"
#include "InputMapper.h"
#include "LightsManager.h"
#include "StdString.h"
#include "arch/Lights/LightsDriver.h"

REGISTER_LIGHTS_DRIVER_CLASS(fusion);
REGISTER_AUTODETECT_LIGHTS_DRIVER_CLASS(fusion);

bool LightsDriver_fusion::ShouldAutoLoad() {
  return HidDevice::IsDevicePresent(FUSION_VID, FUSION_PID);
}

LightsDriver_fusion::LightsDriver_fusion()
    : dev_{FUSION_VID, FUSION_PID}, output_buffer_{0} {}

LightsDriver_fusion::~LightsDriver_fusion() {}

void LightsDriver_fusion::Set(const LightsState* ls) {
  if (!dev_.FoundOnce()) {
    return;
  }

  output_buffer_[FUSION_REPORT_ID] = FUSION_LIGHTING_REPORTID;

  output_buffer_[FUSION_MAR_LR] =
      ls->cabinet_lights_[LIGHT_MARQUEE_LR_RIGHT] ? 0xFF : 0x00;
  output_buffer_[FUSION_MAR_UR] =
      ls->cabinet_lights_[LIGHT_MARQUEE_UP_RIGHT] ? 0xFF : 0x00;
  output_buffer_[FUSION_MAR_LL] =
      ls->cabinet_lights_[LIGHT_MARQUEE_LR_LEFT] ? 0xFF : 0x00;
  output_buffer_[FUSION_MAR_UL] =
      ls->cabinet_lights_[LIGHT_MARQUEE_UP_LEFT] ? 0xFF : 0x00;

  output_buffer_[FUSION_NEON] = (ls->cabinet_lights_[LIGHT_BASS_LEFT] ||
                                 ls->cabinet_lights_[LIGHT_BASS_RIGHT])
                                    ? 0xFF
                                    : 0x00;

  // fusion does not have player button lights...

  // check to see which game we are running as it can change during gameplay.
  const InputScheme* input = &GAMESTATE->GetCurrentGame()->input_scheme_;
  std::string input_name = input->name_;

  if (EqualsNoCase(input_name, "dance")) {
    output_buffer_[FUSION_P1_UL] =
        ls->game_button_lights_[GameController_1][DANCE_BUTTON_UP] ? 0xFF
                                                                   : 0x00;
    output_buffer_[FUSION_P1_UR] =
        ls->game_button_lights_[GameController_1][DANCE_BUTTON_DOWN] ? 0xFF
                                                                     : 0x00;
    output_buffer_[FUSION_P1_CN] =
        ls->game_button_lights_[GameController_1][DANCE_BUTTON_LEFT] ? 0xFF
                                                                     : 0x00;
    output_buffer_[FUSION_P1_LL] =
        ls->game_button_lights_[GameController_1][DANCE_BUTTON_RIGHT] ? 0xFF
                                                                      : 0x00;

    output_buffer_[FUSION_P2_UL] =
        ls->game_button_lights_[GameController_2][DANCE_BUTTON_UP] ? 0xFF
                                                                   : 0x00;
    output_buffer_[FUSION_P2_UR] =
        ls->game_button_lights_[GameController_2][DANCE_BUTTON_DOWN] ? 0xFF
                                                                     : 0x00;
    output_buffer_[FUSION_P2_CN] =
        ls->game_button_lights_[GameController_2][DANCE_BUTTON_LEFT] ? 0xFF
                                                                     : 0x00;
    output_buffer_[FUSION_P2_LL] =
        ls->game_button_lights_[GameController_2][DANCE_BUTTON_RIGHT] ? 0xFF
                                                                      : 0x00;
  } else if (EqualsNoCase(input_name, "pump")) {
    output_buffer_[FUSION_P1_UL] =
        ls->game_button_lights_[GameController_1][PUMP_BUTTON_UPLEFT] ? 0xFF
                                                                      : 0x00;
    output_buffer_[FUSION_P1_UR] =
        ls->game_button_lights_[GameController_1][PUMP_BUTTON_UPRIGHT] ? 0xFF
                                                                       : 0x00;
    output_buffer_[FUSION_P1_CN] =
        ls->game_button_lights_[GameController_1][PUMP_BUTTON_CENTER] ? 0xFF
                                                                      : 0x00;
    output_buffer_[FUSION_P1_LL] =
        ls->game_button_lights_[GameController_1][PUMP_BUTTON_DOWNLEFT] ? 0xFF
                                                                        : 0x00;
    output_buffer_[FUSION_P1_LR] =
        ls->game_button_lights_[GameController_1][PUMP_BUTTON_DOWNRIGHT] ? 0xFF
                                                                         : 0x00;

    output_buffer_[FUSION_P2_UL] =
        ls->game_button_lights_[GameController_2][PUMP_BUTTON_UPLEFT] ? 0xFF
                                                                      : 0x00;
    output_buffer_[FUSION_P2_UR] =
        ls->game_button_lights_[GameController_2][PUMP_BUTTON_UPRIGHT] ? 0xFF
                                                                       : 0x00;
    output_buffer_[FUSION_P2_CN] =
        ls->game_button_lights_[GameController_2][PUMP_BUTTON_CENTER] ? 0xFF
                                                                      : 0x00;
    output_buffer_[FUSION_P2_LL] =
        ls->game_button_lights_[GameController_2][PUMP_BUTTON_DOWNLEFT] ? 0xFF
                                                                        : 0x00;
    output_buffer_[FUSION_P2_LR] =
        ls->game_button_lights_[GameController_2][PUMP_BUTTON_DOWNRIGHT] ? 0xFF
                                                                         : 0x00;
  }

  output_buffer_[FUSION_COIN_PULSE] = ls->coin_counter_ ? 0xFF : 0x00;

  // tying bass to led for debugging is fun!
  output_buffer_[FUSION_LED] = output_buffer_[FUSION_NEON];

  if (memcmp(output_buffer_, prev_output_buffer_, sizeof(output_buffer_)) !=
      0) {
    dev_.Write((unsigned char*)&output_buffer_, FUSION_HID_LIGHTS_REPORT_SIZE);
    memcpy(prev_output_buffer_, output_buffer_, sizeof(prev_output_buffer_));
  }
}
