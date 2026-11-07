#ifndef SCREEN_EVALUATION_H
#define SCREEN_EVALUATION_H

#include <string>

#include "AutoActor.h"
#include "Banner.h"
#include "BitmapText.h"
#include "GameConstantsAndTypes.h"
#include "GradeDisplay.h"
#include "InputQueue.h"
#include "PercentageDisplay.h"
#include "PlayerNumber.h"
#include "RageSound.h"
#include "RollingNumbers.h"
#include "ScreenMessage.h"
#include "ScreenWithMenuElements.h"
#include "Sprite.h"
#include "StageStats.h"
#include "ThemeMetric.h"

/**
 * @brief How many songs are shown at the end?
 *
 * In the traditional summary, only the last three stages are shown.
 * If any extra stages are passed, those get shown as well. */
const int MAX_SONGS_TO_SHOW = 5;
/** @brief The different judgment lines shown. */
enum JudgmentLine {
  JudgmentLine_W1,   /**< The line showing absolutely perfect hits. */
  JudgmentLine_W2,   /**< The line showing just-a-smidge-off perfect hits. */
  JudgmentLine_W3,   /**< The line showing almost perfect hits. */
  JudgmentLine_W4,   /**< The line showing hits that were not that good. */
  JudgmentLine_W5,   /**< The line showing hits that were almost missed. */
  JudgmentLine_Miss, /**< The line showing missed notes. */
  JudgmentLine_Held, /**< The line showing held down notes. */
  JudgmentLine_MaxCombo, /**< The line showing the player's max combo. */
  NUM_JudgmentLine,      /**< The number of judgment lines available. */
  JudgmentLine_Invalid
};
/** @brief The number of details based on the radar categories. */
enum DetailLine {
  DetailLine_NumSteps, /**< The number of steps hit. */
  DetailLine_Jumps,    /**< The number of jumps hit together. */
  DetailLine_Holds,    /**< The number of holds held. */
  DetailLine_Mines,    /**< The number of mines avoided. */
  DetailLine_Hands,    /**< The number of hands hit (somehow) */
  DetailLine_Rolls,    /**< The number of rolls hit repeatedly. */
  DetailLine_Lifts,    /**< The number of lifts lifted up. */
  DetailLine_Fakes,    /**< The number of fakes to be ignored. */
  NUM_DetailLine       /**< The nuber of detailed lines. */
};
/** @brief Shows the player their score after gameplay has ended. */
class ScreenEvaluation : public ScreenWithMenuElements {
 public:
  ScreenEvaluation();
  virtual ~ScreenEvaluation();
  virtual void Init();
  virtual bool Input(const InputEventPlus& input);
  virtual void HandleScreenMessage(const ScreenMessage sm);

  virtual bool MenuBack(const InputEventPlus& input);
  virtual bool MenuStart(const InputEventPlus& input);
  virtual bool MenuRestart(const InputEventPlus& input);
  virtual void PushSelf(lua_State* L);
  StageStats* GetStageStats() { return stage_stats_; }

 protected:
  void HandleMenuStart();

  StageStats* stage_stats_;
  StageStats final_eval_stage_stats_;

  // banner area
  Banner large_banner_;
  AutoActor spr_large_banner_frame_;
  BitmapText text_player_options_[NUM_PLAYERS];
  BitmapText text_song_options_;
  AutoActor spr_disqualified_[NUM_PLAYERS];
  Banner small_banner_[MAX_SONGS_TO_SHOW];
  AutoActor spr_small_banner_frame_[MAX_SONGS_TO_SHOW];

  // grade area
  AutoActor spr_grade_frame_[NUM_PLAYERS];
  GradeDisplay grades_[NUM_PLAYERS];

  // points area
  PercentageDisplay percent_[NUM_PLAYERS];
  AutoActor spr_percent_frame_[NUM_PLAYERS];

  // bonus area
  AutoActor spr_bonus_frame_[NUM_PLAYERS];
  Sprite spr_possible_bar_[NUM_PLAYERS][NUM_RadarCategory];
  Sprite spr_actual_bar_[NUM_PLAYERS][NUM_RadarCategory];

  // survived area
  AutoActor spr_survived_frame_[NUM_PLAYERS];
  BitmapText text_survived_number_[NUM_PLAYERS];

  // win area
  AutoActor spr_win_frame_[NUM_PLAYERS];
  Sprite spr_win_[NUM_PLAYERS];

  // judgment area
  AutoActor spr_shared_judgment_line_labels_[NUM_JudgmentLine];
  RollingNumbers text_judgment_line_number_[NUM_JudgmentLine][NUM_PLAYERS];

  // stats area
  AutoActor spr_detail_frame_[NUM_PLAYERS];
  BitmapText text_detail_text_[NUM_DetailLine][NUM_PLAYERS];

  // score area
  AutoActor spr_score_label_;
  RollingNumbers text_score_[NUM_PLAYERS];

  // time area
  AutoActor spr_time_label_;
  BitmapText text_time_[NUM_PLAYERS];

  RageSound sound_start_;  // sound played if the player passes or fails

  ThemeMetric<bool> SUMMARY;
  ThemeMetric<std::string> ROLLING_NUMBERS_CLASS;
  ThemeMetric<std::string> ROLLING_NUMBERS_MAX_COMBO_CLASS;
  /** @brief Did a player save a screenshot of their score? */
  bool saved_screenshot_[NUM_PLAYERS];
};

#endif

/**
 * @file
 * @author Chris Danford (c) 2001-2004
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
