#include "LifeMeterBar.h"

#include <algorithm>
#include <cstddef>
#include <string>

#include "ActorUtil.h"
#include "Course.h"
#include "Difficulty.h"
#include "GameConstantsAndTypes.h"
#include "GameState.h"
#include "LifeMeter.h"
#include "LuaReference.h"
#include "MessageManager.h"
#include "PlayerNumber.h"
#include "PlayerOptions.h"
#include "PlayerStageStats.h"
#include "PlayerState.h"
#include "PrefsManager.h"
#include "RageUtil.h"
#include "Steps.h"
#include "StreamDisplay.h"
#include "ThemeManager.h"
#include "ThemeMetric.h"
#include "global.h"

static std::string LIFE_PERCENT_CHANGE_NAME(size_t i) {
  return "LifePercentChange" + ScoreEventToString((ScoreEvent)i);
}

LifeMeterBar::LifeMeterBar() {
  DANGER_THRESHOLD.Load("LifeMeterBar", "DangerThreshold");
  INITIAL_VALUE.Load("LifeMeterBar", "InitialValue");
  HOT_VALUE.Load("LifeMeterBar", "HotValue");
  LIFE_MULTIPLIER.Load("LifeMeterBar", "LifeMultiplier");
  MIN_STAY_ALIVE.Load("LifeMeterBar", "MinStayAlive");
  FORCE_LIFE_DIFFICULTY_ON_EXTRA_STAGE.Load(
      "LifeMeterBar", "ForceLifeDifficultyOnExtraStage");
  EXTRA_STAGE_LIFE_DIFFICULTY.Load("LifeMeterBar", "ExtraStageLifeDifficulty");
  life_percent_change_.Load(
      "LifeMeterBar", LIFE_PERCENT_CHANGE_NAME, NUM_ScoreEvent);

  player_state_ = nullptr;

  const std::string type = "LifeMeterBar";

  passing_alpha_ = 0;
  hot_alpha_ = 0;

  base_life_difficulty_ = PREFSMAN->life_difficulty_scale_;
  life_difficulty_ = base_life_difficulty_;

  // set up progressive lifebar
  progressive_lifebar_ = PREFSMAN->progressive_lifebar_;
  miss_combo_ = 0;

  // set up combotoregainlife
  combo_to_regain_life_ = 0;

  bool b_extra = GAMESTATE->IsAnExtraStage();
  std::string s_extra = b_extra ? "extra " : "";

  spr_under_.Load(THEME->GetPathG(type, s_extra + "Under"));
  spr_under_->SetName("Under");
  ActorUtil::LoadAllCommandsAndSetXY(spr_under_, type);
  this->AddChild(spr_under_);

  spr_danger_.Load(THEME->GetPathG(type, s_extra + "Danger"));
  spr_danger_->SetName("Danger");
  ActorUtil::LoadAllCommandsAndSetXY(spr_danger_, type);
  this->AddChild(spr_danger_);

  stream_ = new StreamDisplay;
  stream_->Load(b_extra ? "StreamDisplayExtra" : "StreamDisplay");
  stream_->SetName("Stream");
  ActorUtil::LoadAllCommandsAndSetXY(stream_, type);
  this->AddChild(stream_);

  spr_over_.Load(THEME->GetPathG(type, s_extra + "Over"));
  spr_over_->SetName("Over");
  ActorUtil::LoadAllCommandsAndSetXY(spr_over_, type);
  this->AddChild(spr_over_);
}

LifeMeterBar::~LifeMeterBar() { RageUtil::SafeDelete(stream_); }

void LifeMeterBar::Load(
    const PlayerState* player_state, PlayerStageStats* player_stage_stats) {
  LifeMeter::Load(player_state, player_stage_stats);

  PlayerNumber pn = player_state->player_number_;

  DrainType dtype = player_state->player_options_.GetStage().drain_type_;
  switch (dtype) {
    case DrainType_Normal:
      life_percentage_ = INITIAL_VALUE;
      break;
      /* These types only go down, so they always start at full. */
    case DrainType_NoRecover:
    case DrainType_SuddenDeath:
      life_percentage_ = 1.0f;
      break;
    default:
      FAIL_M(ssprintf("Invalid DrainType: %i", dtype));
  }

  // Change life difficulty to really easy if merciful beginner on
  merciful_beginner_in_effect_ =
      GAMESTATE->play_mode_ == PLAY_MODE_REGULAR &&
      GAMESTATE->IsPlayerEnabled(player_state) &&
      GAMESTATE->cur_steps_[pn]->GetDifficulty() == Difficulty_Beginner &&
      PREFSMAN->merciful_beginner_;

  AfterLifeChanged();
}

void LifeMeterBar::ChangeLife(TapNoteScore score) {
  float delta_life = 0.f;
  switch (score) {
    DEFAULT_FAIL(score);
    case TNS_W1:
      delta_life = life_percent_change_.GetValue(SE_W1);
      break;
    case TNS_W2:
      delta_life = life_percent_change_.GetValue(SE_W2);
      break;
    case TNS_W3:
      delta_life = life_percent_change_.GetValue(SE_W3);
      break;
    case TNS_W4:
      delta_life = life_percent_change_.GetValue(SE_W4);
      break;
    case TNS_W5:
      delta_life = life_percent_change_.GetValue(SE_W5);
      break;
    case TNS_Miss:
      delta_life = life_percent_change_.GetValue(SE_Miss);
      break;
    case TNS_HitMine:
      delta_life = life_percent_change_.GetValue(SE_HitMine);
      break;
    case TNS_None:
      delta_life = life_percent_change_.GetValue(SE_Miss);
      break;
    case TNS_CheckpointHit:
      delta_life = life_percent_change_.GetValue(SE_CheckpointHit);
      break;
    case TNS_CheckpointMiss:
      delta_life = life_percent_change_.GetValue(SE_CheckpointMiss);
      break;
  }

  // this was previously if( IsHot()  &&  score < TNS_GOOD ) in 3.9... -freem
  if (PREFSMAN->harsh_hot_life_penalty_ && IsHot() && delta_life < 0) {
    delta_life = std::min(
        delta_life, -0.10f);  // make it take a while to get back to "hot"
  }

  switch (player_state_->player_options_.GetSong().drain_type_) {
    DEFAULT_FAIL(player_state_->player_options_.GetSong().drain_type_);
    case DrainType_Normal:
      break;
    case DrainType_NoRecover:
      delta_life = std::min(delta_life, 0.0f);
      break;
    case DrainType_SuddenDeath:
      if (score < MIN_STAY_ALIVE) {
        delta_life = -1.0f;
      } else {
        delta_life = 0;
      }
      break;
  }

  ChangeLife(delta_life);
}

void LifeMeterBar::ChangeLife(HoldNoteScore score, TapNoteScore tscore) {
  float delta_life = 0.f;
  DrainType dtype = player_state_->player_options_.GetSong().drain_type_;
  switch (dtype) {
    case DrainType_Normal:
      switch (score) {
        case HNS_Held:
          delta_life = life_percent_change_.GetValue(SE_Held);
          break;
        case HNS_LetGo:
          delta_life = life_percent_change_.GetValue(SE_LetGo);
          break;
        case HNS_Missed:
          delta_life = life_percent_change_.GetValue(SE_Missed);
          break;
        default:
          FAIL_M(ssprintf("Invalid HoldNoteScore: %i", score));
      }
      if (PREFSMAN->harsh_hot_life_penalty_ && IsHot() && score == HNS_LetGo) {
        delta_life = -0.10f;  // make it take a while to get back to "hot"
      }
      break;
    case DrainType_NoRecover:
      switch (score) {
        case HNS_Held:
          delta_life = +0.000f;
          break;
        case HNS_LetGo:
          delta_life = life_percent_change_.GetValue(SE_LetGo);
          break;
        case HNS_Missed:
          delta_life = +0.000f;
          break;
        default:
          FAIL_M(ssprintf("Invalid HoldNoteScore: %i", score));
      }
      break;
    case DrainType_SuddenDeath:
      switch (score) {
        case HNS_Held:
          delta_life = +0;
          break;
        case HNS_LetGo:
          delta_life = -1.0f;
          break;
        case HNS_Missed:
          delta_life = +0;
          break;
        default:
          FAIL_M(ssprintf("Invalid HoldNoteScore: %i", score));
      }
      break;
    default:
      FAIL_M(ssprintf("Invalid DrainType: %i", dtype));
  }

  ChangeLife(delta_life);
}

void LifeMeterBar::ChangeLife(float delta_life) {
  bool use_merciful_drain =
      merciful_beginner_in_effect_ || PREFSMAN->merciful_drain_;
  if (use_merciful_drain && delta_life < 0) {
    delta_life *= SCALE(life_percentage_, 0.f, 1.f, 0.5f, 1.f);
  }

  // handle progressiveness and ComboToRegainLife here
  if (delta_life >= 0) {
    miss_combo_ = 0;
    combo_to_regain_life_ = std::max(combo_to_regain_life_ - 1, 0);
    if (combo_to_regain_life_ > 0) {
      delta_life = 0.0f;
    }
  } else {
    delta_life *= 1 + (float)progressive_lifebar_ / 8 * miss_combo_;
    // do this after; only successive W5/miss will increase the amount of life
    // lost.
    miss_combo_++;
    /* Increase by m_iRegenComboAfterMiss; never push it beyond
     * m_iMaxRegenComboAfterMiss but don't reduce it if it's already past. */
    const int new_combo_to_regain_life = std::min(
        (int)PREFSMAN->max_regen_combo_after_miss_,
        combo_to_regain_life_ + PREFSMAN->regen_combo_after_miss_);

    combo_to_regain_life_ =
        std::max(combo_to_regain_life_, new_combo_to_regain_life);
  }

  // If we've already failed, there's no point in letting them fill up the bar
  // again.
  if (player_stage_stats_->failed_) {
    return;
  }

  switch (player_state_->player_options_.GetSong().drain_type_) {
    case DrainType_Normal:
    case DrainType_NoRecover:
      if (delta_life > 0) {
        delta_life *= life_difficulty_;
      } else {
        delta_life /= life_difficulty_;
      }
      break;
    case DrainType_SuddenDeath:
      // This should always -1.0f;
      if (delta_life < 0) {
        delta_life = -1.0f;
      } else {
        delta_life = 0;
      }
      break;
    default:
      break;
  }

  life_percentage_ += delta_life;
  rage_clamp(life_percentage_, 0, LIFE_MULTIPLIER);
  AfterLifeChanged();
}

void LifeMeterBar::SetLife(float value) {
  life_percentage_ = value;
  rage_clamp(life_percentage_, 0, LIFE_MULTIPLIER);
  AfterLifeChanged();
}

extern ThemeMetric<bool> PENALIZE_TAP_SCORE_NONE;
void LifeMeterBar::HandleTapScoreNone() {
  if (PENALIZE_TAP_SCORE_NONE) {
    ChangeLife(TNS_None);
  }
}

void LifeMeterBar::AfterLifeChanged() {
  stream_->SetPercent(life_percentage_);

  Message msg("LifeChanged");
  msg.SetParam("Player", player_state_->player_number_);
  msg.SetParam("LifeMeter", LuaReference::CreateFromPush(*this));
  MESSAGEMAN->Broadcast(msg);
}

bool LifeMeterBar::IsHot() const { return life_percentage_ >= HOT_VALUE; }

bool LifeMeterBar::IsInDanger() const {
  return life_percentage_ < DANGER_THRESHOLD;
}

bool LifeMeterBar::IsFailing() const {
  return life_percentage_ <=
         player_state_->player_options_.GetCurrent().passmark_;
}

void LifeMeterBar::Update(float delta_time) {
  LifeMeter::Update(delta_time);

  passing_alpha_ += !IsFailing() ? +delta_time * 2 : -delta_time * 2;
  rage_clamp(passing_alpha_, 0, 1);

  hot_alpha_ += IsHot() ? +delta_time * 2 : -delta_time * 2;
  rage_clamp(hot_alpha_, 0, 1);

  stream_->SetPassingAlpha(passing_alpha_);
  stream_->SetHotAlpha(hot_alpha_);

  if (player_state_->health_state_ == HealthState_Danger) {
    spr_danger_->SetVisible(true);
  } else {
    spr_danger_->SetVisible(false);
  }
}

void LifeMeterBar::UpdateNonstopLifebar() {
  int cleared, total, progressive_lifebar_difficulty;

  switch (GAMESTATE->play_mode_) {
    case PLAY_MODE_REGULAR:
      if (GAMESTATE->IsEventMode() || GAMESTATE->demonstration_or_jukebox_) {
        return;
      }

      cleared = GAMESTATE->current_stage_index_;
      total = PREFSMAN->songs_per_play_;
      progressive_lifebar_difficulty = PREFSMAN->progressive_stage_lifebar_;
      break;
    case PLAY_MODE_NONSTOP:
      cleared = GAMESTATE->GetCourseSongIndex();
      total = GAMESTATE->cur_course_->GetEstimatedNumStages();
      progressive_lifebar_difficulty = PREFSMAN->progressive_nonstop_lifebar_;
      break;
    default:
      return;
  }

  //	if (iCleared > iTotal) iCleared = iTotal; // clear/iTotal <= 1
  //	if (iTotal == 0) iTotal = 1;  // no division by 0

  if (GAMESTATE->IsAnExtraStage() && FORCE_LIFE_DIFFICULTY_ON_EXTRA_STAGE) {
    // extra stage is its own thing, should not be progressive
    // and it should be as difficult as life 4
    // (e.g. it should not depend on life settings)

    progressive_lifebar_ = 0;
    life_difficulty_ = EXTRA_STAGE_LIFE_DIFFICULTY;
    return;
  }

  if (total > 1) {
    life_difficulty_ =
        base_life_difficulty_ -
        0.2f * (int)(progressive_lifebar_difficulty * cleared / (total - 1));
  } else {
    life_difficulty_ =
        base_life_difficulty_ - 0.2f * progressive_lifebar_difficulty;
  }

  if (life_difficulty_ >= 0.4f) {
    return;
  }

  /* Approximate deductions for a miss
   * Life 1 :    5   %
   * Life 2 :    5.7 %
   * Life 3 :    6.6 %
   * Life 4 :    8   %
   * Life 5 :   10   %
   * Life 6 :   13.3 %
   * Life 7 :   20   %
   * Life 8 :   26.6 %
   * Life 9 :   32   %
   * Life 10:   40   %
   * Life 11:   50   %
   * Life 12:   57.1 %
   * Life 13:   66.6 %
   * Life 14:   80   %
   * Life 15:  100   %
   * Life 16+: 200   %
   *
   * Note there is 200%, because boos take off 1/2 as much as
   * a miss, and a W5 would suck up half of your lifebar.
   *
   * Everything past 7 is intended mainly for nonstop mode.
   */

  // the lifebar is pretty harsh at 0.4 already (you lose
  // about 20% of your lifebar); at 0.2 it would be 40%, which
  // is too harsh at one difficulty level higher.  Override.
  int life_difficulty = int((1.8f - life_difficulty_) / 0.2f);

  // first eight values don't matter
  float difficulty_values[16] = {0,     0,     0,     0,     0,    0,
                                 0,     0,     0.3f,  0.25f, 0.2f, 0.16f,
                                 0.14f, 0.12f, 0.10f, 0.08f};

  if (life_difficulty >= 16) {
    // judge 16 or higher
    life_difficulty_ = 0.04f;
    return;
  }

  life_difficulty_ = difficulty_values[life_difficulty];
  return;
}

void LifeMeterBar::FillForHowToPlay(int num_w2s, int num_misses) {
  progressive_lifebar_ = 0;  // disable progressive lifebar

  float amount_for_w2 = num_w2s * life_difficulty_ * 0.008f;
  float amount_for_miss = num_misses / life_difficulty_ * 0.08f;

  life_percentage_ = amount_for_miss - amount_for_w2;
  rage_clamp(life_percentage_, 0.0f, 1.0f);
  AfterLifeChanged();
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
