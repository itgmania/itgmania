#include "ScreenOptionsToggleSongs.h"

#include <string>
#include <vector>

#include "OptionRowHandler.h"
#include "PlayerNumber.h"
#include "PrefsManager.h"
#include "Screen.h"
#include "ScreenManager.h"
#include "ScreenOptions.h"
#include "Song.h"
#include "SongManager.h"
#include "UnlockManager.h"

// main page (group list)
REGISTER_SCREEN_CLASS(ScreenOptionsToggleSongs);

void ScreenOptionsToggleSongs::BeginScreen() {
  groups_.clear();

  std::vector<OptionRowHandler*> vHands;

  std::vector<std::string> asAllGroups;
  SONGMAN->GetSongGroupNames(asAllGroups);
  for (const std::string& sGroup : asAllGroups) {
    vHands.push_back(OptionRowHandlerUtil::MakeNull());
    OptionRowDefinition& def = vHands.back()->def_;

    def.name_ = sGroup;
    def.explanation_name_ = "Select Group";
    def.allow_theme_title_ = false;  // not themable
    def.allow_theme_items_ = false;  // already themed
    def.one_choice_for_all_players_ = true;
    def.choices_.clear();
    def.choices_.push_back("");

    groups_.push_back(sGroup);
  }
  ScreenOptions::InitMenu(vHands);

  ScreenOptions::BeginScreen();
}

void ScreenOptionsToggleSongs::ProcessMenuStart(const InputEventPlus& input) {
  if (IsTransitioning()) {
    return;
  }

  // switch to the subpage with the specified group
  int iRow = GetCurrentRow();
  if (rows_[iRow]->GetRowType() == OptionRow::RowType_Exit) {
    ScreenOptions::ProcessMenuStart(input);
    return;
  }

  ToggleSongs::m_sGroup = groups_[iRow];
  SCREENMAN->SetNewScreen("ScreenOptionsToggleSongsSubPage");
}

void ScreenOptionsToggleSongs::ImportOptions(
    int row, const std::vector<PlayerNumber>& vpns) {}
void ScreenOptionsToggleSongs::ExportOptions(
    int row, const std::vector<PlayerNumber>& vpns) {}

// subpage (has the songs in a specific group)
REGISTER_SCREEN_CLASS(ScreenOptionsToggleSongsSubPage);
void ScreenOptionsToggleSongsSubPage::BeginScreen() {
  songs_.clear();

  std::vector<OptionRowHandler*> vHands;

  const std::vector<Song*>& apAllSongs =
      SONGMAN->GetSongs(ToggleSongs::m_sGroup);
  for (Song* pSong : apAllSongs) {
    if (UNLOCKMAN->SongIsLocked(pSong) & ~LOCKED_DISABLED) {
      continue;
    }

    vHands.push_back(OptionRowHandlerUtil::MakeNull());
    OptionRowDefinition& def = vHands.back()->def_;

    def.name_ = pSong->GetTranslitFullTitle();
    def.allow_theme_title_ = false;  // not themable
    def.explanation_name_ = "Toggle Song";
    def.one_choice_for_all_players_ = true;
    def.choices_.clear();
    def.choices_.push_back("On");
    def.choices_.push_back("Off");
    def.allow_theme_items_ = false;  // already themed

    songs_.push_back(pSong);
  }

  InitMenu(vHands);

  ScreenOptions::BeginScreen();
}

void ScreenOptionsToggleSongsSubPage::ImportOptions(
    int iRow, const std::vector<PlayerNumber>& vpns) {
  if (iRow >= (int)songs_.size()) {  // exit row
    return;
  }

  OptionRow& row = *rows_[iRow];
  bool bEnable = songs_[iRow]->GetEnabled();
  int iSelection = bEnable ? 0 : 1;
  row.SetOneSharedSelection(iSelection);
}

void ScreenOptionsToggleSongsSubPage::ExportOptions(
    int iRow, const std::vector<PlayerNumber>& vpns) {
  if (iRow >= (int)songs_.size()) {  // exit row
    return;
  }

  const OptionRow& row = *rows_[iRow];
  int iSelection = row.GetOneSharedSelection();
  bool bEnable = (iSelection == 0);
  songs_[iRow]->SetEnabled(bEnable);

  SONGMAN->SaveEnabledSongsToPref();
  PREFSMAN->SavePrefsToDisk();
}

/*
 * (c) 2007 Glenn Maynard
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
