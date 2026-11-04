#include "MenuTimer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>

#include "Actor.h"
#include "ActorFrame.h"
#include "ActorUtil.h"
#include "GameSoundManager.h"
#include "LuaManager.h"
#include "RageUtil.h"
#include "ScreenManager.h"
#include "ScreenMessage.h"
#include "ThemeManager.h"
#include "ThemeMetric.h"
#include "global.h"

std::string WARNING_COMMAND_NAME(size_t i) {
  return ssprintf("Warning%dCommand", int(i));
}

static const float TIMER_PAUSE_SECONDS = 99.99f;

MenuTimer::MenuTimer() {
  stall_seconds_ = 0;
  stall_seconds_left_ = 0;
  paused_ = false;
  silent_ = false;
  WARNING_COMMAND = nullptr;
}

MenuTimer::~MenuTimer() { delete WARNING_COMMAND; }

void MenuTimer::Load(std::string sMetricsGroup) {
  spr_frame_.Load(THEME->GetPathG(sMetricsGroup, "Frame"));
  spr_frame_->SetName("Frame");
  ActorUtil::LoadAllCommandsAndSetXY(spr_frame_, sMetricsGroup);
  this->AddChild(spr_frame_);

  for (int i = 0; i < NUM_MENU_TIMER_TEXTS; i++) {
    text_[i].LoadFromFont(THEME->GetPathF(sMetricsGroup, "numbers"));
    text_[i].SetName(ssprintf("Text%d", i + 1));
    ActorUtil::LoadAllCommandsAndOnCommand(text_[i], sMetricsGroup);
    this->AddChild(&text_[i]);
  }

  expr_format_text_[0] =
      THEME->GetMetricR(sMetricsGroup, "Text1FormatFunction");
  expr_format_text_[1] =
      THEME->GetMetricR(sMetricsGroup, "Text2FormatFunction");

  SetSeconds(TIMER_PAUSE_SECONDS);

  sound_beep_.Load(THEME->GetPathS(sMetricsGroup, "tick"));

  WARNING_START.Load(sMetricsGroup, "WarningStart");
  WARNING_BEEP_START.Load(sMetricsGroup, "WarningBeepStart");
  MAX_STALL_SECONDS.Load(sMetricsGroup, "MaxStallSeconds");
  HURRY_UP_TRANSITION.Load(sMetricsGroup, "HurryUpTransition");

  // clear warning commands out if they already exist. -aj
  if (WARNING_COMMAND) {
    WARNING_COMMAND->Clear();
  }

  WARNING_COMMAND = new ThemeMetric1D<apActorCommands>(
      sMetricsGroup, WARNING_COMMAND_NAME, WARNING_START + 1);

  stall_seconds_left_ = MAX_STALL_SECONDS;
}

void MenuTimer::EnableStealth(bool bStealth) {
  EnableSilent(bStealth);

  for (int i = 0; i < NUM_MENU_TIMER_TEXTS; i++) {
    text_[i].SetVisible(!bStealth);
  }
}

void MenuTimer::Update(float fDeltaTime) {
  ActorFrame::Update(fDeltaTime);

  if (paused_) {
    return;
  }

  // run down the stall time if any
  if (stall_seconds_ > 0) {
    stall_seconds_ = std::max(stall_seconds_ - fDeltaTime, 0.0f);
  }
  if (stall_seconds_ > 0) {
    return;
  }

  const float fOldSecondsLeft = seconds_left_;
  seconds_left_ -= fDeltaTime;
  seconds_left_ = std::max(0.0f, seconds_left_);
  const float fNewSecondsLeft = seconds_left_;

  SetText(fNewSecondsLeft);

  if (fOldSecondsLeft == fNewSecondsLeft) {
    return;
  }

  if (fOldSecondsLeft > HURRY_UP_TRANSITION &&
      fNewSecondsLeft < HURRY_UP_TRANSITION) {
    SOUND->PlayOnceFromAnnouncer("hurry up");
  }

  int iCrossed = std::floor(fOldSecondsLeft);
  if (fOldSecondsLeft > iCrossed && fNewSecondsLeft < iCrossed)  // crossed
  {
    if (iCrossed <= WARNING_START) {
      for (int i = 0; i < NUM_MENU_TIMER_TEXTS; i++) {
        text_[i].RunCommands(WARNING_COMMAND->GetValue(iCrossed));
      }
    }

    if (iCrossed <= WARNING_BEEP_START && sound_beep_.IsLoaded() && !silent_) {
      sound_beep_.Play(false);
    }
  }

  if (fNewSecondsLeft == 0) {
    Stop();
    SCREENMAN->PostMessageToTopScreen(SM_MenuTimer, 0);
    for (int i = 0; i < NUM_MENU_TIMER_TEXTS; i++) {
      text_[i].StopEffect();
    }
  }
}

void MenuTimer::Pause() { paused_ = true; }

void MenuTimer::Stop() {
  /* Don't call SetSeconds if we're already at 0: let the existing tweens
   * finish. */
  if (seconds_left_ >= 1) {
    SetSeconds(0);
  }
  Pause();
}

void MenuTimer::Disable() {
  SetSeconds(TIMER_PAUSE_SECONDS);
  Pause();
}

void MenuTimer::Stall() {
  // Max amount of stall time we'll use:
  const float Amt = std::min(0.5f, stall_seconds_left_);

  // Amount of stall time to add:
  const float ToAdd = Amt - stall_seconds_;

  stall_seconds_ += ToAdd;
  stall_seconds_left_ -= ToAdd;
}

void MenuTimer::SetSeconds(float fSeconds) {
  seconds_left_ = fSeconds;

  for (int i = 0; i < NUM_MENU_TIMER_TEXTS; i++) {
    text_[i].PlayCommand("On");
  }

  SetText(fSeconds);
}

void MenuTimer::Start() { paused_ = false; }

void MenuTimer::SetText(float fSeconds) {
  Lua* L = LUA->Get();

  for (int i = 0; i < NUM_MENU_TIMER_TEXTS; i++) {
    // function
    expr_format_text_[i].PushSelf(L);
    ASSERT(!lua_isnil(L, -1));

    // 1st parameter
    LuaHelpers::Push(L, fSeconds);

    // call function with 1 argument and 1 result
    std::string errorMessage =
        ssprintf("Error running Text%dFormatFunction: ", i + 1);
    LuaHelpers::RunScriptOnStack(L, errorMessage, 1, 1, true);

    std::string sText;
    LuaHelpers::Pop(L, sText);

    text_[i].SetText(sText);
  }

  LUA->Release(L);
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the MenuTimer. */
class LunaMenuTimer : public Luna<MenuTimer> {
 public:
  static int SetSeconds(T* p, lua_State* L) {
    p->SetSeconds(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int GetSeconds(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetSeconds());
    return 1;
  }
  static int start(T* p, lua_State* L) {
    p->Start();
    COMMON_RETURN_SELF;
  }
  static int pause(T* p, lua_State* L) {
    p->Pause();
    COMMON_RETURN_SELF;
  }
  static int stop(T* p, lua_State* L) {
    p->Stop();
    COMMON_RETURN_SELF;
  }
  static int disable(T* p, lua_State* L) {
    p->Disable();
    COMMON_RETURN_SELF;
  }
  static int silent(T* p, lua_State* L) {
    p->EnableSilent(BArg(1));
    COMMON_RETURN_SELF;
  }
  static int stealth(T* p, lua_State* L) {
    p->EnableStealth(BArg(1));
    COMMON_RETURN_SELF;
  }

  LunaMenuTimer() {
    ADD_METHOD(SetSeconds);
    ADD_METHOD(GetSeconds);
    ADD_METHOD(start);
    ADD_METHOD(pause);
    ADD_METHOD(stop);
    ADD_METHOD(disable);
    ADD_METHOD(silent);
    ADD_METHOD(stealth);
  }
};

LUA_REGISTER_DERIVED_CLASS(MenuTimer, ActorFrame)
// lua end

/*
 * (c) 2002-2004 Chris Danford
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
