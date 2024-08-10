#include "global.h"
#include "GamePreferences.h"
#include "GameState.h"
#include "Player.h"
#include "PlayerState.h"
#include "ScreenDimensions.h"
#include "Style.h"
#include "EditModePlayerManager.h"


void EditModePlayerManager::AddPlayers(const NoteData& note_data) {
	int enabled_players = 0;
	FOREACH_EnabledPlayer(pn){
		players_[pn] = std::make_shared<PlayerPlus>();

		PlayerPlus& player = *players_[pn];
		player->Init("Player", GAMESTATE->m_pPlayerState[pn], nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
		player.Load(note_data);

		player->CacheAllUsedNoteSkins();
		GAMESTATE->m_pPlayerState[pn]->m_PlayerController = PC_HUMAN;
		player->SetXY(SCREEN_CENTER_X, SCREEN_CENTER_Y);
		player->SetZoom(SCREEN_HEIGHT / 480);
		enabled_players++;
	}

	// If 2p, slide each notefield left and right.
	if (enabled_players > 1) {
		(*players_[PLAYER_1])->SetXY((SCREEN_LEFT + SCREEN_CENTER_X) / 2, SCREEN_CENTER_Y);
		(*players_[PLAYER_2])->SetXY((SCREEN_RIGHT + SCREEN_CENTER_X) / 2, SCREEN_CENTER_Y);
	}

	SetVisible(false);
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

bool EditModePlayerManager::HandleGameplayInput(const InputEventPlus& input, const GameButtonType& gbt) {
	if (gbt == GameButtonType_Step && GAMESTATE->IsPlayerEnabled(input.pn)) {
		if (GAMESTATE->m_pPlayerState[input.pn]->m_PlayerController == PC_AUTOPLAY) {
			return false;
		}
		const int iCol = GAMESTATE->GetCurrentStyle(input.pn)->GameInputToColumn(input.GameI);
		if (iCol != -1) {
			(*players_[input.pn])->Step(iCol, -1, input.DeviceI.ts, false, input.type == IET_RELEASE);
		}
		return true;
	}
	return false;
}

void EditModePlayerManager::SetupAutoplay() {
	for (auto& player : players_) {
		if (GAMESTATE->m_pPlayerState[player.first]->m_PlayerOptions.GetCurrent().m_fPlayerAutoPlay != 0) {
			GAMESTATE->m_pPlayerState[player.first]->m_PlayerController = PC_AUTOPLAY;
		}
		else {
			GAMESTATE->m_pPlayerState[player.first]->m_PlayerController = GamePreferences::m_AutoPlay;
		}
	}
}

void EditModePlayerManager::CacheAllUsedNoteSkins() {
	for (auto& player : players_) {
		(*player.second)->CacheAllUsedNoteSkins();
	}
}
