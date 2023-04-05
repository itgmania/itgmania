#ifndef Character_H
#define Character_H

#include "GameConstantsAndTypes.h"
#include "LuaReference.h"
#include "RageTexturePreloader.h"

// TODO(teejusb): Remove forward declarations.
struct lua_State;
typedef lua_State Lua;

// A persona that defines attacks for use in battle.
class Character {
 public:
  Character();
  ~Character() {}

	// Return true if successful
  bool Load(RString char_dir);

  RString GetTakingABreakPath() const;
  RString GetCardPath() const { return card_path_; }
  RString GetIconPath() const { return icon_path_; }

  RString GetModelPath() const;
  RString GetRestAnimationPath() const;
  RString GetWarmUpAnimationPath() const;
  RString GetDanceAnimationPath() const;
  RString GetSongSelectIconPath() const;
  RString GetStageIconPath() const;
  bool Has2DElems();

  bool IsDefaultCharacter() const {
    return character_id_.CompareNoCase("default") == 0;
  }

  void DemandGraphics();
  void UndemandGraphics();

  // Lua
  void PushSelf(Lua* L);

  // Smart accessor
  const RString& GetDisplayName() const {
    return !display_name_.empty() ? display_name_ : character_id_;
  }

  RString character_dir_;
  RString character_id_;
  apActorCommands m_cmdInit;
  RString attacks_[NUM_ATTACK_LEVELS][NUM_ATTACKS_PER_LEVEL];
  RageTexturePreloader preload_;
  int preload_ref_count_;

 private:
  RString display_name_;
  RString card_path_;
  RString icon_path_;
};

#endif  // Character_H

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
