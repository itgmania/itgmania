#include "global.h"
#include "StepParityGenerator.h"
#include "StepParityCost.h"
#include "NoteData.h"
#include "TechCounts.h"
#include "GameState.h"


using namespace StepParity;



void StepParityGenerator::analyzeNoteData(const NoteData &in)
{	
	columnCount = in.GetNumTracks();
	
	CreateRows(in);

	if(rows.size() == 0)
	{
		LOG->Trace("StepParityGenerator::analyze no rows, bailing out");
		return;
	}
	buildStateGraph();
	analyzeGraph();
}


void StepParityGenerator::analyzeGraph() {
	nodes_for_rows = computeCheapestPath();
	ASSERT_M(nodes_for_rows.size() == rows.size(), "nodes_for_rows should be the same length as rows!");

	for (unsigned long i = 0; i < rows.size(); i++)
	{
		StepParityNode *node = graph[nodes_for_rows[i]];
		rows[i].setFootPlacement(node->state.columns);
	}
}


void StepParityGenerator::buildStateGraph()
{
	// The first node of the graph is beginningState, which represents the time before
	// the first note (and so it's roIndex is considered -1)
	State beginningState(columnCount);
	beginningState.rowIndex = -1;
	beginningState.second = rows[0].second - 1;
	StepParityNode *startNode = graph.addOrGetExistingNode(beginningState);

	graph.startNode = startNode;

	std::queue<State> previousStates;
	previousStates.push(beginningState);
	StepParityCost costCalculator(layout);

	for (unsigned long i = 0; i < rows.size(); i++)
	{
		std::vector<State> uniqueStates;
		Row &row = rows[i];
		std::vector<FootPlacement> *PermuteFootPlacements = getFootPlacementPermutations(row);
		while (!previousStates.empty())
		{
			State state = previousStates.front();
			StepParityNode *initialNode = graph.addOrGetExistingNode(state);

			for(auto it = PermuteFootPlacements->begin(); it != PermuteFootPlacements->end(); it++)
			{
				State resultState = initResultState(state, row, *it);
				float* costs = costCalculator.getActionCost(&state, &resultState, rows, i);
				resultState.calculateHashes();
				StepParityNode *resultNode = graph.addOrGetExistingNode(resultState);
				graph.addEdge(initialNode, resultNode, costs);
				if(std::find(uniqueStates.begin(), uniqueStates.end(), resultState) == uniqueStates.end())
				{
					uniqueStates.push_back(resultState);
				}
			}
			previousStates.pop();
		}
		
		for (State s : uniqueStates)
		{
			previousStates.push(s);
		}
	}
	
	// at this point, previousStates holds all of the states for the very last row,
	// which just get connected to the endState
	State endState(columnCount);
	endState.rowIndex = rows.size();
	endState.second = rows[rows.size() - 1].second + 1;
	StepParityNode *endNode = graph.addOrGetExistingNode(endState);
	graph.endNode = endNode;
	while(!previousStates.empty())
	{
		State state = previousStates.front();
		StepParityNode *node = graph.addOrGetExistingNode(state);
        float * emptyCosts = new float[NUM_Cost];
        for(int i = 0; i < NUM_Cost; i++)
        {
            emptyCosts[i] = 0;
        }
		graph.addEdge(node, endNode, emptyCosts);
		previousStates.pop();
	}
}


State StepParityGenerator::initResultState(State &initialState, Row &row, const FootPlacement &columns)
{
	State resultState(row.columnCount);
	resultState.columns = columns;
	resultState.rowIndex = row.rowIndex;
	// I tried to condense this, but kept getting the logic messed up
	for (unsigned long i = 0; i < columns.size(); i++)
	{
		if(columns[i] == NONE) {
			continue;
		}
        resultState.whereTheFeetAre[columns[i]] = i;
        
		if(row.holds[i].type == TapNoteType_Empty)
		{
			resultState.movedFeet[i] = columns[i];
            resultState.didTheFootMove[columns[i]] = true;
			continue;
		}
		if(initialState.columns[i] != columns[i])
		{
			resultState.movedFeet[i] = columns[i];
            resultState.didTheFootMove[columns[i]] = true;
		}
	}

	for (unsigned long i = 0; i < columns.size(); i++)
	{
		if(columns[i] == NONE) {
			continue;
		}

		if(row.holds[i].type != TapNoteType_Empty)
		{
			resultState.holdFeet[i] = columns[i];
            resultState.isTheFootHolding[columns[i]] = true;
		}
	}
	resultState.second = row.second;
	return resultState;
}


std::vector<FootPlacement>* StepParityGenerator::getFootPlacementPermutations(const Row &row)
{
	int cacheKey = getPermuteCacheKey(row);	
	auto maybePermuteFootPlacements = permuteCache.find(cacheKey);
	
	if (maybePermuteFootPlacements == permuteCache.end())
	{
		FootPlacement blankColumns(row.columnCount, NONE);
		std::vector<FootPlacement> computedPermutations = PermuteFootPlacements(row, blankColumns, 0);
		permuteCache[cacheKey] = std::move(computedPermutations);
	}
	return &permuteCache[cacheKey];
}


std::vector<FootPlacement> StepParityGenerator::PermuteFootPlacements(const Row &row, FootPlacement columns, unsigned long column)
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
			return std::vector<FootPlacement>();
		}
		if (leftHeelIndex != -1 && leftToeIndex != -1)
		{
			if (!layout.bracketCheck(leftHeelIndex, leftToeIndex))
				return std::vector<FootPlacement>();
		}
		if (rightHeelIndex != -1 && rightToeIndex != -1)
		{
			if (!layout.bracketCheck(rightHeelIndex, rightToeIndex))
				return std::vector<FootPlacement>();
		}
		return {columns};
	}

	std::vector<FootPlacement> permutations;
	if (row.notes[column].type != TapNoteType_Empty  || row.holds[column].type != TapNoteType_Empty) {
		
	  for (StepParity::Foot foot: FEET) {
		if(std::find(columns.begin(), columns.end(), foot) != columns.end())
		{
			continue;
		}

		FootPlacement newColumns = columns;

		newColumns[column] = foot;
		std::vector<FootPlacement> p = PermuteFootPlacements(row, newColumns, column + 1);
		permutations.insert(permutations.end(), p.begin(), p.end());
	  }
	  return permutations;
	}
	return PermuteFootPlacements(row, columns, column + 1);
}


std::vector<int> StepParityGenerator::computeCheapestPath()
{
	int start = graph.startNode->id;
	int end = graph.endNode->id;
	std::vector<int> shortest_path;
	std::vector<float> cost(graph.nodeCount(), FLT_MAX);
	std::vector<int> predecessor(graph.nodeCount(), -1);

	cost[start] = 0;
	for (int i = start; i <= end; i++)
	{
		StepParityNode *node = graph[i];
		for(auto neighbor: node->neighbors)
		{
			int neighbor_id = neighbor.first->id;
			float weight = neighbor.second[COST_TOTAL];
//            printf("computeCheapestPath:: weight = %f", weight);
			if(cost[i] + weight < cost[neighbor_id])
			{
				cost[neighbor_id] = cost[i] + weight;
				predecessor[neighbor_id] = i;
			}
		}
	}

	int current_node = end;
	while(current_node != start)
	{
		ASSERT_M(current_node != -1, "WHOA");
		if(current_node != end)
		{
			shortest_path.push_back(current_node);
		}
		current_node = predecessor[current_node];
	}
	std::reverse(shortest_path.begin(), shortest_path.end());
	return shortest_path;
}


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


void StepParityGenerator::CreateRows(const NoteData &in)
{	
	TimingData *timing = GAMESTATE->GetProcessedTimingData();
	int columnCount = in.GetNumTracks();

	RowCounter counter = RowCounter(columnCount);

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
			// If this mine occurs on the same row as everything else that's been counted
			// (in other words, if this note doesn't represent the start of a new row),
			// and this isn't the very first row, put it in nextMines??
			// I honestly don't know why this works the way it does, it all feels
			// really backwards to me.
			// I think the complication comes from the fact that this is getting handled
			// before checking whether or not this note represens a new row.
			// But we only want to create a new Row if it has at least one note.
			// So probably something like
			
			/*
			 
			 for(note of notes)
			 {
				if(note is empty note)
				 {
					continue
				 }
				if(note is on new row and counter has at least one note)
				{
					create new row
					reset counter
				}
				check if note is a mine or fake mine
				if note is fake continue
				put note into counter.notes
				
			 }
			 */
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
				AddRow(counter);
			}

			// Move mines and fakeMines to "next", and reset counters
			counter.lastColumnSecond = note.second;
			counter.lastColumnBeat = note.beat;
			counter.nextMines.assign(counter.mines.begin(), counter.mines.end());
			counter.nextFakeMines.assign(counter.fakeMines.begin(), counter.fakeMines.end());
			counter.notes = std::vector<IntermediateNoteData>(columnCount);
			counter.mines = std::vector<float>(columnCount);
			counter.fakeMines = std::vector<float>(columnCount);

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

	AddRow(counter);
}

void StepParityGenerator::AddRow(RowCounter &counter)
{
	Row newRow = CreateRow(counter);
	newRow.rowIndex = rows.size();
	rows.push_back(newRow);
}

Row StepParityGenerator::CreateRow(RowCounter &counter)
{
	Row row = Row(columnCount);
	row.notes.assign(counter.notes.begin(), counter.notes.end());
	row.mines.assign(counter.nextMines.begin(), counter.nextMines.end());
	row.fakeMines.assign(counter.nextFakeMines.begin(), counter.nextFakeMines.end());
	row.second = counter.lastColumnSecond;
	row.beat = counter.lastColumnBeat;

	for (int c = 0; c < columnCount; c++)
	{
		// save any active holds
		if (counter.activeHolds[c].type == TapNoteType_Empty || counter.activeHolds[c].second >= counter.lastColumnSecond)
		{
			row.holds[c] = IntermediateNoteData();
		}
		else
		{
			row.holds[c] = counter.activeHolds[c];
		}

		// save any hold tails

		if (counter.activeHolds[c].type != TapNoteType_Empty)
		{
			if (abs(counter.activeHolds[c].beat + counter.activeHolds[c].hold_length - counter.lastColumnBeat) < 0.0005)
			{
				row.holdTails.insert(c);
			}
		}
	}
	return row;
}

int StepParityGenerator::getPermuteCacheKey(const Row &row)
{
	int key = 0;

	for (unsigned long i = 0; i < row.notes.size() && i < row.holds.size(); i++)
	{
		if(row.notes[i].type != TapNoteType_Empty || row.holds[i].type != TapNoteType_Empty)
		{
			key += pow(2, i);
		}
	}
	return key;
}

Json::Value StepParityGenerator::SMEditorParityJson()
{
	Json::Value root;
	
	for (unsigned long i = 0; i < nodes_for_rows.size(); i++)
	{
		StepParityNode *node = graph[nodes_for_rows[i]];
		root.append(node->state.ToJson(false));
	}
	
	return root;
}
