#include "ScreenTestLights.h"

#include <string>

#include "ActorUtil.h"
#include "GameInput.h"
#include "InputEventPlus.h"
#include "InputFilter.h"
#include "InputMapper.h"
#include "LightsManager.h"
#include "LocalizedString.h"
#include "PlayerNumber.h"
#include "RageUtil.h"
#include "Screen.h"
#include "ScreenManager.h"
#include "ScreenMessage.h"
#include "ScreenWithMenuElements.h"
#include "ThemeManager.h"

REGISTER_SCREEN_CLASS(ScreenTestLights);

void ScreenTestLights::Init() {
  ScreenWithMenuElements::Init();

  text_inputs_.SetName("Text");
  text_inputs_.LoadFromFont(THEME->GetPathF("Common", "normal"));
  text_inputs_.SetText("");
  LOAD_ALL_COMMANDS_AND_SET_XY(text_inputs_);
  this->AddChild(&text_inputs_);
}

void ScreenTestLights::BeginScreen() {
  LIGHTSMAN->SetLightsMode(LIGHTSMODE_TEST_AUTO_CYCLE);
  ScreenWithMenuElements::BeginScreen();
}

void ScreenTestLights::EndScreen() {
  LIGHTSMAN->SetLightsMode(LIGHTSMODE_MENU_START_AND_DIRECTIONS);
  ScreenWithMenuElements::EndScreen();
}

static LocalizedString AUTO_CYCLE("ScreenTestLights", "Auto Cycle");
static LocalizedString MANUAL_CYCLE("ScreenTestLights", "Manual Cycle");
static LocalizedString CABINET_LIGHT("ScreenTestLights", "cabinet light");
static LocalizedString CONTROLLER_LIGHT("ScreenTestLights", "controller light");
void ScreenTestLights::Update(float delta_time) {
  Screen::Update(delta_time);

  if (timer_back_to_auto_cycle_.Ago() > 20) {
    timer_back_to_auto_cycle_.Touch();
    LIGHTSMAN->SetLightsMode(LIGHTSMODE_TEST_AUTO_CYCLE);
  }

  CabinetLight cl = LIGHTSMAN->GetFirstLitCabinetLight();
  GameInput gi = LIGHTSMAN->GetFirstLitGameButtonLight();

  std::string s;
  std::string light_mode;
  std::string cabinet_light_id;
  std::string game_button_light_id;
  std::string side;

  switch (LIGHTSMAN->GetLightsMode()) {
    case LIGHTSMODE_TEST_AUTO_CYCLE:
      s += AUTO_CYCLE.GetValue() + "\n";
      light_mode += AUTO_CYCLE.GetValue();
      break;
    case LIGHTSMODE_TEST_MANUAL_CYCLE:
      s += MANUAL_CYCLE.GetValue() + "\n";
      light_mode += MANUAL_CYCLE.GetValue();
      break;
    default:
      break;
  }

  if (cl == CabinetLight_Invalid) {
    s += CABINET_LIGHT.GetValue() + ": -----\n";
    cabinet_light_id += "None";
  } else {
    s += ssprintf(
        "%s: %d %s\n", CABINET_LIGHT.GetValue().c_str(), cl,
        CabinetLightToString(cl).c_str());
    cabinet_light_id += CabinetLightToString(cl).c_str();
  }

  if (!gi.IsValid()) {
    s += CONTROLLER_LIGHT.GetValue() + ": -----\n";
    game_button_light_id += "None";
  } else {
    std::string game_button =
        GameButtonToLocalizedString(INPUTMAPPER->GetInputScheme(), gi.button_);
    PlayerNumber pn = (PlayerNumber)(gi.controller_);
    s += ssprintf(
        "%s: %s %d %s\n", CONTROLLER_LIGHT.GetValue().c_str(),
        PlayerNumberToString(pn).c_str(), gi.button_, game_button.c_str());
    game_button_light_id += game_button.c_str();
    side += PlayerNumberToString(pn).c_str();
  }

  Message msg("TestLightEvent");
  msg.SetParam("LightMode", light_mode);
  msg.SetParam("CabinetLightId", cabinet_light_id);
  msg.SetParam("Side", side);
  msg.SetParam("GameButtonLightId", game_button_light_id);

  MESSAGEMAN->Broadcast(msg);
  text_inputs_.SetText(s);
}

bool ScreenTestLights::Input(const InputEventPlus& input) {
  if (input.type_ != IET_FIRST_PRESS && input.type_ != IET_REPEAT) {
    return false;  // ignore
  }

  return ScreenWithMenuElements::Input(input);  // default handler
}

bool ScreenTestLights::MenuLeft(const InputEventPlus& input) {
  if (LIGHTSMAN->GetLightsMode() != LIGHTSMODE_TEST_MANUAL_CYCLE) {
    LIGHTSMAN->SetLightsMode(LIGHTSMODE_TEST_MANUAL_CYCLE);
  }
  if (input.pn_ == PLAYER_1) {
    LIGHTSMAN->PrevTestCabinetLight();
  } else {
    LIGHTSMAN->PrevTestGameButtonLight();
  }
  timer_back_to_auto_cycle_.Touch();
  return true;
}

bool ScreenTestLights::MenuRight(const InputEventPlus& input) {
  if (LIGHTSMAN->GetLightsMode() != LIGHTSMODE_TEST_MANUAL_CYCLE) {
    LIGHTSMAN->SetLightsMode(LIGHTSMODE_TEST_MANUAL_CYCLE);
  }
  if (input.pn_ == PLAYER_1) {
    LIGHTSMAN->NextTestCabinetLight();
  } else {
    LIGHTSMAN->NextTestGameButtonLight();
  }
  timer_back_to_auto_cycle_.Touch();
  return true;
}

bool ScreenTestLights::MenuStart(const InputEventPlus& input) {
  return MenuBack(input);
}

bool ScreenTestLights::MenuBack(const InputEventPlus& input) {
  if (IsTransitioning()) {
    return false;
  }
  SCREENMAN->PlayStartSound();
  StartTransitioningScreen(SM_GoToPrevScreen);
  return true;
}

/*
 * (c) 2004 Chris Danford
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
