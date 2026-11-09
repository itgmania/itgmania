#include "EditMenu.h"

#include <cstddef>
#include <string>
#include <vector>

#include "ActorUtil.h"
#include "CommonMetrics.h"
#include "Difficulty.h"
#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "GameManager.h"
#include "GameState.h"
#include "ImageCache.h"
#include "LocalizedString.h"
#include "PlayerNumber.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "Song.h"
#include "SongManager.h"
#include "SongUtil.h"
#include "Steps.h"
#include "StepsUtil.h"
#include "ThemeManager.h"
#include "UnlockManager.h"
#include "global.h"

static const char* EditMenuRowNames[] = {
    "Group",           "Song",        "StepsType", "Steps",
    "SourceStepsType", "SourceSteps", "Action"};
XToString(EditMenuRow);
XToLocalizedString(EditMenuRow);

static const char* EditMenuActionNames[] = {
    "Edit", "Delete", "Create", "Practice", "LoadAutosave",
};
XToString(EditMenuAction);
XToLocalizedString(EditMenuAction);
StringToX(EditMenuAction);

static std::string ARROWS_X_NAME(size_t i) {
  return ssprintf("Arrows%dX", int(i + 1));
}
static std::string ROW_Y_NAME(size_t i) {
  return ssprintf("Row%dY", int(i + 1));
}

void EditMenu::StripLockedStepsAndDifficulty(
    std::vector<StepsAndDifficulty>& v) {
  const Song* song = GetSelectedSong();
  for (int i = (int)v.size() - 1; i >= 0; i--) {
    if (v[i].steps_ && UNLOCKMAN->StepsIsLocked(song, v[i].steps_)) {
      v.erase(v.begin() + i);
    }
  }
}

void EditMenu::GetSongsToShowForGroup(
    const std::string& group, std::vector<Song*>& songs_out) {
  if (group == "") {
    songs_out.clear();
    return;
  }
  songs_out = SONGMAN->GetSongs(SHOW_GROUPS.GetValue() ? group : kGroupAll);
  EditMode mode = EDIT_MODE.GetValue();
  switch (mode) {
    case EditMode_Practice:
    case EditMode_CourseMods:
    case EditMode_Home:
      for (int i = songs_out.size() - 1; i >= 0; i--) {
        const Song* song = songs_out[i];
        if (!song->NormallyDisplayed() || song->IsTutorial()) {
          songs_out.erase(songs_out.begin() + i);
        }
      }
      break;
    case EditMode_Full:
      break;
    default:
      FAIL_M(ssprintf("Invalid edit mode: %i", mode));
  }
  SongUtil::SortSongPointerArrayByTitle(songs_out);
}

void EditMenu::GetGroupsToShow(std::vector<std::string>& groups_out) {
  groups_out.clear();
  if (!SHOW_GROUPS.GetValue()) {
    return;
  }

  SONGMAN->GetSongGroupNames(groups_out);
  for (int i = groups_out.size() - 1; i >= 0; i--) {
    const std::string& group = groups_out[i];
    std::vector<Song*> songs;
    GetSongsToShowForGroup(group, songs);
    // strip groups that have no unlocked songs
    if (songs.empty()) {
      groups_out.erase(groups_out.begin() + i);
    }
  }
}

EditMenu::EditMenu() {}

EditMenu::~EditMenu() { IMAGECACHE->Undemand("Banner"); }

void EditMenu::Load(const std::string& type) {
  LOG->Trace("EditMenu::Load");

  SHOW_GROUPS.Load(type, "ShowGroups");
  ARROWS_X.Load(type, ARROWS_X_NAME, kNumArrows);
  ARROWS_ENABLED_COMMAND.Load(type, "ArrowsEnabledCommand");
  ARROWS_DISABLED_COMMAND.Load(type, "ArrowsDisabledCommand");
  ROW_Y.Load(type, ROW_Y_NAME, NUM_EditMenuRow);
  EDIT_MODE.Load(type, "EditMode");
  TEXT_BANNER_TYPE.Load(name_, "TextBannerType");

  for (int i = 0; i < 2; i++) {
    spr_arrows_[i].Load(THEME->GetPathG(type, i == 0 ? "left" : "right"));
    spr_arrows_[i]->SetX(ARROWS_X.GetValue(i));
    this->AddChild(spr_arrows_[i]);
  }

  selected_row_ = GetFirstRow();

  ZERO(selection_);

  FOREACH_EditMenuRow(r) {
    text_label_[r].SetName(ssprintf("Label%i", r + 1));
    text_label_[r].LoadFromFont(THEME->GetPathF(type, "title"));
    text_label_[r].SetText(EditMenuRowToLocalizedString(r));
    ActorUtil::LoadAllCommandsAndSetXY(text_label_[r], type);
    // m_textLabel[r].SetHorizAlign( align_left );
    this->AddChild(&text_label_[r]);

    text_value_[r].SetName(ssprintf("Value%i", r + 1));
    text_value_[r].LoadFromFont(THEME->GetPathF(type, "value"));
    text_value_[r].SetText("blah");
    ActorUtil::LoadAllCommandsAndSetXY(text_value_[r], type);
    this->AddChild(&text_value_[r]);
  }

  text_label_[ROW_GROUP].SetVisible(SHOW_GROUPS.GetValue());
  text_value_[ROW_GROUP].SetVisible(SHOW_GROUPS.GetValue());

  // Load low-res banners, if needed.
  IMAGECACHE->Demand("Banner");

  if (SHOW_GROUPS.GetValue()) {
    group_banner_.SetName("GroupBanner");
    ActorUtil::SetXY(group_banner_, type);
    ActorUtil::LoadAllCommands(group_banner_, type);
    this->AddChild(&group_banner_);
  }

  song_banner_.SetName("SongBanner");
  ActorUtil::SetXY(song_banner_, type);
  ActorUtil::LoadAllCommands(song_banner_, type);
  this->AddChild(&song_banner_);

  song_text_banner_.SetName("SongTextBanner");
  song_text_banner_.Load(TEXT_BANNER_TYPE);
  ActorUtil::SetXY(song_text_banner_, type);
  ActorUtil::LoadAllCommands(song_text_banner_, type);
  this->AddChild(&song_text_banner_);

  steps_display_.SetName("StepsDisplay");
  steps_display_.Load("StepsDisplayEdit", nullptr);
  ActorUtil::SetXY(steps_display_, type);
  this->AddChild(&steps_display_);

  steps_display_source_.SetName("StepsDisplaySource");
  steps_display_source_.Load("StepsDisplayEdit", nullptr);
  ActorUtil::SetXY(steps_display_source_, type);
  this->AddChild(&steps_display_source_);

  sound_change_row_.Load(THEME->GetPathS(type, "row"), true);
  sound_change_value_.Load(THEME->GetPathS(type, "value"), true);

  // fill in data structures
  GetGroupsToShow(groups_);

  // In EditMode_Practice this will be filled in by OnRowValueChanged()
  if (EDIT_MODE.GetValue() != EditMode_Practice) {
    steps_types_ = CommonMetrics::STEPS_TYPES_TO_SHOW.GetValue();
  }

  RefreshAll();
}

void EditMenu::RefreshAll() {
  if (!SafeToUse()) {
    return;
  }

  ChangeToRow(GetFirstRow());
  OnRowValueChanged((EditMenuRow)0);

  // Select the current song if any
  if (GAMESTATE->cur_song_) {
    for (unsigned i = 0; i < groups_.size(); i++) {
      if (GAMESTATE->cur_song_->group_name_ == groups_[i]) {
        selection_[ROW_GROUP] = i;
      }
    }
    OnRowValueChanged(ROW_GROUP);

    for (unsigned i = 0; i < songs_.size(); i++) {
      if (GAMESTATE->cur_song_ == songs_[i]) {
        selection_[ROW_SONG] = i;
      }
    }
    OnRowValueChanged(ROW_SONG);

    // Select the current StepsType and difficulty if any
    if (GAMESTATE->cur_steps_[PLAYER_1]) {
      for (unsigned i = 0; i < steps_types_.size(); i++) {
        if (steps_types_[i] == GAMESTATE->cur_steps_[PLAYER_1]->steps_type_) {
          selection_[ROW_STEPS_TYPE] = i;
          OnRowValueChanged(ROW_STEPS_TYPE);
        }
      }

      for (unsigned i = 0; i < steps_.size(); i++) {
        const Steps* steps = steps_[i].steps_;
        if (steps == GAMESTATE->cur_steps_[PLAYER_1]) {
          selection_[ROW_STEPS] = i;
          OnRowValueChanged(ROW_STEPS);
        }
      }
    }
  }
}

bool EditMenu::SafeToUse() { return !groups_.empty(); }

bool EditMenu::CanGoUp() { return selected_row_ != GetFirstRow(); }

bool EditMenu::CanGoDown() { return selected_row_ != NUM_EditMenuRow - 1; }

bool EditMenu::CanGoLeft() {
  if (GetRowSize(selected_row_) <= 0) {
    return false;
  }
  if (selected_row_ == ROW_SONG || selected_row_ == ROW_GROUP) {
    return true;  // wraps
  }
  return selection_[selected_row_] != 0;
}

int EditMenu::GetRowSize(EditMenuRow er) const {
  switch (er) {
    case ROW_GROUP:
      return groups_.size();
    case ROW_SONG:
      return songs_.size();
    case ROW_STEPS_TYPE:
      return steps_types_.size();
    case ROW_STEPS:
      return steps_.size();
    case ROW_SOURCE_STEPS_TYPE:
      return steps_types_.size();
    case ROW_SOURCE_STEPS:
      return source_steps_.size();
    case ROW_ACTION:
      return actions_.size();
    default:
      FAIL_M(ssprintf("Non-existant EditMenuRow %i", er));
  }
}

bool EditMenu::CanGoRight() {
  if (GetRowSize(selected_row_) <= 0) {
    return false;
  }
  if (selected_row_ == ROW_SONG || selected_row_ == ROW_GROUP) {
    return true;  // wraps
  }
  return selection_[selected_row_] != GetRowSize(selected_row_) - 1;
}

bool EditMenu::RowIsSelectable(EditMenuRow row) {
  if (EDIT_MODE == EditMode_Home && row == ROW_STEPS) {
    return false;
  }

  if (GetSelectedSteps()) {
    switch (row) {
      case ROW_SOURCE_STEPS_TYPE:
      case ROW_SOURCE_STEPS:
        return false;
      default:
        return true;
    }
  }
  return true;
}

void EditMenu::Up() {
  EditMenuRow dest = selected_row_;
  do {
    dest = (EditMenuRow)(dest - 1);
  } while (!RowIsSelectable(dest));
  ASSERT(dest >= 0);
  ChangeToRow(dest);
  sound_change_row_.Play(true);
}

void EditMenu::Down() {
  EditMenuRow dest = selected_row_;
  do {
    dest = (EditMenuRow)(dest + 1);
  } while (!RowIsSelectable(dest));
  ASSERT(dest < NUM_EditMenuRow);
  ChangeToRow(dest);
  sound_change_row_.Play(true);
}

void EditMenu::Left() {
  if (CanGoLeft()) {
    selection_[selected_row_]--;
    wrap(selection_[selected_row_], GetRowSize(selected_row_));
    OnRowValueChanged(selected_row_);
    sound_change_value_.Play(true);
  }
}

void EditMenu::Right() {
  if (CanGoRight()) {
    selection_[selected_row_]++;
    wrap(selection_[selected_row_], GetRowSize(selected_row_));
    OnRowValueChanged(selected_row_);
    sound_change_value_.Play(true);
  }
}

void EditMenu::ChangeToRow(EditMenuRow new_row) {
  text_label_[new_row].PlayCommand("GainFocus");
  text_value_[new_row].PlayCommand("GainFocus");
  text_label_[selected_row_].PlayCommand("LoseFocus");
  text_value_[selected_row_].PlayCommand("LoseFocus");

  selected_row_ = new_row;

  for (int i = 0; i < 2; i++) {
    spr_arrows_[i]->SetY(ROW_Y.GetValue(new_row));
  }
  UpdateArrows();
}

void EditMenu::UpdateArrows() {
  spr_arrows_[0]->RunCommands(
      CanGoLeft() ? ARROWS_ENABLED_COMMAND : ARROWS_DISABLED_COMMAND);
  spr_arrows_[1]->RunCommands(
      CanGoRight() ? ARROWS_ENABLED_COMMAND : ARROWS_DISABLED_COMMAND);
  spr_arrows_[0]->EnableAnimation(CanGoLeft());
  spr_arrows_[1]->EnableAnimation(CanGoRight());
}

static LocalizedString BLANK("EditMenu", "Blank");
void EditMenu::OnRowValueChanged(EditMenuRow row) {
  if (!SafeToUse()) {
    return;
  }

  UpdateArrows();

  EditMode mode = EDIT_MODE.GetValue();

  switch (row) {
    case ROW_GROUP:
      songs_.clear();
      if (GetSelectedGroup() == "") {
        text_value_[ROW_GROUP].SetText(
            THEME->GetString(name_, "No Group Selected."));
        if (SHOW_GROUPS.GetValue()) {
          group_banner_.LoadFallback();
          group_banner_.PlayCommand("Changed");
        }
      } else {
        text_value_[ROW_GROUP].SetText(
            SONGMAN->ShortenGroupName(GetSelectedGroup()));
        if (SHOW_GROUPS.GetValue()) {
          group_banner_.LoadFromSongGroup(GetSelectedGroup());
          group_banner_.PlayCommand("Changed");
        }
        if (mode == EditMode_Practice) {
          std::vector<Song*> songs;
          GetSongsToShowForGroup(GetSelectedGroup(), songs);
          // Filter out songs that aren't playable.
          for (Song* s : songs) {
            if (SongUtil::IsSongPlayable(s)) {
              songs_.push_back(s);
            }
          }
        } else {
          GetSongsToShowForGroup(GetSelectedGroup(), songs_);
        }
      }

      selection_[ROW_SONG] = 0;
      [[fallthrough]];
    case ROW_SONG:
      if (GetSelectedSong() == nullptr) {
        text_value_[ROW_SONG].SetText("");
        song_banner_.LoadFallback();
        song_banner_.PlayCommand("Changed");
        song_text_banner_.SetFromString("", "", "", "", "", "");
        if (mode == EditMode_Practice) {
          selection_[ROW_STEPS_TYPE] = 0;
          steps_types_.clear();
        }
      } else {
        text_value_[ROW_SONG].SetText("");
        song_banner_.LoadFromSong(GetSelectedSong());
        song_banner_.PlayCommand("Changed");
        song_text_banner_.SetFromSong(GetSelectedSong());

        if (mode == EditMode_Practice) {
          StepsType org_sel = StepsType_Invalid;
          if (!steps_types_.empty())  // Not first run
          {
            ASSERT((int)steps_types_.size() > selection_[ROW_STEPS_TYPE]);
            org_sel = steps_types_[selection_[ROW_STEPS_TYPE]];
          }

          // The StepsType selection may no longer be valid. Zero it for now.
          selection_[ROW_STEPS_TYPE] = 0;
          steps_types_.clear();

          // Only show StepsTypes for which we have valid Steps.
          std::vector<StepsType> sts =
              CommonMetrics::STEPS_TYPES_TO_SHOW.GetValue();
          for (StepsType& st : sts) {
            if (SongUtil::GetStepsByDifficulty(
                    GetSelectedSong(), st, Difficulty_Invalid, false) !=
                nullptr) {
              steps_types_.push_back(st);
            }

            // Try to preserve the user's StepsType selection.
            if (st == org_sel) {
              selection_[ROW_STEPS_TYPE] = steps_types_.size() - 1;
            }
          }
        }
      }

      [[fallthrough]];
    case ROW_STEPS_TYPE:
      if (GetSelectedStepsType() == StepsType_Invalid) {
        text_value_[ROW_STEPS_TYPE].SetText(
            THEME->GetString(name_, "No StepsType selected."));
        steps_.clear();
      } else {
        text_value_[ROW_STEPS_TYPE].SetText(
            GAMEMAN->GetStepsTypeInfo(GetSelectedStepsType())
                .GetLocalizedString());

        Difficulty old = Difficulty_Invalid;
        if (!steps_.empty()) {
          old = GetSelectedDifficulty();
        }

        steps_.clear();

        FOREACH_ENUM(Difficulty, dc) {
          if (dc == Difficulty_Edit) {
            switch (mode) {
              case EditMode_Full:
              case EditMode_CourseMods:
              case EditMode_Practice: {
                std::vector<Steps*> v;
                SongUtil::GetSteps(
                    GetSelectedSong(), v, GetSelectedStepsType(),
                    Difficulty_Edit);
                StepsUtil::SortStepsByDescription(v);
                for (Steps* p : v) {
                  steps_.push_back(StepsAndDifficulty(p, dc));
                }
              } break;
              case EditMode_Home:
                // have only "New Edit"
                break;
              default:
                FAIL_M(ssprintf("Invalid edit mode: %i", mode));
            }

            switch (mode) {
              case EditMode_Practice:
              case EditMode_CourseMods:
                break;
              case EditMode_Home:
              case EditMode_Full:
                steps_.push_back(
                    StepsAndDifficulty(nullptr, dc));  // "New Edit"
                break;
              default:
                FAIL_M(ssprintf("Invalid edit mode: %i", mode));
            }
          } else {
            Steps* steps = SongUtil::GetStepsByDifficulty(
                GetSelectedSong(), GetSelectedStepsType(), dc);
            if (steps && UNLOCKMAN->StepsIsLocked(GetSelectedSong(), steps)) {
              steps = nullptr;
            }

            switch (mode) {
              case EditMode_Home:
                // don't allow selecting of non-edits in HomeMode
                break;
              case EditMode_Practice:
              case EditMode_CourseMods:
                // only show this difficulty if steps exist
                if (steps) {
                  steps_.push_back(StepsAndDifficulty(steps, dc));
                }
                break;
              case EditMode_Full:
                // show this difficulty whether or not steps exist.
                steps_.push_back(StepsAndDifficulty(steps, dc));
                break;
              default:
                FAIL_M(ssprintf("Invalid edit mode: %i", mode));
            }
          }
        }
        StripLockedStepsAndDifficulty(steps_);

        int i = 0;
        for (const StepsAndDifficulty& s : steps_) {
          if (s.dc_ == old) {
            selection_[ROW_STEPS] = i;
            break;
          }
          i++;
        }
        rage_clamp(selection_[ROW_STEPS], 0, steps_.size() - 1);
      }
      [[fallthrough]];
    case ROW_STEPS:
      if (GetSelectedSteps() == nullptr && mode == EditMode_Practice) {
        text_value_[ROW_STEPS].SetText(
            THEME->GetString(name_, "No Steps selected."));
        steps_display_.Unset();
      } else {
        std::string s = CustomDifficultyToLocalizedString(GetCustomDifficulty(
            GetSelectedStepsType(), GetSelectedDifficulty(),
            CourseType_Invalid));

        text_value_[ROW_STEPS].SetText(s);
        if (GetSelectedSteps()) {
          steps_display_.SetFromSteps(GetSelectedSteps());
        } else {
          steps_display_.SetFromStepsTypeAndMeterAndDifficultyAndCourseType(
              GetSelectedSourceStepsType(), 0, GetSelectedDifficulty(),
              CourseType_Invalid);
        }
      }
      [[fallthrough]];
    case ROW_SOURCE_STEPS_TYPE:
      if (mode == EditMode_Practice) {
        text_label_[ROW_SOURCE_STEPS_TYPE].SetVisible(false);
        text_value_[ROW_SOURCE_STEPS_TYPE].SetVisible(false);
      } else {
        text_label_[ROW_SOURCE_STEPS_TYPE].SetVisible(
            GetSelectedSteps() ? false : true);
        text_value_[ROW_SOURCE_STEPS_TYPE].SetVisible(
            GetSelectedSteps() ? false : true);
        text_value_[ROW_SOURCE_STEPS_TYPE].SetText(
            GAMEMAN->GetStepsTypeInfo(GetSelectedSourceStepsType())
                .GetLocalizedString());
        source_steps_.clear();
        source_steps_.push_back(
            StepsAndDifficulty(nullptr, Difficulty_Invalid));  // "blank"

        FOREACH_ENUM(Difficulty, dc) {
          // fill in m_vpSourceSteps
          if (dc != Difficulty_Edit) {
            Steps* steps = SongUtil::GetStepsByDifficulty(
                GetSelectedSong(), GetSelectedSourceStepsType(), dc);
            if (steps != nullptr) {
              source_steps_.push_back(StepsAndDifficulty(steps, dc));
            }
          } else {
            std::vector<Steps*> v;
            SongUtil::GetSteps(
                GetSelectedSong(), v, GetSelectedSourceStepsType(), dc);
            StepsUtil::SortStepsByDescription(v);
            for (Steps* steps : v) {
              source_steps_.push_back(StepsAndDifficulty(steps, dc));
            }
          }
        }
        StripLockedStepsAndDifficulty(steps_);
        rage_clamp(selection_[ROW_SOURCE_STEPS], 0, source_steps_.size() - 1);
      }
      [[fallthrough]];
    case ROW_SOURCE_STEPS:
      if (mode == EditMode_Practice) {
        text_label_[ROW_SOURCE_STEPS].SetVisible(false);
        text_value_[ROW_SOURCE_STEPS].SetVisible(false);
        actions_.clear();
        actions_.push_back(EditMenuAction_Practice);
      } else {
        text_label_[ROW_SOURCE_STEPS].SetVisible(
            GetSelectedSteps() ? false : true);
        text_value_[ROW_SOURCE_STEPS].SetVisible(
            GetSelectedSteps() ? false : true);
        {
          std::string s;
          if (GetSelectedSourceDifficulty() == Difficulty_Invalid) {
            s = BLANK;
          } else {
            s = CustomDifficultyToLocalizedString(GetCustomDifficulty(
                GetSelectedSourceStepsType(), GetSelectedSourceDifficulty(),
                CourseType_Invalid));
          }
          text_value_[ROW_SOURCE_STEPS].SetText(s);
        }
        bool hide_meter = false;
        if (GetSelectedSourceDifficulty() == Difficulty_Invalid) {
          hide_meter = true;
        } else if (GetSelectedSourceSteps()) {
          steps_display_source_.SetFromSteps(GetSelectedSourceSteps());
        } else {
          steps_display_source_
              .SetFromStepsTypeAndMeterAndDifficultyAndCourseType(
                  GetSelectedSourceStepsType(), 0,
                  GetSelectedSourceDifficulty(), CourseType_Invalid);
        }
        steps_display_source_.SetVisible(!(hide_meter || GetSelectedSteps()));

        actions_.clear();
        // Stick autosave in the list first so that people will see it. -Kyz
        Song* cur_song = GetSelectedSong();
        if (cur_song != nullptr && cur_song->HasAutosaveFile() &&
            !cur_song->WasLoadedFromAutosave()) {
          actions_.push_back(EditMenuAction_LoadAutosave);
        }
        if (GetSelectedSteps()) {
          switch (mode) {
            case EditMode_Practice:
            case EditMode_CourseMods:
              actions_.push_back(EditMenuAction_Practice);
              break;
            case EditMode_Home:
            case EditMode_Full:
              actions_.push_back(EditMenuAction_Edit);
              actions_.push_back(EditMenuAction_Delete);
              break;
            default:
              FAIL_M(ssprintf("Invalid edit mode: %i", mode));
          }
        } else {
          actions_.push_back(EditMenuAction_Create);
        }
        selection_[ROW_ACTION] = 0;
      }
      [[fallthrough]];
    case ROW_ACTION:
      if (GetSelectedAction() == EditMenuAction_Invalid) {
        text_value_[ROW_ACTION].SetText(
            THEME->GetString(name_, "No valid action."));
      } else {
        text_value_[ROW_ACTION].SetText(
            EditMenuActionToLocalizedString(GetSelectedAction()));
      }
      break;
    default:
      FAIL_M(ssprintf("Invalid EditMenuRow: %i", row));
  }
}

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
