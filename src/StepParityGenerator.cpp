#include "global.h"
#include "StepParityGenerator.h"
#include "StepParityCost.h"
#include "NoteData.h"
#include "TechCounts.h"
#include "GameState.h"

using namespace StepParity;

bool StepParityGenerator::analyzeNoteData(const NoteData &in)
{
	columnCount_ = in.GetNumTracks();
	
	CreateRows(in);

	if(rows.size() == 0)
	{
		LOG->Trace("StepParityGenerator::analyze no rows, bailing out");
		return false;
	}
	buildStateGraph();
	return analyzeGraph();
}

bool StepParityGenerator::analyzeGraph() {
	nodes_for_rows = computeCheapestPath();
	if(nodes_for_rows.size() != rows.size())
	{
		LOG->Info("StepParityGenerator::analyzeGraph: nodes_for_rows should be the same length as rows! This means we probably generated an invalid graph fro this chart.");
		return false;
	}

	for (unsigned long i = 0; i < rows.size(); i++)
	{
		StepParityNode *node = nodes[nodes_for_rows[i]];
		rows[i].setFootPlacement(node->state->combinedColumns);
	}
	return true;
}

void StepParityGenerator::buildStateGraph()
{
	// The first node of the graph is beginningState, which represents the time before
	// the first note (and so it's roIndex is considered -1)
	beginningState = new State(columnCount_);
	startNode = addNode(beginningState, rows[0].second - 1, -1);
	
	std::queue<StepParityNode *> previousNodes;
	previousNodes.push(startNode);
	StepParityCost costCalculator(layout);

	for (unsigned long i = 0; i < rows.size(); i++)
	{
		std::vector<StepParityNode *> resultNodes;
		Row &row = rows[i];
		const std::vector<FootPlacement> *permutations = getFootPlacementPermutations(row);
		
		while (!previousNodes.empty())
		{
			StepParityNode *initialNode = previousNodes.front();
			float elapsedTime = row.second - initialNode->second;
			for(auto it = permutations->begin(); it != permutations->end(); it++)
			{
				State * resultState = initResultState(initialNode->state, row, *it);
				
				float cost = costCalculator.getActionCost(initialNode->state, resultState, rows, i, elapsedTime);
				addStateToGraph(resultState, initialNode, row, resultNodes, cost);
				
			}
			previousNodes.pop();
		}
		
		for (StepParityNode * n : resultNodes)
		{
			previousNodes.push(n);
		}
	}
	
	// at this point, previousStates holds all of the states for the very last row,
	// which just get connected to the endState
	endingState = new State(columnCount_);
	endNode = addNode(endingState, rows[rows.size() - 1].second + 1, rows.size());
	
	while(!previousNodes.empty())
	{
		StepParityNode *node = previousNodes.front();
		addEdge(node, endNode, 0);
		previousNodes.pop();
	}
}

void StepParityGenerator::addStateToGraph(State * resultState, StepParityNode * initialNode, Row & row, std::vector<StepParityNode *> &existingNodesForThisRow, float cost)
{
	
	for(StepParityNode * existingNode : existingNodesForThisRow)
	{
		if(existingNode->state == resultState)
		{
			addEdge(initialNode, existingNode, cost);
			return;
		}
	}
	StepParityNode *resultNode = addNode(resultState, row.second, row.rowIndex);
	addEdge(initialNode, resultNode, cost);
	existingNodesForThisRow.push_back(resultNode);
}


State * StepParityGenerator::initResultState(State * initialState, Row &row, const FootPlacement &columns)
{
	if(tmpState == nullptr)
	{
		tmpState = new State(row.columnCount);
	}
	
	State * resultState = tmpState;
	

	// reset resultState
	
	resultState->moved_mask = 0;
	resultState->holding_mask = 0;
	for(int i = 0; i < NUM_Foot; i++)
	{
		resultState->whereTheFeetAre[i] = INVALID_COLUMN;
		resultState->whatNoteTheFootIsHitting[i] = INVALID_COLUMN;
		resultState->didTheFootMove[i] = false;
		resultState->isTheFootHolding[i] = false;
	}
	
	for (unsigned long i = 0; i < columns.size(); i++)
	{
		resultState->columns[i] = NONE;
		resultState->combinedColumns[i] = NONE;
		resultState->movedFeet[i] = NONE;
		resultState->holdFeet[i] = NONE;
	}
		
	// I tried to condense this, but kept getting the logic messed up
	for (unsigned long i = 0; i < columns.size(); i++)
	{
		resultState->columns[i] = columns[i];
		if(columns[i] == NONE) {
			continue;
		}
		resultState->whatNoteTheFootIsHitting[columns[i]] = i;

		if(row.holds[i].type == TapNoteType_Empty)
		{
			resultState->movedFeet[i] = columns[i];
			resultState->didTheFootMove[columns[i]] = true;
			continue;
		}
		if(initialState->combinedColumns[i] != columns[i])
		{
			resultState->movedFeet[i] = columns[i];
			resultState->didTheFootMove[columns[i]] = true;
		}
	}

	for (unsigned long i = 0; i < columns.size(); i++)
	{
		if(columns[i] == NONE) {
			continue;
		}

		if(row.holds[i].type != TapNoteType_Empty)
		{
			resultState->holdFeet[i] = columns[i];
			resultState->isTheFootHolding[columns[i]] = true;
		}
		
		uint16_t bit = 0x1 << i;
		uint16_t foot_mask = FOOT_MASKS[columns[i]];
		if((row.hold_mask & bit) != 0)
		{
			resultState->holding_mask |= foot_mask;
		}
		if((row.hold_mask & bit) == 0 || (initialState->combinedColumns[i] != columns[i]))
		{
			resultState->moved_mask |= foot_mask;
		}
	}
	
	mergeInitialAndResultPosition(initialState, resultState, (int)columns.size());
	
	std::uint64_t stateHash = getStateCacheKey(resultState);
	
	auto maybeState = stateCache.find(stateHash);
	
	if(maybeState != stateCache.end())
	{
		State* cachedState = maybeState->second;
		return maybeState->second;
	}
	
	stateCache.insert({stateHash, resultState});
	tmpState = nullptr;
	return resultState;
}

// This merges the `columns` properties of initialState and resultState, which
// fully represents the player's position on the dance stage.
// For example:
// initialState.combinedColumns = [L,0,0,R]
// resultState.columns = [0,L,0,0]
// combinedColumns = [0,L,0,R]
// This eventually gets saved back to resultState
void StepParityGenerator::mergeInitialAndResultPosition(State * initialState, State * resultState, int columnCount)
{
	// Merge initial + result position
	for (int i = 0; i < columnCount; i++) {
	  	  // copy in data from resultState over the top which overrides it, as long as it's not nothing
	  	  if (resultState->columns[i] != NONE) {
		  	  resultState->combinedColumns[i] = resultState->columns[i];
		continue;
	  	  }

	  	  // copy in data from initialState, if it wasn't moved
	  	  if (
		initialState->combinedColumns[i] == LEFT_HEEL ||
		initialState->combinedColumns[i] == RIGHT_HEEL
	  	  ) {
		if (!resultState->didTheFootMove[initialState->combinedColumns[i]]) {
			resultState->combinedColumns[i] = initialState->combinedColumns[i];
		}
	  	  } else if (initialState->combinedColumns[i] == LEFT_TOE) {
		if (
		  	  !resultState->didTheFootMove[LEFT_TOE] &&
		  	  !resultState->didTheFootMove[LEFT_HEEL]
		) {
			resultState->combinedColumns[i] = initialState->combinedColumns[i];
		}
	  	  } else if (initialState->combinedColumns[i] == RIGHT_TOE) {
		if (
		  	  !resultState->didTheFootMove[RIGHT_TOE] &&
		  	  !resultState->didTheFootMove[RIGHT_HEEL]
		) {
			resultState->combinedColumns[i] = initialState->combinedColumns[i];
		}
	  	  }
	}
	
	for(int i = 0; i < columnCount; i++)
	{
		if(resultState->combinedColumns[i] != NONE)
		{
			resultState->whereTheFeetAre[resultState->combinedColumns[i]] = i;
		}
	}
}

const std::vector<FootPlacement>* StepParityGenerator::getFootPlacementPermutations(const Row &row)
{
	int cacheKey = row.note_mask | row.hold_mask;
	
	auto maybePermuteFootPlacements = layout->permuteCache.find(cacheKey);
	
	if (maybePermuteFootPlacements == layout->permuteCache.end())
	{
		maybePermuteFootPlacements = layout->permuteCache.find(row.note_mask);
	}
	
	if(maybePermuteFootPlacements == layout->permuteCache.end())
	{
		return &(layout->permuteCache.at(0));
	}
	else
	{
		auto& value = maybePermuteFootPlacements->second;
		return &value;
	}
}

std::vector<int> StepParityGenerator::computeCheapestPath()
{
	int start = startNode->id;
	int end = endNode->id;
	std::vector<int> shortest_path;
	std::vector<float> cost(nodes.size(), FLT_MAX);
	std::vector<int> predecessor(nodes.size(), -1);

	cost[start] = 0;
	for (int i = start; i <= end; i++)
	{
		
		StepParityNode *node = nodes[i];
		for(auto neighbor: node->neighbors)
		{
			int neighbor_id = neighbor.first->id;
			float weight = neighbor.second;
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
		
		if(current_node == -1)
		{
			LOG->Info("StepParityGenerator::computeCheapestPath: encountered a value of -1 for 'current_node', this means that we did not produce a valid chart.");
			return {};
		}
		if(current_node != end)
		{
			shortest_path.push_back(current_node);
		}
		current_node = predecessor[current_node];
	}
	std::reverse(shortest_path.begin(), shortest_path.end());
	return shortest_path;
}

void StepParityGenerator::CreateIntermediateNoteData(
		const NoteData &in, std::vector<IntermediateNoteData> &out)
{
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
	int columnCount = in.GetNumTracks();

	RowCounter counter = RowCounter(columnCount);

	std::vector<IntermediateNoteData> noteData;

	CreateIntermediateNoteData(in, noteData);

	for (IntermediateNoteData note : noteData)
	{
		if (note.type == TapNoteType_Empty || note.type == TapNoteType_AutoKeysound)
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
			// We're past the previous row, so save all of the previous row's data.
			if (counter.lastColumnSecond != CLM_SECOND_INVALID)
			{
				AddRow(counter);
			}

			// Move mines and fakeMines to "next", and reset counters.
			counter.lastColumnSecond = note.second;
			counter.lastColumnBeat = note.beat;
			counter.nextMines.assign(counter.mines.begin(), counter.mines.end());
			counter.nextFakeMines.assign(counter.fakeMines.begin(), counter.fakeMines.end());
			counter.notes = std::vector<IntermediateNoteData>(columnCount_);
			counter.mines = std::vector<float>(columnCount_);
			counter.fakeMines = std::vector<float>(columnCount_);

			// Reset any now-inactive holds to empty values.
			for (int c = 0; c < columnCount_; c++)
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
	Row row = Row(columnCount_);
	row.notes.assign(counter.notes.begin(), counter.notes.end());
	row.mines.assign(counter.nextMines.begin(), counter.nextMines.end());
	row.fakeMines.assign(counter.nextFakeMines.begin(), counter.nextFakeMines.end());
	row.second = counter.lastColumnSecond;
	row.beat = counter.lastColumnBeat;

	for (int c = 0; c < columnCount_; c++)
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
		
		// write to masks
		uint16_t bit = 0x1 << c;
		if(row.notes[c].type == TapNoteType_Tap || row.notes[c].type == TapNoteType_HoldHead)
		{
			row.note_mask |= bit;
		}
		
		if(row.holds[c].type != TapNoteType_Empty)
		{
			row.hold_mask |= bit;
		}
		
		if(row.mines[c] != 0)
		{
			row.mine_mask |= bit;
		}
		if(row.fakeMines[c] != 0)
		{
			row.fake_mine_mask |= bit;
		}
		
	}
	return row;
}

int StepParityGenerator::getPermuteCacheKey(const Row &row)
{
	int mask_key = row.note_mask | row.hold_mask;
	return mask_key;
}

std::uint64_t StepParityGenerator::getStateCacheKey(State * state)
{
	std::uint64_t value = 0;
	const std::uint64_t prime = 31;
	for(Foot f : state->columns)
	{
		value *= prime;
		value += f;
	}
	for(Foot f : state->combinedColumns)
	{
		value *= prime;
		value += f;
	}
	for(Foot f : state->movedFeet)
	{
		value *= prime;
		value += f;
	}
	for(Foot f : state->holdFeet)
	{
		value *= prime;
		value += f;
	}
	return value;
}

StepParityNode * StepParityGenerator::addNode(State *state, float second, int rowIndex)
{
	StepParityNode * newNode = new StepParityNode(state, second, rowIndex);
	newNode->id = int(nodes.size());
	nodes.push_back(newNode);
	return newNode;
}

void StepParityGenerator::addEdge(StepParityNode* from, StepParityNode* to, float cost)
{
	from->neighbors[to] = cost;
}
