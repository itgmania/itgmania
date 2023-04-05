#include "global.h"

#include "CharacterManager.h"

#include "Character.h"
#include "GameState.h"
#include "LuaManager.h"

#include <vector>


#define CHARACTERS_DIR "/Characters/"

// Global object accessible from anywhere in the program.
CharacterManager* CHARMAN = nullptr;  

CharacterManager::CharacterManager() {
  // Register with Lua.
  {
    Lua* L = LUA->Get();
    lua_pushstring(L, "CHARMAN");
    this->PushSelf(L);
    lua_settable(L, LUA_GLOBALSINDEX);
    LUA->Release(L);
  }

  for (unsigned i = 0; i < characters_.size(); ++i) {
    SAFE_DELETE(characters_[i]);
  }
  characters_.clear();

  std::vector<RString> as;
  GetDirListing(CHARACTERS_DIR "*", as, true, true);
  StripCvsAndSvn(as);
  StripMacResourceForks(as);

  bool found_default = false;
  for (unsigned i = 0; i < as.size(); i++) {
    RString character_name, dummy;
    splitpath(as[i], dummy, character_name, dummy);
    character_name.MakeLower();

    if (character_name.CompareNoCase("default") == 0) {
      found_default = true;
    }

    Character* character = new Character;
    if (character->Load(as[i])) {
      characters_.push_back(character);
    } else {
      delete character;
    }
  }

  if (!found_default) {
    RageException::Throw("'Characters/default' is missing.");
  }
}

CharacterManager::~CharacterManager() {
  for (unsigned i = 0; i < characters_.size(); ++i) {
    SAFE_DELETE(characters_[i]);
  }

  // Unregister with Lua.
  LUA->UnsetGlobal("CHARMAN");
}

void CharacterManager::GetCharacters(std::vector<Character*>& characters_out) {
  for (unsigned i = 0; i < characters_.size(); ++i) {
    if (!characters_[i]->IsDefaultCharacter()) {
      characters_out.push_back(characters_[i]);
    }
  }
}

Character* CharacterManager::GetRandomCharacter() {
  std::vector<Character*> characters;
  GetCharacters(characters);
  if (characters.size()) {
    return characters[RandomInt(characters.size())];
  } else {
    return GetDefaultCharacter();
  }
}

Character* CharacterManager::GetDefaultCharacter() {
  for (Character* c : characters_) {
    if (c->IsDefaultCharacter()) {
      return c;
    }
  }

  // We always have the default character.
  FAIL_M("There must be a default character available!");
}

void CharacterManager::DemandGraphics() {
  for (Character* c : characters_) {
    c->DemandGraphics();
  }
}

void CharacterManager::UndemandGraphics() {
  for (Character* c : characters_) {
    c->UndemandGraphics();
  }
}

Character* CharacterManager::GetCharacterFromID(RString character_id) {
  for (unsigned i = 0; i < characters_.size(); ++i) {
    if (characters_[i]->character_id_ == character_id) {
      return characters_[i];
    }
  }

  return nullptr;
}

// lua start
#include "LuaBinding.h"

// Allow Lua to have access to the CharacterManager.
class LunaCharacterManager : public Luna<CharacterManager> {
 public:
  static int GetCharacter(T* p, lua_State* L) {
    Character* character = p->GetCharacterFromID(SArg(1));
    if (character != nullptr) {
      character->PushSelf(L);
    } else {
      lua_pushnil(L);
    }

    return 1;
  }

  static int GetRandomCharacter(T* p, lua_State* L) {
    Character* character = p->GetRandomCharacter();
    if (character != nullptr) {
      character->PushSelf(L);
    } else {
      lua_pushnil(L);
    }

    return 1;
  }

  static int GetAllCharacters(T* p, lua_State* L) {
    std::vector<Character*> vChars;
    p->GetCharacters(vChars);

    LuaHelpers::CreateTableFromArray(vChars, L);
    return 1;
  }

  static int GetCharacterCount(T* p, lua_State* L) {
    std::vector<Character*> chars;
    p->GetCharacters(chars);
    lua_pushnumber(L, chars.size());
    return 1;
  }

  LunaCharacterManager() {
    ADD_METHOD(GetCharacter);
    // sm-ssc adds:
    ADD_METHOD(GetRandomCharacter);
    ADD_METHOD(GetAllCharacters);
    ADD_METHOD(GetCharacterCount);
  }
};

LUA_REGISTER_CLASS(CharacterManager)
// lua end

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
