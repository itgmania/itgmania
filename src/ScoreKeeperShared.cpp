#include "ScoreKeeperShared.h"

#include <vector>

#include "Attack.h"
#include "GameState.h"
#include "NoteDataUtil.h"
#include "NoteTypes.h"
#include "PlayerStageStats.h"
#include "PlayerState.h"
#include "ScoreKeeper.h"
#include "ScoreKeeperNormal.h"
#include "Song.h"
#include "Steps.h"
#include "Style.h"
#include "TimingData.h"

/* In Routine, we have two Players, but the master one handles all of the
 * scoring.  The other one will just receive misses for everything, and
 * shouldn't do anything. */
ScoreKeeperShared::ScoreKeeperShared(
    PlayerState* player_state, PlayerStageStats* player_stage_stats)
    : ScoreKeeperNormal(player_state, player_stage_stats) {}

void ScoreKeeperShared::Load(
    const std::vector<Song*>& songs, const std::vector<Steps*>& ap_steps,
    const std::vector<AttackArray>& modifiers) {
  ScoreKeeperNormal::Load(songs, ap_steps, modifiers);

  const Style* style =
      GAMESTATE->GetCurrentStyle(player_state_->player_number_);
  if (style == nullptr ||
      style->style_type_ != StyleType_TwoPlayersSharedSides) {
    return;
  }

  // Routine shared scoring receives judgments from both players. Recompute
  // possible points using both composite parts so percentage stays bounded.
  int total_possible_dance_points = 0;
  int total_possible_grade_points = 0;
  for (unsigned i = 0; i < ap_steps.size(); ++i) {
    Song* song = songs[i];
    Steps* p_steps = ap_steps[i];
    ASSERT(song != nullptr);
    ASSERT(p_steps != nullptr);

    NoteData temp;
    p_steps->GetNoteData(temp);

    NoteData transformed_for_style;
    style->GetTransformedNoteDataForStyle(
        player_state_->player_number_, temp, transformed_for_style);

    std::vector<NoteData> parts;
    if (transformed_for_style.IsComposite()) {
      NoteDataUtil::SplitCompositeNoteData(transformed_for_style, parts);
    } else {
      parts.push_back(transformed_for_style);
    }

    for (const NoteData& part : parts) {
      NoteData pre = part;
      NoteDataUtil::TransformNoteData(
          pre, *(p_steps->GetTimingData()), modifiers[i], p_steps->steps_type_,
          song);

      NoteData post = pre;
      NoteDataUtil::TransformNoteData(
          post, *(p_steps->GetTimingData()),
          player_state_->player_options_.GetStage(), p_steps->steps_type_);

      GAMESTATE->SetProcessedTimingData(
          const_cast<TimingData*>(p_steps->GetTimingData()));
      total_possible_dance_points += GetPossibleDancePoints(
          &pre, &post, p_steps->GetTimingData(), song->music_length_seconds_);
      total_possible_grade_points += GetPossibleGradePoints(
          &pre, &post, p_steps->GetTimingData(), song->music_length_seconds_);
      GAMESTATE->SetProcessedTimingData(nullptr);
    }
  }

  player_stage_stats_->possible_dance_points_ = total_possible_dance_points;
  player_stage_stats_->possible_grade_points_ = total_possible_grade_points;
}

// These ScoreKeepers don't get to draw.
void ScoreKeeperShared::DrawPrimitives() {
  ScoreKeeperNormal::DrawPrimitives();
}

void ScoreKeeperShared::Update(float delta) {
  ScoreKeeperNormal::Update(delta);
}

void ScoreKeeperShared::OnNextSong(
    int song_in_course_index, const Steps* steps, const NoteData* note_data) {
  ScoreKeeperNormal::OnNextSong(song_in_course_index, steps, note_data);
}

void ScoreKeeperShared::HandleTapScore(const TapNote& tn) {
  ScoreKeeperNormal::HandleTapScore(tn);
}

void ScoreKeeperShared::HandleTapRowScore(const NoteData& nd, int row) {
  ScoreKeeperNormal::HandleTapRowScore(nd, row);
}

void ScoreKeeperShared::HandleHoldScore(const TapNote& tn) {
  ScoreKeeperNormal::HandleHoldScore(tn);
}

void ScoreKeeperShared::HandleHoldActiveSeconds(float music_seconds_held) {
  ScoreKeeperNormal::HandleHoldActiveSeconds(music_seconds_held);
}

void ScoreKeeperShared::HandleHoldCheckpointScore(
    const NoteData& nd, int row, int num_holds_held_this_row,
    int num_holds_missed_this_row) {
  ScoreKeeperNormal::HandleHoldCheckpointScore(
      nd, row, num_holds_held_this_row, num_holds_missed_this_row);
}

void ScoreKeeperShared::HandleTapScoreNone() {
  ScoreKeeperNormal::HandleTapScoreNone();
}

/*
 * (c) 2006-2010 Steve Checkoway, Glenn Maynard
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
