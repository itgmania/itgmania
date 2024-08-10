#include "global.h"
#include "GameplayAssist.h"
#include "GamePreferences.h"
#include "GameState.h"
#include "Player.h"
#include "PlayerState.h"
#include "ScreenDimensions.h"
#include "Style.h"
#include "EditModePlayerManager.h"


void EditModePlayerManager::AddPlayers(const NoteData& note_data) {
	FOREACH_EnabledPlayer(pn){
		players_[pn] = std::make_shared<PlayerPlus>();

		PlayerPlus& player = *players_[pn];
		player->Init("Player", GAMESTATE->m_pPlayerState[pn], nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
		player.Load(note_data);

		player->CacheAllUsedNoteSkins();
		GAMESTATE->m_pPlayerState[pn]->m_PlayerController = PC_HUMAN;
		player->SetY(SCREEN_CENTER_Y);
		player->SetZoom(SCREEN_HEIGHT / 480);
		StyleType style_type = GAMESTATE->GetCurrentStyle(pn)->m_StyleType;
		player->SetX(THEME->GetMetricF("ScreenGameplay", ssprintf("PlayerP%d%sX", pn + 1, StyleTypeToString(style_type).c_str())));
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


void EditModePlayerManager::PlayTicks(GameplayAssist& gameplay_assist) {
	for (auto& player : players_) {
		// Edit mode does not support 2p practicing different steps, so
		// enabling one set of ticks is fine.
		gameplay_assist.PlayTicks((*player.second)->GetNoteData(), (*player.second)->GetPlayerState());
		return;
	}
	
}
