#ifndef GAMEMANAGER_H
#define GAMEMANAGER_H

#include <vector>

#include "Game.h"
#include "GameConstantsAndTypes.h"
#include "GameInput.h"
#include "Style.h"

struct lua_State;

// The collective information about a Steps' Type.
struct StepsTypeInfo {
  const char* name;
  // The number of tracks, or columns, of this type.
  int num_tracks;
  // A flag to determine if we allow this type to be autogen'ed to other types.
  bool allow_autogen;
  // The most basic StyleType that this StpesTypeInfo is used with.
  StepsTypeCategory steps_type_category;
  RString GetLocalizedString() const;
};

// Manages Games and Styles.
class GameManager {
 public:
  GameManager();
  ~GameManager();

  void GetStylesForGame(
      const Game* game, std::vector<const Style*>& styles_out,
      bool editor = false);
  const Game* GetGameForStyle(const Style* style);
  void GetStepsTypesForGame(
      const Game* game, std::vector<StepsType>& steps_type_out);
  const Style* GetEditorStyleForStepsType(StepsType steps_type);
  void GetDemonstrationStylesForGame(
      const Game* game, std::vector<const Style*>& styles_out);
  const Style* GetHowToPlayStyleForGame(const Game* game);
  void GetCompatibleStyles(
      const Game* game, int num_players, std::vector<const Style*>& styles_out);
  const Style* GetFirstCompatibleStyle(
      const Game* game, int num_players, StepsType steps_type);

  void GetEnabledGames(std::vector<const Game*>& games_out);
  const Game* GetDefaultGame();
  bool IsGameEnabled(const Game* game);
  int GetIndexFromGame(const Game* game);
  const Game* GetGameFromIndex(int index);

  const StepsTypeInfo& GetStepsTypeInfo(StepsType steps_type);
  StepsType StringToStepsType(RString steps_type_str);
  const Game* StringToGame(RString game_str);
  const Style* GameAndStringToStyle(const Game* game, RString style_str);
  RString StyleToLocalizedString(const Style* style);

  // Lua
  void PushSelf(lua_State* L);
};

// Global and accessible from anywhere in our program.
extern GameManager* GAMEMAN;

#endif  // GAMEMANAGER_H

/**
 * @file
 * @author Chris Danford, Glenn Maynard (c) 2001-2004
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
