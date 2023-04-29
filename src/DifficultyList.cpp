#include "global.h"

#include "DifficultyList.h"

#include <cstddef>
#include <vector>

#include "CommonMetrics.h"
#include "GameState.h"
#include "Song.h"
#include "SongUtil.h"
#include "Steps.h"
#include "StepsDisplay.h"
#include "StepsUtil.h"
#include "Style.h"
#include "XmlFile.h"

// Specifies the max number of charts available for a song.
// This includes autogenned charts.
inline constexpr int MAX_METERS =
    (Enum::to_integral(NUM_Difficulty) * Enum::to_integral(NUM_StepsType)) +
    MAX_EDITS_PER_SONG;

REGISTER_ACTOR_CLASS(StepsDisplayList);

StepsDisplayList::StepsDisplayList() {
  is_shown_ = true;

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

  if (m_sName.empty()) {
    LuaHelpers::ReportScriptError("StepsDisplayList must have a Name");
    return;
  }

  ITEMS_SPACING_Y.Load(m_sName, "ItemsSpacingY");
  NUM_SHOWN_ITEMS.Load(m_sName, "NumShownItems");
  CAPITALIZE_DIFFICULTY_NAMES.Load(m_sName, "CapitalizeDifficultyNames");
  MOVE_COMMAND.Load(m_sName, "MoveCommand");

  lines_.resize(MAX_METERS);
  cur_song_ = nullptr;

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
    lines_[m].meter.SetName("Row");
    lines_[m].meter.Load("StepsDisplayListRow", nullptr);
    this->AddChild(&lines_[m].meter);
  }

  UpdatePositions();
  PositionItems();
}

int StepsDisplayList::GetCurrentRowIndex(PlayerNumber pn) const {
  Difficulty ClosestDifficulty = GAMESTATE->GetClosestShownDifficulty(pn);

  for (unsigned i = 0; i < rows_.size(); ++i) {
    const Row& row = rows_[i];

    if (GAMESTATE->m_pCurSteps[pn] == nullptr) {
      if (row.difficulty == ClosestDifficulty) {
        return i;
      }
    } else {
      if (GAMESTATE->m_pCurSteps[pn].Get() == row.steps) {
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
  const int half_size = total / 2;

  int first_start, first_end, second_start, second_end;

  // Choices for each player. If only one player is active, it's the same for
  // both.
  int P1Choice = GAMESTATE->IsHumanPlayer(PLAYER_1)   ? current_row[PLAYER_1]
                 : GAMESTATE->IsHumanPlayer(PLAYER_2) ? current_row[PLAYER_2]
                                                      : 0;
  int P2Choice = GAMESTATE->IsHumanPlayer(PLAYER_2)   ? current_row[PLAYER_2]
                 : GAMESTATE->IsHumanPlayer(PLAYER_1) ? current_row[PLAYER_1]
                                                      : 0;

  std::vector<Row>& rows = rows_;

  const bool both_players_activated =
      GAMESTATE->IsHumanPlayer(PLAYER_1) && GAMESTATE->IsHumanPlayer(PLAYER_2);
  if (!both_players_activated) {
    // Simply center the cursor.
    first_start = std::max(P1Choice - half_size, 0);
    first_end = first_start + total;
    second_start = second_end = first_end;
  } else {
    // First half:
    const int earliest = std::min(P1Choice, P2Choice);
    first_start = std::max(earliest - half_size / 2, 0);
    first_end = first_start + half_size;

    // Second half:
    const int latest = std::max(P1Choice, P2Choice);

    second_start = std::max(latest - half_size / 2, 0);

    // Don't overlap.
    second_start = std::max(second_start, first_end);

    second_end = second_start + half_size;
  }

  first_end = std::min(first_end, (int)rows.size());
  second_end = std::min(second_end, (int)rows.size());

  // If less than total (and rows.size()) are displayed, fill in the empty
  // space intelligently.
  for (;;) {
    const int sum = (first_end - first_start) + (second_end - second_start);
    if (sum >= (int)rows.size() || sum >= total) {
      // Nothing more to display, or no room.
      break;
    }

    // First priority: expand the top of the second half until it meets
    // the first half.
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
  for (int i = 0; i < (int)rows.size(); ++i) {
    float ItemPosition;
    if (i < first_start) {
      ItemPosition = -0.5f;
    } else if (i < first_end) {
      ItemPosition = (float)pos++;
    } else if (i < second_start) {
      ItemPosition = half_size - 0.5f;
    } else if (i < second_end) {
      ItemPosition = (float)pos++;
    } else {
      ItemPosition = (float)total - 0.5f;
    }

    Row& row = rows[i];

    float y = ITEMS_SPACING_Y * ItemPosition;
    row.y = y;
    row.is_hidden = i < first_start || (i >= first_end && i < second_start) ||
                    i >= second_end;
  }
}

void StepsDisplayList::PositionItems() {
  for (std::size_t i = 0; i < MAX_METERS; ++i) {
    bool bUnused = (i >= rows_.size());
    lines_[i].meter.SetVisible(!bUnused);
  }

  for (std::size_t m = 0; m < rows_.size(); ++m) {
    Row& row = rows_[m];
    bool is_hidden = row.is_hidden;
    if (!is_shown_) {
      is_hidden = true;
    }

    const float fDiffuseAlpha = is_hidden ? 0.0f : 1.0f;
    if (lines_[m].meter.GetDestY() != row.y ||
        lines_[m].meter.DestTweenState().diffuse[0][3] != fDiffuseAlpha) {
      lines_[m].meter.RunCommands(MOVE_COMMAND.GetValue());
      lines_[m].meter.RunCommandsOnChildren(MOVE_COMMAND.GetValue());
    }

    lines_[m].meter.SetY(row.y);
  }

  for (std::size_t m = 0; m < MAX_METERS; ++m) {
    bool is_hidden = true;
    if (is_shown_ && m < rows_.size()) {
      is_hidden = rows_[m].is_hidden;
    }

    float diffuse_alpha = is_hidden ? 0.0f : 1.0f;

    lines_[m].meter.SetDiffuseAlpha(diffuse_alpha);
  }

  FOREACH_HumanPlayer(pn) {
    int current_row = GetCurrentRowIndex(pn);

    float y = 0;
    if (current_row < (int)rows_.size()) {
      y = rows_[current_row].y;
    }

    cursor_frames_[pn].PlayCommand("Change");
    cursor_frames_[pn].SetY(y);
  }
}

void StepsDisplayList::SetFromGameState() {
  const Song* song = GAMESTATE->m_pCurSong;
  unsigned i = 0;

  if (song == nullptr) {
    // FIXME: This clamps to between the min and the max difficulty, but
    // it really should round to the nearest difficulty that's in
    // DIFFICULTIES_TO_SHOW.
    const std::vector<Difficulty>& difficulties =
        CommonMetrics::DIFFICULTIES_TO_SHOW.GetValue();
    rows_.resize(difficulties.size());
    for (const Difficulty& d : difficulties) {
      rows_[i].difficulty = d;
      lines_[i].meter.SetFromStepsTypeAndMeterAndDifficultyAndCourseType(
          GAMESTATE->GetCurrentStyle(PLAYER_INVALID)->m_StepsType, 0, d,
          CourseType_Invalid);
      ++i;
    }
  } else {
    std::vector<Steps*> vpSteps;
    SongUtil::GetPlayableSteps(song, vpSteps);
    // Should match the sort in ScreenSelectMusic::AfterMusicChange.

    rows_.resize(vpSteps.size());
    for (const Steps* s : vpSteps) {
      // LOG->Trace(ssprintf("setting steps for row %i",i));
      rows_[i].steps = s;
      lines_[i].meter.SetFromSteps(s);
      ++i;
    }
  }

  while (i < MAX_METERS) {
    lines_[i++].meter.Unset();
  }

  UpdatePositions();
  PositionItems();

  for (std::size_t m = 0; m < MAX_METERS; ++m) {
    lines_[m].meter.FinishTweening();
  }
}

void StepsDisplayList::HideRows() {
  for (unsigned m = 0; m < rows_.size(); ++m) {
    Line& line = lines_[m];

    line.meter.FinishTweening();
    line.meter.SetDiffuseAlpha(0);
  }
}

void StepsDisplayList::TweenOnScreen() {
  FOREACH_HumanPlayer(pn) ON_COMMAND(cursors_[pn]);

  for (std::size_t m = 0; m < MAX_METERS; ++m) {
    ON_COMMAND(lines_[m].meter);
  }

  this->SetHibernate(0.5f);
  is_shown_ = true;
  for (unsigned m = 0; m < rows_.size(); ++m) {
    Line& line = lines_[m];

    line.meter.FinishTweening();
  }

  HideRows();
  PositionItems();

  FOREACH_HumanPlayer(pn) COMMAND(cursors_[pn], "TweenOn");
}

void StepsDisplayList::TweenOffScreen() {}

void StepsDisplayList::Show() {
  is_shown_ = true;

  SetFromGameState();

  HideRows();
  PositionItems();

  FOREACH_HumanPlayer(pn) COMMAND(cursors_[pn], "Show");
}

void StepsDisplayList::Hide() {
  is_shown_ = false;
  PositionItems();

  FOREACH_HumanPlayer(pn) COMMAND(cursors_[pn], "Hide");
}

void StepsDisplayList::HandleMessage(const Message& msg) {
  FOREACH_ENUM(PlayerNumber, pn) {
    if (msg.GetName() ==
            MessageIDToString((
                MessageID)(Message_CurrentStepsP1Changed + Enum::to_integral(pn))) ||
        msg.GetName() ==
            MessageIDToString((
                MessageID)(Message_CurrentTrailP1Changed + Enum::to_integral(pn)))) {
      SetFromGameState();
    }
  }

  ActorFrame::HandleMessage(msg);
}

// lua start
#include "LuaBinding.h"

// Allow Lua to have access to the StepsDisplayList.
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
