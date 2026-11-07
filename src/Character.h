#ifndef Character_H
#define Character_H

#include <string>

#include "GameConstantsAndTypes.h"
#include "LuaReference.h"
#include "StdString.h"
struct lua_State;
typedef lua_State Lua;

/** @brief A persona that defines attacks for use in battle. */
class Character {
 public:
  Character();
  ~Character() {}

  bool Load(std::string char_dir);  // return true if successful

  std::string GetCardPath() const { return card_path_; }
  std::string GetIconPath() const { return icon_path_; }

  std::string GetModelPath() const;
  std::string GetRestAnimationPath() const;
  std::string GetWarmUpAnimationPath() const;
  std::string GetDanceAnimationPath() const;
  std::string GetSongSelectIconPath() const;
  std::string GetStageIconPath() const;
  bool IsDefaultCharacter() const {
    return CompareNoCase(character_id_, "default") == 0;
  }

  // Lua
  void PushSelf(Lua* L);

  // smart accessor
  const std::string& GetDisplayName() const {
    return !display_name_.empty() ? display_name_ : character_id_;
  }

  std::string char_dir_;
  std::string character_id_;

 private:
  std::string display_name_;
  std::string card_path_;
  std::string icon_path_;

 public:
  apActorCommands init_;

  std::string attacks_[NUM_ATTACK_LEVELS][NUM_ATTACKS_PER_LEVEL];
};

#endif

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
