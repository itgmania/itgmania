#include "global.h"

#include "CourseContentsList.h"

#include "ActorUtil.h"
#include "Course.h"
#include "GameConstantsAndTypes.h"
#include "GameState.h"
#include "RageLog.h"
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

void CourseContentsList::LoadFromNode(const XNode* node) {
  int max_songs = 5;
  node->GetAttrValue("MaxSongs", max_songs);

  const XNode* display_node = node->GetChild("Display");
  if (display_node == nullptr) {
    LuaHelpers::ReportScriptErrorFmt(
        "%s: CourseContentsList: missing the Display child",
        ActorUtil::GetWhere(node).c_str());
    return;
  }

  for (int i = 0; i < max_songs; ++i) {
    Actor* display = ActorUtil::LoadFromNode(display_node, this);
    display->SetUseZBuffer(true);
    display_.push_back(display);
  }

  ActorScroller::LoadFromNode(node);
}

void CourseContentsList::SetFromGameState() {
  RemoveAllChildren();

  if (GAMESTATE->GetMasterPlayerNumber() == PlayerNumber_Invalid) {
    return;
  }
  const Trail* master_trail =
      GAMESTATE->m_pCurTrail[GAMESTATE->GetMasterPlayerNumber()];
  if (master_trail == nullptr) {
    return;
  }
  unsigned num_entries_to_show = master_trail->m_vEntries.size();
  CLAMP(num_entries_to_show, 0, display_.size());

  for (int i = 0; i < (int)num_entries_to_show; i++) {
    Actor* display = display_[i];
    SetItemFromGameState(display, i);
    this->AddChild(display);
  }

  bool loop = master_trail->m_vEntries.size() > num_entries_to_show;

  this->SetLoop(loop);
  this->Load2();
  this->SetTransformFromHeight(display_[0]->GetUnzoomedHeight());
  this->EnableMask(
      display_[0]->GetUnzoomedWidth(), display_[0]->GetUnzoomedHeight());

  if (loop) {
    SetPauseCountdownSeconds(1.5f);
    this->SetDestinationItem((float)display_.size() + 1);  // loop forever
  }
}

void CourseContentsList::SetItemFromGameState(
    Actor* actor, int course_entry_index) {
  const Course* pCourse = GAMESTATE->m_pCurCourse;

  FOREACH_HumanPlayer(pn) {
    const Trail* trail = GAMESTATE->m_pCurTrail[pn];
    if (trail == nullptr ||
        course_entry_index >= (int)trail->m_vEntries.size() ||
        course_entry_index >= (int)pCourse->m_vEntries.size()) {
      continue;
    }

    const TrailEntry* trail_entry = &trail->m_vEntries[course_entry_index];
    const CourseEntry* course_entry = &pCourse->m_vEntries[course_entry_index];
    if (trail_entry == nullptr) {
      continue;
    }

    RString meter;
    Difficulty difficulty;
    if (trail_entry->bSecret) {
      if (course_entry == nullptr) {
        continue;
      }

      int low = course_entry->steps_criteria_.m_iLowMeter;
      int how = course_entry->steps_criteria_.m_iHighMeter;

      bool low_is_set = low != -1;
      bool high_is_set = how != -1;

      if (!low_is_set && !high_is_set) {
        meter = "?";
      }
      if (!low_is_set && high_is_set) {
        meter = ssprintf(">=%d", how);
      } else if (low_is_set && !high_is_set) {
        meter = ssprintf("<=%d", low);
      } else if (low_is_set && high_is_set) {
        if (low == how) {
          meter = ssprintf("%d", low);
        } else {
          meter = ssprintf("%d-%d", low, how);
        }
      }

      difficulty = trail_entry->dc;
      if (difficulty == Difficulty_Invalid) {
        difficulty = Difficulty_Edit;
      }
    } else {
      meter = ssprintf("%d", trail_entry->pSteps->GetMeter());
      difficulty = trail_entry->pSteps->GetDifficulty();
    }

    Message msg("SetSong");
    msg.SetParam("PlayerNumber", pn);
    msg.SetParam("Song", trail_entry->pSong);
    msg.SetParam("Steps", trail_entry->pSteps);
    msg.SetParam("Difficulty", difficulty);
    msg.SetParam("Meter", meter);
    msg.SetParam("Number", course_entry_index + 1);
    msg.SetParam("Modifiers", trail_entry->Modifiers);
    msg.SetParam("Secret", trail_entry->bSecret);
    actor->HandleMessage(msg);
  }
}

// lua start
#include "LuaBinding.h"

// Allow Lua to have access to the CourseContentsList.
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
