#include "ScreenOptionsEditCourse.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "CourseUtil.h"
#include "Difficulty.h"
#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "GameState.h"
#include "InputEventPlus.h"
#include "LocalizedString.h"
#include "OptionRowHandler.h"
#include "PlayerNumber.h"
#include "RageUtil.h"
#include "Screen.h"
#include "ScreenMessage.h"
#include "ScreenMiniMenu.h"
#include "ScreenOptions.h"
#include "ScreenPrompt.h"
#include "Song.h"
#include "SongManager.h"
#include "SongUtil.h"
#include "Steps.h"
#include "StepsUtil.h"
#include "Style.h"
#include "global.h"

static void GetStepsForSong(Song* song, std::vector<Steps*>& steps_out) {
  SongUtil::GetSteps(
      song, steps_out,
      GAMESTATE->GetCurrentStyle(GAMESTATE->GetMasterPlayerNumber())
          ->steps_type_);
  // xxx: If the StepsType isn't valid for the current game, this will cause
  // a crash when changing songs. -aj
  StepsUtil::RemoveLockedSteps(song, steps_out);
  StepsUtil::SortNotesArrayByDifficulty(steps_out);
}

// XXX: very similar to OptionRowHandlerSteps
class EditCourseOptionRowHandlerSteps : public OptionRowHandler {
 public:
  void Load(int entry_index) { entry_index_ = entry_index; }
  virtual ReloadChanged Reload() {
    def_.choices_.clear();
    steps_.clear();

    Song* song = GAMESTATE->cur_song_;
    if (song)  // playing a song
    {
      GetStepsForSong(song, steps_);
      for (const Steps* steps : steps_) {
        std::string s;
        if (steps->GetDifficulty() == Difficulty_Edit) {
          s = steps->GetDescription();
        } else {
          s = CustomDifficultyToLocalizedString(StepsToCustomDifficulty(steps));
        }
        s += ssprintf(" %d", steps->GetMeter());
        def_.choices_.push_back(s);
      }
      def_.enabled_for_players_.clear();
      def_.enabled_for_players_.insert(PLAYER_1);
    } else {
      def_.choices_.push_back("n/a");
      steps_.push_back(nullptr);
      def_.enabled_for_players_.clear();
    }

    return RELOAD_CHANGED_ALL;
  }
  virtual void ImportOption(
      OptionRow* row, const std::vector<PlayerNumber>& vpns,
      std::vector<bool> selected_out[NUM_PLAYERS]) const {
    Trail* trail = GAMESTATE->cur_trail_[PLAYER_1];
    Steps* steps;
    if (trail) {
      if (entry_index_ < (int)trail->entries_.size()) {
        steps = trail->entries_[entry_index_].steps_;
      }
    }

    std::vector<Steps*>::const_iterator iter =
        find(steps_.begin(), steps_.end(), steps);
    if (iter == steps_.end()) {
      row->SetOneSharedSelection(0);
    } else {
      int index = iter - steps_.begin();
      row->SetOneSharedSelection(index);
    }
  }
  virtual int ExportOption(
      const std::vector<PlayerNumber>& vpns,
      const std::vector<bool> selected[NUM_PLAYERS]) const {
    return 0;
  }
  Steps* GetSteps(int steps_index) const { return steps_[steps_index]; }

 protected:
  int entry_index_;
  std::vector<Steps*> steps_;
};

const int NUM_SONG_ROWS = 20;

REGISTER_SCREEN_CLASS(ScreenOptionsEditCourse);

enum EditCourseRow { EditCourseRow_Minutes, NUM_EditCourseRow };

enum RowType {
  RowType_Song,
  RowType_Steps,
  NUM_RowType,
  RowType_Invalid,
};
static int RowToEntryIndex(int row) {
  if (row < NUM_EditCourseRow) {
    return -1;
  }

  return (row - NUM_EditCourseRow) / NUM_RowType;
}
static RowType RowToRowType(int row) {
  if (row < NUM_EditCourseRow) {
    return RowType_Invalid;
  }
  return (RowType)((row - NUM_EditCourseRow) % NUM_RowType);
}
static int EntryIndexAndRowTypeToRow(int entry_index, RowType row_type) {
  return NUM_EditCourseRow + entry_index * NUM_RowType + row_type;
}

void ScreenOptionsEditCourse::Init() {
  ScreenOptions::Init();

  SongCriteria sc;
  sc.selectable_ = SongCriteria::Selectable_Yes;
  sc.tutorial_ = SongCriteria::Tutorial_No;
  sc.locked_ = SongCriteria::Locked_Unlocked;

  SongUtil::FilterSongs(sc, SONGMAN->GetAllSongs(), songs_, true);

  SongUtil::SortSongPointerArrayByTitle(songs_);
}

const MenuRowDef g_MenuRows[] = {
    MenuRowDef(
        -1, "Max Minutes", true, EditMode_Practice, true, false, 0, nullptr),
};

static LocalizedString EMPTY("ScreenOptionsEditCourse", "-Empty-");
static LocalizedString SONG("ScreenOptionsEditCourse", "Song");
static LocalizedString STEPS("ScreenOptionsEditCourse", "Steps");
static LocalizedString MINUTES("ScreenOptionsEditCourse", "minutes");

static std::string MakeMinutesString(int mins) {
  if (mins == 0) {
    return "No Cut-off";
  }
  return ssprintf("%d", mins) + " " + MINUTES.GetValue();
}

void ScreenOptionsEditCourse::BeginScreen() {
  std::vector<OptionRowHandler*> hands;

  FOREACH_ENUM(EditCourseRow, row_index) {
    const MenuRowDef& mr = g_MenuRows[row_index];
    OptionRowHandler* hand = OptionRowHandlerUtil::MakeSimple(mr);

    hand->def_.layout_type_ = LAYOUT_SHOW_ONE_IN_ROW;
    hand->def_.choices_.clear();

    switch (row_index) {
      DEFAULT_FAIL(row_index);
      case EditCourseRow_Minutes:
        hand->def_.choices_.push_back(MakeMinutesString(0));
        for (int i = EditCourseUtil::MIN_WORKOUT_MINUTES; i <= 20; i += 2) {
          hand->def_.choices_.push_back(MakeMinutesString(i));
        }
        for (int i = 20; i <= EditCourseUtil::MAX_WORKOUT_MINUTES; i += 5) {
          hand->def_.choices_.push_back(MakeMinutesString(i));
        }
        break;
    }

    hand->def_.export_on_change_ = true;
    hands.push_back(hand);
  }

  for (int i = 0; i < NUM_SONG_ROWS; i++) {
    {
      MenuRowDef mrd = MenuRowDef(
          -1, "---", true, EditMode_Practice, true, false, 0,
          EMPTY.GetValue().c_str());
      for (const Song* s : songs_) {
        mrd.choices_.push_back(s->GetDisplayFullTitle());
      }
      mrd.name_ = ssprintf((SONG.GetValue() + " %d").c_str(), i + 1);
      OptionRowHandler* hand = OptionRowHandlerUtil::MakeSimple(mrd);
      hand->def_.allow_theme_title_ = false;  // already themed
      hand->def_.explanation_name_ = "Choose Song";
      hand->def_.layout_type_ = LAYOUT_SHOW_ONE_IN_ROW;
      hand->def_.export_on_change_ = true;
      hands.push_back(hand);
    }

    {
      EditCourseOptionRowHandlerSteps* hand =
          new EditCourseOptionRowHandlerSteps;
      hand->Load(i);
      hand->def_.choices_.push_back("n/a");
      hand->def_.name_ = ssprintf((STEPS.GetValue() + " %d").c_str(), i + 1);
      hand->def_.allow_theme_title_ = false;  // already themed
      hand->def_.allow_theme_items_ = false;  // already themed
      hand->def_.explanation_name_ = "Choose Steps";
      hand->def_.one_choice_for_all_players_ = true;
      hand->def_.layout_type_ = LAYOUT_SHOW_ONE_IN_ROW;
      hand->def_.export_on_change_ = true;
      hands.push_back(hand);
    }
  }

  ScreenOptions::InitMenu(hands);

  ScreenOptions::BeginScreen();

  for (int i = 0; i < (int)rows_.size(); i++) {
    OptionRow* row = rows_[i];
    current_row_[PLAYER_1] = i;
    this->SetCurrentSong();
    row->Reload();
  }
  current_row_[PLAYER_1] = 0;

  this->SetCurrentSong();

  // this->AfterChangeRow( PLAYER_1 );
}

ScreenOptionsEditCourse::~ScreenOptionsEditCourse() {}

void ScreenOptionsEditCourse::ImportOptions(
    int i_row, const std::vector<PlayerNumber>& vpns) {
  OptionRow& row = *rows_[i_row];
  if (row.GetRowType() == OptionRow::RowType_Exit) {
    return;
  }

  switch (i_row) {
    case EditCourseRow_Minutes:
      row.SetOneSharedSelection(0);
      row.SetOneSharedSelectionIfPresent(MakeMinutesString(
          static_cast<int>(GAMESTATE->cur_course_->goal_seconds_) / 60));
      break;
    default: {
      int entry_index = RowToEntryIndex(i_row);
      RowType row_type = RowToRowType(i_row);

      switch (row_type) {
        DEFAULT_FAIL(row_type);
        case RowType_Song: {
          Song* song = nullptr;
          if (entry_index < (int)GAMESTATE->cur_course_->entries_.size()) {
            song =
                GAMESTATE->cur_course_->entries_[entry_index].song_id_.ToSong();
          }

          std::vector<Song*>::iterator iter =
              find(songs_.begin(), songs_.end(), song);
          if (iter == songs_.end()) {
            row.SetOneSharedSelection(0);
          } else {
            row.SetOneSharedSelection(1 + iter - songs_.begin());
          }
        } break;
        case RowType_Steps:
          // the OptionRowHandler does its own importing
          break;
      }
    } break;
  }
}

void ScreenOptionsEditCourse::ExportOptions(
    int i_row, const std::vector<PlayerNumber>& vpns) {
  FOREACH_ENUM(EditCourseRow, i) {
    OptionRow& row = *rows_[i];
    int index = row.GetOneSharedSelection(true);
    std::string value;
    if (index >= 0) {
      value = row.GetRowDef().choices_[index];
    }

    switch (i) {
      DEFAULT_FAIL(i);
      case EditCourseRow_Minutes:
        GAMESTATE->cur_course_->goal_seconds_ = 0;
        int mins;
        if (sscanf(value.c_str(), "%d", &mins) == 1) {
          GAMESTATE->cur_course_->goal_seconds_ = float(mins * 60);
        }
        break;
    }
  }

  GAMESTATE->cur_course_->entries_.clear();

  for (int i = NUM_EditCourseRow; i < (int)rows_.size(); i++) {
    OptionRow& row = *rows_[i];
    if (row.GetRowType() == OptionRow::RowType_Exit) {
      continue;
    }

    RowType row_type = RowToRowType(i);
    int entry_index = RowToEntryIndex(i);

    switch (row_type) {
      case RowType_Song: {
        Song* song = this->GetSongForEntry(entry_index);
        if (song) {
          Steps* steps = this->GetStepsForEntry(entry_index);
          ASSERT_M(steps != nullptr, "No Steps for this Song!");
          CourseEntry ce;
          ce.song_id_.FromSong(song);
          ce.steps_criteria_.difficulty_ = steps->GetDifficulty();
          GAMESTATE->cur_course_->entries_.push_back(ce);
        }
      } break;
      case RowType_Steps:
        // push each CourseEntry when we handle each RowType_Song above
        break;
      default:
        break;
    }
  }

  EditCourseUtil::UpdateAndSetTrail();
}

void ScreenOptionsEditCourse::HandleScreenMessage(const ScreenMessage sm) {
  if (sm == SM_ExportOptions) {
    // g_Workout.m_vpSongs.clear();
  }

  ScreenOptions::HandleScreenMessage(sm);
}

void ScreenOptionsEditCourse::SetCurrentSong() {
  int i_row = current_row_[PLAYER_1];
  OptionRow& row = *rows_[i_row];

  if (row.GetRowType() == OptionRow::RowType_Exit) {
    GAMESTATE->cur_song_.Set(nullptr);
    GAMESTATE->cur_steps_[PLAYER_1].Set(nullptr);
  } else {
    i_row = current_row_[PLAYER_1];
    int entry_index = RowToEntryIndex(i_row);
    Song* song = nullptr;
    if (entry_index != -1) {
      int current_song_row =
          EntryIndexAndRowTypeToRow(entry_index, RowType_Song);
      OptionRow& o_row = *rows_[current_song_row];
      int index = o_row.GetOneSelection(PLAYER_1);
      if (index != 0) {
        song = songs_[index - 1];
      }
    }
    if (song != nullptr) {
      GAMESTATE->cur_song_.Set(song);
    }
  }
}

void ScreenOptionsEditCourse::SetCurrentSteps() {
  Song* song = GAMESTATE->cur_song_;
  if (song) {
    int i_row = current_row_[PLAYER_1];
    int entry_index = RowToEntryIndex(i_row);
    OptionRow& row =
        *rows_[EntryIndexAndRowTypeToRow(entry_index, RowType_Steps)];
    int steps_index = row.GetOneSharedSelection();
    const EditCourseOptionRowHandlerSteps* hand =
        dynamic_cast<const EditCourseOptionRowHandlerSteps*>(row.GetHandler());
    ASSERT(hand != nullptr);
    Steps* steps = hand->GetSteps(steps_index);
    GAMESTATE->cur_steps_[PLAYER_1].Set(steps);
  } else {
    GAMESTATE->cur_steps_[PLAYER_1].Set(nullptr);
  }
}

Song* ScreenOptionsEditCourse::GetSongForEntry(int entry_index) {
  int i_row = EntryIndexAndRowTypeToRow(entry_index, RowType_Song);
  OptionRow& row = *rows_[i_row];

  int index = row.GetOneSharedSelection();
  if (index == 0) {
    return nullptr;
  }
  return songs_[index - 1];
}

Steps* ScreenOptionsEditCourse::GetStepsForEntry(int entry_index) {
  int i_row = EntryIndexAndRowTypeToRow(entry_index, RowType_Steps);
  OptionRow& row = *rows_[i_row];
  int index = row.GetOneSharedSelection();
  Song* song = GetSongForEntry(entry_index);
  std::vector<Steps*> steps;
  GetStepsForSong(song, steps);
  return steps[index];
}

void ScreenOptionsEditCourse::AfterChangeRow(PlayerNumber pn) {
  ScreenOptions::AfterChangeRow(pn);

  const int cur_row = current_row_[pn];
  // only do this if it's not the first row. -aj
  if (cur_row > 0) {
    SetCurrentSong();
    SetCurrentSteps();
  }
}

void ScreenOptionsEditCourse::AfterChangeValueInRow(
    int i_row, PlayerNumber pn) {
  ScreenOptions::AfterChangeValueInRow(i_row, pn);

  int entry_index = RowToEntryIndex(i_row);
  RowType row_type = RowToRowType(i_row);
  switch (row_type) {
    case RowType_Song: {
      SetCurrentSong();
      OptionRow& row =
          *rows_[EntryIndexAndRowTypeToRow(entry_index, RowType_Steps)];
      row.Reload();
    } break;
    case RowType_Steps:
      SetCurrentSteps();
      break;
    default:
      break;
  }
}

const int MIN_ENABLED_SONGS = 2;

static LocalizedString MUST_ENABLE_AT_LEAST(
    "ScreenOptionsEditCourse", "You must enable at least %d songs.");
void ScreenOptionsEditCourse::ProcessMenuStart(const InputEventPlus& input) {
  if (IsTransitioning()) {
    return;
  }

  int row = current_row_[GAMESTATE->GetMasterPlayerNumber()];

  unsigned song_count = GAMESTATE->cur_course_->entries_.size();

  if (rows_[row]->GetRowType() == OptionRow::RowType_Exit &&
      song_count < unsigned(MIN_ENABLED_SONGS)) {
    ScreenPrompt::Prompt(
        SM_None,
        ssprintf(MUST_ENABLE_AT_LEAST.GetValue().c_str(), MIN_ENABLED_SONGS));
    return;
  }

  ScreenOptions::ProcessMenuStart(input);
}

/*
 * (c) 2003-2004 Chris Danford
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
