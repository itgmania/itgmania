#include "LifeMeterBattery.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "ActorUtil.h"
#include "Course.h"
#include "GameConstantsAndTypes.h"
#include "GameState.h"
#include "LifeMeter.h"
#include "LuaManager.h"
#include "MessageManager.h"
#include "PlayerNumber.h"
#include "PlayerStageStats.h"
#include "PlayerState.h"
#include "RageUtil.h"
#include "ThemeManager.h"

LifeMeterBattery::LifeMeterBattery() {
  lives_left_ = 4;
  trailing_lives_left_ = lives_left_;

  sound_gain_life_.Load(THEME->GetPathS("LifeMeterBattery", "gain"));
  sound_lose_life_.Load(THEME->GetPathS("LifeMeterBattery", "lose"), true);
}

void LifeMeterBattery::Load(
    const PlayerState* pPlayerState, PlayerStageStats* pPlayerStageStats) {
  LifeMeter::Load(pPlayerState, pPlayerStageStats);

  lives_left_ = player_state_->player_options_.GetStage().battery_lives_;
  trailing_lives_left_ = lives_left_;

  const std::string sType = "LifeMeterBattery";
  PlayerNumber pn = pPlayerState->player_number_;

  MIN_SCORE_TO_KEEP_LIFE.Load(sType, "MinScoreToKeepLife");
  MAX_LIVES.Load(sType, "MaxLives");
  DANGER_THRESHOLD.Load(sType, "DangerThreshold");
  SUBTRACT_LIVES.Load(sType, "SubtractLives");
  MINES_SUBTRACT_LIVES.Load(sType, "MinesSubtractLives");
  HELD_ADD_LIVES.Load(sType, "HeldAddLives");
  LET_GO_SUBTRACT_LIVES.Load(sType, "LetGoSubtractLives");
  COURSE_SONG_REWARD_LIVES.Load(sType, "CourseSongRewardLives");

  LIVES_FORMAT.Load(sType, "NumLivesFormat");

  bool bPlayerEnabled = GAMESTATE->IsPlayerEnabled(pPlayerState);
  LuaThreadVariable var(
      "Player", LuaReference::Create(player_state_->player_number_));

  spr_frame_.Load(THEME->GetPathG(sType, "frame"));
  this->AddChild(spr_frame_);

  spr_battery_.Load(THEME->GetPathG(sType, "lives"));
  spr_battery_->SetName(ssprintf("BatteryP%i", int(pn + 1)));
  if (bPlayerEnabled) {
    ActorUtil::LoadAllCommandsAndSetXY(spr_battery_, sType);
    this->AddChild(spr_battery_);
  }

  text_num_lives_.LoadFromFont(THEME->GetPathF(sType, "lives"));
  text_num_lives_.SetName(ssprintf("NumLivesP%i", int(pn + 1)));
  if (bPlayerEnabled) {
    ActorUtil::LoadAllCommandsAndSetXY(text_num_lives_, sType);
    this->AddChild(&text_num_lives_);
  }
  // old hardcoded commands:
  /*
  m_sprFrame.SetZoomX( pn==PLAYER_1 ? 1.0f : -1.0f );
  m_sprBattery.SetZoomX( pn==PLAYER_1 ? 1.0f : -1.0f );
  m_sprBattery.SetX( BATTERY_X[pn] );
  m_textNumLives.SetX( NUM_X[pn] );
  m_textNumLives.SetY( NUM_Y );
  */

  if (bPlayerEnabled) {
    percent_.Load(
        pPlayerState, pPlayerStageStats, "LifeMeterBattery Percent", true);
    // old hardcoded commands (this is useful, but let the themer decide
    // what they want to do, please -aj)
    // m_Percent.SetZoomX( pn==PLAYER_1 ? 1.0f : -1.0f );
    this->AddChild(&percent_);
  }

  Refresh();
}

void LifeMeterBattery::OnSongEnded() {
  if (player_stage_stats_->failed_ || lives_left_ == 0) {
    return;
  }

  if (lives_left_ < player_state_->player_options_.GetSong().battery_lives_) {
    trailing_lives_left_ = lives_left_;
    PlayerNumber pn = player_state_->player_number_;
    const Course* pCourse = GAMESTATE->cur_course_;

    if (pCourse &&
        pCourse->entries_[GAMESTATE->GetCourseSongIndex()].gain_lives_ > -1) {
      lives_left_ +=
          pCourse->entries_[GAMESTATE->GetCourseSongIndex()].gain_lives_;
    } else {
      Lua* L = LUA->Get();
      COURSE_SONG_REWARD_LIVES.PushSelf(L);
      PushSelf(L);
      LuaHelpers::Push(L, pn);
      std::string error = "Error running CourseSongRewardLives callback: ";
      LuaHelpers::RunScriptOnStack(L, error, 2, 1, true);
      lives_left_ += (int)luaL_optnumber(L, -1, 0);
      lua_settop(L, 0);
      LUA->Release(L);
    }
    lives_left_ = std::min(
        lives_left_, player_state_->player_options_.GetSong().battery_lives_);

    if (trailing_lives_left_ < lives_left_) {
      sound_gain_life_.Play(false);
    }
  }

  Refresh();
}

void LifeMeterBattery::SubtractLives(int iLives) {
  if (iLives <= 0) {
    return;
  }

  trailing_lives_left_ = lives_left_;
  lives_left_ -= iLives;
  sound_lose_life_.Play(false);
  text_num_lives_.PlayCommand("LoseLife");

  Refresh();
}

void LifeMeterBattery::AddLives(int iLives) {
  if (iLives <= 0) {
    return;
  }
  if (MAX_LIVES != 0 && lives_left_ >= MAX_LIVES) {
    return;
  }

  trailing_lives_left_ = lives_left_;
  lives_left_ += iLives;
  sound_gain_life_.Play(false);
  text_num_lives_.PlayCommand("GainLife");

  Refresh();
}

void LifeMeterBattery::ChangeLives(int iLifeDiff) {
  if (iLifeDiff < 0) {
    SubtractLives(std::abs(iLifeDiff));
  } else if (iLifeDiff > 0) {
    AddLives(iLifeDiff);
  }
}

void LifeMeterBattery::ChangeLife(TapNoteScore score) {
  if (lives_left_ == 0) {
    return;
  }

  bool bSubtract = false;
  // this probably doesn't handle hold checkpoints. -aj
  if (score == TNS_HitMine && MINES_SUBTRACT_LIVES > 0) {
    SubtractLives(MINES_SUBTRACT_LIVES);
    bSubtract = true;
  } else {
    if (score < MIN_SCORE_TO_KEEP_LIFE && score > TNS_CheckpointMiss &&
        SUBTRACT_LIVES > 0) {
      SubtractLives(SUBTRACT_LIVES);
      bSubtract = true;
    }
  }
  BroadcastLifeChanged(bSubtract);
}

void LifeMeterBattery::ChangeLife(HoldNoteScore score, TapNoteScore tscore) {
  if (lives_left_ == 0) {
    return;
  }

  bool bSubtract = false;
  if (score == HNS_Held && HELD_ADD_LIVES > 0) {
    AddLives(HELD_ADD_LIVES);
  }
  if (score == HNS_LetGo && LET_GO_SUBTRACT_LIVES > 0) {
    SubtractLives(LET_GO_SUBTRACT_LIVES);
    bSubtract = true;
  }
  BroadcastLifeChanged(bSubtract);
}

void LifeMeterBattery::SetLife(float value) {
  int new_lives = static_cast<int>(value);
  bool lost = (new_lives < lives_left_);
  lives_left_ = new_lives;
  BroadcastLifeChanged(lost);
}

void LifeMeterBattery::BroadcastLifeChanged(bool lost_life) {
  Message msg("LifeChanged");
  msg.SetParam("Player", player_state_->player_number_);
  msg.SetParam("LifeMeter", LuaReference::CreateFromPush(*this));
  msg.SetParam("LivesLeft", GetLivesLeft());
  msg.SetParam("LostLife", lost_life);
  MESSAGEMAN->Broadcast(msg);
}

void LifeMeterBattery::HandleTapScoreNone() {
  // do nothing
}

void LifeMeterBattery::ChangeLife(float fDeltaLifePercent) {}

bool LifeMeterBattery::IsInDanger() const {
  return lives_left_ < DANGER_THRESHOLD;
}

bool LifeMeterBattery::IsHot() const {
  return lives_left_ == player_state_->player_options_.GetSong().battery_lives_;
}

bool LifeMeterBattery::IsFailing() const { return lives_left_ == 0; }

float LifeMeterBattery::GetLife() const {
  if (!player_state_->player_options_.GetSong().battery_lives_) {
    return 1;
  }

  return float(lives_left_) /
         player_state_->player_options_.GetSong().battery_lives_;
}
void LifeMeterBattery::Refresh() {
  text_num_lives_.SetText(
      ssprintf(LIVES_FORMAT.GetValue().c_str(), lives_left_));
  if (lives_left_ < 0) {
    // hide text to avoid showing -1
    text_num_lives_.SetVisible(false);
  }
  // update m_sprBattery
}

int LifeMeterBattery::GetTotalLives() {
  return player_state_->player_options_.GetSong().battery_lives_;
}

void LifeMeterBattery::Update(float fDeltaTime) {
  LifeMeter::Update(fDeltaTime);
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the LifeMeterBattery. */
class LunaLifeMeterBattery : public Luna<LifeMeterBattery> {
 public:
  static int GetLivesLeft(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetLivesLeft());
    return 1;
  }
  static int GetTotalLives(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetTotalLives());
    return 1;
  }
  static int ChangeLives(T* p, lua_State* L) {
    p->ChangeLives(IArg(1));
    COMMON_RETURN_SELF;
  }

  LunaLifeMeterBattery() {
    ADD_METHOD(GetLivesLeft);
    ADD_METHOD(GetTotalLives);
    ADD_METHOD(ChangeLives);
  }
};

LUA_REGISTER_DERIVED_CLASS(LifeMeterBattery, LifeMeter)

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
