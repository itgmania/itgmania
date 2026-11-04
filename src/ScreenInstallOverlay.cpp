#include "ScreenInstallOverlay.h"

#include <algorithm>

#include "GameConstantsAndTypes.h"
#include "PlayerNumber.h"
#include "RageFileManager.h"
#include "RageUtil.h"
#include "Screen.h"
#include "ScreenManager.h"
#include "StdString.h"
#include "ThemeManager.h"
#include "global.h"
class Song;
#include <string>
#include <vector>

#include "ActorUtil.h"
#include "CommandLineActions.h"
#include "GameManager.h"
#include "GameState.h"
#include "SongManager.h"
#include "StepMania.h"

struct PlayAfterLaunchInfo {
  std::string song_dir_;
  std::string theme_;
  bool any_song_changed_;
  bool any_theme_changed_;

  PlayAfterLaunchInfo() {
    any_song_changed_ = false;
    any_theme_changed_ = false;
  }

  void OverlayWith(const PlayAfterLaunchInfo& other) {
    if (!other.song_dir_.empty()) {
      song_dir_ = other.song_dir_;
    }
    if (!other.theme_.empty()) {
      theme_ = other.theme_;
    }
    any_song_changed_ |= other.any_song_changed_;
    any_theme_changed_ |= other.any_theme_changed_;
  }
};

void InstallSmzipOsArg(const std::string& sOsZipFile, PlayAfterLaunchInfo& out);
PlayAfterLaunchInfo DoInstalls(CommandLineActions::CommandLineArgs args);

static void Parse(const std::string& sDir, PlayAfterLaunchInfo& out) {
  std::vector<std::string> vsDirParts;
  split(sDir, "/", vsDirParts, true);
  if (vsDirParts.size() == 3 && EqualsNoCase(vsDirParts[0], "Songs")) {
    out.song_dir_ = "/" + sDir;
  } else if (vsDirParts.size() == 2 && EqualsNoCase(vsDirParts[0], "Themes")) {
    out.theme_ = vsDirParts[1];
  }
}

static const std::string TEMP_ZIP_MOUNT_POINT = "/@temp-zip/";
const std::string TEMP_OS_MOUNT_POINT = "/@temp-os/";

static void InstallSmzip(
    const std::string& sZipFile, PlayAfterLaunchInfo& out) {
  if (!FILEMAN->Mount("zip", sZipFile, TEMP_ZIP_MOUNT_POINT)) {
    FAIL_M("Failed to mount " + sZipFile);
  }

  std::vector<std::string> vsFiles;
  {
    std::vector<std::string> vsRawFiles;
    GetDirListingRecursive(TEMP_ZIP_MOUNT_POINT, "*", vsRawFiles);

    std::vector<std::string> vsPrettyFiles;
    for (const std::string& s : vsRawFiles) {
      if (EqualsNoCase(GetExtension(s), "ctl")) {
        continue;
      }

      vsFiles.push_back(s);

      std::string s2 = Right(s, s.length() - TEMP_ZIP_MOUNT_POINT.length());
      vsPrettyFiles.push_back(s2);
    }
    sort(vsPrettyFiles.begin(), vsPrettyFiles.end());
  }

  std::string sResult = "Success installing " + sZipFile;
  for (std::string& tmpFile : vsFiles) {
    std::string sDestFile = tmpFile;
    sDestFile =
        Right(sDestFile, sDestFile.length() - TEMP_ZIP_MOUNT_POINT.length());

    std::string sDir, sThrowAway;
    splitpath(sDestFile, sDir, sThrowAway, sThrowAway);

    Parse(sDir, out);
    out.any_song_changed_ = true;

    FILEMAN->CreateDir(sDir);

    if (!FileCopy(tmpFile, sDestFile)) {
      sResult = "Error extracting " + sDestFile;
      break;
    }
  }
  FILEMAN->Unmount("zip", sZipFile, TEMP_ZIP_MOUNT_POINT);

  SCREENMAN->SystemMessage(sResult);
}

void InstallSmzipOsArg(
    const std::string& sOsZipFile, PlayAfterLaunchInfo& out) {
  SCREENMAN->SystemMessage("Installing " + sOsZipFile);

  std::string sOsDir, sFilename, sExt;
  splitpath(sOsZipFile, sOsDir, sFilename, sExt);

  if (!FILEMAN->Mount("dir", sOsDir, TEMP_OS_MOUNT_POINT)) {
    FAIL_M("Failed to mount " + sOsDir);
  }
  InstallSmzip(TEMP_OS_MOUNT_POINT + sFilename + sExt, out);

  FILEMAN->Unmount("dir", sOsDir, TEMP_OS_MOUNT_POINT);
}

static bool IsPackageFile(const std::string& arg) {
  std::string ext = GetExtension(arg);
  return EqualsNoCase(ext, "smzip") || EqualsNoCase(ext, "zip");
}

PlayAfterLaunchInfo DoInstalls(CommandLineActions::CommandLineArgs args) {
  PlayAfterLaunchInfo ret;
  for (int i = 0; i < (int)args.argv_.size(); i++) {
    std::string s = args.argv_[i];
    if (IsPackageFile(s)) {
      InstallSmzipOsArg(s, ret);
    }
  }
  return ret;
}

REGISTER_SCREEN_CLASS(ScreenInstallOverlay);

ScreenInstallOverlay::~ScreenInstallOverlay() {}
void ScreenInstallOverlay::Init() {
  Screen::Init();

  text_status_.LoadFromFont(THEME->GetPathF("ScreenInstallOverlay", "status"));
  text_status_.SetName("Status");
  ActorUtil::LoadAllCommandsAndSetXY(text_status_, "ScreenInstallOverlay");
  this->AddChild(&text_status_);
}

bool ScreenInstallOverlay::Input(const InputEventPlus& input) {
  return Screen::Input(input);
}

void ScreenInstallOverlay::Update(float fDeltaTime) {
  Screen::Update(fDeltaTime);
  PlayAfterLaunchInfo playAfterLaunchInfo;
  while (CommandLineActions::ToProcess.size() > 0) {
    CommandLineActions::CommandLineArgs args =
        CommandLineActions::ToProcess.back();
    CommandLineActions::ToProcess.pop_back();
    PlayAfterLaunchInfo pali2 = DoInstalls(args);
    playAfterLaunchInfo.OverlayWith(pali2);
  }
  if (playAfterLaunchInfo.any_song_changed_) {
    SONGMAN->Reload(false);
  }

  if (!playAfterLaunchInfo.song_dir_.empty()) {
    Song* pSong = nullptr;
    GAMESTATE->Reset();
    std::string sInitialScreen;
    if (playAfterLaunchInfo.song_dir_.length() > 0) {
      pSong = SONGMAN->GetSongFromDir(playAfterLaunchInfo.song_dir_);
    }
    if (pSong) {
      std::vector<const Style*> vpStyle;
      GAMEMAN->GetStylesForGame(GAMESTATE->cur_game_, vpStyle, false);
      GAMESTATE->play_mode_.Set(PLAY_MODE_REGULAR);
      GAMESTATE->side_is_joined_[0] = true;
      GAMESTATE->SetMasterPlayerNumber(PLAYER_1);
      GAMESTATE->SetCurrentStyle(vpStyle[0], PLAYER_1);
      GAMESTATE->cur_song_.Set(pSong);
      GAMESTATE->preferred_song_ = pSong;
      sInitialScreen = StepMania::GetSelectMusicScreen();
    } else {
      sInitialScreen = StepMania::GetInitialScreen();
    }

    Screen* curScreen = SCREENMAN->GetTopScreen();
    if (curScreen->GetScreenType() == game_menu ||
        curScreen->GetScreenType() == attract) {
      SCREENMAN->SetNewScreen(sInitialScreen);
    }
  }
}

/*
 * (c) 2001-2005 Chris Danford, Glenn Maynard
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
