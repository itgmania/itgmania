#include "ScreenOptionsManageProfiles.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "Command.h"
#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "GameState.h"
#include "LocalizedString.h"
#include "OptionRowHandler.h"
#include "PlayerNumber.h"
#include "Preference.h"
#include "PrefsManager.h"
#include "Profile.h"
#include "ProfileManager.h"
#include "RageUtil.h"
#include "Screen.h"
#include "ScreenDimensions.h"
#include "ScreenManager.h"
#include "ScreenMessage.h"
#include "ScreenMiniMenu.h"
#include "ScreenOptions.h"
#include "ScreenPrompt.h"
#include "ScreenTextEntry.h"
#include "global.h"

static LocalizedString NEW_PROFILE_DEFAULT_NAME(
    "ScreenOptionsManageProfiles", "NewProfileDefaultName");

#define SHOW_CREATE_NEW (!PROFILEMAN->FixedProfiles())

AutoScreenMessage(SM_BackFromEnterNameForNew);
AutoScreenMessage(SM_BackFromRename);
AutoScreenMessage(SM_BackFromDeleteConfirm);
AutoScreenMessage(SM_BackFromClearConfirm);
AutoScreenMessage(SM_BackFromContextMenu);

enum ProfileAction {
  ProfileAction_SetDefaultP1,
  ProfileAction_SetDefaultP2,
  ProfileAction_Edit,
  ProfileAction_Rename,
  ProfileAction_Delete,
  ProfileAction_Clear,
  ProfileAction_MergeToMachine,
  ProfileAction_MergeToMachineSkipTotal,
  ProfileAction_MergeToP1,
  ProfileAction_MergeToP2,
  ProfileAction_ChangeToGuest,
  ProfileAction_ChangeToNormal,
  ProfileAction_ChangeToTest,
  ProfileAction_MoveUp,
  ProfileAction_MoveDown,
  NUM_ProfileAction
};
static const char* ProfileActionNames[] = {
    "SetDefaultP1", "SetDefaultP2", "Edit",           "Rename",
    "Delete",       "Clear",        "MergeToMachine", "MergeToMachineSkipTotal",
    "MergeToP1",    "MergeToP2",    "ChangeToGuest",  "ChangeToNormal",
    "ChangeToTest", "MoveUp",       "MoveDown",
};
XToString(ProfileAction);
XToLocalizedString(ProfileAction);
static MenuDef g_TempMenu("ScreenMiniMenuContext");

static LocalizedString PROFILE_NAME_BLANK(
    "ScreenEditMenu", "Profile name cannot be blank.");
static LocalizedString PROFILE_NAME_CONFLICTS(
    "ScreenEditMenu",
    "The name you chose conflicts with another profile. Please use a different "
    "name.");
static bool ValidateLocalProfileName(
    const std::string& answer, std::string& error_out) {
  if (answer == "") {
    error_out = PROFILE_NAME_BLANK;
    return false;
  }

  Profile* profile =
      PROFILEMAN->GetLocalProfile(GAMESTATE->edit_local_profile_id_);
  if (profile != nullptr && answer == profile->display_name_) {
    return true;  // unchanged
  }

  std::vector<std::string> profile_names;
  PROFILEMAN->GetLocalProfileDisplayNames(profile_names);
  bool already_a_profile_with_this_name =
      find(profile_names.begin(), profile_names.end(), answer) !=
      profile_names.end();
  if (already_a_profile_with_this_name) {
    error_out = PROFILE_NAME_CONFLICTS;
    return false;
  }

  return true;
}

// Strip disallowed characters before they are appended to the profile name.
static const std::string kProfileNameBlockedChars = "'\"";
static bool ValidateAppendProfileName(
    const std::string& /*sAnswerBeforeChar*/, std::string& append) {
  append.erase(
      std::remove_if(
          append.begin(), append.end(),
          [](unsigned char c) {
            // Drop non-printables, non ascii, & explicitly blocked chars
            return c < 0x20 || c > 0x7E ||
                   kProfileNameBlockedChars.find(static_cast<char>(c)) !=
                       std::string::npos;
          }),
      append.end());
  // Return true even if we stripped everything; an empty append is fine
  // (ValidateAppend returning false would abort the whole input).
  return true;
}

REGISTER_SCREEN_CLASS(ScreenOptionsManageProfiles);

void ScreenOptionsManageProfiles::Init() {
  ScreenOptions::Init();

  SetNavigation(NAV_THREE_KEY_MENU);
  SetInputMode(INPUTMODE_SHARE_CURSOR);
}

void ScreenOptionsManageProfiles::BeginScreen() {
  // FIXME
  // int iIndex = 0;
  std::vector<OptionRowHandler*> option_row_handlers;

  if (SHOW_CREATE_NEW) {
    OptionRowHandler* hand = OptionRowHandlerUtil::Make(ParseCommands(
        ssprintf("gamecommand;screen,%s;name,dummy", name_.c_str())));
    OptionRowDefinition& def = hand->def_;
    def.layout_type_ = LAYOUT_SHOW_ALL_IN_ROW;
    def.allow_theme_title_ = true;
    def.allow_theme_items_ = false;
    def.name_ = "Create New Profile";
    def.explanation_name_ = "Create New Profile";
    option_row_handlers.push_back(hand);

    // FIXME
    // gc.Load( iIndex++,  );
  }

  PROFILEMAN->GetLocalProfileIDs(local_profile_id_);

  for (const std::string& s : local_profile_id_) {
    Profile* profile = PROFILEMAN->GetLocalProfile(s);
    ASSERT(profile != nullptr);

    std::string command = ssprintf(
        "gamecommand;screen,ScreenOptionsCustomizeProfile;profileid,%s;name,"
        "dummy",
        s.c_str());
    OptionRowHandler* hand = OptionRowHandlerUtil::Make(ParseCommands(command));
    OptionRowDefinition& def = hand->def_;
    def.layout_type_ = LAYOUT_SHOW_ALL_IN_ROW;
    def.allow_theme_title_ = false;
    def.allow_theme_items_ = false;
    def.name_ = profile->display_name_;
    def.explanation_name_ = "Select Profile";

    PlayerNumber pn = PLAYER_INVALID;
    FOREACH_PlayerNumber(
        p) if (s == ProfileManager::default_local_profile_id_[p].Get()) pn = p;
    if (pn != PLAYER_INVALID) {
      def.choices_.push_back(PlayerNumberToLocalizedString(pn));
    }
    option_row_handlers.push_back(hand);

    // FIXME
    // gc.Load( iIndex++,  );
  }

  ScreenOptions::InitMenu(option_row_handlers);

  // Save sEditLocalProfileID before calling ScreenOptions::BeginScreen, because
  // it will get clobbered.
  std::string edit_local_profile_id = GAMESTATE->edit_local_profile_id_;

  ScreenOptions::BeginScreen();

  // select the last chosen profile
  if (!edit_local_profile_id.empty()) {
    std::vector<std::string>::const_iterator iter = find(
        local_profile_id_.begin(), local_profile_id_.end(),
        edit_local_profile_id);
    if (iter != local_profile_id_.end()) {
      int index = iter - local_profile_id_.begin();
      this->MoveRowAbsolute(PLAYER_1, 1 + index);
    }
  } else if (!local_profile_id_.empty()) {
    // select the first item below "create new"
    this->MoveRowAbsolute(PLAYER_1, 1);
  }

  AfterChangeRow(PLAYER_1);
}

static LocalizedString CONFIRM_DELETE_PROFILE(
    "ScreenOptionsManageProfiles",
    "Are you sure you want to delete the profile '%s'?");
static LocalizedString CONFIRM_CLEAR_PROFILE(
    "ScreenOptionsManageProfiles",
    "Are you sure you want to clear all data in the profile '%s'?");
static LocalizedString ENTER_PROFILE_NAME(
    "ScreenOptionsManageProfiles", "Enter a name for the profile.");
void ScreenOptionsManageProfiles::HandleScreenMessage(const ScreenMessage sm) {
  if (sm == SM_GoToNextScreen) {
    int cur_row = current_row_[GAMESTATE->GetMasterPlayerNumber()];
    OptionRow& row = *rows_[cur_row];
    if (row.GetRowType() == OptionRow::RowType_Exit) {
      this->HandleScreenMessage(SM_GoToPrevScreen);
      return;  // don't call base
    }
  } else if (sm == SM_BackFromEnterNameForNew) {
    if (!ScreenTextEntry::cancelled_last_) {
      ASSERT(
          ScreenTextEntry::last_answer_ !=
          "");  // validate should have assured this

      std::string new_name = ScreenTextEntry::last_answer_;
      ASSERT(GAMESTATE->edit_local_profile_id_.Get().empty());

      int num_profiles = PROFILEMAN->GetNumLocalProfiles();

      // create
      std::string profile_id;

      // is this the correct way to go about checking the return value? -aj
      bool create_profile = PROFILEMAN->CreateLocalProfile(
          ScreenTextEntry::last_answer_, profile_id);
      ASSERT(create_profile);

      GAMESTATE->edit_local_profile_id_.Set(profile_id);

      if (num_profiles < NUM_PLAYERS) {
        int first_unused = -1;
        int index = 0;
        for (const Preference<std::string>* i :
             PROFILEMAN->default_local_profile_id_.v_) {
          std::string local_profile_id = i->Get();
          if (local_profile_id.empty()) {
            first_unused = index;
            break;
          }
        }
        if (first_unused != -1) {
          PROFILEMAN->default_local_profile_id_.v_[first_unused]->Set(
              profile_id);
        }
      }

      SCREENMAN->SetNewScreen(this->name_);  // reload
    }
  } else if (sm == SM_BackFromRename) {
    if (!ScreenTextEntry::cancelled_last_) {
      ASSERT(
          ScreenTextEntry::last_answer_ !=
          "");  // validate should have assured this

      std::string new_name = ScreenTextEntry::last_answer_;
      PROFILEMAN->RenameLocalProfile(
          GAMESTATE->edit_local_profile_id_, new_name);
      if (PREFSMAN->profile_sort_order_ == ProfileSortOrder_Alphabetical) {
        PROFILEMAN->MoveProfileSorted(
            GetLocalProfileIndexWithFocus(),
            PREFSMAN->profile_sort_order_ascending_);
      }
      SCREENMAN->SetNewScreen(this->name_);  // reload
    }
  } else if (sm == SM_BackFromDeleteConfirm) {
    if (ScreenPrompt::last_answer_ == ANSWER_YES) {
      // Select the profile nearest to the one that was just deleted.
      int i_index = -1;
      std::vector<std::string>::const_iterator iter = find(
          local_profile_id_.begin(), local_profile_id_.end(),
          GAMESTATE->edit_local_profile_id_.Get());
      if (iter != local_profile_id_.end()) {
        i_index = iter - local_profile_id_.begin();
      }
      rage_clamp(i_index, 0, local_profile_id_.size() - 1);
      GAMESTATE->edit_local_profile_id_.Set(local_profile_id_[i_index]);

      PROFILEMAN->DeleteLocalProfile(GetLocalProfileIDWithFocus());

      SCREENMAN->SetNewScreen(this->name_);  // reload
    }
  } else if (sm == SM_BackFromClearConfirm) {
    if (ScreenPrompt::last_answer_ == ANSWER_YES) {
      GAMESTATE->GetEditLocalProfile()->InitAll();

      SCREENMAN->SetNewScreen(this->name_);  // reload
    }
  } else if (sm == SM_BackFromContextMenu) {
    if (!ScreenMiniMenu::cancelled_) {
      Profile* profile =
          PROFILEMAN->GetLocalProfile(GAMESTATE->edit_local_profile_id_);
      ASSERT(profile != nullptr);

      switch (ScreenMiniMenu::last_row_code_) {
        default:
          FAIL_M(ssprintf(
              "Last row code not a valid ProfileAction: %i",
              ScreenMiniMenu::last_row_code_));
        case ProfileAction_SetDefaultP1:
        case ProfileAction_SetDefaultP2: {
          FOREACH_PlayerNumber(p) if (
              ProfileManager::default_local_profile_id_[p].Get() ==
              GetLocalProfileIDWithFocus())
              ProfileManager::default_local_profile_id_[p]
                  .Set("");

          PlayerNumber pn = (PlayerNumber)(ScreenMiniMenu::last_row_code_ -
                                           ProfileAction_SetDefaultP1);
          ProfileManager::default_local_profile_id_[pn].Set(
              GetLocalProfileIDWithFocus());

          SCREENMAN->SetNewScreen(this->name_);  // reload
        } break;
        case ProfileAction_Edit: {
          GAMESTATE->edit_local_profile_id_.Set(GetLocalProfileIDWithFocus());

          ScreenOptions::BeginFadingOut();
        } break;
        case ProfileAction_Rename: {
          ScreenTextEntry::TextEntry(
              SM_BackFromRename, ENTER_PROFILE_NAME, profile->display_name_,
              PROFILE_MAX_DISPLAY_NAME_LENGTH, ValidateLocalProfileName,
              nullptr, nullptr, false, ValidateAppendProfileName);
        } break;
        case ProfileAction_Delete: {
          std::string title = profile->display_name_;
          std::string message = ssprintf(
              CONFIRM_DELETE_PROFILE.GetValue().c_str(), title.c_str());
          ScreenPrompt::Prompt(
              SM_BackFromDeleteConfirm, message, PROMPT_YES_NO);
        } break;
        case ProfileAction_Clear: {
          std::string title = profile->display_name_;
          std::string message =
              ssprintf(CONFIRM_CLEAR_PROFILE.GetValue().c_str(), title.c_str());
          ScreenPrompt::Prompt(SM_BackFromClearConfirm, message, PROMPT_YES_NO);
        } break;
        case ProfileAction_MergeToMachine:
          PROFILEMAN->MergeLocalProfileIntoMachine(
              GetLocalProfileIDWithFocus(), false);
          break;
        case ProfileAction_MergeToMachineSkipTotal:
          PROFILEMAN->MergeLocalProfileIntoMachine(
              GetLocalProfileIDWithFocus(), true);
          break;
        case ProfileAction_MergeToP1:
          PROFILEMAN->MergeLocalProfiles(
              GetLocalProfileIDWithFocus(),
              ProfileManager::default_local_profile_id_[PLAYER_1].Get());
          break;
        case ProfileAction_MergeToP2:
          PROFILEMAN->MergeLocalProfiles(
              GetLocalProfileIDWithFocus(),
              ProfileManager::default_local_profile_id_[PLAYER_2].Get());
          break;
        case ProfileAction_ChangeToGuest:
          PROFILEMAN->ChangeProfileType(
              GetLocalProfileIndexWithFocus(), ProfileType_Guest);
          SCREENMAN->SetNewScreen(this->name_);  // reload
          break;
        case ProfileAction_ChangeToNormal:
          PROFILEMAN->ChangeProfileType(
              GetLocalProfileIndexWithFocus(), ProfileType_Normal);
          SCREENMAN->SetNewScreen(this->name_);  // reload
          break;
        case ProfileAction_ChangeToTest:
          PROFILEMAN->ChangeProfileType(
              GetLocalProfileIndexWithFocus(), ProfileType_Test);
          SCREENMAN->SetNewScreen(this->name_);  // reload
          break;
        case ProfileAction_MoveUp:
          PROFILEMAN->MoveProfilePriority(
              GetLocalProfileIndexWithFocus(), true);
          SCREENMAN->SetNewScreen(this->name_);  // reload
          break;
        case ProfileAction_MoveDown:
          PROFILEMAN->MoveProfilePriority(
              GetLocalProfileIndexWithFocus(), false);
          SCREENMAN->SetNewScreen(this->name_);  // reload
          break;
      }
    }
  } else if (sm == SM_LoseFocus) {
    this->PlayCommand("ScreenLoseFocus");
  } else if (sm == SM_GainFocus) {
    this->PlayCommand("ScreenGainFocus");
  }

  ScreenOptions::HandleScreenMessage(sm);
}

void ScreenOptionsManageProfiles::AfterChangeRow(PlayerNumber pn) {
  GAMESTATE->edit_local_profile_id_.Set(GetLocalProfileIDWithFocus());

  ScreenOptions::AfterChangeRow(pn);
}

void ScreenOptionsManageProfiles::ProcessMenuStart(const InputEventPlus&) {
  if (IsTransitioning()) {
    return;
  }

  int cur_row = current_row_[GAMESTATE->GetMasterPlayerNumber()];
  OptionRow& row = *rows_[cur_row];

  if (SHOW_CREATE_NEW && cur_row == 0)  // "create new"
  {
    std::vector<std::string> used_names;
    PROFILEMAN->GetLocalProfileDisplayNames(used_names);

    std::string potential_name;
    for (int i = 1; i < 1000; i++) {
      potential_name =
          ssprintf("%s%04d", NEW_PROFILE_DEFAULT_NAME.GetValue().c_str(), i);
      bool name_is_used =
          find(used_names.begin(), used_names.end(), potential_name) !=
          used_names.end();
      if (!name_is_used) {
        break;
      }
    }
    ScreenTextEntry::TextEntry(
        SM_BackFromEnterNameForNew, ENTER_PROFILE_NAME, potential_name,
        PROFILE_MAX_DISPLAY_NAME_LENGTH, ValidateLocalProfileName, nullptr,
        nullptr, false, ValidateAppendProfileName);
  } else if (row.GetRowType() == OptionRow::RowType_Exit) {
    SCREENMAN->PlayStartSound();
    this->BeginFadingOut();
  } else  // a profile
  {
    g_TempMenu.rows_.clear();

#define ADD_ACTION(i)                                                          \
  g_TempMenu.rows_.push_back(MenuRowDef(                                       \
      i, ProfileActionToLocalizedString(i), true, EditMode_Home, false, false, \
      0, ""));

    ADD_ACTION(ProfileAction_SetDefaultP1);
    ADD_ACTION(ProfileAction_SetDefaultP2);
    if (PROFILEMAN->FixedProfiles()) {
      ADD_ACTION(ProfileAction_Rename);
      ADD_ACTION(ProfileAction_Clear);
    } else {
      ADD_ACTION(ProfileAction_Edit);
      ADD_ACTION(ProfileAction_Rename);
      ADD_ACTION(ProfileAction_Delete);
      ADD_ACTION(ProfileAction_MergeToMachine);
      ADD_ACTION(ProfileAction_MergeToMachineSkipTotal);
      ADD_ACTION(ProfileAction_MergeToP1);
      ADD_ACTION(ProfileAction_MergeToP2);
      ADD_ACTION(ProfileAction_ChangeToGuest);
      ADD_ACTION(ProfileAction_ChangeToNormal);
      ADD_ACTION(ProfileAction_ChangeToTest);
      // There technically isn't an issue keeping these enabled for other
      // ProfileSorts, but it's not really useful as it would be overwritten.
      if (PREFSMAN->profile_sort_order_ == ProfileSortOrder_Priority) {
        ADD_ACTION(ProfileAction_MoveUp);
        ADD_ACTION(ProfileAction_MoveDown);
      }
    }

    int width, x, y;
    this->GetWidthXY(PLAYER_1, cur_row, 0, width, x, y);

    ScreenMiniMenu::MiniMenu(
        &g_TempMenu, SM_BackFromContextMenu, SM_BackFromContextMenu, (float)x,
        SCREEN_CENTER_Y);
  }
}

void ScreenOptionsManageProfiles::ImportOptions(
    int /* iRow */, const std::vector<PlayerNumber>& /* vpns */) {}

void ScreenOptionsManageProfiles::ExportOptions(
    int /* iRow */, const std::vector<PlayerNumber>& /* vpns */) {}

int ScreenOptionsManageProfiles::GetLocalProfileIndexWithFocus() const {
  int cur_row = current_row_[GAMESTATE->GetMasterPlayerNumber()];
  OptionRow& row = *rows_[cur_row];

  if (SHOW_CREATE_NEW && cur_row == 0) {  // "create new"
    return -1;
  } else if (row.GetRowType() == OptionRow::RowType_Exit) {
    return -1;
  }

  // a profile
  int index = cur_row + (SHOW_CREATE_NEW ? -1 : 0);
  return index;
}

std::string ScreenOptionsManageProfiles::GetLocalProfileIDWithFocus() const {
  int index = GetLocalProfileIndexWithFocus();
  if (index == -1) {
    return std::string();
  }
  return local_profile_id_[index];
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
