#include "LightsDriver_MinimaidHID.h"

#include <cstring>

#include "GameInput.h"
#include "LightsManager.h"
#include "PlayerNumber.h"
#include "arch/Lights/LightsDriver.h"

REGISTER_LIGHTS_DRIVER_CLASS(MinimaidHID);
// Register MinimaidHID under alias name: LinuxMinimaid for backward
// compatibility
REGISTER_LIGHTS_DRIVER_ALIAS(LinuxMinimaid, MinimaidHID);

LightsDriver_MinimaidHID::LightsDriver_MinimaidHID()
    : dev_{MM_VID, MM_PID, MM_LIGHTS_INTERFACE_NUM} {}

LightsDriver_MinimaidHID::~LightsDriver_MinimaidHID() {
  mm_lighting_state_ddr_t newLights;
  newLights.raw_ = 0;

  // on exit clear the lights
  sendReport(newLights);
}

void LightsDriver_MinimaidHID::sendReport(mm_lighting_state_ddr_t lights) {
  mm_output_report_t newReport;
  memset(newReport.raw_, 0, sizeof(newReport));

  // static report id.
  newReport.report_id_ = MM_REPORT_ID;

  // always ensure the keyboard mode is on.
  newReport.kb_enable_ = 1;

  // copy the lights in
  newReport.lights_.raw_ = lights.raw_;

  // always ensure the pads are turned on by overring these.
  // (this is FL5 on the schematics)
  newReport.lights_.p1_pad_en_ = true;
  newReport.lights_.p2_pad_en_ = true;

  // tie the neons to the blue led for fun and debugging.
  newReport.blueled_ = newReport.lights_.neons_ ? 0xFF : 0x00;

  dev_.Write((unsigned char*)&newReport.raw_, MM_OUTPUTREPORT_SIZE);
}

void LightsDriver_MinimaidHID::Set(const LightsState* ls) {
  if (!dev_.FoundOnce()) {
    return;
  }

  mm_lighting_state_ddr_t newLights;
  newLights.raw_ = 0;

  // create the state.
  newLights.mar_p1_lower_ = ls->cabinet_lights_[LIGHT_MARQUEE_LR_LEFT];
  newLights.mar_p2_lower_ = ls->cabinet_lights_[LIGHT_MARQUEE_LR_RIGHT];
  newLights.mar_p1_upper_ = ls->cabinet_lights_[LIGHT_MARQUEE_UP_LEFT];
  newLights.mar_p2_upper_ = ls->cabinet_lights_[LIGHT_MARQUEE_UP_RIGHT];

  newLights.neons_ = ls->cabinet_lights_[LIGHT_BASS_LEFT] ||
                     ls->cabinet_lights_[LIGHT_BASS_RIGHT];

  newLights.p1_menu_ = ls->game_button_lights_[PLAYER_1][GAME_BUTTON_START];
  newLights.p2_menu_ = ls->game_button_lights_[PLAYER_2][GAME_BUTTON_START];

  newLights.p1_up_ = ls->game_button_lights_[PLAYER_1][DANCE_BUTTON_UP];
  newLights.p1_down_ = ls->game_button_lights_[PLAYER_1][DANCE_BUTTON_DOWN];
  newLights.p1_left_ = ls->game_button_lights_[PLAYER_1][DANCE_BUTTON_LEFT];
  newLights.p1_right_ = ls->game_button_lights_[PLAYER_1][DANCE_BUTTON_RIGHT];

  newLights.p2_up_ = ls->game_button_lights_[PLAYER_2][DANCE_BUTTON_UP];
  newLights.p2_down_ = ls->game_button_lights_[PLAYER_2][DANCE_BUTTON_DOWN];
  newLights.p2_left_ = ls->game_button_lights_[PLAYER_2][DANCE_BUTTON_LEFT];
  newLights.p2_right_ = ls->game_button_lights_[PLAYER_2][DANCE_BUTTON_RIGHT];

  // only push on changes.
  if (prev_lights_.raw_ != newLights.raw_) {
    sendReport(newLights);
    prev_lights_.raw_ = newLights.raw_;
  }
}
