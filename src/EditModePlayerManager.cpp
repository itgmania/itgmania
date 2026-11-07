#include "EditModePlayerManager.h"

#include <memory>

#include "ActorFrame.h"
#include "GameConstantsAndTypes.h"
#include "GamePreferences.h"
#include "GameState.h"
#include "GameplayAssist.h"
#include "InputEventPlus.h"
#include "InputFilter.h"
#include "ModsGroup.h"
#include "Player.h"
#include "PlayerNumber.h"
#include "PlayerOptions.h"
#include "PlayerState.h"
#include "RageUtil.h"
#include "ScreenDimensions.h"
#include "Style.h"
#include "ThemeManager.h"

void EditModePlayerManager::AddPlayers(const NoteData& note_data) {
  FOREACH_EnabledPlayer(pn) {
    players_[pn] = std::make_shared<PlayerPlus>();

    PlayerPlus& player = *players_[pn];
    player->Init(
        "Player", GAMESTATE->player_state_[pn], nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr);
    player.Load(note_data);

    // We set the actor alias here instead of the name to maintain
    // backward compatability with scripted stepcharts that expect
    // the Player's actor frame to be unnamed.
    player->SetAlias("Player" + PlayerNumberToString(pn));

    if ((GAMESTATE->GetCurrentStyle(GAMESTATE->GetMasterPlayerNumber())
             ->style_type_ == StyleType_TwoPlayersSharedSides) &&
        pn != PLAYER_1) {
      // Ensure P2 has the same PlayerOptions (speed, mini, modifiers, etc)
      // but ensure each player has their own noteskin.
      const std::string preferred_noteskin =
          GAMESTATE->player_state_[pn]
              ->player_options_.GetPreferred()
              .note_skin_;
      player->GetPlayerState()->player_options_ =
          GAMESTATE->player_state_[PLAYER_1]->player_options_;
      if (!preferred_noteskin.empty()) {
        PO_GROUP_ASSIGN(
            player->GetPlayerState()->player_options_, ModsLevel_Preferred,
            note_skin_, preferred_noteskin);
      }
    }
    player->CacheAllUsedNoteSkins();
    GAMESTATE->player_state_[pn]->player_controller_ = PC_HUMAN;
    player->SetY(SCREEN_CENTER_Y);
    player->SetZoom(SCREEN_HEIGHT / 480);
    StyleType style_type = GAMESTATE->GetCurrentStyle(pn)->style_type_;
    if (center_) {
      // Use the doubles style positioning for centering.
      style_type = StyleType_OnePlayerTwoSides;
    }
    player->SetX(THEME->GetMetricF(
        "ScreenGameplay",
        ssprintf(
            "PlayerP%d%sX", pn + 1, StyleTypeToString(style_type).c_str())));

    // Initial state is to hide the notefield.
    player->SetVisible(false);
  }
}

void EditModePlayerManager::AddPlayersToActorFrame(ActorFrame& frame) {
  for (auto& player : players_) {
    frame.AddChild(*player.second);
  }
}

void EditModePlayerManager::ReloadNoteData(const NoteData& note_data) {
  for (auto& player : players_) {
    player.second->Load(note_data);
  }
}

void EditModePlayerManager::SetVisible(bool visible) {
  for (auto& player : players_) {
    (*player.second)->SetVisible(visible);
  }
}

bool EditModePlayerManager::HandleGameplayInput(
    const InputEventPlus& input, const GameButtonType& gbt) {
  PlayerNumber pn = input.pn_;

  if (gbt == GameButtonType_Step && GAMESTATE->IsPlayerEnabled(pn)) {
    if (GAMESTATE->player_state_[pn]->player_controller_ == PC_AUTOPLAY) {
      return false;
    }
    const int col =
        GAMESTATE->GetCurrentStyle(pn)->GameInputToColumn(input.game_i_);
    if (col != -1) {
      // Pass inputs for both players to Player 1 in routine style.
      if (GAMESTATE->GetCurrentStyle(GAMESTATE->GetMasterPlayerNumber())
              ->style_type_ == StyleType_TwoPlayersSharedSides) {
        (*players_[PLAYER_1])
            ->Step(
                col, -1, input.device_i_.ts_, false,
                input.type_ == IET_RELEASE);
        (*players_[PLAYER_2])
            ->Step(
                col, -1, input.device_i_.ts_, false,
                input.type_ == IET_RELEASE);
      } else {
        (*players_[pn])
            ->Step(
                col, -1, input.device_i_.ts_, false,
                input.type_ == IET_RELEASE);
      }
    }
    return true;
  }
  return false;
}

void EditModePlayerManager::SetupAutoplay() {
  for (auto& player : players_) {
    if (GAMESTATE->player_state_[player.first]
            ->player_options_.GetCurrent()
            .player_auto_play_ != 0) {
      GAMESTATE->player_state_[player.first]->player_controller_ = PC_AUTOPLAY;
    } else {
      GAMESTATE->player_state_[player.first]->player_controller_ =
          GamePreferences::m_AutoPlay;
    }
  }
}

void EditModePlayerManager::CacheAllUsedNoteSkins() {
  for (auto& player : players_) {
    (*player.second)->CacheAllUsedNoteSkins();
  }
}

void EditModePlayerManager::PlayTicks(GameplayAssist& gameplay_assist) {
  for (auto& player : players_) {
    // Edit mode does not support 2p practicing different steps, so
    // enabling one set of ticks is fine.
    gameplay_assist.PlayTicks(
        (*player.second)->GetNoteData(), (*player.second)->GetPlayerState());
    return;
  }
}
