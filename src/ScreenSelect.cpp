#include "ScreenSelect.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "Command.h"
#include "GameCommand.h"
#include "GameInput.h"
#include "GameSoundManager.h"
#include "GameState.h"
#include "InputEventPlus.h"
#include "InputFilter.h"
#include "LuaManager.h"
#include "MessageManager.h"
#include "PlayerNumber.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "ScreenManager.h"
#include "ScreenMessage.h"
#include "ScreenWithMenuElements.h"
#include "StdString.h"
#include "ThemeManager.h"
#include "global.h"

#define CHOICE_NAMES THEME->GetMetric(name_, "ChoiceNames")
#define CHOICE(s) THEME->GetMetric(name_, ssprintf("Choice%s", s.c_str()))
#define IDLE_TIMEOUT_SCREEN THEME->GetMetric(name_, "IdleTimeoutScreen")
#define UPDATE_ON_MESSAGE THEME->GetMetric(name_, "UpdateOnMessage")

void ScreenSelect::Init() {
  IDLE_COMMENT_SECONDS.Load(name_, "IdleCommentSeconds");
  IDLE_TIMEOUT_SECONDS.Load(name_, "IdleTimeoutSeconds");
  ALLOW_DISABLED_PLAYER_INPUT.Load(name_, "AllowDisabledPlayerInput");

  ScreenWithMenuElements::Init();

  // Load messages to update on
  split(UPDATE_ON_MESSAGE, ",", subscribed_messages_);
  for (unsigned i = 0; i < subscribed_messages_.size(); ++i) {
    MESSAGEMAN->Subscribe(this, subscribed_messages_[i]);
  }
  // Subscribe to PlayerJoined, if not already.
  if (!MESSAGEMAN->IsSubscribedToMessage(this, Message_PlayerJoined)) {
    this->SubscribeToMessage(Message_PlayerJoined);
  }

  // Load choices
  // Allow lua as an alternative to metrics.
  std::string choice_names = CHOICE_NAMES;
  if (Left(choice_names, 4) == "lua,") {
    std::string command = Right(choice_names, choice_names.size() - 4);
    Lua* L = LUA->Get();
    if (LuaHelpers::RunExpression(L, command, name_ + "::ChoiceNames")) {
      if (!lua_istable(L, 1)) {
        LuaHelpers::ReportScriptError(
            name_ +
            "::ChoiceNames expression did not return a table of gamecommands.");
      } else {
        size_t len = lua_objlen(L, 1);
        for (size_t i = 1; i <= len; ++i) {
          lua_rawgeti(L, 1, i);
          if (!lua_isstring(L, -1)) {
            LuaHelpers::ReportScriptErrorFmt(
                (name_ + "::ChoiceNames element %zu is not a string.").c_str(),
                i);
          } else {
            std::string com = SArg(-1);
            GameCommand mc;
            mc.ApplyCommitsScreens(false);
            mc.name_ = ssprintf("%zu", i);
            Commands cmd = ParseCommands(com);
            mc.Load(i, cmd);
            game_commands_.push_back(mc);
          }
          lua_pop(L, 1);
        }
      }
    }
    lua_settop(L, 0);
    LUA->Release(L);
  } else {
    // Instead of using NUM_CHOICES, use a comma-separated list of choices.
    // Each element in the list is a choice name. This level of indirection
    // makes it easier to add or remove items without having to change a
    // bunch of indices.
    std::vector<std::string> as_choice_names;
    split(CHOICE_NAMES, ",", as_choice_names, true);

    for (unsigned c = 0; c < as_choice_names.size(); c++) {
      std::string choice_name = as_choice_names[c];

      GameCommand mc;
      mc.ApplyCommitsScreens(false);
      mc.name_ = choice_name;
      Commands cmd = ParseCommands(CHOICE(choice_name));
      mc.Load(c, cmd);
      game_commands_.push_back(mc);
    }
  }

  if (game_commands_.empty()) {
    LuaHelpers::ReportScriptErrorFmt(
        "Screen \"%s\" does not set any choices.", name_.c_str());
  }
}

void ScreenSelect::BeginScreen() {
  ScreenWithMenuElements::BeginScreen();

  timer_idle_comment_.GetDeltaTime();
  timer_idle_timeout_.GetDeltaTime();
}

ScreenSelect::~ScreenSelect() {
  LOG->Trace("ScreenSelect::~ScreenSelect()");
  for (unsigned i = 0; i < subscribed_messages_.size(); ++i) {
    MESSAGEMAN->Unsubscribe(this, subscribed_messages_[i]);
  }
}

void ScreenSelect::Update(float delta) {
  if (!IsTransitioning()) {
    if (IDLE_COMMENT_SECONDS > 0 &&
        timer_idle_comment_.Ago() >= IDLE_COMMENT_SECONDS) {
      SOUND->PlayOnceFromAnnouncer(name_ + " IdleComment");
      timer_idle_comment_.GetDeltaTime();
    }

    if (IDLE_TIMEOUT_SECONDS > 0 &&
        timer_idle_timeout_.Ago() >= IDLE_TIMEOUT_SECONDS) {
      SCREENMAN->SetNewScreen(IDLE_TIMEOUT_SCREEN);
      timer_idle_timeout_.GetDeltaTime();
      return;
    }
  }

  ScreenWithMenuElements::Update(delta);
}

bool ScreenSelect::Input(const InputEventPlus& input) {
  //	LOG->Trace( "ScreenSelect::Input()" );

  /* Reset the announcer timers when a key is pressed. */
  timer_idle_comment_.GetDeltaTime();
  timer_idle_timeout_.GetDeltaTime();

  /* Choices may change when more coins are inserted. */
  if (input.menu_i_ == GAME_BUTTON_COIN && input.type_ == IET_FIRST_PRESS) {
    this->UpdateSelectableChoices();
  }

  if (input.menu_i_ == GAME_BUTTON_START && input.type_ == IET_FIRST_PRESS &&
      GAMESTATE->JoinInput(input.pn_)) {
    // HACK: Only play start sound for the 2nd player who joins. The
    // start sound for the 1st player will be played by ScreenTitleMenu
    // when the player makes a selection on the screen.
    if (GAMESTATE->GetNumSidesJoined() > 1) {
      SCREENMAN->PlayStartSound();
    }

    if (!ALLOW_DISABLED_PLAYER_INPUT) {
      return false;  // don't let the screen handle the MENU_START press
    }
  }

  if (!GAMESTATE->IsPlayerEnabled(input.pn_)) {
    // block input of disabled players
    if (!ALLOW_DISABLED_PLAYER_INPUT) {
      return false;
    }

    /* Never allow a START press by a player that's still not joined, even if
     * ALLOW_DISABLED_PLAYER_INPUT would allow other types of input. If we
     * let a non-joined player start, we might start the game with no
     * players joined (eg. if ScreenTitleJoin is started in pay with no
     * credits). */
    if (input.menu_i_ == GAME_BUTTON_START) {
      return false;
    }
  }

  return ScreenWithMenuElements::Input(input);  // default input handler
}

void ScreenSelect::HandleScreenMessage(const ScreenMessage sm) {
  if (sm == SM_BeginFadingOut)  // Screen is starting to tween out.
  {
    /* Don't call GameCommand::Apply once per player on screens that
     * have a shared selection. This can cause change messages to be
     * broadcast multiple times. Detect whether all players have the
     * same choice, and  if so, call ApplyToAll instead.
     * TODO: Think of a better way to handle this.
     */
    ASSERT(GAMESTATE->GetMasterPlayerNumber() != PlayerNumber_Invalid);
    int masters_index =
        this->GetSelectionIndex(GAMESTATE->GetMasterPlayerNumber());
    bool all_players_chose_the_same = true;
    FOREACH_HumanPlayer(p) {
      if (this->GetSelectionIndex(p) != masters_index) {
        all_players_chose_the_same = false;
        break;
      }
    }

    if (!game_commands_.empty()) {
      if (all_players_chose_the_same) {
        const GameCommand& gc = game_commands_[masters_index];
        next_screen_ = gc.screen_;
        if (!gc.invalid_) {
          gc.ApplyToAllPlayers();
        }
      } else {
        FOREACH_HumanPlayer(p) {
          int index = this->GetSelectionIndex(p);
          const GameCommand& gc = game_commands_[index];
          next_screen_ = gc.screen_;
          if (!gc.invalid_) {
            gc.Apply(p);
          }
        }
      }
    }
    StopTimer();

    SCREENMAN->RefreshCreditsMessages();

    if (IsTransitioning()) {
      // If the player pressed Start and Back on the same frame, the
      // Cancel transition can have started after the
      // SM_BeginFadingOut message was queued.
      LOG->Warn("Still transitioning when playing SM_GoToNextScreen");
    } else {
      StartTransitioningScreen(SM_GoToNextScreen);
    }
  }

  ScreenWithMenuElements::HandleScreenMessage(sm);
}

void ScreenSelect::HandleMessage(const Message& msg) {
  if (find(
          subscribed_messages_.begin(), subscribed_messages_.end(),
          msg.GetName()) != subscribed_messages_.end()) {
    this->UpdateSelectableChoices();
  }

  ScreenWithMenuElements::HandleMessage(msg);
}

bool ScreenSelect::MenuBack(const InputEventPlus& input) {
  Cancel(SM_GoToPrevScreen);
  return true;
}

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
