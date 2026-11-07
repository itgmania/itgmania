#include "ScreenOptionsExportPackage.h"

#include <string>
#include <vector>

#include "GameState.h"
#include "OptionRowHandler.h"
#include "PlayerNumber.h"
#include "RageFile.h"
#include "RageUtil.h"
#include "Screen.h"
#include "ScreenManager.h"
#include "ScreenMessage.h"
#include "ScreenOptions.h"
#include "ScreenPrompt.h"
#include "ScreenWithMenuElements.h"
#include "SongManager.h"
#include "SpecialFiles.h"
#include "StdString.h"
#include "archutils/SpecialDirs.h"

// main page (type list)
REGISTER_SCREEN_CLASS(ScreenOptionsExportPackage);

void ScreenOptionsExportPackage::Init() {
  ScreenOptions::Init();

  SetNavigation(NAV_THREE_KEY_MENU);
  SetInputMode(INPUTMODE_SHARE_CURSOR);
}

void ScreenOptionsExportPackage::BeginScreen() {
  // Fill m_vsPackageTypes:
  package_types_.push_back("Themes");
  package_types_.push_back("NoteSkins");
  package_types_.push_back("Courses");
  package_types_.push_back("Songs");
  // announcers, characters, others?

  std::vector<OptionRowHandler*> option_row_handlers;
  for (const std::string& s : package_types_) {
    OptionRowHandler* hand = OptionRowHandlerUtil::MakeNull();
    OptionRowDefinition& def = hand->def_;

    def.name_ = s;
    def.allow_explanation_ = false;
    // def.m_sExplanationName = "# files, # MB, # subdirs";
    def.allow_theme_title_ = false;
    def.allow_theme_items_ = false;
    def.layout_type_ = LAYOUT_SHOW_ALL_IN_ROW;
    def.one_choice_for_all_players_ = true;
    def.choices_.clear();
    def.choices_.push_back("");
    option_row_handlers.push_back(hand);
  }
  ScreenOptions::InitMenu(option_row_handlers);

  ScreenOptions::BeginScreen();
}

void ScreenOptionsExportPackage::ProcessMenuStart(const InputEventPlus& input) {
  if (IsTransitioning()) {
    return;
  }

  // switch to the subpage with the specified type
  // int iCurRow = m_iCurrentRow[GAMESTATE->GetMasterPlayerNumber()];
  int row = GetCurrentRow();
  if (rows_[row]->GetRowType() == OptionRow::RowType_Exit) {
    ScreenOptions::ProcessMenuStart(input);
    return;
  }

  ExportPackages::m_sPackageType = package_types_[row];

  SCREENMAN->PlayStartSound();
  this->BeginFadingOut();
  // todo: find a way to make this transition not be instant.
  SCREENMAN->SetNewScreen("ScreenOptionsExportPackageSubPage");
}

// todo: process menu back in SubGroup mode

void ScreenOptionsExportPackage::ImportOptions(
    int /* iRow */, const std::vector<PlayerNumber>& /* vpns */) {}

void ScreenOptionsExportPackage::ExportOptions(
    int /* iRow */, const std::vector<PlayerNumber>& /* vpns */) {}

// subpage (has all folders for the specified type)
REGISTER_SCREEN_CLASS(ScreenOptionsExportPackageSubPage);
void ScreenOptionsExportPackageSubPage::Init() {
  ScreenOptions::Init();

  SetNavigation(NAV_THREE_KEY_MENU);
  SetInputMode(INPUTMODE_SHARE_CURSOR);
}

void ScreenOptionsExportPackageSubPage::BeginScreen() {
  ScreenWithMenuElements::BeginScreen();

  // Check type and fill m_vsPossibleDirsToExport
  const std::string* package_type = &ExportPackages::m_sPackageType;
  if (*package_type == "Themes") {
    // add themes
    GetDirListing(
        SpecialFiles::THEMES_DIR + "*", possible_dirs_to_export_, true, true);
  } else if (*package_type == "NoteSkins") {
    // add noteskins
    std::vector<std::string> vs;
    GetDirListing(SpecialFiles::NOTESKINS_DIR + "*", vs, true, true);
    for (const std::string& s : vs) {
      GetDirListing(s + "*", possible_dirs_to_export_, true, true);
    }
  } else if (*package_type == "Courses") {
    // Add courses. Only support courses that are in a group folder.
    // Support for courses not in a group folder should be phased out.
    std::vector<std::string> vs;
    GetDirListing(SpecialFiles::COURSES_DIR + "*", vs, true, true);
    StripCvsAndSvn(vs);
    StripMacResourceForks(vs);
    for (const std::string& s : vs) {
      possible_dirs_to_export_.push_back(s);
      GetDirListing(s + "/*", possible_dirs_to_export_, true, true);
    }
  } else if (*package_type == "Songs") {
    // Add song groups
    std::vector<std::string> all_groups;
    SONGMAN->GetSongGroupNames(all_groups);
    for (const std::string& s : all_groups) {
      possible_dirs_to_export_.push_back(s);
    }
  } else if (*package_type == "SubGroup") {
    // ExportPackages::m_sFolder
    std::vector<std::string> vs;
    GetDirListing(
        SpecialFiles::SONGS_DIR + "/" + ExportPackages::m_sFolder + "/*", vs,
        true, true);
    for (const std::string& s : vs) {
      possible_dirs_to_export_.push_back(s);
      GetDirListing(s + "/*", possible_dirs_to_export_, true, true);
    }
  }
  StripCvsAndSvn(possible_dirs_to_export_);
  StripMacResourceForks(possible_dirs_to_export_);

  std::vector<OptionRowHandler*> option_row_handlers;
  for (const std::string& s : possible_dirs_to_export_) {
    OptionRowHandler* hand = OptionRowHandlerUtil::MakeNull();
    OptionRowDefinition& def = hand->def_;
    def.layout_type_ = LAYOUT_SHOW_ALL_IN_ROW;
    def.allow_theme_title_ = false;
    def.allow_theme_items_ = false;
    def.allow_explanation_ = false;
    def.name_ = s;
    def.explanation_name_ = "# files, # MB, # subdirs";

    def.choices_.push_back("");
    option_row_handlers.push_back(hand);
  }
  ScreenOptions::InitMenu(option_row_handlers);

  ScreenOptions::BeginScreen();
}

static std::string ReplaceInvalidFileNameChars(std::string old_file_name) {
  std::string new_file_name = old_file_name;
  const char chars_to_replace[] = {
      ' ', '!', '@', '#', '$', '%', '^',  '&',  '*', '(', ')', '+', '=',
      '[', ']', '{', '}', '|', ':', '\"', '\\', '<', '>', ',', '?', '/'};
  for (unsigned i = 0; i < sizeof(chars_to_replace); i++) {
    Replace(new_file_name, chars_to_replace[i], '_');
  }
  return new_file_name;
}

static bool ExportPackage(
    std::string package_name, std::string dir_to_export,
    std::string& error_out) {
  // Mount Desktop/ for each OS.
  std::string desktop_dir = SpecialDirs::GetDesktopDir();
  std::string fn = desktop_dir + package_name;
  RageFile f;
  if (!f.Open(fn, RageFile::WRITE)) {
    error_out = ssprintf(
        "Couldn't open %s for writing: %s", fn.c_str(), f.GetError().c_str());
    return false;
  }

  return false;
}

void ScreenOptionsExportPackageSubPage::ProcessMenuStart(
    const InputEventPlus& input) {
  if (IsTransitioning()) {
    return;
  }

  int cur_row = current_row_[GAMESTATE->GetMasterPlayerNumber()];
  if (rows_[cur_row]->GetRowType() == OptionRow::RowType_Exit) {
    ScreenOptions::ProcessMenuStart(input);
    return;
  }

  if (ExportPackages::m_sPackageType == "Courses" ||
      ExportPackages::m_sPackageType == "NoteSkins" ||
      ExportPackages::m_sPackageType == "Songs") {
    // find folder name
    ExportPackages::m_sPackageType = "SubGroup";
    ExportPackages::m_sFolder = possible_dirs_to_export_[cur_row];
    SCREENMAN->SetNewScreen("ScreenOptionsExportPackageSubPage");
    return;
  }

  std::string dir_to_export = possible_dirs_to_export_[cur_row];
  std::string package_name =
      ReplaceInvalidFileNameChars(dir_to_export + ".smzip");

  std::string error;
  if (ExportPackage(package_name, dir_to_export, error)) {
    ScreenPrompt::Prompt(
        SM_None,
        ssprintf("Exported '%s' to the desktop", dir_to_export.c_str()));
  } else {
    ScreenPrompt::Prompt(
        SM_None, ssprintf("Failed to export package: %s", error.c_str()));
  }
}

void ScreenOptionsExportPackageSubPage::ImportOptions(
    int row, const std::vector<PlayerNumber>& vpns) {}

void ScreenOptionsExportPackageSubPage::ExportOptions(
    int row, const std::vector<PlayerNumber>& vpns) {}

/*
 * (c) 2002-2014 Chris Danford, AJ Kelly, Renaud Lepage
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
