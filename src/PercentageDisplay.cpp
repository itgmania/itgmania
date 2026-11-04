#include "PercentageDisplay.h"

#include <algorithm>
#include <string>

#include "ActorFrame.h"
#include "ActorUtil.h"
#include "Course.h"
#include "GameState.h"
#include "LuaManager.h"
#include "PlayerNumber.h"
#include "PlayerStageStats.h"
#include "PlayerState.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "StdString.h"
#include "ThemeManager.h"
#include "XmlFile.h"
#include "global.h"

REGISTER_ACTOR_CLASS(PercentageDisplay);

PercentageDisplay::PercentageDisplay() {
  player_state_ = nullptr;
  player_stage_stats_ = nullptr;

  last_ = -1;
  last_max_ = -1;
  dance_points_digits_ = 0;
  use_remainder_ = false;
  auto_refresh_ = false;
  format_percent_score_.SetFromExpression("FormatPercentScore");
}

void PercentageDisplay::LoadFromNode(const XNode* pNode) {
  pNode->GetAttrValue("DancePointsDigits", dance_points_digits_);
  pNode->GetAttrValue("AutoRefresh", auto_refresh_);
  {
    Lua* L = LUA->Get();
    if (pNode->PushAttrValue(L, "FormatPercentScore")) {
      format_percent_score_.SetFromStack(L);
      if (format_percent_score_.GetLuaType() != LUA_TFUNCTION) {
        // Not reported as an error because _fallback and default provided bad
        // examples in their [LifeMeterBattery Percent]:Format metric and nobody
        // realized it was supposed to be set to a function. -Kyz
        LOG->Trace(
            "Format attribute for PercentageDisplay named '%s' is not a "
            "function. Defaulting to 'FormatPercentScore'.",
            GetName().c_str());
        format_percent_score_.SetFromExpression("FormatPercentScore");
      }
    } else {
      lua_pop(L, 1);
    }
    LUA->Release(L);
  }

  const XNode* pChild = pNode->GetChild("Percent");
  if (pChild == nullptr) {
    LuaHelpers::ReportScriptError(
        ActorUtil::GetWhere(pNode) +
        ": PercentageDisplay: missing the node \"Percent\"");
    // Make a BitmapText just so we don't crash.
    text_percent_.LoadFromFont(THEME->GetPathF("", "Common Normal"));
  } else {
    text_percent_.LoadFromNode(pChild);
  }
  this->AddChild(&text_percent_);

  pChild = pNode->GetChild("PercentRemainder");
  if (!ShowDancePointsNotPercentage() && pChild != nullptr) {
    use_remainder_ = true;
    text_percent_remainder_.LoadFromNode(pChild);
    this->AddChild(&text_percent_remainder_);
  }

  // only run the Init command after we load Fonts.
  ActorFrame::LoadFromNode(pNode);
}

void PercentageDisplay::Load(
    const PlayerState* pPlayerState,
    const PlayerStageStats* pPlayerStageStats) {
  player_state_ = pPlayerState;
  player_stage_stats_ = pPlayerStageStats;

  Refresh();
}

void PercentageDisplay::Load(
    const PlayerState* pPlayerState, const PlayerStageStats* pPlayerStageStats,
    const std::string& sMetricsGroup, bool bAutoRefresh) {
  player_state_ = pPlayerState;
  player_stage_stats_ = pPlayerStageStats;
  auto_refresh_ = bAutoRefresh;

  dance_points_digits_ = THEME->GetMetricI(sMetricsGroup, "DancePointsDigits");
  use_remainder_ = THEME->GetMetricB(sMetricsGroup, "PercentUseRemainder");
  format_percent_score_ = THEME->GetMetricR(sMetricsGroup, "Format");

  percent_format_ = THEME->GetMetric(sMetricsGroup, "PercentFormat");
  remainder_format_ = THEME->GetMetric(sMetricsGroup, "RemainderFormat");

  if (format_percent_score_.GetLuaType() != LUA_TFUNCTION) {
    // Not reported as an error because _fallback and default provided bad
    // examples in their [LifeMeterBattery Percent]:Format metric and nobody
    // realized it was supposed to be set to a function. -Kyz
    LOG->Trace(
        "Format metric is not a function in [%s]. Defaulting to "
        "'FormatPercentScore'.",
        sMetricsGroup.c_str());
    format_percent_score_.SetFromExpression("FormatPercentScore");
  }

  if (ShowDancePointsNotPercentage()) {
    text_percent_.SetName(
        "DancePoints" + PlayerNumberToString(player_state_->player_number_));
  } else {
    text_percent_.SetName(
        "Percent" + PlayerNumberToString(player_state_->player_number_));
  }

  text_percent_.LoadFromFont(THEME->GetPathF(sMetricsGroup, "text"));
  ActorUtil::SetXY(text_percent_, sMetricsGroup);
  ActorUtil::LoadAllCommands(text_percent_, sMetricsGroup);
  this->AddChild(&text_percent_);

  if (!ShowDancePointsNotPercentage() && use_remainder_) {
    text_percent_remainder_.SetName(
        "PercentRemainder" +
        PlayerNumberToString(player_state_->player_number_));
    text_percent_remainder_.LoadFromFont(
        THEME->GetPathF(sMetricsGroup, "remainder"));
    ActorUtil::SetXY(text_percent_remainder_, sMetricsGroup);
    ActorUtil::LoadAllCommands(text_percent_remainder_, sMetricsGroup);
    ASSERT(text_percent_remainder_.HasCommand("Off"));
    text_percent_remainder_.SetText("456");
    this->AddChild(&text_percent_remainder_);
  }

  Refresh();
}

void PercentageDisplay::Update(float fDeltaTime) {
  ActorFrame::Update(fDeltaTime);

  if (auto_refresh_) {
    Refresh();
  }
}

void PercentageDisplay::Refresh() {
  const int iActualDancePoints = player_stage_stats_->actual_dance_points_;
  const int iCurPossibleDancePoints =
      player_stage_stats_->cur_possible_dance_points_;

  if (iActualDancePoints == last_ && iCurPossibleDancePoints == last_max_) {
    return;
  }

  last_ = iActualDancePoints;
  last_max_ = iCurPossibleDancePoints;

  std::string sNumToDisplay;

  if (ShowDancePointsNotPercentage()) {
    sNumToDisplay =
        ssprintf("%*d", dance_points_digits_, std::max(0, iActualDancePoints));
  } else {
    float fPercentDancePoints = player_stage_stats_->GetPercentDancePoints();

    // clamp percentage - feedback is that negative numbers look weird here.
    rage_clamp(fPercentDancePoints, 0.f, 1.f);

    if (use_remainder_) {
      int iPercentWhole = int(fPercentDancePoints * 100);
      int iPercentRemainder = int(
          (fPercentDancePoints * 100 - int(fPercentDancePoints * 100)) * 10);
      sNumToDisplay = ssprintf(percent_format_.c_str(), iPercentWhole);
      text_percent_remainder_.SetText(
          ssprintf(remainder_format_.c_str(), iPercentRemainder));
    } else {
      if (format_percent_score_.GetLuaType() == LUA_TFUNCTION) {
        Lua* L = LUA->Get();
        format_percent_score_.PushSelf(L);
        LuaHelpers::Push(L, fPercentDancePoints);
        std::string Error = "Error running FormatPercentScore: ";
        LuaHelpers::RunScriptOnStack(L, Error, 1, 1, true);  // 1 arg, 1 result
        LuaHelpers::Pop(L, sNumToDisplay);
        LUA->Release(L);
      }

      // HACK: Use the last frame in the numbers texture as '-'
      Replace(sNumToDisplay, '-', 'x');
    }
  }

  text_percent_.SetText(sNumToDisplay);
}

bool PercentageDisplay::ShowDancePointsNotPercentage() const {
  // Use straight dance points in workout because the percentage denominator
  // isn't accurate - we don't know when the players are going to stop.

  if (GAMESTATE->cur_course_) {
    if (GAMESTATE->cur_course_->goal_seconds_ > 0) {
      return true;
    }
  }

  return false;
}

#include "LuaBinding.h"

/** @brief Allow Lua to have access to the PercentageDisplay. */
class LunaPercentageDisplay : public Luna<PercentageDisplay> {
 public:
  static int LoadFromStats(T* p, lua_State* L) {
    const PlayerState* pStageStats = Luna<PlayerState>::check(L, 1);
    const PlayerStageStats* pPlayerStageStats =
        Luna<PlayerStageStats>::check(L, 2);
    p->Load(pStageStats, pPlayerStageStats);
    COMMON_RETURN_SELF;
  }

  LunaPercentageDisplay() { ADD_METHOD(LoadFromStats); }
};

LUA_REGISTER_DERIVED_CLASS(PercentageDisplay, ActorFrame)
// lua end

/*
 * (c) 2001-2003 Chris Danford
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
