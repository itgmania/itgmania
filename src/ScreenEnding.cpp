#include "ScreenEnding.h"

#include <cmath>

#include "ActorUtil.h"
#include "AnnouncerManager.h"
#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "GameManager.h"
#include "GameSoundManager.h"
#include "GameState.h"
#include "InputEventPlus.h"
#include "MemoryCardManager.h"
#include "ModsGroup.h"
#include "PlayerNumber.h"
#include "PlayerState.h"
#include "PrefsManager.h"
#include "Profile.h"
#include "ProfileManager.h"
#include "RageUtil.h"
#include "RageUtil/RandomNumbers.h"
#include "Screen.h"
#include "ScreenAttract.h"
#include "ScreenManager.h"
#include "ScreenMessage.h"
#include "Song.h"
#include "SongManager.h"
#include "StatsManager.h"
#include "ThemeManager.h"

REGISTER_SCREEN_CLASS(ScreenEnding);
ScreenEnding::ScreenEnding() {
  if (PREFSMAN->test_initial_screen_.Get() != "") {
    PROFILEMAN->LoadFirstAvailableProfile(PLAYER_1);
    PROFILEMAN->LoadFirstAvailableProfile(PLAYER_2);

    GAMESTATE->play_mode_.Set(PLAY_MODE_REGULAR);
    GAMESTATE->SetCurrentStyle(
        GAMEMAN->GameAndStringToStyle(GAMEMAN->GetDefaultGame(), "versus"),
        PLAYER_INVALID);
    GAMESTATE->JoinPlayer(PLAYER_1);
    GAMESTATE->JoinPlayer(PLAYER_2);
    GAMESTATE->cur_song_.Set(SONGMAN->GetRandomSong());
    GAMESTATE->cur_course_.Set(SONGMAN->GetRandomCourse());
    GAMESTATE->cur_steps_[PLAYER_1].Set(GAMESTATE->cur_song_->GetAllSteps()[0]);
    GAMESTATE->cur_steps_[PLAYER_2].Set(GAMESTATE->cur_song_->GetAllSteps()[0]);
    STATSMAN->cur_stage_stats_.player_[PLAYER_1].possible_steps_.push_back(
        GAMESTATE->cur_steps_[PLAYER_1]);
    STATSMAN->cur_stage_stats_.player_[PLAYER_2].possible_steps_.push_back(
        GAMESTATE->cur_steps_[PLAYER_2]);
    STATSMAN->cur_stage_stats_.player_[PLAYER_1].steps_played_ = 1;
    STATSMAN->cur_stage_stats_.player_[PLAYER_2].steps_played_ = 1;
    PO_GROUP_ASSIGN(
        GAMESTATE->player_state_[PLAYER_1]->player_options_, ModsLevel_Stage,
        scroll_speed_, 2.0f);
    PO_GROUP_ASSIGN(
        GAMESTATE->player_state_[PLAYER_2]->player_options_, ModsLevel_Stage,
        scroll_speed_, 2.0f);
    GAMESTATE->current_stage_index_ = 0;
    FOREACH_ENUM(PlayerNumber, p)
    GAMESTATE->player_stage_tokens_[p] = 1;
    PO_GROUP_CALL(
        GAMESTATE->player_state_[PLAYER_1]->player_options_, ModsLevel_Stage,
        ChooseRandomModifiers);
    PO_GROUP_CALL(
        GAMESTATE->player_state_[PLAYER_2]->player_options_, ModsLevel_Stage,
        ChooseRandomModifiers);

    for (float f = 0; f < 100.0f; f += 1.0f) {
      float fP1 = std::fmod(f / 100 * 4 + .3f, 1);
      STATSMAN->cur_stage_stats_.player_[PLAYER_1].SetLifeRecordAt(fP1, f);
      STATSMAN->cur_stage_stats_.player_[PLAYER_2].SetLifeRecordAt(1 - fP1, f);
    }

    STATSMAN->cur_stage_stats_.player_[PLAYER_1].actual_dance_points_ =
        RandomInt(3);
    STATSMAN->cur_stage_stats_.player_[PLAYER_1].possible_dance_points_ = 2;
    STATSMAN->cur_stage_stats_.player_[PLAYER_2].actual_dance_points_ =
        RandomInt(2);
    STATSMAN->cur_stage_stats_.player_[PLAYER_2].possible_dance_points_ = 1;
    STATSMAN->cur_stage_stats_.player_[PLAYER_1].cur_combo_ = 0;
    STATSMAN->cur_stage_stats_.player_[PLAYER_1].UpdateComboList(0, false);
    STATSMAN->cur_stage_stats_.player_[PLAYER_1].cur_combo_ = 1;
    STATSMAN->cur_stage_stats_.player_[PLAYER_1].UpdateComboList(1, false);
    STATSMAN->cur_stage_stats_.player_[PLAYER_1].cur_combo_ = 50;
    STATSMAN->cur_stage_stats_.player_[PLAYER_1].UpdateComboList(25, false);
    STATSMAN->cur_stage_stats_.player_[PLAYER_1].cur_combo_ = 250;
    STATSMAN->cur_stage_stats_.player_[PLAYER_1].UpdateComboList(100, false);

    STATSMAN->cur_stage_stats_.player_[PLAYER_1].tap_note_scores_[TNS_W1] =
        RandomInt(2);
    STATSMAN->cur_stage_stats_.player_[PLAYER_1].tap_note_scores_[TNS_W2] =
        RandomInt(2);
    STATSMAN->cur_stage_stats_.player_[PLAYER_1].tap_note_scores_[TNS_W3] =
        RandomInt(2);
    STATSMAN->cur_stage_stats_.player_[PLAYER_2].tap_note_scores_[TNS_W1] =
        RandomInt(2);
    STATSMAN->cur_stage_stats_.player_[PLAYER_2].tap_note_scores_[TNS_W2] =
        RandomInt(2);
    STATSMAN->cur_stage_stats_.player_[PLAYER_2].tap_note_scores_[TNS_W3] =
        RandomInt(2);

    STATSMAN->played_stage_stats_.clear();
  }
}

void ScreenEnding::Init() {
  ScreenAttract::Init();

  FOREACH_HumanPlayer(p) {
    if (!PROFILEMAN->IsPersistentProfile(p)) {
      continue;
    }

    spr_remove_memory_card_[p].SetName(ssprintf("RemoveCardP%d", p + 1));
    spr_remove_memory_card_[p].Load(
        THEME->GetPathG("ScreenEnding", ssprintf("remove card P%d", p + 1)));
    switch (MEMCARDMAN->GetCardState(p)) {
      case MemoryCardState_Removed:
      case MemoryCardState_NoCard:
        spr_remove_memory_card_[p].SetVisible(false);
      default:
        break;
    }
    LOAD_ALL_COMMANDS_AND_SET_XY_AND_ON_COMMAND(spr_remove_memory_card_[p]);
    this->AddChild(&spr_remove_memory_card_[p]);
  }

  SOUND->PlayOnceFromDir(ANNOUNCER->GetPathTo("music scroll"));

  // Now that we've read the data from the profile, it's ok to Reset()
  GAMESTATE->Reset();
}

bool ScreenEnding::Input(const InputEventPlus& input) {
  bool handled = false;
  if (!IsTransitioning()) {
    switch (input.menu_i_) {
      case GAME_BUTTON_START:
        SCREENMAN->PostMessageToTopScreen(SM_BeginFadingOut, 0);
        handled = true;
      default:
        break;
    }
  }

  return ScreenAttract::Input(input) || handled;
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
