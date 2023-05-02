#include "global.h"

#include "LightsManager.h"

#include <cmath>
#include <cstddef>
#include <vector>

#include "Actor.h"
#include "CommonMetrics.h"
#include "Game.h"
#include "GameInput.h"  // for GameController
#include "GameManager.h"
#include "GameState.h"
#include "InputMapper.h"
#include "Preference.h"
#include "PrefsManager.h"
#include "RageTimer.h"
#include "RageUtil.h"
#include "Style.h"
#include "arch/Lights/LightsDriver.h"

const RString DEFAULT_LIGHTS_DRIVER = "SystemMessage,Export";
static Preference<RString> g_sLightsDriver(
    "LightsDriver", "");  // "" == DEFAULT_LIGHTS_DRIVER
Preference<float> g_fLightsFalloffSeconds(
		"LightsFalloffSeconds", 0.1f);
Preference<float> g_fLightsAheadSeconds(
		"LightsAheadSeconds", 0.05f);
static Preference<bool> g_bBlinkGameplayButtonLightsOnNote(
    "BlinkGameplayButtonLightsOnNote", false);

static ThemeMetric<RString> GAME_BUTTONS_TO_SHOW(
    "LightsManager", "GameButtonsToShow");

static const char* CabinetLightNames[] = {
    "MarqueeUpLeft",  "MarqueeUpRight", "MarqueeLrLeft",
    "MarqueeLrRight", "BassLeft",       "BassRight",
};
XToString(CabinetLight);
StringToX(CabinetLight);

static const char* LightsModeNames[] = {
    "Attract",       "Joining",
    "MenuStartOnly", "MenuStartAndDirections",
    "Demonstration", "Gameplay",
    "Stage",         "Cleared",
    "TestAutoCycle", "TestManualCycle",
};
XToString(LightsMode);
LuaXType(LightsMode);

static void GetUsedGameInputs(std::vector<GameInput>& game_inputs_out) {
  game_inputs_out.clear();

  std::vector<RString> game_buttons;
  split(GAME_BUTTONS_TO_SHOW.GetValue(), ",", game_buttons);
  FOREACH_ENUM(GameController, game_controller) {
    for (const RString& button : game_buttons) {
      GameButton game_button =
          StringToGameButton(INPUTMAPPER->GetInputScheme(), button);
      if (game_button != GameButton_Invalid) {
        GameInput game_input = GameInput(game_controller, game_button);
        game_inputs_out.push_back(game_input);
      }
    }
  }

  std::set<GameInput> game_inputs;
  std::vector<const Style*> styles;
  GAMEMAN->GetStylesForGame(GAMESTATE->cur_game_, styles);
  const auto& value = CommonMetrics::STEPS_TYPES_TO_SHOW.GetValue();
  for (const Style* style : styles) {
    bool found = std::find(value.begin(), value.end(), style->m_StepsType) !=
                 value.end();
    if (!found) {
      continue;
    }
    FOREACH_PlayerNumber(pn) {
      for (int col = 0; col < style->m_iColsPerPlayer; ++col) {
        std::vector<GameInput> game_input;
        style->StyleInputToGameInput(col, pn, game_input);
        for (std::size_t i = 0; i < game_input.size(); ++i) {
          if (game_input[i].IsValid()) {
            game_inputs.insert(game_input[i]);
          }
        }
      }
    }
  }

  for (const GameInput& input : game_inputs) {
    game_inputs_out.push_back(input);
  }
}

// Global and accessible from anywhere in our program.
LightsManager* LIGHTSMAN = nullptr;

LightsManager::LightsManager() {
  ZERO(secs_left_in_cabinet_light_blink_);
  ZERO(secs_left_in_game_button_blink_);
  ZERO(actor_lights_);
  ZERO(secs_left_in_actor_light_blink_);
  queued_coin_counter_pulses_ = 0;
  coin_counter_timer_.SetZero();

  lights_mode_ = LIGHTSMODE_JOINING;
  RString driver = g_sLightsDriver.Get();
  if (driver.empty()) {
    driver = DEFAULT_LIGHTS_DRIVER;
  }
  LightsDriver::Create(driver, drivers_);

  SetLightsMode(LIGHTSMODE_ATTRACT);
}

LightsManager::~LightsManager() {
  for (LightsDriver* driver : drivers_) {
    SAFE_DELETE(driver);
  }
  drivers_.clear();
}

// NOTE(aj): Allow themer to change these. (rewritten; who wrote original?)
static const float g_fLightEffectRiseSeconds = 0.075f;
static const float g_fLightEffectFalloffSeconds = 0.35f;
static const float g_fCoinPulseTime = 0.100f;
void LightsManager::BlinkActorLight(CabinetLight cabinet_light) {
  secs_left_in_actor_light_blink_[cabinet_light] = g_fLightEffectRiseSeconds;
}

float LightsManager::GetActorLightLatencySeconds() const {
  return g_fLightEffectRiseSeconds;
}

void LightsManager::Update(float delta) {
  // Update actor effect lights.
  FOREACH_CabinetLight(cabinet_light) {
    float time = delta;
    float& duration = secs_left_in_actor_light_blink_[cabinet_light];
    if (duration > 0) {
      // The light has power left. Brighten it.
      float seconds = std::min(duration, time);
      duration -= seconds;
      time -= seconds;
      fapproach(
          actor_lights_[cabinet_light], 1, seconds / g_fLightEffectRiseSeconds);
    }

    if (time > 0) {
      // The light is out of power. Dim it.
      fapproach(
          actor_lights_[cabinet_light], 0, time / g_fLightEffectFalloffSeconds);
    }

    Actor::SetBGMLight(cabinet_light, actor_lights_[cabinet_light]);
  }

  if (!IsEnabled()) {
    return;
  }

  // update lights falloff
  {
    FOREACH_CabinetLight(cabinet_light)
        fapproach(secs_left_in_cabinet_light_blink_[cabinet_light], 0, delta);
    FOREACH_ENUM(GameController, game_controller)
    FOREACH_ENUM(GameButton, game_button)
    fapproach(
        secs_left_in_game_button_blink_[game_controller][game_button], 0,
        delta);
  }

  // Set new lights state cabinet lights
  {
    ZERO(lights_state_.cabinet_lights);
    ZERO(lights_state_.game_button_lights);
  }

  {
    lights_state_.coin_counter = false;
    if (!coin_counter_timer_.IsZero()) {
      float ago = coin_counter_timer_.Ago();
      if (ago < g_fCoinPulseTime) {
        lights_state_.coin_counter = true;
      } else if (ago >= g_fCoinPulseTime * 2) {
        coin_counter_timer_.SetZero();
      }
    } else if (queued_coin_counter_pulses_) {
      coin_counter_timer_.Touch();
      --queued_coin_counter_pulses_;
    }
  }

  if (lights_mode_ == LIGHTSMODE_TEST_AUTO_CYCLE) {
    test_auto_cycle_current_index_ += delta;
    test_auto_cycle_current_index_ =
        std::fmod(test_auto_cycle_current_index_, NUM_CabinetLight * 100);
  }

  switch (lights_mode_) {
    DEFAULT_FAIL(lights_mode_);

    case LIGHTSMODE_ATTRACT: {
      int secs = (int)RageTimer::GetTimeSinceStartFast();
      int top_index = secs % 4;

      // NOTE(Aldo): Disabled this line, apparently it was a forgotten
      // initialization
      // CabinetLight cl = CabinetLight_Invalid;

      switch (top_index) {
        DEFAULT_FAIL(top_index);
        case 0:
          lights_state_.cabinet_lights[LIGHT_MARQUEE_UP_LEFT] = true;
          break;
        case 1:
          lights_state_.cabinet_lights[LIGHT_MARQUEE_LR_RIGHT] = true;
          break;
        case 2:
          lights_state_.cabinet_lights[LIGHT_MARQUEE_UP_RIGHT] = true;
          break;
        case 3:
          lights_state_.cabinet_lights[LIGHT_MARQUEE_LR_LEFT] = true;
          break;
      }

      if (top_index == 0) {
        lights_state_.cabinet_lights[LIGHT_BASS_LEFT] = true;
        lights_state_.cabinet_lights[LIGHT_BASS_RIGHT] = true;
      }

      break;
    }
    case LIGHTSMODE_MENU_START_ONLY:
    case LIGHTSMODE_MENU_START_AND_DIRECTIONS:
    case LIGHTSMODE_JOINING: {
      static int light;

      // If we've crossed a beat boundary, advance the light index.
      {
        static float last_beat;
        float light_song_beat = GAMESTATE->position_.m_fLightSongBeat;

        if (fracf(light_song_beat) < fracf(last_beat)) {
          ++light;
          wrap(light, 4);
        }

        last_beat = light_song_beat;
      }

      CabinetLight cabinet_light = CabinetLight_Invalid;

      switch (light) {
        DEFAULT_FAIL(light);
        case 0:
          cabinet_light = LIGHT_MARQUEE_UP_LEFT;
          break;
        case 1:
          cabinet_light = LIGHT_MARQUEE_LR_RIGHT;
          break;
        case 2:
          cabinet_light = LIGHT_MARQUEE_UP_RIGHT;
          break;
        case 3:
          cabinet_light = LIGHT_MARQUEE_LR_LEFT;
          break;
      }

      lights_state_.cabinet_lights[cabinet_light] = true;

      break;
    }

    case LIGHTSMODE_DEMONSTRATION:
    case LIGHTSMODE_GAMEPLAY: {
      FOREACH_CabinetLight(cabinet_light)
          lights_state_.cabinet_lights[cabinet_light] =
          secs_left_in_cabinet_light_blink_[cabinet_light] > 0;
      break;
    }

    case LIGHTSMODE_STAGE:
    case LIGHTSMODE_ALL_CLEARED: {
      FOREACH_CabinetLight(cabinet_light)
          lights_state_.cabinet_lights[cabinet_light] = true;

      break;
    }

    case LIGHTSMODE_TEST_AUTO_CYCLE: {
      int sec = GetTestAutoCycleCurrentIndex();

      CabinetLight cabinet_light = CabinetLight(sec % NUM_CabinetLight);
      lights_state_.cabinet_lights[cabinet_light] = true;

      break;
    }

    case LIGHTSMODE_TEST_MANUAL_CYCLE: {
      CabinetLight cabint_light = cabinet_light_test_manual_cycle_current_;
      lights_state_.cabinet_lights[cabint_light] = true;

      break;
    }
  }

  // Update game controller lights
  switch (lights_mode_) {
    DEFAULT_FAIL(lights_mode_);

    case LIGHTSMODE_ALL_CLEARED:
    case LIGHTSMODE_STAGE:
    case LIGHTSMODE_JOINING: {
      FOREACH_ENUM(GameController, game_controller) {
        if (GAMESTATE->side_is_joined_[game_controller]) {
          FOREACH_ENUM(GameButton, game_button)
          lights_state_.game_button_lights[game_controller][game_button] = true;
        }
      }

      break;
    }

    case LIGHTSMODE_MENU_START_ONLY:
    case LIGHTSMODE_MENU_START_AND_DIRECTIONS: {
      float light_song_beat = GAMESTATE->position_.m_fLightSongBeat;

      // Blink menu lights on the first half of the beat
      if (fracf(light_song_beat) <= 0.5f) {
        FOREACH_PlayerNumber(pn) {
          if (!GAMESTATE->side_is_joined_[pn]) {
            continue;
          }

          lights_state_.game_button_lights[pn][GAME_BUTTON_START] = true;

          if (lights_mode_ == LIGHTSMODE_MENU_START_AND_DIRECTIONS) {
            lights_state_.game_button_lights[pn][GAME_BUTTON_MENULEFT] = true;
            lights_state_.game_button_lights[pn][GAME_BUTTON_MENURIGHT] = true;
            lights_state_.game_button_lights[pn][GAME_BUTTON_MENUUP] = true;
            lights_state_.game_button_lights[pn][GAME_BUTTON_MENUDOWN] = true;
          } else {
            // Flash select during evaluation screen to indicate that the button
            // can be used for screenshots etc.
            lights_state_.game_button_lights[pn][GAME_BUTTON_SELECT] = true;
          }
        }
      }

      // Fall through to blink on button presses.
      [[fallthrough]];
    }

    case LIGHTSMODE_DEMONSTRATION:
    case LIGHTSMODE_GAMEPLAY: {
      bool gameplay =
          (lights_mode_ == LIGHTSMODE_DEMONSTRATION ||
           lights_mode_ == LIGHTSMODE_GAMEPLAY);

      // Blink on notes during gameplay.
      if (gameplay && g_bBlinkGameplayButtonLightsOnNote) {
        FOREACH_ENUM(GameController, game_controller) {
          FOREACH_ENUM(GameButton, game_button) {
            lights_state_.game_button_lights[game_controller][game_button] =
                secs_left_in_game_button_blink_[game_controller][game_button] >
                0;
          }
        }
        break;
      }

      // fall through to blink on button presses
      [[fallthrough]];
    }

    case LIGHTSMODE_ATTRACT: {
      // Blink on button presses.
      FOREACH_ENUM(GameController, game_controller) {
        FOREACH_GameButton_Custom(game_button) {
          bool on = INPUTMAPPER->IsBeingPressed(
              GameInput(game_controller, game_button));
          lights_state_.game_button_lights[game_controller][game_button] = on;
        }
      }

      break;
    }

    case LIGHTSMODE_TEST_AUTO_CYCLE: {
      int index = GetTestAutoCycleCurrentIndex();

      std::vector<GameInput> game_inputs;
      GetUsedGameInputs(game_inputs);
      wrap(index, game_inputs.size());

      ZERO(lights_state_.game_button_lights);

      GameController game_controller = game_inputs[index].controller;
      GameButton game_button = game_inputs[index].button;
      lights_state_.game_button_lights[game_controller][game_button] = true;

      break;
    }

    case LIGHTSMODE_TEST_MANUAL_CYCLE: {
      ZERO(lights_state_.game_button_lights);

      std::vector<GameInput> game_inputs;
      GetUsedGameInputs(game_inputs);

      if (controller_test_manual_cycle_current_ != -1) {
        GameController gc =
            game_inputs[controller_test_manual_cycle_current_].controller;
        GameButton gb =
            game_inputs[controller_test_manual_cycle_current_].button;
        lights_state_.game_button_lights[gc][gb] = true;
      }

      break;
    }
  }

  // If not joined, has enough credits, and not too late to join, then blink the
  // menu buttons rapidly so they'll press Start
  {
    int beat = (int)(GAMESTATE->position_.m_fLightSongBeat * 4);
    bool blink_on = (beat % 2) == 0;
    FOREACH_PlayerNumber(pn) {
      if (!GAMESTATE->side_is_joined_[pn] && GAMESTATE->PlayersCanJoin() &&
          GAMESTATE->EnoughCreditsToJoin()) {
        lights_state_.game_button_lights[pn][GAME_BUTTON_START] = blink_on;
      }
    }
  }

  // Apply new light values we set above.
  for (LightsDriver* driver : drivers_) {
    driver->Set(&lights_state_);
  }
}

void LightsManager::BlinkCabinetLight(CabinetLight cabinet_light) {
  secs_left_in_cabinet_light_blink_[cabinet_light] = g_fLightsFalloffSeconds;
}

void LightsManager::BlinkGameButton(GameInput game_input) {
  secs_left_in_game_button_blink_[game_input.controller][game_input.button] =
      g_fLightsFalloffSeconds;
}

void LightsManager::SetLightsMode(LightsMode lights_mode) {
  lights_mode_ = lights_mode;
  test_auto_cycle_current_index_ = 0;
  cabinet_light_test_manual_cycle_current_ = CabinetLight_Invalid;
  controller_test_manual_cycle_current_ = -1;
}

LightsMode LightsManager::GetLightsMode() { return lights_mode_; }

void LightsManager::ChangeTestCabinetLight(int dir) {
  controller_test_manual_cycle_current_ = -1;

  enum_add(cabinet_light_test_manual_cycle_current_, dir);
  wrap(
      *ConvertValue<int>(&cabinet_light_test_manual_cycle_current_),
      NUM_CabinetLight);
}

void LightsManager::ChangeTestGameButtonLight(int dir) {
  cabinet_light_test_manual_cycle_current_ = CabinetLight_Invalid;

  std::vector<GameInput> game_inputs;
  GetUsedGameInputs(game_inputs);

  controller_test_manual_cycle_current_ += dir;
  wrap(controller_test_manual_cycle_current_, game_inputs.size());
}

CabinetLight LightsManager::GetFirstLitCabinetLight() {
  FOREACH_CabinetLight(cabinet_light) {
    if (lights_state_.cabinet_lights[cabinet_light]) {
      return cabinet_light;
    }
  }
  return CabinetLight_Invalid;
}

GameInput LightsManager::GetFirstLitGameButtonLight() {
  FOREACH_ENUM(GameController, game_controller) {
    FOREACH_ENUM(GameButton, game_button) {
      if (lights_state_.game_button_lights[game_controller][game_button]) {
        return GameInput(game_controller, game_button);
      }
    }
  }
  return GameInput();
}

bool LightsManager::IsEnabled() const {
  return drivers_.size() >= 1 || PREFSMAN->m_bDebugLights;
}

void LightsManager::TurnOffAllLights() {
  for (LightsDriver* driver : drivers_) {
    driver->Reset();
  }
}

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
