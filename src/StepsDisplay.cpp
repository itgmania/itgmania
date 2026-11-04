#include "StepsDisplay.h"

#include <algorithm>
#include <string>

#include "ActorFrame.h"
#include "ActorUtil.h"
#include "Difficulty.h"
#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "GameState.h"
#include "LuaBinding.h"
#include "LuaManager.h"
#include "MessageManager.h"
#include "PlayerNumber.h"
#include "PlayerState.h"
#include "RageUtil.h"
#include "SongManager.h"
#include "Steps.h"
#include "ThemeManager.h"
#include "global.h"

REGISTER_ACTOR_CLASS(StepsDisplay);

StepsDisplay::StepsDisplay() {}

/* sID experiment:
 *
 * Names of an actor, "Foo":
 * [Foo]
 * Metric=abc
 *
 * [ScreenSomething]
 * FooP1X=20
 * FooP2Y=30
 *
 * Graphics\Foo under p1
 *
 * We want to call it different things in different contexts: we may only want
 * one set of internal metrics for a given use, but separate metrics for each
 * player at the screen level, and we may or may not want separate names at the
 * asset level.
 *
 * As is, we tend to end up having to either duplicate [Foo] to [FooP1] and
 * [FooP2] or not use m_sName for [Foo], which limits its use.  Let's try using
 * a separate name for internal metrics.  I'm not sure if this will cause more
 * confusion than good, so I'm trying it first in only this object.
 */

void StepsDisplay::Load(
    const std::string& sMetricsGroup, const PlayerState* pPlayerState) {
  metrics_group_ = sMetricsGroup;

  /* We can't use global ThemeMetric<std::string>s, because we can have multiple
   * StepsDisplays on screen at once, with different names. */
  num_ticks_.Load(metrics_group_, "NumTicks");
  max_ticks_.Load(metrics_group_, "MaxTicks");
  show_ticks_.Load(metrics_group_, "ShowTicks");
  show_meter_.Load(metrics_group_, "ShowMeter");
  show_description_.Load(metrics_group_, "ShowDescription");
  show_credit_.Load(metrics_group_, "ShowCredit");
  show_autogen_.Load(metrics_group_, "ShowAutogen");
  show_steps_type_.Load(metrics_group_, "ShowStepsType");
  zero_meter_string_.Load(metrics_group_, "ZeroMeterString");
  meter_format_string_.Load(metrics_group_, "MeterFormatString");

  spr_frame_.Load(THEME->GetPathG(metrics_group_, "frame"));
  spr_frame_->SetName("Frame");
  ActorUtil::LoadAllCommandsAndSetXYAndOnCommand(spr_frame_, metrics_group_);
  this->AddChild(spr_frame_);

  if (show_ticks_) {
    std::string sChars = "10";  // on, off (todo: make this metricable -aj)
    text_ticks_.SetName("Ticks");
    text_ticks_.LoadFromTextureAndChars(
        THEME->GetPathF(metrics_group_, "ticks"), sChars);
    ActorUtil::LoadAllCommandsAndSetXYAndOnCommand(text_ticks_, metrics_group_);
    this->AddChild(&text_ticks_);
  }

  if (show_meter_) {
    text_meter_.SetName("Meter");
    text_meter_.LoadFromFont(THEME->GetPathF(metrics_group_, "meter"));
    ActorUtil::LoadAllCommandsAndSetXYAndOnCommand(text_meter_, metrics_group_);
    this->AddChild(&text_meter_);

    // These commands should have been loaded by SetXYAndOnCommand above.
    ASSERT(text_meter_.HasCommand("Set"));
  }

  if (show_description_) {
    text_description_.SetName("Description");
    text_description_.LoadFromFont(
        THEME->GetPathF(metrics_group_, "Description"));
    ActorUtil::LoadAllCommandsAndSetXYAndOnCommand(
        text_description_, metrics_group_);
    this->AddChild(&text_description_);
  }
  if (show_credit_) {
    text_author_.SetName("Step Author");
    text_author_.LoadFromFont(THEME->GetPathF(metrics_group_, "Credit"));
    ActorUtil::LoadAllCommandsAndSetXYAndOnCommand(
        text_author_, metrics_group_);
    this->AddChild(&text_author_);
  }

  if (show_autogen_) {
    spr_autogen_.Load(THEME->GetPathG(metrics_group_, "Autogen"));
    spr_autogen_->SetName("Autogen");
    ActorUtil::LoadAllCommandsAndSetXYAndOnCommand(
        spr_autogen_, metrics_group_);
    this->AddChild(spr_autogen_);
  }

  if (show_steps_type_) {
    spr_steps_type_.Load(THEME->GetPathG(metrics_group_, "StepsType"));
    spr_steps_type_->SetName("StepsType");
    ActorUtil::LoadAllCommandsAndSetXYAndOnCommand(
        spr_steps_type_, metrics_group_);
    this->AddChild(spr_steps_type_);
  }

  // Play Load Command
  PlayerState* pPlayerState_ = const_cast<PlayerState*>(pPlayerState);
  Message msg("Load");
  if (pPlayerState_) {
    msg.SetParam("PlayerState", LuaReference::CreateFromPush(*pPlayerState_));
  }
  this->HandleMessage(msg);

  Unset();
}

void StepsDisplay::SetFromGameState(PlayerNumber pn) {
  if (GAMESTATE->IsCourseMode()) {
    // figure out what course type is selected somehow.
    const Trail* pTrail = GAMESTATE->cur_trail_[pn];
    if (pTrail) {
      SetFromTrail(pTrail);
    } else {
      SetFromStepsTypeAndMeterAndDifficultyAndCourseType(
          StepsType_Invalid, 0, GAMESTATE->preferred_course_difficulty_[pn],
          CourseType_Invalid);
    }
  } else {
    const Steps* pSteps = GAMESTATE->cur_steps_[pn];
    if (pSteps) {
      SetFromSteps(pSteps);
    } else {
      SetFromStepsTypeAndMeterAndDifficultyAndCourseType(
          StepsType_Invalid, 0, GAMESTATE->preferred_difficulty_[pn],
          CourseType_Invalid);
    }
  }
}

void StepsDisplay::SetFromSteps(const Steps* pSteps) {
  if (pSteps == nullptr) {
    Unset();
    return;
  }

  SetParams params = {
      pSteps,
      nullptr,
      pSteps->GetMeter(),
      pSteps->steps_type_,
      pSteps->GetDifficulty(),
      CourseType_Invalid};
  SetInternal(params);
}

void StepsDisplay::SetFromTrail(const Trail* pTrail) {
  if (pTrail == nullptr) {
    Unset();
    return;
  }

  SetParams params = {
      nullptr,
      pTrail,
      pTrail->GetMeter(),
      pTrail->steps_type_,
      pTrail->course_difficulty_,
      pTrail->course_type_};
  SetInternal(params);
}

void StepsDisplay::Unset() { this->SetVisible(false); }

void StepsDisplay::SetFromStepsTypeAndMeterAndDifficultyAndCourseType(
    StepsType st, int iMeter, Difficulty dc, CourseType ct) {
  SetParams params = {nullptr, nullptr, iMeter, st, dc, ct};
  SetInternal(params);
}

void StepsDisplay::SetInternal(const SetParams& params) {
  this->SetVisible(true);
  Message msg("Set");

  std::string sCustomDifficulty;
  if (params.steps_) {
    sCustomDifficulty = StepsToCustomDifficulty(params.steps_);
  } else if (params.trail_) {
    sCustomDifficulty = TrailToCustomDifficulty(params.trail_);
  } else {
    sCustomDifficulty = GetCustomDifficulty(params.st_, params.dc_, params.ct_);
  }
  msg.SetParam("CustomDifficulty", sCustomDifficulty);

  std::string sDisplayDescription;
  if (params.steps_ && params.steps_->IsAnEdit()) {
    sDisplayDescription = params.steps_->GetDescription();
  } else if (sCustomDifficulty.empty()) {
    sDisplayDescription = std::string();
  } else {
    sDisplayDescription = CustomDifficultyToLocalizedString(sCustomDifficulty);
  }
  msg.SetParam("DisplayDescription", sDisplayDescription);

  std::string sDisplayCredit;
  if (params.steps_) {
    sDisplayCredit = params.steps_->GetCredit();
  }

  if (params.steps_) {
    msg.SetParam("Steps", LuaReference::CreateFromPush(*(Steps*)params.steps_));
  }
  if (params.trail_) {
    msg.SetParam("Trail", LuaReference::CreateFromPush(*(Trail*)params.trail_));
  }
  msg.SetParam("Meter", params.meter_);
  msg.SetParam("StepsType", params.st_);

  spr_frame_->HandleMessage(msg);

  if (show_ticks_) {
    // todo: let themers handle the logic of tick text. -aj
    char on = char('1');
    char off = '0';

    std::string sNewText;
    int iNumOn = std::min((int)max_ticks_, params.meter_);
    sNewText.insert(sNewText.end(), iNumOn, on);
    int iNumOff = std::max(0, num_ticks_ - iNumOn);
    sNewText.insert(sNewText.end(), iNumOff, off);
    text_ticks_.SetText(sNewText);
    text_ticks_.HandleMessage(msg);
  }

  if (show_meter_) {
    if (params.meter_ == 0)  // Unset calls with this
    {
      text_meter_.SetText(zero_meter_string_);
    } else {
      const std::string sMeter =
          ssprintf(meter_format_string_.GetValue().c_str(), params.meter_);
      text_meter_.SetText(sMeter);
      text_meter_.HandleMessage(msg);
    }
  }

  if (show_description_) {
    text_description_.SetText(sDisplayDescription);
    text_description_.HandleMessage(msg);
  }
  if (show_credit_) {
    text_author_.SetText(sDisplayCredit);
    text_author_.HandleMessage(msg);
  }

  if (show_autogen_) {
    bool b = params.steps_ && params.steps_->IsAutogen();
    spr_autogen_->HandleMessage(msg);
    spr_autogen_->SetVisible(b);
  }

  if (show_steps_type_) {
    if (params.st_ != StepsType_Invalid) {
      /*
      std::string sStepsType = GAMEMAN->GetStepsTypeInfo(params.st).szName;
      m_sprStepsType.Load( THEME->GetPathG(m_sMetricsGroup,"StepsType
      "+sStepsType) );
      */
      spr_steps_type_->HandleMessage(msg);
    }
  }

  this->HandleMessage(msg);
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the StepsDisplay. */
class LunaStepsDisplay : public Luna<StepsDisplay> {
 public:
  static int Load(T* p, lua_State* L) {
    p->Load(SArg(1), nullptr);
    COMMON_RETURN_SELF;
  }
  static int SetFromSteps(T* p, lua_State* L) {
    if (lua_isnil(L, 1)) {
      p->SetFromSteps(nullptr);
    } else {
      Steps* pS = Luna<Steps>::check(L, 1);
      p->SetFromSteps(pS);
    }
    COMMON_RETURN_SELF;
  }
  static int SetFromTrail(T* p, lua_State* L) {
    if (lua_isnil(L, 1)) {
      p->SetFromTrail(nullptr);
    } else {
      Trail* pT = Luna<Trail>::check(L, 1);
      p->SetFromTrail(pT);
    }
    COMMON_RETURN_SELF;
  }
  static int SetFromGameState(T* p, lua_State* L) {
    PlayerNumber pn = Enum::Check<PlayerNumber>(L, 1);
    p->SetFromGameState(pn);
    COMMON_RETURN_SELF;
  }

  LunaStepsDisplay() {
    ADD_METHOD(Load);
    ADD_METHOD(SetFromSteps);
    ADD_METHOD(SetFromTrail);
    ADD_METHOD(SetFromGameState);
  }
};

LUA_REGISTER_DERIVED_CLASS(StepsDisplay, ActorFrame)
// lua end

/*
 * (c) 2001-2004 Chris Danford, Glenn Maynard
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
