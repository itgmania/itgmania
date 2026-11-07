#ifndef PlayerStageStats_H
#define PlayerStageStats_H

#include <map>
#include <string>
#include <vector>

#include "GameConstantsAndTypes.h"
#include "Grade.h"
#include "HighScore.h"
#include "PlayerNumber.h"
#include "RadarValues.h"

class Steps;
class Style;
struct lua_State;
/** @brief Contains statistics for one stage of play - either one song, or a
 * whole course. */
class PlayerStageStats {
 public:
  /** @brief Set up the PlayerStageStats with default values. */
  PlayerStageStats() { InternalInit(); }
  void InternalInit();
  void Init(PlayerNumber pn);

  /**
   * @brief Add stats from one PlayerStageStats to another.
   * @param other the other stats to add to this one. */
  void AddStats(const PlayerStageStats& other);  // accumulate

  Grade GetGrade() const;
  static float MakePercentScore(int actual, int possible);
  static std::string FormatPercentScore(float percent_score);
  float GetPercentDancePoints() const;

  int GetLessonScoreActual() const;
  int GetLessonScoreNeeded() const;
  void ResetScoreForLesson();

  PlayerNumber player_number_;
  const Style* style_;

  bool joined_;
  bool player_can_achieve_full_combo_;
  std::vector<Steps*> possible_steps_;
  int steps_played_;  // how many of m_vpPossibleStepshow many of
                      // m_vpPossibleSteps were played
  /**
   * @brief How far into the music did the Player last before failing?
   *
   * This is updated by Gameplay, and scaled by the music rate. */
  float alive_seconds_;

  /**
   * @brief Have the Players failed at any point during the song?
   *
   * If FAIL_OFF is in use, this is always false.
   *
   * If health recovery is possible after failing (requires two players),
   * this is only set if both players were failing at the same time. */
  bool failed_;

  int possible_dance_points_;
  int cur_possible_dance_points_;
  int actual_dance_points_;
  int possible_grade_points_;
  int tap_note_scores_[NUM_TapNoteScore];
  int hold_note_scores_[NUM_HoldNoteScore];
  /** @brief The Player's current combo. */
  unsigned int cur_combo_;
  /** @brief The Player's max combo. */
  unsigned int max_combo_;
  /** @brief The Player's current miss combo. */
  unsigned int cur_miss_combo_;
  int cur_score_multiplier_;
  /** @brief The player's current score. */
  unsigned int score_;
  /** @brief The theoretically highest score the Player could have at this
   * point. */
  unsigned int cur_max_score_;
  /** @brief The maximum score the Player can get this goaround. */
  unsigned int max_score_;

  /**
   * @brief The possible RadarValues for a song.
   *
   * This is filled in by ScreenGameplay on the start of the notes. */
  RadarValues radar_possible_;
  RadarValues radar_actual_;
  /** @brief How many songs were passed by the Player? */
  int songs_passed_;
  /** @brief How many songs were played by the Player? */
  int songs_played_;
  /**
   * @brief How many seconds were left for the Player?
   *
   * This is used in the Survival mode. */
  float life_remaining_seconds_;

  // workout
  float num_controller_steps_;
  float calories_burned_;

  std::map<float, float> life_record_;
  void SetLifeRecordAt(float life, float steps_second);
  void GetLifeRecord(
      float* life_out, int num_samples, float steps_end_second) const;
  float GetLifeRecordLerpAt(float steps_second) const;
  float GetCurrentLife() const;

  struct Combo_t {
    // Update GetComboList in PlayerStageStats.cpp when adding new members that
    // should be visible from the Lua side.
    /**
     * @brief The start time of the combo.
     *
     * This uses the same scale as the combo list mapping. */
    float start_second_;
    /**
     * @brief The size time of the combo.
     *
     * This uses the same scale as the life record. */
    float size_seconds_;

    /** @brief The size of the Combo, in steps. */
    int cnt_;

    /**
     * @brief The size of the combo that didn't come from this stage.
     *
     * This is generally rolled over from the last song.
     * It is also a subset of m_cnt. */
    int rollover_;

    /**
     * @brief Retrieve the size of the combo that came from this song.
     * @return this song's combo size. */
    int GetStageCnt() const { return cnt_ - rollover_; }

    Combo_t() : start_second_(0), size_seconds_(0), cnt_(0), rollover_(0) {}
    bool IsZero() const { return start_second_ < 0; }
  };
  std::vector<Combo_t> combo_list_;
  float first_second_;
  float last_second_;

  int GetComboAtStartOfStage() const;
  bool FullComboOfScore(TapNoteScore all_greater_or_equal) const;
  bool FullCombo() const { return FullComboOfScore(TNS_W3); }
  TapNoteScore GetBestFullComboTapNoteScore() const;
  bool SingleDigitsOfScore(TapNoteScore all_greater_or_equal) const;
  bool OneOfScore(TapNoteScore all_greater_or_equal) const;
  float GetPercentageOfTaps(TapNoteScore tns) const;
  void UpdateComboList(float second, bool rollover);
  Combo_t GetMaxCombo() const;

  float GetSurvivalSeconds() const {
    return alive_seconds_ + life_remaining_seconds_;
  }

  // Final results:
  void CalcAwards(PlayerNumber p, bool gave_up, bool used_autoplay);
  StageAward stage_award_;
  PeakComboAward peak_combo_award_;

  int personal_high_score_index_;
  int machine_high_score_index_;
  bool disqualified_;
  bool IsDisqualified() const;

  RankingCategory rc_;
  HighScore high_score_;

  // Lua
  void PushSelf(lua_State* L);
};

#endif

/**
 * @file
 * @author Chris Danford, Glenn Maynard (c) 2001-2004
 * @section LICENSE
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
