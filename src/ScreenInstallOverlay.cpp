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

void InstallSmzipOsArg(
    const std::string& os_zip_file, PlayAfterLaunchInfo& out);
PlayAfterLaunchInfo DoInstalls(CommandLineActions::CommandLineArgs args);

static void Parse(const std::string& dir, PlayAfterLaunchInfo& out) {
  std::vector<std::string> dir_parts;
  split(dir, "/", dir_parts, true);
  if (dir_parts.size() == 3 && EqualsNoCase(dir_parts[0], "Songs")) {
    out.song_dir_ = "/" + dir;
  } else if (dir_parts.size() == 2 && EqualsNoCase(dir_parts[0], "Themes")) {
    out.theme_ = dir_parts[1];
  }
}

static const std::string TEMP_ZIP_MOUNT_POINT = "/@temp-zip/";
const std::string TEMP_OS_MOUNT_POINT = "/@temp-os/";

static void InstallSmzip(
    const std::string& zip_file, PlayAfterLaunchInfo& out) {
  if (!FILEMAN->Mount("zip", zip_file, TEMP_ZIP_MOUNT_POINT)) {
    FAIL_M("Failed to mount " + zip_file);
  }

  std::vector<std::string> files;
  {
    std::vector<std::string> raw_files;
    GetDirListingRecursive(TEMP_ZIP_MOUNT_POINT, "*", raw_files);

    std::vector<std::string> pretty_files;
    for (const std::string& s : raw_files) {
      if (EqualsNoCase(GetExtension(s), "ctl")) {
        continue;
      }

      files.push_back(s);

      std::string s2 = Right(s, s.length() - TEMP_ZIP_MOUNT_POINT.length());
      pretty_files.push_back(s2);
    }
    sort(pretty_files.begin(), pretty_files.end());
  }

  std::string result = "Success installing " + zip_file;
  for (std::string& tmp_file : files) {
    std::string dest_file = tmp_file;
    dest_file =
        Right(dest_file, dest_file.length() - TEMP_ZIP_MOUNT_POINT.length());

    std::string dir, throw_away;
    splitpath(dest_file, dir, throw_away, throw_away);

    Parse(dir, out);
    out.any_song_changed_ = true;

    FILEMAN->CreateDir(dir);

    if (!FileCopy(tmp_file, dest_file)) {
      result = "Error extracting " + dest_file;
      break;
    }
  }
  FILEMAN->Unmount("zip", zip_file, TEMP_ZIP_MOUNT_POINT);

  SCREENMAN->SystemMessage(result);
}

void InstallSmzipOsArg(
    const std::string& os_zip_file, PlayAfterLaunchInfo& out) {
  SCREENMAN->SystemMessage("Installing " + os_zip_file);

  std::string os_dir, filename, ext;
  splitpath(os_zip_file, os_dir, filename, ext);

  if (!FILEMAN->Mount("dir", os_dir, TEMP_OS_MOUNT_POINT)) {
    FAIL_M("Failed to mount " + os_dir);
  }
  InstallSmzip(TEMP_OS_MOUNT_POINT + filename + ext, out);

  FILEMAN->Unmount("dir", os_dir, TEMP_OS_MOUNT_POINT);
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

void ScreenInstallOverlay::Update(float delta_time) {
  Screen::Update(delta_time);
  PlayAfterLaunchInfo play_after_launch_info;
  while (CommandLineActions::ToProcess.size() > 0) {
    CommandLineActions::CommandLineArgs args =
        CommandLineActions::ToProcess.back();
    CommandLineActions::ToProcess.pop_back();
    PlayAfterLaunchInfo pali2 = DoInstalls(args);
    play_after_launch_info.OverlayWith(pali2);
  }
  if (play_after_launch_info.any_song_changed_) {
    SONGMAN->Reload(false);
  }

  if (!play_after_launch_info.song_dir_.empty()) {
    Song* song = nullptr;
    GAMESTATE->Reset();
    std::string initial_screen;
    if (play_after_launch_info.song_dir_.length() > 0) {
      song = SONGMAN->GetSongFromDir(play_after_launch_info.song_dir_);
    }
    if (song) {
      std::vector<const Style*> style;
      GAMEMAN->GetStylesForGame(GAMESTATE->cur_game_, style, false);
      GAMESTATE->play_mode_.Set(PLAY_MODE_REGULAR);
      GAMESTATE->side_is_joined_[0] = true;
      GAMESTATE->SetMasterPlayerNumber(PLAYER_1);
      GAMESTATE->SetCurrentStyle(style[0], PLAYER_1);
      GAMESTATE->cur_song_.Set(song);
      GAMESTATE->preferred_song_ = song;
      initial_screen = StepMania::GetSelectMusicScreen();
    } else {
      initial_screen = StepMania::GetInitialScreen();
    }

    Screen* cur_screen = SCREENMAN->GetTopScreen();
    if (cur_screen->GetScreenType() == game_menu ||
        cur_screen->GetScreenType() == attract) {
      SCREENMAN->SetNewScreen(initial_screen);
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
