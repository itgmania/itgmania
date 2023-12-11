#include "global.h"

#include "HoldJudgment.h"

#include "ActorUtil.h"
#include "GameConstantsAndTypes.h"
#include "RageUtil.h"
#include "ThemeManager.h"
#include "ThemeMetric.h"
#include "XmlFile.h"

REGISTER_ACTOR_CLASS(HoldJudgment);

HoldJudgment::HoldJudgment() { multiplayer_to_track_ = MultiPlayer_Invalid; }

void HoldJudgment::Load(const RString& path) {
  judgment_.Load(path);
  judgment_->StopAnimating();
  judgment_->SetName("HoldJudgment");
  ActorUtil::LoadAllCommands(*judgment_, "HoldJudgment");
  ResetAnimation();
  this->AddChild(judgment_);
}

void HoldJudgment::LoadFromNode(const XNode* node) {
  RString file;
  if (!ActorUtil::GetAttrPath(node, "File", file)) {
    LuaHelpers::ReportScriptErrorFmt(
        "%s: HoldJudgment: missing the attribute \"File\"",
        ActorUtil::GetWhere(node).c_str());
  }

  CollapsePath(file);

  Load(file);

  ActorFrame::LoadFromNode(node);
}

void HoldJudgment::ResetAnimation() {
  ASSERT(judgment_.IsLoaded());
  judgment_->SetDiffuse(RageColor(1, 1, 1, 0));
  judgment_->SetXY(0, 0);
  judgment_->StopTweening();
  judgment_->StopEffect();
}

void HoldJudgment::SetHoldJudgment(HoldNoteScore hns) {
  // NOTE(Matt): To save API. Command can handle if desired.
  if (hns != HNS_Missed) {
    ResetAnimation();
  }

  switch (hns) {
    case HNS_Held:
      judgment_->SetState(0);
      judgment_->PlayCommand("Held");
      break;
    case HNS_LetGo:
      judgment_->SetState(1);
      judgment_->PlayCommand("LetGo");
      break;
    case HNS_Missed:
			// NOTE(Matt): Not until after 5.0
      // judgment_->SetState( 2 );
      judgment_->PlayCommand("MissedHold");
      break;
    case HNS_None:
    default:
      FAIL_M(ssprintf("Cannot set hold judgment to %i", hns));
  }
}

void HoldJudgment::LoadFromMultiPlayer(MultiPlayer multiplayer) {
	// Assert only load once.
  ASSERT(multiplayer_to_track_ == MultiPlayer_Invalid);
  multiplayer_to_track_ = multiplayer;
  this->SubscribeToMessage("Judgment");
}

void HoldJudgment::HandleMessage(const Message& msg) {
  if (multiplayer_to_track_ != MultiPlayer_Invalid &&
			msg.GetName() == "Judgment") {
    MultiPlayer multiplayer;
    if (msg.GetParam("MultiPlayer", multiplayer) &&
				multiplayer == multiplayer_to_track_) {
      HoldNoteScore hns;
      if (msg.GetParam("HoldNoteScore", hns)) {
        SetHoldJudgment(hns);
      }
    }
  }

  ActorFrame::HandleMessage(msg);
}

// lua start
#include "LuaBinding.h"

// Allow Lua to have access to the HoldJudgment.
class LunaHoldJudgment : public Luna<HoldJudgment> {
 public:
  static int LoadFromMultiPlayer(T* p, lua_State* L) {
    p->LoadFromMultiPlayer(Enum::Check<MultiPlayer>(L, 1));
    COMMON_RETURN_SELF;
  }

  LunaHoldJudgment() { ADD_METHOD(LoadFromMultiPlayer); }
};

LUA_REGISTER_DERIVED_CLASS(HoldJudgment, ActorFrame)
// lua end

/*
 * (c) 2001-2004 Chris Danford
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
