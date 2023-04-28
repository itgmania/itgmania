#ifndef COMMON_METRICS_H
#define COMMON_METRICS_H

#include <vector>

#include "Difficulty.h"
#include "GameConstantsAndTypes.h"
#include "LocalizedString.h"
#include "PlayerNumber.h"
#include "ThemeMetric.h"

// Types
class ThemeMetricDifficultiesToShow : public ThemeMetric<RString> {
 public:
  ThemeMetricDifficultiesToShow() : difficulties_() {}
  ThemeMetricDifficultiesToShow(const RString& group, const RString& name);
  void Read();
  const std::vector<Difficulty>& GetValue() const;

 private:
  std::vector<Difficulty> difficulties_;
};

class ThemeMetricCourseDifficultiesToShow : public ThemeMetric<RString> {
 public:
  ThemeMetricCourseDifficultiesToShow() : course_difficulties_() {}
  ThemeMetricCourseDifficultiesToShow(
      const RString& group, const RString& name);
  void Read();
  const std::vector<CourseDifficulty>& GetValue() const;

 private:
  std::vector<CourseDifficulty> course_difficulties_;
};

class ThemeMetricStepsTypesToShow : public ThemeMetric<RString> {
 public:
  ThemeMetricStepsTypesToShow() : steps_types_() {}
  ThemeMetricStepsTypesToShow(const RString& group, const RString& name);
  void Read();
  const std::vector<StepsType>& GetValue() const;

 private:
  std::vector<StepsType> steps_types_;
};

// Definitions of metrics that are in the "Common" group.
//
// These metrics are used throughout the metrics file.
namespace CommonMetrics {

// The first screen in the attract loop.
extern ThemeMetric<RString> FIRST_ATTRACT_SCREEN;
// The screen that appears when pressing the operator button.
extern ThemeMetric<RString> OPERATOR_MENU_SCREEN;
// The default modifiers to apply.
extern ThemeMetric<RString> DEFAULT_MODIFIERS;
// The caption on the title bar.
extern LocalizedString WINDOW_TITLE;
// How many entries should be shown before showing "Various" instead.
extern ThemeMetric<int> MAX_COURSE_ENTRIES_BEFORE_VARIOUS;
// Adjusts the assist tick sound's playback time.
extern ThemeMetric<float> TICK_EARLY_SECONDS;
// the name of the default noteskin.
extern ThemeMetric<RString> DEFAULT_NOTESKIN_NAME;
// Which difficulties are to be shown?
extern ThemeMetricDifficultiesToShow DIFFICULTIES_TO_SHOW;
// Which course difficulties are to be shown?
extern ThemeMetricCourseDifficultiesToShow COURSE_DIFFICULTIES_TO_SHOW;
// Which step types are to be shown?
//
// This metric (StepsTypesToHide) takes a list of StepsTypes to hide and
// returns a list of StepsTypes to show.
extern ThemeMetricStepsTypesToShow STEPS_TYPES_TO_SHOW;
// Does the player need to explicitly set a style?
extern ThemeMetric<bool> AUTO_SET_STYLE;
// How many decimal places are used?
extern ThemeMetric<int> PERCENT_SCORE_DECIMAL_PLACES;

extern ThemeMetric<RString> IMAGES_TO_CACHE;

RString LocalizeOptionItem(const RString& option, bool is_optional);

};  // namespace CommonMetrics

#endif  // COMMON_METRICS_H

/**
 * @file
 * @author Chris Danford (c) 2003-2004
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
