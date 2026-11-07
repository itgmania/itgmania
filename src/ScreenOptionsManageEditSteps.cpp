#include "ScreenOptionsManageEditSteps.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "GameManager.h"
#include "GameState.h"
#include "LocalizedString.h"
#include "NotesWriterSM.h"
#include "OptionRowHandler.h"
#include "PlayerNumber.h"
#include "Profile.h"
#include "ProfileManager.h"
#include "RageFileManager.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "Screen.h"
#include "ScreenManager.h"
#include "ScreenMessage.h"
#include "ScreenMiniMenu.h"
#include "ScreenOptions.h"
#include "ScreenPrompt.h"
#include "ScreenTextEntry.h"
#include "Song.h"
#include "SongManager.h"
#include "SongUtil.h"
#include "Steps.h"
#include "global.h"

AutoScreenMessage(SM_BackFromRename);
AutoScreenMessage(SM_BackFromDelete);
AutoScreenMessage(SM_BackFromContextMenu);

enum StepsEditAction {
  StepsEditAction_Edit,
  StepsEditAction_Rename,
  StepsEditAction_Delete,
  NUM_StepsEditAction
};
static const char* StepsEditActionNames[] = {
    "Edit",
    "Rename",
    "Delete",
};
XToString(StepsEditAction);
/** @brief Loop through each StepsEditAction. */
#define FOREACH_StepsEditAction(i) FOREACH_ENUM(StepsEditAction, i)

static MenuDef g_TempMenu("ScreenMiniMenuContext");

REGISTER_SCREEN_CLASS(ScreenOptionsManageEditSteps);

void ScreenOptionsManageEditSteps::Init() {
  ScreenOptions::Init();

  CREATE_NEW_SCREEN.Load(name_, "CreateNewScreen");
}

void ScreenOptionsManageEditSteps::BeginScreen() {
  // Reload so that we're consistent with the disk in case the user has been
  // dinking around with their edits.
  SONGMAN->FreeAllLoadedFromProfile(ProfileSlot_Machine);
  SONGMAN->LoadStepEditsFromProfileDir(
      PROFILEMAN->GetProfileDir(ProfileSlot_Machine), ProfileSlot_Machine);
  SONGMAN->LoadCourseEditsFromProfileDir(
      PROFILEMAN->GetProfileDir(ProfileSlot_Machine), ProfileSlot_Machine);
  GAMESTATE->cur_song_.Set(nullptr);
  GAMESTATE->cur_steps_[PLAYER_1].Set(nullptr);

  std::vector<OptionRowHandler*> hands;

  int index = 0;

  {
    hands.push_back(OptionRowHandlerUtil::MakeNull());
    OptionRowDefinition& def = hands.back()->def_;
    def.layout_type_ = LAYOUT_SHOW_ONE_IN_ROW;
    def.one_choice_for_all_players_ = true;
    def.name_ = "Create New Edit Steps";
    def.explanation_name_ = "Create New Edit Steps";
    def.choices_.clear();
    def.choices_.push_back("");
    index++;
  }

  SONGMAN->GetStepsLoadedFromProfile(steps_, ProfileSlot_Machine);

  for (const Steps* s : steps_) {
    hands.push_back(OptionRowHandlerUtil::MakeNull());
    OptionRowDefinition& def = hands.back()->def_;

    Song* song = s->song_;

    def.name_ = song->GetTranslitFullTitle() + " - " + s->GetDescription();
    def.allow_theme_title_ = false;  // not themable
    def.explanation_name_ = "Select Edit Steps";
    def.choices_.clear();
    StepsType st = s->steps_type_;
    std::string type = GAMEMAN->GetStepsTypeInfo(st).GetLocalizedString();
    def.choices_.push_back(type);
    def.allow_theme_items_ = false;  // already themed
    index++;
  }

  ScreenOptions::InitMenu(hands);

  ScreenOptions::BeginScreen();

  // select the last chosen course
  if (GAMESTATE->cur_steps_[PLAYER_1]) {
    std::vector<Steps*>::const_iterator iter =
        find(steps_.begin(), steps_.end(), GAMESTATE->cur_steps_[PLAYER_1]);
    if (iter != steps_.end()) {
      index = iter - steps_.begin();
      this->MoveRowAbsolute(PLAYER_1, 1 + index);
    }
  }

  AfterChangeRow(PLAYER_1);
}

static LocalizedString THESE_STEPS_WILL_BE_LOST(
    "ScreenOptionsManageEditSteps", "These steps will be lost permanently.");
static LocalizedString CONTINUE_WITH_DELETE(
    "ScreenOptionsManageEditSteps", "Continue with delete?");
static LocalizedString ENTER_NAME_FOR_STEPS(
    "ScreenOptionsManageEditSteps", "Enter a name for these steps.");
void ScreenOptionsManageEditSteps::HandleScreenMessage(const ScreenMessage sm) {
  if (sm == SM_GoToNextScreen) {
    int cur_row = current_row_[GAMESTATE->GetMasterPlayerNumber()];

    if (cur_row == 0)  // "create new"
    {
      SCREENMAN->SetNewScreen(CREATE_NEW_SCREEN);
      return;  // don't call base
    } else if (rows_[cur_row]->GetRowType() == OptionRow::RowType_Exit) {
      this->HandleScreenMessage(SM_GoToPrevScreen);
      return;  // don't call base
    } else     // a Steps
    {
      Steps* steps = GAMESTATE->cur_steps_[PLAYER_1];
      ASSERT(steps != nullptr);
      const Style* style =
          GAMEMAN->GetEditorStyleForStepsType(steps->steps_type_);
      GAMESTATE->SetCurrentStyle(style, PLAYER_INVALID);
      // do base behavior
    }
  } else if (sm == SM_BackFromRename) {
    if (!ScreenTextEntry::cancelled_last_) {
      ASSERT(
          ScreenTextEntry::last_answer_ !=
          "");  // validate should have assured this

      Steps* steps = GAMESTATE->cur_steps_[PLAYER_1];
      Song* song = steps->song_;

      steps->SetDescription(ScreenTextEntry::last_answer_);

      std::string error;
      if (!NotesWriterSM::WriteEditFileToMachine(song, steps, error)) {
        ScreenPrompt::Prompt(SM_None, error);
        return;
      }

      SCREENMAN->SetNewScreen(this->name_);  // reload
    }
  } else if (sm == SM_BackFromDelete) {
    if (ScreenPrompt::last_answer_ == ANSWER_YES) {
      LOG->Trace("Delete successful; deleting Steps from memory");

      Steps* steps = GetStepsWithFocus();
      FILEMAN->Remove(steps->GetFilename());
      SONGMAN->DeleteSteps(steps);
      GAMESTATE->cur_steps_[PLAYER_1].Set(nullptr);
      SCREENMAN->SetNewScreen(this->name_);  // reload
    }
  } else if (sm == SM_BackFromContextMenu) {
    if (!ScreenMiniMenu::cancelled_) {
      switch (ScreenMiniMenu::last_row_code_) {
        case StepsEditAction_Edit: {
          Steps* steps = GetStepsWithFocus();
          Song* song = steps->song_;
          GAMESTATE->cur_song_.Set(song);
          GAMESTATE->cur_steps_[PLAYER_1].Set(steps);

          ScreenOptions::BeginFadingOut();
        } break;
        case StepsEditAction_Rename: {
          ScreenTextEntry::TextEntry(
              SM_BackFromRename, ENTER_NAME_FOR_STEPS,
              GAMESTATE->cur_steps_[PLAYER_1]->GetDescription(),
              MAX_STEPS_DESCRIPTION_LENGTH,
              SongUtil::ValidateCurrentEditStepsDescription);
        } break;
        case StepsEditAction_Delete: {
          ScreenPrompt::Prompt(
              SM_BackFromDelete,
              THESE_STEPS_WILL_BE_LOST.GetValue() + "\n\n" +
                  CONTINUE_WITH_DELETE.GetValue(),
              PROMPT_YES_NO, ANSWER_NO);
        } break;
      }
    }
  } else if (sm == SM_LoseFocus) {
    this->PlayCommand("ScreenLoseFocus");
  } else if (sm == SM_GainFocus) {
    this->PlayCommand("ScreenGainFocus");
  }

  ScreenOptions::HandleScreenMessage(sm);
}

void ScreenOptionsManageEditSteps::AfterChangeRow(PlayerNumber pn) {
  Steps* steps = GetStepsWithFocus();
  Song* song = steps ? steps->song_ : nullptr;

  GAMESTATE->cur_song_.Set(song);
  GAMESTATE->cur_steps_[PLAYER_1].Set(steps);

  ScreenOptions::AfterChangeRow(pn);
}

static LocalizedString YOU_HAVE_MAX_STEP_EDITS(
    "ScreenOptionsManageEditSteps",
    "You have %d step edits, the maximum number allowed.");
static LocalizedString YOU_MUST_DELETE(
    "ScreenOptionsManageEditSteps",
    "You must delete an existing steps edit before creating a new steps edit.");
void ScreenOptionsManageEditSteps::ProcessMenuStart(const InputEventPlus&) {
  if (IsTransitioning()) {
    return;
  }

  int cur_row = current_row_[GAMESTATE->GetMasterPlayerNumber()];

  if (cur_row == 0)  // "create new"
  {
    std::vector<Steps*> v;
    SONGMAN->GetStepsLoadedFromProfile(v, ProfileSlot_Machine);
    if (v.size() >= size_t(MAX_EDIT_STEPS_PER_PROFILE)) {
      std::string s = ssprintf(
          (YOU_HAVE_MAX_STEP_EDITS.GetValue() + "\n\n" +
           YOU_MUST_DELETE.GetValue())
              .c_str(),
          MAX_EDIT_STEPS_PER_PROFILE);
      ScreenPrompt::Prompt(SM_None, s);
      return;
    }
    SCREENMAN->PlayStartSound();
    this->BeginFadingOut();
  } else if (rows_[cur_row]->GetRowType() == OptionRow::RowType_Exit) {
    SCREENMAN->PlayStartSound();
    this->BeginFadingOut();
  } else  // a Steps
  {
    g_TempMenu.rows_.clear();
    FOREACH_StepsEditAction(i) {
      MenuRowDef mrd(
          i, StepsEditActionToString(i), true, EditMode_Home, true, true, 0,
          "");
      g_TempMenu.rows_.push_back(mrd);
    }

    int width, x, y;
    this->GetWidthXY(PLAYER_1, cur_row, 0, width, x, y);
    ScreenMiniMenu::MiniMenu(
        &g_TempMenu, SM_BackFromContextMenu, SM_BackFromContextMenu, (float)x,
        (float)y);
  }
}

void ScreenOptionsManageEditSteps::ImportOptions(
    int row, const std::vector<PlayerNumber>& vpns) {}

void ScreenOptionsManageEditSteps::ExportOptions(
    int row, const std::vector<PlayerNumber>& vpns) {}

Steps* ScreenOptionsManageEditSteps::GetStepsWithFocus() const {
  int cur_row = current_row_[GAMESTATE->GetMasterPlayerNumber()];
  if (cur_row == 0) {
    return nullptr;
  } else if (rows_[cur_row]->GetRowType() == OptionRow::RowType_Exit) {
    return nullptr;
  }

  // a Steps
  int steps_index = cur_row - 1;
  return steps_[steps_index];
}

/*
 * (c) 2002-2005 Chris Danford
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
