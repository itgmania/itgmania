#ifndef STEP_PARITY_GENERATOR_H
#define STEP_PARITY_GENERATOR_H

#include "GameConstantsAndTypes.h"
#include "NoteData.h"
#include "json/json.h"
#include "JsonUtil.h"

struct TechCounts;

const RString FEET_LABELS[] = {"N", "L", "l", "R", "r", "5??", "6??"};
const RString TapNoteTypeShortNames[] = {
	"Empty",
	"Tap",
	"Hold",
	"HoldTail",
	"Mine",
	"Lift",
	"Attack",
	"AutoKeySound",
	"Fake",
	"",
	""
};

const RString TapNoteSubTypeShortNames[] = {
	"Hold",
	"Roll",
	//"Mine",
	"",
	""
};

namespace StepParity {

	enum WEIGHTS {
	DOUBLESTEP= 850,
	BRACKETJACK= 20,
	JACK= 30,
	JUMP= 30,
	BRACKETTAP= 400,
	HOLDSWITCH= 20,
	MINE= 10000,
	FOOTSWITCH= 5000,
	MISSED_FOOTSWITCH= 500,
	FACING= 2,
	DISTANCE= 6,
	SPIN= 1000,
	SIDESWITCH= 130,
	};

	enum Foot
	{
		NONE = 0,
		LEFT_HEEL= 1,
		LEFT_TOE = 2,
		RIGHT_HEEL = 3,
		RIGHT_TOE = 4,
		// LEFT_HEEL= 1 << 0,
		// LEFT_TOE = 1 << 1,
		// RIGHT_HEEL = 1 << 2,
		// RIGHT_TOE = 1 << 3,
	};

	struct StagePoint {
		float x;
		float y;
	};

	typedef std::vector<StagePoint> StageLayout;


	template<typename Container>
	Json::Value FeetToJson(const Container& feets, bool useStrings = false)
	{
		Json::Value root;
		for(Foot f: feets)
		{
			if(useStrings)
			{
				root.append(FEET_LABELS[static_cast<int>(f)]);
			}
			else
			{
				root.append(static_cast<int>(f));
			}
		}
		return root;
	}
	struct State {
		std::vector<Foot> columns;
		std::vector<Foot> movedFeet;
		std::vector<Foot> holdFeet;
		float second;

		Json::Value ToJson(bool useStrings = false)
		{
			Json::Value root;
			Json::Value jsonColumns = FeetToJson(columns, useStrings);
			Json::Value jsonMovedFeet = FeetToJson(movedFeet, useStrings);
			Json::Value jsonHoldFeet = FeetToJson(holdFeet, useStrings);

			root["columns"] = jsonColumns;
			root["movedFeet"] = jsonMovedFeet;
			root["holdFeet"] = jsonHoldFeet;
			root["second"] = second;

			return root;
		}
	};

	struct Action {
		Action *head;
		Action *parent;
		State *initialState;
		State *resultState;
		float cost;

		Json::Value ToJson(bool useStrings = false)
		{
			Json::Value root;

			root["initialState"] = initialState->ToJson(useStrings);
			root["resultState"] = resultState->ToJson(useStrings);
			root["cost"] = cost;
			return root;
		}
	};

	/** @brief A convenience struct used to encapsulate data from NoteData in an 
	 * easier to work with format.
	*/
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
		Json::Value ToJson(bool useStrings = false)
		{
			Json::Value root;
			if(useStrings)
			{
				root["type"] = TapNoteTypeShortNames[static_cast<int>(type)];
				root["subtype"] = TapNoteSubTypeShortNames[static_cast<int>(subtype)];
				root["parity"] = FEET_LABELS[static_cast<int>(parity)];
				
			}
			else
			{
				root["type"] = static_cast<int>(type);
				root["subtype"] = static_cast<int>(subtype);
				root["parity"] = static_cast<int>(parity);
			}
			root["col"] = col;
			root["row"] = row;
			root["beat"] = beat;
			root["hold"] = hold_length;
			root["warped"] = warped;
			root["fake"] = fake;
			root["second"] = second;
			return root;
		}
	};

	/** @brief A slightly complicated structure to encapsulate all of the data for a given 
	 * row of a step chart.
	 * `notes` and `holds` will always have `columnCount` entries. "Empty" columns will have a type of TapNoteType_Empty.
	 * 
	*/
	struct Row {

		// notes for the given row
		std::vector<IntermediateNoteData> notes;	
		// Indicates any active holds, including ones that
		// didn't start on this row
		std::vector<IntermediateNoteData> holds;	
		std::set<int> holdTails;
		std::vector<int> mines;
		std::vector<int> fakeMines;
		float second = 0;
		float beat = 0;
		int _columnCount = 0;
		float cost = 0;
		Action *selectedAction = nullptr;
		Row(int columnCount)
		{
			_columnCount = columnCount;
			Clear();
		}

		void Clear()
		{
			notes.resize(_columnCount);
			holds.resize(_columnCount);
			holdTails.clear();
			mines.resize(_columnCount);
			fakeMines.resize(_columnCount);
			second = 0;
			beat = 0;
			cost = 0;
		}

		Json::Value ToJson(bool useStrings = false)
		{
			Json::Value root;
			Json::Value jsonNotes;
			Json::Value jsonHolds;
			Json::Value jsonHoldTails;
			Json::Value jsonMines;
			Json::Value jsonFakeMines;

			for (IntermediateNoteData n : notes) { jsonNotes.append(n.ToJson(useStrings)); }
			for (IntermediateNoteData n : holds) { jsonHolds.append(n.ToJson(useStrings)); }
			for (int t : holdTails) { jsonHoldTails.append(t); }
			for (int m : mines) { jsonMines.append(m); }
			for (int f : fakeMines) { jsonFakeMines.append(f); }

			root["notes"] = jsonNotes;
			root["holds"] = jsonHolds;
			root["hold_tails"] = jsonHoldTails;
			root["mines"] = jsonMines;
			root["fake_mines"] = jsonFakeMines;
			root["second"] = second;
			root["beat"] = beat;
			root["cost"] = cost;
			if(selectedAction != nullptr)
			{
				root["selectedAction"] = selectedAction->ToJson(false);
			}
			else
			{
				root["selectedAction"] = "";
			}
			return root;
		}
	};

	class StepParityGenerator 
	{
	private:
		StageLayout layout;
		std::map<int, std::map<RString, float>> costCache;
		int cacheCounter = 0;
		int exploreCounter = 0;
		std::map < int, std::vector<std::vector<StepParity::Foot>>> permuteCache;

		unsigned long SEARCH_DEPTH = 16;
		unsigned long SEARCH_BREADTH = 30;
		float LOOKAHEAD_COST_MOD = 0.95;

	public:
		void analyze(const NoteData &in, std::vector<StepParity::Row> &out, RString stepsTypeStr, bool log);
		void analyzeRows(std::vector<StepParity::Row> &rows, RString stepsTypeStr, int columnCount);
		std::vector<Action*> GetPossibleActions(State *initialState, std::vector<Row>& rows, int rowIndex);
		std::vector<std::vector<Foot>> PermuteColumns(Row &row,    std::vector<Foot> columns, unsigned long column);
		std::vector<Action *> getBestMoveLookahead(State *state, std::vector<Row>& rows, int rowIndex);

		// Defined in StepParityCost.cpp
		void getActionCost(Action *action, std::vector<Row>& rows, int rowIndex);
		void mergeInitialAndResultPosition(Action *action, std::vector<StepParity::Foot> &combinedColumns);
		float calcHoldSwitchCost(Action *action, Row &row, std::vector<StepParity::Foot> &combinedColumns);
		float calcBracketTapCost(Action *action, Row &row, int leftHeel, int leftToe, int rightHeel, int rightToe, float elapsedTime);
		float calcMovingFootWhileOtherIsntOnPadCost(Action *action);
		float calcJackCost(Action *action, std::vector<Row> &rows, int rowIndex, bool movedLeft, bool movedRight, bool jackedLeft, bool jackedRight, bool didJump);
		float calcJumpCost(Row &row, bool movedLeft, bool movedRight, float elapsedTime);
		float calcMissedFootswitchCost(Row &row, bool jackedLeft, bool jackedRight);
		float calcFacingAndSpinCosts(Action *action, std::vector<StepParity::Foot> &combinedColumns);
		float caclFootswitchCost(Action *action, Row &row, std::vector<StepParity::Foot> &combinedColumns, float elapsedTime);
		float calcSideswitchCost(Action *action);
		float calcJackedNotesTooCloseTogetherCost(bool movedLeft, bool movedRight, bool jackedLeft, bool jackedRight, float elapsedTime);
		float calcBigMovementsQuicklyCost(Action *action, float elapsedTime);

		bool didDoubleStep(Action *action, std::vector<Row> &rows, int rowIndex, bool movedLeft, bool jackedLeft, bool movedRight, bool jackedRight);
		bool didJackLeft(Action *action, int leftHeel, int leftToe, bool movedLeft, bool didJump);
		bool didJackRight(Action *action, int rightHeel, int rightToe, bool movedRight, bool didJump);
		/** @brief Converts the NoteData into a more convenient format that already has all of the timing data
	 incorporated into it. */
		void CreateIntermediateNoteData(const NoteData &in, std::vector<IntermediateNoteData> &out);
		void CreateRows(const NoteData &in, std::vector<Row> &out);
		
	private:
		// helper functions

		float getCachedCost(int rowIndex, RString cacheKey);
		int getPermuteCacheKey(Row &row);
		Action *initAction(State *initialState, Row &row, std::vector<Foot> columns);
		bool bracketCheck(int column1, int column2);
		float getDistanceSq(StepParity::StagePoint p1, StepParity::StagePoint p2);
		float getPlayerAngle(StepParity::StagePoint left, StepParity::StagePoint right);

		float getXDifference(int leftIndex, int rightIndex);
		float getYDifference(int leftIndex, int rightIndex);
		StagePoint averagePoint(int leftIndex, int rightIndex);
	};
};

#endif