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
    : dev{MM_VID, MM_PID, MM_LIGHTS_INTERFACE_NUM} {}

LightsDriver_MinimaidHID::~LightsDriver_MinimaidHID() {
  mm_lighting_state_ddr_t newLights;
  newLights.raw = 0;

  // on exit clear the lights
  sendReport(newLights);
}

void LightsDriver_MinimaidHID::sendReport(mm_lighting_state_ddr_t lights) {
  mm_output_report_t newReport;
  memset(newReport.raw, 0, sizeof(newReport));

  // static report id.
  newReport.report_id = MM_REPORT_ID;

  // always ensure the keyboard mode is on.
  newReport.kb_enable = 1;

  // copy the lights in
  newReport.lights.raw = lights.raw;

  // always ensure the pads are turned on by overring these.
  // (this is FL5 on the schematics)
  newReport.lights.p1_pad_en = true;
  newReport.lights.p2_pad_en = true;

  // tie the neons to the blue led for fun and debugging.
  newReport.blueled = newReport.lights.neons ? 0xFF : 0x00;

  dev.Write((unsigned char*)&newReport.raw, MM_OUTPUTREPORT_SIZE);
}

void LightsDriver_MinimaidHID::Set(const LightsState* ls) {
  if (!dev.FoundOnce()) {
    return;
  }

  mm_lighting_state_ddr_t newLights;
  newLights.raw = 0;

  // create the state.
  newLights.mar_p1_lower = ls->m_bCabinetLights[LIGHT_MARQUEE_LR_LEFT];
  newLights.mar_p2_lower = ls->m_bCabinetLights[LIGHT_MARQUEE_LR_RIGHT];
  newLights.mar_p1_upper = ls->m_bCabinetLights[LIGHT_MARQUEE_UP_LEFT];
  newLights.mar_p2_upper = ls->m_bCabinetLights[LIGHT_MARQUEE_UP_RIGHT];

  newLights.neons = ls->m_bCabinetLights[LIGHT_BASS_LEFT] ||
                    ls->m_bCabinetLights[LIGHT_BASS_RIGHT];

  newLights.p1_menu = ls->m_bGameButtonLights[PLAYER_1][GAME_BUTTON_START];
  newLights.p2_menu = ls->m_bGameButtonLights[PLAYER_2][GAME_BUTTON_START];

  newLights.p1_up = ls->m_bGameButtonLights[PLAYER_1][DANCE_BUTTON_UP];
  newLights.p1_down = ls->m_bGameButtonLights[PLAYER_1][DANCE_BUTTON_DOWN];
  newLights.p1_left = ls->m_bGameButtonLights[PLAYER_1][DANCE_BUTTON_LEFT];
  newLights.p1_right = ls->m_bGameButtonLights[PLAYER_1][DANCE_BUTTON_RIGHT];

  newLights.p2_up = ls->m_bGameButtonLights[PLAYER_2][DANCE_BUTTON_UP];
  newLights.p2_down = ls->m_bGameButtonLights[PLAYER_2][DANCE_BUTTON_DOWN];
  newLights.p2_left = ls->m_bGameButtonLights[PLAYER_2][DANCE_BUTTON_LEFT];
  newLights.p2_right = ls->m_bGameButtonLights[PLAYER_2][DANCE_BUTTON_RIGHT];

  // only push on changes.
  if (prevLights.raw != newLights.raw) {
    sendReport(newLights);
    prevLights.raw = newLights.raw;
  }
}
