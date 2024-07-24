#include "global.h"
#include "StepParityDatastructs.h"

using namespace StepParity;


//
// Graph/Node methods
//
int calculateVectorHash(const std::vector<Foot> &vec)
{
	int value = 0;
	for(Foot f : vec)
	{
		value *= 5;
		value += f;
	}
	return value;
}

bool State::operator==(const State &other) const
{
   return rowIndex == other.rowIndex &&
   columns == other.columns &&
   movedFeet == other.movedFeet &&
   holdFeet == other.holdFeet;
}

bool State::operator<(const State &other) const
{

	if(rowIndex != other.rowIndex) {
		return rowIndex < other.rowIndex;
	}
	if(columnsHash != other.columnsHash) {
		return columnsHash < other.columnsHash;
	}
	if(movedFeetHash != other.movedFeetHash) {
		return movedFeetHash < other.movedFeetHash;
	}
	if(holdFeetHash != other.holdFeetHash) {
		return holdFeetHash < other.holdFeetHash;
	}
	return false;
}

void State::calculateHashes()
{
	columnsHash = calculateVectorHash(columns);
	movedFeetHash = calculateVectorHash(movedFeet);
	holdFeetHash = calculateVectorHash(holdFeet);
}

StepParityNode * StepParityGraph::addOrGetExistingNode(const State &state)
{
	// this is silly, but the start node has a rowIndex of -1
	// which doesn't work as an array index.
	int rowIndex = state.rowIndex + 1; 
	while (static_cast<int>(stateNodeMap.size()) <= rowIndex)
	{
		stateNodeMap.push_back(std::map<State, StepParityNode *, StateComparator>());
	}
	
	if(stateNodeMap[rowIndex].find(state) == stateNodeMap[rowIndex].end())
	{
		StepParityNode* newNode = new StepParityNode(state);
        newNode->id = int(nodes.size());
        nodes.push_back(newNode);
        newNode->state.idx = int(states.size());
        states.push_back(&(newNode->state));
		stateNodeMap[rowIndex][state] = newNode;
	}

	return stateNodeMap[rowIndex][state];
}


//
// Json methods
//


template<typename Container>
Json::Value FeetToJson(const Container& feets, bool useStrings)
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

Json::Value State::ToJson(bool useStrings)
{
	Json::Value root;
	Json::Value jsonColumns = FeetToJson(columns, useStrings);
	Json::Value jsonMovedFeet = FeetToJson(movedFeet, useStrings);
	Json::Value jsonHoldFeet = FeetToJson(holdFeet, useStrings);
    
    root["idx"] = idx;
    root["columns"] = jsonColumns;
	root["movedFeet"] = jsonMovedFeet;
	root["holdFeet"] = jsonHoldFeet;
	root["second"] = second;
	root["rowIndex"] = rowIndex;
	return root;
}

Json::Value IntermediateNoteData::ToJson(bool useStrings)
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

Json::Value Row::ToJson(bool useStrings)
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

	return root;
}

Json::Value StepParityNode::ToJson()
{
	Json::Value root;
	Json::Value jsonNeighbors;

	for (auto it = neighbors.begin(); it != neighbors.end(); it++)
	{
		Json::Value n;
		n["id"] = it->first->id;
        Json::Value jsonCosts;
        float * costs = it->second;
        for(int i = 0; i < NUM_Cost; i++)
        {
            jsonCosts[COST_LABELS[i]] = costs[i];
        }
        n["costs"] = jsonCosts;
		jsonNeighbors.append(n);
	}
	root["id"] = id;
    root["stateIdx"] = state.idx;
	root["neighbors"] = jsonNeighbors;
	return root;
}


Json::Value Row::ToJsonRows(const std::vector<Row> & rows, bool useStrings)
{
	Json::Value root;
	for(Row row: rows)
	{
		root.append(row.ToJson(useStrings));
	}
	return root;
}

Json::Value Row::ParityRowsJson(const std::vector<Row> & rows)
{
	Json::Value root;
	for(Row row: rows)
	{
		Json::Value pr;
		for(IntermediateNoteData note: row.notes)
		{
			pr.append(static_cast<int>(note.parity));
		}
		root.append(pr);
	}
	return root;
}


Json::Value StepParityGraph::ToJson()
{
    Json::Value jsonNodes;
	for(auto node: nodes)
	{
        jsonNodes.append(node->ToJson());
	}
    Json::Value jsonStates;
    for(auto state: states)
    {
        jsonStates.append(state->ToJson(false));
    }
    
    Json::Value root;
    root["nodes"] = jsonNodes;
    root["states"] = jsonStates;
	return root;
}

Json::Value StepParityGraph::NodeStateJson()
{
	Json::Value root;
	for(auto node: nodes)
	{
		Json::Value nodeJson;
		nodeJson["id"] = node->id;
		nodeJson["state"] = node->state.ToJson(false);
		root.append(nodeJson);
	}
	return root;
}
