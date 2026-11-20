#include "LightsDriver_MinimaidHID.h"

#include <cstring>

#include "GameInput.h"
#include "LightsManager.h"
#include "PlayerNumber.h"
#include "arch/Lights/LightsDriver.h"

REGISTER_LIGHTS_DRIVER_CLASS(MinimaidHID);
REGISTER_AUTODETECT_LIGHTS_DRIVER_CLASS(MinimaidHID);
// Register MinimaidHID under alias name: LinuxMinimaid for backward
// compatibility
REGISTER_LIGHTS_DRIVER_ALIAS(LinuxMinimaid, MinimaidHID);

bool LightsDriver_MinimaidHID::ShouldAutoLoad() {
  return HidDevice::IsDevicePresent(MM_VID, MM_PID, MM_LIGHTS_INTERFACE_NUM);
}

LightsDriver_MinimaidHID::LightsDriver_MinimaidHID()
    : dev_{MM_VID, MM_PID, MM_LIGHTS_INTERFACE_NUM} {}

LightsDriver_MinimaidHID::~LightsDriver_MinimaidHID() {
  mm_lighting_state_ddr_t new_lights;
  new_lights.raw_ = 0;

  // on exit clear the lights
  sendReport(new_lights);
}

void LightsDriver_MinimaidHID::sendReport(mm_lighting_state_ddr_t lights) {
  mm_output_report_t new_report;
  memset(new_report.raw_, 0, sizeof(new_report));

  // static report id.
  new_report.report_id_ = MM_REPORT_ID;

  // always ensure the keyboard mode is on.
  new_report.kb_enable_ = 1;

  // copy the lights in
  new_report.lights_.raw_ = lights.raw_;

  // always ensure the pads are turned on by overring these.
  // (this is FL5 on the schematics)
  new_report.lights_.p1_pad_en_ = true;
  new_report.lights_.p2_pad_en_ = true;

  // tie the neons to the blue led for fun and debugging.
  new_report.blueled_ = new_report.lights_.neons_ ? 0xFF : 0x00;

  dev_.Write((unsigned char*)&new_report.raw_, MM_OUTPUTREPORT_SIZE);
}

void LightsDriver_MinimaidHID::Set(const LightsState* ls) {
  if (!dev_.FoundOnce()) {
    return;
  }

  mm_lighting_state_ddr_t new_lights;
  new_lights.raw_ = 0;

  // create the state.
  new_lights.mar_p1_lower_ = ls->cabinet_lights_[LIGHT_MARQUEE_LR_LEFT];
  new_lights.mar_p2_lower_ = ls->cabinet_lights_[LIGHT_MARQUEE_LR_RIGHT];
  new_lights.mar_p1_upper_ = ls->cabinet_lights_[LIGHT_MARQUEE_UP_LEFT];
  new_lights.mar_p2_upper_ = ls->cabinet_lights_[LIGHT_MARQUEE_UP_RIGHT];

  new_lights.neons_ = ls->cabinet_lights_[LIGHT_BASS_LEFT] ||
                      ls->cabinet_lights_[LIGHT_BASS_RIGHT];

  new_lights.p1_menu_ = ls->game_button_lights_[PLAYER_1][GAME_BUTTON_START];
  new_lights.p2_menu_ = ls->game_button_lights_[PLAYER_2][GAME_BUTTON_START];

  new_lights.p1_up_ = ls->game_button_lights_[PLAYER_1][DANCE_BUTTON_UP];
  new_lights.p1_down_ = ls->game_button_lights_[PLAYER_1][DANCE_BUTTON_DOWN];
  new_lights.p1_left_ = ls->game_button_lights_[PLAYER_1][DANCE_BUTTON_LEFT];
  new_lights.p1_right_ = ls->game_button_lights_[PLAYER_1][DANCE_BUTTON_RIGHT];

  new_lights.p2_up_ = ls->game_button_lights_[PLAYER_2][DANCE_BUTTON_UP];
  new_lights.p2_down_ = ls->game_button_lights_[PLAYER_2][DANCE_BUTTON_DOWN];
  new_lights.p2_left_ = ls->game_button_lights_[PLAYER_2][DANCE_BUTTON_LEFT];
  new_lights.p2_right_ = ls->game_button_lights_[PLAYER_2][DANCE_BUTTON_RIGHT];

  // only push on changes.
  if (prev_lights_.raw_ != new_lights.raw_) {
    sendReport(new_lights);
    prev_lights_.raw_ = new_lights.raw_;
  }
}
