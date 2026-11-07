#include "ScreenOptionsEditProfile.h"

#include <string>
#include <vector>

#include "Character.h"
#include "CharacterManager.h"
#include "GameState.h"
#include "OptionRowHandler.h"
#include "PlayerNumber.h"
#include "Profile.h"
#include "ProfileManager.h"
#include "Screen.h"
#include "ScreenManager.h"
#include "ScreenMessage.h"
#include "ScreenOptions.h"
#include "global.h"

enum EditProfileRow {
  ROW_CHARACTER,
};

REGISTER_SCREEN_CLASS(ScreenOptionsEditProfile);

void ScreenOptionsEditProfile::Init() { ScreenOptions::Init(); }

void ScreenOptionsEditProfile::BeginScreen() {
  original_ = *GAMESTATE->GetEditLocalProfile();

  std::vector<OptionRowHandler*> hands;

  Profile* profile =
      PROFILEMAN->GetLocalProfile(GAMESTATE->edit_local_profile_id_);
  ASSERT(profile != nullptr);

  {
    hands.push_back(OptionRowHandlerUtil::MakeNull());
    OptionRowDefinition& def = hands.back()->def_;
    def.layout_type_ = LAYOUT_SHOW_ONE_IN_ROW;
    def.one_choice_for_all_players_ = true;
    def.allow_theme_items_ = false;
    def.allow_theme_title_ = false;
    def.allow_explanation_ = false;
    def.export_on_change_ = true;
    def.name_ = "Character";
    def.choices_.clear();
    std::vector<Character*> characters;
    CHARMAN->GetCharacters(characters);
    for (const Character* c : characters) {
      def.choices_.push_back(c->GetDisplayName());
    }
    if (def.choices_.empty()) {
      def.choices_.push_back(std::string());
    }
  }

  InitMenu(hands);

  ScreenOptions::BeginScreen();
}

ScreenOptionsEditProfile::~ScreenOptionsEditProfile() {}

void ScreenOptionsEditProfile::ImportOptions(
    int i_row, const std::vector<PlayerNumber>& vpns) {
  Profile* profile =
      PROFILEMAN->GetLocalProfile(GAMESTATE->edit_local_profile_id_);
  ASSERT(profile != nullptr);
  OptionRow& row = *rows_[i_row];

  switch (i_row) {
    case ROW_CHARACTER:
      row.SetOneSharedSelectionIfPresent(profile->character_id_);
      break;
  }
}

void ScreenOptionsEditProfile::ExportOptions(
    int i_row, const std::vector<PlayerNumber>& vpns) {
  Profile* profile =
      PROFILEMAN->GetLocalProfile(GAMESTATE->edit_local_profile_id_);
  ASSERT(profile != nullptr);
  OptionRow& row = *rows_[i_row];
  int index = row.GetOneSharedSelection(true);
  std::string value;
  if (index >= 0) {
    value = row.GetRowDef().choices_[index];
  }

  switch (i_row) {
    case ROW_CHARACTER:
      profile->character_id_ = value;
      break;
  }
}

void ScreenOptionsEditProfile::HandleScreenMessage(const ScreenMessage sm) {
  if (sm == SM_GoToNextScreen) {
    PROFILEMAN->SaveLocalProfile(GAMESTATE->edit_local_profile_id_);
  } else if (sm == SM_GoToPrevScreen) {
    *GAMESTATE->GetEditLocalProfile() = original_;
  }

  ScreenOptions::HandleScreenMessage(sm);
}

void ScreenOptionsEditProfile::AfterChangeValueInRow(int row, PlayerNumber pn) {
  ScreenOptions::AfterChangeValueInRow(row, pn);

  // cause the overlay to reload
  GAMESTATE->edit_local_profile_id_.Set(GAMESTATE->edit_local_profile_id_);
}

void ScreenOptionsEditProfile::ProcessMenuStart(const InputEventPlus& input) {
  if (IsTransitioning()) {
    return;
  }

  int row = GetCurrentRow();
  // OptionRow &row = *m_pRows[iRow];

  switch (row) {
    case ROW_CHARACTER: {
    } break;
    default:
      ScreenOptions::ProcessMenuStart(input);
      break;
  }
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
