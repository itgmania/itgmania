#include "StepParityDatastructs.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

#include "EnumHelper.h"
#include "LocalizedString.h"
#include "LuaBinding.h"
#include "NoteTypes.h"

using namespace StepParity;

static const char* FootNames[] = {
    "None", "LeftHeel", "LeftToe", "RightHeel", "RightToe"};

XToString(Foot);
XToLocalizedString(Foot);
LuaFunction(
    FootToLocalizedString, FootToLocalizedString(Enum::Check<Foot>(L, 1)));
LuaXType(Foot);

float facing_penalty(float v) {
  float base = -1 * std::min(v, 0.0f);
  float result = pow(base, 1.8) * 100;
  return result;
}

void StageLayout::preCalculateStuff() {
  for (int left = 0; left < column_count_; left++) {
    for (int right = 0; right < column_count_; right++) {
      int idx = left * column_count_ + right;

      StagePoint avg_point = {
          (columns_[left].x_ + columns_[right].x_) / 2.0f,
          (columns_[left].y_ + columns_[right].y_) / 2.0f,
      };
      avg_points_[idx] = avg_point;

      float dx = columns_[left].x_ - columns_[right].x_;
      float dy = columns_[left].y_ - columns_[right].y_;
      float dist_sq = (dx * dx) + (dy * dy);
      float dist = sqrt(dist_sq);
      distances_[idx] = dist;

      if (dist == 0) {
        facing_x_penalties_[idx] = 0;
        facing_y_penalties_[idx] = 0;
      } else {
        float normalized_dx = dx / dist;
        float normalized_dy = dy / dist;
        float xm = getXDifference(left, right);
        float ym = getYDifference(left, right);

        facing_x_penalties_[idx] = std::max(0.0f, facing_penalty(xm));
        facing_y_penalties_[idx] = std::max(0.0f, facing_penalty(ym));
      }
    }
  }
}

// Counts the number of ones in the binary representation
// of the given `x`.
// TODO: use std::popcount if we upgrade to C++20.
inline int popcount(uint64_t x) {
#if defined(__clang__) || defined(__GNUC__)
  return __builtin_popcountll(x);
#else
  // popcount64b from https://en.wikipedia.org/wiki/Hamming_weight
  constexpr uint64_t m1 = 0x5555555555555555;  // binary: 0101...
  constexpr uint64_t m2 = 0x3333333333333333;  // binary: 00110011..
  constexpr uint64_t m4 = 0x0f0f0f0f0f0f0f0f;  // binary:  4 zeros,  4 ones ...
  x -= (x >> 1) & m1;              // put count of each 2 bits into those 2 bits
  x = (x & m2) + ((x >> 2) & m2);  // put count of each 4 bits into those 4 bits
  x = (x + (x >> 4)) & m4;         // put count of each 8 bits into those 8 bits
  x += x >> 8;   // put count of each 16 bits into their lowest 8 bits
  x += x >> 16;  // put count of each 32 bits into their lowest 8 bits
  x += x >> 32;  // put count of each 64 bits into their lowest 8 bits
  return x & 0x7f;
#endif
}

void StageLayout::preGeneratePermutations() {
  // Set an empty array for a bitmask of 0x0, this is used
  // by StepParityGenerator::getFootPlacementPermutations
  // to return an empty array
  permute_cache_[0] = {};

  for (unsigned int i = 0; i < pow(2, column_count_); i++) {
    int bits = popcount(i);
    if (bits > 4) {
      continue;
    }
    FootPlacement blank_columns(column_count_, Foot_None);

    std::vector<FootPlacement> placements =
        PermuteFootPlacements(i, blank_columns, 0);
    if (placements.size() > 0) {
      permute_cache_[i] = std::move(placements);
    }
  }
}

std::vector<FootPlacement> StageLayout::PermuteFootPlacements(
    unsigned int mask, FootPlacement test_columns, unsigned long column) {
  // If column >= columns.size(), we've reached the end of the row.
  // Perform some final validation before returning the contents of test_columns
  if (column >= test_columns.size()) {
    int left_heel_index = StepParity::kInvalidColumn;
    int left_toe_index = StepParity::kInvalidColumn;
    int right_heel_index = StepParity::kInvalidColumn;
    int right_toe_index = StepParity::kInvalidColumn;

    for (unsigned long i = 0; i < test_columns.size(); i++) {
      if (test_columns[i] == Foot_None) {
        continue;
      }
      if (test_columns[i] == Foot_LeftHeel) {
        left_heel_index = i;
      }
      if (test_columns[i] == Foot_LeftToe) {
        left_toe_index = i;
      }
      if (test_columns[i] == Foot_RightHeel) {
        right_heel_index = i;
      }
      if (test_columns[i] == Foot_RightToe) {
        right_toe_index = i;
      }
    }

    // Filter out actually invalid combinations:
    // - We don't want permutations where the toe is on an arrow, but not the
    // heel
    // - We don't want impossible brackets (eg you can't bracket up and down)
    if ((left_heel_index == StepParity::kInvalidColumn &&
         left_toe_index != StepParity::kInvalidColumn) ||
        (right_heel_index == StepParity::kInvalidColumn &&
         right_toe_index != StepParity::kInvalidColumn)) {
      return std::vector<FootPlacement>();
    }
    if (left_heel_index != StepParity::kInvalidColumn &&
        left_toe_index != StepParity::kInvalidColumn) {
      if (!bracketCheck(left_heel_index, left_toe_index)) {
        return std::vector<FootPlacement>();
      }
    }
    if (right_heel_index != StepParity::kInvalidColumn &&
        right_toe_index != StepParity::kInvalidColumn) {
      if (!bracketCheck(right_heel_index, right_toe_index)) {
        return std::vector<FootPlacement>();
      }
    }
    return {test_columns};
  }
  // If this column has a valid tap/hold head, or is actively holding a note,
  // iterate through values of StepParity::Foot. For each foot part, check that
  // it's not already present in columns, and if not, create a copy of columns,
  // and set the current foot part to the current column.
  // Then pass it to PermuteFootPlacements() and increment the column index.
  // Collect each permutationm, and then return all of them.
  //
  // The `ignoreHolds` flag is used as a workaround for situations where
  // we can't find a valid foot placement that allows us to continue the holds
  // (BREACH PROTOCOL doubles has  a row 0311 1000 which isn't bracketable
  // while still holding p1 down)
  std::vector<FootPlacement> permutations;
  bool active = (mask & (0x1 << column)) != 0;

  if (active) {
    for (StepParity::Foot foot : kFeet) {
      if (std::find(test_columns.begin(), test_columns.end(), foot) !=
          test_columns.end()) {
        continue;
      }

      FootPlacement new_columns = test_columns;

      new_columns[column] = foot;
      std::vector<FootPlacement> p =
          PermuteFootPlacements(mask, new_columns, column + 1);
      permutations.insert(permutations.end(), p.begin(), p.end());
    }
    return permutations;
  }
  // If the current column doesn't have any taps or holds,
  // then we don't need to generate any permutations for it.
  // Return the contents of calling PermuteFootPlacements() for the next column.
  return PermuteFootPlacements(mask, test_columns, column + 1);
}

// Row
void Row::setFootPlacement(const StepParity::State* state) {
  for (int c = 0; c < column_count_; c++) {
    if (notes_[c].type_ != TapNoteType_Empty) {
      columns_[c] = state->combined_columns_[c];
      where_the_feet_are_[state->combined_columns_[c]] = c;
      note_count_ += 1;
    }
  }
}
