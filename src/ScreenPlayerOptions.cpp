#include "ScreenPlayerOptions.h"

#include <algorithm>
#include <vector>

#include "ActorUtil.h"
#include "AnnouncerManager.h"
#include "CodeDetector.h"
#include "GameSoundManager.h"
#include "GameState.h"
#include "InputEventPlus.h"
#include "InputFilter.h"
#include "MessageManager.h"
#include "ModsGroup.h"
#include "PlayerNumber.h"
#include "PlayerState.h"
#include "PrefsManager.h"
#include "RageUtil.h"
#include "Screen.h"
#include "ScreenManager.h"
#include "ScreenMessage.h"
#include "ScreenOptions.h"
#include "ScreenOptionsMaster.h"
#include "ThemeManager.h"
#include "global.h"

REGISTER_SCREEN_CLASS(ScreenPlayerOptions);

void ScreenPlayerOptions::Init() {
  ScreenOptionsMaster::Init();

  FOREACH_PlayerNumber(p) {
    spr_disqualify_[p].Load(THEME->GetPathG(name_, "disqualify"));
    spr_disqualify_[p]->SetName(ssprintf("DisqualifyP%i", p + 1));
    LOAD_ALL_COMMANDS_AND_SET_XY(spr_disqualify_[p]);
    spr_disqualify_[p]->SetVisible(
        false);  // unhide later if handicapping options are discovered
    spr_disqualify_[p]->SetDrawOrder(2);
    frame_container_.AddChild(spr_disqualify_[p]);
  }

  ask_options_message_ =
      !GAMESTATE->IsEditing() && PREFSMAN->show_song_options_ == Maybe_ASK;

  accepted_choices_ = false;
  go_to_options_ = (PREFSMAN->show_song_options_ == Maybe_YES);

  SOUND->PlayOnceFromDir(ANNOUNCER->GetPathTo("player options intro"));

  FOREACH_PlayerNumber(p) row_causes_disqualified_[p].resize(
      rows_.size(), false);
}

void ScreenPlayerOptions::BeginScreen() {
  FOREACH_PlayerNumber(p) ON_COMMAND(spr_disqualify_[p]);

  ScreenOptionsMaster::BeginScreen();

  FOREACH_HumanPlayer(p) {
    for (unsigned r = 0; r < rows_.size(); r++) {
      UpdateDisqualified(r, p);
    }
  }
}

bool ScreenPlayerOptions::Input(const InputEventPlus& input) {
  bool handled = false;

  if (ask_options_message_ && input.type_ == IET_FIRST_PRESS &&
      !in_.IsTransitioning() && input.menu_i_ == GAME_BUTTON_START) {
    if (accepted_choices_ && !go_to_options_) {
      go_to_options_ = true;
      this->PlayCommand("GoToOptions");
      SCREENMAN->PlayStartSound();
      handled = true;
    }
  }

  PlayerNumber pn = input.pn_;
  if (GAMESTATE->IsHumanPlayer(pn) &&
      CodeDetector::EnteredCode(
          input.game_i_.controller_, CODE_CANCEL_ALL_PLAYER_OPTIONS)) {
    // apply the game default mods, but not the Profile saved mods
    GAMESTATE->player_state_[pn]->ResetToDefaultPlayerOptions(
        ModsLevel_Preferred);

    MESSAGEMAN->Broadcast(ssprintf("CancelAllP%i", pn + 1));

    for (unsigned r = 0; r < rows_.size(); r++) {
      std::vector<PlayerNumber> v;
      v.push_back(pn);
      int old_focus = rows_[r]->GetChoiceInRowWithFocus(pn);
      this->ImportOptions(r, v);
      rows_[r]->AfterImportOptions(pn);
      this->UpdateDisqualified(r, pn);
      rows_[r]->SetChoiceInRowWithFocus(pn, old_focus);
    }
    handled = true;
  }

  handled = ScreenOptionsMaster::Input(input) || handled;

  // UGLY: Update m_Disqualified whenever Start is pressed
  if (GAMESTATE->IsHumanPlayer(pn) && input.menu_i_ == GAME_BUTTON_START) {
    int row = current_row_[pn];
    UpdateDisqualified(row, pn);
  }
  return handled;
}

void ScreenPlayerOptions::HandleScreenMessage(const ScreenMessage sm) {
  if (sm == SM_BeginFadingOut &&
      ask_options_message_)  // user accepts the page of options
  {
    accepted_choices_ = true;

    // show "hold START for options"
    this->PlayCommand("AskForGoToOptions");
  }

  ScreenOptionsMaster::HandleScreenMessage(sm);
}

void ScreenPlayerOptions::UpdateDisqualified(int row, PlayerNumber pn) {
  ASSERT(GAMESTATE->IsHumanPlayer(pn));

  // save original player options
  PlayerOptions orig =
      GAMESTATE->player_state_[pn]->player_options_.GetPreferred();

  // Find out if the current row when exported causes disqualification.
  // Exporting the row will fill GAMESTATE->m_PlayerOptions.
  PO_GROUP_CALL(
      GAMESTATE->player_state_[pn]->player_options_, ModsLevel_Preferred, Init);
  std::vector<PlayerNumber> v;
  v.push_back(pn);
  ExportOptions(row, v);
  bool row_causes_disqualified = GAMESTATE->CurrentOptionsDisqualifyPlayer(pn);
  row_causes_disqualified_[pn][row] = row_causes_disqualified;

  // Update disqualified graphic
  bool disqualified = std::any_of(
      row_causes_disqualified_[pn].begin(), row_causes_disqualified_[pn].end(),
      [](const bool& b) { return b; });

  spr_disqualify_[pn]->SetVisible(disqualified);

  // restore previous player options in case the user escapes back after this
  GAMESTATE->player_state_[pn]->player_options_.Assign(
      ModsLevel_Preferred, orig);
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the ScreenPlayerOptions. */
class LunaScreenPlayerOptions : public Luna<ScreenPlayerOptions> {
 public:
  static int GetGoToOptions(T* p, lua_State* L) {
    lua_pushboolean(L, p->GetGoToOptions());
    return 1;
  }

  LunaScreenPlayerOptions() { ADD_METHOD(GetGoToOptions); }
};

LUA_REGISTER_DERIVED_CLASS(ScreenPlayerOptions, ScreenOptions)
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
