#include "HoldJudgment.h"

#include <string>

#include "ActorUtil.h"
#include "GameConstantsAndTypes.h"
#include "LuaManager.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "global.h"

REGISTER_ACTOR_CLASS(HoldJudgment);

HoldJudgment::HoldJudgment() {}

void HoldJudgment::Load(const std::string& sPath) {
  spr_judgment_.Load(sPath);
  spr_judgment_->StopAnimating();
  spr_judgment_->SetName("HoldJudgment");
  ActorUtil::LoadAllCommands(*spr_judgment_, "HoldJudgment");
  ResetAnimation();
  this->AddChild(spr_judgment_);
}

void HoldJudgment::LoadFromNode(const XNode* pNode) {
  std::string sFile;
  if (!ActorUtil::GetAttrPath(pNode, "File", sFile)) {
    LuaHelpers::ReportScriptErrorFmt(
        "%s: HoldJudgment: missing the attribute \"File\"",
        ActorUtil::GetWhere(pNode).c_str());
  }

  CollapsePath(sFile);

  Load(sFile);

  ActorFrame::LoadFromNode(pNode);
}

void HoldJudgment::ResetAnimation() {
  ASSERT(spr_judgment_.IsLoaded());
  spr_judgment_->SetDiffuse(RageColor(1, 1, 1, 0));
  spr_judgment_->SetXY(0, 0);
  spr_judgment_->StopTweening();
  spr_judgment_->StopEffect();
}

void HoldJudgment::SetHoldJudgment(HoldNoteScore hns) {
  // LOG->Trace( "Judgment::SetJudgment()" );

  // Matt: To save API. Command can handle if desired.
  if (hns != HNS_Missed) {
    ResetAnimation();
  }

  switch (hns) {
    case HNS_Held:
      spr_judgment_->SetState(0);
      spr_judgment_->PlayCommand("Held");
      break;
    case HNS_LetGo:
      spr_judgment_->SetState(1);
      spr_judgment_->PlayCommand("LetGo");
      break;
    case HNS_Missed:
      // m_sprJudgment->SetState( 2 ); // Matt: Not until after 5.0
      spr_judgment_->PlayCommand("MissedHold");
      break;
    case HNS_None:
    default:
      FAIL_M(ssprintf("Cannot set hold judgment to %i", hns));
  }
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the HoldJudgment. */
class LunaHoldJudgment : public Luna<HoldJudgment> {
 public:
  LunaHoldJudgment() {}
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
