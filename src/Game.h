#ifndef GAMEDEF_H
#define GAMEDEF_H

#include "GameConstantsAndTypes.h"
#include "GameInput.h"
#include "InputMapper.h"

struct lua_State;
class Style;

// PrimaryMenuButton and SecondaryMenuButton are used to support using
// DeviceInputs that only  navigate the menus.

// A button being a primary menu button means that this GameButton will generate
// a the corresponding MenuInput IF AND ONLY IF the GameButton corresponding to
// the pimary input is not mapped.

// Example 1: A user is using an arcade machine as their controller. Most
// machines have MenuLeft, MenuStart, and MenuRight buttons on the cabinet, so
// they should be used to navigate menus. The user will map these DeviceInputs
// to the GameButtons "MenuLeft (optional)", "MenuStart", and "MenuRight
// (optional)".

// Example 2:  A user is using PlayStation dance pads to play. These controllers
// don't have dedicated DeviceInputs for MenuLeft and MenuRight. The user maps
// Up, Down, Left, and Right as normal. Since the Left and Right GameButtons
// have the flag FLAG_SECONDARY_MENU_*, they will function as MenuLeft and
// MenuRight as long as "MenuLeft (optional)" and "MenuRight (optional)" are not
// mapped.

/** @brief Holds information about a particular style of a game (e.g. "single",
 * "double"). */
struct Game {
  const char* name_;
  const Style* const* styles_;

  /** @brief Do we count multiple notes in a row as separate notes, or as one
   * note? */
  bool count_notes_separately_;
  bool players_have_separate_styles_;

  InputScheme input_scheme_;

  struct PerButtonInfo {
    GameButtonType gbt_;
  };
  /**
   * @brief Data for each Game-specific GameButton.
   *
   * This starts at GAME_BUTTON_NEXT. */
  PerButtonInfo per_button_info_[NUM_GameButton];
  const PerButtonInfo* GetPerButtonInfo(GameButton gb) const;

  TapNoteScore MapTapNoteScore(TapNoteScore tns) const;
  TapNoteScore map_w1_to_;
  TapNoteScore map_w2_to_;
  TapNoteScore map_w3_to_;
  TapNoteScore map_w4_to_;
  TapNoteScore map_w5_to_;
  TapNoteScore GetMapJudgmentTo(TapNoteScore tns) const;

  // Lua
  void PushSelf(lua_State* L);
};

#endif

/**
 * @file
 * @author Chris Danford (c) 2001-2002
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
