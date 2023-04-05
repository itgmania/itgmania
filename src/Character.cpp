#include "global.h"

#include "Character.h"

#include "ActorUtil.h"
#include "IniFile.h"
#include "RageTextureID.h"
#include "RageUtil.h"

#include <vector>

RString GetRandomFileInDir(RString dir);

Character::Character()
    : character_dir_(""),
      character_id_(""),
      preload_ref_count_(0),
      display_name_(""),
      card_path_(""),
      icon_path_("") {}

bool Character::Load(RString char_dir) {
  // Save character directory
  if (char_dir.Right(1) != "/") {
    char_dir += "/";
  }
  character_dir_ = char_dir;

  // Save ID
  {
    std::vector<RString> as;
    split(char_dir, "/", as);
    character_id_ = as.back();
  }

  {
    std::vector<RString> as;
    GetDirListing(character_dir_ + "card.png", as, false, true);
    GetDirListing(character_dir_ + "card.jpg", as, false, true);
    GetDirListing(character_dir_ + "card.jpeg", as, false, true);
    GetDirListing(character_dir_ + "card.gif", as, false, true);
    GetDirListing(character_dir_ + "card.bmp", as, false, true);
    if (as.empty()) {
      card_path_ = "";
    } else {
      card_path_ = as[0];
    }
  }

  {
    std::vector<RString> as;
    GetDirListing(character_dir_ + "icon.png", as, false, true);
    GetDirListing(character_dir_ + "icon.jpg", as, false, true);
    GetDirListing(character_dir_ + "icon.jpeg", as, false, true);
    GetDirListing(character_dir_ + "icon.gif", as, false, true);
    GetDirListing(character_dir_ + "icon.bmp", as, false, true);
    if (as.empty()) {
      icon_path_ = "";
    } else {
      icon_path_ = as[0];
    }
  }

  // Save attacks
  IniFile ini;
  if (!ini.ReadFile(char_dir + "character.ini")) {
    return false;
  }
  for (int i = 0; i < NUM_ATTACK_LEVELS; i++) {
    for (int j = 0; j < NUM_ATTACKS_PER_LEVEL; j++) {
      ini.GetValue(
          "Character", ssprintf("Level%dAttack%d", i + 1, j + 1),
          attacks_[i][j]);
    }
  }

  // Get optional display name
  ini.GetValue("Character", "DisplayName", display_name_);

  // Get optional InitCommand
  RString s;
  ini.GetValue("Character", "InitCommand", s);
  m_cmdInit = ActorUtil::ParseActorCommands(s);

  return true;
}

RString GetRandomFileInDir(RString dir) {
  std::vector<RString> files;
  GetDirListing(dir, files, false, true);
  if (files.empty()) {
    return RString();
  } else {
    return files[RandomInt(files.size())];
  }
}

RString Character::GetModelPath() const {
  RString s = character_dir_ + "model.txt";
  if (DoesFileExist(s)) {
    return s;
  } else {
    return RString();
  }
}

RString Character::GetRestAnimationPath() const {
  return DerefRedir(GetRandomFileInDir(character_dir_ + "Rest/"));
}
RString Character::GetWarmUpAnimationPath() const {
  return DerefRedir(GetRandomFileInDir(character_dir_ + "WarmUp/"));
}
RString Character::GetDanceAnimationPath() const {
  return DerefRedir(GetRandomFileInDir(character_dir_ + "Dance/"));
}
RString Character::GetTakingABreakPath() const {
  std::vector<RString> as;
  GetDirListing(character_dir_ + "break.png", as, false, true);
  GetDirListing(character_dir_ + "break.jpg", as, false, true);
  GetDirListing(character_dir_ + "break.jpeg", as, false, true);
  GetDirListing(character_dir_ + "break.gif", as, false, true);
  GetDirListing(character_dir_ + "break.bmp", as, false, true);
  if (as.empty()) {
    return RString();
  } else {
    return as[0];
  }
}

RString Character::GetSongSelectIconPath() const {
  std::vector<RString> as;
  // First try and find an icon specific to the select music screen
  // so you can have different icons for music select / char select.
  GetDirListing(character_dir_ + "selectmusicicon.png", as, false, true);
  GetDirListing(character_dir_ + "selectmusicicon.jpg", as, false, true);
  GetDirListing(character_dir_ + "selectmusicicon.jpeg", as, false, true);
  GetDirListing(character_dir_ + "selectmusicicon.gif", as, false, true);
  GetDirListing(character_dir_ + "selectmusicicon.bmp", as, false, true);

  if (as.empty()) {
    // If that failed, try using the regular icon.
    GetDirListing(character_dir_ + "icon.png", as, false, true);
    GetDirListing(character_dir_ + "icon.jpg", as, false, true);
    GetDirListing(character_dir_ + "icon.jpeg", as, false, true);
    GetDirListing(character_dir_ + "icon.gif", as, false, true);
    GetDirListing(character_dir_ + "icon.bmp", as, false, true);
    if (as.empty()) {
      return RString();
    } else {
      return as[0];
    }
  } else {
    return as[0];
  }
}

RString Character::GetStageIconPath() const {
  std::vector<RString> as;
  // First try and find an icon specific to the select music screen
  // so you can have different icons for music select / char select.
  GetDirListing(character_dir_ + "stageicon.png", as, false, true);
  GetDirListing(character_dir_ + "stageicon.jpg", as, false, true);
  GetDirListing(character_dir_ + "stageicon.jpeg", as, false, true);
  GetDirListing(character_dir_ + "stageicon.gif", as, false, true);
  GetDirListing(character_dir_ + "stageicon.bmp", as, false, true);

  if (as.empty()) {
    // If that failed, try using the regular icon.
    GetDirListing(character_dir_ + "card.png", as, false, true);
    GetDirListing(character_dir_ + "card.jpg", as, false, true);
    GetDirListing(character_dir_ + "card.jpeg", as, false, true);
    GetDirListing(character_dir_ + "card.gif", as, false, true);
    GetDirListing(character_dir_ + "card.bmp", as, false, true);
    if (as.empty()) {
      return RString();
    } else {
      return as[0];
    }
  } else {
    return as[0];
  }
}

bool Character::Has2DElems() {
  // Check 2D Idle BGAnim exists.
  if (DoesFileExist(character_dir_ + "2DFail/BGAnimation.ini")) {
    return true;
  }
  // Check 2D Idle BGAnim exists.
  if (DoesFileExist(character_dir_ + "2DFever/BGAnimation.ini")) {
    return true;
  }
  // Check 2D Idle BGAnim exists.
  if (DoesFileExist(character_dir_ + "2DGood/BGAnimation.ini")) {
    return true;
  }
  // Check 2D Idle BGAnim exists.
  if (DoesFileExist(character_dir_ + "2DMiss/BGAnimation.ini")) {
    return true;
  }
  // Check 2D Idle BGAnim exists.
  if (DoesFileExist(character_dir_ + "2DWin/BGAnimation.ini")) {
    return true;
  }
  // Check 2D Idle BGAnim exists.
  if (DoesFileExist(character_dir_ + "2DWinFever/BGAnimation.ini")) {
    return true;
  }
  // Check 2D Idle BGAnim exists.
  if (DoesFileExist(character_dir_ + "2DGreat/BGAnimation.ini")) {
    return true;
  }
  // Check 2D Idle BGAnim exists.
  if (DoesFileExist(character_dir_ + "2DIdle/BGAnimation.ini")) {
    return true;
  }
  return false;
}

void Character::DemandGraphics() {
  ++preload_ref_count_;
  if (preload_ref_count_ == 1) {
    RString s = GetIconPath();
    if (!s.empty()) {
      preload_.Load(s);
    }
  }
}

void Character::UndemandGraphics() {
  --preload_ref_count_;
  if (preload_ref_count_ == 0) {
    preload_.UnloadAll();
  }
}

// lua start
#include "LuaBinding.h"

// Allow Lua to have access to the Character.
class LunaCharacter : public Luna<Character> {
 public:
  static int GetCardPath(T* p, lua_State* L) {
    lua_pushstring(L, p->GetCardPath());
    return 1;
  }

  static int GetIconPath(T* p, lua_State* L) {
    lua_pushstring(L, p->GetIconPath());
    return 1;
  }

  static int GetSongSelectIconPath(T* p, lua_State* L) {
    lua_pushstring(L, p->GetSongSelectIconPath());
    return 1;
  }

  static int GetStageIconPath(T* p, lua_State* L) {
    lua_pushstring(L, p->GetStageIconPath());
    return 1;
  }

  static int GetModelPath(T* p, lua_State* L) {
    lua_pushstring(L, p->GetModelPath());
    return 1;
  }

  static int GetRestAnimationPath(T* p, lua_State* L) {
    lua_pushstring(L, p->GetRestAnimationPath());
    return 1;
  }

  static int GetWarmUpAnimationPath(T* p, lua_State* L) {
    lua_pushstring(L, p->GetWarmUpAnimationPath());
    return 1;
  }

  static int GetDanceAnimationPath(T* p, lua_State* L) {
    lua_pushstring(L, p->GetDanceAnimationPath());
    return 1;
  }

  static int GetCharacterDir(T* p, lua_State* L) {
    lua_pushstring(L, p->character_dir_);
    return 1;
  }

  static int GetCharacterID(T* p, lua_State* L) {
    lua_pushstring(L, p->character_id_);
    return 1;
  }

  static int GetDisplayName(T* p, lua_State* L) {
    lua_pushstring(L, p->GetDisplayName());
    return 1;
  }

  LunaCharacter() {
    ADD_METHOD(GetCardPath);
    ADD_METHOD(GetIconPath);
    ADD_METHOD(GetSongSelectIconPath);
    ADD_METHOD(GetStageIconPath);
    // sm-ssc adds:
    ADD_METHOD(GetModelPath);
    ADD_METHOD(GetRestAnimationPath);
    ADD_METHOD(GetWarmUpAnimationPath);
    ADD_METHOD(GetDanceAnimationPath);
    ADD_METHOD(GetCharacterDir);
    ADD_METHOD(GetCharacterID);
    ADD_METHOD(GetDisplayName);
  }
};

LUA_REGISTER_CLASS(Character)
// lua end

/*
 * (c) 2003 Chris Danford
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
