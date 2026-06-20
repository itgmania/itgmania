#ifndef STEP_PARITY_COST_H
#define STEP_PARITY_COST_H

#include "StepParityDatastructs.h"

namespace StepParity {
const int DOUBLESTEP = 850;
const int BRACKETJACK = 20;
const int JACK = 30;
const int JUMP = 0;
const int SLOW_BRACKET = 300;
const int TWISTED_FOOT = 100000;
const int BRACKETTAP = 400;
const int HOLDSWITCH = 55;
const int MINE = 10000;
const int FOOTSWITCH = 325;
const int MISSED_FOOTSWITCH = 500;
const int FACING = 2;
const int DISTANCE = 6;
const int SPIN = 1000;
const int SIDESWITCH = 130;
const int CROWDED_BRACKET = 0;
const int OTHER = 0;

// 0.1 = 1/16th note at 150bpm. Jacks quicker than this are harder.
// Above this speed, footswitches should be prioritized
constexpr float JACK_THRESHOLD = 0.1f;
// 0.15 = 1/8th at 200bpm, or 3/16th at 150bpm. Below this speed, jumps should
// be prioritized
constexpr float SLOW_BRACKET_THRESHOLD = 0.15f;
// 0.2 = 1/8th at 150bpm. Footswitches slower than this are harder.
// Below this speed, jacks should be prioritized
constexpr float SLOW_FOOTSWITCH_THRESHOLD = 0.2f;
// 0.4 = 1/4th at 150bpm. Once a footswitch gets slow enough, though,
// just ignore it.
constexpr float SLOW_FOOTSWITCH_IGNORE = 0.4f;

class StepParityCost {
 private:
  const StageLayout* layout;

 public:
  StepParityCost(const StageLayout* _layout) : layout(_layout) {}

  /// @brief Computes and returns a cost value for the player moving from
  /// initialState to resultState.
  /// @param initialState The starting position of the player
  /// @param resultState The end position of the player
  /// @param row The row represented by resultState
  /// @param previousRow The row preceding row, or nullptr if there is none
  /// @return The computed cost
  float getActionCost(
      const State* initialState, const State* resultState, const Row& row,
      const Row* previousRow, const FootPlacement& columns, float elapsedTime);

 private:
  float calcMineCost(const State* resultState, const Row& row, int columnCount);
  float calcHoldSwitchCost(
      const State* initialState, const State* resultState, const Row& row,
      int columnCount);
  float calcBracketTapCost(
      const State* initialState, const Row& row, int leftHeel, int leftToe,
      int rightHeel, int rightToe, float elapsedTime);
  float calcBracketJackCost(
      const State* resultState, bool movedLeft, bool movedRight,
      bool jackedLeft, bool jackedRight, bool didJump);
  float calcDoublestepCost(
      const State* initialState, const State* resultState, const Row& row,
      const Row* previousRow, bool movedLeft, bool movedRight, bool jackedLeft,
      bool jackedRight, bool didJump);
  float calcSlowBracketCost(
      const Row& row, bool movedLeft, bool movedRight, float elapsedTime);
  float calcTwistedFootCost(const State* resultState);
  float calcMissedFootswitchCost(
      const Row& row, bool jackedLeft, bool jackedRight);
  float calcFacingCosts(const State* resultState);
  float calcSpinCosts(const State* initialState, const State* resultState);
  float calcFootswitchCost(
      const State* initialState, const FootPlacement& columns, const Row& row,
      float elapsedTime, int columnCount);
  float calcSideswitchCost(
      const State* initialState, const State* resultState,
      const FootPlacement& columns);
  float calcJackCost(
      bool movedLeft, bool movedRight, bool jackedLeft, bool jackedRight,
      float elapsedTime);
  float calcBigMovementsQuicklyCost(
      const State* initialState, const State* resultState, float elapsedTime);

  bool didDoubleStep(
      const State* initialState, const Row& row, const Row* previousRow,
      bool movedLeft, bool jackedLeft, bool movedRight, bool jackedRight);
  bool didJackLeft(
      const State* initialState, const State* resultState, int leftHeel,
      int leftToe, bool movedLeft, bool didJump);
  bool didJackRight(
      const State* initialState, const State* resultState, int rightHeel,
      int rightToe, bool movedRight, bool didJump);
};
};  // namespace StepParity

#endif
