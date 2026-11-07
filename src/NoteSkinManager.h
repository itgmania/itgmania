#ifndef NOTE_SKIN_MANAGER_H
#define NOTE_SKIN_MANAGER_H

#include <string>
#include <vector>

#include "Actor.h"
#include "GameInput.h"
#include "LuaManager.h"
#include "PlayerNumber.h"
#include "Style.h"
#include "global.h"

struct Game;
struct NoteSkinData;

/** @brief Loads note skins. */
class NoteSkinManager {
 public:
  NoteSkinManager();
  ~NoteSkinManager();

  void RefreshNoteSkinData(const Game* game);
  void GetNoteSkinNames(
      const Game* game, std::vector<std::string>& add_to,
      bool include_variants = true);
  void GetNoteSkinNames(
      std::vector<std::string>& add_to,
      bool include_variants =
          true);  // looks up current const Game* in GAMESTATE
  void GetVariantNamesForNoteSkin(
      const std::string& note_skin, std::vector<std::string>& add_to);
  void GetVariantNamesForNoteSkin(
      const Game* game, const std::string& note_skin,
      std::vector<std::string>&
          add_to);  // looks up current const Game* in GAMESTATE
  bool NoteSkinNameInList(
      const std::string name, std::vector<std::string> name_list);
  bool DoesNoteSkinExist(
      const std::string&
          note_skin);  // looks up current const Game* in GAMESTATE
  bool HasVariants(const std::string& note_skin);  // looks up current const
                                                   // Game* in GAMESTATE
  bool IsNoteSkinVariant(const std::string& note_skin);
  bool DoNoteSkinsExistForGame(const Game* game);
  std::string
  GetDefaultNoteSkinName();  // looks up current const Game* in GAMESTATE

  void ValidateNoteSkinName(std::string& name);

  void SetCurrentNoteSkin(const std::string& note_skin) {
    current_note_skin_ = note_skin;
  }
  const std::string& GetCurrentNoteSkin() { return current_note_skin_; }
  void SetPlayerNumber(PlayerNumber pn) { player_number_ = pn; }
  void SetGameController(GameController gc) { game_controller_ = gc; }
  std::string GetPath(
      const std::string& button_name, const std::string& element);
  bool PushActorTemplate(
      Lua* L, const std::string& button, const std::string& element,
      bool sprite_only, const std::string& color);
  Actor* LoadActor(
      const std::string& button, const std::string& element,
      Actor* parent = nullptr, bool sprite_only = false,
      const std::string& color = "4th");

  std::string GetMetric(
      const std::string& button_name, const std::string& value);
  int GetMetricI(const std::string& button_name, const std::string& value_name);
  float GetMetricF(
      const std::string& button_name, const std::string& value_name);
  bool GetMetricB(
      const std::string& button_name, const std::string& value_name);
  apActorCommands GetMetricA(
      const std::string& button_name, const std::string& value_name);

  // Lua
  void PushSelf(lua_State* L);

 protected:
  std::string GetPathFromDirAndFile(
      const std::string& dir, const std::string& file_name);
  void GetAllNoteSkinNamesForGame(
      const Game* game, std::vector<std::string>& add_to,
      bool include_variants = true);
  void FilterNoteSkinsByStyle(
      const Style* style, std::vector<std::string>& add_to);

  bool LoadNoteSkinData(
      const std::string& note_skin_name, NoteSkinData& data_out);
  bool LoadNoteSkinDataRecursive(
      const std::string& note_skin_name, NoteSkinData& data_out);
  std::string current_note_skin_;
  const Game* cur_game_;

  // xxx: is this the best way to implement this? -freem
  PlayerNumber player_number_;
  GameController game_controller_;
};

extern NoteSkinManager*
    NOTESKIN;  // global and accessible from anywhere in our program

class LockNoteSkin {
 public:
  LockNoteSkin(const std::string& note_skin) {
    ASSERT(NOTESKIN->GetCurrentNoteSkin().empty());
    NOTESKIN->SetCurrentNoteSkin(note_skin);
  }
  ~LockNoteSkin() { NOTESKIN->SetCurrentNoteSkin(std::string()); }
};

#endif

/**
 * @file
 * @author Chris Danford (c) 2003-2004
 * @section LICENSE
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
