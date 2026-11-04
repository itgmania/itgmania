#ifndef EDIT_MENU_H
#define EDIT_MENU_H

#include <string>
#include <vector>

#include "Actor.h"
#include "ActorFrame.h"
#include "AutoActor.h"
#include "BitmapText.h"
#include "Difficulty.h"
#include "EnumHelper.h"
#include "FadingBanner.h"
#include "GameConstantsAndTypes.h"
#include "RageSound.h"
#include "StepsDisplay.h"
#include "TextBanner.h"
#include "ThemeMetric.h"

/** @brief What type of row is needed for the EditMenu? */
enum EditMenuRow {
  ROW_GROUP,
  ROW_SONG,
  ROW_STEPS_TYPE,
  ROW_STEPS,
  ROW_SOURCE_STEPS_TYPE,
  ROW_SOURCE_STEPS,
  ROW_ACTION,
  NUM_EditMenuRow /**< The number of EditMenuRows available. */
};
/** @brief Loop through each EditMenuRow. */
#define FOREACH_EditMenuRow(r) FOREACH_ENUM(EditMenuRow, r)
/**
 * @brief Turn the EditMenuRow into a string.
 * @param r the row.
 * @return the string. */
const std::string& EditMenuRowToString(EditMenuRow r);
/**
 * @brief Turn the EditMenuRow into a localized string.
 * @param r the row.
 * @return the localized string. */
const std::string& EditMenuRowToLocalizedString(EditMenuRow r);

/** @brief The different actions one can take on a step. */
enum EditMenuAction {
  EditMenuAction_Edit,     /**< Modify the current step for the Song. */
  EditMenuAction_Delete,   /**< Remove the current step from the Song. */
  EditMenuAction_Create,   /**< Create a new step for the Song. */
  EditMenuAction_Practice, /**< Practice the current step for the Song. */
  EditMenuAction_LoadAutosave,
  NUM_EditMenuAction, /**< The number of MenuActions available to choose from.
                       */
  EditMenuAction_Invalid
};
/** @brief Loop through each EditMenuAction. */
#define FOREACH_EditMenuAction(ema) FOREACH_ENUM(EditMenuAction, ema)
/**
 * @brief Turn the EditMenuAction into a string.
 * @param ema the action.
 * @return the string. */
const std::string& EditMenuActionToString(EditMenuAction ema);
/**
 * @brief Turn the EditMenuAction into a localized string.
 * @param ema the action.
 * @return the localized string. */
const std::string& EditMenuActionToLocalizedString(EditMenuAction ema);

/** @brief How many arrows are used for the EditMenu? */
const int NUM_ARROWS = 2;

/**
 * @brief UI on Edit Menu screen.
 *
 * Create Steps, delete Steps, or launch Steps in editor. */
class EditMenu : public ActorFrame {
 public:
  /** @brief Set up the EditMenu. */
  EditMenu();
  /** @brief Destroy the EditMenu. */
  ~EditMenu();
  void Load(const std::string& sType);

  /** @brief Determine if we can move up.
   * @return true if we can, false otherwise. */
  bool CanGoUp();
  /** @brief Determine if we can move down.
   * @return true if we can, false otherwise. */
  bool CanGoDown();
  /** @brief Determine if we can move left.
   * @return true if we can, false otherwise. */
  bool CanGoLeft();
  /** @brief Determine if we can move right.
   * @return true if we can, false otherwise. */
  bool CanGoRight();
  /** @brief Determine if the EditMenuRow is selectable.
   * @param row the row in question.
   * @return true if it can be selected, false otherwise. */
  bool RowIsSelectable(EditMenuRow row);

  /** @brief Move up to the next selection. */
  void Up();
  /** @brief Move down to the next selection. */
  void Down();
  /** @brief Move left to the next selection. */
  void Left();
  /** @brief Move right to the next selection. */
  void Right();

  void RefreshAll();

  bool SafeToUse();

#define RETURN_IF_INVALID(check, retval) \
  if (check) {                           \
    return retval;                       \
  }

  /** @brief Retrieve the currently selected group.
   * @return the current group. */
  std::string GetSelectedGroup() const {
    if (!SHOW_GROUPS.GetValue()) {
      return GROUP_ALL;
    }
    int groups = static_cast<int>(groups_.size());
    RETURN_IF_INVALID(selection_[ROW_GROUP] >= groups, "");
    return groups_[selection_[ROW_GROUP]];
  }
  /** @brief Retrieve the currently selected song.
   * @return the current song. */
  Song* GetSelectedSong() const {
    RETURN_IF_INVALID(
        songs_.empty() || selection_[ROW_SONG] >= (int)songs_.size(), nullptr);
    return songs_[selection_[ROW_SONG]];
  }
  /** @brief Retrieve the currently selected steps type.
   * @return the current steps type. */
  StepsType GetSelectedStepsType() const {
    RETURN_IF_INVALID(
        steps_types_.empty() ||
            selection_[ROW_STEPS_TYPE] >= (int)steps_types_.size(),
        StepsType_Invalid);
    return steps_types_[selection_[ROW_STEPS_TYPE]];
  }
  /** @brief Retrieve the currently selected steps.
   * @return the current steps. */
  Steps* GetSelectedSteps() const {
    RETURN_IF_INVALID(
        steps_.empty() || selection_[ROW_STEPS] >= (int)steps_.size(), nullptr);
    return steps_[selection_[ROW_STEPS]].steps_;
  }
  /** @brief Retrieve the currently selected difficulty.
   * @return the current difficulty. */
  Difficulty GetSelectedDifficulty() const {
    RETURN_IF_INVALID(
        steps_.empty() || selection_[ROW_STEPS] >= (int)steps_.size(),
        Difficulty_Invalid);
    return steps_[selection_[ROW_STEPS]].dc_;
  }
  /** @brief Retrieve the currently selected source steps type.
   * @return the current source steps type. */
  StepsType GetSelectedSourceStepsType() const {
    RETURN_IF_INVALID(
        steps_types_.empty() ||
            selection_[ROW_SOURCE_STEPS_TYPE] >= (int)steps_types_.size(),
        StepsType_Invalid);
    return steps_types_[selection_[ROW_SOURCE_STEPS_TYPE]];
  }
  /** @brief Retrieve the currently selected source steps.
   * @return the current source steps. */
  Steps* GetSelectedSourceSteps() const {
    RETURN_IF_INVALID(
        source_steps_.empty() ||
            selection_[ROW_SOURCE_STEPS] >= (int)source_steps_.size(),
        nullptr);
    return source_steps_[selection_[ROW_SOURCE_STEPS]].steps_;
  }
  /** @brief Retrieve the currently selected difficulty.
   * @return the current difficulty. */
  Difficulty GetSelectedSourceDifficulty() const {
    RETURN_IF_INVALID(
        source_steps_.empty() ||
            selection_[ROW_SOURCE_STEPS] >= (int)source_steps_.size(),
        Difficulty_Invalid);
    return source_steps_[selection_[ROW_SOURCE_STEPS]].dc_;
  }
  /** @brief Retrieve the currently selected action.
   * @return the current action. */
  EditMenuAction GetSelectedAction() const {
    RETURN_IF_INVALID(
        actions_.empty() || selection_[ROW_ACTION] >= (int)actions_.size(),
        EditMenuAction_Invalid);
    return actions_[selection_[ROW_ACTION]];
  }

#undef RETURN_IF_INVALID

  /** @brief Retrieve the currently selected row.
   * @return the current row. */
  EditMenuRow GetSelectedRow() const { return selected_row_; }

 private:
  struct StepsAndDifficulty;

  void StripLockedStepsAndDifficulty(std::vector<StepsAndDifficulty>& v);
  void GetSongsToShowForGroup(
      const std::string& sGroup, std::vector<Song*>& vpSongsOut);
  void GetGroupsToShow(std::vector<std::string>& vsGroupsOut);

  void UpdateArrows();
  AutoActor spr_arrows_[NUM_ARROWS];

  EditMenuRow selected_row_;
  EditMenuRow GetFirstRow() const {
    return SHOW_GROUPS.GetValue() ? ROW_GROUP : ROW_SONG;
  }
  int GetRowSize(EditMenuRow er) const;
  int selection_[NUM_EditMenuRow];
  BitmapText text_label_[NUM_EditMenuRow];
  BitmapText text_value_[NUM_EditMenuRow];

  /** @brief The group's banner. */
  FadingBanner group_banner_;
  /** @brief The Song's banner. */
  FadingBanner song_banner_;
  TextBanner song_text_banner_;
  StepsDisplay steps_display_;
  StepsDisplay steps_display_source_;

  struct StepsAndDifficulty {
    StepsAndDifficulty(Steps* s, Difficulty d) {
      steps_ = s;
      dc_ = d;
    }
    Steps* steps_;
    Difficulty dc_;
  };

  /** @brief The list of groups. */
  std::vector<std::string> groups_;
  /** @brief The list of Songs in a group. */
  std::vector<Song*> songs_;
  std::vector<StepsType> steps_types_;
  std::vector<StepsAndDifficulty> steps_;
  std::vector<StepsAndDifficulty> source_steps_;
  std::vector<EditMenuAction> actions_;

  void OnRowValueChanged(EditMenuRow row);
  void ChangeToRow(EditMenuRow newRow);

  RageSound sound_change_row_;
  RageSound sound_change_value_;

  /** @brief A metric to determine if groups are shown. */
  ThemeMetric<bool> SHOW_GROUPS;
  ThemeMetric1D<float> ARROWS_X;
  ThemeMetric<apActorCommands> ARROWS_ENABLED_COMMAND;
  ThemeMetric<apActorCommands> ARROWS_DISABLED_COMMAND;
  ThemeMetric1D<float> ROW_Y;

 public:
  ThemeMetric<EditMode> EDIT_MODE;
  ThemeMetric<std::string> TEXT_BANNER_TYPE;
};

#endif

/**
 * @file
 * @author Chris Danford (c) 2001-2004
 * @section LICENSE
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
