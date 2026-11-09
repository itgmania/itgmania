#include "ScreenGameplayLesson.h"

#include <string>
#include <vector>

#include "Actor.h"
#include "AutoActor.h"
#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "GamePreferences.h"
#include "GameState.h"
#include "InputEventPlus.h"
#include "LuaManager.h"
#include "LuaReference.h"
#include "MessageManager.h"
#include "PlayerNumber.h"
#include "PlayerStageStats.h"
#include "RageUtil.h"
#include "Screen.h"
#include "ScreenGameplay.h"
#include "ScreenGameplayNormal.h"
#include "ScreenMessage.h"
#include "Song.h"
#include "StatsManager.h"
#include "global.h"

REGISTER_SCREEN_CLASS(ScreenGameplayLesson);
ScreenGameplayLesson::ScreenGameplayLesson() {
  current_page_index_ = 0;
  try_ = Try_1;
}

void ScreenGameplayLesson::Init() {
  ASSERT(
      GAMESTATE->GetCurrentStyle(GAMESTATE->GetMasterPlayerNumber()) !=
      nullptr);
  ASSERT(GAMESTATE->cur_song_ != nullptr);

  /* Now that we've set up, init the base class. */
  ScreenGameplayNormal::Init();

  ClearMessageQueue();  // remove all of the messages set in ScreenGameplay that
                        // animate "ready", "here we go", etc.

  GAMESTATE->gameplay_lead_in_.Set(false);

  dancing_state_ = STATE_DANCING;

  // Load pages
  Song* song = GAMESTATE->cur_song_;
  std::string dir = song->GetSongDir();
  std::vector<std::string> vs;
  GetDirListing(dir + "Page*", vs, true, true);
  pages_.resize(vs.size());
  int i = 0;
  for (const std::string& s : vs) {
    AutoActor& aa = pages_[i];

    LuaThreadVariable index("PageIndex", LuaReference::Create(i++));
    LuaThreadVariable pages("NumPages", LuaReference::Create((int)vs.size()));
    aa.Load(s);

    aa->SetDrawOrder(kDrawOrderOverlay + 1);
    this->AddChild(aa);
  }

  i = 0;
  for (AutoActor& aa : pages_) {
    bool is_first = (i++ == 0);
    aa->PlayCommand(is_first ? "Show" : "Hide");
    aa->PlayCommand("On");
  }

  // Reset stage number (not relevant in lessons)
  GAMESTATE->current_stage_index_ = 0;
  FOREACH_ENUM(PlayerNumber, p)
  GAMESTATE->player_stage_tokens_[p] = 1;

  // Autoplay during demonstration
  FOREACH_EnabledPlayerInfo(player_info_, pi) pi->GetPlayerState()
      ->player_controller_ = PC_AUTOPLAY;
}

bool ScreenGameplayLesson::Input(const InputEventPlus& input) {
  // LOG->Trace( "ScreenGameplayLesson::Input()" );

  if (current_page_index_ != -1) {
    // show a lesson page
    return Screen::Input(input);
  } else {
    // in the "your turn" section"
    return ScreenGameplay::Input(input);
  }
}

void ScreenGameplayLesson::HandleScreenMessage(const ScreenMessage sm) {
  if (sm == SM_NotesEnded) {
    bool showing_a_page = current_page_index_ != -1;

    // While showing a page, loop the music.
    if (showing_a_page) {
      ResetAndRestartCurrentSong();
    } else {
      PlayerStageStats& pss = STATSMAN->cur_stage_stats_.player_[PLAYER_1];
      int actual = pss.GetLessonScoreActual();
      int needed = pss.GetLessonScoreNeeded();
      bool cleared = actual >= needed;
      bool any_tries_left = try_ + 1 < NUM_Try;

      if (cleared) {
        MESSAGEMAN->Broadcast(Message_LessonCleared);
        this->HandleScreenMessage(SM_LeaveGameplay);

        // Commit scores here since we don't go through an eval screen.
        // Only commit if we've cleared.  Don't commit if we've failed all 3
        // tries.
        STATSMAN->cur_stage_stats_.FinalizeScores(false);
      } else if (any_tries_left) {
        ResetAndRestartCurrentSong();

        try_ = (Try)(try_ + 1);
        MESSAGEMAN->Broadcast(
            (MessageID)(Message_LessonTry1 + Enum::to_integral(try_)));
      } else {
        this->HandleScreenMessage(SM_BeginFailed);
        MESSAGEMAN->Broadcast(Message_LessonFailed);
      }
    }
    return;  // handled
  }

  ScreenGameplay::HandleScreenMessage(sm);
}

bool ScreenGameplayLesson::MenuStart(const InputEventPlus& input) {
  // XXX: Allow repeats?
  if (current_page_index_ == -1) {
    return false;
  }
  ChangeLessonPage(+1);
  return true;
}

bool ScreenGameplayLesson::MenuBack(const InputEventPlus& input) {
  // XXX: Allow repeats?
  if (current_page_index_ == 0) {
    BeginBackingOutFromGameplay();
    return true;
  }
  if (current_page_index_ == -1) {
    return false;
  }
  ChangeLessonPage(-1);
  return true;
}

void ScreenGameplayLesson::ChangeLessonPage(int dir) {
  if (current_page_index_ + dir < 0) {
    // don't change
    return;
  } else if (current_page_index_ + dir >= (int)pages_.size()) {
    pages_[current_page_index_]->PlayCommand("Hide");
    current_page_index_ = -1;

    ResetAndRestartCurrentSong();

    MESSAGEMAN->Broadcast(
        (MessageID)(Message_LessonTry1 + Enum::to_integral(try_)));

    // Change back to the current autoplay setting (in most cases, human
    // controlled).
    FOREACH_EnabledPlayerInfo(player_info_, pi) {
      /* Mike: I'm not sure if you can actually have the player options enabled
       * in this mode, but I'm putting the check here anyways to play it safe.
       * If it's unnecessary, then it should be removed. */
      if (pi->GetPlayerState()
              ->player_options_.GetCurrent()
              .player_auto_play_ != 0) {
        pi->GetPlayerState()->player_controller_ = PC_AUTOPLAY;
      } else {
        pi->GetPlayerState()->player_controller_ = GamePreferences::auto_play;
      }
    }
  } else {
    pages_[current_page_index_]->PlayCommand("Hide");
    current_page_index_ += dir;
    pages_[current_page_index_]->PlayCommand("Show");
  }
}

void ScreenGameplayLesson::ResetAndRestartCurrentSong() {
  STATSMAN->cur_stage_stats_.player_[PLAYER_1].ResetScoreForLesson();
  sound_music_->Stop();
  ReloadCurrentSong();
  StartPlayingSong(2, 0);
}

/*
 * (c) 2003-2004 Chris Danford
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
