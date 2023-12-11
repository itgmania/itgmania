#include "global.h"

#include "ComboGraph.h"

#include "ActorUtil.h"
#include "BitmapText.h"
#include "RageLog.h"
#include "StageStats.h"
#include "XmlFile.h"

const int kMinComboSizeToShow = 5;

REGISTER_ACTOR_CLASS(ComboGraph);

ComboGraph::ComboGraph() {
  DeleteChildrenWhenDone(true);

  normal_combo_ = nullptr;
  max_combo_ = nullptr;
  combo_number_ = nullptr;
}

void ComboGraph::Load(RString metrics_group) {
  BODY_WIDTH.Load(metrics_group, "BodyWidth");
  BODY_HEIGHT.Load(metrics_group, "BodyHeight");

  // These need to be set so that a theme can use zoomtowidth/zoomtoheight and
  // get correct behavior.
  this->SetWidth(BODY_WIDTH);
  this->SetHeight(BODY_HEIGHT);

  Actor* actor = nullptr;

  backing_ = ActorUtil::MakeActor(THEME->GetPathG(metrics_group, "Backing"));
  if (backing_ != nullptr) {
    backing_->ZoomToWidth(BODY_WIDTH);
    backing_->ZoomToHeight(BODY_HEIGHT);
    this->AddChild(backing_);
  }

  normal_combo_ =
      ActorUtil::MakeActor(THEME->GetPathG(metrics_group, "NormalCombo"));
  if (normal_combo_ != nullptr) {
    normal_combo_->ZoomToWidth(BODY_WIDTH);
    normal_combo_->ZoomToHeight(BODY_HEIGHT);
    this->AddChild(normal_combo_);
  }

  max_combo_ =
      ActorUtil::MakeActor(THEME->GetPathG(metrics_group, "MaxCombo"));
  if (max_combo_ != nullptr) {
    max_combo_->ZoomToWidth(BODY_WIDTH);
    max_combo_->ZoomToHeight(BODY_HEIGHT);
    this->AddChild(max_combo_);
  }

  actor = ActorUtil::MakeActor(THEME->GetPathG(metrics_group, "ComboNumber"));
  if (actor != nullptr) {
    combo_number_ = dynamic_cast<BitmapText*>(actor);
    if (combo_number_ != nullptr) {
      this->AddChild(combo_number_);
    } else {
      LuaHelpers::ReportScriptErrorFmt(
          "ComboGraph: \"metrics_group\" \"ComboNumber\" must be a BitmapText");
    }
  }
}

void ComboGraph::Set(const StageStats& s, const PlayerStageStats& pss) {
  const float first_second = 0;
  const float last_second = s.GetTotalPossibleStepsSeconds();

  // Find the largest combo.
  int max_combo_size = 0;
  for (unsigned i = 0; i < pss.m_ComboList.size(); ++i) {
    max_combo_size = std::max(max_combo_size, pss.m_ComboList[i].GetStageCnt());
  }

  for (unsigned i = 0; i < pss.m_ComboList.size(); ++i) {
    const PlayerStageStats::Combo_t& combo = pss.m_ComboList[i];
    if (combo.GetStageCnt() < kMinComboSizeToShow) {
      continue;  // too small
    }

    const bool is_max = (combo.GetStageCnt() == max_combo_size);

    LOG->Trace(
        "combo %i is %f+%f of %f", i, combo.m_fStartSecond,
        combo.m_fSizeSeconds, last_second);
    Actor* sprite = is_max ? max_combo_->Copy() : normal_combo_->Copy();

    const float start =
        SCALE(combo.m_fStartSecond, first_second, last_second, 0.0f, 1.0f);
    const float size =
        SCALE(combo.m_fSizeSeconds, 0, last_second - first_second, 0.0f, 1.0f);
    sprite->SetCropLeft(SCALE(size, 0.0f, 1.0f, 0.5f, 0.0f));
    sprite->SetCropRight(SCALE(size, 0.0f, 1.0f, 0.5f, 0.0f));

    sprite->SetCropLeft(start);
    sprite->SetCropRight(1 - (size + start));

    this->AddChild(sprite);
  }

  for (unsigned i = 0; i < pss.m_ComboList.size(); ++i) {
    const PlayerStageStats::Combo_t& combo = pss.m_ComboList[i];
    if (combo.GetStageCnt() < kMinComboSizeToShow) {
      continue;  // too small
    }

    if (!max_combo_size) {
      continue;
    }

    const bool is_max = (combo.GetStageCnt() == max_combo_size);
    if (!is_max) {
      continue;
    }

    BitmapText* text = combo_number_->Copy();

    const float start =
        SCALE(combo.m_fStartSecond, first_second, last_second, 0.0f, 1.0f);
    const float size =
        SCALE(combo.m_fSizeSeconds, 0, last_second - first_second, 0.0f, 1.0f);

    const float fCenterPercent = start + size / 2;
    const float fCenterXPos = SCALE(
        fCenterPercent, 0.0f, 1.0f, -BODY_WIDTH / 2.0f, BODY_WIDTH / 2.0f);
    text->SetX(fCenterXPos);

    text->SetText(ssprintf("%i", combo.GetStageCnt()));

    this->AddChild(text);
  }

  // Hide the templates.
  normal_combo_->SetVisible(false);
  max_combo_->SetVisible(false);
  combo_number_->SetVisible(false);
}

// lua start
#include "LuaBinding.h"

// Allow Lua to have access to the ComboGraph.
class LunaComboGraph : public Luna<ComboGraph> {
 public:
  static int Load(T* p, lua_State* L) {
    p->Load(SArg(1));
    COMMON_RETURN_SELF;
  }

  static int Set(T* p, lua_State* L) {
    StageStats* stage_stats = Luna<StageStats>::check(L, 1);
    PlayerStageStats* player_stage_stats = Luna<PlayerStageStats>::check(L, 2);
    p->Set(*stage_stats, *player_stage_stats);
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
