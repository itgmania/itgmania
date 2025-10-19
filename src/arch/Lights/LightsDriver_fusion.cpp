#include "LightsDriver_fusion.h"

#include "Game.h"
#include "GameState.h"
#include "RageLog.h"
#include "global.h"

REGISTER_LIGHTS_DRIVER_CLASS(fusion);

LightsDriver_fusion::LightsDriver_fusion()
    : dev{FUSION_VID, FUSION_PID}, outputBuffer{0} {}

LightsDriver_fusion::~LightsDriver_fusion() {}

void LightsDriver_fusion::Set(const LightsState* ls) {
  if (!dev.FoundOnce()) {
    return;
  }

  outputBuffer[FUSION_REPORT_ID] = FUSION_LIGHTING_REPORTID;

  outputBuffer[FUSION_MAR_LR] =
      ls->m_bCabinetLights[LIGHT_MARQUEE_LR_RIGHT] ? 0xFF : 0x00;
  outputBuffer[FUSION_MAR_UR] =
      ls->m_bCabinetLights[LIGHT_MARQUEE_UP_RIGHT] ? 0xFF : 0x00;
  outputBuffer[FUSION_MAR_LL] =
      ls->m_bCabinetLights[LIGHT_MARQUEE_LR_LEFT] ? 0xFF : 0x00;
  outputBuffer[FUSION_MAR_UL] =
      ls->m_bCabinetLights[LIGHT_MARQUEE_UP_LEFT] ? 0xFF : 0x00;

  outputBuffer[FUSION_NEON] = (ls->m_bCabinetLights[LIGHT_BASS_LEFT] ||
                               ls->m_bCabinetLights[LIGHT_BASS_RIGHT])
                                  ? 0xFF
                                  : 0x00;

  // fusion does not have player button lights...

  // check to see which game we are running as it can change during gameplay.
  const InputScheme* pInput = &GAMESTATE->GetCurrentGame()->m_InputScheme;
  RString sInputName = pInput->m_szName;

  if (EqualsNoCase(sInputName, "dance")) {
    outputBuffer[FUSION_P1_UL] =
        ls->m_bGameButtonLights[GameController_1][DANCE_BUTTON_UP] ? 0xFF
                                                                   : 0x00;
    outputBuffer[FUSION_P1_UR] =
        ls->m_bGameButtonLights[GameController_1][DANCE_BUTTON_DOWN] ? 0xFF
                                                                     : 0x00;
    outputBuffer[FUSION_P1_CN] =
        ls->m_bGameButtonLights[GameController_1][DANCE_BUTTON_LEFT] ? 0xFF
                                                                     : 0x00;
    outputBuffer[FUSION_P1_LL] =
        ls->m_bGameButtonLights[GameController_1][DANCE_BUTTON_RIGHT] ? 0xFF
                                                                      : 0x00;

    outputBuffer[FUSION_P2_UL] =
        ls->m_bGameButtonLights[GameController_2][DANCE_BUTTON_UP] ? 0xFF
                                                                   : 0x00;
    outputBuffer[FUSION_P2_UR] =
        ls->m_bGameButtonLights[GameController_2][DANCE_BUTTON_DOWN] ? 0xFF
                                                                     : 0x00;
    outputBuffer[FUSION_P2_CN] =
        ls->m_bGameButtonLights[GameController_2][DANCE_BUTTON_LEFT] ? 0xFF
                                                                     : 0x00;
    outputBuffer[FUSION_P2_LL] =
        ls->m_bGameButtonLights[GameController_2][DANCE_BUTTON_RIGHT] ? 0xFF
                                                                      : 0x00;
  } else if (EqualsNoCase(sInputName, "pump")) {
    outputBuffer[FUSION_P1_UL] =
        ls->m_bGameButtonLights[GameController_1][PUMP_BUTTON_UPLEFT] ? 0xFF
                                                                      : 0x00;
    outputBuffer[FUSION_P1_UR] =
        ls->m_bGameButtonLights[GameController_1][PUMP_BUTTON_UPRIGHT] ? 0xFF
                                                                       : 0x00;
    outputBuffer[FUSION_P1_CN] =
        ls->m_bGameButtonLights[GameController_1][PUMP_BUTTON_CENTER] ? 0xFF
                                                                      : 0x00;
    outputBuffer[FUSION_P1_LL] =
        ls->m_bGameButtonLights[GameController_1][PUMP_BUTTON_DOWNLEFT] ? 0xFF
                                                                        : 0x00;
    outputBuffer[FUSION_P1_LR] =
        ls->m_bGameButtonLights[GameController_1][PUMP_BUTTON_DOWNRIGHT] ? 0xFF
                                                                         : 0x00;

    outputBuffer[FUSION_P2_UL] =
        ls->m_bGameButtonLights[GameController_2][PUMP_BUTTON_UPLEFT] ? 0xFF
                                                                      : 0x00;
    outputBuffer[FUSION_P2_UR] =
        ls->m_bGameButtonLights[GameController_2][PUMP_BUTTON_UPRIGHT] ? 0xFF
                                                                       : 0x00;
    outputBuffer[FUSION_P2_CN] =
        ls->m_bGameButtonLights[GameController_2][PUMP_BUTTON_CENTER] ? 0xFF
                                                                      : 0x00;
    outputBuffer[FUSION_P2_LL] =
        ls->m_bGameButtonLights[GameController_2][PUMP_BUTTON_DOWNLEFT] ? 0xFF
                                                                        : 0x00;
    outputBuffer[FUSION_P2_LR] =
        ls->m_bGameButtonLights[GameController_2][PUMP_BUTTON_DOWNRIGHT] ? 0xFF
                                                                         : 0x00;
  }

  // tying bass to led for debugging is fun!
  outputBuffer[FUSION_LED] = outputBuffer[FUSION_NEON];

  if (memcmp(outputBuffer, prevOutputBuffer, sizeof(outputBuffer)) != 0) {
    dev.Write((unsigned char*)&outputBuffer, FUSION_HID_LIGHTS_REPORT_SIZE);
    memcpy(prevOutputBuffer, outputBuffer, sizeof(prevOutputBuffer));
  }
}
