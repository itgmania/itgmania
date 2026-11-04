/* ScoreKeeperNormal -  */

#ifndef SCORE_KEEPER_NORMAL_H
#define SCORE_KEEPER_NORMAL_H

#include <vector>

#include "Attack.h"
#include "GameConstantsAndTypes.h"
#include "LuaReference.h"
#include "ScoreKeeper.h"
#include "ScreenMessage.h"
#include "ThemeMetric.h"

class Steps;
class Song;
struct RadarValues;
class TimingData;

AutoScreenMessage(SM_PlayToasty);

/** @brief The default ScoreKeeper implementation. */
class ScoreKeeperNormal : public ScoreKeeper {
  void AddScoreInternal(TapNoteScore score);
  int CalcNextToastyAt(int level);

  int score_remainder_;
  int max_possible_points_;
  int tap_notes_hit_;  // number of notes judged so far, needed by scoring

  int num_taps_and_holds_;
  int max_score_so_far_;  // for nonstop scoring
  int point_bonus_;       // the difference to award at the end
  int cur_toasty_combo_;
  int cur_toasty_level_;
  int next_toasty_at_;
  bool is_last_song_in_course_;
  bool is_beginner_;

  ThemeMetric<bool> combo_is_per_row_;
  ThemeMetric<bool> miss_combo_is_per_row_;
  ThemeMetric<TapNoteScore> min_score_to_continue_combo_;
  ThemeMetric<TapNoteScore> min_score_to_maintain_combo_;
  ThemeMetric<TapNoteScore> max_score_to_increment_miss_combo_;
  ThemeMetric<bool> mine_hit_increments_miss_combo_;
  ThemeMetric<bool> avoid_mine_increments_combo_;
  ThemeMetric<bool> use_internal_scoring_;

  ThemeMetric<TapNoteScore> toasty_min_tns_;
  ThemeMetric<LuaReference> toasty_trigger_;

  std::vector<Steps*> steps_;

  virtual void AddTapScore(TapNoteScore tns);
  virtual void AddHoldScore(HoldNoteScore hns);
  virtual void AddTapRowScore(TapNoteScore tns, const NoteData& nd, int iRow);

  /* Configuration: */
  /* Score after each tap will be rounded to the nearest m_iRoundTo; 1 to do
   * nothing. */
  int round_to_;
  int combo_bonus_factor_[NUM_TapNoteScore];

 public:
  ScoreKeeperNormal(
      PlayerState* pPlayerState, PlayerStageStats* pPlayerStageStats);

  void Load(
      const std::vector<Song*>& apSongs, const std::vector<Steps*>& apSteps,
      const std::vector<AttackArray>& asModifiers);

  // before a song plays (called multiple times if course)
  void OnNextSong(
      int iSongInCourseIndex, const Steps* pSteps, const NoteData* pNoteData);

  void HandleTapScore(const TapNote& tn);
  void HandleTapRowScore(const NoteData& nd, int iRow);
  void HandleHoldScore(const TapNote& tn);
  void HandleHoldActiveSeconds(float /* fMusicSecondsHeld */) {};
  void HandleHoldCheckpointScore(
      const NoteData& nd, int iRow, int iNumHoldsHeldThisRow,
      int iNumHoldsMissedThisRow);
  void HandleTapScoreNone();

  // This must be calculated using only cached radar values so that we can
  // do it quickly.
  static int GetPossibleDancePoints(
      NoteData* nd, const TimingData* td, float fSongSeconds);
  static int GetPossibleDancePoints(
      NoteData* ndPre, NoteData* ndPost, const TimingData* td,
      float fSongSeconds);
  static int GetPossibleGradePoints(
      NoteData* nd, const TimingData* td, float fSongSeconds);
  static int GetPossibleGradePoints(
      NoteData* ndPre, NoteData* ndPost, const TimingData* td,
      float fSongSeconds);

  int TapNoteScoreToDancePoints(TapNoteScore tns) const;
  int HoldNoteScoreToDancePoints(HoldNoteScore hns) const;
  static int TapNoteScoreToDancePoints(TapNoteScore tns, bool bBeginner);
  static int HoldNoteScoreToDancePoints(HoldNoteScore hns, bool bBeginner);
  static int TapNoteScoreToGradePoints(TapNoteScore tns, bool bBeginner);
  static int HoldNoteScoreToGradePoints(HoldNoteScore hns, bool bBeginner);

 private:
  /**
   * @brief Take care of some internal work with our scoring systems.
   * @param tns the Tap Note score earned.
   * @param maximum the best tap note score possible.
   * @param row the row the score was earned. Mainly for ComboSegment stuff. */
  void HandleTapNoteScoreInternal(
      TapNoteScore tns, TapNoteScore maximum, int row);
  void HandleComboInternal(
      int iNumHitContinueCombo, int iNumHitMaintainCombo, int iNumBreakCombo,
      int iRow = -1);
  void HandleRowComboInternal(
      TapNoteScore tns, int iNumTapsInRow, int iRow = -1);
  void GetRowCounts(
      const NoteData& nd, int iRow, int& iNumHitContinueCombo,
      int& iNumHitMaintainCombo, int& iNumBreakCombo);
};

#endif

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
