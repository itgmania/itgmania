#include "DifficultyList.h"

#include <algorithm>
#include <cstddef>
#include <vector>

#include "ActorFrame.h"
#include "ActorUtil.h"
#include "CommonMetrics.h"
#include "Difficulty.h"
#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "GameState.h"
#include "LuaManager.h"
#include "MessageManager.h"
#include "PlayerNumber.h"
#include "RageUtil.h"
#include "Song.h"
#include "SongUtil.h"
#include "Steps.h"
#include "StepsDisplay.h"
#include "StepsUtil.h"
#include "Style.h"
#include "XmlFile.h"
#include "global.h"

/** @brief Specifies the max number of charts available for a song.
 *
 * This includes autogenned charts. */
inline constexpr int MAX_METERS =
    (Enum::to_integral(NUM_Difficulty) * Enum::to_integral(NUM_StepsType)) +
    MAX_EDITS_PER_SONG;

REGISTER_ACTOR_CLASS(StepsDisplayList);

StepsDisplayList::StepsDisplayList() {
  shown_ = true;

  FOREACH_ENUM(PlayerNumber, pn) {
    SubscribeToMessage(
        (MessageID)(Message_CurrentStepsP1Changed + Enum::to_integral(pn)));
    SubscribeToMessage(
        (MessageID)(Message_CurrentTrailP1Changed + Enum::to_integral(pn)));
  }
}

StepsDisplayList::~StepsDisplayList() {}

void StepsDisplayList::LoadFromNode(const XNode* node) {
  ActorFrame::LoadFromNode(node);

  if (name_.empty()) {
    LuaHelpers::ReportScriptError("StepsDisplayList must have a Name");
    return;
  }

  ITEMS_SPACING_Y.Load(name_, "ItemsSpacingY");
  NUM_SHOWN_ITEMS.Load(name_, "NumShownItems");
  MOVE_COMMAND.Load(name_, "MoveCommand");

  lines_.resize(MAX_METERS);

  FOREACH_ENUM(PlayerNumber, pn) {
    const XNode* child = node->GetChild(ssprintf("CursorP%i", pn + 1));
    if (child == nullptr) {
      LuaHelpers::ReportScriptErrorFmt(
          "%s: StepsDisplayList: missing the node \"CursorP%d\"",
          ActorUtil::GetWhere(node).c_str(), pn + 1);
    } else {
      cursors_[pn].LoadActorFromNode(child, this);
    }

    /* Hack: we need to tween cursors both up to down (cursor motion) and
     * visible to invisible (fading).  Cursor motion needs to stoptweening, so
     * multiple motions don't queue and look unresponsive.  However, that
     * stoptweening interrupts fading, resulting in the cursor remaining
     * invisible or partially invisible.  So, do them in separate tweening
     * stacks.  This means the Cursor command can't change diffuse colors; I
     * think we do need a diffuse color stack ... */
    child = node->GetChild(ssprintf("CursorP%iFrame", pn + 1));
    if (child == nullptr) {
      LuaHelpers::ReportScriptErrorFmt(
          "%s: StepsDisplayList: missing the node \"CursorP%dFrame\"",
          ActorUtil::GetWhere(node).c_str(), pn + 1);
    } else {
      cursor_frames_[pn].LoadFromNode(child);
      cursor_frames_[pn].AddChild(cursors_[pn]);
      this->AddChild(&cursor_frames_[pn]);
    }
  }

  for (unsigned m = 0; m < lines_.size(); ++m) {
    // todo: Use Row1, Row2 for names? also m_sName+"Row" -aj
    lines_[m].meter_.SetName("Row");
    lines_[m].meter_.Load("StepsDisplayListRow", nullptr);
    this->AddChild(&lines_[m].meter_);
  }

  UpdatePositions();
  PositionItems();
}

int StepsDisplayList::GetCurrentRowIndex(PlayerNumber pn) const {
  Difficulty closest_difficulty = GAMESTATE->GetClosestShownDifficulty(pn);

  for (unsigned i = 0; i < rows_.size(); i++) {
    const Row& row = rows_[i];

    if (GAMESTATE->cur_steps_[pn] == nullptr) {
      if (row.dc_ == closest_difficulty) {
        return i;
      }
    } else {
      if (GAMESTATE->cur_steps_[pn].Get() == row.steps_) {
        return i;
      }
    }
  }

  return 0;
}

// Update m_fY and m_bHidden[].
void StepsDisplayList::UpdatePositions() {
  int current_row[NUM_PLAYERS];
  FOREACH_HumanPlayer(p) current_row[p] = GetCurrentRowIndex(p);

  const int total = NUM_SHOWN_ITEMS;
  const int halfsize = total / 2;

  int first_start, first_end, second_start, second_end;

  // Choices for each player. If only one player is active, it's the same for
  // both.
  int p1_choice = GAMESTATE->IsHumanPlayer(PLAYER_1)   ? current_row[PLAYER_1]
                  : GAMESTATE->IsHumanPlayer(PLAYER_2) ? current_row[PLAYER_2]
                                                       : 0;
  int p2_choice = GAMESTATE->IsHumanPlayer(PLAYER_2)   ? current_row[PLAYER_2]
                  : GAMESTATE->IsHumanPlayer(PLAYER_1) ? current_row[PLAYER_1]
                                                       : 0;

  std::vector<Row>& rows = rows_;

  const bool both_players_activated =
      GAMESTATE->IsHumanPlayer(PLAYER_1) && GAMESTATE->IsHumanPlayer(PLAYER_2);
  if (!both_players_activated) {
    // Simply center the cursor.
    first_start = std::max(p1_choice - halfsize, 0);
    first_end = first_start + total;
    second_start = second_end = first_end;
  } else {
    // First half:
    const int earliest = std::min(p1_choice, p2_choice);
    first_start = std::max(earliest - halfsize / 2, 0);
    first_end = first_start + halfsize;

    // Second half:
    const int latest = std::max(p1_choice, p2_choice);

    second_start = std::max(latest - halfsize / 2, 0);

    // Don't overlap.
    second_start = std::max(second_start, first_end);

    second_end = second_start + halfsize;
  }

  first_end = std::min(first_end, (int)rows.size());
  second_end = std::min(second_end, (int)rows.size());

  /* If less than total (and Rows.size()) are displayed, fill in the empty
   * space intelligently. */
  for (;;) {
    const int sum = (first_end - first_start) + (second_end - second_start);
    if (sum >= (int)rows.size() || sum >= total) {
      break;  // nothing more to display, or no room
    }

    /* First priority: expand the top of the second half until it meets
     * the first half. */
    if (second_start > first_end) {
      second_start--;
    }
    // Otherwise, expand either end.
    else if (first_start > 0) {
      first_start--;
    } else if (second_end < (int)rows.size()) {
      second_end++;
    } else {
      FAIL_M("Do we have room to grow, or don't we?");
    }
  }

  int pos = 0;
  for (int i = 0; i < (int)rows.size(); i++)  // foreach row
  {
    float item_position;
    if (i < first_start) {
      item_position = -0.5f;
    } else if (i < first_end) {
      item_position = (float)pos++;
    } else if (i < second_start) {
      item_position = halfsize - 0.5f;
    } else if (i < second_end) {
      item_position = (float)pos++;
    } else {
      item_position = (float)total - 0.5f;
    }

    Row& row = rows[i];

    float y = ITEMS_SPACING_Y * item_position;
    row.y_ = y;
    row.hidden_ = i < first_start || (i >= first_end && i < second_start) ||
                  i >= second_end;
  }
}

void StepsDisplayList::PositionItems() {
  for (size_t i = 0; i < MAX_METERS; ++i) {
    bool unused = (i >= rows_.size());
    lines_[i].meter_.SetVisible(!unused);
  }

  for (size_t m = 0; m < rows_.size(); ++m) {
    Row& row = rows_[m];
    bool hidden = row.hidden_;
    if (!shown_) {
      hidden = true;
    }

    const float diffuse_alpha = hidden ? 0.0f : 1.0f;
    if (lines_[m].meter_.GetDestY() != row.y_ ||
        lines_[m].meter_.DestTweenState().diffuse_[0][3] != diffuse_alpha) {
      lines_[m].meter_.RunCommands(MOVE_COMMAND.GetValue());
      lines_[m].meter_.RunCommandsOnChildren(MOVE_COMMAND.GetValue());
    }

    lines_[m].meter_.SetY(row.y_);
  }

  for (size_t m = 0; m < MAX_METERS; ++m) {
    bool hidden = true;
    if (shown_ && m < rows_.size()) {
      hidden = rows_[m].hidden_;
    }

    float diffuse_alpha = hidden ? 0.0f : 1.0f;

    lines_[m].meter_.SetDiffuseAlpha(diffuse_alpha);
  }

  FOREACH_HumanPlayer(pn) {
    int current_row = GetCurrentRowIndex(pn);

    float y = 0;
    if (current_row < (int)rows_.size()) {
      y = rows_[current_row].y_;
    }

    cursor_frames_[pn].PlayCommand("Change");
    cursor_frames_[pn].SetY(y);
  }
}

void StepsDisplayList::SetFromGameState() {
  const Song* song = GAMESTATE->cur_song_;
  unsigned i = 0;

  if (song == nullptr) {
    // FIXME: This clamps to between the min and the max difficulty, but
    // it really should round to the nearest difficulty that's in
    // DIFFICULTIES_TO_SHOW.
    const std::vector<Difficulty>& difficulties =
        CommonMetrics::DIFFICULTIES_TO_SHOW.GetValue();
    rows_.resize(difficulties.size());
    for (const Difficulty& d : difficulties) {
      rows_[i].dc_ = d;
      lines_[i].meter_.SetFromStepsTypeAndMeterAndDifficultyAndCourseType(
          GAMESTATE->GetCurrentStyle(PLAYER_INVALID)->steps_type_, 0, d,
          CourseType_Invalid);
      ++i;
    }
  } else {
    std::vector<Steps*> steps;
    SongUtil::GetPlayableSteps(song, steps);
    // Should match the sort in ScreenSelectMusic::AfterMusicChange.

    rows_.resize(steps.size());
    for (const Steps* s : steps) {
      // LOG->Trace(ssprintf("setting steps for row %i",i));
      rows_[i].steps_ = s;
      lines_[i].meter_.SetFromSteps(s);
      ++i;
    }
  }

  while (i < MAX_METERS) {
    lines_[i++].meter_.Unset();
  }

  UpdatePositions();
  PositionItems();

  for (size_t m = 0; m < MAX_METERS; ++m) {
    lines_[m].meter_.FinishTweening();
  }
}

void StepsDisplayList::HandleMessage(const Message& msg) {
  FOREACH_ENUM(PlayerNumber, pn) {
    if (msg.GetName() ==
            MessageIDToString((MessageID)(Message_CurrentStepsP1Changed +
                                          Enum::to_integral(pn))) ||
        msg.GetName() ==
            MessageIDToString((MessageID)(Message_CurrentTrailP1Changed +
                                          Enum::to_integral(pn)))) {
      SetFromGameState();
    }
  }

  ActorFrame::HandleMessage(msg);
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the StepsDisplayList. */
class LunaStepsDisplayList : public Luna<StepsDisplayList> {
 public:
  static int setfromgamestate(T* p, lua_State* L) {
    p->SetFromGameState();
    COMMON_RETURN_SELF;
  }

  LunaStepsDisplayList() { ADD_METHOD(setfromgamestate); }
};

LUA_REGISTER_DERIVED_CLASS(StepsDisplayList, ActorFrame)
// lua end

/*
 * (c) 2003-2004 Glenn Maynard
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
