#ifndef TRAIL_H
#define TRAIL_H

#include <string>
#include <vector>

#include "Attack.h"
#include "Difficulty.h"
#include "GameConstantsAndTypes.h"
#include "RadarValues.h"

class Song;
class Steps;
struct lua_State;

/** @brief One such Song and
 * <a class="el" href="class_steps.html">Step</a> in the entire Trail. */
struct TrailEntry {
  TrailEntry()
      : song_(nullptr),
        steps_(nullptr),
        modifiers_(""),
        attacks_(),
        secret_(false),
        low_meter_(-1),
        high_meter_(-1),
        dc_(Difficulty_Invalid) {}
  void GetAttackArray(AttackArray& out) const;

  /** @brief The Song involved in the entry. */
  Song* song_;
  /** @brief The <a class="el" href="class_steps.html">Step</a> involved in the
   * entry. */
  Steps* steps_;
  /** @brief The Modifiers applied for the whole Song. */
  std::string modifiers_;
  /** @brief The Attacks that will take place durring the Song. */
  AttackArray attacks_;
  /**
   * @brief Is this Song and its Step meant to be a secret?
   * If so, it will show text such as "???" to indicate that it's a mystery. */
  bool secret_;

  /* These represent the meter and difficulty used by the course to pick the
   * steps; if you want the real difficulty and meter, look at pSteps. */
  int low_meter_;
  int high_meter_;
  Difficulty dc_;
  bool ContainsTransformOrTurn() const;

  // Lua
  void PushSelf(lua_State* L);
};

/** @brief A queue of Songs and Steps that are generated from a Course. */
class Trail {
 public:
  StepsType steps_type_;
  CourseType course_type_;
  CourseDifficulty course_difficulty_;
  std::vector<TrailEntry> entries_;
  int specified_meter_;  // == -1 if no meter specified
  mutable bool radar_values_cached_;
  mutable RadarValues cached_radar_values_;

  /**
   * @brief Set up the Trail with default values.
   *
   * This used to call Init(), which is still available. */
  Trail()
      : steps_type_(StepsType_Invalid),
        course_type_(CourseType_Invalid),
        course_difficulty_(Difficulty_Invalid),
        entries_(),
        specified_meter_(-1),
        radar_values_cached_(false),
        cached_radar_values_() {}
  void Init() {
    steps_type_ = StepsType_Invalid;
    course_difficulty_ = Difficulty_Invalid;
    specified_meter_ = -1;
    entries_.clear();
    radar_values_cached_ = false;
  }

  const RadarValues& GetRadarValues() const;
  void SetRadarValues(const RadarValues& rv);  // for pre-populating cache
  int GetMeter() const;
  int GetTotalMeter() const;
  float GetLengthSeconds() const;
  void GetDisplayBpms(DisplayBpms& add_to) const;
  bool IsSecret() const;
  bool ContainsSong(const Song* song) const;

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
