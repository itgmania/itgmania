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

  std::vector<OptionRowHandler*> OptionRowHandlers;
  for (const std::string& s : package_types_) {
    OptionRowHandler* pHand = OptionRowHandlerUtil::MakeNull();
    OptionRowDefinition& def = pHand->def_;

    def.name_ = s;
    def.allow_explanation_ = false;
    // def.m_sExplanationName = "# files, # MB, # subdirs";
    def.allow_theme_title_ = false;
    def.allow_theme_items_ = false;
    def.layout_type_ = LAYOUT_SHOW_ALL_IN_ROW;
    def.one_choice_for_all_players_ = true;
    def.choices_.clear();
    def.choices_.push_back("");
    OptionRowHandlers.push_back(pHand);
  }
  ScreenOptions::InitMenu(OptionRowHandlers);

  ScreenOptions::BeginScreen();
}

void ScreenOptionsExportPackage::ProcessMenuStart(const InputEventPlus& input) {
  if (IsTransitioning()) {
    return;
  }

  // switch to the subpage with the specified type
  // int iCurRow = m_iCurrentRow[GAMESTATE->GetMasterPlayerNumber()];
  int iRow = GetCurrentRow();
  if (rows_[iRow]->GetRowType() == OptionRow::RowType_Exit) {
    ScreenOptions::ProcessMenuStart(input);
    return;
  }

  ExportPackages::m_sPackageType = package_types_[iRow];

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
  const std::string* s_packageType = &ExportPackages::m_sPackageType;
  if (*s_packageType == "Themes") {
    // add themes
    GetDirListing(
        SpecialFiles::THEMES_DIR + "*", possible_dirs_to_export_, true, true);
  } else if (*s_packageType == "NoteSkins") {
    // add noteskins
    std::vector<std::string> vs;
    GetDirListing(SpecialFiles::NOTESKINS_DIR + "*", vs, true, true);
    for (const std::string& s : vs) {
      GetDirListing(s + "*", possible_dirs_to_export_, true, true);
    }
  } else if (*s_packageType == "Courses") {
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
  } else if (*s_packageType == "Songs") {
    // Add song groups
    std::vector<std::string> asAllGroups;
    SONGMAN->GetSongGroupNames(asAllGroups);
    for (const std::string& s : asAllGroups) {
      possible_dirs_to_export_.push_back(s);
    }
  } else if (*s_packageType == "SubGroup") {
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

  std::vector<OptionRowHandler*> OptionRowHandlers;
  for (const std::string& s : possible_dirs_to_export_) {
    OptionRowHandler* pHand = OptionRowHandlerUtil::MakeNull();
    OptionRowDefinition& def = pHand->def_;
    def.layout_type_ = LAYOUT_SHOW_ALL_IN_ROW;
    def.allow_theme_title_ = false;
    def.allow_theme_items_ = false;
    def.allow_explanation_ = false;
    def.name_ = s;
    def.explanation_name_ = "# files, # MB, # subdirs";

    def.choices_.push_back("");
    OptionRowHandlers.push_back(pHand);
  }
  ScreenOptions::InitMenu(OptionRowHandlers);

  ScreenOptions::BeginScreen();
}

static std::string ReplaceInvalidFileNameChars(std::string sOldFileName) {
  std::string sNewFileName = sOldFileName;
  const char charsToReplace[] = {' ', '!',  '@',  '#', '$', '%', '^', '&', '*',
                                 '(', ')',  '+',  '=', '[', ']', '{', '}', '|',
                                 ':', '\"', '\\', '<', '>', ',', '?', '/'};
  for (unsigned i = 0; i < sizeof(charsToReplace); i++) {
    Replace(sNewFileName, charsToReplace[i], '_');
  }
  return sNewFileName;
}

static bool ExportPackage(
    std::string sPackageName, std::string sDirToExport,
    std::string& sErrorOut) {
  // Mount Desktop/ for each OS.
  std::string sDesktopDir = SpecialDirs::GetDesktopDir();
  std::string fn = sDesktopDir + sPackageName;
  RageFile f;
  if (!f.Open(fn, RageFile::WRITE)) {
    sErrorOut = ssprintf(
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

  int iCurRow = current_row_[GAMESTATE->GetMasterPlayerNumber()];
  if (rows_[iCurRow]->GetRowType() == OptionRow::RowType_Exit) {
    ScreenOptions::ProcessMenuStart(input);
    return;
  }

  if (ExportPackages::m_sPackageType == "Courses" ||
      ExportPackages::m_sPackageType == "NoteSkins" ||
      ExportPackages::m_sPackageType == "Songs") {
    // find folder name
    ExportPackages::m_sPackageType = "SubGroup";
    ExportPackages::m_sFolder = possible_dirs_to_export_[iCurRow];
    SCREENMAN->SetNewScreen("ScreenOptionsExportPackageSubPage");
    return;
  }

  std::string sDirToExport = possible_dirs_to_export_[iCurRow];
  std::string sPackageName =
      ReplaceInvalidFileNameChars(sDirToExport + ".smzip");

  std::string sError;
  if (ExportPackage(sPackageName, sDirToExport, sError)) {
    ScreenPrompt::Prompt(
        SM_None,
        ssprintf("Exported '%s' to the desktop", sDirToExport.c_str()));
  } else {
    ScreenPrompt::Prompt(
        SM_None, ssprintf("Failed to export package: %s", sError.c_str()));
  }
}

void ScreenOptionsExportPackageSubPage::ImportOptions(
    int iRow, const std::vector<PlayerNumber>& vpns) {}

void ScreenOptionsExportPackageSubPage::ExportOptions(
    int iRow, const std::vector<PlayerNumber>& vpns) {}

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
