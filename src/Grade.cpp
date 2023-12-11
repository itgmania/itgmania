#include "global.h"

#include "Grade.h"

#include "EnumHelper.h"
#include "LuaManager.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "ThemeManager.h"

LuaXType(Grade);

// The current number of grade tiers being used.
ThemeMetric<int> NUM_GRADE_TIERS_USED("PlayerStageStats", "NumGradeTiersUsed");

Grade GetNextPossibleGrade(Grade grade) {
  if (grade < NUM_GRADE_TIERS_USED - 1) {
    return (Grade)(grade + 1);
  } else if (grade == NUM_GRADE_TIERS_USED - 1) {
    return Grade_Failed;
  } else {
    return Grade_Invalid;
  }
}

RString GradeToLocalizedString(Grade grade) {
  RString grade_str = GradeToString(grade);
  if (!THEME->HasString("Grade", grade_str)) {
    return "???";
  }
  return THEME->GetString("Grade", grade_str);
}

RString GradeToOldString(Grade grade) {
  // string is meant to be human readable
  switch (GradeToOldGrade(grade)) {
    case Grade_Tier01:
      return "AAAA";
    case Grade_Tier02:
      return "AAA";
    case Grade_Tier03:
      return "AA";
    case Grade_Tier04:
      return "A";
    case Grade_Tier05:
      return "B";
    case Grade_Tier06:
      return "C";
    case Grade_Tier07:
      return "D";
    case Grade_Failed:
      return "E";
    case Grade_NoData:
      return "N";
    default:
      return "N";
  }
};

Grade GradeToOldGrade(Grade grade) {
  // There used to be 7 grades (plus fail) but grades can now be defined by
  // themes. So we need to re-scale the grade bands based on how many actual
  // grades the theme defines.
  if (grade < NUM_GRADE_TIERS_USED) {
    grade = (Grade)std::lround(
        (double)grade * Enum::to_integral(Grade_Tier07) /
        (NUM_GRADE_TIERS_USED - 1));
  }

  return grade;
}

Grade StringToGrade(const RString& grade_str) {
  RString s = grade_str;
  s.MakeUpper();

  // new style
  int tier;
  if (sscanf(grade_str.c_str(), "Tier%02d", &tier) == 1 && tier >= 0 &&
      tier < NUM_Grade) {
    return (Grade)(tier - 1);
  } else if (s == "FAILED") {
    return Grade_Failed;
  } else if (s == "NODATA") {
    return Grade_NoData;
  }

  LOG->Warn("Invalid grade: %s", grade_str.c_str());
  return Grade_NoData;
};

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
