#include "ScreenContinue.h"

#include <algorithm>
#include <cmath>

#include "EnumHelper.h"
#include "GameInput.h"
#include "GameState.h"
#include "InputEventPlus.h"
#include "InputFilter.h"
#include "MemoryCardManager.h"
#include "MenuTimer.h"
#include "MessageManager.h"
#include "PlayerNumber.h"
#include "Screen.h"
#include "ScreenManager.h"
#include "ScreenMessage.h"
#include "ScreenWithMenuElements.h"

REGISTER_SCREEN_CLASS(ScreenContinue);

void ScreenContinue::Init() {
  ScreenWithMenuElementsSimple::Init();

  this->SubscribeToMessage(Message_PlayerJoined);

  FORCE_TIMER_WAIT.Load(name_, "ForceTimerWait");
}

void ScreenContinue::BeginScreen() {
  GAMESTATE->SetCurrentStyle(nullptr, kPlayerInvalid);

  // Unjoin human players with 0 stages left and reset non-human players.
  // We need to reset non-human players because data in non-human (CPU)
  // players will be filled, and there may be stale pointers to things like
  // edit Steps.
  FOREACH_ENUM(PlayerNumber, p) {
    if (GAMESTATE->IsHumanPlayer(p)) {
      bool player_done = GAMESTATE->player_stage_tokens_[p] <= 0;
      if (player_done) {
        GAMESTATE->UnjoinPlayer(p);
        MEMCARDMAN->UnlockCard(p);
      }
    } else {
      GAMESTATE->ResetPlayer(p);
    }
  }

  ScreenWithMenuElementsSimple::BeginScreen();
}

bool ScreenContinue::Input(const InputEventPlus& input) {
  if (input.menu_i_ == GAME_BUTTON_COIN && input.type_ == IET_FIRST_PRESS) {
    ResetTimer();
  }

  if (input.menu_i_ == GAME_BUTTON_START && input.type_ == IET_FIRST_PRESS &&
      GAMESTATE->JoinInput(input.pn_)) {
    return true;  // handled
  }

  if (IsTransitioning()) {
    return true;
  }

  if (input.type_ == IET_FIRST_PRESS && GAMESTATE->IsHumanPlayer(input.pn_) &&
      FORCE_TIMER_WAIT) {
    switch (input.menu_i_) {
      case GAME_BUTTON_START:
      case GAME_BUTTON_UP:
      case GAME_BUTTON_DOWN:
      case GAME_BUTTON_LEFT:
      case GAME_BUTTON_RIGHT: {
        float seconds = std::floor(menu_timer_->GetSeconds()) - 0.0001f;
        seconds = std::max(seconds, 0.0001f);  // don't set to 0
        menu_timer_->SetSeconds(seconds);
        Message msg("HurryTimer");
        msg.SetParam("PlayerNumber", input.pn_);
        this->HandleMessage(msg);
        return true;  // handled
      }
      default:
        break;
    }
  }

  return ScreenWithMenuElementsSimple::Input(input);
}

void ScreenContinue::HandleScreenMessage(const ScreenMessage sm) {
  if (sm == SM_MenuTimer) {
    if (!IsTransitioning()) {
      StartTransitioningScreen(SM_GoToNextScreen);
    }
    return;
  }

  ScreenWithMenuElementsSimple::HandleScreenMessage(sm);
}

void ScreenContinue::HandleMessage(const Message& msg) {
  if (msg == Message_PlayerJoined) {
    ResetTimer();

    bool all_players_are_enabled = true;
    FOREACH_ENUM(PlayerNumber, p) {
      if (!GAMESTATE->IsPlayerEnabled(p)) {
        all_players_are_enabled = false;
      }
    }

    if (all_players_are_enabled) {
      menu_timer_->Stop();
      if (!IsTransitioning()) {
        StartTransitioningScreen(SM_GoToNextScreen);
      }
    }
  }

  ScreenWithMenuElementsSimple::HandleMessage(msg);
}

/*
 * (c) 2004 Chris Danford
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
