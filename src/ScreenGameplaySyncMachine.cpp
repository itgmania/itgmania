#include "ScreenGameplaySyncMachine.h"

#include <string>
#include <vector>

#include "ActorUtil.h"
#include "AdjustSync.h"
#include "GameConstantsAndTypes.h"
#include "GameInput.h"
#include "GameManager.h"
#include "GamePreferences.h"
#include "GameState.h"
#include "InputEventPlus.h"
#include "LocalizedString.h"
#include "MessageManager.h"
#include "ModsGroup.h"
#include "NotesLoaderSM.h"
#include "NotesLoaderSSC.h"
#include "PlayerNumber.h"
#include "PrefsManager.h"
#include "RageUtil.h"
#include "Screen.h"
#include "ScreenGameplay.h"
#include "ScreenGameplayNormal.h"
#include "ScreenMessage.h"
#include "SongOptions.h"
#include "SongUtil.h"
#include "StdString.h"
#include "ThemeManager.h"
#include "global.h"

REGISTER_SCREEN_CLASS(ScreenGameplaySyncMachine);

void ScreenGameplaySyncMachine::Init() {
  GAMESTATE->play_mode_.Set(PLAY_MODE_REGULAR);
  GAMESTATE->SetCurrentStyle(
      GAMEMAN->GetHowToPlayStyleForGame(GAMESTATE->cur_game_), PLAYER_INVALID);
  AdjustSync::ResetOriginalSyncData();

  std::string file = THEME->GetPathO("ScreenGameplaySyncMachine", "music");
  // Allow themers to use either a .ssc or .sm file for this. -aj
  SSCLoader loader_ssc;
  SMLoader loader_sm;
  if (Right(file, 4) == ".ssc") {
    loader_ssc.LoadFromSimfile(file, song_);
  } else {
    loader_sm.LoadFromSimfile(file, song_);
  }

  song_.SetSongDir(Dirname(file));
  song_.TidyUpData();

  GAMESTATE->cur_song_.Set(&song_);
  // Needs proper StepsType -freem
  std::vector<Steps*> vp_steps;
  SongUtil::GetPlayableSteps(&song_, vp_steps);
  ASSERT_M(
      vp_steps.size() > 0, "No playable steps for ScreenGameplaySyncMachine");
  Steps* p_steps = vp_steps[0];
  GAMESTATE->cur_steps_[GAMESTATE->GetFirstHumanPlayer()].Set(p_steps);

  GamePreferences::m_AutoPlay.Set(PC_HUMAN);

  ScreenGameplayNormal::Init();

  SO_GROUP_ASSIGN(
      GAMESTATE->song_options_, ModsLevel_Stage, autosync_type_,
      AutosyncType_Machine);

  ClearMessageQueue();  // remove all of the messages set in ScreenGameplay that
                        // animate "ready", "here we go", etc.

  GAMESTATE->gameplay_lead_in_.Set(false);

  dancing_state_ = STATE_DANCING;

  text_sync_info_.SetName("SyncInfo");
  text_sync_info_.LoadFromFont(THEME->GetPathF(name_, "SyncInfo"));
  ActorUtil::LoadAllCommands(text_sync_info_, name_);
  this->AddChild(&text_sync_info_);

  this->SubscribeToMessage(Message_AutosyncChanged);

  RefreshText();
}

void ScreenGameplaySyncMachine::Update(float delta) {
  ScreenGameplayNormal::Update(delta);
  RefreshText();
}

bool ScreenGameplaySyncMachine::Input(const InputEventPlus& input) {
  // Hack to make this work from Player2's controls
  InputEventPlus _input = input;

  if (_input.game_i_.controller_ != GameController_Invalid) {
    _input.game_i_.controller_ = GameController_1;
  }
  if (_input.pn_ != PLAYER_INVALID) {
    _input.pn_ = PLAYER_1;
  }

  return ScreenGameplay::Input(_input);
}

void ScreenGameplaySyncMachine::HandleScreenMessage(const ScreenMessage sm) {
  if (sm == SM_NotesEnded) {
    ResetAndRestartCurrentSong();
    return;  // handled
  }

  ScreenGameplayNormal::HandleScreenMessage(sm);

  if (sm == SM_GoToPrevScreen || sm == SM_GoToNextScreen) {
    FOREACH_PlayerNumber(pn) { GAMESTATE->cur_steps_[pn].Set(nullptr); }
    GAMESTATE->play_mode_.Set(PlayMode_Invalid);
    GAMESTATE->SetCurrentStyle(nullptr, PLAYER_INVALID);
    GAMESTATE->cur_song_.Set(nullptr);
  }
}

void ScreenGameplaySyncMachine::ResetAndRestartCurrentSong() {
  sound_music_->Stop();
  ReloadCurrentSong();
  StartPlayingSong(4, 0);

  // reset autosync
  AdjustSync::ResetAutosync();
}

static LocalizedString OLD_OFFSET("ScreenGameplaySyncMachine", "Old offset");
static LocalizedString NEW_OFFSET("ScreenGameplaySyncMachine", "New offset");
static LocalizedString COLLECTING_SAMPLE(
    "ScreenGameplaySyncMachine", "Collecting sample");
static LocalizedString STANDARD_DEVIATION(
    "ScreenGameplaySyncMachine", "Standard deviation");
void ScreenGameplaySyncMachine::RefreshText() {
  float f_new = PREFSMAN->global_offset_seconds_;
  float old = AdjustSync::global_offset_seconds_original_;
  float std_dev = AdjustSync::standard_deviation_;
  std::string s;
  s += OLD_OFFSET.GetValue() + ssprintf(": %0.3f\n", old);
  s += NEW_OFFSET.GetValue() + ssprintf(": %0.3f\n", f_new);
  s += STANDARD_DEVIATION.GetValue() + ssprintf(": %0.3f\n", std_dev);
  s += COLLECTING_SAMPLE.GetValue() +
       ssprintf(
           ": %d / %d", AdjustSync::autosync_offset_sample_ + 1,
           AdjustSync::kOffsetSampleCount);

  text_sync_info_.SetText(s);
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
