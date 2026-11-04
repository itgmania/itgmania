#include "ComboGraph.h"

#include <algorithm>
#include <string>

#include "Actor.h"
#include "ActorFrame.h"
#include "ActorUtil.h"
#include "BitmapText.h"
#include "LuaManager.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "StageStats.h"
#include "ThemeManager.h"

const int MinComboSizeToShow = 5;

REGISTER_ACTOR_CLASS(ComboGraph);

ComboGraph::ComboGraph() {
  DeleteChildrenWhenDone(true);

  normal_combo_ = nullptr;
  max_combo_ = nullptr;
  combo_number_ = nullptr;
}

void ComboGraph::Load(std::string sMetricsGroup) {
  BODY_WIDTH.Load(sMetricsGroup, "BodyWidth");
  BODY_HEIGHT.Load(sMetricsGroup, "BodyHeight");

  // These need to be set so that a theme can use zoomtowidth/zoomtoheight and
  // get correct behavior.
  this->SetWidth(BODY_WIDTH);
  this->SetHeight(BODY_HEIGHT);

  Actor* pActor = nullptr;

  backing_ = ActorUtil::MakeActor(THEME->GetPathG(sMetricsGroup, "Backing"));
  if (backing_ != nullptr) {
    backing_->ZoomToWidth(BODY_WIDTH);
    backing_->ZoomToHeight(BODY_HEIGHT);
    this->AddChild(backing_);
  }

  normal_combo_ =
      ActorUtil::MakeActor(THEME->GetPathG(sMetricsGroup, "NormalCombo"));
  if (normal_combo_ != nullptr) {
    normal_combo_->ZoomToWidth(BODY_WIDTH);
    normal_combo_->ZoomToHeight(BODY_HEIGHT);
    this->AddChild(normal_combo_);
  }

  max_combo_ = ActorUtil::MakeActor(THEME->GetPathG(sMetricsGroup, "MaxCombo"));
  if (max_combo_ != nullptr) {
    max_combo_->ZoomToWidth(BODY_WIDTH);
    max_combo_->ZoomToHeight(BODY_HEIGHT);
    this->AddChild(max_combo_);
  }

  pActor = ActorUtil::MakeActor(THEME->GetPathG(sMetricsGroup, "ComboNumber"));
  if (pActor != nullptr) {
    combo_number_ = dynamic_cast<BitmapText*>(pActor);
    if (combo_number_ != nullptr) {
      this->AddChild(combo_number_);
    } else {
      LuaHelpers::ReportScriptErrorFmt(
          "ComboGraph: \"sMetricsGroup\" \"ComboNumber\" must be a BitmapText");
    }
  }
}

void ComboGraph::Set(const StageStats& s, const PlayerStageStats& pss) {
  const float fFirstSecond = 0;
  const float fLastSecond = s.GetTotalPossibleStepsSeconds();

  // Find the largest combo.
  int iMaxComboSize = 0;
  for (unsigned i = 0; i < pss.combo_list_.size(); ++i) {
    iMaxComboSize = std::max(iMaxComboSize, pss.combo_list_[i].GetStageCnt());
  }

  for (unsigned i = 0; i < pss.combo_list_.size(); ++i) {
    const PlayerStageStats::Combo_t& combo = pss.combo_list_[i];
    if (combo.GetStageCnt() < MinComboSizeToShow) {
      continue;  // too small
    }

    const bool bIsMax = (combo.GetStageCnt() == iMaxComboSize);

    LOG->Trace(
        "combo %i is %f+%f of %f", i, combo.start_second_, combo.size_seconds_,
        fLastSecond);
    Actor* pSprite = bIsMax ? max_combo_->Copy() : normal_combo_->Copy();

    const float fStart =
        SCALE(combo.start_second_, fFirstSecond, fLastSecond, 0.0f, 1.0f);
    const float fSize =
        SCALE(combo.size_seconds_, 0, fLastSecond - fFirstSecond, 0.0f, 1.0f);
    pSprite->SetCropLeft(SCALE(fSize, 0.0f, 1.0f, 0.5f, 0.0f));
    pSprite->SetCropRight(SCALE(fSize, 0.0f, 1.0f, 0.5f, 0.0f));

    pSprite->SetCropLeft(fStart);
    pSprite->SetCropRight(1 - (fSize + fStart));

    this->AddChild(pSprite);
  }

  for (unsigned i = 0; i < pss.combo_list_.size(); ++i) {
    const PlayerStageStats::Combo_t& combo = pss.combo_list_[i];
    if (combo.GetStageCnt() < MinComboSizeToShow) {
      continue;  // too small
    }

    if (!iMaxComboSize) {
      continue;
    }

    const bool bIsMax = (combo.GetStageCnt() == iMaxComboSize);
    if (!bIsMax) {
      continue;
    }

    BitmapText* pText = combo_number_->Copy();

    const float fStart =
        SCALE(combo.start_second_, fFirstSecond, fLastSecond, 0.0f, 1.0f);
    const float fSize =
        SCALE(combo.size_seconds_, 0, fLastSecond - fFirstSecond, 0.0f, 1.0f);

    const float fCenterPercent = fStart + fSize / 2;
    const float fCenterXPos = SCALE(
        fCenterPercent, 0.0f, 1.0f, -BODY_WIDTH / 2.0f, BODY_WIDTH / 2.0f);
    pText->SetX(fCenterXPos);

    pText->SetText(ssprintf("%i", combo.GetStageCnt()));

    this->AddChild(pText);
  }

  // Hide the templates.
  normal_combo_->SetVisible(false);
  max_combo_->SetVisible(false);
  combo_number_->SetVisible(false);
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the ComboGraph. */
class LunaComboGraph : public Luna<ComboGraph> {
 public:
  static int Load(T* p, lua_State* L) {
    p->Load(SArg(1));
    COMMON_RETURN_SELF;
  }
  static int Set(T* p, lua_State* L) {
    StageStats* pStageStats = Luna<StageStats>::check(L, 1);
    PlayerStageStats* pPlayerStageStats = Luna<PlayerStageStats>::check(L, 2);
    p->Set(*pStageStats, *pPlayerStageStats);
    COMMON_RETURN_SELF;
  }

  LunaComboGraph() {
    ADD_METHOD(Load);
    ADD_METHOD(Set);
  }
};

LUA_REGISTER_DERIVED_CLASS(ComboGraph, ActorFrame)
// lua end

/*
 * (c) 2003 Glenn Maynard
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
