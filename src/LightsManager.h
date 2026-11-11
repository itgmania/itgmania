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

extern Preference<float> lights_falloff_seconds;
extern Preference<float> lights_ahead_seconds;

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
  bool cabinet_lights_[NUM_CabinetLight] = {};
  bool game_button_lights_[NUM_GameController][NUM_GameButton] = {};

  // This isn't actually a light, but it's typically implemented in the same
  // way.
  bool coin_counter_ = false;

  // equality operator
  bool operator==(const LightsState& rhs) const {
    // compare cabinet lights
    for (int i = 0; i < NUM_CabinetLight; ++i) {
      if (cabinet_lights_[i] != rhs.cabinet_lights_[i]) {
        return false;
      }
    }

    // compare game button lights
    for (int c = 0; c < NUM_GameController; ++c) {
      for (int b = 0; b < NUM_GameButton; ++b) {
        if (game_button_lights_[c][b] != rhs.game_button_lights_[c][b]) {
          return false;
        }
      }
    }

    // compare coin counter
    if (coin_counter_ != rhs.coin_counter_) {
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

  void Update(float delta_time);
  bool IsEnabled() const;

  void BlinkCabinetLight(CabinetLight cl);
  void BlinkGameButton(GameInput gi);
  void TurnOffAllLights();
  void PulseCoinCounter() { ++queued_coin_counter_pulses_; }
  void SetLightsMode(LightsMode lm);
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
  float actor_lights_[NUM_CabinetLight];  // current "power" of each actor light
  float
      secs_left_in_actor_light_blink_[NUM_CabinetLight];  // duration to "power"
                                                          // an actor light

  std::vector<LightsDriver*> drivers_;
  LightsMode lights_mode_;

  LightsState lights_state_;
  LightsState prev_lights_state_;

  int queued_coin_counter_pulses_;
  RageTimer coin_counter_timer_;

  bool lights_thread_shutdown_;
  RageThread lights_thread_;
  RageEvent* lights_mutex_;
  std::queue<LightsState> lights_queue_;

  int LightsManThreadMain();

  static int LightsManThread_Start(void* p) {
    ((LightsManager*)p)->LightsManThreadMain();
    return 0;
  }

  int GetTestAutoCycleCurrentIndex() {
    return (int)test_auto_cycle_current_index_;
  }

  float test_auto_cycle_current_index_;
  CabinetLight test_manual_cycle_current_;
  int controller_test_manual_cycle_current_;
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
