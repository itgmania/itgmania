#ifndef LIGHTSMANAGER_H
#define LIGHTSMANAGER_H

#include <vector>

#include "EnumHelper.h"
#include "GameInput.h"
#include "PlayerNumber.h"
#include "Preference.h"
#include "RageTimer.h"

// TODO(teejusb): Remove forward declaration.
class LightsDriver;

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

// Loop through each CabinetLight on the machine.
#define FOREACH_CabinetLight(i) FOREACH_ENUM(CabinetLight, i)
const RString& CabinetLightToString(CabinetLight cabine_light);
CabinetLight StringToCabinetLight(const RString& cabine_light_str);

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
const RString& LightsModeToString(LightsMode lights_mode);
LuaDeclareType(LightsMode);

struct LightsState {
  bool cabinet_lights[NUM_CabinetLight];
  bool game_button_lights[NUM_GameController][NUM_GameButton];

  // This isn't actually a light, but it's typically implemented in the same
  // way.
  bool coin_counter;
};

// Control lights.
class LightsManager {
 public:
  LightsManager();
  ~LightsManager();

  void Update(float delta);
  bool IsEnabled() const;

  void BlinkCabinetLight(CabinetLight cabinet_light);
  void BlinkGameButton(GameInput game_input);
  void BlinkActorLight(CabinetLight cabinet_light);
  void TurnOffAllLights();
  void PulseCoinCounter() { ++queued_coin_counter_pulses_; }
  float GetActorLightLatencySeconds() const;

  void SetLightsMode(LightsMode lights_mode);
  LightsMode GetLightsMode();

  void PrevTestCabinetLight() { ChangeTestCabinetLight(-1); }
  void NextTestCabinetLight() { ChangeTestCabinetLight(+1); }
  void PrevTestGameButtonLight() { ChangeTestGameButtonLight(-1); }
  void NextTestGameButtonLight() { ChangeTestGameButtonLight(+1); }

  CabinetLight GetFirstLitCabinetLight();
  GameInput GetFirstLitGameButtonLight();

 private:
  void ChangeTestCabinetLight(int dir);
  void ChangeTestGameButtonLight(int dir);

  float secs_left_in_cabinet_light_blink_[NUM_CabinetLight];
  float secs_left_in_game_button_blink_[NUM_GameController][NUM_GameButton];
	// Current "power" of each actor light.
  float actor_lights_[NUM_CabinetLight];
	// Duration to "power" an actor light.
  float secs_left_in_actor_light_blink_[NUM_CabinetLight];

  std::vector<LightsDriver*> drivers_;
  LightsMode lights_mode_;
  LightsState lights_state_;

  int queued_coin_counter_pulses_;
  RageTimer coin_counter_timer_;

  int GetTestAutoCycleCurrentIndex() {
    return (int)test_auto_cycle_current_index_;
  }

  float test_auto_cycle_current_index_;
  CabinetLight cabinet_light_test_manual_cycle_current_;
  int controller_test_manual_cycle_current_;
};

// Global and accessible from anywhere in our program.
extern LightsManager* LIGHTSMAN;

#endif  // LIGHTSMANAGER_H

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
