#ifndef GRADE_H
#define GRADE_H

#include "EnumHelper.h"
#include "RageUtil.h"
#include "ThemeMetric.h"

// The list of grading tiers available.
// TODO(Wolfman2000): Look into a more flexible system without a fixed number of
// grades.
enum Grade {
  Grade_Tier01,  // Usually an AAAA
  Grade_Tier02,  // Usually an AAA
  Grade_Tier03,  // Usually an AA
  Grade_Tier04,  // Usually an A
  Grade_Tier05,  // Usually a B
  Grade_Tier06,  // Usually a C
  Grade_Tier07,  // Usually a D
  Grade_Tier08,
  Grade_Tier09,
  Grade_Tier10,
  Grade_Tier11,
  Grade_Tier12,
  Grade_Tier13,
  Grade_Tier14,
  Grade_Tier15,
  Grade_Tier16,
  Grade_Tier17,
  Grade_Tier18,
  Grade_Tier19,
  Grade_Tier20,
  Grade_Failed,  // Usually an E
  NUM_Grade,
  Grade_Invalid,
};

// Have an alternative for if there is no data for grading.
#define Grade_NoData Grade_Invalid

// Convert the grade supplied to a string representation.
//
// This is in the header so the test sets don't require Grade.cpp (through
// PrefsManager), since that pulls in ThemeManager.
static inline RString GradeToString(Grade grade) {
  ASSERT_M(
      (grade >= 0 && grade < NUM_Grade) || grade == Grade_NoData,
      ssprintf("grade = %d", grade));

  switch (grade) {
    case Grade_NoData:
      return "NoData";
    case Grade_Failed:
      return "Failed";
    default:
      return ssprintf("Tier%02d", grade + 1);
  }
}

// Convert to the old version styled grade strings.
//
// This is mainly for backward compatibility purposes, but the announcer
// also uses it. Think "AAA", "B", etc.
// This is only referenced in ScreenEvaluation at the moment.
RString GradeToOldString(Grade grade);
Grade GradeToOldGrade(Grade grade);
RString GradeToLocalizedString(Grade grade);
// Convert the given RString into a proper Grade.
Grade StringToGrade(const RString& grade_str);
LuaDeclareType(Grade);
extern ThemeMetric<int> NUM_GRADE_TIERS_USED;
#define NUM_POSSIBLE_GRADES (NUM_GRADE_TIERS_USED + 1)
// Step through the enumerator one at a time to get the next Grade.
Grade GetNextPossibleGrade(Grade grade);
// Loop through each possible Grade.
#define FOREACH_PossibleGrade(grade)                     \
  for (Grade grade = (Grade)(0); grade != Grade_Invalid; \
       g = GetNextPossibleGrade(grade))

#endif  // GRADE_H

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
