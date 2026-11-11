#ifndef ScreenRanking_H
#define ScreenRanking_H

#include <string>
#include <utility>
#include <vector>

#include "AutoActor.h"
#include "Banner.h"
#include "BitmapText.h"
#include "CommonMetrics.h"
#include "Difficulty.h"
#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "LocalizedString.h"
#include "RageTypes.h"
#include "ScreenAttract.h"
#include "ScreenMessage.h"
#include "ThemeMetric.h"

class Course;
class Trail;
typedef std::pair<Difficulty, StepsType> DifficultyAndStepsType;

const int kNumRankingLines = 5;

enum RankingType {
  RankingType_Category,       // Top N HighScores for one Category
  RankingType_SpecificTrail,  // Top N HighScores for one Course and Trail
  NUM_RankingType,
  RankingType_Invalid
};
LuaDeclareType(RankingType);

class ScreenRanking : public ScreenAttract {
 public:
  virtual void Init();
  virtual void BeginScreen();

  void HandleScreenMessage(const ScreenMessage sm);

 protected:
  struct PageToShow {
    PageToShow() {
      course_ = nullptr;
      trail_ = nullptr;
    }

    int color_index_;
    std::vector<DifficultyAndStepsType> types_;

    // RankingPageType_Category
    RankingCategory category_;

    // RankingPageType_SpecificCourses
    Course* course_;
    Trail* trail_;
  };

  virtual float SetPage(const PageToShow& pts);

  BitmapText text_steps_type_;  // for category, course, all_steps

  std::vector<PageToShow> pages_to_show_;
  unsigned next_page_to_show_;

  // Don't use the version in CommonMetrics because we may have multiple
  // ranking screens that want to show different types and difficulties.
  ThemeMetricStepsTypesToShow STEPS_TYPES_TO_SHOW;
  ThemeMetric<float> PAGE_FADE_SECONDS;

  ThemeMetric<RankingType> RANKING_TYPE;
  ThemeMetric<std::string> COURSES_TO_SHOW;
  ThemeMetric<float> SECONDS_PER_PAGE;

  Banner banner_;                 // for course
  BitmapText text_category_;      // for category
  BitmapText text_course_title_;  // for course

  AutoActor spr_bullets_[kNumRankingLines];   // for category and course
  BitmapText text_names_[kNumRankingLines];   // for category and course
  BitmapText text_scores_[kNumRankingLines];  // for category and course
  BitmapText text_points_[kNumRankingLines];  // for course
  BitmapText text_time_[kNumRankingLines];    // for course
  ThemeMetric1D<RageColor> STEPS_TYPE_COLOR;

  LocalizedString NO_SCORE_NAME;
  ThemeMetric<float> ROW_SPACING_X;
  ThemeMetric<float> ROW_SPACING_Y;
  ThemeMetric<float> BULLET_START_X;
  ThemeMetric<float> BULLET_START_Y;
  ThemeMetric<float> NAME_START_X;
  ThemeMetric<float> NAME_START_Y;
  ThemeMetric<float> SCORE_START_X;
  ThemeMetric<float> SCORE_START_Y;
  ThemeMetric<float> POINTS_START_X;
  ThemeMetric<float> POINTS_START_Y;
  ThemeMetric<float> TIME_START_X;
  ThemeMetric<float> TIME_START_Y;
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
