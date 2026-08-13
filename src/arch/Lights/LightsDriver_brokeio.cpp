#include "LightsDriver_brokeio.h"

#include <cstring>
#include <string>

#include "Game.h"
#include "GameInput.h"
#include "GameState.h"
#include "InputMapper.h"
#include "LightsManager.h"
#include "StdString.h"
#include "arch/Lights/LightsDriver.h"

REGISTER_LIGHTS_DRIVER_CLASS(brokeio);

LightsDriver_brokeio::LightsDriver_brokeio()
    : dev{BROKEIO_VID, BROKEIO_PID}, outputBuffer{0} {}

LightsDriver_brokeio::~LightsDriver_brokeio() {}

void LightsDriver_brokeio::Set(const LightsState* ls) {
  if (!dev.FoundOnce()) {
    return;
  }

  outputBuffer[BROKEIO_REPORT_ID] = BROKEIO_LIGHTING_REPORTID;

  outputBuffer[BROKEIO_MAR_LR] =
      ls->m_bCabinetLights[LIGHT_MARQUEE_LR_RIGHT] ? 0xFF : 0x00;
  outputBuffer[BROKEIO_MAR_UR] =
      ls->m_bCabinetLights[LIGHT_MARQUEE_UP_RIGHT] ? 0xFF : 0x00;
  outputBuffer[BROKEIO_MAR_LL] =
      ls->m_bCabinetLights[LIGHT_MARQUEE_LR_LEFT] ? 0xFF : 0x00;
  outputBuffer[BROKEIO_MAR_UL] =
      ls->m_bCabinetLights[LIGHT_MARQUEE_UP_LEFT] ? 0xFF : 0x00;

  outputBuffer[BROKEIO_NEON] = (ls->m_bCabinetLights[LIGHT_BASS_LEFT] ||
                                ls->m_bCabinetLights[LIGHT_BASS_RIGHT])
                                   ? 0xFF
                                   : 0x00;

  outputBuffer[BROKEIO_COIN_PULSE] = ls->m_bCoinCounter ? 0xFF : 0x00;

  // check to see which game we are running as it can change during gameplay.
  const InputScheme* pInput = &GAMESTATE->GetCurrentGame()->m_InputScheme;
  std::string sInputName = pInput->m_szName;

  if (EqualsNoCase(sInputName, "dance")) {
    outputBuffer[BROKEIO_P1_UL] =
        ls->m_bGameButtonLights[GameController_1][DANCE_BUTTON_UP] ? 0xFF
                                                                   : 0x00;
    outputBuffer[BROKEIO_P1_UR] =
        ls->m_bGameButtonLights[GameController_1][DANCE_BUTTON_DOWN] ? 0xFF
                                                                     : 0x00;
    outputBuffer[BROKEIO_P1_CN] =
        ls->m_bGameButtonLights[GameController_1][DANCE_BUTTON_LEFT] ? 0xFF
                                                                     : 0x00;
    outputBuffer[BROKEIO_P1_LL] =
        ls->m_bGameButtonLights[GameController_1][DANCE_BUTTON_RIGHT] ? 0xFF
                                                                      : 0x00;

    outputBuffer[BROKEIO_P2_UL] =
        ls->m_bGameButtonLights[GameController_2][DANCE_BUTTON_UP] ? 0xFF
                                                                   : 0x00;
    outputBuffer[BROKEIO_P2_UR] =
        ls->m_bGameButtonLights[GameController_2][DANCE_BUTTON_DOWN] ? 0xFF
                                                                     : 0x00;
    outputBuffer[BROKEIO_P2_CN] =
        ls->m_bGameButtonLights[GameController_2][DANCE_BUTTON_LEFT] ? 0xFF
                                                                     : 0x00;
    outputBuffer[BROKEIO_P2_LL] =
        ls->m_bGameButtonLights[GameController_2][DANCE_BUTTON_RIGHT] ? 0xFF
                                                                      : 0x00;
  } else if (EqualsNoCase(sInputName, "pump")) {
    outputBuffer[BROKEIO_P1_UL] =
        ls->m_bGameButtonLights[GameController_1][PUMP_BUTTON_UPLEFT] ? 0xFF
                                                                      : 0x00;
    outputBuffer[BROKEIO_P1_UR] =
        ls->m_bGameButtonLights[GameController_1][PUMP_BUTTON_UPRIGHT] ? 0xFF
                                                                       : 0x00;
    outputBuffer[BROKEIO_P1_CN] =
        ls->m_bGameButtonLights[GameController_1][PUMP_BUTTON_CENTER] ? 0xFF
                                                                      : 0x00;
    outputBuffer[BROKEIO_P1_LL] =
        ls->m_bGameButtonLights[GameController_1][PUMP_BUTTON_DOWNLEFT] ? 0xFF
                                                                        : 0x00;
    outputBuffer[BROKEIO_P1_LR] =
        ls->m_bGameButtonLights[GameController_1][PUMP_BUTTON_DOWNRIGHT] ? 0xFF
                                                                         : 0x00;

    outputBuffer[BROKEIO_P2_UL] =
        ls->m_bGameButtonLights[GameController_2][PUMP_BUTTON_UPLEFT] ? 0xFF
                                                                      : 0x00;
    outputBuffer[BROKEIO_P2_UR] =
        ls->m_bGameButtonLights[GameController_2][PUMP_BUTTON_UPRIGHT] ? 0xFF
                                                                       : 0x00;
    outputBuffer[BROKEIO_P2_CN] =
        ls->m_bGameButtonLights[GameController_2][PUMP_BUTTON_CENTER] ? 0xFF
                                                                      : 0x00;
    outputBuffer[BROKEIO_P2_LL] =
        ls->m_bGameButtonLights[GameController_2][PUMP_BUTTON_DOWNLEFT] ? 0xFF
                                                                        : 0x00;
    outputBuffer[BROKEIO_P2_LR] =
        ls->m_bGameButtonLights[GameController_2][PUMP_BUTTON_DOWNRIGHT] ? 0xFF
                                                                         : 0x00;
  }

  // only write to BrokeIO when lights change.
  if (memcmp(outputBuffer, prevOutputBuffer, sizeof(outputBuffer)) != 0) {
    dev.Write((unsigned char*)&outputBuffer, BROKEIO_HID_LIGHTS_REPORT_SIZE);
    memcpy(prevOutputBuffer, outputBuffer, sizeof(prevOutputBuffer));
  }
}
