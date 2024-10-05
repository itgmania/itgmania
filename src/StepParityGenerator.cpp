#include "global.h"
#include "StepParityGenerator.h"
#include "StepParityCost.h"
#include "NoteData.h"
#include "TechCounts.h"
#include "GameState.h"

using namespace StepParity;

bool StepParityGenerator::analyzeNoteData(const NoteData &in)
{
	columnCount = in.GetNumTracks();
	
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
	beginningState = new State(columnCount);
	startNode = addNode(beginningState, rows[0].second - 1, -1);
	
	std::queue<StepParityNode *> previousNodes;
	previousNodes.push(startNode);
	StepParityCost costCalculator(layout);

	for (unsigned long i = 0; i < rows.size(); i++)
	{
		std::vector<StepParityNode *> resultNodes;
		Row &row = rows[i];
		std::vector<FootPlacement> *permutations = getFootPlacementPermutations(row);
				
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
	endingState = new State(columnCount);
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
	State * resultState = new State(row.columnCount);
	resultState->columns = columns;
	
	
	// I tried to condense this, but kept getting the logic messed up
	for (unsigned long i = 0; i < columns.size(); i++)
	{
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
	}
	
	mergeInitialAndResultPosition(initialState, resultState, (int)columns.size());
	
	std::uint64_t stateHash = getStateCacheKey(resultState);
	
	auto maybeState = stateCache.find(stateHash);
	
	if(maybeState != stateCache.end())
	{
		State* cachedState = maybeState->second;
		delete resultState;
		return maybeState->second;
	}
	
	stateCache.insert({stateHash, resultState});
	
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

// For the given row, generate all of the possible foot placements
// (even if they're not physically possible)
//
// We cache this data and return a pointer for two reasons:
// - It takes a long time to generate the permutations
// - We end up generating a lot of redundant data, so caching it saves memory

std::vector<FootPlacement>* StepParityGenerator::getFootPlacementPermutations(const Row &row)
{
	int cacheKey = getPermuteCacheKey(row);	
	auto maybePermuteFootPlacements = permuteCache.find(cacheKey);
	
	if (maybePermuteFootPlacements == permuteCache.end())
	{
		FootPlacement blankColumns(row.columnCount, NONE);
		std::vector<FootPlacement> computedPermutations = PermuteFootPlacements(row, blankColumns, 0, false);
		
		// if we didn't get any permutations, try again, ignoring holds
		if(computedPermutations.size() == 0)
		{
			computedPermutations = PermuteFootPlacements(row, blankColumns, 0, true);
		}
		// and if we _still_ don't have any permutations, just return a blank row
		// (this will all buildStateGraph to at least generate a fully connected graph)
		if(computedPermutations.size() == 0)
		{
			computedPermutations.push_back(blankColumns);
		}
		permuteCache[cacheKey] = std::move(computedPermutations);
		
	}
	return &permuteCache[cacheKey];
}

// Recursively generate each permutation for the given row.
std::vector<FootPlacement> StepParityGenerator::PermuteFootPlacements(const Row &row, FootPlacement columns, unsigned long column, bool ignoreHolds)
{
	// If column >= columns.size(), we've reached the end of the row.
	// Perform some final validation before returning the contents of columns
	if (column >= columns.size())
	{
		int leftHeelIndex = StepParity::INVALID_COLUMN;
		int leftToeIndex = StepParity::INVALID_COLUMN;
		int rightHeelIndex = StepParity::INVALID_COLUMN;
		int rightToeIndex = StepParity::INVALID_COLUMN;
		
		for (unsigned long i = 0; i < columns.size(); i++)
		{
			if (columns[i] == NONE)
			{
				continue;
			}
			if (columns[i] == LEFT_HEEL)
			{
				leftHeelIndex = i;
			}
			if (columns[i] == LEFT_TOE)
			{
				leftToeIndex = i;
			}
			if (columns[i] == RIGHT_HEEL)
			{
				rightHeelIndex = i;
			}
			if (columns[i] == RIGHT_TOE)
			{
				rightToeIndex = i;
			}
		}
		
		// Filter out actually invalid combinations:
		// - We don't want permutations where the toe is on an arrow, but not the heel
		// - We don't want impossible brackets (eg you can't bracket up and down)
		if (
			(leftHeelIndex == StepParity::INVALID_COLUMN && leftToeIndex != StepParity::INVALID_COLUMN) ||
			(rightHeelIndex == StepParity::INVALID_COLUMN && rightToeIndex != StepParity::INVALID_COLUMN))
		{
			return std::vector<FootPlacement>();
		}
		if (leftHeelIndex != StepParity::INVALID_COLUMN && leftToeIndex != StepParity::INVALID_COLUMN)
		{
			if (!layout.bracketCheck(leftHeelIndex, leftToeIndex))
			{
				return std::vector<FootPlacement>();
			}
		}
		if (rightHeelIndex != StepParity::INVALID_COLUMN && rightToeIndex != StepParity::INVALID_COLUMN)
		{
			if (!layout.bracketCheck(rightHeelIndex, rightToeIndex))
			{
				return std::vector<FootPlacement>();
			}
		}
		return {columns};
	}

	// If this column has a valid tap/hold head, or is actively holding a note,
	// iterate through values of StepParity::Foot. For each foot part, check that
	// it's not already present in columns, and if not, create a copy of columns,
	// and set the current foot part to the current column.
	// Then pass it to PermuteFootPlacements() and increment the column index.
	// Collect each permutationm, and then return all of them.
	//
	// The `ignoreHolds` flag is used as a workaround for situations where
	// we can't find a valid foot placement that allows us to continue the holds
	// (BREACH PROTOCOL doubles has  a row 0311 1000 which isn't bracketable
	// while still holding p1 down)
	std::vector<FootPlacement> permutations;
	if (row.notes[column].type != TapNoteType_Empty  ||
			(ignoreHolds == false && row.holds[column].type != TapNoteType_Empty))
	{
	  	  for (StepParity::Foot foot: FEET) {
		if(std::find(columns.begin(), columns.end(), foot) != columns.end())
		{
			continue;
		}

		FootPlacement newColumns = columns;

		newColumns[column] = foot;
		std::vector<FootPlacement> p = PermuteFootPlacements(row, newColumns, column + 1, ignoreHolds);
		permutations.insert(permutations.end(), p.begin(), p.end());
	  	  }
	  	  return permutations;
	}
	// If the current column doesn't have any taps or holds,
	// then we don't need to generate any permutations for it.
	// Return the contents of calling PermuteFootPlacements() for the next column.
	return PermuteFootPlacements(row, columns, column + 1, ignoreHolds);
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
			counter.notes = std::vector<IntermediateNoteData>(columnCount);
			counter.mines = std::vector<float>(columnCount);
			counter.fakeMines = std::vector<float>(columnCount);

			// Reset any now-inactive holds to empty values.
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
