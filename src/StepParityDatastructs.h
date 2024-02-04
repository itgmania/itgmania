#ifndef STEP_PARITY_DATASTRUCTS_H
#define STEP_PARITY_DATASTRUCTS_H

#include "GameConstantsAndTypes.h"
#include "NoteData.h"
#include "json/json.h"
#include "JsonUtil.h"
#include <queue>
#include <unordered_map>


namespace StepParity {

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
	const RString FEET_LABELS[] = {"N", "L", "l", "R", "r", "5??", "6??"};
	const RString TapNoteTypeShortNames[] = { "Empty", "Tap",  "Mine",  "Attack", "AutoKeySound", "Fake", "", "" };
	const RString TapNoteSubTypeShortNames[] = { "Hold", "Roll", "", "" };	

	struct StagePoint {
		float x;
		float y;
	};

	/// @brief A vector of StagePoints, which represents the 
	/// relative position of each arrow on the dance stage. 
	typedef std::vector<StagePoint> StageLayout;

	/// @brief A vector of Foot values, which represents the player's
	/// foot placement on the dance stage.
	typedef std::vector<Foot> FootPlacement;

	/// @brief Represents a specific possible state of the player's position 
	/// for a given row of the step chart.
	struct State {
		FootPlacement columns;	 // The position of the player
		FootPlacement movedFeet; // Any feet that have moved from the previous state to this one
		FootPlacement holdFeet;  // Any feet that stayed in place due to a hold/roll note.
		float second;			 // The time of the song represented by this state
		int rowIndex;			 // The index of the row represented by this state

		State()
		{
			State(4);
		}
		
		State(int columnCount)
		{
			columns = FootPlacement(columnCount, NONE);
			movedFeet = FootPlacement(columnCount, NONE);
			holdFeet = FootPlacement(columnCount, NONE);
			second = 0;
			rowIndex = 0;
		}

		Json::Value ToJson(bool useStrings);

		bool operator==(const State& other) const {
			return rowIndex == other.rowIndex &&
				   columns == other.columns &&
				   movedFeet == other.movedFeet &&
				   holdFeet == other.holdFeet;
		}

		bool operator<(const State& other) const {

			if(rowIndex != other.rowIndex) {
				return rowIndex < other.rowIndex;
			}
			if(columns != other.columns) {
				return std::lexicographical_compare(columns.begin(), columns.end(), other.columns.begin(), other.columns.end());
			}
			if(movedFeet != other.movedFeet) {
				return std::lexicographical_compare(movedFeet.begin(), movedFeet.end(), other.movedFeet.begin(), other.movedFeet.end());
			}
			if(holdFeet != other.holdFeet) {
				return std::lexicographical_compare(holdFeet.begin(), holdFeet.end(), other.holdFeet.begin(), other.holdFeet.end());
			}
			return false;
		}
	};

	/// @brief A convenience struct used to encapsulate data from NoteData in an 
	/// easier to work with format.
	struct IntermediateNoteData {
		TapNoteType type = TapNoteType_Empty; 	// type of the note
		TapNoteSubType subtype = TapNoteSubType_Invalid;
		int col;			// column/track number
		int row;			// row on which the note occurs
		float beat;			// beat on which the note occurs
		float hold_length;	// If type is TapNoteType_HoldTail, length of hold, in beats

		bool warped;		// Is this note warped?
		bool fake;			// Is this note fake (besides being TapNoteType_Fake)?
		float second;		// time into the song on which the note occurs

		Foot parity = NONE; 		// Which foot (and which part of the foot) will most likely be used
		Json::Value ToJson(bool useStrings);
	};


	/// @brief A slightly complicated structure to encapsulate all of the data for a given 
	/// row of a step chart.
	/// 'notes' and 'holds' will always have 'columnCount' entries. "Empty" columns will have a type of TapNoteType_Empty.
	struct Row {

		
		std::vector<IntermediateNoteData> notes; // notes for the given row
		std::vector<IntermediateNoteData> holds; // Any active hold notes, including ones thta started before this row
		std::set<int> holdTails;				 // Column index of any holds that end on this row
		std::vector<float> mines;				 // The time at which the last mine occurred for each column
		std::vector<float> fakeMines;			 // The time at which the last fake mine occurred for each column

		float second = 0;
		float beat = 0;
		int rowIndex = 0;
		int columnCount = 0;

		Row(int _columnCount)
		{
			columnCount = _columnCount;
			notes.resize(columnCount);
			holds.resize(columnCount);
			holdTails.clear();
			mines.resize(columnCount);
			fakeMines.resize(columnCount);
			second = 0;
			beat = 0;
			rowIndex = 0;
		}

		Json::Value ToJson(bool useStrings);
		static Json::Value ToJsonRows(std::vector<Row> rows, bool useStrings);
		static Json::Value ParityRowsJson(std::vector<Row> rows);
	};

	/// @brief A counter used while creating rows
	struct RowCounter
	{
		std::vector<IntermediateNoteData> notes;
		std::vector<IntermediateNoteData> activeHolds;
		float lastColumnSecond = CLM_SECOND_INVALID;
		float lastColumnBeat = CLM_SECOND_INVALID;

		std::vector<float> mines;
		std::vector<float> fakeMines;
		std::vector<float> nextMines;
		std::vector<float> nextFakeMines;

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

		StepParity::Row CreateRow(int columnCount)
		{
			Row row = Row(columnCount);
			row.notes.assign(notes.begin(), notes.end());
			row.mines.assign(nextMines.begin(), nextMines.end());
			row.fakeMines.assign(nextFakeMines.begin(), nextFakeMines.end());
			row.second = lastColumnSecond;
			row.beat = lastColumnBeat;

			for (int c = 0; c < columnCount; c++)
			{
				// save any active holds
				if (activeHolds[c].type == TapNoteType_Empty || activeHolds[c].second >= lastColumnSecond)
				{
					row.holds[c] = IntermediateNoteData();
				}
				else
				{
					row.holds[c] = activeHolds[c];
				}

				// save any hold tails

				if (activeHolds[c].type != TapNoteType_Empty)
				{
					if (abs(activeHolds[c].beat + activeHolds[c].hold_length - lastColumnBeat) < 0.0005)
					{
						row.holdTails.insert(c);
					}
				}
			}
			return row;
		}
	};

	/// @brief A node within a StepParityGraph.
	/// Represents a given state, and its connections to the states in the
	/// following row of the step chart.
	struct StepParityNode
	{
		int id = 0;	// The index of this node in its graph
		State state;

		std::unordered_map<StepParityNode *, float> neighbors; // Connections to, and the cost of moving to, the connected nodes
		StepParityNode(State _state)
		{
			state = _state;
		}

		Json::Value ToJson();
		
		int neighborCount()
		{
			return neighbors.size();
		}
	};

	/// @brief A comparator, needed in order use State objects as the key in a std::map.
	struct StateComparator
	{
		bool operator()(const State& lhs, const State& rhs) const {
			return lhs < rhs;
		}
	};
	
	/// @brief A graph, representing all of the possible states for a step chart.
	class StepParityGraph
	{
	private:
		std::vector<StepParityNode *> nodes; 
		std::unordered_map<int, std::map<State, StepParityNode *, StateComparator>> stateNodeMap;

	public:
		StepParityNode * startNode;	// This represents the very start of the song, before any notes
		StepParityNode *endNode;	// This represents the end of the song, after all of the notes

		~StepParityGraph()
		{
			for(StepParityNode * node: nodes)
			{
				delete node;
			}
		}

		/// @brief Returns a pointer to a StepParityNode that represents the given state within the graph.
		/// If a node already exists, it is returned, otherwise a new one is created and added to the graph.
		/// @param state 
		/// @return 
		StepParityNode* addOrGetExistingNode(State state)
		{
			if (stateNodeMap.find(state.rowIndex) == stateNodeMap.end()) {
				stateNodeMap[state.rowIndex] = std::map<State, StepParityNode*, StateComparator>();
			}
			if(stateNodeMap[state.rowIndex].find(state) == stateNodeMap[state.rowIndex].end())
			{
				StepParityNode* newNode = new StepParityNode(state);
				newNode->id = nodes.size();
				nodes.push_back(newNode);
				stateNodeMap[state.rowIndex][state] = newNode;
			}

			return stateNodeMap[state.rowIndex][state];
		}


		void addEdge(StepParityNode* from, StepParityNode* to, float cost) {
			from->neighbors[to] = cost;
		}

		int nodeCount() const
		{
			return nodes.size();
		}
		
		Json::Value ToJson();

		StepParityNode* operator[](int index) const{
			return nodes[index];
		}
	};


};

#endif
