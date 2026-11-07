#include "TechCounts.h"

#include <algorithm>
#include <string>
#include <vector>

#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "LuaBinding.h"
#include "LuaManager.h"
#include "RageUtil.h"
#include "StepParityDatastructs.h"

// 0.176 ~= 1/8th at 175bpm
// Anything slower isn't counted as a jack
constexpr float JACK_CUTOFF = 0.176f;
// 0.3 = 1/4th at 200bpm (or 3/16th at 150bpm)
// Anything slower isn't counted as a footswitch
constexpr float FOOTSWITCH_CUTOFF = 0.3f;
// 0.235 ~= 1/8th at 128bpm
// Anything slower isn't counted as a doublestep
constexpr float DOUBLESTEP_CUTOFF = 0.235f;

// TechCounts methods
TechCounts::TechCounts() { MakeUnknown(); }

void TechCounts::MakeUnknown() {
  FOREACH_ENUM(TechCountsCategory, rc) { (*this)[rc] = TECHCOUNTS_VAL_UNKNOWN; }
}

void TechCounts::Zero() {
  FOREACH_ENUM(TechCountsCategory, rc) { (*this)[rc] = 0; }
}

void TechCounts::CalculateTechCountsFromRows(
    const std::vector<StepParity::Row>& rows,
    const StepParity::StageLayout* layout, TechCounts& out,
    std::vector<NoteAnnotation>& annotations_out) {
  for (unsigned long i = 0; i < rows.size(); i++) {
    NoteAnnotation annotation;
    const StepParity::Row& current_row = rows[i];
    annotation.beat_ = current_row.beat_;
    annotation.where_the_feet_are_.assign(
        current_row.where_the_feet_are_.begin(),
        current_row.where_the_feet_are_.end());

    // Check for brackets
    if (current_row.note_count_ >= 2) {
      if (current_row.where_the_feet_are_[StepParity::Foot_LeftHeel] !=
              StepParity::INVALID_COLUMN &&
          current_row.where_the_feet_are_[StepParity::Foot_LeftToe] !=
              StepParity::INVALID_COLUMN) {
        out[TechCountsCategory_Brackets] += 1;
        annotation.tech_.push_back(TechCountsCategory_Brackets);
      }

      if (current_row.where_the_feet_are_[StepParity::Foot_RightHeel] !=
              StepParity::INVALID_COLUMN &&
          current_row.where_the_feet_are_[StepParity::Foot_RightToe] !=
              StepParity::INVALID_COLUMN) {
        out[TechCountsCategory_Brackets] += 1;
        annotation.tech_.push_back(TechCountsCategory_Brackets);
      }
    }

    // If this is the first row, none of the other tech counts are applicable
    if (i == 0) {
      annotations_out.push_back(annotation);
      continue;
    }

    const StepParity::Row& previous_row = rows[i - 1];

    float elapsed_time = current_row.second_ - previous_row.second_;

    // Jacks are same arrow same foot
    // Doublestep is same foot on successive arrows
    // Brackets are jumps with one foot

    // Footswitch is different foot on the up or down arrow
    // Sideswitch is footswitch on left or right arrow
    // Crossovers are left foot on right arrow or vice versa

    // Check for jacks and doublesteps
    if (current_row.note_count_ == 1 && previous_row.note_count_ == 1) {
      for (StepParity::Foot foot : StepParity::FEET) {
        if (current_row.where_the_feet_are_[foot] ==
                StepParity::INVALID_COLUMN ||
            previous_row.where_the_feet_are_[foot] ==
                StepParity::INVALID_COLUMN) {
          continue;
        }

        if (previous_row.where_the_feet_are_[foot] ==
            current_row.where_the_feet_are_[foot]) {
          if (elapsed_time < JACK_CUTOFF) {
            out[TechCountsCategory_Jacks] += 1;
            annotation.tech_.push_back(TechCountsCategory_Jacks);
          }
        } else {
          if (elapsed_time < DOUBLESTEP_CUTOFF) {
            out[TechCountsCategory_Doublesteps] += 1;
            annotation.tech_.push_back(TechCountsCategory_Doublesteps);
          }
        }
      }
    }

    // Check for up footswitches
    for (int c : layout->up_arrows_) {
      if (isFootswitch(c, current_row, previous_row, elapsed_time)) {
        out[TechCountsCategory_UpFootswitches] += 1;
        out[TechCountsCategory_Footswitches] += 1;
        annotation.tech_.push_back(TechCountsCategory_UpFootswitches);
        annotation.tech_.push_back(TechCountsCategory_Footswitches);
      }
    }
    // Check for down footswitches
    for (int c : layout->down_arrows_) {
      if (isFootswitch(c, current_row, previous_row, elapsed_time)) {
        out[TechCountsCategory_DownFootswitches] += 1;
        out[TechCountsCategory_Footswitches] += 1;
        annotation.tech_.push_back(TechCountsCategory_DownFootswitches);
        annotation.tech_.push_back(TechCountsCategory_Footswitches);
      }
    }

    // Check for sideswitches
    for (int c : layout->side_arrows_) {
      if (isFootswitch(c, current_row, previous_row, elapsed_time)) {
        out[TechCountsCategory_Sideswitches] += 1;
        annotation.tech_.push_back(TechCountsCategory_Sideswitches);
      }
    }

    // Check for crossovers
    int left_heel = current_row.where_the_feet_are_[StepParity::Foot_LeftHeel];
    int left_toe = current_row.where_the_feet_are_[StepParity::Foot_LeftToe];
    int right_heel =
        current_row.where_the_feet_are_[StepParity::Foot_RightHeel];
    int right_toe = current_row.where_the_feet_are_[StepParity::Foot_RightToe];

    int previous_left_heel =
        previous_row.where_the_feet_are_[StepParity::Foot_LeftHeel];
    int previous_left_toe =
        previous_row.where_the_feet_are_[StepParity::Foot_LeftToe];
    int previous_right_heel =
        previous_row.where_the_feet_are_[StepParity::Foot_RightHeel];
    int previous_right_toe =
        previous_row.where_the_feet_are_[StepParity::Foot_RightToe];

    // Check for the following:
    // - We moved the right foot on this row,
    // - we moved the left foot on the previous row,
    // - we didn't move the right foot on the previous row
    // - Is the position of the right foot farther left than the left foot
    // - If so, check the row before the previous row for the following:
    //   - Was the right foot on a difference position
    //   - Was the right foot farther right than the left?
    //     - If so, then this was a full crossover (like RDL, starting on right
    //     foot)
    //     - otherwise, then this was probably a half crossover (like UDL,
    //     starting on right foot)
    if (right_heel != StepParity::INVALID_COLUMN &&
        previous_left_heel != StepParity::INVALID_COLUMN &&
        previous_right_heel == StepParity::INVALID_COLUMN) {
      StepParity::StagePoint left_pos =
          layout->averagePoint(previous_left_heel, previous_left_toe);
      StepParity::StagePoint right_pos =
          layout->averagePoint(right_heel, right_toe);

      if (right_pos.x_ < left_pos.x_) {
        if (i > 1) {
          const StepParity::Row& previous_previous_row = rows[i - 2];
          int previous_previous_right_heel =
              previous_previous_row
                  .where_the_feet_are_[StepParity::Foot_RightHeel];

          if (previous_previous_right_heel != StepParity::INVALID_COLUMN &&
              previous_previous_right_heel != right_heel) {
            StepParity::StagePoint previous_previous_right_pos =
                layout->columns_[previous_previous_right_heel];
            if (previous_previous_right_pos.x_ > left_pos.x_) {
              out[TechCountsCategory_FullCrossovers] += 1;
              annotation.tech_.push_back(TechCountsCategory_FullCrossovers);
            } else {
              out[TechCountsCategory_HalfCrossovers] += 1;
              annotation.tech_.push_back(TechCountsCategory_HalfCrossovers);
            }
            out[TechCountsCategory_Crossovers] += 1;
            annotation.tech_.push_back(TechCountsCategory_Crossovers);
          }
        } else {
          out[TechCountsCategory_HalfCrossovers] += 1;
          out[TechCountsCategory_Crossovers] += 1;
          annotation.tech_.push_back(TechCountsCategory_HalfCrossovers);
          annotation.tech_.push_back(TechCountsCategory_Crossovers);
        }
      }
    }
    // And check the same thing, starting with left foot
    else if (
        left_heel != StepParity::INVALID_COLUMN &&
        previous_right_heel != StepParity::INVALID_COLUMN &&
        previous_left_heel == StepParity::INVALID_COLUMN) {
      StepParity::StagePoint left_pos =
          layout->averagePoint(left_heel, left_toe);
      StepParity::StagePoint right_pos =
          layout->averagePoint(previous_right_heel, previous_right_toe);

      if (right_pos.x_ < left_pos.x_) {
        if (i > 1) {
          const StepParity::Row& previous_previous_row = rows[i - 2];
          int previous_previous_left_heel =
              previous_previous_row
                  .where_the_feet_are_[StepParity::Foot_LeftHeel];
          if (previous_previous_left_heel != StepParity::INVALID_COLUMN &&
              previous_previous_left_heel != left_heel) {
            StepParity::StagePoint previous_previous_left_pos =
                layout->columns_[previous_previous_left_heel];
            if (right_pos.x_ > previous_previous_left_pos.x_) {
              out[TechCountsCategory_FullCrossovers] += 1;
              annotation.tech_.push_back(TechCountsCategory_FullCrossovers);
            } else {
              out[TechCountsCategory_HalfCrossovers] += 1;
              annotation.tech_.push_back(TechCountsCategory_HalfCrossovers);
            }
            out[TechCountsCategory_Crossovers] += 1;
            annotation.tech_.push_back(TechCountsCategory_Crossovers);
          }
        } else {
          out[TechCountsCategory_HalfCrossovers] += 1;
          out[TechCountsCategory_Crossovers] += 1;
          annotation.tech_.push_back(TechCountsCategory_HalfCrossovers);
          annotation.tech_.push_back(TechCountsCategory_Crossovers);
        }
      }
    }
    annotations_out.push_back(annotation);
  }
}

bool TechCounts::isFootswitch(
    int c, const StepParity::Row& current_row,
    const StepParity::Row& previous_row, float elapsed_time) {
  if (current_row.columns_[c] == StepParity::Foot_None ||
      previous_row.columns_[c] == StepParity::Foot_None) {
    return false;
  }

  if (previous_row.columns_[c] != current_row.columns_[c] &&
      StepParity::OTHER_PART_OF_FOOT[previous_row.columns_[c]] !=
          current_row.columns_[c] &&
      elapsed_time < FOOTSWITCH_CUTOFF) {
    return true;
  }
  return false;
}

// lua start
class LunaTechCounts : public Luna<TechCounts> {
 public:
  static int GetValue(T* p, lua_State* L) {
    lua_pushnumber(L, (*p)[Enum::Check<TechCountsCategory>(L, 1)]);
    return 1;
  }

  LunaTechCounts() { ADD_METHOD(GetValue); }
};

LUA_REGISTER_CLASS(TechCounts)

/*
 * (c) 2023 Michael Votaw
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
