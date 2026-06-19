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
    State* initialState, State* resultState, std::vector<Row>& rows,
    const FootPlacement& columns, int rowIndex, float elapsedTime) {
  Row& row = rows[rowIndex];
  int columnCount = row.columnCount;

  float cost = 0;

  // Mine weighting
  int leftHeel = resultState->whatNoteTheFootIsHitting[Foot_LeftHeel];
  int leftToe = resultState->whatNoteTheFootIsHitting[Foot_LeftToe];
  int rightHeel = resultState->whatNoteTheFootIsHitting[Foot_RightHeel];
  int rightToe = resultState->whatNoteTheFootIsHitting[Foot_RightToe];

  bool movedLeft = resultState->didTheFootMove[Foot_LeftHeel] ||
                   resultState->didTheFootMove[Foot_LeftToe];

  bool movedRight = resultState->didTheFootMove[Foot_RightHeel] ||
                    resultState->didTheFootMove[Foot_RightToe];

  // Note that this is checking whether the previous state was a jump, not
  // whether the current state is
  bool didJump = ((initialState->didTheFootMove[Foot_LeftHeel] &&
                   !initialState->isTheFootHolding[Foot_LeftHeel]) ||
                  (initialState->didTheFootMove[Foot_LeftToe] &&
                   !initialState->isTheFootHolding[Foot_LeftToe])) &&
                 ((initialState->didTheFootMove[Foot_RightHeel] &&
                   !initialState->isTheFootHolding[Foot_RightHeel]) ||
                  (initialState->didTheFootMove[Foot_RightToe] &&
                   !initialState->isTheFootHolding[Foot_RightToe]));

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
      initialState, resultState, rows, rowIndex, movedLeft, movedRight,
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
    State* resultState, Row& row, int columnCount) {
  if (row.mine_mask == 0 && row.fake_mine_mask == 0) {
    return 0.0f;
  }
  float cost = 0;

  for (int i = 0; i < columnCount; i++) {
    if (resultState->combinedColumns[i] != Foot_None &&
        (row.mines[i] != 0 || row.fakeMines[i] != 0)) {
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
    State* initialState, State* resultState, Row& row, int columnCount) {
  if (row.hold_mask == 0) {
    return 0.0f;
  }

  float cost = 0;

  for (int c = 0; c < columnCount; c++) {
    if (row.holds[c].type == TapNoteType_Empty) {
      continue;
    }
    if (((resultState->combinedColumns[c] == Foot_LeftHeel ||
          resultState->combinedColumns[c] == Foot_LeftToe) &&
         initialState->combinedColumns[c] != Foot_LeftToe &&
         initialState->combinedColumns[c] != Foot_LeftHeel) ||
        ((resultState->combinedColumns[c] == Foot_RightHeel ||
          resultState->combinedColumns[c] == Foot_RightToe) &&
         initialState->combinedColumns[c] != Foot_RightToe &&
         initialState->combinedColumns[c] != Foot_RightHeel)) {
      int previousFoot =
          initialState->whereTheFeetAre[resultState->combinedColumns[c]];
      cost += HOLDSWITCH * (previousFoot == INVALID_COLUMN
                                ? 1
                                : sqrt(layout->getDistanceSq(c, previousFoot)));
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
    State* initialState, Row& row, int leftHeel, int leftToe, int rightHeel,
    int rightToe, float elapsedTime) {
  if (row.hold_mask == 0) {
    return 0.0f;
  }

  // Small penalty for trying to jack a bracket during a hold
  float cost = 0;
  if (leftHeel != INVALID_COLUMN && leftToe != INVALID_COLUMN) {
    float jackPenalty = 1;
    if (initialState->didTheFootMove[Foot_LeftHeel] ||
        initialState->didTheFootMove[Foot_LeftToe]) {
      jackPenalty = 1 / elapsedTime;
    }
    if (row.holds[leftHeel].type != TapNoteType_Empty &&
        row.holds[leftToe].type == TapNoteType_Empty) {
      cost += BRACKETTAP * jackPenalty;
    }
    if (row.holds[leftToe].type != TapNoteType_Empty &&
        row.holds[leftHeel].type == TapNoteType_Empty) {
      cost += BRACKETTAP * jackPenalty;
    }
  }

  if (rightHeel != INVALID_COLUMN && rightToe != INVALID_COLUMN) {
    float jackPenalty = 1;
    if (initialState->didTheFootMove[Foot_RightToe] ||
        initialState->didTheFootMove[Foot_RightHeel]) {
      jackPenalty = 1 / elapsedTime;
    }

    if (row.holds[rightHeel].type != TapNoteType_Empty &&
        row.holds[rightToe].type == TapNoteType_Empty) {
      cost += BRACKETTAP * jackPenalty;
    }
    if (row.holds[rightToe].type != TapNoteType_Empty &&
        row.holds[rightHeel].type == TapNoteType_Empty) {
      cost += BRACKETTAP * jackPenalty;
    }
  }
  return cost;
}

float StepParityCost::calcBracketJackCost(
    State* resultState, bool movedLeft, bool movedRight, bool jackedLeft,
    bool jackedRight, bool didJump) {
  if (movedLeft == movedRight || resultState->holding_mask != 0 || didJump) {
    return 0.0f;
  }
  float cost = 0;

  if (jackedLeft && resultState->didTheFootMove[Foot_LeftHeel] &&
      resultState->didTheFootMove[Foot_LeftToe]) {
    cost += BRACKETJACK;
  }

  if (jackedRight && resultState->didTheFootMove[Foot_RightHeel] &&
      resultState->didTheFootMove[Foot_RightToe]) {
    cost += BRACKETJACK;
  }
  return cost;
}

float StepParityCost::calcDoublestepCost(
    State* initialState, State* resultState, std::vector<Row>& rows,
    int rowIndex, bool movedLeft, bool movedRight, bool jackedLeft,
    bool jackedRight, bool didJump) {
  if ((movedLeft == movedRight) || resultState->holding_mask != 0 || didJump) {
    return 0.0f;
  }

  float cost = 0;
  bool doublestepped = didDoubleStep(
      initialState, rows, rowIndex, movedLeft, jackedLeft, movedRight,
      jackedRight);

  if (doublestepped) {
    cost += DOUBLESTEP;
  }
  return cost;
}

// Jumps should be prioritized over brackets below a certain speed
float StepParityCost::calcSlowBracketCost(
    Row& row, bool movedLeft, bool movedRight, float elapsedTime) {
  float cost = 0;
  if (elapsedTime > SLOW_BRACKET_THRESHOLD && movedLeft != movedRight &&
      std::count_if(
          row.notes.begin(), row.notes.end(),
          [](StepParity::IntermediateNoteData note) {
            return note.type != TapNoteType_Empty;
          }) >= 2) {
    float timediff = elapsedTime - SLOW_BRACKET_THRESHOLD;
    cost += timediff * SLOW_BRACKET;
  }
  return cost;
}

// Does this placement result in one of the feet being twisted around?
// This should probably be getting filtered out as an invalid positioning before
// we even get to calculating costs.
float StepParityCost::calcTwistedFootCost(State* resultState) {
  float cost = 0;
  int leftHeel = resultState->whatNoteTheFootIsHitting[Foot_LeftHeel];
  int leftToe = resultState->whatNoteTheFootIsHitting[Foot_LeftToe];
  int rightHeel = resultState->whatNoteTheFootIsHitting[Foot_RightHeel];
  int rightToe = resultState->whatNoteTheFootIsHitting[Foot_RightToe];

  StagePoint leftPos = layout->averagePoint(leftHeel, leftToe);
  StagePoint rightPos = layout->averagePoint(rightHeel, rightToe);

  bool crossedOver = rightPos.x < leftPos.x;
  bool rightBackwards =
      rightHeel != INVALID_COLUMN && rightToe != INVALID_COLUMN
          ? layout->columns[rightToe].y < layout->columns[rightHeel].y
          : false;
  bool leftBackwards =
      leftHeel != INVALID_COLUMN && leftToe != INVALID_COLUMN
          ? layout->columns[leftToe].y < layout->columns[leftHeel].y
          : false;

  if (!crossedOver && (rightBackwards || leftBackwards)) {
    cost += TWISTED_FOOT;
  }
  return cost;
}

float StepParityCost::calcMissedFootswitchCost(
    Row& row, bool jackedLeft, bool jackedRight) {
  float cost = 0;
  if ((jackedLeft || jackedRight) &&
      (row.mine_mask != 0 || row.fake_mine_mask != 0)) {
    cost += MISSED_FOOTSWITCH;
  }
  return cost;
}

float StepParityCost::calcFacingCosts(State* resultState) {
  int endLeftHeel = resultState->whereTheFeetAre[Foot_LeftHeel];
  int endLeftToe = resultState->whereTheFeetAre[Foot_LeftToe];
  int endRightHeel = resultState->whereTheFeetAre[Foot_RightHeel];
  int endRightToe = resultState->whereTheFeetAre[Foot_RightToe];

  if (endLeftToe == INVALID_COLUMN) {
    endLeftToe = endLeftHeel;
  }
  if (endRightToe == INVALID_COLUMN) {
    endRightToe = endRightHeel;
  }

  float heelFacingPenalty =
      layout->getXFacingPenalty(endLeftHeel, endRightHeel) * FACING;
  float toesFacingPenalty =
      layout->getXFacingPenalty(endLeftToe, endRightToe) * FACING;
  float leftFacingPenalty =
      layout->getYFacingPenalty(endLeftHeel, endLeftToe) * FACING;
  float rightFacingPenalty =
      layout->getYFacingPenalty(endRightHeel, endRightToe) * FACING;

  float cost = heelFacingPenalty + toesFacingPenalty + leftFacingPenalty +
               rightFacingPenalty;
  return cost;
}

float StepParityCost::calcSpinCosts(State* initialState, State* resultState) {
  float cost = 0;

  int endLeftHeel = resultState->whereTheFeetAre[Foot_LeftHeel];
  int endLeftToe = resultState->whereTheFeetAre[Foot_LeftToe];
  int endRightHeel = resultState->whereTheFeetAre[Foot_RightHeel];
  int endRightToe = resultState->whereTheFeetAre[Foot_RightToe];

  if (endLeftToe == INVALID_COLUMN) {
    endLeftToe = endLeftHeel;
  }
  if (endRightToe == INVALID_COLUMN) {
    endRightToe = endRightHeel;
  }

  // spin
  StagePoint previousLeftPos = layout->averagePoint(
      initialState->whereTheFeetAre[Foot_LeftHeel],
      initialState->whereTheFeetAre[Foot_LeftToe]);
  StagePoint previousRightPos = layout->averagePoint(
      initialState->whereTheFeetAre[Foot_RightHeel],
      initialState->whereTheFeetAre[Foot_RightToe]);
  StagePoint leftPos = layout->averagePoint(endLeftHeel, endLeftToe);
  StagePoint rightPos = layout->averagePoint(endRightHeel, endRightToe);

  if (rightPos.x < leftPos.x && previousRightPos.x < previousLeftPos.x &&
      rightPos.y < leftPos.y && previousRightPos.y > previousLeftPos.y) {
    cost += SPIN;
  }
  if (rightPos.x < leftPos.x && previousRightPos.x < previousLeftPos.x &&
      rightPos.y > leftPos.y && previousRightPos.y < previousLeftPos.y) {
    cost += SPIN;
  }
  return cost;
}

// Footswitches are harder to do when they get too slow.
// Notes with an elapsed time greater than this will incur a penalty
float StepParityCost::calcFootswitchCost(
    State* initialState, const FootPlacement& columns, Row& row,
    float elapsedTime, int columnCount) {
  if (elapsedTime < SLOW_FOOTSWITCH_THRESHOLD ||
      elapsedTime >= SLOW_FOOTSWITCH_IGNORE) {
    return 0.0f;
  }

  // footswitching has no penalty if there's a mine nearby
  if (row.mine_mask != 0 || row.fake_mine_mask != 0) {
    return 0.0f;
  }

  float cost = 0;
  float timeScaled = elapsedTime - SLOW_FOOTSWITCH_THRESHOLD;

  for (int i = 0; i < columnCount; i++) {
    if (initialState->combinedColumns[i] == Foot_None ||
        columns[i] == Foot_None) {
      continue;
    }

    if (initialState->combinedColumns[i] != columns[i] &&
        initialState->combinedColumns[i] != OTHER_PART_OF_FOOT[columns[i]]) {
      cost +=
          (timeScaled / (SLOW_FOOTSWITCH_THRESHOLD + timeScaled)) * FOOTSWITCH;
      break;
    }
  }
  return cost;
}

float StepParityCost::calcSideswitchCost(
    State* initialState, State* resultState, const FootPlacement& columns) {
  float cost = 0;
  for (auto c : layout->sideArrows) {
    if (initialState->combinedColumns[c] != columns[c] &&
        columns[c] != Foot_None &&
        initialState->combinedColumns[c] != Foot_None &&
        !resultState->didTheFootMove[initialState->combinedColumns[c]]) {
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
    State* initialState, State* resultState, float elapsedTime) {
  float cost = 0;
  for (StepParity::Foot foot : FEET) {
    if ((resultState->moved_mask & FOOT_MASKS[foot]) == 0) {
      continue;
    }

    int initialPosition = initialState->whereTheFeetAre[foot];
    if (initialPosition == INVALID_COLUMN) {
      continue;
    }

    int resultPosition = resultState->whatNoteTheFootIsHitting[foot];

    // If we're bracketing something, and the toes are now where the heel
    // was, then we don't need to worry about it, we're not actually moving
    // the foot very far
    bool isBracketing =
        resultState->whatNoteTheFootIsHitting[OTHER_PART_OF_FOOT[foot]] !=
        INVALID_COLUMN;
    if (isBracketing &&
        resultState->whatNoteTheFootIsHitting[OTHER_PART_OF_FOOT[foot]] ==
            initialPosition) {
      continue;
    }

    float dist =
        (layout->getDistance(initialPosition, resultPosition) * DISTANCE) /
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
    State* initialState, std::vector<Row>& rows, int rowIndex, bool movedLeft,
    bool jackedLeft, bool movedRight, bool jackedRight) {
  Row& row = rows[rowIndex];
  bool doublestepped = false;
  if (movedLeft && !jackedLeft &&
      ((initialState->didTheFootMove[Foot_LeftHeel] &&
        !initialState->isTheFootHolding[Foot_LeftHeel]) ||
       (initialState->didTheFootMove[Foot_LeftToe] &&
        !initialState->isTheFootHolding[Foot_LeftToe]))) {
    doublestepped = true;
  }
  if (movedRight && !jackedRight &&
      ((initialState->didTheFootMove[Foot_RightHeel] &&
        !initialState->isTheFootHolding[Foot_RightHeel]) ||
       (initialState->didTheFootMove[Foot_RightToe] &&
        !initialState->isTheFootHolding[Foot_RightToe]))) {
    doublestepped = true;
  }

  if (rowIndex - 1 > -1) {
    StepParity::Row& lastRow = rows[rowIndex - 1];
    for (StepParity::IntermediateNoteData hold : lastRow.holds) {
      if (hold.type == TapNoteType_Empty) {
        continue;
      }
      float endBeat = row.beat;
      float startBeat = lastRow.beat;
      // if a hold tail extends past the last row & ends in between, we can
      // doublestep
      if (hold.beat + hold.hold_length > startBeat &&
          hold.beat + hold.hold_length < endBeat) {
        doublestepped = false;
      }
      // if the hold tail extends past this row, we can doublestep
      if (hold.beat + hold.hold_length >= endBeat) {
        doublestepped = false;
      }
    }
  }
  return doublestepped;
}

bool StepParityCost::didJackLeft(
    State* initialState, State* resultState, int leftHeel, int leftToe,
    bool movedLeft, bool didJump) {
  bool jackedLeft = false;
  if (!didJump && movedLeft) {
    if (leftHeel > INVALID_COLUMN &&
        initialState->combinedColumns[leftHeel] == Foot_LeftHeel &&
        !resultState->isTheFootHolding[Foot_LeftHeel] &&
        ((initialState->didTheFootMove[Foot_LeftHeel] &&
          !initialState->isTheFootHolding[Foot_LeftHeel]) ||
         (initialState->didTheFootMove[Foot_LeftToe] &&
          !initialState->isTheFootHolding[Foot_LeftToe]))) {
      jackedLeft = true;
    }
    if (leftToe > INVALID_COLUMN &&
        initialState->combinedColumns[leftToe] == Foot_LeftToe &&
        !resultState->isTheFootHolding[Foot_LeftToe] &&
        ((initialState->didTheFootMove[Foot_LeftHeel] &&
          !initialState->isTheFootHolding[Foot_LeftHeel]) ||
         (initialState->didTheFootMove[Foot_LeftToe] &&
          !initialState->isTheFootHolding[Foot_LeftToe]))) {
      jackedLeft = true;
    }
  }
  return jackedLeft;
}

bool StepParityCost::didJackRight(
    State* initialState, State* resultState, int rightHeel, int rightToe,
    bool movedRight, bool didJump) {
  bool jackedRight = false;
  if (!didJump && movedRight) {
    if (rightHeel > INVALID_COLUMN &&
        initialState->combinedColumns[rightHeel] == Foot_RightHeel &&
        !resultState->isTheFootHolding[Foot_RightHeel] &&
        ((initialState->didTheFootMove[Foot_RightHeel] &&
          !initialState->isTheFootHolding[Foot_RightHeel]) ||
         (initialState->didTheFootMove[Foot_RightToe] &&
          !initialState->isTheFootHolding[Foot_RightToe]))) {
      jackedRight = true;
    }
    if (rightToe > INVALID_COLUMN &&
        initialState->combinedColumns[rightToe] == Foot_RightToe &&
        !resultState->isTheFootHolding[Foot_RightToe] &&
        ((initialState->didTheFootMove[Foot_RightHeel] &&
          !initialState->isTheFootHolding[Foot_RightHeel]) ||
         (initialState->didTheFootMove[Foot_RightToe] &&
          !initialState->isTheFootHolding[Foot_RightToe]))) {
      jackedRight = true;
    }
  }
  return jackedRight;
}
