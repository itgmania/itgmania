#include "StepParityCost.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "NoteTypes.h"
#include "StepParityDatastructs.h"

using namespace StepParity;

namespace {
template <typename T>
bool isEmpty(const std::vector<T>& vec, int columnCount) {
  for (int i = 0; i < columnCount; i++) {
    if (static_cast<int>(vec[i]) != 0) {
      return false;
    }
  }
  return true;
}
}  // namespace

float StepParityCost::getActionCost(
    const State* initialState, const State* resultState, const Row& row,
    const Row* previousRow, const FootPlacement& columns, float elapsedTime) {
  int columnCount = row.column_count_;

  float cost = 0;

  // Mine weighting
  int leftHeel = resultState->what_note_the_foot_is_hitting_[Foot_LeftHeel];
  int leftToe = resultState->what_note_the_foot_is_hitting_[Foot_LeftToe];
  int rightHeel = resultState->what_note_the_foot_is_hitting_[Foot_RightHeel];
  int rightToe = resultState->what_note_the_foot_is_hitting_[Foot_RightToe];

  bool movedLeft = resultState->did_the_foot_move_[Foot_LeftHeel] ||
                   resultState->did_the_foot_move_[Foot_LeftToe];

  bool movedRight = resultState->did_the_foot_move_[Foot_RightHeel] ||
                    resultState->did_the_foot_move_[Foot_RightToe];

  // Note that this is checking whether the previous state was a jump, not
  // whether the current state is
  bool didJump = ((initialState->did_the_foot_move_[Foot_LeftHeel] &&
                   !initialState->is_the_foot_holding_[Foot_LeftHeel]) ||
                  (initialState->did_the_foot_move_[Foot_LeftToe] &&
                   !initialState->is_the_foot_holding_[Foot_LeftToe])) &&
                 ((initialState->did_the_foot_move_[Foot_RightHeel] &&
                   !initialState->is_the_foot_holding_[Foot_RightHeel]) ||
                  (initialState->did_the_foot_move_[Foot_RightToe] &&
                   !initialState->is_the_foot_holding_[Foot_RightToe]));

  bool jackedLeft = didJackLeft(
      initialState, resultState, leftHeel, leftToe, movedLeft, didJump);
  bool jackedRight = didJackRight(
      initialState, resultState, rightHeel, rightToe, movedRight, didJump);

  cost += calcMineCost(resultState, row, columnCount);
  cost += calcHoldSwitchCost(initialState, resultState, row, columnCount);
  cost += calcBracketTapCost(
      initialState, row, leftHeel, leftToe, rightHeel, rightToe, elapsedTime);
  cost += calcBracketJackCost(
      resultState, movedLeft, movedRight, jackedLeft, jackedRight, didJump);
  cost += calcDoublestepCost(
      initialState, resultState, row, previousRow, movedLeft, movedRight,
      jackedLeft, jackedRight, didJump);
  cost += calcSlowBracketCost(row, movedLeft, movedRight, elapsedTime);
  cost += calcTwistedFootCost(resultState);
  cost += calcFacingCosts(resultState);
  cost += calcSpinCosts(initialState, resultState);
  cost +=
      calcFootswitchCost(initialState, columns, row, elapsedTime, columnCount);
  cost += calcSideswitchCost(initialState, resultState, columns);
  cost += calcMissedFootswitchCost(row, jackedLeft, jackedRight);
  cost +=
      calcJackCost(movedLeft, movedRight, jackedLeft, jackedRight, elapsedTime);
  cost += calcBigMovementsQuicklyCost(initialState, resultState, elapsedTime);

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
    const State* resultState, const Row& row, int columnCount) {
  if (row.mine_mask_ == 0 && row.fake_mine_mask_ == 0) {
    return 0.0f;
  }
  float cost = 0;

  for (int i = 0; i < columnCount; i++) {
    if (resultState->combined_columns_[i] != Foot_None &&
        (row.mines_[i] != 0 || row.fake_mines_[i] != 0)) {
      cost += MINE;
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
    const State* initialState, const State* resultState, const Row& row,
    int columnCount) {
  if (row.hold_mask_ == 0) {
    return 0.0f;
  }

  float cost = 0;

  for (int c = 0; c < columnCount; c++) {
    if (row.holds_[c].type_ == TapNoteType_Empty) {
      continue;
    }
    if (((resultState->combined_columns_[c] == Foot_LeftHeel ||
          resultState->combined_columns_[c] == Foot_LeftToe) &&
         initialState->combined_columns_[c] != Foot_LeftToe &&
         initialState->combined_columns_[c] != Foot_LeftHeel) ||
        ((resultState->combined_columns_[c] == Foot_RightHeel ||
          resultState->combined_columns_[c] == Foot_RightToe) &&
         initialState->combined_columns_[c] != Foot_RightToe &&
         initialState->combined_columns_[c] != Foot_RightHeel)) {
      int previousFoot =
          initialState->where_the_feet_are_[resultState->combined_columns_[c]];
      cost +=
          HOLDSWITCH * (previousFoot == INVALID_COLUMN
                            ? 1
                            : sqrt(layout_->getDistanceSq(c, previousFoot)));
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
    const State* initialState, const Row& row, int leftHeel, int leftToe,
    int rightHeel, int rightToe, float elapsedTime) {
  if (row.hold_mask_ == 0) {
    return 0.0f;
  }

  // Small penalty for trying to jack a bracket during a hold
  float cost = 0;
  if (leftHeel != INVALID_COLUMN && leftToe != INVALID_COLUMN) {
    float jackPenalty = 1;
    if (initialState->did_the_foot_move_[Foot_LeftHeel] ||
        initialState->did_the_foot_move_[Foot_LeftToe]) {
      jackPenalty = 1 / elapsedTime;
    }
    if (row.holds_[leftHeel].type_ != TapNoteType_Empty &&
        row.holds_[leftToe].type_ == TapNoteType_Empty) {
      cost += BRACKETTAP * jackPenalty;
    }
    if (row.holds_[leftToe].type_ != TapNoteType_Empty &&
        row.holds_[leftHeel].type_ == TapNoteType_Empty) {
      cost += BRACKETTAP * jackPenalty;
    }
  }

  if (rightHeel != INVALID_COLUMN && rightToe != INVALID_COLUMN) {
    float jackPenalty = 1;
    if (initialState->did_the_foot_move_[Foot_RightToe] ||
        initialState->did_the_foot_move_[Foot_RightHeel]) {
      jackPenalty = 1 / elapsedTime;
    }

    if (row.holds_[rightHeel].type_ != TapNoteType_Empty &&
        row.holds_[rightToe].type_ == TapNoteType_Empty) {
      cost += BRACKETTAP * jackPenalty;
    }
    if (row.holds_[rightToe].type_ != TapNoteType_Empty &&
        row.holds_[rightHeel].type_ == TapNoteType_Empty) {
      cost += BRACKETTAP * jackPenalty;
    }
  }
  return cost;
}

float StepParityCost::calcBracketJackCost(
    const State* resultState, bool movedLeft, bool movedRight, bool jackedLeft,
    bool jackedRight, bool didJump) {
  if (movedLeft == movedRight || resultState->holding_mask_ != 0 || didJump) {
    return 0.0f;
  }
  float cost = 0;

  if (jackedLeft && resultState->did_the_foot_move_[Foot_LeftHeel] &&
      resultState->did_the_foot_move_[Foot_LeftToe]) {
    cost += BRACKETJACK;
  }

  if (jackedRight && resultState->did_the_foot_move_[Foot_RightHeel] &&
      resultState->did_the_foot_move_[Foot_RightToe]) {
    cost += BRACKETJACK;
  }
  return cost;
}

float StepParityCost::calcDoublestepCost(
    const State* initialState, const State* resultState, const Row& row,
    const Row* previousRow, bool movedLeft, bool movedRight, bool jackedLeft,
    bool jackedRight, bool didJump) {
  if ((movedLeft == movedRight) || resultState->holding_mask_ != 0 || didJump) {
    return 0.0f;
  }

  float cost = 0;
  bool doublestepped = didDoubleStep(
      initialState, row, previousRow, movedLeft, jackedLeft, movedRight,
      jackedRight);

  if (doublestepped) {
    cost += DOUBLESTEP;
  }
  return cost;
}

// Jumps should be prioritized over brackets below a certain speed
float StepParityCost::calcSlowBracketCost(
    const Row& row, bool movedLeft, bool movedRight, float elapsedTime) {
  float cost = 0;
  if (elapsedTime > SLOW_BRACKET_THRESHOLD && movedLeft != movedRight &&
      std::count_if(
          row.notes_.begin(), row.notes_.end(),
          [](StepParity::IntermediateNoteData note) {
            return note.type_ != TapNoteType_Empty;
          }) >= 2) {
    float timediff = elapsedTime - SLOW_BRACKET_THRESHOLD;
    cost += timediff * SLOW_BRACKET;
  }
  return cost;
}

// Does this placement result in one of the feet being twisted around?
// This should probably be getting filtered out as an invalid positioning before
// we even get to calculating costs.
float StepParityCost::calcTwistedFootCost(const State* resultState) {
  float cost = 0;
  int leftHeel = resultState->what_note_the_foot_is_hitting_[Foot_LeftHeel];
  int leftToe = resultState->what_note_the_foot_is_hitting_[Foot_LeftToe];
  int rightHeel = resultState->what_note_the_foot_is_hitting_[Foot_RightHeel];
  int rightToe = resultState->what_note_the_foot_is_hitting_[Foot_RightToe];

  StagePoint leftPos = layout_->averagePoint(leftHeel, leftToe);
  StagePoint rightPos = layout_->averagePoint(rightHeel, rightToe);

  bool crossedOver = rightPos.x_ < leftPos.x_;
  bool rightBackwards =
      rightHeel != INVALID_COLUMN && rightToe != INVALID_COLUMN
          ? layout_->columns_[rightToe].y_ < layout_->columns_[rightHeel].y_
          : false;
  bool leftBackwards =
      leftHeel != INVALID_COLUMN && leftToe != INVALID_COLUMN
          ? layout_->columns_[leftToe].y_ < layout_->columns_[leftHeel].y_
          : false;

  if (!crossedOver && (rightBackwards || leftBackwards)) {
    cost += TWISTED_FOOT;
  }
  return cost;
}

float StepParityCost::calcMissedFootswitchCost(
    const Row& row, bool jackedLeft, bool jackedRight) {
  float cost = 0;
  if ((jackedLeft || jackedRight) &&
      (row.mine_mask_ != 0 || row.fake_mine_mask_ != 0)) {
    cost += MISSED_FOOTSWITCH;
  }
  return cost;
}

float StepParityCost::calcFacingCosts(const State* resultState) {
  int endLeftHeel = resultState->where_the_feet_are_[Foot_LeftHeel];
  int endLeftToe = resultState->where_the_feet_are_[Foot_LeftToe];
  int endRightHeel = resultState->where_the_feet_are_[Foot_RightHeel];
  int endRightToe = resultState->where_the_feet_are_[Foot_RightToe];

  if (endLeftToe == INVALID_COLUMN) {
    endLeftToe = endLeftHeel;
  }
  if (endRightToe == INVALID_COLUMN) {
    endRightToe = endRightHeel;
  }

  float heelFacingPenalty =
      layout_->getXFacingPenalty(endLeftHeel, endRightHeel) * FACING;
  float toesFacingPenalty =
      layout_->getXFacingPenalty(endLeftToe, endRightToe) * FACING;
  float leftFacingPenalty =
      layout_->getYFacingPenalty(endLeftHeel, endLeftToe) * FACING;
  float rightFacingPenalty =
      layout_->getYFacingPenalty(endRightHeel, endRightToe) * FACING;

  float cost = heelFacingPenalty + toesFacingPenalty + leftFacingPenalty +
               rightFacingPenalty;
  return cost;
}

float StepParityCost::calcSpinCosts(
    const State* initialState, const State* resultState) {
  float cost = 0;

  int endLeftHeel = resultState->where_the_feet_are_[Foot_LeftHeel];
  int endLeftToe = resultState->where_the_feet_are_[Foot_LeftToe];
  int endRightHeel = resultState->where_the_feet_are_[Foot_RightHeel];
  int endRightToe = resultState->where_the_feet_are_[Foot_RightToe];

  if (endLeftToe == INVALID_COLUMN) {
    endLeftToe = endLeftHeel;
  }
  if (endRightToe == INVALID_COLUMN) {
    endRightToe = endRightHeel;
  }

  // spin
  StagePoint previousLeftPos = layout_->averagePoint(
      initialState->where_the_feet_are_[Foot_LeftHeel],
      initialState->where_the_feet_are_[Foot_LeftToe]);
  StagePoint previousRightPos = layout_->averagePoint(
      initialState->where_the_feet_are_[Foot_RightHeel],
      initialState->where_the_feet_are_[Foot_RightToe]);
  StagePoint leftPos = layout_->averagePoint(endLeftHeel, endLeftToe);
  StagePoint rightPos = layout_->averagePoint(endRightHeel, endRightToe);

  if (rightPos.x_ < leftPos.x_ && previousRightPos.x_ < previousLeftPos.x_ &&
      rightPos.y_ < leftPos.y_ && previousRightPos.y_ > previousLeftPos.y_) {
    cost += SPIN;
  }
  if (rightPos.x_ < leftPos.x_ && previousRightPos.x_ < previousLeftPos.x_ &&
      rightPos.y_ > leftPos.y_ && previousRightPos.y_ < previousLeftPos.y_) {
    cost += SPIN;
  }
  return cost;
}

// Footswitches are harder to do when they get too slow.
// Notes with an elapsed time greater than this will incur a penalty
float StepParityCost::calcFootswitchCost(
    const State* initialState, const FootPlacement& columns, const Row& row,
    float elapsedTime, int columnCount) {
  if (elapsedTime < SLOW_FOOTSWITCH_THRESHOLD ||
      elapsedTime >= SLOW_FOOTSWITCH_IGNORE) {
    return 0.0f;
  }

  // footswitching has no penalty if there's a mine nearby
  if (row.mine_mask_ != 0 || row.fake_mine_mask_ != 0) {
    return 0.0f;
  }

  float cost = 0;
  float timeScaled = elapsedTime - SLOW_FOOTSWITCH_THRESHOLD;

  for (int i = 0; i < columnCount; i++) {
    if (initialState->combined_columns_[i] == Foot_None ||
        columns[i] == Foot_None) {
      continue;
    }

    if (initialState->combined_columns_[i] != columns[i] &&
        initialState->combined_columns_[i] != OTHER_PART_OF_FOOT[columns[i]]) {
      cost +=
          (timeScaled / (SLOW_FOOTSWITCH_THRESHOLD + timeScaled)) * FOOTSWITCH;
      break;
    }
  }
  return cost;
}

float StepParityCost::calcSideswitchCost(
    const State* initialState, const State* resultState,
    const FootPlacement& columns) {
  float cost = 0;
  for (auto c : layout_->side_arrows_) {
    if (initialState->combined_columns_[c] != columns[c] &&
        columns[c] != Foot_None &&
        initialState->combined_columns_[c] != Foot_None &&
        !resultState->did_the_foot_move_[initialState->combined_columns_[c]]) {
      cost += SIDESWITCH;
    }
  }
  return cost;
}

// Jacks are harder to do the faster they are.
// Add a penalty when they get faster than 16ths at 150bpm (0.1 seconds)
float StepParityCost::calcJackCost(
    bool movedLeft, bool movedRight, bool jackedLeft, bool jackedRight,
    float elapsedTime) {
  float cost = 0;
  // weighting for jacking two notes too close to eachother
  if (elapsedTime < JACK_THRESHOLD && movedLeft != movedRight) {
    float timeScaled = JACK_THRESHOLD - elapsedTime;
    if (jackedLeft || jackedRight) {
      cost += (1 / timeScaled - 1 / JACK_THRESHOLD) * JACK;
    }
  }

  return cost;
}

float StepParityCost::calcBigMovementsQuicklyCost(
    const State* initialState, const State* resultState, float elapsedTime) {
  float cost = 0;
  for (StepParity::Foot foot : FEET) {
    if ((resultState->moved_mask_ & FOOT_MASKS[foot]) == 0) {
      continue;
    }

    int initialPosition = initialState->where_the_feet_are_[foot];
    if (initialPosition == INVALID_COLUMN) {
      continue;
    }

    int resultPosition = resultState->what_note_the_foot_is_hitting_[foot];

    // If we're bracketing something, and the toes are now where the heel
    // was, then we don't need to worry about it, we're not actually moving
    // the foot very far
    bool isBracketing =
        resultState->what_note_the_foot_is_hitting_[OTHER_PART_OF_FOOT[foot]] !=
        INVALID_COLUMN;
    if (isBracketing &&
        resultState->what_note_the_foot_is_hitting_[OTHER_PART_OF_FOOT[foot]] ==
            initialPosition) {
      continue;
    }

    float dist =
        (layout_->getDistance(initialPosition, resultPosition) * DISTANCE) /
        elapsedTime;
    // Otherwise if we're still bracketing, this is probably a less drastic
    // movement
    if (isBracketing) {
      dist = dist * 0.2;
    }
    cost += dist;
  }

  return cost;
}

bool StepParityCost::didDoubleStep(
    const State* initialState, const Row& row, const Row* previousRow,
    bool movedLeft, bool jackedLeft, bool movedRight, bool jackedRight) {
  bool doublestepped = false;
  if (movedLeft && !jackedLeft &&
      ((initialState->did_the_foot_move_[Foot_LeftHeel] &&
        !initialState->is_the_foot_holding_[Foot_LeftHeel]) ||
       (initialState->did_the_foot_move_[Foot_LeftToe] &&
        !initialState->is_the_foot_holding_[Foot_LeftToe]))) {
    doublestepped = true;
  }
  if (movedRight && !jackedRight &&
      ((initialState->did_the_foot_move_[Foot_RightHeel] &&
        !initialState->is_the_foot_holding_[Foot_RightHeel]) ||
       (initialState->did_the_foot_move_[Foot_RightToe] &&
        !initialState->is_the_foot_holding_[Foot_RightToe]))) {
    doublestepped = true;
  }

  if (previousRow != nullptr) {
    const StepParity::Row& lastRow = *previousRow;
    for (StepParity::IntermediateNoteData hold : lastRow.holds_) {
      if (hold.type_ == TapNoteType_Empty) {
        continue;
      }
      float endBeat = row.beat_;
      float startBeat = lastRow.beat_;
      // if a hold tail extends past the last row & ends in between, we can
      // doublestep
      if (hold.beat_ + hold.hold_length_ > startBeat &&
          hold.beat_ + hold.hold_length_ < endBeat) {
        doublestepped = false;
      }
      // if the hold tail extends past this row, we can doublestep
      if (hold.beat_ + hold.hold_length_ >= endBeat) {
        doublestepped = false;
      }
    }
  }
  return doublestepped;
}

bool StepParityCost::didJackLeft(
    const State* initialState, const State* resultState, int leftHeel,
    int leftToe, bool movedLeft, bool didJump) {
  bool jackedLeft = false;
  if (!didJump && movedLeft) {
    if (leftHeel > INVALID_COLUMN &&
        initialState->combined_columns_[leftHeel] == Foot_LeftHeel &&
        !resultState->is_the_foot_holding_[Foot_LeftHeel] &&
        ((initialState->did_the_foot_move_[Foot_LeftHeel] &&
          !initialState->is_the_foot_holding_[Foot_LeftHeel]) ||
         (initialState->did_the_foot_move_[Foot_LeftToe] &&
          !initialState->is_the_foot_holding_[Foot_LeftToe]))) {
      jackedLeft = true;
    }
    if (leftToe > INVALID_COLUMN &&
        initialState->combined_columns_[leftToe] == Foot_LeftToe &&
        !resultState->is_the_foot_holding_[Foot_LeftToe] &&
        ((initialState->did_the_foot_move_[Foot_LeftHeel] &&
          !initialState->is_the_foot_holding_[Foot_LeftHeel]) ||
         (initialState->did_the_foot_move_[Foot_LeftToe] &&
          !initialState->is_the_foot_holding_[Foot_LeftToe]))) {
      jackedLeft = true;
    }
  }
  return jackedLeft;
}

bool StepParityCost::didJackRight(
    const State* initialState, const State* resultState, int rightHeel,
    int rightToe, bool movedRight, bool didJump) {
  bool jackedRight = false;
  if (!didJump && movedRight) {
    if (rightHeel > INVALID_COLUMN &&
        initialState->combined_columns_[rightHeel] == Foot_RightHeel &&
        !resultState->is_the_foot_holding_[Foot_RightHeel] &&
        ((initialState->did_the_foot_move_[Foot_RightHeel] &&
          !initialState->is_the_foot_holding_[Foot_RightHeel]) ||
         (initialState->did_the_foot_move_[Foot_RightToe] &&
          !initialState->is_the_foot_holding_[Foot_RightToe]))) {
      jackedRight = true;
    }
    if (rightToe > INVALID_COLUMN &&
        initialState->combined_columns_[rightToe] == Foot_RightToe &&
        !resultState->is_the_foot_holding_[Foot_RightToe] &&
        ((initialState->did_the_foot_move_[Foot_RightHeel] &&
          !initialState->is_the_foot_holding_[Foot_RightHeel]) ||
         (initialState->did_the_foot_move_[Foot_RightToe] &&
          !initialState->is_the_foot_holding_[Foot_RightToe]))) {
      jackedRight = true;
    }
  }
  return jackedRight;
}
