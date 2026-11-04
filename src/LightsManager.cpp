#include "LightsManager.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "Actor.h"
#include "CommonMetrics.h"
#include "EnumHelper.h"
#include "Game.h"
#include "GameInput.h"  // for GameController
#include "GameManager.h"
#include "GameState.h"
#include "InputMapper.h"
#include "PlayerNumber.h"
#include "PlayerOptions.h"
#include "PlayerState.h"
#include "Preference.h"
#include "PrefsManager.h"
#include "RageThreads.h"
#include "RageTimer.h"
#include "RageUtil.h"
#include "RageUtil/ConvertValue.h"
#include "Style.h"
#include "ThemeMetric.h"
#include "arch/Lights/LightsDriver.h"
#include "global.h"

const std::string DEFAULT_LIGHTS_DRIVER = "SystemMessage,Export";
static Preference<std::string> g_sLightsDriver(
    "LightsDriver", "");  // "" == DEFAULT_LIGHTS_DRIVER
Preference<float> g_fLightsFalloffSeconds("LightsFalloffSeconds", 0.1f);
Preference<float> g_fLightsAheadSeconds("LightsAheadSeconds", 0.05f);
static Preference<bool> g_bBlinkGameplayButtonLightsOnNote(
    "BlinkGameplayButtonLightsOnNote", false);

static ThemeMetric<std::string> GAME_BUTTONS_TO_SHOW(
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

static void GetUsedGameInputs(std::vector<GameInput>& vGameInputsOut) {
  vGameInputsOut.clear();

  std::vector<std::string> asGameButtons;
  split(GAME_BUTTONS_TO_SHOW.GetValue(), ",", asGameButtons);
  FOREACH_ENUM(GameController, gc) {
    for (const std::string& button : asGameButtons) {
      GameButton gb = StringToGameButton(INPUTMAPPER->GetInputScheme(), button);
      if (gb != GameButton_Invalid) {
        GameInput gi = GameInput(gc, gb);
        vGameInputsOut.push_back(gi);
      }
    }
  }

  std::set<GameInput> vGIs;
  std::vector<const Style*> vStyles;
  GAMEMAN->GetStylesForGame(GAMESTATE->cur_game_, vStyles);
  const auto& value = CommonMetrics::STEPS_TYPES_TO_SHOW.GetValue();
  for (const Style* style : vStyles) {
    bool bFound =
        find(value.begin(), value.end(), style->steps_type_) != value.end();
    if (!bFound) {
      continue;
    }
    FOREACH_PlayerNumber(pn) {
      for (int iCol = 0; iCol < style->cols_per_player_; ++iCol) {
        std::vector<GameInput> gi;
        style->StyleInputToGameInput(iCol, pn, gi);
        for (size_t i = 0; i < gi.size(); ++i) {
          if (gi[i].IsValid()) {
            vGIs.insert(gi[i]);
          }
        }
      }
    }
  }

  for (const GameInput& input : vGIs) {
    vGameInputsOut.push_back(input);
  }
}

LightsManager* LIGHTSMAN =
    nullptr;  // global and accessible from anywhere in our program

LightsManager::LightsManager() {
  ZERO(secs_left_in_cabinet_light_blink_);
  ZERO(secs_left_in_game_button_blink_);
  ZERO(actor_lights_);
  ZERO(secs_left_in_actor_light_blink_);
  queued_coin_counter_pulses_ = 0;
  coin_counter_timer_.SetZero();

  lights_mode_ = LIGHTSMODE_JOINING;
  std::string sDriver = g_sLightsDriver.Get();
  if (sDriver.empty()) {
    sDriver = DEFAULT_LIGHTS_DRIVER;
  }
  LightsDriver::Create(sDriver, drivers_);

  SetLightsMode(LIGHTSMODE_ATTRACT);

  lights_mutex_ = new RageEvent("LightsMutex");
  lights_thread_shutdown_ = false;
  lights_thread_.SetName("LightsManager thread");
  lights_thread_.Create(LightsManThread_Start, this);
}

LightsManager::~LightsManager() {
  // tell the thread to quit.
  if (lights_mutex_ != nullptr) {
    lights_mutex_->Lock();
    lights_thread_shutdown_ = true;
    lights_mutex_->Signal();
    lights_mutex_->Unlock();
    lights_thread_.Wait();
  }

  for (LightsDriver* iter : drivers_) {
    RageUtil::SafeDelete(iter);
  }
  drivers_.clear();
}

int LightsManager::LightsManThreadMain() {
  while (!lights_thread_shutdown_) {
    lights_mutex_->Lock();

    // spin here until we get a light to push out.
    while (lights_queue_.empty() && !lights_thread_shutdown_) {
      lights_mutex_->Wait();
    }

    if (lights_thread_shutdown_) {
      lights_mutex_->Unlock();
      break;
    }

    // pop the first in first state out from the queue
    LightsState push = lights_queue_.front();
    lights_queue_.pop();

    lights_mutex_->Unlock();

    // push to all of the devices
    // could take time, it's why we are in at thread
    for (LightsDriver* iter : drivers_) {
      iter->Set(&push);
    }
  }

  return 0;
}

// XXX: Allow themer to change these. (rewritten; who wrote original? -aj)
static const float g_fLightEffectRiseSeconds = 0.075f;
static const float g_fLightEffectFalloffSeconds = 0.35f;
static const float g_fCoinPulseTime = 0.100f;
void LightsManager::Update(float fDeltaTime) {
  // Update actor effect lights.
  FOREACH_CabinetLight(cl) {
    float fTime = fDeltaTime;
    float& fDuration = secs_left_in_actor_light_blink_[cl];
    if (fDuration > 0) {
      // The light has power left.  Brighten it.
      float fSeconds = std::min(fDuration, fTime);
      fDuration -= fSeconds;
      fTime -= fSeconds;
      fapproach(actor_lights_[cl], 1, fSeconds / g_fLightEffectRiseSeconds);
    }

    if (fTime > 0) {
      // The light is out of power.  Dim it.
      fapproach(actor_lights_[cl], 0, fTime / g_fLightEffectFalloffSeconds);
    }

    Actor::SetBGMLight(cl, actor_lights_[cl]);
  }

  if (!IsEnabled()) {
    return;
  }

  // update lights falloff
  {
    FOREACH_CabinetLight(cl)
        fapproach(secs_left_in_cabinet_light_blink_[cl], 0, fDeltaTime);
    FOREACH_ENUM(GameController, gc)
    FOREACH_ENUM(GameButton, gb)
    fapproach(secs_left_in_game_button_blink_[gc][gb], 0, fDeltaTime);
  }

  // Set new lights state cabinet lights
  {
    ZERO(lights_state_.cabinet_lights_);
    ZERO(lights_state_.game_button_lights_);
  }

  {
    lights_state_.coin_counter_ = false;
    if (!coin_counter_timer_.IsZero()) {
      float fAgo = coin_counter_timer_.Ago();
      if (fAgo < g_fCoinPulseTime) {
        lights_state_.coin_counter_ = true;
      } else if (fAgo >= g_fCoinPulseTime * 2) {
        coin_counter_timer_.SetZero();
      }
    } else if (queued_coin_counter_pulses_) {
      coin_counter_timer_.Touch();
      --queued_coin_counter_pulses_;
    }
  }

  if (lights_mode_ == LIGHTSMODE_TEST_AUTO_CYCLE) {
    test_auto_cycle_current_index_ += fDeltaTime;
    test_auto_cycle_current_index_ =
        std::fmod(test_auto_cycle_current_index_, NUM_CabinetLight * 100);
  }

  switch (lights_mode_) {
    DEFAULT_FAIL(lights_mode_);

    case LIGHTSMODE_ATTRACT: {
      int iSec = RageTimer::GetTimeSinceStartSeconds();
      int iTopIndex = iSec % 4;

      // Aldo: Disabled this line, apparently it was a forgotten initialization
      // CabinetLight cl = CabinetLight_Invalid;

      switch (iTopIndex) {
        DEFAULT_FAIL(iTopIndex);
        case 0:
          lights_state_.cabinet_lights_[LIGHT_MARQUEE_UP_LEFT] = true;
          break;
        case 1:
          lights_state_.cabinet_lights_[LIGHT_MARQUEE_LR_RIGHT] = true;
          break;
        case 2:
          lights_state_.cabinet_lights_[LIGHT_MARQUEE_UP_RIGHT] = true;
          break;
        case 3:
          lights_state_.cabinet_lights_[LIGHT_MARQUEE_LR_LEFT] = true;
          break;
      }

      if (iTopIndex == 0) {
        lights_state_.cabinet_lights_[LIGHT_BASS_LEFT] = true;
        lights_state_.cabinet_lights_[LIGHT_BASS_RIGHT] = true;
      }

      break;
    }
    case LIGHTSMODE_MENU_START_ONLY:
    case LIGHTSMODE_MENU_START_AND_DIRECTIONS:
    case LIGHTSMODE_JOINING: {
      static int iLight;

      // if we've crossed a beat boundary, advance the light index
      {
        static float fLastBeat;
        float fLightSongBeat = GAMESTATE->position_.light_song_beat_;

        if (fracf(fLightSongBeat) < fracf(fLastBeat)) {
          ++iLight;
          wrap(iLight, 4);
        }

        fLastBeat = fLightSongBeat;
      }

      CabinetLight cl = CabinetLight_Invalid;

      switch (iLight) {
        DEFAULT_FAIL(iLight);
        case 0:
          cl = LIGHT_MARQUEE_UP_LEFT;
          break;
        case 1:
          cl = LIGHT_MARQUEE_LR_RIGHT;
          break;
        case 2:
          cl = LIGHT_MARQUEE_UP_RIGHT;
          break;
        case 3:
          cl = LIGHT_MARQUEE_LR_LEFT;
          break;
      }

      lights_state_.cabinet_lights_[cl] = true;

      break;
    }

    case LIGHTSMODE_DEMONSTRATION:
    case LIGHTSMODE_GAMEPLAY: {
      FOREACH_CabinetLight(cl) {
        int currLightPlayerNumber = (cl & 1) == 1 ? 1 : 0;
        bool skipLightActivation = false;

        auto playerOptions = GAMESTATE->player_state_[currLightPlayerNumber]
                                 ->player_options_.GetCurrent();

        if (cl == LIGHT_MARQUEE_UP_LEFT || cl == LIGHT_MARQUEE_UP_RIGHT ||
            cl == LIGHT_MARQUEE_LR_LEFT || cl == LIGHT_MARQUEE_LR_RIGHT) {
          if (GAMESTATE->IsHumanPlayer((PlayerNumber)currLightPlayerNumber)) {
            skipLightActivation =
                playerOptions.hide_light_type_ ==
                    HideLightType::HideLightType_HideMarqueeLights ||
                playerOptions.hide_light_type_ ==
                    HideLightType::HideLightType_HideAllLights;
          }
        } else {
          bool lightsBassParallel = PREFSMAN->lights_bass_parallel_;

          int otherPn = currLightPlayerNumber == 0 ? 1 : 0;
          auto otherPlayerOptions =
              GAMESTATE->player_state_[otherPn]->player_options_.GetCurrent();

          if (GAMESTATE->IsHumanPlayer((PlayerNumber)currLightPlayerNumber)) {
            skipLightActivation =
                playerOptions.hide_light_type_ ==
                    HideLightType::HideLightType_HideBassLights ||
                playerOptions.hide_light_type_ ==
                    HideLightType::HideLightType_HideAllLights ||
                (lightsBassParallel &&
                 (otherPlayerOptions.hide_light_type_ ==
                      HideLightType::HideLightType_HideBassLights ||
                  otherPlayerOptions.hide_light_type_ ==
                      HideLightType::HideLightType_HideAllLights));
          } else {
            skipLightActivation =
                lightsBassParallel &&
                (otherPlayerOptions.hide_light_type_ ==
                     HideLightType::HideLightType_HideBassLights ||
                 otherPlayerOptions.hide_light_type_ ==
                     HideLightType::HideLightType_HideAllLights);
          }
        }

        if (!skipLightActivation) {
          lights_state_.cabinet_lights_[cl] =
              secs_left_in_cabinet_light_blink_[cl] > 0;
        }
      }
      break;
    }

    case LIGHTSMODE_STAGE:
    case LIGHTSMODE_ALL_CLEARED: {
      FOREACH_CabinetLight(cl) lights_state_.cabinet_lights_[cl] = true;

      break;
    }

    case LIGHTSMODE_TEST_AUTO_CYCLE: {
      int iSec = GetTestAutoCycleCurrentIndex();

      CabinetLight cl = CabinetLight(iSec % NUM_CabinetLight);
      lights_state_.cabinet_lights_[cl] = true;

      break;
    }

    case LIGHTSMODE_TEST_MANUAL_CYCLE: {
      CabinetLight cl = test_manual_cycle_current_;
      lights_state_.cabinet_lights_[cl] = true;

      break;
    }
  }

  // Update game controller lights
  switch (lights_mode_) {
    DEFAULT_FAIL(lights_mode_);

    case LIGHTSMODE_ALL_CLEARED:
    case LIGHTSMODE_STAGE:
    case LIGHTSMODE_JOINING: {
      FOREACH_ENUM(GameController, gc) {
        if (GAMESTATE->side_is_joined_[gc]) {
          FOREACH_ENUM(GameButton, gb)
          lights_state_.game_button_lights_[gc][gb] = true;
        }
      }

      break;
    }

    case LIGHTSMODE_MENU_START_ONLY:
    case LIGHTSMODE_MENU_START_AND_DIRECTIONS: {
      float fLightSongBeat = GAMESTATE->position_.light_song_beat_;

      /* Blink menu lights on the first half of the beat */
      if (fracf(fLightSongBeat) <= 0.5f) {
        FOREACH_PlayerNumber(pn) {
          if (!GAMESTATE->side_is_joined_[pn]) {
            continue;
          }

          lights_state_.game_button_lights_[pn][GAME_BUTTON_START] = true;

          if (lights_mode_ == LIGHTSMODE_MENU_START_AND_DIRECTIONS) {
            lights_state_.game_button_lights_[pn][GAME_BUTTON_MENULEFT] = true;
            lights_state_.game_button_lights_[pn][GAME_BUTTON_MENURIGHT] = true;
            lights_state_.game_button_lights_[pn][GAME_BUTTON_MENUUP] = true;
            lights_state_.game_button_lights_[pn][GAME_BUTTON_MENUDOWN] = true;
          } else {
            // flash select during evaluation screen to indicate
            // that the button can be used for screenshots etc.
            lights_state_.game_button_lights_[pn][GAME_BUTTON_SELECT] = true;
          }
        }
      }

      // fall through to blink on button presses
      [[fallthrough]];
    }

    case LIGHTSMODE_DEMONSTRATION:
    case LIGHTSMODE_GAMEPLAY: {
      bool bGameplay = (lights_mode_ == LIGHTSMODE_DEMONSTRATION) ||
                       (lights_mode_ == LIGHTSMODE_GAMEPLAY);

      // Blink on notes during gameplay.
      if (bGameplay && g_bBlinkGameplayButtonLightsOnNote) {
        FOREACH_ENUM(GameController, gc) {
          FOREACH_ENUM(GameButton, gb) {
            lights_state_.game_button_lights_[gc][gb] =
                secs_left_in_game_button_blink_[gc][gb] > 0;
          }
        }
        break;
      }

      // fall through to blink on button presses
      [[fallthrough]];
    }

    case LIGHTSMODE_ATTRACT: {
      // Blink on game button presses.
      // GAME_BUTTON_CUSTOM_01 -> NUM_GameButton
      FOREACH_ENUM(GameController, gc) {
        FOREACH_GameButton_Custom(gb) {
          bool bOn = INPUTMAPPER->IsBeingPressed(GameInput(gc, gb));
          // or them in the event the game wants to turn them on too.
          lights_state_.game_button_lights_[gc][gb] |= bOn;
        }
      }

      // Blink on each menu button press
      // GAME_BUTTON_MENULEFT -> GAME_BUTTON_BACK
      // left, right, up, down, start, select, and back.
      FOREACH_ENUM(GameController, gc) {
        FOREACH_GameButton_Menu(gb) {
          bool bOn = INPUTMAPPER->IsBeingPressed(GameInput(gc, gb));
          // or them in the event the game wants to turn them on too.
          lights_state_.game_button_lights_[gc][gb] |= bOn;
        }
      }

      break;
    }

    case LIGHTSMODE_TEST_AUTO_CYCLE: {
      int index = GetTestAutoCycleCurrentIndex();

      std::vector<GameInput> vGI;
      GetUsedGameInputs(vGI);
      wrap(index, vGI.size());

      ZERO(lights_state_.game_button_lights_);

      GameController gc = vGI[index].controller_;
      GameButton gb = vGI[index].button_;
      lights_state_.game_button_lights_[gc][gb] = true;

      break;
    }

    case LIGHTSMODE_TEST_MANUAL_CYCLE: {
      ZERO(lights_state_.game_button_lights_);

      std::vector<GameInput> vGI;
      GetUsedGameInputs(vGI);

      if (controller_test_manual_cycle_current_ != -1) {
        GameController gc =
            vGI[controller_test_manual_cycle_current_].controller_;
        GameButton gb = vGI[controller_test_manual_cycle_current_].button_;
        lights_state_.game_button_lights_[gc][gb] = true;
      }

      break;
    }
  }

  // If not joined, has enough credits, and not too late to join, then
  // blink the menu buttons rapidly so they'll press Start
  {
    int iBeat = (int)(GAMESTATE->position_.light_song_beat_ * 4);
    bool bBlinkOn = (iBeat % 2) == 0;
    FOREACH_PlayerNumber(pn) {
      if (!GAMESTATE->side_is_joined_[pn] && GAMESTATE->PlayersCanJoin() &&
          GAMESTATE->EnoughCreditsToJoin()) {
        lights_state_.game_button_lights_[pn][GAME_BUTTON_START] = bBlinkOn;
      }
    }
  }

  // apply new light values we set above

  // only push to thread on changes.
  if (lights_state_ != prev_lights_state_) {
    // add lights state to queue.
    lights_mutex_->Lock();
    lights_queue_.push(lights_state_);
    lights_mutex_->Signal();
    lights_mutex_->Unlock();

    prev_lights_state_ = std::move(lights_state_);
  }
}

void LightsManager::BlinkCabinetLight(CabinetLight cl) {
  secs_left_in_cabinet_light_blink_[cl] = g_fLightsFalloffSeconds;
}

void LightsManager::BlinkGameButton(GameInput gi) {
  secs_left_in_game_button_blink_[gi.controller_][gi.button_] =
      g_fLightsFalloffSeconds;
}

void LightsManager::SetLightsMode(LightsMode lm) {
  lights_mode_ = lm;
  test_auto_cycle_current_index_ = 0;
  test_manual_cycle_current_ = CabinetLight_Invalid;
  controller_test_manual_cycle_current_ = -1;
}

LightsMode LightsManager::GetLightsMode() { return lights_mode_; }

void LightsManager::ChangeTestCabinetLight(int iDir) {
  controller_test_manual_cycle_current_ = -1;

  enum_add(test_manual_cycle_current_, iDir);
  wrap(*ConvertValue<int>(&test_manual_cycle_current_), NUM_CabinetLight);
}

void LightsManager::ChangeTestGameButtonLight(int iDir) {
  test_manual_cycle_current_ = CabinetLight_Invalid;

  std::vector<GameInput> vGI;
  GetUsedGameInputs(vGI);

  controller_test_manual_cycle_current_ += iDir;
  wrap(controller_test_manual_cycle_current_, vGI.size());
}

CabinetLight LightsManager::GetFirstLitCabinetLight() {
  FOREACH_CabinetLight(cl) {
    if (lights_state_.cabinet_lights_[cl]) {
      return cl;
    }
  }
  return CabinetLight_Invalid;
}

GameInput LightsManager::GetFirstLitGameButtonLight() {
  FOREACH_ENUM(GameController, gc) {
    FOREACH_ENUM(GameButton, gb) {
      if (lights_state_.game_button_lights_[gc][gb]) {
        return GameInput(gc, gb);
      }
    }
  }
  return GameInput();
}

bool LightsManager::IsEnabled() const {
  return drivers_.size() >= 1 || PREFSMAN->debug_lights_;
}

void LightsManager::TurnOffAllLights() {
  for (LightsDriver* iter : drivers_) {
    iter->Reset();
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
