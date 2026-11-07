#include "ScoreKeeperRave.h"

#include <cstddef>
#include <string>

#include "Attack.h"
#include "Character.h"
#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "GameState.h"
#include "NoteTypes.h"
#include "PlayerNumber.h"
#include "PlayerState.h"
#include "Preference.h"
#include "PrefsManager.h"
#include "RageUtil.h"
#include "RageUtil/RandomNumbers.h"
#include "ScoreKeeper.h"
#include "ThemeMetric.h"
#include "global.h"

ThemeMetric<float> ATTACK_DURATION_SECONDS(
    "ScoreKeeperRave", "AttackDurationSeconds");

static void SuperMeterPercentChangeInit(
    size_t /*ScoreEvent*/ i, std::string& name_out, float& default_value_out) {
  ScoreEvent ci = (ScoreEvent)i;
  name_out = "SuperMeterPercentChange" + ScoreEventToString(ci);
  switch (ci) {
    case SE_CheckpointHit:
      default_value_out = +0.05f;
      break;
    case SE_W1:
      default_value_out = +0.05f;
      break;
    case SE_W2:
      default_value_out = +0.04f;
      break;
    case SE_W3:
      default_value_out = +0.02f;
      break;
    case SE_W4:
      default_value_out = +0.00f;
      break;
    case SE_W5:
      default_value_out = +0.00f;
      break;
    case SE_Miss:
      default_value_out = -0.20f;
      break;
    case SE_HitMine:
      default_value_out = -0.40f;
      break;
    case SE_CheckpointMiss:
      default_value_out = -0.20f;
      break;
    case SE_Held:
      default_value_out = +0.04f;
      break;
    case SE_LetGo:
      default_value_out = -0.20f;
      break;
    case SE_Missed:
      default_value_out = -0.00f;
      break;
      DEFAULT_FAIL(ci);
  }
}

static Preference1D<float> g_fSuperMeterPercentChange(
    SuperMeterPercentChangeInit, NUM_ScoreEvent);

ScoreKeeperRave::ScoreKeeperRave(
    PlayerState* player_state, PlayerStageStats* player_stage_stats)
    : ScoreKeeper(player_state, player_stage_stats) {}

void ScoreKeeperRave::HandleTapScore(const TapNote& tn) {
  TapNoteScore score = tn.result_.tns_;
  float percent_to_move = 0;

  if (score == TNS_HitMine) {
    percent_to_move = g_fSuperMeterPercentChange[SE_HitMine];
  }

  AddSuperMeterDelta(percent_to_move);
}

#define CROSSED(val) (fOld < val && fNew >= val)
void ScoreKeeperRave::HandleTapRowScore(const NoteData& nd, int row) {
  TapNoteScore score_of_last_tap;
  int num_taps_in_row;
  float percent_to_move = 0.0f;

  GetScoreOfLastTapInRow(nd, row, score_of_last_tap, num_taps_in_row);
  if (num_taps_in_row <= 0) {
    return;
  }
  switch (score_of_last_tap) {
    DEFAULT_FAIL(score_of_last_tap);
    case TNS_W1:
      percent_to_move = g_fSuperMeterPercentChange[SE_W1];
      break;
    case TNS_W2:
      percent_to_move = g_fSuperMeterPercentChange[SE_W2];
      break;
    case TNS_W3:
      percent_to_move = g_fSuperMeterPercentChange[SE_W3];
      break;
    case TNS_W4:
      percent_to_move = g_fSuperMeterPercentChange[SE_W4];
      break;
    case TNS_W5:
      percent_to_move = g_fSuperMeterPercentChange[SE_W5];
      break;
    case TNS_Miss:
      percent_to_move = g_fSuperMeterPercentChange[SE_Miss];
      break;
  }
  AddSuperMeterDelta(percent_to_move);
}

void ScoreKeeperRave::HandleHoldScore(const TapNote& tn) {
  // todo: should hit mine be handled in HandleTapRow score instead? -aj
  TapNoteScore tap_score = tn.result_.tns_;
  float percent_to_move = 0.0f;
  switch (tap_score) {
    case TNS_HitMine:
      percent_to_move = g_fSuperMeterPercentChange[SE_HitMine];
      break;
    default:
      break;
  }

  // Playing with this code enabled seems to feel "wrong", but I'm leaving it
  // in for player feedback. -aj
  HoldNoteScore hold_score = tn.hold_result_.hns_;
  switch (hold_score) {
    case HNS_Held:
      percent_to_move = g_fSuperMeterPercentChange[SE_Held];
      break;
    case HNS_LetGo:
      percent_to_move = g_fSuperMeterPercentChange[SE_LetGo];
      break;
    case HNS_Missed:
      percent_to_move = g_fSuperMeterPercentChange[SE_Missed];
      break;
    default:
      break;
  }
  AddSuperMeterDelta(percent_to_move);
}

extern ThemeMetric<bool> PENALIZE_TAP_SCORE_NONE;
void ScoreKeeperRave::HandleTapScoreNone() {
  if (PENALIZE_TAP_SCORE_NONE) {
    float percent_to_move = g_fSuperMeterPercentChange[SE_Miss];
    AddSuperMeterDelta(percent_to_move);
  }
}

void ScoreKeeperRave::AddSuperMeterDelta(float unscaled_percent_change) {
  if (PREFSMAN->merciful_drain_ && unscaled_percent_change < 0) {
    float super_percentage =
        player_state_->super_meter_ / Enum::to_integral(NUM_ATTACK_LEVELS);
    unscaled_percent_change *= SCALE(super_percentage, 0.f, 1.f, 0.5f, 1.f);
  }

  // more mercy: Grow super meter slower or faster depending on life.
  if (PREFSMAN->merciful_super_meter_) {
    float life_percentage = 0;
    switch (player_state_->player_number_) {
      case PLAYER_1:
        life_percentage = GAMESTATE->tug_life_percent_p1_;
        break;
      case PLAYER_2:
        life_percentage = 1 - GAMESTATE->tug_life_percent_p1_;
        break;
      default:
        FAIL_M(ssprintf(
            "Invalid player number: %i", player_state_->player_number_));
    }
    rage_clamp(life_percentage, 0.f, 1.f);
    if (unscaled_percent_change > 0) {
      unscaled_percent_change *= SCALE(life_percentage, 0.f, 1.f, 1.7f, 0.3f);
    } else {  // fUnscaledPercentChange <= 0
      unscaled_percent_change /= SCALE(life_percentage, 0.f, 1.f, 1.7f, 0.3f);
    }
  }

  // mercy: drop super meter faster if at a higher level
  if (unscaled_percent_change < 0) {
    unscaled_percent_change *=
        SCALE(player_state_->super_meter_, 0.f, 1.f, 0.01f, 1.f);
  }

  AttackLevel old_al = (AttackLevel)(int)player_state_->super_meter_;

  float percent_to_move = unscaled_percent_change;
  player_state_->super_meter_ +=
      percent_to_move * player_state_->super_meter_growth_scale_;
  rage_clamp(player_state_->super_meter_, 0.f, NUM_ATTACK_LEVELS);

  AttackLevel new_al = (AttackLevel)(int)player_state_->super_meter_;

  if (new_al > old_al) {
    LaunchAttack(old_al);
    if (new_al == NUM_ATTACK_LEVELS) {  // hit upper bounds of meter
      player_state_->super_meter_ -= 1.f;
    }
  }

  // mercy: if losing remove attacks on life drain
  if (unscaled_percent_change < 0) {
    bool winning;
    switch (player_state_->player_number_) {
      case PLAYER_1:
        winning = GAMESTATE->tug_life_percent_p1_ > 0.5f;
        break;
      case PLAYER_2:
        winning = GAMESTATE->tug_life_percent_p1_ < 0.5f;
        break;
      default:
        winning = false;
        FAIL_M(ssprintf(
            "Invalid player number: %i", player_state_->player_number_));
    }
    if (!winning) {
      player_state_->EndActiveAttacks();
    }
  }
}

void ScoreKeeperRave::LaunchAttack(AttackLevel al) {
  PlayerNumber pn = player_state_->player_number_;

  std::string* attacks =
      GAMESTATE->cur_characters_[pn]->attacks_[al];  // [NUM_ATTACKS_PER_LEVEL]
  std::string attack_to_give;

  if (GAMESTATE->cur_characters_[pn] != nullptr) {
    attack_to_give = attacks[RandomInt(NUM_ATTACKS_PER_LEVEL)];
  } else {
    // "If you add any noteskins here, you need to make sure they're cached,
    // too." -? Noteskins probably won't work here anymore. -aj
    std::string default_attacks[8] = {"1.5x",   "2.0x",  "0.5x",  "reverse",
                                      "sudden", "boost", "brake", "wave"};
    attack_to_give = default_attacks[RandomInt(8)];
  }

  PlayerNumber to_attack = OPPOSITE_PLAYER[pn];
  PlayerState* player_state_to_attack = GAMESTATE->player_state_[to_attack];

  Attack a;
  a.level_ = al;
  a.secs_remaining_ = ATTACK_DURATION_SECONDS;
  a.modifiers_ = attack_to_give;

  // remove current attack (if any)
  player_state_to_attack->RemoveActiveAttacks();

  // apply new attack
  player_state_to_attack->LaunchAttack(a);

  //	SCREENMAN->SystemMessage( ssprintf( "attacking %d with %s", pnToAttack,
  // sAttackToGive.c_str() ) );
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
