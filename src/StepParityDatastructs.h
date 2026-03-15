#ifndef STEP_PARITY_DATASTRUCTS_H
#define STEP_PARITY_DATASTRUCTS_H

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "GameConstantsAndTypes.h"
#include "NoteTypes.h"

namespace StepParity {

const int INVALID_COLUMN = -1;
const float CLM_SECOND_INVALID = -1;
const int MAX_COLUMNS = 16;

enum Foot { NONE = 0, LEFT_HEEL, LEFT_TOE, RIGHT_HEEL, RIGHT_TOE, NUM_Foot };

/// @brief A vector of Foot values, which represents the player's
/// foot placement on the dance stage.
typedef std::vector<Foot> FootPlacement;

const std::vector<uint16_t> FOOT_MASKS = {0, 1, 2, 4, 8};

const int16_t FOOT_MASK_LEFT =
    FOOT_MASKS[Foot::LEFT_HEEL] | FOOT_MASKS[Foot::LEFT_TOE];
const int16_t FOOT_MASK_RIGHT =
    FOOT_MASKS[Foot::RIGHT_HEEL] | FOOT_MASKS[Foot::RIGHT_TOE];

const std::array<StepParity::Foot, 4> FEET = {
    LEFT_HEEL, LEFT_TOE, RIGHT_HEEL, RIGHT_TOE};
// A map for getting the other part of the foot, when you don't actually care
// what part it is.
// OTHER_PART_OF_FOOT[LEFT_HEEL] == LEFT_TOE
const std::array<StepParity::Foot, 5> OTHER_PART_OF_FOOT = {
    NONE, LEFT_TOE, LEFT_HEEL, RIGHT_TOE, RIGHT_HEEL};

const std::string FEET_LABELS[] = {"N", "L", "l", "R", "r", "5??", "6??"};
const std::string TapNoteTypeShortNames[] = {
    "Empty", "Tap", "Mine", "Attack", "AutoKeySound", "Fake", "", ""};
const std::string TapNoteSubTypeShortNames[] = {"Hold", "Roll", "", ""};

enum Cost {
  COST_DOUBLESTEP = 0,
  COST_BRACKETJACK,
  COST_JACK,
  COST_JUMP,
  COST_SLOW_BRACKET,
  COST_TWISTED_FOOT,
  COST_BRACKETTAP,
  COST_HOLDSWITCH,
  COST_MINE,
  COST_FOOTSWITCH,
  COST_MISSED_FOOTSWITCH,
  COST_FACING,
  COST_DISTANCE,
  COST_SPIN,
  COST_SIDESWITCH,
  COST_CROWDED_BRACKET,
  COST_OTHER,
  COST_TOTAL,
  NUM_Cost
};
const std::string COST_LABELS[] = {
    "DOUBLESTEP",        "BRACKETJACK", "JACK",       "JUMP", "SLOW_BRACKET",
    "TWISTED_FOOT",      "BRACKETTAP",  "HOLDSWITCH", "MINE", "FOOTSWITCH",
    "MISSED_FOOTSWITCH", "FACING",      "DISTANCE",   "SPIN", "SIDESWITCH",
    "CROWDED_BRACKET",   "OTHER",       "TOTAL"};

struct StagePoint {
  float x;
  float y;
};

// StageLayout represents the relative position of each panel on the dance
// stage, and provides some basic math function
struct StageLayout {
  StepsType type;
  int columnCount;
  std::vector<StagePoint> columns;
  std::vector<int> upArrows;
  std::vector<int> downArrows;
  std::vector<int> sideArrows;

  std::vector<StagePoint> avgPoints;
  std::vector<float> distances;
  std::vector<float> facingXPenalties;
  std::vector<float> facingYPenalties;
  std::unordered_map<int, std::vector<FootPlacement>> permuteCache;

  StageLayout(
      StepsType t, const std::vector<StagePoint>& c, const std::vector<int>& u,
      const std::vector<int>& d, const std::vector<int>& s)
      : type(t), columns(c), upArrows(u), downArrows(d), sideArrows(s) {
    this->columnCount = static_cast<int>(this->columns.size());
    this->preCalculateStuff();
    this->preGeneratePermutations();
  }

  bool bracketCheck(int column1, int column2) const;
  bool isSideArrow(int column) const;
  bool isUpArrow(int column) const;
  bool isDownArrow(int column) const;
  float getDistanceSq(int c1, int c2) const;
  float getDistanceSq(StagePoint p1, StagePoint p2) const;
  float getDistance(int leftIndex, int rightIndex) const;
  float getXFacingPenalty(int leftIndex, int rightIndex) const;
  float getYFacingPenalty(int leftIndex, int rightIndex) const;
  float getXDifference(int leftIndex, int rightIndex) const;
  float getYDifference(int leftIndex, int rightIndex) const;
  StagePoint averagePoint(int leftIndex, int rightIndex) const;
  float getPlayerAngle(int c1, int c2) const;
  float getPlayerAngle(
      StepParity::StagePoint left, StepParity::StagePoint right) const;

  void preCalculateStuff();
  void preGeneratePermutations();
  std::vector<StepParity::FootPlacement> PermuteFootPlacements(
      unsigned int, StepParity::FootPlacement columns, unsigned long column);
};

/// @brief Represents a specific possible state of the player's position
/// for a given row of the step chart.
struct State {
  StepParity::Foot combinedColumns[MAX_COLUMNS] =
      {};  // The resulting position of the player

  // masks that indicate which foot moved/is holding.
  uint16_t moved_mask = 0;
  uint16_t holding_mask = 0;
  // mask equivalent of combinedColumns, 3 bits for each column
  uint32_t combined_mask = 0;

  int whereTheFeetAre[NUM_Foot] = {};  // the inverse of combinedColumns
  int whatNoteTheFootIsHitting[NUM_Foot] = {};
  bool didTheFootMove[NUM_Foot] = {};
  bool isTheFootHolding[NUM_Foot] = {};

  bool operator==(const State& other) const;
};

/// @brief A convenience struct used to encapsulate data from NoteData in an
/// easier to work with format.
struct IntermediateNoteData {
  TapNoteType type = TapNoteType_Empty;  // type of the note
  TapNoteSubType subtype = TapNoteSubType_Invalid;
  int col = 0;     // column/track number
  int row = 0;     // row on which the note occurs
  float beat = 0;  // beat on which the note occurs
  float hold_length =
      0;  // If type is TapNoteType_HoldHead, length of hold, in beats

  bool warped = false;  // Is this note warped?
  bool fake = false;    // Is this note fake (besides being TapNoteType_Fake)?
  float second = 0;     // time into the song on which the note occurs

  Foot parity =
      NONE;  // Which foot (and which part of the foot) will most likely be used
};

/// @brief A slightly complicated structure to encapsulate all of the data for a
/// given row of a step chart. 'notes' and 'holds' will always have
/// 'columnCount' entries. "Empty" columns will have a type of
/// TapNoteType_Empty. This shouldn't be confused with the idea of "rows"
/// elsewhere in SM. Here, we only use these Rows to represent a row that isn't
/// empty.
struct Row {
  // notes for the given row
  std::vector<IntermediateNoteData> notes;
  // Any active hold notes, including ones that started before this row
  std::vector<IntermediateNoteData> holds;

  // If a mine occurred either on this row, or on a row on its own immediately
  // preceding this one, the time of when that mine occurred, indexed by column.
  std::vector<float> mines;
  // The same thing, but for fake mines
  std::vector<float> fakeMines;

  FootPlacement columns;
  std::vector<int> whereTheFeetAre;

  // bit masks for quick comparisons
  uint16_t note_mask = 0;
  uint16_t hold_mask = 0;
  uint16_t mine_mask = 0;
  uint16_t fake_mine_mask = 0;

  float second = 0;
  float beat = 0;
  int rowIndex = 0;
  int columnCount = 0;
  int noteCount = 0;
  Row() : Row(0) {};

  Row(int _columnCount) {
    columnCount = _columnCount;
    notes = std::vector<IntermediateNoteData>(columnCount);
    holds = std::vector<IntermediateNoteData>(columnCount);
    mines = std::vector<float>(columnCount, 0);
    fakeMines = std::vector<float>(columnCount, 0);
    columns = std::vector<StepParity::Foot>(columnCount, StepParity::NONE);
    whereTheFeetAre = std::vector<int>(StepParity::NUM_Foot, INVALID_COLUMN);
  }

  void setFootPlacement(const StepParity::State* state);

  bool operator==(const Row& other) const;
  bool operator!=(const Row& other) const;
};

/// @brief A counter used while creating rows
struct RowCounter {
  // Notes for the "current" row being generated
  std::vector<IntermediateNoteData> notes;
  // Any holds that are active for the current row
  std::vector<IntermediateNoteData> activeHolds;
  float lastColumnSecond = CLM_SECOND_INVALID;
  float lastColumnBeat = CLM_SECOND_INVALID;

  // The time at which a mine occurred for the current row,
  // indexed by column
  std::vector<float> mines;
  // The time at which a fake mine occurred for the current row,
  // indexed by column
  std::vector<float> fakeMines;

  // The time at which a mine occurred in the _previous_ row,
  // indexed by column
  std::vector<float> nextMines;
  // The time at which a fake mine occurred in the _previous_ row,
  // indexed by column
  std::vector<float> nextFakeMines;
  // number of "notes" added to the counter for the current row.
  int noteCount = 0;
  RowCounter(int columnCount) {
    notes = std::vector<IntermediateNoteData>(columnCount);
    activeHolds = std::vector<IntermediateNoteData>(columnCount);
    mines = std::vector<float>(columnCount, 0);
    fakeMines = std::vector<float>(columnCount, 0);
    nextMines = std::vector<float>(columnCount, 0);
    nextFakeMines = std::vector<float>(columnCount, 0);

    lastColumnSecond = CLM_SECOND_INVALID;
    lastColumnBeat = CLM_SECOND_INVALID;
  }
};

/// @brief A node within a StepParityGraph.
/// Represents a given state, and its connections to the states in the
/// following row of the step chart.
struct StepParityNode {
  // The index of this node in its graph
  int id = 0;
  State* state;
  int rowIndex = 0;
  float second = 0;

  float totalCost = 0;
  StepParityNode* previousNode = 0;

  StepParityNode(State* _state, float _second, int _rowIndex) {
    state = _state;
    rowIndex = _rowIndex;
    second = _second;
  }
};
};  // namespace StepParity

#endif
