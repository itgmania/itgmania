#include "ModIconRow.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "ActorFrame.h"
#include "ActorUtil.h"
#include "EnumHelper.h"
#include "GameState.h"
#include "LuaManager.h"
#include "MessageManager.h"
#include "ModIcon.h"
#include "PlayerNumber.h"
#include "PlayerOptions.h"
#include "PlayerState.h"
#include "RageUtil.h"
#include "global.h"

int OptionToPreferredColumn(std::string option_text);

REGISTER_ACTOR_CLASS(ModIconRow);

ModIconRow::ModIconRow() {
  pn_ = PlayerNumber_Invalid;

  this->SubscribeToMessage("PlayerOptionsChanged");
}

ModIconRow::~ModIconRow() {
  for (ModIcon* p : mod_icon_) {
    RageUtil::SafeDelete(p);
  }
  this->RemoveAllChildren();
}

void ModIconRow::Load(const std::string& metrics_group, PlayerNumber pn) {
  ASSERT_M(pn_ == PlayerNumber_Invalid, "Multiple calls to Load");

  metrics_group_ = metrics_group;
  pn_ = pn;

  SPACING_X.Load(metrics_group, "SpacingX");
  SPACING_Y.Load(metrics_group, "SpacingY");
  NUM_OPTION_ICONS.Load(metrics_group, "NumModIcons");
  OPTION_ICON_METRICS_GROUP.Load(metrics_group, "ModIconMetricsGroup");

  for (int i = 0; i < NUM_OPTION_ICONS; i++) {
    ModIcon* p = new ModIcon;
    p->SetName("ModIcon");
    float offset = SCALE(
        i, 0, NUM_OPTION_ICONS - 1, -(NUM_OPTION_ICONS - 1) / 2.0f,
        (float)(NUM_OPTION_ICONS - 1) / 2.0f);
    p->SetXY(offset * SPACING_X, offset * SPACING_Y);
    p->Load(OPTION_ICON_METRICS_GROUP);
    ActorUtil::LoadAllCommands(p, metrics_group);
    mod_icon_.push_back(p);
    this->AddChild(p);
  }

  SetFromGameState();
}

void ModIconRow::HandleMessage(const Message& msg) {
  if (msg.GetName() == "PlayerOptionsChanged") {
    SetFromGameState();
  }

  ActorFrame::HandleMessage(msg);
}

struct OptionColumnEntry {
  const char* string_;
  int slot_index_;

  // void FromStack( lua_State *L, int iPos );
};

// todo: metric these? -aj
static const OptionColumnEntry g_OptionColumnEntries[] = {
    {"Boost", 0},
    {"Brake", 0},
    {"Wave", 0},
    {"Expand", 0},
    {"Boomerang", 0},
    //--------------------//
    {"Drunk", 1},
    {"Dizzy", 1},
    {"Mini", 1},
    {"Flip", 1},
    {"Tornado", 1},
    //--------------------//
    {"Hidden", 2},
    {"Sudden", 2},
    {"Stealth", 2},
    {"Blink", 2},
    {"RandomVanish", 2},
    //--------------------//
    {"Mirror", 3},
    {"LRMirror", 3},
    {"UDMirror", 3},
    {"Left", 3},
    {"Right", 3},
    {"Shuffle", 3},
    {"SuperShuffle", 3},
    //--------------------//
    {"Little", 4},
    {"NoHolds", 4},
    {"Dark", 4},
    {"Blind", 4},
    //--------------------//
    {"Reverse", 5},
    {"Split", 5},
    {"Alternate", 5},
    {"Cross", 5},
    {"Centered", 5},
    //--------------------//
    {"Incoming", 6},
    {"Space", 6},
    {"Hallway", 6},
    {"Distant", 6},
};

int OptionToPreferredColumn(std::string option_text) {
  // Speedups always go in column 0. digit ... x
  if (option_text.size() > 1 && isdigit(option_text[0]) &&
      tolower(option_text[option_text.size() - 1]) == 'x') {
    return 0;
  }

  for (unsigned i = 0; i < ARRAYLEN(g_OptionColumnEntries); i++) {
    if (g_OptionColumnEntries[i].string_ == option_text) {
      return g_OptionColumnEntries[i].slot_index_;
    }
  }

  // This warns about C1234 and noteskins.
  //	LOG->Warn("Unknown option: '%s'", sOptionText.c_str() );
  return 0;
}

void ModIconRow::SetFromGameState() {
  PlayerNumber pn = pn_;

  std::string s_options =
      GAMESTATE->player_state_[pn]->player_options_.GetStage().GetString();
  std::vector<std::string> vs_options;
  split(s_options, ", ", vs_options, true);

  std::vector<std::string>
      text;  // fill these with what will be displayed on the tabs
  text.resize(mod_icon_.size());

  // for each option, look for the best column to place it in
  for (unsigned i = 0; i < vs_options.size(); i++) {
    std::string option = vs_options[i];
    int preferred_col = OptionToPreferredColumn(option);
    preferred_col = std::clamp(preferred_col, 0, (int)mod_icon_.size() - 1);

    // search for a vacant spot
    for (int j = preferred_col; j < NUM_OPTION_ICONS; j++) {
      if (text[j] != "") {
        continue;
      } else {
        text[j] = option;
        break;
      }
    }
  }

  for (unsigned i = 0; i < mod_icon_.size(); i++) {
    mod_icon_[i]->Set(text[i]);
  }
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the ModIconRow. */
class LunaModIconRow : public Luna<ModIconRow> {
 public:
  static int Load(T* p, lua_State* L) {
    p->Load(SArg(1), Enum::Check<PlayerNumber>(L, 2));
    COMMON_RETURN_SELF;
  }

  LunaModIconRow() { ADD_METHOD(Load); }
};

LUA_REGISTER_DERIVED_CLASS(ModIconRow, ActorFrame)

// lua end

/*
 * (c) 2002-2004 Chris Danford
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
