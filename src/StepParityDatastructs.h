#ifndef STEP_PARITY_DATASTRUCTS_H
#define STEP_PARITY_DATASTRUCTS_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "GameConstantsAndTypes.h"
#include "NoteTypes.h"

namespace StepParity {

const int INVALID_COLUMN = -1;
const float CLM_SECOND_INVALID = -1;
// Currently we only support 4 and 8 panel.
const int MAX_COLUMNS = 8;

enum Foot {
  Foot_None = 0,
  Foot_LeftHeel,
  Foot_LeftToe,
  Foot_RightHeel,
  Foot_RightToe,
  NUM_Foot,
  Foot_Invalid
};

/// @brief A vector of Foot values, which represents the player's
/// foot placement on the dance stage.
typedef std::vector<Foot> FootPlacement;

const std::vector<uint16_t> FOOT_MASKS = {0, 1, 2, 4, 8};

const std::array<StepParity::Foot, 4> FEET = {
    Foot_LeftHeel, Foot_LeftToe, Foot_RightHeel, Foot_RightToe};
// A map for getting the other part of the foot, when you don't actually care
// what part it is.
// OTHER_PART_OF_FOOT[LEFT_HEEL] == LEFT_TOE
const std::array<StepParity::Foot, 5> OTHER_PART_OF_FOOT = {
    Foot_None, Foot_LeftToe, Foot_LeftHeel, Foot_RightToe, Foot_RightHeel};

struct StagePoint {
  float x_;
  float y_;
};

// StageLayout represents the relative position of each panel on the dance
// stage, and provides some basic math function
struct StageLayout {
  static const int kMaxTable = MAX_COLUMNS * MAX_COLUMNS;

  StepsType type_;
  int column_count_;
  std::array<StagePoint, MAX_COLUMNS> columns_ = {};
  std::vector<int> up_arrows_;
  std::vector<int> down_arrows_;
  std::vector<int> side_arrows_;

  // Lookup tables indexed by (leftColumn * columnCount + rightColumn).
  std::array<StagePoint, kMaxTable> avg_points_ = {};
  std::array<float, kMaxTable> distances_ = {};
  std::array<float, kMaxTable> facing_x_penalties_ = {};
  std::array<float, kMaxTable> facing_y_penalties_ = {};
  std::unordered_map<int, std::vector<FootPlacement>> permute_cache_;

  StageLayout(
      StepsType t, const std::vector<StagePoint>& c, const std::vector<int>& u,
      const std::vector<int>& d, const std::vector<int>& s)
      : type_(t), up_arrows_(u), down_arrows_(d), side_arrows_(s) {
    this->column_count_ = static_cast<int>(c.size());
    for (int i = 0; i < this->column_count_; i++) {
      this->columns_[i] = c[i];
    }
    this->preCalculateStuff();
    this->preGeneratePermutations();
  }

  // These are small geometry accessors called from the per-(node *
  // permutation * row) cost loop, so they're defined inline here to let the
  // cost calculator inline them (LTO is off, so a separate TU wouldn't).
  bool bracketCheck(int column1, int column2) const {
    float dist = getDistance(column1, column2);
    return (dist * dist) <= 2;
  }
  float getDistanceSq(int c1, int c2) const {
    return getDistanceSq(columns_[c1], columns_[c2]);
  }
  float getDistanceSq(StagePoint p1, StagePoint p2) const {
    return (p1.y_ - p2.y_) * (p1.y_ - p2.y_) +
           (p1.x_ - p2.x_) * (p1.x_ - p2.x_);
  }
  float getDistance(int left_index, int right_index) const {
    if (left_index == INVALID_COLUMN || right_index == INVALID_COLUMN) {
      return 0;
    }
    return distances_[left_index * column_count_ + right_index];
  }
  float getXFacingPenalty(int left_index, int right_index) const {
    if (left_index == INVALID_COLUMN || right_index == INVALID_COLUMN) {
      return 0;
    }
    return facing_x_penalties_[left_index * column_count_ + right_index];
  }
  float getYFacingPenalty(int left_index, int right_index) const {
    if (left_index == INVALID_COLUMN || right_index == INVALID_COLUMN) {
      return 0;
    }
    return facing_y_penalties_[left_index * column_count_ + right_index];
  }
  float getXDifference(int left_index, int right_index) const {
    if (left_index == right_index) {
      return 0;
    }
    float dx = columns_[right_index].x_ - columns_[left_index].x_;
    float dy = columns_[right_index].y_ - columns_[left_index].y_;
    float distance = std::sqrt(dx * dx + dy * dy);
    dx /= distance;
    bool negative = dx <= 0;
    dx = std::pow(dx, 4);
    return negative ? -dx : dx;
  }
  float getYDifference(int left_index, int right_index) const {
    if (left_index == right_index) {
      return 0;
    }
    float dx = columns_[right_index].x_ - columns_[left_index].x_;
    float dy = columns_[right_index].y_ - columns_[left_index].y_;
    float distance = std::sqrt(dx * dx + dy * dy);
    dy /= distance;
    bool negative = dy <= 0;
    dy = std::pow(dy, 4);
    return negative ? -dy : dy;
  }
  StagePoint averagePoint(int left_index, int right_index) const {
    if (left_index == INVALID_COLUMN && right_index == INVALID_COLUMN) {
      return {0, 0};
    }
    if (left_index == INVALID_COLUMN) {
      return columns_[right_index];
    }
    if (right_index == INVALID_COLUMN) {
      return columns_[left_index];
    }
    return avg_points_[left_index * column_count_ + right_index];
  }

  void preCalculateStuff();
  void preGeneratePermutations();
  std::vector<StepParity::FootPlacement> PermuteFootPlacements(
      unsigned int, StepParity::FootPlacement columns, unsigned long column);
};

/// @brief Represents a specific possible state of the player's position
/// for a given row of the step chart.
struct State {
  StepParity::Foot combined_columns_[MAX_COLUMNS] =
      {};  // The resulting position of the player

  // masks that indicate which foot moved/is holding.
  uint16_t moved_mask_ = 0;
  uint16_t holding_mask_ = 0;
  // mask equivalent of combinedColumns, 3 bits for each column
  uint32_t combined_mask_ = 0;

  int where_the_feet_are_[NUM_Foot] = {};  // the inverse of combinedColumns
  int what_note_the_foot_is_hitting_[NUM_Foot] = {};
  bool did_the_foot_move_[NUM_Foot] = {};
  bool is_the_foot_holding_[NUM_Foot] = {};
};

/// @brief A convenience struct used to encapsulate data from NoteData in an
/// easier to work with format.
struct IntermediateNoteData {
  TapNoteType type_ = TapNoteType_Empty;  // type of the note
  float beat_ = 0;                        // beat on which the note occurs
  float hold_length_ =
      0;  // If type is TapNoteType_HoldHead, length of hold, in beats

  bool fake_ = false;  // Is this note fake (besides being TapNoteType_Fake)?
  float second_ = 0;   // time into the song on which the note occurs
};

/// @brief A slightly complicated structure to encapsulate all of the data for a
/// given row of a step chart. 'notes' and 'holds' will always have
/// 'columnCount' entries. "Empty" columns will have a type of
/// TapNoteType_Empty. This shouldn't be confused with the idea of "rows"
/// elsewhere in SM. Here, we only use these Rows to represent a row that isn't
/// empty.
struct Row {
  // notes for the given row
  std::array<IntermediateNoteData, MAX_COLUMNS> notes_ = {};
  // Any active hold notes, including ones that started before this row
  std::array<IntermediateNoteData, MAX_COLUMNS> holds_ = {};

  // If a mine occurred either on this row, or on a row on its own immediately
  // preceding this one, the time of when that mine occurred, indexed by column.
  std::array<float, MAX_COLUMNS> mines_ = {};
  // The same thing, but for fake mines
  std::array<float, MAX_COLUMNS> fake_mines_ = {};

  std::array<Foot, MAX_COLUMNS> columns_ = {};
  std::array<int, NUM_Foot> where_the_feet_are_;

  // bit masks for quick comparisons
  uint16_t note_mask_ = 0;
  uint16_t hold_mask_ = 0;
  uint16_t mine_mask_ = 0;
  uint16_t fake_mine_mask_ = 0;

  float second_ = 0;
  float beat_ = 0;
  int row_index_ = 0;
  int column_count_ = 0;
  int note_count_ = 0;

  Row(int column_count) {
    column_count_ = column_count;
    where_the_feet_are_.fill(INVALID_COLUMN);
  }

  void setFootPlacement(const StepParity::State* state);
};

/// @brief A counter used while creating rows
struct RowCounter {
  // Notes for the "current" row being generated
  std::vector<IntermediateNoteData> notes_;
  // Any holds that are active for the current row
  std::vector<IntermediateNoteData> active_holds_;
  float last_column_second_ = CLM_SECOND_INVALID;
  float last_column_beat_ = CLM_SECOND_INVALID;

  // The time at which a mine occurred for the current row,
  // indexed by column
  std::vector<float> mines_;
  // The time at which a fake mine occurred for the current row,
  // indexed by column
  std::vector<float> fake_mines_;

  // The time at which a mine occurred in the _previous_ row,
  // indexed by column
  std::vector<float> next_mines_;
  // The time at which a fake mine occurred in the _previous_ row,
  // indexed by column
  std::vector<float> next_fake_mines_;
  // number of "notes" added to the counter for the current row.
  int note_count_ = 0;
  RowCounter(int column_count) {
    notes_ = std::vector<IntermediateNoteData>(column_count);
    active_holds_ = std::vector<IntermediateNoteData>(column_count);
    mines_ = std::vector<float>(column_count, 0);
    fake_mines_ = std::vector<float>(column_count, 0);
    next_mines_ = std::vector<float>(column_count, 0);
    next_fake_mines_ = std::vector<float>(column_count, 0);

    last_column_second_ = CLM_SECOND_INVALID;
    last_column_beat_ = CLM_SECOND_INVALID;
  }
};

/// @brief A node within a StepParityGraph.
/// Represents a given state, and its connections to the states in the
/// following row of the step chart.
struct StepParityNode {
  // The index of this node in its graph
  int id_ = 0;
  State* state_;
  int row_index_ = 0;
  float second_ = 0;

  float total_cost_ = 0;
  StepParityNode* previous_node_ = 0;

  StepParityNode(State* state, float second, int row_index) {
    state_ = state;
    row_index_ = row_index;
    second_ = second;
  }
};
};  // namespace StepParity

#endif
