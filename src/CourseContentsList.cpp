#include "CourseContentsList.h"

#include <string>

#include "Actor.h"
#include "ActorScroller.h"
#include "ActorUtil.h"
#include "Course.h"
#include "Difficulty.h"
#include "GameState.h"
#include "LuaManager.h"
#include "MessageManager.h"
#include "PlayerNumber.h"
#include "RageUtil.h"
#include "Steps.h"
#include "Trail.h"
#include "XmlFile.h"

REGISTER_ACTOR_CLASS(CourseContentsList);

CourseContentsList::~CourseContentsList() {
  for (Actor* d : display_) {
    delete d;
  }
  display_.clear();
}

void CourseContentsList::LoadFromNode(const XNode* pNode) {
  int iMaxSongs = 5;
  pNode->GetAttrValue("MaxSongs", iMaxSongs);

  const XNode* pDisplayNode = pNode->GetChild("Display");
  if (pDisplayNode == nullptr) {
    LuaHelpers::ReportScriptErrorFmt(
        "%s: CourseContentsList: missing the Display child",
        ActorUtil::GetWhere(pNode).c_str());
    return;
  }

  for (int i = 0; i < iMaxSongs; i++) {
    Actor* pDisplay = ActorUtil::LoadFromNode(pDisplayNode, this);
    pDisplay->SetUseZBuffer(true);
    display_.push_back(pDisplay);
  }

  ActorScroller::LoadFromNode(pNode);
}

void CourseContentsList::SetFromGameState() {
  RemoveAllChildren();

  if (GAMESTATE->GetMasterPlayerNumber() == PlayerNumber_Invalid) {
    return;
  }
  const Trail* pMasterTrail =
      GAMESTATE->cur_trail_[GAMESTATE->GetMasterPlayerNumber()];
  if (pMasterTrail == nullptr) {
    return;
  }
  unsigned uNumEntriesToShow = pMasterTrail->entries_.size();
  rage_clamp(uNumEntriesToShow, 0, display_.size());

  for (int i = 0; i < (int)uNumEntriesToShow; i++) {
    Actor* pDisplay = display_[i];
    SetItemFromGameState(pDisplay, i);
    this->AddChild(pDisplay);
  }

  bool bLoop = pMasterTrail->entries_.size() > uNumEntriesToShow;

  this->SetLoop(bLoop);
  this->Load2();
  this->SetTransformFromHeight(display_[0]->GetUnzoomedHeight());
  this->EnableMask(
      display_[0]->GetUnzoomedWidth(), display_[0]->GetUnzoomedHeight());

  if (bLoop) {
    SetPauseCountdownSeconds(1.5f);
    this->SetDestinationItem((float)display_.size() + 1);  // loop forever
  }
}

void CourseContentsList::SetItemFromGameState(
    Actor* pActor, int iCourseEntryIndex) {
  const Course* pCourse = GAMESTATE->cur_course_;

  FOREACH_HumanPlayer(pn) {
    const Trail* pTrail = GAMESTATE->cur_trail_[pn];
    if (pTrail == nullptr ||
        iCourseEntryIndex >= (int)pTrail->entries_.size() ||
        iCourseEntryIndex >= (int)pCourse->entries_.size()) {
      continue;
    }

    const TrailEntry* te = &pTrail->entries_[iCourseEntryIndex];
    const CourseEntry* ce = &pCourse->entries_[iCourseEntryIndex];
    if (te == nullptr) {
      continue;
    }

    std::string s;
    Difficulty dc;
    if (te->secret_) {
      if (ce == nullptr) {
        continue;
      }

      int iLow = ce->steps_criteria_.low_meter_;
      int iHigh = ce->steps_criteria_.high_meter_;

      bool bLowIsSet = iLow != -1;
      bool bHighIsSet = iHigh != -1;

      if (!bLowIsSet && !bHighIsSet) {
        s = "?";
      }
      if (!bLowIsSet && bHighIsSet) {
        s = ssprintf(">=%d", iHigh);
      } else if (bLowIsSet && !bHighIsSet) {
        s = ssprintf("<=%d", iLow);
      } else if (bLowIsSet && bHighIsSet) {
        if (iLow == iHigh) {
          s = ssprintf("%d", iLow);
        } else {
          s = ssprintf("%d-%d", iLow, iHigh);
        }
      }

      dc = te->dc_;
      if (dc == Difficulty_Invalid) {
        dc = Difficulty_Edit;
      }
    } else {
      s = ssprintf("%d", te->steps_->GetMeter());
      dc = te->steps_->GetDifficulty();
    }

    Message msg("SetSong");
    msg.SetParam("PlayerNumber", pn);
    msg.SetParam("Song", te->song_);
    msg.SetParam("Steps", te->steps_);
    msg.SetParam("Difficulty", dc);
    msg.SetParam("Meter", s);
    msg.SetParam("Number", iCourseEntryIndex + 1);
    msg.SetParam("Modifiers", te->modifiers_);
    msg.SetParam("Secret", te->secret_);
    pActor->HandleMessage(msg);
  }
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the CourseContentsList. */
class LunaCourseContentsList : public Luna<CourseContentsList> {
 public:
  static int SetFromGameState(T* p, lua_State* L) {
    p->SetFromGameState();
    COMMON_RETURN_SELF;
  }

  LunaCourseContentsList() { ADD_METHOD(SetFromGameState); }
};

LUA_REGISTER_DERIVED_CLASS(CourseContentsList, ActorScroller)
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
