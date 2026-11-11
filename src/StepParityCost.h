#ifndef STEP_PARITY_COST_H
#define STEP_PARITY_COST_H

#include "StepParityDatastructs.h"

namespace StepParity {
const int kDoublestep = 850;
const int kBracketjack = 20;
const int kJack = 30;
const int kJump = 0;
const int kSlowBracket = 300;
const int kTwistedFoot = 100000;
const int kBrackettap = 400;
const int kHoldswitch = 55;
const int kMine = 10000;
const int kFootswitch = 325;
const int kMissedFootswitch = 500;
const int kFacing = 2;
const int kDistance = 6;
const int kSpin = 1000;
const int kSideswitch = 130;
const int kCrowdedBracket = 0;
const int kOther = 0;

// 0.1 = 1/16th note at 150bpm. Jacks quicker than this are harder.
// Above this speed, footswitches should be prioritized
constexpr float kJackThreshold = 0.1f;
// 0.15 = 1/8th at 200bpm, or 3/16th at 150bpm. Below this speed, jumps should
// be prioritized
constexpr float kSlowBracketThreshold = 0.15f;
// 0.2 = 1/8th at 150bpm. Footswitches slower than this are harder.
// Below this speed, jacks should be prioritized
constexpr float kSlowFootswitchThreshold = 0.2f;
// 0.4 = 1/4th at 150bpm. Once a footswitch gets slow enough, though,
// just ignore it.
constexpr float kSlowFootswitchIgnore = 0.4f;

class StepParityCost {
 private:
  const StageLayout* layout_;

 public:
  StepParityCost(const StageLayout* layout) : layout_(layout) {}

  /// @brief Computes and returns a cost value for the player moving from
  /// initialState to resultState.
  /// @param initialState The starting position of the player
  /// @param resultState The end position of the player
  /// @param row The row represented by resultState
  /// @param previousRow The row preceding row, or nullptr if there is none
  /// @return The computed cost
  float getActionCost(
      const State* initial_state, const State* result_state, const Row& row,
      const Row* previous_row, const FootPlacement& columns,
      float elapsed_time);

 private:
  float calcMineCost(
      const State* result_state, const Row& row, int column_count);
  float calcHoldSwitchCost(
      const State* initial_state, const State* result_state, const Row& row,
      int column_count);
  float calcBracketTapCost(
      const State* initial_state, const Row& row, int left_heel, int left_toe,
      int right_heel, int right_toe, float elapsed_time);
  float calcBracketJackCost(
      const State* result_state, bool moved_left, bool moved_right,
      bool jacked_left, bool jacked_right, bool did_jump);
  float calcDoublestepCost(
      const State* initial_state, const State* result_state, const Row& row,
      const Row* previous_row, bool moved_left, bool moved_right,
      bool jacked_left, bool jacked_right, bool did_jump);
  float calcSlowBracketCost(
      const Row& row, bool moved_left, bool moved_right, float elapsed_time);
  float calcTwistedFootCost(const State* result_state);
  float calcMissedFootswitchCost(
      const Row& row, bool jacked_left, bool jacked_right);
  float calcFacingCosts(const State* result_state);
  float calcSpinCosts(const State* initial_state, const State* result_state);
  float calcFootswitchCost(
      const State* initial_state, const FootPlacement& columns, const Row& row,
      float elapsed_time, int column_count);
  float calcSideswitchCost(
      const State* initial_state, const State* result_state,
      const FootPlacement& columns);
  float calcJackCost(
      bool moved_left, bool moved_right, bool jacked_left, bool jacked_right,
      float elapsed_time);
  float calcBigMovementsQuicklyCost(
      const State* initial_state, const State* result_state,
      float elapsed_time);

  bool didDoubleStep(
      const State* initial_state, const Row& row, const Row* previous_row,
      bool moved_left, bool jacked_left, bool moved_right, bool jacked_right);
  bool didJackLeft(
      const State* initial_state, const State* result_state, int left_heel,
      int left_toe, bool moved_left, bool did_jump);
  bool didJackRight(
      const State* initial_state, const State* result_state, int right_heel,
      int right_toe, bool moved_right, bool did_jump);
};
};  // namespace StepParity

#endif
