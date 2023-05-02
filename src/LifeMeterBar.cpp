#include "global.h"

#include "LifeMeterBar.h"

#include <cstddef>

#include "ActorUtil.h"
#include "Course.h"
#include "GameState.h"
#include "PlayerState.h"
#include "PrefsManager.h"
#include "Quad.h"
#include "RageLog.h"
#include "RageMath.h"
#include "RageTimer.h"
#include "Song.h"
#include "StatsManager.h"
#include "Steps.h"
#include "StreamDisplay.h"
#include "ThemeManager.h"
#include "ThemeMetric.h"

static RString LIFE_PERCENT_CHANGE_NAME(std::size_t i) {
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

  const RString type = "LifeMeterBar";

  passing_alpha_ = 0;
  hot_alpha_ = 0;

  base_life_difficulty_ = PREFSMAN->m_fLifeDifficultyScale;
  life_difficulty_ = base_life_difficulty_;

  // Set up progressive lifebar.
  progressive_life_bar_ = PREFSMAN->m_iProgressiveLifebar;
  miss_combo_ = 0;

  // Set up combo_to_regain_life_.
  combo_to_regain_life_ = 0;

  bool is_extra_stage = GAMESTATE->IsAnExtraStage();
  RString extra = is_extra_stage ? "extra " : "";

  under_.Load(THEME->GetPathG(type, extra + "Under"));
  under_->SetName("Under");
  ActorUtil::LoadAllCommandsAndSetXY(under_, type);
  this->AddChild(under_);

  danger_.Load(THEME->GetPathG(type, extra + "Danger"));
  danger_->SetName("Danger");
  ActorUtil::LoadAllCommandsAndSetXY(danger_, type);
  this->AddChild(danger_);

  stream_ = new StreamDisplay;
  stream_->Load(is_extra_stage ? "StreamDisplayExtra" : "StreamDisplay");
  stream_->SetName("Stream");
  ActorUtil::LoadAllCommandsAndSetXY(stream_, type);
  this->AddChild(stream_);

  over_.Load(THEME->GetPathG(type, extra + "Over"));
  over_->SetName("Over");
  ActorUtil::LoadAllCommandsAndSetXY(over_, type);
  this->AddChild(over_);
}

LifeMeterBar::~LifeMeterBar() { SAFE_DELETE(stream_); }

void LifeMeterBar::Load(
    const PlayerState* player_state, PlayerStageStats* player_stage_stats) {
  LifeMeter::Load(player_state, player_stage_stats);

  PlayerNumber pn = player_state->m_PlayerNumber;

  DrainType drain_type = player_state->m_PlayerOptions.GetStage().m_DrainType;
  switch (drain_type) {
    case DrainType_Normal:
      life_percentage_ = INITIAL_VALUE;
      break;
      // These types only go down, so they always start at full.
    case DrainType_NoRecover:
    case DrainType_SuddenDeath:
      life_percentage_ = 1.0f;
      break;
    default:
      FAIL_M(ssprintf("Invalid DrainType: %i", drain_type));
  }

  // Change life difficulty to really easy if merciful beginner on
  merciful_beginner_in_effect_ =
      GAMESTATE->play_mode_ == PLAY_MODE_REGULAR &&
      GAMESTATE->IsPlayerEnabled(player_state) &&
      GAMESTATE->cur_steps_[pn]->GetDifficulty() == Difficulty_Beginner &&
      PREFSMAN->m_bMercifulBeginner;

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

  // NOTE(freem): this was previously if( IsHot() && score < TNS_GOOD ) in 3.9.
  if (PREFSMAN->m_HarshHotLifePenalty && IsHot() && delta_life < 0) {
		// Make it take a while to get back to "hot".
    delta_life = std::min(delta_life, -0.10f);
  }

  switch (player_state_->m_PlayerOptions.GetSong().m_DrainType) {
    DEFAULT_FAIL(player_state_->m_PlayerOptions.GetSong().m_DrainType);
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

void LifeMeterBar::ChangeLife(HoldNoteScore hns, TapNoteScore tns) {
  float delta_life = 0.f;
  DrainType drain_type = player_state_->m_PlayerOptions.GetSong().m_DrainType;
  switch (drain_type) {
    case DrainType_Normal:
      switch (hns) {
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
          FAIL_M(ssprintf("Invalid HoldNoteScore: %i", hns));
      }
      if (PREFSMAN->m_HarshHotLifePenalty && IsHot() && hns == HNS_LetGo) {
				// Make it take a while to get back to "hot".
        delta_life = -0.10f;
      }
      break;
    case DrainType_NoRecover:
      switch (hns) {
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
          FAIL_M(ssprintf("Invalid HoldNoteScore: %i", hns));
      }
      break;
    case DrainType_SuddenDeath:
      switch (hns) {
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
          FAIL_M(ssprintf("Invalid HoldNoteScore: %i", hns));
      }
      break;
    default:
      FAIL_M(ssprintf("Invalid DrainType: %i", drain_type));
  }

  ChangeLife(delta_life);
}

void LifeMeterBar::ChangeLife(float delta_life) {
  bool use_merciful_drain =
      merciful_beginner_in_effect_ || PREFSMAN->m_bMercifulDrain;
  if (use_merciful_drain && delta_life < 0) {
    delta_life *= SCALE(life_percentage_, 0.f, 1.f, 0.5f, 1.f);
  }

  // Handle progressiveness and ComboToRegainLife here.
  if (delta_life >= 0) {
    miss_combo_ = 0;
    combo_to_regain_life_ = std::max(combo_to_regain_life_ - 1, 0);
    if (combo_to_regain_life_ > 0) {
      delta_life = 0.0f;
    }
  } else {
    delta_life *= 1 + (float)progressive_life_bar_ / 8 * miss_combo_;
    // Do this after; only successive W5/miss will increase the amount of life
    // lost.
    miss_combo_++;
    // Increase by m_iRegenComboAfterMiss; never push it beyond
    // m_iMaxRegenComboAfterMiss but don't reduce it if it's already past.
    const int new_combo_to_regain_life = std::min(
        (int)PREFSMAN->m_iMaxRegenComboAfterMiss,
        combo_to_regain_life_ + PREFSMAN->m_iRegenComboAfterMiss);

    combo_to_regain_life_ =
				std::max(combo_to_regain_life_, new_combo_to_regain_life);
  }

  // If we've already failed, there's no point in letting them fill up the bar
  // again.
  if (player_stage_stats_->m_bFailed) {
    return;
  }

  switch (player_state_->m_PlayerOptions.GetSong().m_DrainType) {
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
  CLAMP(life_percentage_, 0, LIFE_MULTIPLIER);
  AfterLifeChanged();
}

void LifeMeterBar::SetLife(float value) {
  life_percentage_ = value;
  CLAMP(life_percentage_, 0, LIFE_MULTIPLIER);
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
  msg.SetParam("Player", player_state_->m_PlayerNumber);
  msg.SetParam("LifeMeter", LuaReference::CreateFromPush(*this));
  MESSAGEMAN->Broadcast(msg);
}

bool LifeMeterBar::IsHot() const { return life_percentage_ >= HOT_VALUE; }

bool LifeMeterBar::IsInDanger() const {
  return life_percentage_ < DANGER_THRESHOLD;
}

bool LifeMeterBar::IsFailing() const {
  return life_percentage_ <=
         player_state_->m_PlayerOptions.GetCurrent().m_fPassmark;
}

void LifeMeterBar::Update(float delta_time) {
  LifeMeter::Update(delta_time);

  passing_alpha_ += !IsFailing() ? +delta_time * 2 : -delta_time * 2;
  CLAMP(passing_alpha_, 0, 1);

  hot_alpha_ += IsHot() ? +delta_time * 2 : -delta_time * 2;
  CLAMP(hot_alpha_, 0, 1);

  stream_->SetPassingAlpha(passing_alpha_);
  stream_->SetHotAlpha(hot_alpha_);

  if (player_state_->m_HealthState == HealthState_Danger) {
    danger_->SetVisible(true);
  } else {
    danger_->SetVisible(false);
  }
}

void LifeMeterBar::UpdateNonstopLifebar() {
  int cleared;
	int total;
	int progressive_lifebar_difficulty;

  switch (GAMESTATE->play_mode_) {
    case PLAY_MODE_REGULAR:
      if (GAMESTATE->IsEventMode() || GAMESTATE->demonstration_or_jukebox_) {
        return;
      }

      cleared = GAMESTATE->current_stage_index_;
      total = PREFSMAN->m_iSongsPerPlay;
      progressive_lifebar_difficulty = PREFSMAN->m_iProgressiveStageLifebar;
      break;
    case PLAY_MODE_NONSTOP:
      cleared = GAMESTATE->GetCourseSongIndex();
      total = GAMESTATE->cur_course_->GetEstimatedNumStages();
      progressive_lifebar_difficulty = PREFSMAN->m_iProgressiveNonstopLifebar;
      break;
    default:
      return;
  }

  //	if (cleared > total) cleared = total; // clear/total <= 1
  //	if (total == 0) total = 1;  // no division by 0

  if (GAMESTATE->IsAnExtraStage() && FORCE_LIFE_DIFFICULTY_ON_EXTRA_STAGE) {
    // Extra stage is its own thing, should not be progressive and it should be
		// as difficult as life 4 (e.g. it should not depend on life settings).
    progressive_life_bar_ = 0;
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

  // Approximate deductions for a miss
  // Life 1 :    5   %
  // Life 2 :    5.7 %
  // Life 3 :    6.6 %
  // Life 4 :    8   %
  // Life 5 :   10   %
  // Life 6 :   13.3 %
  // Life 7 :   20   %
  // Life 8 :   26.6 %
  // Life 9 :   32   %
  // Life 10:   40   %
  // Life 11:   50   %
  // Life 12:   57.1 %
  // Life 13:   66.6 %
  // Life 14:   80   %
  // Life 15:  100   %
  // Life 16+: 200   %
  //
  // Note there is 200%, because boos take off 1/2 as much as a miss, and a W5
	// would suck up half of your lifebar.
  //
  // Everything past 7 is intended mainly for nonstop mode.

  // The lifebar is pretty harsh at 0.4 already (you lose about 20% of your
	// lifebar); at 0.2 it would be 40%, which is too harsh at one difficulty
	// level higher. Override.
  int life_difficulty = int((1.8f - life_difficulty_) / 0.2f);

  // First eight values don't matter.
  float difficulty_values[16] = {0,     0,     0,     0,     0,    0,
                                 0,     0,     0.3f,  0.25f, 0.2f, 0.16f,
                                 0.14f, 0.12f, 0.10f, 0.08f};

  if (life_difficulty >= 16) {
    // Judge 16 or higher.
    life_difficulty_ = 0.04f;
    return;
  }

  life_difficulty_ = difficulty_values[life_difficulty];
  return;
}

void LifeMeterBar::FillForHowToPlay(int num_w2s, int num_misses) {
	// Disable progressive lifebar.
  progressive_life_bar_ = 0;

  float amount_for_w2 = num_w2s * life_difficulty_ * 0.008f;
  float amount_for_miss = num_misses / life_difficulty_ * 0.08f;

  life_percentage_ = amount_for_miss - amount_for_w2;
  CLAMP(life_percentage_, 0.0f, 1.0f);
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
