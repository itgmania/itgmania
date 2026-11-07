#ifndef ScreenHighScores_H
#define ScreenHighScores_H

#include <string>
#include <utility>
#include <vector>

#include "Actor.h"
#include "Attack.h"
#include "Course.h"
#include "Difficulty.h"
#include "DynamicActorScroller.h"
#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "InputQueue.h"
#include "ScreenAttract.h"
#include "ScreenMessage.h"
#include "ThemeMetric.h"

typedef std::pair<Difficulty, StepsType> DifficultyAndStepsType;

enum HighScoresType {
  HighScoresType_AllSteps,        // Top 1 HighScore for N Steps in each Song
  HighScoresType_NonstopCourses,  // Top 1 HighScore for N Trails in each Course
  HighScoresType_OniCourses,
  HighScoresType_SurvivalCourses,
  HighScoresType_AllCourses,
  NUM_HighScoresType,
  HighScoresType_Invalid
};
LuaDeclareType(HighScoresType);

class ScoreScroller : public DynamicActorScroller {
 public:
  ScoreScroller();
  void LoadSongs(int num_recent_scores);
  void LoadCourses(CourseType ct, int num_recent_scores);
  void Load(std::string class_name);
  void SetDisplay(
      const std::vector<DifficultyAndStepsType>& difficulties_to_show);
  bool Scroll(int dir);
  void ScrollTop();

 protected:
  virtual void ConfigureActor(Actor* actor, int item);
  std::vector<DifficultyAndStepsType> difficulties_to_show_;

  struct ScoreRowItemData  // for all_steps and all_courses
  {
    ScoreRowItemData() {
      song_ = nullptr;
      course_ = nullptr;
    }

    Song* song_;
    Course* course_;
  };
  std::vector<ScoreRowItemData> score_row_item_data_;

  ThemeMetric<int> SCROLLER_ITEMS_TO_DRAW;
  ThemeMetric<float> SCROLLER_SECONDS_PER_ITEM;
};

class ScreenHighScores : public ScreenAttract {
 public:
  virtual void Init();
  virtual void BeginScreen();

  void HandleScreenMessage(const ScreenMessage sm);
  virtual bool Input(const InputEventPlus& input);
  virtual bool MenuStart(const InputEventPlus& input);
  virtual bool MenuBack(const InputEventPlus& input);
  virtual bool MenuLeft(const InputEventPlus& input) {
    DoScroll(-1);
    return true;
  }
  virtual bool MenuRight(const InputEventPlus& input) {
    DoScroll(+1);
    return true;
  }
  virtual bool MenuUp(const InputEventPlus& input) {
    DoScroll(-1);
    return true;
  }
  virtual bool MenuDown(const InputEventPlus& input) {
    DoScroll(+1);
    return true;
  }

 private:
  void DoScroll(int dir);

  ThemeMetric<bool> MANUAL_SCROLLING;
  ThemeMetric<HighScoresType> HIGH_SCORES_TYPE;
  ThemeMetric<int> NUM_COLUMNS;
  ThemeMetric1D<Difficulty> COLUMN_DIFFICULTY;
  ThemeMetric1D<StepsType> COLUMN_STEPS_TYPE;
  ThemeMetric<int> MAX_ITEMS_TO_SHOW;
  ScoreScroller scroller_;
};

#endif

/*
 * (c) 2001-2007 Chris Danford, Ben Nordstrom, Glenn Maynard
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
