#include "StepParityCost.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "NoteTypes.h"
#include "StepParityDatastructs.h"

using namespace StepParity;

namespace {
template <typename T>
bool isEmpty(const std::vector<T>& vec, int column_count) {
  for (int i = 0; i < column_count; i++) {
    if (static_cast<int>(vec[i]) != 0) {
      return false;
    }
  }
  return true;
}
}  // namespace

float StepParityCost::getActionCost(
    const State* initial_state, const State* result_state, const Row& row,
    const Row* previous_row, const FootPlacement& columns, float elapsed_time) {
  int column_count = row.column_count_;

  float cost = 0;

  // Mine weighting
  int left_heel = result_state->what_note_the_foot_is_hitting_[Foot_LeftHeel];
  int left_toe = result_state->what_note_the_foot_is_hitting_[Foot_LeftToe];
  int right_heel = result_state->what_note_the_foot_is_hitting_[Foot_RightHeel];
  int right_toe = result_state->what_note_the_foot_is_hitting_[Foot_RightToe];

  bool moved_left = result_state->did_the_foot_move_[Foot_LeftHeel] ||
                    result_state->did_the_foot_move_[Foot_LeftToe];

  bool moved_right = result_state->did_the_foot_move_[Foot_RightHeel] ||
                     result_state->did_the_foot_move_[Foot_RightToe];

  // Note that this is checking whether the previous state was a jump, not
  // whether the current state is
  bool did_jump = ((initial_state->did_the_foot_move_[Foot_LeftHeel] &&
                    !initial_state->is_the_foot_holding_[Foot_LeftHeel]) ||
                   (initial_state->did_the_foot_move_[Foot_LeftToe] &&
                    !initial_state->is_the_foot_holding_[Foot_LeftToe])) &&
                  ((initial_state->did_the_foot_move_[Foot_RightHeel] &&
                    !initial_state->is_the_foot_holding_[Foot_RightHeel]) ||
                   (initial_state->did_the_foot_move_[Foot_RightToe] &&
                    !initial_state->is_the_foot_holding_[Foot_RightToe]));

  bool jacked_left = didJackLeft(
      initial_state, result_state, left_heel, left_toe, moved_left, did_jump);
  bool jacked_right = didJackRight(
      initial_state, result_state, right_heel, right_toe, moved_right,
      did_jump);

  cost += calcMineCost(result_state, row, column_count);
  cost += calcHoldSwitchCost(initial_state, result_state, row, column_count);
  cost += calcBracketTapCost(
      initial_state, row, left_heel, left_toe, right_heel, right_toe,
      elapsed_time);
  cost += calcBracketJackCost(
      result_state, moved_left, moved_right, jacked_left, jacked_right,
      did_jump);
  cost += calcDoublestepCost(
      initial_state, result_state, row, previous_row, moved_left, moved_right,
      jacked_left, jacked_right, did_jump);
  cost += calcSlowBracketCost(row, moved_left, moved_right, elapsed_time);
  cost += calcTwistedFootCost(result_state);
  cost += calcFacingCosts(result_state);
  cost += calcSpinCosts(initial_state, result_state);
  cost += calcFootswitchCost(
      initial_state, columns, row, elapsed_time, column_count);
  cost += calcSideswitchCost(initial_state, result_state, columns);
  cost += calcMissedFootswitchCost(row, jacked_left, jacked_right);
  cost += calcJackCost(
      moved_left, moved_right, jacked_left, jacked_right, elapsed_time);
  cost +=
      calcBigMovementsQuicklyCost(initial_state, result_state, elapsed_time);

  return cost;
}

// Calculate the cost of avoiding a mine before the current step
// If a mine occurred just before a step, add to the cost
// ex:
// 00M0
// 0010 <- add cost
//
// 00M0
// 0100 <- no cost
float StepParityCost::calcMineCost(
    const State* result_state, const Row& row, int column_count) {
  if (row.mine_mask_ == 0 && row.fake_mine_mask_ == 0) {
    return 0.0f;
  }
  float cost = 0;

  for (int i = 0; i < column_count; i++) {
    if (result_state->combined_columns_[i] != Foot_None &&
        (row.mines_[i] != 0 || row.fake_mines_[i] != 0)) {
      cost += kMine;
      break;
    }
  }
  return cost;
}

// Calculate a cost from having to switch feet in the middle of a hold.
// Multiply the HOLDSWITCH cost by the distance that the "intial" foot
// that was holding the note had to travel to it's new position.
// If the initial foot doesn't move anywhere, then don't mulitply it by
// anything.
float StepParityCost::calcHoldSwitchCost(
    const State* initial_state, const State* result_state, const Row& row,
    int column_count) {
  if (row.hold_mask_ == 0) {
    return 0.0f;
  }

  float cost = 0;

  for (int c = 0; c < column_count; c++) {
    if (row.holds_[c].type_ == TapNoteType_Empty) {
      continue;
    }
    if (((result_state->combined_columns_[c] == Foot_LeftHeel ||
          result_state->combined_columns_[c] == Foot_LeftToe) &&
         initial_state->combined_columns_[c] != Foot_LeftToe &&
         initial_state->combined_columns_[c] != Foot_LeftHeel) ||
        ((result_state->combined_columns_[c] == Foot_RightHeel ||
          result_state->combined_columns_[c] == Foot_RightToe) &&
         initial_state->combined_columns_[c] != Foot_RightToe &&
         initial_state->combined_columns_[c] != Foot_RightHeel)) {
      int previous_foot =
          initial_state
              ->where_the_feet_are_[result_state->combined_columns_[c]];
      cost +=
          kHoldswitch * (previous_foot == kInvalidColumn
                             ? 1
                             : sqrt(layout_->getDistanceSq(c, previous_foot)));
    }
  }
  return cost;
}

// Calculate the cost of tapping a bracket during a hold note
//
// ex:
// 0200
// 0000
// 1000 <- maybe bracketable, if left heel is holding Down arrow
// 0300

float StepParityCost::calcBracketTapCost(
    const State* initial_state, const Row& row, int left_heel, int left_toe,
    int right_heel, int right_toe, float elapsed_time) {
  if (row.hold_mask_ == 0) {
    return 0.0f;
  }

  // Small penalty for trying to jack a bracket during a hold
  float cost = 0;
  if (left_heel != kInvalidColumn && left_toe != kInvalidColumn) {
    float jack_penalty = 1;
    if (initial_state->did_the_foot_move_[Foot_LeftHeel] ||
        initial_state->did_the_foot_move_[Foot_LeftToe]) {
      jack_penalty = 1 / elapsed_time;
    }
    if (row.holds_[left_heel].type_ != TapNoteType_Empty &&
        row.holds_[left_toe].type_ == TapNoteType_Empty) {
      cost += kBrackettap * jack_penalty;
    }
    if (row.holds_[left_toe].type_ != TapNoteType_Empty &&
        row.holds_[left_heel].type_ == TapNoteType_Empty) {
      cost += kBrackettap * jack_penalty;
    }
  }

  if (right_heel != kInvalidColumn && right_toe != kInvalidColumn) {
    float jack_penalty = 1;
    if (initial_state->did_the_foot_move_[Foot_RightToe] ||
        initial_state->did_the_foot_move_[Foot_RightHeel]) {
      jack_penalty = 1 / elapsed_time;
    }

    if (row.holds_[right_heel].type_ != TapNoteType_Empty &&
        row.holds_[right_toe].type_ == TapNoteType_Empty) {
      cost += kBrackettap * jack_penalty;
    }
    if (row.holds_[right_toe].type_ != TapNoteType_Empty &&
        row.holds_[right_heel].type_ == TapNoteType_Empty) {
      cost += kBrackettap * jack_penalty;
    }
  }
  return cost;
}

float StepParityCost::calcBracketJackCost(
    const State* result_state, bool moved_left, bool moved_right,
    bool jacked_left, bool jacked_right, bool did_jump) {
  if (moved_left == moved_right || result_state->holding_mask_ != 0 ||
      did_jump) {
    return 0.0f;
  }
  float cost = 0;

  if (jacked_left && result_state->did_the_foot_move_[Foot_LeftHeel] &&
      result_state->did_the_foot_move_[Foot_LeftToe]) {
    cost += kBracketjack;
  }

  if (jacked_right && result_state->did_the_foot_move_[Foot_RightHeel] &&
      result_state->did_the_foot_move_[Foot_RightToe]) {
    cost += kBracketjack;
  }
  return cost;
}

float StepParityCost::calcDoublestepCost(
    const State* initial_state, const State* result_state, const Row& row,
    const Row* previous_row, bool moved_left, bool moved_right,
    bool jacked_left, bool jacked_right, bool did_jump) {
  if ((moved_left == moved_right) || result_state->holding_mask_ != 0 ||
      did_jump) {
    return 0.0f;
  }

  float cost = 0;
  bool doublestepped = didDoubleStep(
      initial_state, row, previous_row, moved_left, jacked_left, moved_right,
      jacked_right);

  if (doublestepped) {
    cost += kDoublestep;
  }
  return cost;
}

// Jumps should be prioritized over brackets below a certain speed
float StepParityCost::calcSlowBracketCost(
    const Row& row, bool moved_left, bool moved_right, float elapsed_time) {
  float cost = 0;
  if (elapsed_time > kSlowBracketThreshold && moved_left != moved_right &&
      std::count_if(
          row.notes_.begin(), row.notes_.end(),
          [](StepParity::IntermediateNoteData note) {
            return note.type_ != TapNoteType_Empty;
          }) >= 2) {
    float timediff = elapsed_time - kSlowBracketThreshold;
    cost += timediff * kSlowBracket;
  }
  return cost;
}

// Does this placement result in one of the feet being twisted around?
// This should probably be getting filtered out as an invalid positioning before
// we even get to calculating costs.
float StepParityCost::calcTwistedFootCost(const State* result_state) {
  float cost = 0;
  int left_heel = result_state->what_note_the_foot_is_hitting_[Foot_LeftHeel];
  int left_toe = result_state->what_note_the_foot_is_hitting_[Foot_LeftToe];
  int right_heel = result_state->what_note_the_foot_is_hitting_[Foot_RightHeel];
  int right_toe = result_state->what_note_the_foot_is_hitting_[Foot_RightToe];

  StagePoint left_pos = layout_->averagePoint(left_heel, left_toe);
  StagePoint right_pos = layout_->averagePoint(right_heel, right_toe);

  bool crossed_over = right_pos.x_ < left_pos.x_;
  bool right_backwards =
      right_heel != kInvalidColumn && right_toe != kInvalidColumn
          ? layout_->columns_[right_toe].y_ < layout_->columns_[right_heel].y_
          : false;
  bool left_backwards =
      left_heel != kInvalidColumn && left_toe != kInvalidColumn
          ? layout_->columns_[left_toe].y_ < layout_->columns_[left_heel].y_
          : false;

  if (!crossed_over && (right_backwards || left_backwards)) {
    cost += kTwistedFoot;
  }
  return cost;
}

float StepParityCost::calcMissedFootswitchCost(
    const Row& row, bool jacked_left, bool jacked_right) {
  float cost = 0;
  if ((jacked_left || jacked_right) &&
      (row.mine_mask_ != 0 || row.fake_mine_mask_ != 0)) {
    cost += kMissedFootswitch;
  }
  return cost;
}

float StepParityCost::calcFacingCosts(const State* result_state) {
  int end_left_heel = result_state->where_the_feet_are_[Foot_LeftHeel];
  int end_left_toe = result_state->where_the_feet_are_[Foot_LeftToe];
  int end_right_heel = result_state->where_the_feet_are_[Foot_RightHeel];
  int end_right_toe = result_state->where_the_feet_are_[Foot_RightToe];

  if (end_left_toe == kInvalidColumn) {
    end_left_toe = end_left_heel;
  }
  if (end_right_toe == kInvalidColumn) {
    end_right_toe = end_right_heel;
  }

  float heel_facing_penalty =
      layout_->getXFacingPenalty(end_left_heel, end_right_heel) * kFacing;
  float toes_facing_penalty =
      layout_->getXFacingPenalty(end_left_toe, end_right_toe) * kFacing;
  float left_facing_penalty =
      layout_->getYFacingPenalty(end_left_heel, end_left_toe) * kFacing;
  float right_facing_penalty =
      layout_->getYFacingPenalty(end_right_heel, end_right_toe) * kFacing;

  float cost = heel_facing_penalty + toes_facing_penalty + left_facing_penalty +
               right_facing_penalty;
  return cost;
}

float StepParityCost::calcSpinCosts(
    const State* initial_state, const State* result_state) {
  float cost = 0;

  int end_left_heel = result_state->where_the_feet_are_[Foot_LeftHeel];
  int end_left_toe = result_state->where_the_feet_are_[Foot_LeftToe];
  int end_right_heel = result_state->where_the_feet_are_[Foot_RightHeel];
  int end_right_toe = result_state->where_the_feet_are_[Foot_RightToe];

  if (end_left_toe == kInvalidColumn) {
    end_left_toe = end_left_heel;
  }
  if (end_right_toe == kInvalidColumn) {
    end_right_toe = end_right_heel;
  }

  // spin
  StagePoint previous_left_pos = layout_->averagePoint(
      initial_state->where_the_feet_are_[Foot_LeftHeel],
      initial_state->where_the_feet_are_[Foot_LeftToe]);
  StagePoint previous_right_pos = layout_->averagePoint(
      initial_state->where_the_feet_are_[Foot_RightHeel],
      initial_state->where_the_feet_are_[Foot_RightToe]);
  StagePoint left_pos = layout_->averagePoint(end_left_heel, end_left_toe);
  StagePoint right_pos = layout_->averagePoint(end_right_heel, end_right_toe);

  if (right_pos.x_ < left_pos.x_ &&
      previous_right_pos.x_ < previous_left_pos.x_ &&
      right_pos.y_ < left_pos.y_ &&
      previous_right_pos.y_ > previous_left_pos.y_) {
    cost += kSpin;
  }
  if (right_pos.x_ < left_pos.x_ &&
      previous_right_pos.x_ < previous_left_pos.x_ &&
      right_pos.y_ > left_pos.y_ &&
      previous_right_pos.y_ < previous_left_pos.y_) {
    cost += kSpin;
  }
  return cost;
}

// Footswitches are harder to do when they get too slow.
// Notes with an elapsed time greater than this will incur a penalty
float StepParityCost::calcFootswitchCost(
    const State* initial_state, const FootPlacement& columns, const Row& row,
    float elapsed_time, int column_count) {
  if (elapsed_time < kSlowFootswitchThreshold ||
      elapsed_time >= kSlowFootswitchIgnore) {
    return 0.0f;
  }

  // footswitching has no penalty if there's a mine nearby
  if (row.mine_mask_ != 0 || row.fake_mine_mask_ != 0) {
    return 0.0f;
  }

  float cost = 0;
  float time_scaled = elapsed_time - kSlowFootswitchThreshold;

  for (int i = 0; i < column_count; i++) {
    if (initial_state->combined_columns_[i] == Foot_None ||
        columns[i] == Foot_None) {
      continue;
    }

    if (initial_state->combined_columns_[i] != columns[i] &&
        initial_state->combined_columns_[i] != kOtherPartOfFoot[columns[i]]) {
      cost += (time_scaled / (kSlowFootswitchThreshold + time_scaled)) *
              kFootswitch;
      break;
    }
  }
  return cost;
}

float StepParityCost::calcSideswitchCost(
    const State* initial_state, const State* result_state,
    const FootPlacement& columns) {
  float cost = 0;
  for (auto c : layout_->side_arrows_) {
    if (initial_state->combined_columns_[c] != columns[c] &&
        columns[c] != Foot_None &&
        initial_state->combined_columns_[c] != Foot_None &&
        !result_state
             ->did_the_foot_move_[initial_state->combined_columns_[c]]) {
      cost += kSideswitch;
    }
  }
  return cost;
}

// Jacks are harder to do the faster they are.
// Add a penalty when they get faster than 16ths at 150bpm (0.1 seconds)
float StepParityCost::calcJackCost(
    bool moved_left, bool moved_right, bool jacked_left, bool jacked_right,
    float elapsed_time) {
  float cost = 0;
  // weighting for jacking two notes too close to eachother
  if (elapsed_time < kJackThreshold && moved_left != moved_right) {
    float time_scaled = kJackThreshold - elapsed_time;
    if (jacked_left || jacked_right) {
      cost += (1 / time_scaled - 1 / kJackThreshold) * kJack;
    }
  }

  return cost;
}

float StepParityCost::calcBigMovementsQuicklyCost(
    const State* initial_state, const State* result_state, float elapsed_time) {
  float cost = 0;
  for (StepParity::Foot foot : kFeet) {
    if ((result_state->moved_mask_ & kFootMasks[foot]) == 0) {
      continue;
    }

    int initial_position = initial_state->where_the_feet_are_[foot];
    if (initial_position == kInvalidColumn) {
      continue;
    }

    int result_position = result_state->what_note_the_foot_is_hitting_[foot];

    // If we're bracketing something, and the toes are now where the heel
    // was, then we don't need to worry about it, we're not actually moving
    // the foot very far
    bool is_bracketing =
        result_state->what_note_the_foot_is_hitting_[kOtherPartOfFoot[foot]] !=
        kInvalidColumn;
    if (is_bracketing &&
        result_state->what_note_the_foot_is_hitting_[kOtherPartOfFoot[foot]] ==
            initial_position) {
      continue;
    }

    float dist =
        (layout_->getDistance(initial_position, result_position) * kDistance) /
        elapsed_time;
    // Otherwise if we're still bracketing, this is probably a less drastic
    // movement
    if (is_bracketing) {
      dist = dist * 0.2;
    }
    cost += dist;
  }

  return cost;
}

bool StepParityCost::didDoubleStep(
    const State* initial_state, const Row& row, const Row* previous_row,
    bool moved_left, bool jacked_left, bool moved_right, bool jacked_right) {
  bool doublestepped = false;
  if (moved_left && !jacked_left &&
      ((initial_state->did_the_foot_move_[Foot_LeftHeel] &&
        !initial_state->is_the_foot_holding_[Foot_LeftHeel]) ||
       (initial_state->did_the_foot_move_[Foot_LeftToe] &&
        !initial_state->is_the_foot_holding_[Foot_LeftToe]))) {
    doublestepped = true;
  }
  if (moved_right && !jacked_right &&
      ((initial_state->did_the_foot_move_[Foot_RightHeel] &&
        !initial_state->is_the_foot_holding_[Foot_RightHeel]) ||
       (initial_state->did_the_foot_move_[Foot_RightToe] &&
        !initial_state->is_the_foot_holding_[Foot_RightToe]))) {
    doublestepped = true;
  }

  if (previous_row != nullptr) {
    const StepParity::Row& last_row = *previous_row;
    for (StepParity::IntermediateNoteData hold : last_row.holds_) {
      if (hold.type_ == TapNoteType_Empty) {
        continue;
      }
      float end_beat = row.beat_;
      float start_beat = last_row.beat_;
      // if a hold tail extends past the last row & ends in between, we can
      // doublestep
      if (hold.beat_ + hold.hold_length_ > start_beat &&
          hold.beat_ + hold.hold_length_ < end_beat) {
        doublestepped = false;
      }
      // if the hold tail extends past this row, we can doublestep
      if (hold.beat_ + hold.hold_length_ >= end_beat) {
        doublestepped = false;
      }
    }
  }
  return doublestepped;
}

bool StepParityCost::didJackLeft(
    const State* initial_state, const State* result_state, int left_heel,
    int left_toe, bool moved_left, bool did_jump) {
  bool jacked_left = false;
  if (!did_jump && moved_left) {
    if (left_heel > kInvalidColumn &&
        initial_state->combined_columns_[left_heel] == Foot_LeftHeel &&
        !result_state->is_the_foot_holding_[Foot_LeftHeel] &&
        ((initial_state->did_the_foot_move_[Foot_LeftHeel] &&
          !initial_state->is_the_foot_holding_[Foot_LeftHeel]) ||
         (initial_state->did_the_foot_move_[Foot_LeftToe] &&
          !initial_state->is_the_foot_holding_[Foot_LeftToe]))) {
      jacked_left = true;
    }
    if (left_toe > kInvalidColumn &&
        initial_state->combined_columns_[left_toe] == Foot_LeftToe &&
        !result_state->is_the_foot_holding_[Foot_LeftToe] &&
        ((initial_state->did_the_foot_move_[Foot_LeftHeel] &&
          !initial_state->is_the_foot_holding_[Foot_LeftHeel]) ||
         (initial_state->did_the_foot_move_[Foot_LeftToe] &&
          !initial_state->is_the_foot_holding_[Foot_LeftToe]))) {
      jacked_left = true;
    }
  }
  return jacked_left;
}

bool StepParityCost::didJackRight(
    const State* initial_state, const State* result_state, int right_heel,
    int right_toe, bool moved_right, bool did_jump) {
  bool jacked_right = false;
  if (!did_jump && moved_right) {
    if (right_heel > kInvalidColumn &&
        initial_state->combined_columns_[right_heel] == Foot_RightHeel &&
        !result_state->is_the_foot_holding_[Foot_RightHeel] &&
        ((initial_state->did_the_foot_move_[Foot_RightHeel] &&
          !initial_state->is_the_foot_holding_[Foot_RightHeel]) ||
         (initial_state->did_the_foot_move_[Foot_RightToe] &&
          !initial_state->is_the_foot_holding_[Foot_RightToe]))) {
      jacked_right = true;
    }
    if (right_toe > kInvalidColumn &&
        initial_state->combined_columns_[right_toe] == Foot_RightToe &&
        !result_state->is_the_foot_holding_[Foot_RightToe] &&
        ((initial_state->did_the_foot_move_[Foot_RightHeel] &&
          !initial_state->is_the_foot_holding_[Foot_RightHeel]) ||
         (initial_state->did_the_foot_move_[Foot_RightToe] &&
          !initial_state->is_the_foot_holding_[Foot_RightToe]))) {
      jacked_right = true;
    }
  }
  return jacked_right;
}
