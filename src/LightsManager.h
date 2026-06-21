#ifndef LightsManager_H
#define LightsManager_H

#include <queue>
#include <string>
#include <vector>

#include "EnumHelper.h"
#include "GameInput.h"
#include "Preference.h"
#include "RageThreads.h"
#include "RageTimer.h"

extern Preference<float> g_fLightsFalloffSeconds;
extern Preference<float> g_fLightsAheadSeconds;

enum CabinetLight {
  LIGHT_MARQUEE_UP_LEFT,
  LIGHT_MARQUEE_UP_RIGHT,
  LIGHT_MARQUEE_LR_LEFT,
  LIGHT_MARQUEE_LR_RIGHT,
  LIGHT_BASS_LEFT,
  LIGHT_BASS_RIGHT,
  NUM_CabinetLight,
  CabinetLight_Invalid
};
/** @brief Loop through each CabinetLight on the machine. */
#define FOREACH_CabinetLight(i) FOREACH_ENUM(CabinetLight, i)
const std::string& CabinetLightToString(CabinetLight cl);
CabinetLight StringToCabinetLight(const std::string& s);

enum LightsMode {
  LIGHTSMODE_ATTRACT,
  LIGHTSMODE_JOINING,
  LIGHTSMODE_MENU_START_ONLY,
  LIGHTSMODE_MENU_START_AND_DIRECTIONS,
  LIGHTSMODE_DEMONSTRATION,
  LIGHTSMODE_GAMEPLAY,
  LIGHTSMODE_STAGE,
  LIGHTSMODE_ALL_CLEARED,
  LIGHTSMODE_TEST_AUTO_CYCLE,
  LIGHTSMODE_TEST_MANUAL_CYCLE,
  NUM_LightsMode,
  LightsMode_Invalid
};
const std::string& LightsModeToString(LightsMode lm);
LuaDeclareType(LightsMode);

struct LightsState {
  bool m_bCabinetLights[NUM_CabinetLight] = {};
  bool m_bGameButtonLights[NUM_GameController][NUM_GameButton] = {};

  // This isn't actually a light, but it's typically implemented in the same
  // way.
  bool m_bCoinCounter = false;

  // equality operator
  bool operator==(const LightsState& rhs) const {
    // compare cabinet lights
    for (int i = 0; i < NUM_CabinetLight; ++i) {
      if (m_bCabinetLights[i] != rhs.m_bCabinetLights[i]) {
        return false;
      }
    }

    // compare game button lights
    for (int c = 0; c < NUM_GameController; ++c) {
      for (int b = 0; b < NUM_GameButton; ++b) {
        if (m_bGameButtonLights[c][b] != rhs.m_bGameButtonLights[c][b]) {
          return false;
        }
      }
    }

    // compare coin counter
    if (m_bCoinCounter != rhs.m_bCoinCounter) {
      return false;
    }

    // all fields match
    return true;
  }

  // inequality operator
  bool operator!=(const LightsState& rhs) const { return !(*this == rhs); }
};

class LightsDriver;
/** @brief Control lights. */
class LightsManager {
 public:
  LightsManager();
  ~LightsManager();

  void Update(float fDeltaTime);
  bool IsEnabled() const;

  void BlinkCabinetLight(CabinetLight cl);
  void BlinkGameButton(GameInput gi);
  void BlinkActorLight(CabinetLight cl);
  void TurnOffAllLights();
  void PulseCoinCounter() { ++m_iQueuedCoinCounterPulses; }
  float GetActorLightLatencySeconds() const;

  void SetLightsMode(LightsMode lm);
  LightsMode GetLightsMode();

  void PrevTestCabinetLight() { ChangeTestCabinetLight(-1); }
  void NextTestCabinetLight() { ChangeTestCabinetLight(+1); }
  void PrevTestGameButtonLight() { ChangeTestGameButtonLight(-1); }
  void NextTestGameButtonLight() { ChangeTestGameButtonLight(+1); }

  CabinetLight GetFirstLitCabinetLight();
  GameInput GetFirstLitGameButtonLight();

 private:
  void ChangeTestCabinetLight(int iDir);
  void ChangeTestGameButtonLight(int iDir);

  float m_fSecsLeftInCabinetLightBlink[NUM_CabinetLight];
  float m_fSecsLeftInGameButtonBlink[NUM_GameController][NUM_GameButton];
  float
      m_fActorLights[NUM_CabinetLight];  // current "power" of each actor light
  float m_fSecsLeftInActorLightBlink[NUM_CabinetLight];  // duration to "power"
                                                         // an actor light

  std::vector<LightsDriver*> m_vpDrivers;
  LightsMode m_LightsMode;

  LightsState m_LightsState;
  LightsState m_PrevLightsState;

  int m_iQueuedCoinCounterPulses;
  RageTimer m_CoinCounterTimer;

  bool m_LightsThreadShutdown;
  RageThread m_LightsThread;
  RageEvent* m_LightsMutex;
  std::queue<LightsState> m_LightsQueue;

  int LightsManThreadMain();

  static int LightsManThread_Start(void* p) {
    ((LightsManager*)p)->LightsManThreadMain();
    return 0;
  }

  int GetTestAutoCycleCurrentIndex() {
    return (int)m_fTestAutoCycleCurrentIndex;
  }

  float m_fTestAutoCycleCurrentIndex;
  CabinetLight m_clTestManualCycleCurrent;
  int m_iControllerTestManualCycleCurrent;
};

extern LightsManager*
    LIGHTSMAN;  // global and accessible from anywhere in our program

#endif

/*
 * (c) 2003-2004 Chris Danford
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, provided that the above
 * copyright notice(s) and this permission notice appear in all copies of
 * the Software and that both the above copyright notice(s) and this
 * permission notice appear in supporting documentation.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
 * INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
