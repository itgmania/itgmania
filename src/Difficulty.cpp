#include "global.h"

#include "Difficulty.h"

#include <vector>

#include "GameConstantsAndTypes.h"
#include "GameManager.h"
#include "GameState.h"
#include "LocalizedString.h"
#include "LuaManager.h"
#include "Steps.h"
#include "ThemeMetric.h"
#include "Trail.h"

static const char* DifficultyNames[] = {
    "Beginner", "Easy", "Medium", "Hard", "Challenge", "Edit",
};
XToString(Difficulty);
StringToX(Difficulty);
LuaXType(Difficulty);

const RString& CourseDifficultyToLocalizedString(
    CourseDifficulty course_difficulty) {
  static std::unique_ptr<LocalizedString>
      g_CourseDifficultyName[NUM_Difficulty];
  if (g_CourseDifficultyName[0].get() == nullptr) {
    FOREACH_ENUM(Difficulty, i) {
      std::unique_ptr<LocalizedString> ap(
          new LocalizedString("CourseDifficulty", DifficultyToString(i)));
      g_CourseDifficultyName[i] = move(ap);
    }
  }
  return g_CourseDifficultyName[course_difficulty]->GetValue();
}

LuaFunction(
    CourseDifficultyToLocalizedString,
    CourseDifficultyToLocalizedString(Enum::Check<Difficulty>(L, 1)));

CourseDifficulty GetNextShownCourseDifficulty(
    CourseDifficulty course_difficulty) {
  for (CourseDifficulty d = (CourseDifficulty)(course_difficulty + 1);
       d < NUM_Difficulty; enum_add(d, 1)) {
    if (GAMESTATE->IsCourseDifficultyShown(d)) {
      return d;
    }
  }
  return Difficulty_Invalid;
}

struct OldStyleStringToDifficultyMapHolder {
  std::map<RString, Difficulty> conversion_map;
  OldStyleStringToDifficultyMapHolder() {
    conversion_map["beginner"] = Difficulty_Beginner;
    conversion_map["easy"] = Difficulty_Easy;
    conversion_map["basic"] = Difficulty_Easy;
    conversion_map["light"] = Difficulty_Easy;
    conversion_map["medium"] = Difficulty_Medium;
    conversion_map["another"] = Difficulty_Medium;
    conversion_map["trick"] = Difficulty_Medium;
    conversion_map["standard"] = Difficulty_Medium;
    conversion_map["difficult"] = Difficulty_Medium;
    conversion_map["hard"] = Difficulty_Hard;
    conversion_map["ssr"] = Difficulty_Hard;
    conversion_map["maniac"] = Difficulty_Hard;
    conversion_map["heavy"] = Difficulty_Hard;
    conversion_map["smaniac"] = Difficulty_Challenge;
    conversion_map["challenge"] = Difficulty_Challenge;
    conversion_map["expert"] = Difficulty_Challenge;
    conversion_map["oni"] = Difficulty_Challenge;
    conversion_map["edit"] = Difficulty_Edit;
  }
};
OldStyleStringToDifficultyMapHolder OldStyleStringToDifficulty_converter;
Difficulty OldStyleStringToDifficulty(const RString& difficulty_str) {
  RString s2 = difficulty_str;
  s2.MakeLower();
  auto diff = OldStyleStringToDifficulty_converter.conversion_map.find(s2);
  if (diff != OldStyleStringToDifficulty_converter.conversion_map.end()) {
    return diff->second;
  }
  return Difficulty_Invalid;
}

LuaFunction(OldStyleStringToDifficulty, OldStyleStringToDifficulty(SArg(1)));

static ThemeMetric<RString> NAMES("CustomDifficulty", "Names");

RString GetCustomDifficulty(
    StepsType steps_type, Difficulty difficulty, CourseType course_type) {
  // NOTE(Steve): GAMEMAN->GetStepsTypeInfo( StepsType_Invalid ) will crash.
  // I'm not sure what the correct behavior in this case should be. Should we
  // still allow custom difficulties? Why do we not allow custom difficulties for
  // Edit?
  // NOTE(aj): CustomDifficulty for Edit defeats the purpose of the edit's name.
  if (steps_type == StepsType_Invalid) {
    // This is not totally necessary since DifficultyToString() will
    // return "", but the comment there says that the caller should
    // really be checking for invalid values.
    if (difficulty == Difficulty_Invalid) {
      return RString();
    }
    return DifficultyToString(difficulty);
  }

  if (difficulty == Difficulty_Edit) {
    return "Edit";
  }
  // OPTIMIZATION OPPORTUNITY: cache these metrics and cache the splitting
  std::vector<RString> names;
  split(NAMES, ",", names);
  for (const RString& name : names) {
    ThemeMetric<StepsType> STEPS_TYPE("CustomDifficulty", name + "StepsType");
    if (STEPS_TYPE == StepsType_Invalid || steps_type == STEPS_TYPE) {
      ThemeMetric<Difficulty> DIFFICULTY(
          "CustomDifficulty", name + "Difficulty");
      if (DIFFICULTY == Difficulty_Invalid || difficulty == DIFFICULTY) {
        ThemeMetric<CourseType> COURSE_TYPE(
            "CustomDifficulty", name + "CourseType");
        if (COURSE_TYPE == CourseType_Invalid || course_type == COURSE_TYPE) {
          ThemeMetric<RString> STRING("CustomDifficulty", name + "String");
          return STRING.GetValue();
        }
      }
    }
  }
  // no matching CustomDifficulty, so use a regular difficulty name
  if (difficulty == Difficulty_Invalid) {
    return RString();
  }
  return DifficultyToString(difficulty);
}

LuaFunction(
    GetCustomDifficulty,
    GetCustomDifficulty(
        Enum::Check<StepsType>(L, 1), Enum::Check<Difficulty>(L, 2),
        Enum::Check<CourseType>(L, 3, true)));

RString CustomDifficultyToLocalizedString(const RString& custom_difficulty) {
  return THEME->GetString("CustomDifficulty", custom_difficulty);
}

LuaFunction(
    CustomDifficultyToLocalizedString,
    CustomDifficultyToLocalizedString(SArg(1)));

RString StepsToCustomDifficulty(const Steps* steps) {
  return GetCustomDifficulty(
      steps->m_StepsType, steps->GetDifficulty(), CourseType_Invalid);
}

RString TrailToCustomDifficulty(const Trail* trail) {
  return GetCustomDifficulty(
      trail->m_StepsType, trail->m_CourseDifficulty, trail->m_CourseType);
}

#include "LuaBinding.h"

LuaFunction(
    StepsToCustomDifficulty, StepsToCustomDifficulty(Luna<Steps>::check(L, 1)));
LuaFunction(
    TrailToCustomDifficulty, TrailToCustomDifficulty(Luna<Trail>::check(L, 1)));

/*
 * (c) 2001-2004 Chris Danford
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
