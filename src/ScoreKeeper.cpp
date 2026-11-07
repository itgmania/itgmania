#include "ScoreKeeper.h"

#include <string>

#include "GameConstantsAndTypes.h"
#include "NoteData.h"
#include "NoteDataWithScoring.h"
#include "NoteTypes.h"
#include "PlayerState.h"
#include "RageUtil.h"
#include "global.h"

ScoreKeeper::ScoreKeeper(
    PlayerState* player_state, PlayerStageStats* player_stage_stats) {
  player_state_ = player_state;
  player_stage_stats_ = player_stage_stats;
}

void ScoreKeeper::GetScoreOfLastTapInRow(
    const NoteData& nd, int row, TapNoteScore& out, int& num_taps_in_row_out) {
  int num = 0;

  for (int track = 0; track < nd.GetNumTracks(); ++track) {
    const TapNote& tn = nd.GetTapNote(track, row);

    if (tn.type_ != TapNoteType_Tap && tn.type_ != TapNoteType_HoldHead) {
      continue;
    }
    ++num;
  }
  out = NoteDataWithScoring::LastTapNoteWithResult(nd, row).result_.tns_;
  num_taps_in_row_out = num;
}

#include "ScoreKeeperNormal.h"
#include "ScoreKeeperRave.h"
#include "ScoreKeeperShared.h"

ScoreKeeper* ScoreKeeper::MakeScoreKeeper(
    std::string class_name, PlayerState* player_state,
    PlayerStageStats* player_stage_stats) {
  if (class_name == "ScoreKeeperNormal") {
    return new ScoreKeeperNormal(player_state, player_stage_stats);
  } else if (class_name == "ScoreKeeperRave") {
    return new ScoreKeeperRave(player_state, player_stage_stats);
  } else if (class_name == "ScoreKeeperShared") {
    return new ScoreKeeperShared(player_state, player_stage_stats);
  }
  FAIL_M(ssprintf("Invalid ScoreKeeper named %s!", class_name.c_str()));
}

/*
 * (c) 2006 Steve Checkoway
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
