#include "NoteDataWithScoring.h"

#include <algorithm>
#include <cstddef>
#include <vector>

#include "EnumHelper.h"
#include "Game.h"
#include "GameConstantsAndTypes.h"
#include "GameState.h"
#include "NoteData.h"
#include "NoteTypes.h"
#include "PlayerNumber.h"
#include "PlayerStageStats.h"
#include "RageUtil.h"
#include "ThemeMetric.h"
#include "TimingData.h"
#include "global.h"

extern ThemeMetric<bool> TICK_HOLDS;

namespace {

// ThemeMetric<TapNoteScoreJudgeType> LAST_OR_MINIMUM_TNS
// ("Gameplay","LastOrMinimumTapNoteScore");
static ThemeMetric<TapNoteScore> MIN_SCORE_TO_MAINTAIN_COMBO(
    "Gameplay", "MinScoreToMaintainCombo");

/* Return the last tap score of a row: the grade of the tap that completed
 * the row.  If the row has no tap notes, return -1.  If any tap notes aren't
 * graded (any tap is TNS_None) or are missed (TNS_Miss), return it. */
int LastTapNoteScoreTrack(const NoteData& in, unsigned iRow, PlayerNumber pn) {
  float scoretime = -9999;
  int best_track = -1;
  for (int t = 0; t < in.GetNumTracks(); t++) {
    /* Skip empty tracks and mines */
    const TapNote& tn = in.GetTapNote(t, iRow);
    if (tn.type_ == TapNoteType_Empty || tn.type_ == TapNoteType_Mine ||
        tn.type_ == TapNoteType_Fake || tn.type_ == TapNoteType_AutoKeysound) {
      continue;
    }
    if (tn.pn_ != PLAYER_INVALID && tn.pn_ != pn && pn != PLAYER_INVALID) {
      continue;
    }

    TapNoteScore tns = tn.result_.tns_;

    if (tns == TNS_Miss || tns == TNS_None) {
      return t;
    }

    float tm = tn.result_.tap_note_offset_;
    if (tm < scoretime) {
      continue;
    }

    scoretime = tm;
    best_track = t;
  }

  return best_track;
}

}  // namespace

const TapNote& NoteDataWithScoring::LastTapNoteWithResult(
    const NoteData& in, unsigned iRow, PlayerNumber plnum) {
  // Allow this to be configurable between LastTapNoteScoreTrack and
  // MinTapNoteScore; this change inspired by PumpMania (Zmey, et al) -aj
  /*
  LOG->Trace( ssprintf("hi i'm
  NoteDataWithScoring::LastTapNoteWithResult(NoteData in, iRow=%i, PlayerNumber
  pn)", iRow) ); int iTrack = 0; switch(LAST_OR_MINIMUM_TNS)
  {
          case TapNoteScoreJudgeType_MinimumScore:
                  iTrack = MinTapNoteScoreTrack( in, iRow, pn );
                  LOG->Trace( ssprintf("TapNoteScoreJudgeType_MinimumScore omg
  iTrack is %i and iRow is %i",iTrack,iRow) ); break; case
  TapNoteScoreJudgeType_LastScore: default: iTrack = LastTapNoteScoreTrack( in,
  iRow, pn ); break;
  }
  */
  int iTrack = LastTapNoteScoreTrack(in, iRow, plnum);
  if (iTrack == -1) {
    return TAP_EMPTY;
  }

  // LOG->Trace( ssprintf("returning in.GetTapNote(iTrack=%i, iRow=%i)", iTrack,
  // iRow) );
  return in.GetTapNote(iTrack, iRow);
}

/* Return the minimum tap score of a row.  If the row isn't complete (not all
 * taps have been hit), return TNS_None or TNS_Miss. */
TapNoteScore NoteDataWithScoring::MinTapNoteScore(
    const NoteData& in, unsigned row, PlayerNumber plnum) {
  // LOG->Trace("Hey I'm NoteDataWithScoring::MinTapNoteScore");
  TapNoteScore score = TNS_W1;
  for (int t = 0; t < in.GetNumTracks(); t++) {
    // Ignore mines (and fake arrows), or the score will always be TNS_None.
    const TapNote& tn = in.GetTapNote(t, row);
    if (tn.type_ == TapNoteType_Empty || tn.type_ == TapNoteType_Mine ||
        tn.type_ == TapNoteType_Fake || tn.type_ == TapNoteType_AutoKeysound ||
        (plnum != PlayerNumber_Invalid && tn.pn_ != plnum)) {
      continue;
    }
    score = std::min(score, tn.result_.tns_);
  }

  // LOG->Trace( ssprintf("OMG score is??
  // %s",TapNoteScoreToString(score).c_str()) );
  return score;
}

bool NoteDataWithScoring::IsRowCompletelyJudged(
    const NoteData& in, unsigned row, PlayerNumber plnum) {
  return MinTapNoteScore(in, row, plnum) >= TNS_Miss;
}

namespace {
// Return the ratio of actual combo to max combo.
float GetActualVoltageRadarValue(
    const NoteData& in, float fSongSeconds, const PlayerStageStats& pss) {
  /* STATSMAN->m_CurStageStats.iMaxCombo is unrelated to GetNumTapNotes:
   * m_bComboContinuesBetweenSongs might be on, and the way combo is counted
   * varies depending on the mode and score keeper. Instead, let's use the
   * length of the longest recorded combo. This is only subtly different:
   * it's the percent of the song the longest combo took to get. */
  const PlayerStageStats::Combo_t MaxCombo = pss.GetMaxCombo();
  float fComboPercent =
      SCALE(MaxCombo.size_seconds_, 0, fSongSeconds, 0.0f, 1.0f);
  return std::clamp(fComboPercent, 0.0f, 1.0f);
}

// Return the ratio of actual to possible dance points.
float GetActualChaosRadarValue(
    const NoteData& in, float fSongSeconds, const PlayerStageStats& pss) {
  const int iPossibleDP = pss.possible_dance_points_;
  if (iPossibleDP == 0) {
    return 1;
  }

  const int ActualDP = pss.actual_dance_points_;
  return std::clamp(float(ActualDP) / iPossibleDP, 0.0f, 1.0f);
}
}  // namespace

struct hold_status {
  int end_row_;
  int last_held_row_;
  hold_status(int e, int l) : end_row_(e), last_held_row_(l) {}
};

struct garv_state {
  int curr_row_;
  int notes_hit_for_stream_;
  int jumps_hit_for_air_;
  int holds_held_;
  int rolls_held_;
  int notes_hit_;
  int taps_hit_;
  int jumps_hit_;
  int hands_hit_;
  int mines_avoided_;
  int lifts_hit_;
  // hold_ends tracks where currently active holds will end, which is used
  // to count the number of hands. -Kyz
  std::vector<hold_status> hold_ends_;
  int num_notes_on_curr_row_;
  // num_holds_on_curr_row saves us the work of tracking where holds started
  // just to keep a jump of two holds from counting as a hand.
  int num_holds_on_curr_row_;
  // last_tns_on_row and last_time_on_row are used for deciding whether a jump
  // or hand was successfully hit.
  TapNoteScore last_tns_on_row_;
  float last_time_on_row_;
  // A hand is considered missed if any of the notes is missed.
  TapNoteScore worst_tns_on_row_;
  // TODO?  Make these configurable in some way?
  TapNoteScore stream_tns_;
  TapNoteScore air_tns_;
  TapNoteScore taps_tns_;
  TapNoteScore jumps_tns_;
  TapNoteScore hands_tns_;
  TapNoteScore lifts_tns_;
  bool judgable_;
  garv_state()
      : curr_row_(-1),
        notes_hit_for_stream_(0),
        jumps_hit_for_air_(0),
        holds_held_(0),
        rolls_held_(0),
        notes_hit_(0),
        taps_hit_(0),
        jumps_hit_(0),
        hands_hit_(0),
        mines_avoided_(0),
        lifts_hit_(0),
        num_notes_on_curr_row_(0),
        num_holds_on_curr_row_(0),
        last_tns_on_row_(TapNoteScore_Invalid),
        last_time_on_row_(-9999),
        worst_tns_on_row_(TapNoteScore_Invalid),
        stream_tns_(TNS_W2),
        air_tns_(TNS_W2),
        taps_tns_(TNS_W4),
        jumps_tns_(TNS_W4),
        hands_tns_(TNS_W4),
        lifts_tns_(MIN_SCORE_TO_MAINTAIN_COMBO),
        judgable_(false) {}
};

static void DoRowEndRadarActualCalc(garv_state& state, RadarValues& out) {
  if (state.judgable_ && state.last_tns_on_row_ != TapNoteScore_Invalid) {
    if (state.num_notes_on_curr_row_ >= 1) {
      state.taps_hit_ += (state.last_tns_on_row_ >= state.taps_tns_);
    }
    if (state.num_notes_on_curr_row_ >= 2) {
      state.jumps_hit_for_air_ += (state.last_tns_on_row_ >= state.air_tns_);
      state.jumps_hit_ += (state.last_tns_on_row_ >= state.jumps_tns_);
    }
    if (state.num_notes_on_curr_row_ +
            (state.hold_ends_.size() - state.num_holds_on_curr_row_) >=
        3) {
      if (state.worst_tns_on_row_ >= state.hands_tns_) {
        size_t holds_down = 0;
        for (size_t n = 0; n < state.hold_ends_.size(); ++n) {
          holds_down += (state.curr_row_ <= state.hold_ends_[n].last_held_row_);
        }
        state.hands_hit_ += (holds_down == state.hold_ends_.size());
      }
    }
  }
}

static void UpdateHittable(int curr_row, int& first, int& last) {
  if (first == -1) {
    first = curr_row;
  }
  last = curr_row;
}

void NoteDataWithScoring::GetActualRadarValues(
    const NoteData& in, const PlayerStageStats& pss, float song_seconds,
    RadarValues& out) {
  // Anybody editing this function should also examine
  // NoteDataUtil::CalculateRadarValues to make sure it handles things the
  // same way.
  // Some of this logic is similar or identical to
  // NoteDataUtil::CalculateRadarValues because I couldn't figure out a good
  // way to combine them into one. -Kyz
  PlayerNumber pn = pss.player_number_;
  garv_state state;

  NoteData::all_tracks_const_iterator curr_note =
      in.GetTapNoteRangeAllTracks(0, MAX_NOTE_ROW);
  TimingData* timing = GAMESTATE->GetProcessedTimingData();
  // first_hittable_row and last_hittable_row exist so that
  // GetActualVoltageRadarValue can be passed the correct song length.
  // GetActualVoltageRadarValue scores based on the max combo, a full combo
  // is a full voltage score.  The song length is used instead of trying to
  // figure out the max combo for the song because rolls mean there isn't a
  // limit to the max combo. -Kyz
  int first_hittable_row = -1;
  int last_hittable_row = -1;
  bool tick_holds = TICK_HOLDS;

  while (!curr_note.IsAtEnd()) {
    if (curr_note.Row() != state.curr_row_) {
      DoRowEndRadarActualCalc(state, out);
      state.curr_row_ = curr_note.Row();
      state.num_notes_on_curr_row_ = 0;
      state.num_holds_on_curr_row_ = 0;
      state.judgable_ = timing->IsJudgableAtRow(state.curr_row_);
      for (size_t n = 0; n < state.hold_ends_.size(); ++n) {
        if (state.hold_ends_[n].end_row_ < state.curr_row_) {
          state.hold_ends_.erase(state.hold_ends_.begin() + n);
          --n;
        }
      }
      state.last_tns_on_row_ = TapNoteScore_Invalid;
      state.last_time_on_row_ = -9999;
      state.worst_tns_on_row_ = TapNoteScore_Invalid;
    }
    bool for_this_player = curr_note->pn_ == pn || pn == PLAYER_INVALID ||
                           curr_note->pn_ == PLAYER_INVALID;
    if (state.judgable_ && for_this_player) {
      switch (curr_note->type_) {
        case TapNoteType_HoldTail:
          // If there are tick holds, then the hold tail needs to be counted
          // in last_hittable_row because that's where the combo will end.
          // -Kyz
          if (tick_holds) {
            UpdateHittable(
                state.curr_row_, first_hittable_row, last_hittable_row);
          }
          break;
        case TapNoteType_Tap:
        case TapNoteType_HoldHead:
          // HoldTails and Attacks are counted by IsTap.  But it doesn't
          // make sense to count HoldTails as hittable notes. -Kyz
        case TapNoteType_Attack:
        case TapNoteType_Lift:
          UpdateHittable(
              state.curr_row_, first_hittable_row, last_hittable_row);
          ++state.num_notes_on_curr_row_;
          state.notes_hit_for_stream_ +=
              (curr_note->result_.tns_ >= state.stream_tns_);
          state.notes_hit_ += (curr_note->result_.tns_ >= state.taps_tns_);
          if (curr_note->result_.tns_ < state.worst_tns_on_row_) {
            state.worst_tns_on_row_ = curr_note->result_.tns_;
          }
          if (curr_note->result_.tap_note_offset_ > state.last_time_on_row_) {
            state.last_time_on_row_ = curr_note->result_.tap_note_offset_;
            state.last_tns_on_row_ = curr_note->result_.tns_;
          }
          if (curr_note->type_ == TapNoteType_HoldHead) {
            if (curr_note->sub_type_ == TapNoteSubType_Hold) {
              state.holds_held_ += (curr_note->hold_result_.hns_ == HNS_Held);
            } else if (curr_note->sub_type_ == TapNoteSubType_Roll) {
              state.rolls_held_ += (curr_note->hold_result_.hns_ == HNS_Held);
            }
            state.hold_ends_.push_back(hold_status(
                state.curr_row_ + curr_note->duration_,
                curr_note->hold_result_.last_held_row_));
            ++state.num_holds_on_curr_row_;
          } else if (curr_note->type_ == TapNoteType_Lift) {
            state.lifts_hit_ += (curr_note->result_.tns_ >= state.lifts_tns_);
          }
          break;
        case TapNoteType_Mine:
          state.mines_avoided_ += (curr_note->result_.tns_ == TNS_AvoidMine);
          break;
        case TapNoteType_Fake:
        default:
          break;
      }
    }
    ++curr_note;
  }
  DoRowEndRadarActualCalc(state, out);

  // ScreenGameplay passes in the RadarValues that were calculated by
  // NoteDataUtil::CalculateRadarValues, so those are reused here. -Kyz
  int note_count = out[RadarCategory_Notes];
  int jump_count = out[RadarCategory_Jumps];
  int hold_count = out[RadarCategory_Holds];
  float hittable_steps_length = std::max(
      0.0f,
      timing->GetElapsedTimeFromBeat(NoteRowToBeat(last_hittable_row)) -
          timing->GetElapsedTimeFromBeat(NoteRowToBeat(first_hittable_row)));
  // The for loop and the assert are used to ensure that all fields of
  // RadarValue get set in here.
  FOREACH_ENUM(RadarCategory, rc) {
    switch (rc) {
      case RadarCategory_Stream:
        out[rc] = note_count == 0
                      ? 0.0f
                      : std::clamp(
                            float(state.notes_hit_for_stream_) / note_count,
                            0.0f, 1.0f);
        break;
      case RadarCategory_Voltage:
        out[rc] = GetActualVoltageRadarValue(in, hittable_steps_length, pss);
        break;
      case RadarCategory_Air:
        out[rc] =
            jump_count == 0
                ? 0.0f
                : std::clamp(
                      float(state.jumps_hit_for_air_) / jump_count, 0.0f, 1.0f);
        break;
      case RadarCategory_Freeze:
        out[rc] =
            hold_count == 0
                ? 0.0f
                : std::clamp(float(state.holds_held_) / hold_count, 0.0f, 1.0f);
        break;
      case RadarCategory_Chaos:
        out[rc] = GetActualChaosRadarValue(in, song_seconds, pss);
        break;
      case RadarCategory_TapsAndHolds:
        out[rc] = state.taps_hit_;
        break;
      case RadarCategory_Jumps:
        out[rc] = state.jumps_hit_;
        break;
      case RadarCategory_Holds:
        out[rc] = state.holds_held_;
        break;
      case RadarCategory_Mines:
        out[rc] = state.mines_avoided_;
        break;
      case RadarCategory_Hands:
        out[rc] = state.hands_hit_;
        break;
      case RadarCategory_Rolls:
        out[rc] = state.rolls_held_;
        break;
      case RadarCategory_Lifts:
        out[rc] = state.lifts_hit_;
        break;
      case RadarCategory_Fakes:
        out[rc] = out[rc];
        break;
      case RadarCategory_Notes:
        out[rc] = state.notes_hit_;
        break;
        DEFAULT_FAIL(rc);
    }
  }
}

/*
 * (c) 2001-2004 Chris Danford, Glenn Maynard
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
