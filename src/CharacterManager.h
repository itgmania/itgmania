#ifndef CHARACTER_MANAGER_H
#define CHARACTER_MANAGER_H

#include <vector>

#include "Character.h"

struct lua_State;

// Manage all of the Characters.
class CharacterManager {
 public:
  // Set up the character manager.
  CharacterManager();
  // Destroy the character manager.
  ~CharacterManager();

  void GetCharacters(std::vector<Character*>& characters_out);
  // Get one installed character at random.
  Character* GetRandomCharacter();
  // Get the character assigned as the default.
  Character* GetDefaultCharacter();
  Character* GetCharacterFromID(RString character_id);

  void DemandGraphics();
  void UndemandGraphics();

  // Lua
  void PushSelf(lua_State* L);

 private:
  std::vector<Character*> characters_;
};

// Global and accessible from anywhere in our program.
extern CharacterManager* CHARMAN;

#endif  // CHARACTER_MANAGER_H

/*
 * (c) 2001-2004 Chris Danford
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
