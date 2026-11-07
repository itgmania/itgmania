#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "GameInput.h"
#include "LuaManager.h"
#include "PlayerNumber.h"
#include "global.h"

/*
 * Styles define a set of columns for each player, and information about those
 * columns, like what Instruments are used play those columns and what track
 * to use to populate the column's notes.
 * A "track" is the term used to descibe a particular vertical sting of note
 * in NoteData.
 * A "column" is the term used to describe the vertical string of notes that
 * a player sees on the screen while they're playing.  Column notes are
 * picked from a track, but columns and tracks don't have a 1-to-1
 * correspondance.  For example, dance-versus has 8 columns but only 4 tracks
 * because two players place from the same set of 4 tracks.
 */

#include <algorithm>
#include <cfloat>
#include <string>
#include <vector>

#include "GameState.h"
#include "InputMapper.h"
#include "NoteData.h"
#include "RageUtil.h"
#include "Style.h"

bool Style::GetUsesCenteredArrows() const {
  switch (style_type_) {
    case StyleType_OnePlayerTwoSides:
    case StyleType_TwoPlayersSharedSides:
      return true;
    default:
      return false;
  }
}

void Style::GetTransformedNoteDataForStyle(
    PlayerNumber pn, const NoteData& original, NoteData& note_data_out) const {
  ASSERT(pn >= 0 && pn <= NUM_PLAYERS);

  int new_to_original_track[MAX_COLS_PER_PLAYER];
  for (int col = 0; col < cols_per_player_; col++) {
    ColumnInfo col_info = column_info_[pn][col];
    new_to_original_track[col] = col_info.track_;
  }

  note_data_out.LoadTransformed(
      original, cols_per_player_, new_to_original_track);
}

void Style::StyleInputToGameInput(
    int col, PlayerNumber pn, std::vector<GameInput>& ret) const {
  ASSERT_M(
      pn < NUM_PLAYERS && col < MAX_COLS_PER_PLAYER,
      ssprintf("P%i C%i", pn, col));
  bool using_one_side = style_type_ != StyleType_OnePlayerTwoSides &&
                        style_type_ != StyleType_TwoPlayersSharedSides;

  FOREACH_ENUM(GameController, gc) {
    if (using_one_side && gc != (int)pn) {
      continue;
    }

    int buttons_per_controller =
        INPUTMAPPER->GetInputScheme()->buttons_per_controller_;
    for (GameButton gb = GAME_BUTTON_NEXT; gb < buttons_per_controller;
         gb = (GameButton)(gb + 1)) {
      int this_input_col = input_column_[gc][gb - GAME_BUTTON_NEXT];
      if (this_input_col == END_MAPPING) {
        break;
      }

      // A style can have multiple game inputs mapped to a single column, so
      // we have to return all the game inputs that are valid.  If only the
      // first is returned, then holds will drop on other inputs that should
      // be valid. -Kyz
      if (this_input_col == col) {
        ret.push_back(GameInput(gc, gb));
      }
    }
  }
  if (unlikely(ret.empty())) {
    FAIL_M(ssprintf(
        "Invalid column number %i for player %i in the style %s", col, pn,
        name_));
  }
};

int Style::GameInputToColumn(const GameInput& game_i) const {
  if (game_i.button_ < GAME_BUTTON_NEXT) {
    return Column_Invalid;
  }
  int column_index = game_i.button_ - GAME_BUTTON_NEXT;
  if (input_column_[game_i.controller_][column_index] == NO_MAPPING) {
    return Column_Invalid;
  }

  for (int i = 0; i <= column_index; ++i) {
    if (input_column_[game_i.controller_][i] == END_MAPPING) {
      return Column_Invalid;
    }
  }

  return input_column_[game_i.controller_][column_index];
}

void Style::GetMinAndMaxColX(
    PlayerNumber pn, float& mix_x_out, float& max_x_out) const {
  ASSERT(pn != PLAYER_INVALID);

  mix_x_out = FLT_MAX;
  max_x_out = FLT_MIN;
  for (int i = 0; i < cols_per_player_; i++) {
    mix_x_out = std::min(mix_x_out, column_info_[pn][i].x_offset_);
    max_x_out = std::max(max_x_out, column_info_[pn][i].x_offset_);
  }
}

float Style::GetWidth(PlayerNumber pn) const {
  float left, right;
  GetMinAndMaxColX(pn, left, right);
  // left and right are the center positions of the columns.  The full width
  // needs to be from the edges.
  float width = right - left;
  return width + (width / static_cast<float>(cols_per_player_ - 1));
}

std::string Style::ColToButtonName(int col) const {
  const char* column_name = column_info_[PLAYER_1][col].name_;
  if (column_name != nullptr) {
    return column_name;
  }

  std::vector<GameInput> gi;
  StyleInputToGameInput(col, PLAYER_1, gi);
  return INPUTMAPPER->GetInputScheme()->GetGameButtonName(gi[0].button_);
}

// Lua bindings
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the Style. */
class LunaStyle : public Luna<Style> {
 public:
  static int GetName(T* p, lua_State* L) {
    LuaHelpers::Push(L, (std::string)p->name_);
    return 1;
  }
  DEFINE_METHOD(GetStyleType, style_type_)
  DEFINE_METHOD(GetStepsType, steps_type_)
  DEFINE_METHOD(ColumnsPerPlayer, cols_per_player_)
  static int NeedsZoomOutWith2Players(T* p, lua_State* L) {
    // m_bNeedsZoomOutWith2Players was removed in favor of having
    // ScreenGameplay use the style's width and margin values to calculate
    // the zoom.  So this always returns false. -Kyz
    lua_pushboolean(L, false);
    return 1;
  }
  static int GetWidth(T* p, lua_State* L) {
    PlayerNumber pn = Enum::Check<PlayerNumber>(L, 1);
    lua_pushnumber(L, p->GetWidth(pn));
    return 1;
  }
  DEFINE_METHOD(LockedDifficulty, lock_difficulties_)

  static int GetColumnInfo(T* p, lua_State* L) {
    PlayerNumber pn = Enum::Check<PlayerNumber>(L, 1);
    int col = IArg(2) - 1;
    if (col < 0 || col >= p->cols_per_player_) {
      LuaHelpers::ReportScriptErrorFmt(
          "Style:GetColumnDrawOrder(): column %i out of range( 1 to %i )",
          col + 1, p->cols_per_player_);
      return 0;
    }

    LuaTable ret;
    lua_pushnumber(L, p->column_info_[pn][col].track_ + 1);
    ret.Set(L, "Track");
    lua_pushnumber(L, p->column_info_[pn][col].x_offset_);
    ret.Set(L, "XOffset");
    lua_pushstring(L, p->ColToButtonName(col).c_str());
    ret.Set(L, "Name");

    ret.PushSelf(L);
    return 1;
  }

  static int GetColumnDrawOrder(T* p, lua_State* L) {
    int col = IArg(1) - 1;
    if (col < 0 || col >= p->cols_per_player_ * NUM_PLAYERS) {
      LuaHelpers::ReportScriptErrorFmt(
          "Style:GetColumnDrawOrder(): column %i out of range( 1 to %i )",
          col + 1, p->cols_per_player_ * NUM_PLAYERS);
      return 0;
    }
    lua_pushnumber(L, p->column_draw_order_[col] + 1);
    return 1;
  }

  LunaStyle() {
    ADD_METHOD(GetName);
    ADD_METHOD(GetStyleType);
    ADD_METHOD(GetStepsType);
    ADD_METHOD(GetColumnInfo);
    ADD_METHOD(GetColumnDrawOrder);
    ADD_METHOD(ColumnsPerPlayer);
    ADD_METHOD(NeedsZoomOutWith2Players);
    ADD_METHOD(GetWidth);
    ADD_METHOD(LockedDifficulty);
  }
};

LUA_REGISTER_CLASS(Style)

/*
 * (c) 2001-2002 Chris Danford
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
