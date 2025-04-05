#ifndef STEP_PARITY_DATASTRUCTS_H
#define STEP_PARITY_DATASTRUCTS_H

#include "GameConstantsAndTypes.h"
#include "NoteData.h"
#include <queue>
#include <unordered_map>

namespace StepParity {

	const int INVALID_COLUMN = -1;
	const float CLM_SECOND_INVALID = -1;

	enum Foot
	{
		NONE = 0,
		LEFT_HEEL,
		LEFT_TOE,
		RIGHT_HEEL,
		RIGHT_TOE,
		NUM_Foot
	};

	const std::vector<StepParity::Foot> FEET = {LEFT_HEEL, LEFT_TOE, RIGHT_HEEL, RIGHT_TOE};
	// A map for getting the other part of the foot, when you don't actually care
	// what part it is.
	// OTHER_PART_OF_FOOT[LEFT_HEEL] == LEFT_TOE
	const std::vector<StepParity::Foot> OTHER_PART_OF_FOOT = {NONE, LEFT_TOE, LEFT_HEEL, RIGHT_TOE, RIGHT_HEEL};
	
	const RString FEET_LABELS[] = {"N", "L", "l", "R", "r", "5??", "6??"};
	const RString TapNoteTypeShortNames[] = { "Empty", "Tap",  "Mine",  "Attack", "AutoKeySound", "Fake", "", "" };
	const RString TapNoteSubTypeShortNames[] = { "Hold", "Roll", "", "" };	

	enum Cost
	{
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
		COST_CROWDED_BRACKET ,
		COST_OTHER,
		COST_TOTAL,
		NUM_Cost
	};
	const RString COST_LABELS[] = {
		"DOUBLESTEP",
		"BRACKETJACK",
		"JACK",
		"JUMP",
		"SLOW_BRACKET",
		"TWISTED_FOOT",
		"BRACKETTAP",
		"HOLDSWITCH",
		"MINE",
		"FOOTSWITCH",
		"MISSED_FOOTSWITCH",
		"FACING",
		"DISTANCE",
		"SPIN",
		"SIDESWITCH",
		"CROWDED_BRACKET",
		"OTHER",
		"TOTAL"
	};
    
	struct StagePoint {
		float x;
		float y;
	};
	
	// StageLayout represents the relative position of each panel on the dance stage,
	// and provides some basic math function
	struct StageLayout {
		StepsType type;
		int columnCount;
		std::vector<StagePoint> columns;
		std::vector<int> upArrows;
		std::vector<int> downArrows;
		std::vector<int> sideArrows;
		
		StageLayout(StepsType t,
					 const std::vector<StagePoint>& c,
					 const std::vector<int> & u,
					 const std::vector<int> & d,
					 const std::vector<int> & s) : type(t), columns(c), upArrows(u), downArrows(d), sideArrows(s) {
			this->columnCount = static_cast<int>(this->columns.size());
		}
		
		
		bool bracketCheck(int column1, int column2);
		bool isSideArrow(int column);
		bool isUpArrow(int column);
		bool isDownArrow(int column);
		float getDistanceSq(int c1, int c2);
		float getDistanceSq(StagePoint p1, StagePoint p2);
		float getXDifference(int leftIndex, int rightIndex);
		float getYDifference(int leftIndex, int rightIndex);
		StagePoint averagePoint(int leftIndex, int rightIndex);
		float getPlayerAngle(int c1, int c2);
		float getPlayerAngle(StepParity::StagePoint left, StepParity::StagePoint right);
	};

	/// @brief A vector of Foot values, which represents the player's
	/// foot placement on the dance stage.
	typedef std::vector<Foot> FootPlacement;

	/// @brief Represents a specific possible state of the player's position
	/// for a given row of the step chart.
	struct State {
		FootPlacement columns;	 // what the feet are hitting on this row
		FootPlacement combinedColumns; // The resulting position of the player
		FootPlacement movedFeet; // Any feet that have moved from the previous state to this one
		FootPlacement holdFeet;  // Any feet that stayed in place due to a hold/roll note.

		int whereTheFeetAre[NUM_Foot]; // the inverse of combinedColumns
		int whatNoteTheFootIsHitting[NUM_Foot]; // the inverse of columns
		bool didTheFootMove[NUM_Foot]; // the inverse of movedFeet
		bool isTheFootHolding[NUM_Foot]; //inverse of holdFeet
		
		State(int columnCount)
		{
			columns = FootPlacement(columnCount, NONE);
			combinedColumns = FootPlacement(columnCount, NONE);
			movedFeet = FootPlacement(columnCount, NONE);
			holdFeet = FootPlacement(columnCount, NONE);
			
			for(int i = 0; i < NUM_Foot; i++)
			{
				whatNoteTheFootIsHitting[i] = INVALID_COLUMN;
				whereTheFeetAre[i] = INVALID_COLUMN;
				didTheFootMove[i] = false;
				isTheFootHolding[i] = false;
			}
		}
		
		
		bool operator==(const State& other) const;
	};

	/// @brief A convenience struct used to encapsulate data from NoteData in an 
	/// easier to work with format.
	struct IntermediateNoteData {
		TapNoteType type = TapNoteType_Empty; 	// type of the note
		TapNoteSubType subtype = TapNoteSubType_Invalid;
		int col = 0;			// column/track number
		int row = 0;			// row on which the note occurs
		float beat = 0;			// beat on which the note occurs
		float hold_length = 0;	// If type is TapNoteType_HoldTail, length of hold, in beats

		bool warped = false;		// Is this note warped?
		bool fake = false;			// Is this note fake (besides being TapNoteType_Fake)?
		float second = false;		// time into the song on which the note occurs

		Foot parity = NONE; 		// Which foot (and which part of the foot) will most likely be used
	};


	/// @brief A slightly complicated structure to encapsulate all of the data for a given 
	/// row of a step chart.
	/// 'notes' and 'holds' will always have 'columnCount' entries. "Empty" columns will have a type of TapNoteType_Empty.
	/// This shouldn't be confused with the idea of "rows" elsewhere in SM. Here, we only use
	/// these Rows to represent a row that isn't empty.
	struct Row {
		// notes for the given row
		std::vector<IntermediateNoteData> notes;
		// Any active hold notes, including ones that started before this row
		std::vector<IntermediateNoteData> holds;
		// Column index of any holds that end on this row
		std::set<int> holdTails;
		// If a mine occurred either on this row, or on a row on its own immediately
		// preceding this one, the time of when that mine occurred, indexed by column.
		std::vector<float> mines;
		// The same thing, but for fake mines
		std::vector<float> fakeMines;

		FootPlacement columns;
		std::vector<int> whereTheFeetAre;
		
		float second = 0;
		float beat = 0;
		int rowIndex = 0;
		int columnCount = 0;
		int noteCount = 0;
		Row()
		{
			Row(0);
		}
		
		Row(int _columnCount)
		{
			columnCount = _columnCount;
			notes = std::vector<IntermediateNoteData>(columnCount);
			holds = std::vector<IntermediateNoteData>(columnCount);
			holdTails.clear();
			mines = std::vector<float>(columnCount, 0);
			fakeMines = std::vector<float>(columnCount, 0);
			columns = std::vector<StepParity::Foot>(columnCount, StepParity::NONE);
			whereTheFeetAre = std::vector<int>(StepParity::NUM_Foot, INVALID_COLUMN);
		}
		
		void setFootPlacement(const std::vector<Foot> & footPlacement);
		
		bool operator==(const Row& other) const;
		bool operator!=(const Row& other) const;
	};

	/// @brief A counter used while creating rows
	struct RowCounter
	{
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
		RowCounter(int columnCount)
		{
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
	struct StepParityNode
	{
		// The index of this node in its graph
		int id = 0;
		State * state;
		int rowIndex = 0;
		float second = 0;
		
		// Connections to, and the cost of moving to, the connected nodes
		std::unordered_map<StepParityNode *, float> neighbors;
		
		~StepParityNode()
		{
			neighbors.clear();
		}
		StepParityNode(State *_state, float _second, int _rowIndex)
		{
			state = _state;
			rowIndex = _rowIndex;
			second = _second;
		}
		
		int neighborCount()
		{
			return static_cast<int>(neighbors.size());
		}
	};
};

#endif
