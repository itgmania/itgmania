#include "ScoreKeeperNormal.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "Course.h"
#include "Difficulty.h"
#include "Game.h"
#include "GameConstantsAndTypes.h"
#include "GamePreferences.h"
#include "GameState.h"
#include "LuaManager.h"
#include "MessageManager.h"
#include "NoteData.h"
#include "NoteDataUtil.h"
#include "NoteDataWithScoring.h"
#include "NoteTypes.h"
#include "PlayerNumber.h"
#include "PlayerState.h"
#include "PrefsManager.h"
#include "ProfileManager.h"
#include "RageUtil.h"
#include "ScoreKeeper.h"
#include "ScreenManager.h"
#include "Song.h"
#include "SongManager.h"
#include "StageStats.h"
#include "Steps.h"
#include "Style.h"
#include "ThemeMetric.h"
#include "TimingData.h"
#include "TimingSegments.h"
#include "global.h"

static std::string PercentScoreWeightName(size_t i) {
  return "PercentScoreWeight" + ScoreEventToString((ScoreEvent)i);
}
static std::string GradeWeightName(size_t i) {
  return "GradeWeight" + ScoreEventToString((ScoreEvent)i);
}

static ThemeMetric1D<int> g_iPercentScoreWeight(
    "ScoreKeeperNormal", PercentScoreWeightName, NUM_ScoreEvent);
static ThemeMetric1D<int> g_iGradeWeight(
    "ScoreKeeperNormal", GradeWeightName, NUM_ScoreEvent);

ScoreKeeperNormal::ScoreKeeperNormal(
    PlayerState* pPlayerState, PlayerStageStats* pPlayerStageStats)
    : ScoreKeeper(pPlayerState, pPlayerStageStats) {}

void ScoreKeeperNormal::Load(
    const std::vector<Song*>& apSongs, const std::vector<Steps*>& apSteps,
    const std::vector<AttackArray>& asModifiers) {
  steps_ = apSteps;
  ASSERT(apSongs.size() == apSteps.size());
  ASSERT(apSongs.size() == asModifiers.size());

  // True if a jump is one to combo, false if combo is purely based on tap
  // count.
  combo_is_per_row_.Load("Gameplay", "ComboIsPerRow");
  miss_combo_is_per_row_.Load("Gameplay", "MissComboIsPerRow");
  min_score_to_continue_combo_.Load("Gameplay", "MinScoreToContinueCombo");
  min_score_to_maintain_combo_.Load("Gameplay", "MinScoreToMaintainCombo");
  max_score_to_increment_miss_combo_.Load(
      "Gameplay", "MaxScoreToIncrementMissCombo");
  mine_hit_increments_miss_combo_.Load(
      "Gameplay", "MineHitIncrementsMissCombo");
  avoid_mine_increments_combo_.Load("Gameplay", "AvoidMineIncrementsCombo");
  use_internal_scoring_.Load("Gameplay", "UseInternalScoring");

  // This can be a function or a number, the type is checked when needed.
  // -Kyz
  toasty_trigger_.Load("Gameplay", "ToastyTriggersAt");
  toasty_min_tns_.Load("Gameplay", "ToastyMinTNS");

  // Fill in STATSMAN->m_CurStageStats, calculate multiplier
  int iTotalPossibleDancePoints = 0;
  int iTotalPossibleGradePoints = 0;
  for (unsigned i = 0; i < apSteps.size(); i++) {
    Song* pSong = apSongs[i];
    ASSERT(pSong != nullptr);
    Steps* pSteps = apSteps[i];
    ASSERT(pSteps != nullptr);
    const AttackArray& aa = asModifiers[i];
    NoteData ndTemp;
    pSteps->GetNoteData(ndTemp);

    // We might have been given lots of songs; don't keep them in memory
    // uncompressed.
    pSteps->Compress();

    const Style* pStyle =
        GAMESTATE->GetCurrentStyle(player_state_->player_number_);
    NoteData ndPre;
    pStyle->GetTransformedNoteDataForStyle(
        player_state_->player_number_, ndTemp, ndPre);

    if (ndPre.IsComposite()) {
      std::vector<NoteData> vParts;

      NoteDataUtil::SplitCompositeNoteData(ndPre, vParts);
      ndPre = vParts[player_state_->player_number_];
    }
    /* Compute RadarValues before applying any user-selected mods. Apply
     * Course mods and count them in the "pre" RadarValues because they're
     * forced and not chosen by the user. */
    NoteDataUtil::TransformNoteData(
        ndPre, *(pSteps->GetTimingData()), aa, pSteps->steps_type_, pSong);

    /* Apply user transforms to find out how the notes will really look.
     *
     * XXX: This is brittle: if we end up combining mods for a song differently
     * than ScreenGameplay, we'll end up with the wrong data.  We should
     * probably have eg. GAMESTATE->GetOptionsForCourse(po,so,pn) to get options
     * based on the last call to StoreSelectedOptions and the modifiers list,
     * but that'd mean moving the queues in ScreenGameplay to GameState ... */
    NoteData ndPost = ndPre;
    NoteDataUtil::TransformNoteData(
        ndPost, *(pSteps->GetTimingData()),
        player_state_->player_options_.GetStage(), pSteps->steps_type_);

    GAMESTATE->SetProcessedTimingData(
        pSteps->GetTimingData());  // XXX: Not sure why but
                                   // NoteDataUtil::CalculateRadarValues
                                   // segfaults without this
    iTotalPossibleDancePoints += this->GetPossibleDancePoints(
        &ndPre, &ndPost, pSteps->GetTimingData(), pSong->music_length_seconds_);
    iTotalPossibleGradePoints += this->GetPossibleGradePoints(
        &ndPre, &ndPost, pSteps->GetTimingData(), pSong->music_length_seconds_);
    GAMESTATE->SetProcessedTimingData(nullptr);
  }

  player_stage_stats_->possible_dance_points_ = iTotalPossibleDancePoints;
  player_stage_stats_->possible_grade_points_ = iTotalPossibleGradePoints;

  score_remainder_ = 0;
  cur_toasty_combo_ = 0;
  cur_toasty_level_ = 0;
  // Initialize m_next_toasty_at to 0 so that CalcNextToastyAt just needs to
  // add the value. -Kyz
  next_toasty_at_ = 0;
  next_toasty_at_ = CalcNextToastyAt(cur_toasty_level_);
  max_score_so_far_ = 0;
  point_bonus_ = 0;
  num_taps_and_holds_ = 0;
  is_last_song_in_course_ = false;

  Message msg("ScoreChanged");
  msg.SetParam("PlayerNumber", player_state_->player_number_);
  MESSAGEMAN->Broadcast(msg);

  memset(combo_bonus_factor_, 0, sizeof(combo_bonus_factor_));
  round_to_ = 1;
}

void ScoreKeeperNormal::OnNextSong(
    int iSongInCourseIndex, const Steps* pSteps, const NoteData* pNoteData) {
  /*
    Note on NONSTOP Mode scoring

    Nonstop mode requires the player to play 4 songs in succession, with the
    total maximum possible score for the four song set being 100,000,000. This
    comes from the sum of the four stages' maximum possible scores, which,
    regardless of song or difficulty is:

    10,000,000 for the first song
    20,000,000 for the second song
    30,000,000 for the third song
    40,000,000 for the fourth song

    We extend this to work with nonstop courses of any length.

    We also keep track of this scoring type in endless, with 100mil per
    iteration of all songs, though this score isn't actually seen anywhere right
    now.
  */
  // Calculate the score multiplier
  max_possible_points_ = 0;
  if (GAMESTATE->IsCourseMode()) {
    const int numSongsInCourse = steps_.size();
    ASSERT(numSongsInCourse != 0);

    const int iIndex = iSongInCourseIndex % numSongsInCourse;
    is_last_song_in_course_ = (iIndex + 1 == numSongsInCourse);

    if (numSongsInCourse < 10) {
      const int courseMult = (numSongsInCourse * (numSongsInCourse + 1)) / 2;
      ASSERT(courseMult >= 0);

      max_possible_points_ = (100000000 * (iIndex + 1)) / courseMult;
    } else {
      /* When we have lots of songs, the scale above biases too much: in a
       * course with 50 songs, the first song is worth 80k, the last 4mil, which
       * is too much of a difference.
       *
       * With this, each song in a 50-song course will be worth 2mil. */
      max_possible_points_ = 100000000 / numSongsInCourse;
    }
  } else {
    // long ver and marathon ver songs have higher max possible scores
    int iLengthMultiplier =
        GameState::GetNumStagesMultiplierForSong(GAMESTATE->cur_song_);

    /* This is no longer just simple additive/subtractive scoring,
     * but start with capping the score at the size of the score counter. */
    max_possible_points_ = 10 * 10000000 * iLengthMultiplier;
  }
  ASSERT(max_possible_points_ >= 0);
  max_score_so_far_ += max_possible_points_;

  GAMESTATE->SetProcessedTimingData(
      const_cast<TimingData*>(pSteps->GetTimingData()));

  num_taps_and_holds_ = pNoteData->GetNumRowsWithTapOrHoldHead() +
                        pNoteData->GetNumHoldNotes() + pNoteData->GetNumRolls();

  point_bonus_ = max_possible_points_;
  player_stage_stats_->max_score_ = max_score_so_far_;

  /* MercifulBeginner shouldn't clamp weights in course mode, even if a beginner
   * song is in a course, since that makes PlayerStageStats::GetGrade hard. */
  is_beginner_ = pSteps->GetDifficulty() == Difficulty_Beginner &&
                 !GAMESTATE->IsCourseMode();

  ASSERT(point_bonus_ >= 0);

  tap_notes_hit_ = 0;

  GAMESTATE->SetProcessedTimingData(nullptr);
}

static int GetScore(int p, int Z, int64_t S, int n) {
  /* There's a problem with the scoring system described below. Z/S is truncated
   * to an int. However, in some cases we can end up with very small base
   * scores. Each song in a 50-song nonstop course will be worth 2mil, which is
   * a base of 200k; Z/S will end up being zero.
   *
   * If we rearrange the equation to (p*Z*n) / S, this problem goes away.
   * (To do that, we need to either use 64-bit ints or rearrange it a little
   * more and use floats, since p*Z*n won't fit a 32-bit int.)  However, this
   * changes the scoring rules slightly.
   */

#if 0
	// This is the actual method described below.
	return p * (Z / S) * n;
#elif 1
  // This doesn't round down Z/S.
  return int(int64_t(p) * n * Z / S);
#else
  // This also doesn't round down Z/S. Use this if you don't have 64-bit ints.
  return int(p * n * (float(Z) / S));
#endif
}

void ScoreKeeperNormal::AddTapScore(TapNoteScore tns) {}

void ScoreKeeperNormal::AddHoldScore(HoldNoteScore hns) {
  if (hns == HNS_Held) {
    AddScoreInternal(TNS_W1);
  } else if (hns == HNS_LetGo) {
    AddScoreInternal(
        TNS_W4);  // required for subtractive score display to work properly.
  }
}

void ScoreKeeperNormal::AddTapRowScore(
    TapNoteScore score, const NoteData& nd, int iRow) {
  AddScoreInternal(score);
}

extern ThemeMetric<bool> PENALIZE_TAP_SCORE_NONE;
extern ThemeMetric<bool> TICK_HOLDS;
void ScoreKeeperNormal::HandleTapScoreNone() {
  if (PENALIZE_TAP_SCORE_NONE) {
    player_stage_stats_->cur_combo_ = 0;

    if (player_state_->player_number_ != PLAYER_INVALID) {
      MESSAGEMAN->Broadcast(enum_add2(
          Message_CurrentComboChangedP1, player_state_->player_number_));
    }
  }

  // TODO: networking code
}

void ScoreKeeperNormal::AddScoreInternal(TapNoteScore score) {
  if (use_internal_scoring_) {
    unsigned int& iScore = player_stage_stats_->score_;
    unsigned int& iCurMaxScore = player_stage_stats_->cur_max_score_;

    // See Aaron In Japan for more details about the scoring formulas.
    // Note: this assumes no custom scoring systems are in use.
    int p = 0;  // score multiplier

    switch (score) {
      case TNS_W1:
        p = 10;
        break;
      case TNS_W2:
        p = GAMESTATE->ShowW1() ? 9 : 10;
        break;
      case TNS_W3:
        p = 5;
        break;
      default:
        p = 0;
        break;
    }

    tap_notes_hit_++;

    const int64_t N = uint64_t(num_taps_and_holds_);
    const int64_t sum = (N * (N + 1)) / 2;
    const int Z = max_possible_points_ / 10;

    // Don't use a multiplier if the player has failed
    if (player_stage_stats_->failed_) {
      iScore += p;
      // make score evenly divisible by 5
      // only update this on the next step, to make it less *obvious*
      /* Round to the nearest 5, instead of always rounding down, so a base
       * score of 9 will round to 10, not 5. */
      if (p > 0) {
        iScore = ((iScore + 2) / 5) * 5;
      }
    } else {
      iScore += GetScore(p, Z, sum, tap_notes_hit_);
      const int& iCurrentCombo = player_stage_stats_->cur_combo_;
      iScore += combo_bonus_factor_[score] * iCurrentCombo;
    }

    // Subtract the maximum this step could have been worth from the bonus.
    point_bonus_ -= GetScore(10, Z, sum, tap_notes_hit_);
    // And add the maximum this step could have been worth to the max score up
    // to now.
    iCurMaxScore += GetScore(10, Z, sum, tap_notes_hit_);

    if (tap_notes_hit_ == num_taps_and_holds_ && score >= TNS_W2) {
      if (!player_stage_stats_->failed_) {
        iScore += point_bonus_;
      }
      if (is_last_song_in_course_) {
        iScore += 100000000 - max_score_so_far_;
        iCurMaxScore += 100000000 - max_score_so_far_;

        /* If we're in Endless mode, we'll come around here again, so reset
         * the bonus counter. */
        max_score_so_far_ = 0;
      }
      iCurMaxScore += point_bonus_;
    }

    // Undo rounding from the last tap, and re-round.
    iScore += score_remainder_;
    score_remainder_ = (iScore % round_to_);
    iScore = iScore - score_remainder_;

    // LOG->Trace( "score: %i", iScore );
  }
}

int ScoreKeeperNormal::CalcNextToastyAt(int level) {
  Lua* L = LUA->Get();
  toasty_trigger_.PushSelf(L);
  const int default_amount = 250;
  int amount = default_amount;
  bool erred = false;
  switch (lua_type(L, 1)) {
    case LUA_TNUMBER:
      amount = lua_tointeger(L, 1);
      break;
    case LUA_TFUNCTION: {
      std::string err = "Error running ToastyTriggersAt: ";
      LuaHelpers::Push(L, player_state_->player_number_);
      lua_pushnumber(L, level);
      if (LuaHelpers::RunScriptOnStack(L, err, 2, 1, true)) {
        if (lua_isnumber(L, -1)) {
          amount = lua_tointeger(L, -1);
        } else {
          LuaHelpers::ReportScriptError(
              "Gameplay::ToastyTriggersAt "
              "function must return a number greater than 0.");
          erred = true;
        }
      } else {
        erred = true;
      }
    } break;
    default:
      LuaHelpers::ReportScriptError(
          "Gameplay::ToastyTriggersAt metric has "
          "a nonsensical type, it must be a number or a function.");
      erred = true;
      break;
  }
  if (amount <= 0) {
    if (!erred) {
      LuaHelpers::ReportScriptError(
          "The ToastyTriggersAt value cannot be "
          "less than or equal to 0 because that would be silly.");
    }
    amount = default_amount;
  }
  lua_settop(L, 0);
  LUA->Release(L);
  return next_toasty_at_ + amount;
}

void ScoreKeeperNormal::HandleTapScore(const TapNote& tn) {
  TapNoteScore tns = tn.result_.tns_;

  if (tn.type_ == TapNoteType_Mine) {
    if (tns == TNS_HitMine) {
      if (!player_stage_stats_->failed_) {
        player_stage_stats_->actual_dance_points_ +=
            TapNoteScoreToDancePoints(TNS_HitMine);
      }
      player_stage_stats_->tap_note_scores_[TNS_HitMine] += 1;
      if (mine_hit_increments_miss_combo_) {
        HandleComboInternal(0, 0, 1);
      }
    }

    if (tns == TNS_AvoidMine && avoid_mine_increments_combo_) {
      HandleComboInternal(1, 0, 0);
    }

    Message msg("ScoreChanged");
    msg.SetParam("PlayerNumber", player_state_->player_number_);
    MESSAGEMAN->Broadcast(msg);
  }

  AddTapScore(tns);
}

void ScoreKeeperNormal::HandleHoldCheckpointScore(
    const NoteData& nd, int iRow, int iNumHoldsHeldThisRow,
    int iNumHoldsMissedThisRow) {
  HandleTapNoteScoreInternal(
      iNumHoldsMissedThisRow == 0 ? TNS_CheckpointHit : TNS_CheckpointMiss,
      TNS_CheckpointHit, iRow);
  HandleComboInternal(iNumHoldsHeldThisRow, 0, iNumHoldsMissedThisRow, iRow);
}

void ScoreKeeperNormal::HandleTapNoteScoreInternal(
    TapNoteScore tns, TapNoteScore maximum, int row) {
  // Update dance points.
  if (!player_stage_stats_->failed_) {
    player_stage_stats_->actual_dance_points_ += TapNoteScoreToDancePoints(tns);
  }

  // update judged row totals. Respect Combo segments here.
  TimingData& td =
      *GAMESTATE->cur_steps_[player_state_->player_number_]->GetTimingData();
  ComboSegment* cs = td.GetComboSegmentAtRow(row);
  if (tns == TNS_CheckpointHit || tns >= min_score_to_continue_combo_) {
    player_stage_stats_->tap_note_scores_[tns] += cs->GetCombo();
  } else if (tns == TNS_CheckpointMiss || tns < min_score_to_maintain_combo_) {
    player_stage_stats_->tap_note_scores_[tns] += cs->GetMissCombo();
  } else {
    player_stage_stats_->tap_note_scores_[tns] += 1;
  }

  // increment the current total possible dance score
  player_stage_stats_->cur_possible_dance_points_ +=
      TapNoteScoreToDancePoints(maximum);
}

void ScoreKeeperNormal::HandleComboInternal(
    int iNumHitContinueCombo, int iNumHitMaintainCombo, int iNumBreakCombo,
    int iRow) {
  // Regular combo
  if (combo_is_per_row_) {
    iNumHitContinueCombo = std::min(iNumHitContinueCombo, 1);
    iNumHitMaintainCombo = std::min(iNumHitMaintainCombo, 1);
    iNumBreakCombo = std::min(iNumBreakCombo, 1);
  }

  if (iNumHitContinueCombo > 0 || iNumHitMaintainCombo > 0) {
    player_stage_stats_->cur_miss_combo_ = 0;
  }
  TimingData& td =
      *GAMESTATE->cur_steps_[player_state_->player_number_]->GetTimingData();
  if (iNumBreakCombo == 0) {
    int multiplier =
        (iRow == -1 ? 1 : td.GetComboSegmentAtRow(iRow)->GetCombo());
    player_stage_stats_->cur_combo_ += iNumHitContinueCombo * multiplier;
  } else {
    player_stage_stats_->cur_combo_ = 0;
    int multiplier =
        (iRow == -1 ? 1 : td.GetComboSegmentAtRow(iRow)->GetMissCombo());
    player_stage_stats_->cur_miss_combo_ +=
        (miss_combo_is_per_row_ ? 1 : iNumBreakCombo) * multiplier;
  }
}

void ScoreKeeperNormal::HandleRowComboInternal(
    TapNoteScore tns, int iNumTapsInRow, int iRow) {
  if (combo_is_per_row_) {
    iNumTapsInRow = std::min(iNumTapsInRow, 1);
  }
  TimingData& td =
      *GAMESTATE->cur_steps_[player_state_->player_number_]->GetTimingData();
  if (tns >= min_score_to_continue_combo_) {
    player_stage_stats_->cur_miss_combo_ = 0;
    int multiplier =
        (iRow == -1 ? 1 : td.GetComboSegmentAtRow(iRow)->GetCombo());
    player_stage_stats_->cur_combo_ += iNumTapsInRow * multiplier;
  } else if (tns < min_score_to_maintain_combo_) {
    player_stage_stats_->cur_combo_ = 0;

    if (tns <= max_score_to_increment_miss_combo_) {
      int multiplier =
          (iRow == -1 ? 1 : td.GetComboSegmentAtRow(iRow)->GetMissCombo());
      player_stage_stats_->cur_miss_combo_ +=
          (miss_combo_is_per_row_ ? 1 : iNumTapsInRow) * multiplier;
    }
  }
}

void ScoreKeeperNormal::GetRowCounts(
    const NoteData& nd, int iRow, int& iNumHitContinueCombo,
    int& iNumHitMaintainCombo, int& iNumBreakCombo) {
  iNumHitContinueCombo = iNumHitMaintainCombo = iNumBreakCombo = 0;
  for (int track = 0; track < nd.GetNumTracks(); ++track) {
    const TapNote& tn = nd.GetTapNote(track, iRow);

    if (tn.type_ != TapNoteType_Tap && tn.type_ != TapNoteType_HoldHead &&
        tn.type_ != TapNoteType_Lift) {
      continue;
    }
    TapNoteScore tns = tn.result_.tns_;
    if (tns >= min_score_to_continue_combo_) {
      ++iNumHitContinueCombo;
    } else if (tns >= min_score_to_maintain_combo_) {
      ++iNumHitMaintainCombo;
    } else {
      ++iNumBreakCombo;
    }
  }
}

void ScoreKeeperNormal::HandleTapRowScore(const NoteData& nd, int iRow) {
  int iNumHitContinueCombo, iNumHitMaintainCombo, iNumBreakCombo;
  GetRowCounts(
      nd, iRow, iNumHitContinueCombo, iNumHitMaintainCombo, iNumBreakCombo);

  int iNumTapsInRow =
      iNumHitContinueCombo + iNumHitMaintainCombo + iNumBreakCombo;
  if (iNumTapsInRow <= 0) {
    return;
  }

  TapNoteScore scoreOfLastTap =
      NoteDataWithScoring::LastTapNoteWithResult(nd, iRow).result_.tns_;
  HandleTapNoteScoreInternal(scoreOfLastTap, TNS_W1, iRow);

  if (GAMESTATE->GetCurrentGame()->count_notes_separately_) {
    HandleComboInternal(
        iNumHitContinueCombo, iNumHitMaintainCombo, iNumBreakCombo, iRow);
  } else {
    HandleRowComboInternal(
        scoreOfLastTap, iNumTapsInRow, iRow);  // This should work?
  }

  if (player_state_->player_number_ != PLAYER_INVALID) {
    MESSAGEMAN->Broadcast(enum_add2(
        Message_CurrentComboChangedP1, player_state_->player_number_));
  }

  AddTapRowScore(scoreOfLastTap, nd, iRow);  // only score once per row

  // handle combo logic
#ifndef DEBUG
  if ((GamePreferences::m_AutoPlay != PC_HUMAN ||
       player_state_->player_options_.GetCurrent().player_auto_play_ != 0) &&
      !GAMESTATE->demonstration_or_jukebox_)  // cheaters always prosper >:D -aj
                                              // comment edit
  {
    cur_toasty_combo_ = 0;
    return;
  }
#endif  // DEBUG

  // Toasty combo
  if (scoreOfLastTap >= toasty_min_tns_) {
    cur_toasty_combo_ += iNumTapsInRow;
    if (cur_toasty_combo_ > next_toasty_at_ &&
        !GAMESTATE->demonstration_or_jukebox_) {
      ++cur_toasty_level_;
      // Broadcast the message before posting the screen message so that the
      // transition layer can catch the message to know the level and respond
      // accordingly. -Kyz
      Message msg("ToastyAchieved");
      msg.SetParam("PlayerNumber", player_state_->player_number_);
      msg.SetParam("ToastyCombo", cur_toasty_combo_);
      msg.SetParam("Level", cur_toasty_level_);
      MESSAGEMAN->Broadcast(msg);
      SCREENMAN->PostMessageToTopScreen(SM_PlayToasty, 0);
      // TODO: keep a pointer to the Profile.  Don't index with m_PlayerNumber
      // TODO: Make the profile count the level and combo of the toasty. -Kyz
      PROFILEMAN->IncrementToastiesCount(player_state_->player_number_);
      next_toasty_at_ = CalcNextToastyAt(cur_toasty_level_);
    }
  } else {
    cur_toasty_combo_ = 0;
    cur_toasty_level_ = 0;
    next_toasty_at_ = 0;
    next_toasty_at_ = CalcNextToastyAt(cur_toasty_level_);
    Message msg("ToastyDropped");
    msg.SetParam("PlayerNumber", player_state_->player_number_);
    MESSAGEMAN->Broadcast(msg);
  }

  // TODO: Remove indexing with PlayerNumber
  PlayerNumber pn = player_state_->player_number_;
  float offset = NoteDataWithScoring::LastTapNoteWithResult(nd, iRow)
                     .result_.tap_note_offset_;
  Message msg("ScoreChanged");
  msg.SetParam("PlayerNumber", player_state_->player_number_);
  msg.SetParam("ToastyCombo", cur_toasty_combo_);
  MESSAGEMAN->Broadcast(msg);
}

void ScoreKeeperNormal::HandleHoldScore(const TapNote& tn) {
  HoldNoteScore holdScore = tn.hold_result_.hns_;

  // update dance points totals
  if (!player_stage_stats_->failed_) {
    player_stage_stats_->actual_dance_points_ +=
        HoldNoteScoreToDancePoints(holdScore);
  }
  // increment the current total possible dance score
  player_stage_stats_->cur_possible_dance_points_ +=
      HoldNoteScoreToDancePoints(HNS_Held);
  player_stage_stats_->hold_note_scores_[holdScore]++;

  AddHoldScore(holdScore);

  // TODO: Remove indexing with PlayerNumber
  PlayerNumber pn = player_state_->player_number_;
  Message msg("ScoreChanged");
  msg.SetParam("PlayerNumber", player_state_->player_number_);
  MESSAGEMAN->Broadcast(msg);
}

int ScoreKeeperNormal::GetPossibleDancePoints(
    NoteData* nd, const TimingData* td, float fSongSeconds) {
  /* Note: If W1 timing is disabled or not active (not course mode),
   * W2 will be used instead. */
  // XXX: That's not actually implemented!
  RadarValues radars;
  NoteDataUtil::CalculateRadarValues(*nd, fSongSeconds, radars);

  int ret = 0;

  ret += int(radars[RadarCategory_TapsAndHolds]) *
         TapNoteScoreToDancePoints(TNS_W1, false);
  if (TICK_HOLDS) {
    ret += NoteDataUtil::GetTotalHoldTicks(nd, td) *
           g_iPercentScoreWeight.GetValue(SE_CheckpointHit);
  }
  ret += int(radars[RadarCategory_Holds]) *
         HoldNoteScoreToDancePoints(HNS_Held, false);
  ret += int(radars[RadarCategory_Rolls]) *
         HoldNoteScoreToDancePoints(HNS_Held, false);

  return ret;
}

int ScoreKeeperNormal::GetPossibleDancePoints(
    NoteData* ndPre, NoteData* ndPost, const TimingData* td,
    float fSongSeconds) {
  /* The logic here is that if you use a modifier that adds notes, you should
   * have to hit the new notes to get a high grade. However, if you use one
   * that removes notes, they should simply be counted as misses. */
  return std::max(
      GetPossibleDancePoints(ndPre, td, fSongSeconds),
      GetPossibleDancePoints(ndPost, td, fSongSeconds));
}

int ScoreKeeperNormal::GetPossibleGradePoints(
    NoteData* nd, const TimingData* td, float fSongSeconds) {
  /* Note: if W1 timing is disabled or not active (not course mode),
   * W2 will be used instead. */
  // XXX: That's not actually implemented!
  RadarValues radars;
  NoteDataUtil::CalculateRadarValues(*nd, fSongSeconds, radars);

  int ret = 0;

  ret += int(radars[RadarCategory_TapsAndHolds]) *
         TapNoteScoreToGradePoints(TNS_W1, false);
  if (TICK_HOLDS) {
    ret += NoteDataUtil::GetTotalHoldTicks(nd, td) *
           g_iGradeWeight.GetValue(SE_CheckpointHit);
  }
  ret += int(radars[RadarCategory_Holds]) *
         HoldNoteScoreToGradePoints(HNS_Held, false);
  ret += int(radars[RadarCategory_Rolls]) *
         HoldNoteScoreToGradePoints(HNS_Held, false);

  return ret;
}

int ScoreKeeperNormal::GetPossibleGradePoints(
    NoteData* ndPre, NoteData* ndPost, const TimingData* td,
    float fSongSeconds) {
  /* The logic here is that if you use a modifier that adds notes, you should
   * have to hit the new notes to get a high grade. However, if you use one
   * that removes notes, they should simply be counted as misses. */
  return std::max(
      GetPossibleGradePoints(ndPre, td, fSongSeconds),
      GetPossibleGradePoints(ndPost, td, fSongSeconds));
}

int ScoreKeeperNormal::TapNoteScoreToDancePoints(TapNoteScore tns) const {
  return TapNoteScoreToDancePoints(tns, is_beginner_);
}

int ScoreKeeperNormal::HoldNoteScoreToDancePoints(HoldNoteScore hns) const {
  return HoldNoteScoreToDancePoints(hns, is_beginner_);
}

int ScoreKeeperNormal::TapNoteScoreToDancePoints(
    TapNoteScore tns, bool bBeginner) {
  if (!GAMESTATE->ShowW1() && tns == TNS_W1) {
    tns = TNS_W2;
  }

  /* This is used for Oni percentage displays. Grading values are currently in
   * StageStats::GetGrade. */
  int iWeight = 0;
  switch (tns) {
    DEFAULT_FAIL(tns);
    case TNS_None:
      iWeight = 0;
      break;
    case TNS_HitMine:
      iWeight = g_iPercentScoreWeight.GetValue(SE_HitMine);
      break;
    case TNS_Miss:
      iWeight = g_iPercentScoreWeight.GetValue(SE_Miss);
      break;
    case TNS_W5:
      iWeight = g_iPercentScoreWeight.GetValue(SE_W5);
      break;
    case TNS_W4:
      iWeight = g_iPercentScoreWeight.GetValue(SE_W4);
      break;
    case TNS_W3:
      iWeight = g_iPercentScoreWeight.GetValue(SE_W3);
      break;
    case TNS_W2:
      iWeight = g_iPercentScoreWeight.GetValue(SE_W2);
      break;
    case TNS_W1:
      iWeight = g_iPercentScoreWeight.GetValue(SE_W1);
      break;
    case TNS_CheckpointHit:
      iWeight = g_iPercentScoreWeight.GetValue(SE_CheckpointHit);
      break;
    case TNS_CheckpointMiss:
      iWeight = g_iPercentScoreWeight.GetValue(SE_CheckpointMiss);
      break;
  }
  if (bBeginner && PREFSMAN->merciful_beginner_) {
    iWeight = std::max(0, iWeight);
  }
  return iWeight;
}

int ScoreKeeperNormal::HoldNoteScoreToDancePoints(
    HoldNoteScore hns, bool bBeginner) {
  int iWeight = 0;
  switch (hns) {
    DEFAULT_FAIL(hns);
    case HNS_None:
      iWeight = 0;
      break;
    case HNS_LetGo:
      iWeight = g_iPercentScoreWeight.GetValue(SE_LetGo);
      break;
    case HNS_Held:
      iWeight = g_iPercentScoreWeight.GetValue(SE_Held);
      break;
    case HNS_Missed:
      iWeight = g_iPercentScoreWeight.GetValue(SE_Missed);
      break;
  }
  if (bBeginner && PREFSMAN->merciful_beginner_) {
    iWeight = std::max(0, iWeight);
  }
  return iWeight;
}

int ScoreKeeperNormal::TapNoteScoreToGradePoints(
    TapNoteScore tns, bool bBeginner) {
  if (!GAMESTATE->ShowW1() && tns == TNS_W1) {
    tns = TNS_W2;
  }

  /* This is used for Oni percentage displays. Grading values are currently in
   * StageStats::GetGrade. */
  int iWeight = 0;
  switch (tns) {
    DEFAULT_FAIL(tns);
    case TNS_None:
      iWeight = 0;
      break;
    case TNS_AvoidMine:
      iWeight = 0;
      break;
    case TNS_HitMine:
      iWeight = g_iGradeWeight.GetValue(SE_HitMine);
      break;
    case TNS_Miss:
      iWeight = g_iGradeWeight.GetValue(SE_Miss);
      break;
    case TNS_W5:
      iWeight = g_iGradeWeight.GetValue(SE_W5);
      break;
    case TNS_W4:
      iWeight = g_iGradeWeight.GetValue(SE_W4);
      break;
    case TNS_W3:
      iWeight = g_iGradeWeight.GetValue(SE_W3);
      break;
    case TNS_W2:
      iWeight = g_iGradeWeight.GetValue(SE_W2);
      break;
    case TNS_W1:
      iWeight = g_iGradeWeight.GetValue(SE_W1);
      break;
    case TNS_CheckpointHit:
      iWeight = g_iGradeWeight.GetValue(SE_CheckpointHit);
      break;
    case TNS_CheckpointMiss:
      iWeight = g_iGradeWeight.GetValue(SE_CheckpointMiss);
      break;
  }
  if (bBeginner && PREFSMAN->merciful_beginner_) {
    iWeight = std::max(0, iWeight);
  }
  return iWeight;
}

int ScoreKeeperNormal::HoldNoteScoreToGradePoints(
    HoldNoteScore hns, bool bBeginner) {
  int iWeight = 0;
  switch (hns) {
    DEFAULT_FAIL(hns);
    case HNS_None:
      iWeight = 0;
      break;
    case HNS_LetGo:
      iWeight = g_iGradeWeight.GetValue(SE_LetGo);
      break;
    case HNS_Held:
      iWeight = g_iGradeWeight.GetValue(SE_Held);
      break;
    case HNS_Missed:
      iWeight = g_iGradeWeight.GetValue(SE_Missed);
      break;
  }
  if (bBeginner && PREFSMAN->merciful_beginner_) {
    iWeight = std::max(0, iWeight);
  }
  return iWeight;
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
