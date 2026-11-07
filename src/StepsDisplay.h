#ifndef StepsDisplay_H
#define StepsDisplay_H

#include <string>

#include "ActorFrame.h"
#include "AutoActor.h"
#include "BitmapText.h"
#include "Difficulty.h"
#include "GameConstantsAndTypes.h"
#include "PlayerNumber.h"
#include "ThemeMetric.h"

class Steps;
class Trail;
class PlayerState;
/**
 * @brief A graphical representation of a Steps or a Trail.
 *
 * It has a difficulty number, meter, text, and an edit description. */
class StepsDisplay : public ActorFrame {
 public:
  StepsDisplay();

  void Load(const std::string& metrics_group, const PlayerState* player_state);

  virtual StepsDisplay* Copy() const;

  void SetFromGameState(PlayerNumber pn);
  void SetFromStepsTypeAndMeterAndDifficultyAndCourseType(
      StepsType st, int meter, Difficulty dc, CourseType ct);
  void SetFromSteps(const Steps* steps);
  void SetFromTrail(const Trail* trail);
  void Unset();

  // Lua
  void PushSelf(lua_State* L);

 private:
  struct SetParams {
    const Steps* steps_;
    const Trail* trail_;
    int meter_;
    StepsType st_;  // pass because there may be a StepType icon
    Difficulty dc_;
    CourseType ct_;
  };
  void SetInternal(const SetParams& params);

  std::string metrics_group_;

  AutoActor spr_frame_;
  BitmapText text_ticks_;  // 111100000
  BitmapText text_meter_;  // 3, 9
  /**
   * @brief The description of the chart.
   *
   * This is meant to be separate from the author of the chart. */
  BitmapText text_description_;
  /** @brief The author of the chart. */
  BitmapText text_author_;
  AutoActor spr_autogen_;  // visible if Steps and is autogen'd
  AutoActor spr_steps_type_;

  ThemeMetric<int> num_ticks_;
  ThemeMetric<int> max_ticks_;
  ThemeMetric<bool> show_ticks_;
  ThemeMetric<bool> show_meter_;
  ThemeMetric<bool> show_description_;
  ThemeMetric<bool> show_credit_;
  ThemeMetric<bool> show_autogen_;
  ThemeMetric<bool> show_steps_type_;
  ThemeMetric<std::string> zero_meter_string_;
  ThemeMetric<std::string> meter_format_string_;
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
