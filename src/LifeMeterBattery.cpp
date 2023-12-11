#include "global.h"

#include "LifeMeterBattery.h"

#include <cmath>

#include "ActorUtil.h"
#include "Course.h"
#include "GameState.h"
#include "PlayerState.h"
#include "Steps.h"
#include "ThemeManager.h"

LifeMeterBattery::LifeMeterBattery() {
  lives_left_ = 4;
  trailing_lives_left = lives_left_;

  sound_gain_life_.Load(THEME->GetPathS("LifeMeterBattery", "gain"));
  sound_lose_life_.Load(THEME->GetPathS("LifeMeterBattery", "lose"), true);
}

void LifeMeterBattery::Load(
    const PlayerState* player_state, PlayerStageStats* player_stage_stats) {
  LifeMeter::Load(player_state, player_stage_stats);

  lives_left_ = player_state_->m_PlayerOptions.GetStage().m_BatteryLives;
  trailing_lives_left = lives_left_;

  const RString type = "LifeMeterBattery";
  PlayerNumber pn = player_state->m_PlayerNumber;

  MIN_SCORE_TO_KEEP_LIFE.Load(type, "MinScoreToKeepLife");
  MAX_LIVES.Load(type, "MaxLives");
  DANGER_THRESHOLD.Load(type, "DangerThreshold");
  SUBTRACT_LIVES.Load(type, "SubtractLives");
  MINES_SUBTRACT_LIVES.Load(type, "MinesSubtractLives");
  HELD_ADD_LIVES.Load(type, "HeldAddLives");
  LET_GO_SUBTRACT_LIVES.Load(type, "LetGoSubtractLives");
  COURSE_SONG_REWARD_LIVES.Load(type, "CourseSongRewardLives");

  LIVES_FORMAT.Load(type, "NumLivesFormat");

  bool player_enabled = GAMESTATE->IsPlayerEnabled(player_state);
  LuaThreadVariable var(
      "Player", LuaReference::Create(player_state_->m_PlayerNumber));

  frame_.Load(THEME->GetPathG(type, "frame"));
  this->AddChild(frame_);

  battery_.Load(THEME->GetPathG(type, "lives"));
  battery_->SetName(ssprintf("BatteryP%i", int(pn + 1)));
  if (player_enabled) {
    ActorUtil::LoadAllCommandsAndSetXY(battery_, type);
    this->AddChild(battery_);
  }

  text_num_lives_.LoadFromFont(THEME->GetPathF(type, "lives"));
  text_num_lives_.SetName(ssprintf("NumLivesP%i", int(pn + 1)));
  if (player_enabled) {
    ActorUtil::LoadAllCommandsAndSetXY(text_num_lives_, type);
    this->AddChild(&text_num_lives_);
  }

  if (player_enabled) {
    percent_.Load(
        player_state, player_stage_stats, "LifeMeterBattery Percent", true);
    // NOTE(aj): old hardcoded commands (this is useful, but let the themer
    // decide what they want to do, please)
    // percent_.SetZoomX( pn==PLAYER_1 ? 1.0f : -1.0f );
    this->AddChild(&percent_);
  }

  Refresh();
}

void LifeMeterBattery::OnSongEnded() {
  if (player_stage_stats_->m_bFailed || lives_left_ == 0) {
    return;
  }

  if (lives_left_ < player_state_->m_PlayerOptions.GetSong().m_BatteryLives) {
    trailing_lives_left = lives_left_;
    PlayerNumber pn = player_state_->m_PlayerNumber;
    const Course* course = GAMESTATE->cur_course_;

    if (course &&
        course->m_vEntries[GAMESTATE->GetCourseSongIndex()].gain_lives_ > -1) {
      lives_left_ +=
          course->m_vEntries[GAMESTATE->GetCourseSongIndex()].gain_lives_;
    } else {
      Lua* L = LUA->Get();
      COURSE_SONG_REWARD_LIVES.PushSelf(L);
      PushSelf(L);
      LuaHelpers::Push(L, pn);
      RString error = "Error running CourseSongRewardLives callback: ";
      LuaHelpers::RunScriptOnStack(L, error, 2, 1, true);
      lives_left_ += (int)luaL_optnumber(L, -1, 0);
      lua_settop(L, 0);
      LUA->Release(L);
    }
    lives_left_ = std::min(
        lives_left_, player_state_->m_PlayerOptions.GetSong().m_BatteryLives);

    if (trailing_lives_left < lives_left_) {
      sound_gain_life_.Play(false);
    }
  }

  Refresh();
}

void LifeMeterBattery::SubtractLives(int lives) {
  if (lives <= 0) {
    return;
  }

  trailing_lives_left = lives_left_;
  lives_left_ -= lives;
  sound_lose_life_.Play(false);
  text_num_lives_.PlayCommand("LoseLife");

  Refresh();
}

void LifeMeterBattery::AddLives(int lives) {
  if (lives <= 0) {
    return;
  }
  if (MAX_LIVES != 0 && lives_left_ >= MAX_LIVES) {
    return;
  }

  trailing_lives_left = lives_left_;
  lives_left_ += lives;
  sound_gain_life_.Play(false);
  text_num_lives_.PlayCommand("GainLife");

  Refresh();
}

void LifeMeterBattery::ChangeLives(int life_diff) {
  if (life_diff < 0) {
    SubtractLives(std::abs(life_diff));
  } else if (life_diff > 0) {
    AddLives(life_diff);
  }
}

void LifeMeterBattery::ChangeLife(TapNoteScore tns) {
  if (lives_left_ == 0) {
    return;
  }

  bool subtract = false;
  // NOTE(aj): this probably doesn't handle hold checkpoints.
  if (tns == TNS_HitMine && MINES_SUBTRACT_LIVES > 0) {
    SubtractLives(MINES_SUBTRACT_LIVES);
    subtract = true;
  } else {
    if (tns < MIN_SCORE_TO_KEEP_LIFE && tns > TNS_CheckpointMiss &&
        SUBTRACT_LIVES > 0) {
      SubtractLives(SUBTRACT_LIVES);
      subtract = true;
    }
  }
  BroadcastLifeChanged(subtract);
}

void LifeMeterBattery::ChangeLife(HoldNoteScore hns, TapNoteScore tns) {
  if (lives_left_ == 0) {
    return;
  }

  bool subtract = false;
  if (hns == HNS_Held && HELD_ADD_LIVES > 0) {
    AddLives(HELD_ADD_LIVES);
  }
  if (hns == HNS_LetGo && LET_GO_SUBTRACT_LIVES > 0) {
    SubtractLives(LET_GO_SUBTRACT_LIVES);
    subtract = true;
  }
  BroadcastLifeChanged(subtract);
}

void LifeMeterBattery::SetLife(float value) {
  int new_lives = static_cast<int>(value);
  bool lost = (new_lives < lives_left_);
  lives_left_ = new_lives;
  BroadcastLifeChanged(lost);
}

void LifeMeterBattery::BroadcastLifeChanged(bool lost_life) {
  Message msg("LifeChanged");
  msg.SetParam("Player", player_state_->m_PlayerNumber);
  msg.SetParam("LifeMeter", LuaReference::CreateFromPush(*this));
  msg.SetParam("LivesLeft", GetLivesLeft());
  msg.SetParam("LostLife", lost_life);
  MESSAGEMAN->Broadcast(msg);
}

void LifeMeterBattery::HandleTapScoreNone() {
  // do nothing
}

void LifeMeterBattery::ChangeLife(float delta_life_percent) {}

bool LifeMeterBattery::IsInDanger() const {
  return lives_left_ < DANGER_THRESHOLD;
}

bool LifeMeterBattery::IsHot() const {
  return lives_left_ ==
         player_state_->m_PlayerOptions.GetSong().m_BatteryLives;
}

bool LifeMeterBattery::IsFailing() const { return lives_left_ == 0; }

float LifeMeterBattery::GetLife() const {
  if (!player_state_->m_PlayerOptions.GetSong().m_BatteryLives) {
    return 1;
  }

  return float(lives_left_) /
         player_state_->m_PlayerOptions.GetSong().m_BatteryLives;
}
int LifeMeterBattery::GetRemainingLives() const {
  if (!player_state_->m_PlayerOptions.GetSong().m_BatteryLives) {
    return 1;
  }

  return lives_left_;
}
void LifeMeterBattery::Refresh() {
  text_num_lives_.SetText(ssprintf(LIVES_FORMAT.GetValue(), lives_left_));
  if (lives_left_ < 0) {
    // Hide text to avoid showing -1.
    text_num_lives_.SetVisible(false);
  }
  // Update battery_
}

int LifeMeterBattery::GetTotalLives() {
  return player_state_->m_PlayerOptions.GetSong().m_BatteryLives;
}

void LifeMeterBattery::Update(float fDeltaTime) {
  LifeMeter::Update(fDeltaTime);
}

// lua start
#include "LuaBinding.h"

// Allow Lua to have access to the LifeMeterBattery.
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
