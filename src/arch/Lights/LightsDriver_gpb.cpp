#include "LightsDriver_gpb.h"

#include "Game.h"
#include "GameState.h"
#include "RageLog.h"
#include "global.h"

REGISTER_LIGHTS_DRIVER_CLASS(gpb);

LightsDriver_gpb::LightsDriver_gpb()
    : dev{GPB_VID, GPB_PID, GPB_LIGHTING_INTERFACENUM}, outputBuffer{0} {}

LightsDriver_gpb::~LightsDriver_gpb() {}

void LightsDriver_gpb::Set(const LightsState* ls) {
  if (!dev.FoundOnce()) {
    return;
  }

  outputBuffer[GPB_LIGHTINDEX_REPORT_ID] = GPB_LIGHTING_REPORTID;

  // btns are:
  // p1: red, left, right, green
  // p2: red, left, right, green
  // p1 up, down
  // p2 up, down

  outputBuffer[GPB_LIGHTINDEX_BTN01] =
      ls->m_bGameButtonLights[GameController_1][GAME_BUTTON_SELECT];
  outputBuffer[GPB_LIGHTINDEX_BTN02] =
      ls->m_bGameButtonLights[GameController_1][GAME_BUTTON_LEFT];
  outputBuffer[GPB_LIGHTINDEX_BTN03] =
      ls->m_bGameButtonLights[GameController_1][GAME_BUTTON_RIGHT];
  outputBuffer[GPB_LIGHTINDEX_BTN04] =
      ls->m_bGameButtonLights[GameController_1][GAME_BUTTON_START];

  outputBuffer[GPB_LIGHTINDEX_BTN05] =
      ls->m_bGameButtonLights[GameController_2][GAME_BUTTON_SELECT];
  outputBuffer[GPB_LIGHTINDEX_BTN06] =
      ls->m_bGameButtonLights[GameController_2][GAME_BUTTON_LEFT];
  outputBuffer[GPB_LIGHTINDEX_BTN07] =
      ls->m_bGameButtonLights[GameController_2][GAME_BUTTON_RIGHT];
  outputBuffer[GPB_LIGHTINDEX_BTN08] =
      ls->m_bGameButtonLights[GameController_2][GAME_BUTTON_START];

  outputBuffer[GPB_LIGHTINDEX_BTN09] =
      ls->m_bGameButtonLights[GameController_1][GAME_BUTTON_UP];
  outputBuffer[GPB_LIGHTINDEX_BTN10] =
      ls->m_bGameButtonLights[GameController_1][GAME_BUTTON_DOWN];

  outputBuffer[GPB_LIGHTINDEX_BTN11] =
      ls->m_bGameButtonLights[GameController_2][GAME_BUTTON_UP];
  outputBuffer[GPB_LIGHTINDEX_BTN12] =
      ls->m_bGameButtonLights[GameController_2][GAME_BUTTON_DOWN];

  if (memcmp(outputBuffer, prevOutputBuffer, sizeof(outputBuffer)) != 0) {
    dev.Write((unsigned char*)&outputBuffer, GPB_HID_LIGHTS_REPORT_SIZE);
    memcpy(prevOutputBuffer, outputBuffer, sizeof(prevOutputBuffer));
  }
}
