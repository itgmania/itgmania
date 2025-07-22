#include "LightsDriver_MinimaidHID.h"

#include "Game.h"
#include "GameState.h"
#include "RageLog.h"
#include "global.h"

REGISTER_LIGHTS_DRIVER_CLASS(MinimaidHID);

LightsDriver_MinimaidHID::LightsDriver_MinimaidHID()
    : dev{MM_VID, MM_PID, MM_LIGHTS_INTERFACE_NUM} {
  memset(outputReport.raw, 0x00, sizeof(outputReport.raw));
}

LightsDriver_MinimaidHID::~LightsDriver_MinimaidHID() {}

void LightsDriver_MinimaidHID::Set(const LightsState* ls) {
  if (!dev.FoundOnce()) {
    return;
  }

  mm_output_report_t newReport;
  memset(newReport.raw, 0, sizeof(newReport));

  // static report id.
  newReport.report_id = MM_REPORT_ID;

  // always ensure the keyboard mode is on.
  newReport.kb_enable = 1;

  // always ensure the pads are turned on
  // (this is FL5 on the schematics)
  newReport.lights.p1_pad_en = true;
  newReport.lights.p2_pad_en = true;

  // create the state.
  newReport.lights.mar_p1_lower = ls->m_bCabinetLights[LIGHT_MARQUEE_LR_LEFT];
  newReport.lights.mar_p2_lower = ls->m_bCabinetLights[LIGHT_MARQUEE_LR_RIGHT];
  newReport.lights.mar_p1_upper = ls->m_bCabinetLights[LIGHT_MARQUEE_UP_LEFT];
  newReport.lights.mar_p2_upper = ls->m_bCabinetLights[LIGHT_MARQUEE_UP_RIGHT];

  newReport.lights.neons = ls->m_bCabinetLights[LIGHT_BASS_LEFT] ||
                           ls->m_bCabinetLights[LIGHT_BASS_RIGHT];

  newReport.lights.p1_menu =
      ls->m_bGameButtonLights[PLAYER_1][GAME_BUTTON_START];
  newReport.lights.p2_menu =
      ls->m_bGameButtonLights[PLAYER_2][GAME_BUTTON_START];

  newReport.lights.p1_up = ls->m_bGameButtonLights[PLAYER_1][DANCE_BUTTON_UP];
  newReport.lights.p1_down =
      ls->m_bGameButtonLights[PLAYER_1][DANCE_BUTTON_DOWN];
  newReport.lights.p1_left =
      ls->m_bGameButtonLights[PLAYER_1][DANCE_BUTTON_LEFT];
  newReport.lights.p1_right =
      ls->m_bGameButtonLights[PLAYER_1][DANCE_BUTTON_RIGHT];

  newReport.lights.p2_up = ls->m_bGameButtonLights[PLAYER_2][DANCE_BUTTON_UP];
  newReport.lights.p2_down =
      ls->m_bGameButtonLights[PLAYER_2][DANCE_BUTTON_DOWN];
  newReport.lights.p2_left =
      ls->m_bGameButtonLights[PLAYER_2][DANCE_BUTTON_LEFT];
  newReport.lights.p2_right =
      ls->m_bGameButtonLights[PLAYER_2][DANCE_BUTTON_RIGHT];

  // tie the neons to the blue led for fun and debugging.
  newReport.blueled = newReport.lights.neons ? 0xFF : 0x00;

  // only push on changes.
  if (memcmp(&newReport.raw, &outputReport.raw, MM_OUTPUTREPORT_SIZE) != 0) {
    dev.Write((unsigned char*)&newReport.raw, MM_OUTPUTREPORT_SIZE);
    memcpy(&outputReport.raw, &newReport.raw, MM_OUTPUTREPORT_SIZE);
  }
}
