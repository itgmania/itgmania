#include "global.h"
#include "StepParityGenerator.h"
#include "NoteData.h"
#include "TechCounts.h"
#include "GameState.h"

// Generates foot parity given notedata
// Original algorithm by Jewel, polished by tillvit, then ported to C++

using namespace StepParity;

const std::map<RString, StageLayout> Layouts = {
	{"dance-single", {{-1, 0}, {0, -1}, {0, 1}, {1, 0}}}};

const float CLM_SECOND_INVALID = -1;

const std::vector<StepParity::Foot> FEET = {LEFT_HEEL, LEFT_TOE, RIGHT_HEEL, RIGHT_TOE};
const RString FEET_LABEL = "LlRr";

// helper functions

// StepParity methods


void StepParityGenerator::analyze(const NoteData &in, std::vector<StepParity::Row> &out, RString stepsTypeStr, bool log)
{	
	if(Layouts.find(stepsTypeStr) == Layouts.end())
	{
		LOG->Warn("Tried to call StepParityGenerator::analyze with an unsupported StepsType %s", stepsTypeStr.c_str());
		return;
	}

	layout = Layouts.at(stepsTypeStr);
	int columnCount = in.GetNumTracks();
	float createRowsStart = RageTimer::GetTimeSinceStart();
	CreateRows(in, out);
	float createRowsEnd = RageTimer::GetTimeSinceStart();

	LOG->Trace("StepParityGenerator::analyze time to CreateRows: %.3f seconds", createRowsEnd - createRowsStart);

	if (log)
	{
		LOG->Trace("StepParityGenerator::analyze start");
	}

	if(out.size() == 0)
	{
		if(log)
		{
			LOG->Trace("StepParityGenerator::analyze no rows, bailing out");
		}
		return;
	}

	float analyzeRowsStart = RageTimer::GetTimeSinceStart();
	analyzeRows(out, stepsTypeStr, columnCount);
	float analyzeRowsEnd = RageTimer::GetTimeSinceStart();
	LOG->Trace("StepParityGenerator::analyze time to analyzeRows: %.3f seconds", analyzeRowsEnd - analyzeRowsStart);
LOG->Trace("StepParityGenerator::analyze total time: %.3f seconds", analyzeRowsEnd - createRowsStart);
	if (log)
	{
		LOG->Trace("StepParityGenerator::alalyze finished");
		
		LOG->Trace("Explored nodes: %d, Cached nodes: %d", exploreCounter, cacheCounter);
	}
}

void StepParityGenerator::analyzeRows(std::vector<StepParity::Row> &rows, RString stepsTypeStr, int columnCount)
{
	if(Layouts.find(stepsTypeStr) == Layouts.end())
	{
		LOG->Warn("Tried to call StepParityGenerator::analyze with an unsupported StepsType %s", stepsTypeStr.c_str());
		return;
	}

	layout = Layouts.at(stepsTypeStr);
	State *state = new State();
	state->columns = std::vector<Foot>(columnCount, NONE);
	for (unsigned long i = 0; i < rows.size(); i++)
	{
		auto bestActions = getBestMoveLookahead(state, rows, i);
		Action *bestAction = bestActions[0]->head == nullptr ? bestActions[0] : bestActions[0]->head;

		for (unsigned long j = 0; j < bestAction->resultState->columns.size(); j++) {
			if(rows[i].notes[j].type != TapNoteType_Empty)
			{
				rows[i].notes[j].parity = bestAction->resultState->columns[j];
			}
		}
		rows[i].cost = bestAction->cost;
		rows[i].selectedAction = bestAction;
		state = bestAction->resultState;
		costCache.clear();
	}
}

// Converts the NoteData into a more convenient format that already has all of the timing data
// incorporated into it.
void StepParityGenerator::CreateIntermediateNoteData(const NoteData &in, std::vector<IntermediateNoteData> &out)
{
	TimingData *timing = GAMESTATE->GetProcessedTimingData();
	int columnCount = in.GetNumTracks();

	NoteData::all_tracks_const_iterator curr_note = in.GetTapNoteRangeAllTracks(0, MAX_NOTE_ROW);

	std::vector<IntermediateNoteData> notes;

	for (; !curr_note.IsAtEnd(); ++curr_note)
	{
		IntermediateNoteData note;
		note.type = curr_note->type;
		note.subtype = curr_note->subType;
		note.col = curr_note.Track();

		note.row = curr_note.Row();
		note.beat = NoteRowToBeat(curr_note.Row());
		note.second = timing->GetElapsedTimeFromBeat(note.beat);

		note.fake = note.type == TapNoteType_Fake || timing->IsFakeAtBeat(note.row);
		note.warped = timing->IsWarpAtRow(note.row);

		if (note.type == TapNoteType_HoldHead)
		{
			note.hold_length = NoteRowToBeat(curr_note->iDuration);
		}
		else
		{
			note.hold_length = -1;
		}

		notes.push_back(note);
	}
	out.assign(notes.begin(), notes.end());
}

std::vector<Action*> StepParityGenerator::GetPossibleActions(State *initialState, std::vector<Row>& rows, int rowIndex)
{

	Row row = rows[rowIndex];
	std::vector<StepParity::Foot> blankColumns(4, NONE);

	std::vector<std::vector<StepParity::Foot>> permuteColumns = PermuteColumns(row, blankColumns, 0);

	std::vector<Action*> actions;

	for(std::vector<StepParity::Foot> columns: permuteColumns)
	{
		Action *action = initAction(initialState, row, columns);
		getActionCost(action, rows, rowIndex);
		actions.push_back(action);
	}

	return actions;
}



std::vector<std::vector<StepParity::Foot>> StepParityGenerator::PermuteColumns(Row &row, std::vector<StepParity::Foot> columns, unsigned long column)
{
	if (column >= columns.size())
	{
		int leftHeelIndex = -1;
		int leftToeIndex = -1;
		int rightHeelIndex = -1;
		int rightToeIndex = -1;
		for (unsigned long i = 0; i < columns.size(); i++)
		{
			if (columns[i] == NONE)
				continue;
			if (columns[i] == LEFT_HEEL)
				leftHeelIndex = i;
			if (columns[i] == LEFT_TOE)
				leftToeIndex = i;
			if (columns[i] == RIGHT_HEEL)
				rightHeelIndex = i;
			if (columns[i] == RIGHT_TOE)
				rightToeIndex = i;
		}
		if (
			(leftHeelIndex == -1 && leftToeIndex != -1) ||
			(rightHeelIndex == -1 && rightToeIndex != -1))
		{
			return std::vector<std::vector<StepParity::Foot>>();
		}
		if (leftHeelIndex != -1 && leftToeIndex != -1)
		{
			if (!bracketCheck(leftHeelIndex, leftToeIndex))
				return std::vector<std::vector<StepParity::Foot>>();
		}
		if (rightHeelIndex != -1 && rightToeIndex != -1)
		{
			if (!bracketCheck(rightHeelIndex, rightToeIndex))
				return std::vector<std::vector<StepParity::Foot>>();
		}
		return {columns};
	}

	std::vector<std::vector<StepParity::Foot>> permutations;
	if (row.notes[column].type != TapNoteType_Empty  || row.holds[column].type != TapNoteType_Empty) {
		
	  for (StepParity::Foot foot: FEET) {
		if(std::find(columns.begin(), columns.end(), foot) != columns.end())
		{
			continue;
		}

		std::vector<StepParity::Foot> newColumns = columns;

		newColumns[column] = foot;
		std::vector<std::vector<StepParity::Foot>> p = PermuteColumns(row, newColumns, column + 1);
		permutations.insert(permutations.end(), p.begin(), p.end());
	  }
	  return permutations;
	}
	return PermuteColumns(row, columns, column + 1);
}


bool compareActionCosts(Action * a, Action * b)
{
	return a->cost < b->cost;
}

std::vector<Action *> StepParityGenerator::getBestMoveLookahead(State *state, std::vector<Row> &rows, int rowIndex)
{
	auto actions = GetPossibleActions(state, rows, rowIndex);
	std::sort(actions.begin(), actions.end(), compareActionCosts);

	for (unsigned long i = 1; i < SEARCH_DEPTH && rowIndex + i < rows.size() - 1; i++)
	{
		std::vector<Action *> addl_actions;

		for (auto action : actions)
		{
			auto results = GetPossibleActions(action->resultState, rows, rowIndex + i);
			for(auto result: results)
			{
				result->cost = result->cost * pow(LOOKAHEAD_COST_MOD, i) + action->cost;
				result->head = action->head == nullptr ? action : action->head;
				result->parent = action;
			}
			addl_actions.insert(addl_actions.end(), results.begin(), results.end());
		}
		std::sort(addl_actions.begin(), addl_actions.end(), compareActionCosts);
		if(addl_actions.size() > SEARCH_BREADTH)
		{
			std::vector<Action *> sliced(addl_actions.begin(), addl_actions.begin() + SEARCH_BREADTH);
			actions = sliced;
		}
		else
		{
			actions = addl_actions;
		}
	}
	return actions;
}

struct RowCounter
{
	std::vector<IntermediateNoteData> notes;
	std::vector<IntermediateNoteData> activeHolds;
	float lastColumnSecond = CLM_SECOND_INVALID;
	float lastColumnBeat = CLM_SECOND_INVALID;

	std::vector<int> mines;
	std::vector<int> fakeMines;
	std::vector<int> nextMines;
	std::vector<int> nextFakeMines;

	RowCounter(int columnCount)
	{

		notes = std::vector<IntermediateNoteData>(columnCount);
		activeHolds = std::vector<IntermediateNoteData>(columnCount);
		mines = std::vector<int>(columnCount, 0);
		fakeMines = std::vector<int>(columnCount, 0);
		nextMines = std::vector<int>(columnCount, 0);
		nextFakeMines = std::vector<int>(columnCount, 0);

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

void StepParityGenerator::CreateRows(const NoteData &in, std::vector<StepParity::Row> &out)
{
	layout = Layouts.at("dance-single"); // TODO remove this
	
	TimingData *timing = GAMESTATE->GetProcessedTimingData();
	int columnCount = in.GetNumTracks();

	RowCounter counter = RowCounter(columnCount);

	std::vector<StepParity::Row> rows;
	std::vector<IntermediateNoteData> noteData;

	CreateIntermediateNoteData(in, noteData);

	for (IntermediateNoteData note : noteData)
	{
		if (note.type == TapNoteType_Empty)
		{
			continue;
		}

		if (note.type == TapNoteType_Mine)
		{
			if (note.second == counter.lastColumnSecond && rows.size() > 0)
			{
				if (note.fake)
				{
					counter.nextFakeMines[note.col] = note.second;
				}
				else
				{
					counter.nextMines[note.col] = note.second;
				}
			}
			else
			{
				if (note.fake)
				{
					counter.fakeMines[note.col] = note.second;
				}
				else
				{
					counter.mines[note.col] = note.second;
				}
			}
			continue;
		}

		if (note.fake)
		{
			continue;
		}

		if (counter.lastColumnSecond != note.second)
		{

			// we're past the previous row, so save all of the previous row's data
			if (counter.lastColumnSecond != CLM_SECOND_INVALID)
			{
				Row newRow = counter.CreateRow(columnCount);
				rows.push_back(newRow);
			}

			// Move mines and fakeMines to "next", and reset counters
			counter.lastColumnSecond = note.second;
			counter.lastColumnBeat = note.beat;
			counter.nextMines.assign(counter.mines.begin(), counter.mines.end());
			counter.nextFakeMines.assign(counter.fakeMines.begin(), counter.fakeMines.end());
			counter.notes = std::vector<IntermediateNoteData>(columnCount);
			counter.mines = std::vector<int>(columnCount);
			counter.fakeMines = std::vector<int>(columnCount);

			// reset any now-inactive holds to empty values
			for (int c = 0; c < columnCount; c++)
			{
				if (counter.activeHolds[c].type == TapNoteType_Empty || note.beat > counter.activeHolds[c].beat + counter.activeHolds[c].hold_length)
				{
					counter.activeHolds[c] = IntermediateNoteData();
				}
			}
		}

		counter.notes[note.col] = note;
		if (note.type == TapNoteType_HoldHead)
		{
			counter.activeHolds[note.col] = note;
		}
	}

	Row newRow = counter.CreateRow(columnCount);
	rows.push_back(newRow);

	out.assign(rows.begin(), rows.end());
}



// helper methods

int StepParityGenerator::getPermuteCacheKey(Row &row)
{
	int key = 0;

	for (unsigned long i = 0; i < row.notes.size() && i < row.holds.size(); i++)
	{
		if(row.notes[i].type != TapNoteType_Empty || row.notes[i].type != TapNoteType_Empty)
		{
			key += pow(2, i);
		}
	}
	return key;
}

Action * StepParityGenerator::initAction(State *initialState, Row &row, std::vector<Foot> columns)
{
	Action *action = new Action();
	action->initialState = initialState;
	State *resultState = new State();
	resultState->columns = columns;

	std::set<Foot> movedFeet;
	std::set<Foot> holdFeet;


	// I tried to condense this, but kept getting the logic messed up
	for (unsigned long i = 0; i < columns.size(); i++)
	{
		if(columns[i] == NONE) {
			continue;
		}
		if(row.holds[i].type == TapNoteType_Empty)
		{
			movedFeet.insert(columns[i]);
			continue;
		}
		if(initialState->columns[i] != columns[i])
		{
			movedFeet.insert(columns[i]);
		}
	}

	for (unsigned long i = 0; i < columns.size(); i++)
	{
		if(columns[i] == NONE) {
			continue;
		}

		if(row.holds[i].type != TapNoteType_Empty)
		{
			holdFeet.insert(columns[i]);
		}
	}

	resultState->movedFeet = movedFeet;
	resultState->holdFeet = holdFeet;
	resultState->second = row.second;

	action->resultState = resultState;
	action->cost = 0;

	return action;
}

bool StepParityGenerator::bracketCheck(int column1, int column2)
{
	StagePoint p1 = layout[column1];
	StagePoint p2 = layout[column2];
	return getDistanceSq(p1, p2) <= 2;
}

float StepParityGenerator::getDistanceSq(StepParity::StagePoint p1, StepParity::StagePoint p2)
{
	return (p1.y - p2.y) * (p1.y - p2.y) + (p1.x - p2.x) * (p1.x - p2.x);
}

float StepParityGenerator::getPlayerAngle(StepParity::StagePoint left, StepParity::StagePoint right)
{
	float x1 = right.x - left.x;
	float y1 = right.y - left.y;
	float x2 = 1;
	float y2 = 0;
	float dot = x1 * x2 + y1 * y2;
	float det = x1 * y2 - y1 * x2;
	return atan2f(det, dot);
}


float StepParityGenerator::getXDifference(int leftIndex, int rightIndex) {
	if (leftIndex == rightIndex) return 0;
	float dx = layout[rightIndex].x - layout[leftIndex].x;
	float dy = layout[rightIndex].y - layout[leftIndex].y;

	float distance = sqrt(dx * dx + dy * dy);
	dx /= distance;

	float negative = dx <= 0;

	dx = pow(dx, 4);

	if (negative) dx = -dx;

	return dx;
  }

  float StepParityGenerator::getYDifference(int leftIndex, int rightIndex) {
	if (leftIndex == rightIndex) return 0;
	float dx = layout[rightIndex].x - layout[leftIndex].x;
	float dy = layout[rightIndex].y - layout[leftIndex].y;

	float distance = sqrt(dx * dx + dy * dy);
	dy /= distance;

	float negative = dy <= 0;

	dy = pow(dy, 4);

	if (negative) dy = -dy;

	return dy;
  }

StagePoint StepParityGenerator::averagePoint(int leftIndex, int rightIndex) {
	if (leftIndex == -1 && rightIndex == -1) return { 0,0 };
	if (leftIndex == -1) return layout[rightIndex];
	if (rightIndex == -1) return layout[leftIndex];
	return {
	  (layout[leftIndex].x + layout[rightIndex].x) / 2.0f,
	  (layout[leftIndex].y + layout[rightIndex].y) / 2.0f,
	};
  }