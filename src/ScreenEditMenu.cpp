#include "ScreenEditMenu.h"

#include <string>

#include "ActorUtil.h"
#include "Difficulty.h"
#include "EditMenu.h"
#include "GameConstantsAndTypes.h"
#include "GameManager.h"
#include "GameSoundManager.h"
#include "GameState.h"
#include "LocalizedString.h"
#include "PlayerNumber.h"
#include "RageFile.h"
#include "RageFileManager.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "Screen.h"
#include "ScreenManager.h"
#include "ScreenMessage.h"
#include "ScreenPrompt.h"
#include "ScreenTextEntry.h"
#include "ScreenWithMenuElements.h"
#include "Song.h"
#include "SongManager.h"
#include "SongUtil.h"
#include "Steps.h"
#include "ThemeManager.h"
#include "global.h"

static const std::string TEMP_FILE_NAME = "--temp--";

#define EXPLANATION_TEXT(row) \
  THEME->GetString(name_, "Explanation" + EditMenuRowToString(row))
#define EDIT_MENU_TYPE THEME->GetMetric(name_, "EditMenuType")

AutoScreenMessage(SM_RefreshSelector);
AutoScreenMessage(SM_BackFromEditDescription);

REGISTER_SCREEN_CLASS(ScreenEditMenu);

void ScreenEditMenu::Init() {
  // HACK: Disable any style set by the editor.
  GAMESTATE->SetCurrentStyle(nullptr, PLAYER_INVALID);

  // Enable all players.
  FOREACH_PlayerNumber(pn) GAMESTATE->JoinPlayer(pn);

  // Edit mode DOES NOT WORK if the master player is not player 1.  The same
  // is true of various parts of this poorly designed screen. -Kyz
  if (GAMESTATE->GetMasterPlayerNumber() != PLAYER_1) {
    LOG->Warn(
        "Master player number was not player 1, forcing it to player 1 so that "
        "edit mode will work.  If playing in edit mode doesn't work, this "
        "might be related.");
    GAMESTATE->SetMasterPlayerNumber(PLAYER_1);
  }

  ScreenWithMenuElements::Init();

  selector_.SetName("EditMenu");
  selector_.Load(EDIT_MENU_TYPE);
  selector_.SetXY(0, 0);
  this->AddChild(&selector_);

  text_explanation_.SetName("Explanation");
  text_explanation_.LoadFromFont(THEME->GetPathF(name_, "explanation"));
  LOAD_ALL_COMMANDS_AND_SET_XY(text_explanation_);
  RefreshExplanationText();
  this->AddChild(&text_explanation_);

  text_num_steps_loaded_from_profile_.SetName("NumStepsLoadedFromProfile");
  text_num_steps_loaded_from_profile_.LoadFromFont(
      THEME->GetPathF(name_, "NumStepsLoadedFromProfile"));
  LOAD_ALL_COMMANDS_AND_SET_XY_AND_ON_COMMAND(
      text_num_steps_loaded_from_profile_);
  RefreshNumStepsLoadedFromProfile();
  this->AddChild(&text_num_steps_loaded_from_profile_);
  if (!selector_.SafeToUse()) {
    no_songs_message_.SetName("NoSongsMessage");
    no_songs_message_.LoadFromFont(THEME->GetPathF(name_, "NoSongsMessage"));
    LOAD_ALL_COMMANDS_AND_SET_XY_AND_ON_COMMAND(no_songs_message_);
    AddChild(&no_songs_message_);
    selector_.SetVisible(false);
    text_explanation_.SetVisible(false);
    text_num_steps_loaded_from_profile_.SetVisible(false);
  }
}

void ScreenEditMenu::HandleScreenMessage(const ScreenMessage SM) {
  if (SM == SM_RefreshSelector) {
    selector_.RefreshAll();
    RefreshNumStepsLoadedFromProfile();
  } else if (
      SM == SM_Success &&
      selector_.GetSelectedAction() == EditMenuAction_Delete) {
    LOG->Trace("Delete successful; deleting steps from memory");

    Song* pSong = GAMESTATE->cur_song_;
    Steps* pStepsToDelete = GAMESTATE->cur_steps_[PLAYER_1];
    FOREACH_PlayerNumber(pn) { GAMESTATE->cur_steps_[pn].Set(nullptr); }
    bool bSaveSong = !pStepsToDelete->WasLoadedFromProfile();
    pSong->DeleteSteps(pStepsToDelete);
    SONGMAN->Invalidate(pSong);

    /* Only save to the main .SM file if the steps we're deleting
     * were loaded from it. */
    if (bSaveSong) {
      pSong->Save();
      SCREENMAN->ZeroNextUpdate();
    }
    SCREENMAN->SendMessageToTopScreen(SM_RefreshSelector);
  } else if (
      SM == SM_Failure &&
      selector_.GetSelectedAction() == EditMenuAction_Delete) {
    LOG->Trace("Delete failed; not deleting steps");
  } else if (SM == SM_BackFromEditDescription) {
    if (!ScreenTextEntry::cancelled_last_) {
      SOUND->StopMusic();
      StartTransitioningScreen(SM_GoToNextScreen);
    }
  }

  ScreenWithMenuElements::HandleScreenMessage(SM);
}

bool ScreenEditMenu::MenuUp(const InputEventPlus&) {
  if (selector_.CanGoUp()) {
    selector_.Up();
    RefreshExplanationText();
  }
  return true;
}

bool ScreenEditMenu::MenuDown(const InputEventPlus&) {
  if (selector_.CanGoDown()) {
    selector_.Down();
    RefreshExplanationText();
  }
  return true;
}

bool ScreenEditMenu::MenuLeft(const InputEventPlus&) {
  if (selector_.CanGoLeft()) {
    selector_.Left();
  }
  return true;
}

bool ScreenEditMenu::MenuRight(const InputEventPlus&) {
  if (selector_.CanGoRight()) {
    selector_.Right();
  }
  return true;
}

static std::string GetCopyDescription(const Steps* pSourceSteps) {
  std::string s = pSourceSteps->GetDescription();
  return s;
}

static void SetCurrentStepsDescription(const std::string& s) {
  GAMESTATE->cur_steps_[0]->SetDescription(s);
}

static void DeleteCurrentSteps() {
  GAMESTATE->cur_song_->DeleteSteps(GAMESTATE->cur_steps_[0]);
  GAMESTATE->cur_steps_[0].Set(nullptr);
}

static LocalizedString MISSING_MUSIC_FILE(
    "ScreenEditMenu",
    "This song is missing a music file and cannot be edited.");
static LocalizedString SONG_DIR_READ_ONLY(
    "ScreenEditMenu", "The song directory is read-only and cannot be edited.");
static LocalizedString DELETED_AUTOGEN_STEPS(
    "ScreenEditMenu",
    "These steps are produced by autogen.  You do not need to delete them.");
static LocalizedString STEPS_WILL_BE_LOST(
    "ScreenEditMenu", "These steps will be lost permanently.");
static LocalizedString CONTINUE_WITH_DELETE(
    "ScreenEditMenu", "Continue with delete?");
static LocalizedString ENTER_EDIT_DESCRIPTION(
    "ScreenEditMenu", "Enter a description for this edit.");
static LocalizedString INVALID_SELECTION(
    "ScreenEditMenu",
    "One of the selected things is invalid.  Pick something valid instead.");

bool ScreenEditMenu::MenuStart(const InputEventPlus&) {
  if (IsTransitioning()) {
    return false;
  }
  if (!selector_.SafeToUse()) {
    return false;
  }

  if (selector_.CanGoDown()) {
    selector_.Down();
    RefreshExplanationText();
    return true;
  }

  Song* pSong = selector_.GetSelectedSong();
  StepsType st = selector_.GetSelectedStepsType();
  Difficulty dc = selector_.GetSelectedDifficulty();
  Steps* pSteps = selector_.GetSelectedSteps();
  //	StepsType soureNT	= m_Selector.GetSelectedSourceStepsType();
  //	Difficulty sourceDiff	= m_Selector.GetSelectedSourceDifficulty();
  Steps* pSourceSteps = selector_.GetSelectedSourceSteps();
  EditMenuAction action = selector_.GetSelectedAction();
  if (st == StepsType_Invalid) {
    ScreenPrompt::Prompt(SM_None, INVALID_SELECTION);
    return true;
  }

  GAMESTATE->cur_song_.Set(pSong);
  GAMESTATE->cur_course_.Set(nullptr);
  GAMESTATE->SetCurrentStyle(
      GAMEMAN->GetEditorStyleForStepsType(st), PLAYER_INVALID);
  GAMESTATE->cur_steps_[PLAYER_1].Set(pSteps);

  // handle error cases
  if (!pSong->HasMusic()) {
    ScreenPrompt::Prompt(SM_None, MISSING_MUSIC_FILE);
    return true;
  }

  switch (selector_.EDIT_MODE) {
    case EditMode_Full: {
      std::string sDir = pSong->GetSongDir();
      std::string sTempFile = sDir + TEMP_FILE_NAME;
      RageFile file;
      if (!file.Open(sTempFile, RageFile::WRITE)) {
        ScreenPrompt::Prompt(SM_None, SONG_DIR_READ_ONLY);
        return true;
      }

      file.Close();
      FILEMAN->Remove(sTempFile);
      break;
    }
    default:
      break;
  }

  switch (action) {
    case EditMenuAction_Delete: {
      ASSERT(pSteps != nullptr);
      if (pSteps->IsAutogen()) {
        SCREENMAN->PlayInvalidSound();
        SCREENMAN->SystemMessage(DELETED_AUTOGEN_STEPS.GetValue());
        return true;
      }
    }
    default:
      break;
  }

  // Do work
  switch (action) {
    case EditMenuAction_Edit:
    case EditMenuAction_Practice:
      break;
    case EditMenuAction_Delete:
      ASSERT(pSteps != nullptr);
      ScreenPrompt::Prompt(
          SM_None,
          STEPS_WILL_BE_LOST.GetValue() + "\n\n" +
              CONTINUE_WITH_DELETE.GetValue(),
          PROMPT_YES_NO, ANSWER_NO);
      break;
    case EditMenuAction_LoadAutosave:
      if (pSong) {
        FOREACH_PlayerNumber(pn) { GAMESTATE->cur_steps_[pn].Set(nullptr); }
        pSong->LoadAutosaveFile();
        SONGMAN->Invalidate(pSong);
        SCREENMAN->SendMessageToTopScreen(SM_RefreshSelector);
      }
      break;
    case EditMenuAction_Create:
      ASSERT(!pSteps);
      {
        pSteps = pSong->CreateSteps();

        EditMode mode = selector_.EDIT_MODE;
        switch (mode) {
          default:
            FAIL_M(ssprintf("Invalid EditMode: %i", mode));
          case EditMode_Full:
            break;
          case EditMode_Home:
            pSteps->SetLoadedFromProfile(ProfileSlot_Machine);
            break;
          case EditMode_Practice:
            FAIL_M("Cannot create steps in EditMode_Practice");
        }

        std::string sEditName;
        if (pSourceSteps) {
          pSteps->CopyFrom(pSourceSteps, st, pSong->music_length_seconds_);
          sEditName = GetCopyDescription(pSourceSteps);
        } else {
          pSteps->CreateBlank(st);
          pSteps->SetMeter(1);
          sEditName = "";
        }

        pSteps->SetDifficulty(
            dc);  // override difficulty with the user's choice
        SongUtil::MakeUniqueEditDescription(pSong, st, sEditName);
        pSteps->SetDescription(sEditName);
        pSong->AddSteps(pSteps);

        SCREENMAN->PlayStartSound();

        GAMESTATE->cur_song_.Set(pSong);
        GAMESTATE->cur_steps_[PLAYER_1].Set(pSteps);
        GAMESTATE->cur_course_.Set(nullptr);
      }
      break;
    default:
      FAIL_M(ssprintf("Invalid edit menu action: %i", action));
  }

  // Go to the next screen.
  switch (action) {
    case EditMenuAction_Edit:
    case EditMenuAction_Create:
    case EditMenuAction_Practice: {
      // Prepare for ScreenEdit
      ASSERT(pSteps != nullptr);
      bool bPromptToNameSteps =
          (action == EditMenuAction_Create && dc == Difficulty_Edit);
      if (bPromptToNameSteps) {
        ScreenTextEntry::TextEntry(
            SM_BackFromEditDescription, ENTER_EDIT_DESCRIPTION,
            GAMESTATE->cur_steps_[0]->GetDescription(),
            MAX_STEPS_DESCRIPTION_LENGTH,
            SongUtil::ValidateCurrentStepsDescription,
            SetCurrentStepsDescription, DeleteCurrentSteps);
      } else {
        SOUND->StopMusic();
        SCREENMAN->PlayStartSound();
        StartTransitioningScreen(SM_GoToNextScreen);
      }
    }
      return true;
    case EditMenuAction_Delete:
    case EditMenuAction_LoadAutosave:
      return true;
    default:
      FAIL_M(ssprintf("Invalid edit menu action: %i", action));
  }
}

bool ScreenEditMenu::MenuBack(const InputEventPlus& input) {
  Cancel(SM_GoToPrevScreen);
  return true;
}

void ScreenEditMenu::RefreshExplanationText() {
  text_explanation_.SetText(EXPLANATION_TEXT(selector_.GetSelectedRow()));
  text_explanation_.StopTweening();
  ON_COMMAND(text_explanation_);
}

void ScreenEditMenu::RefreshNumStepsLoadedFromProfile() {
  std::string s =
      ssprintf("edits used: %d", SONGMAN->GetNumStepsLoadedFromProfile());
  text_num_steps_loaded_from_profile_.SetText(s);
}

/*
 * (c) 2002-2004 Chris Danford
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
