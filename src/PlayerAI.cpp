#include "PlayerAI.h"

#include <string>

#include "GameConstantsAndTypes.h"
#include "GameState.h"
#include "IniFile.h"
#include "LuaManager.h"
#include "PlayerState.h"
#include "RageUtil.h"
#include "RageUtil/RandomNumbers.h"
#include "XmlFile.h"
#include "global.h"

#define AI_PATH "Data/AI.ini"

struct TapScoreDistribution {
  float percent_[NUM_TapNoteScore];

  void ChangeWeightsToPercents() {
    float sum = 0;
    for (int i = 0; i < NUM_TapNoteScore; ++i) {
      sum += percent_[i];
    }
    for (int i = 0; i < NUM_TapNoteScore; ++i) {
      percent_[i] /= sum;
    }
  }
  void SetDefaultWeights() {
    percent_[TNS_None] = 0;
    percent_[TNS_Miss] = 1;
    percent_[TNS_W5] = 0;
    percent_[TNS_W4] = 0;
    percent_[TNS_W3] = 0;
    percent_[TNS_W2] = 0;
    percent_[TNS_W1] = 0;
  }

  TapNoteScore GetTapNoteScore() {
    float rand = randomf(0, 1);
    float cumulative_percent = 0;
    for (int i = 0; i <= TNS_W1; i++) {
      cumulative_percent += percent_[i];
      if (rand <= cumulative_percent + 1e-4) {  // rounding error
        return (TapNoteScore)i;
      }
    }
    // the fCumulativePercents must sum to 1.0, so we should never get here!
    ASSERT_M(0, ssprintf("%f,%f", rand, cumulative_percent));
    return TNS_W1;
  }
};

static TapScoreDistribution distributions[kNumSkillLevels];

void PlayerAI::InitFromDisk() {
  IniFile ini;
  bool success = ini.ReadFile(AI_PATH);
  if (!success) {
    LuaHelpers::ReportScriptErrorFmt(
        "Error trying to read \"%s\" to load AI player skill settings.",
        AI_PATH);
    for (int i = 0; i < kNumSkillLevels; ++i) {
      distributions[i].SetDefaultWeights();
      distributions[i].ChangeWeightsToPercents();
    }
  } else {
    for (int i = 0; i < kNumSkillLevels; i++) {
      std::string sKey = ssprintf("Skill%d", i);
      XNode* node = ini.GetChild(sKey);
      TapScoreDistribution& dist = distributions[i];
      if (node == nullptr) {
        LuaHelpers::ReportScriptErrorFmt(
            "AI.ini: \"%s\" section doesn't exist.", sKey.c_str());
        dist.SetDefaultWeights();
      } else {
#define SET_MALF_IF(condition, tns)                                         \
  if (condition) {                                                          \
    LuaHelpers::ReportScriptErrorFmt(                                       \
        "AI weight for " #tns " in \"%s\" section not set.", sKey.c_str()); \
    dist.percent_[tns] = 0;                                                 \
  }
        dist.percent_[TNS_None] = 0;
        success = node->GetAttrValue("WeightMiss", dist.percent_[TNS_Miss]);
        SET_MALF_IF(!success, TNS_Miss);
        success = node->GetAttrValue("WeightW5", dist.percent_[TNS_W5]);
        SET_MALF_IF(!success, TNS_W5);
        success = node->GetAttrValue("WeightW4", dist.percent_[TNS_W4]);
        SET_MALF_IF(!success, TNS_W4);
        success = node->GetAttrValue("WeightW3", dist.percent_[TNS_W3]);
        SET_MALF_IF(!success, TNS_W3);
        success = node->GetAttrValue("WeightW2", dist.percent_[TNS_W2]);
        SET_MALF_IF(!success, TNS_W2);
        success = node->GetAttrValue("WeightW1", dist.percent_[TNS_W1]);
        SET_MALF_IF(!success, TNS_W1);
#undef SET_MALF_IF
      }
      dist.ChangeWeightsToPercents();
    }
  }
}

TapNoteScore PlayerAI::GetTapNoteScore(const PlayerState* player_state) {
  if (player_state->player_controller_ == PC_AUTOPLAY) {
    return TNS_W1;
  }

  /*
  // handle replay data playback -aj
  if( pPlayerState->m_PlayerController == PC_REPLAY )
  {
          // ghost house
  }
  */

  int cpu_skill = player_state->cpu_skill_;

  /* If we're in battle mode, reduce the skill based on the number of modifier
   * attacks against us. If we're in demonstration or jukebox mode with
   * modifiers attached, don't make demonstration miss a lot. */
  // Idea: weight certain modifiers (boomerang, tornado) more? (to simulate
  // readability problems) -aj
  if (!GAMESTATE->demonstration_or_jukebox_) {
    int sum_of_attack_levels =
        player_state->seconds_until_attacks_phased_out_ > 0
            ? player_state->last_positive_sum_of_attack_levels_
            : 0;

    ASSERT_M(
        cpu_skill >= 0 && cpu_skill < kNumSkillLevels,
        ssprintf("%i", cpu_skill));
    ASSERT_M(
        player_state->player_controller_ == PC_CPU,
        ssprintf("%i", player_state->player_controller_));

    cpu_skill -= sum_of_attack_levels * 3;
    rage_clamp(cpu_skill, 0, kNumSkillLevels - 1);
  }

  TapScoreDistribution& distribution = distributions[cpu_skill];

  TapNoteScore score = distribution.GetTapNoteScore();

  const auto& disabled_windows =
      player_state->player_options_.GetCurrent().disabled_windows_;
  // Downgrade the TapNoteScore if that specific window is disabled.
  if (score == TNS_W1 && disabled_windows[TW_W1]) {
    score = TNS_W2;
  }
  if (score == TNS_W2 && disabled_windows[TW_W2]) {
    score = TNS_W3;
  }
  if (score == TNS_W3 && disabled_windows[TW_W3]) {
    score = TNS_W4;
  }
  if (score == TNS_W4 && disabled_windows[TW_W4]) {
    score = TNS_W5;
  }
  if (score == TNS_W5 && disabled_windows[TW_W5]) {
    score = TNS_None;
  }
  return score;
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
