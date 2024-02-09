#include "global.h"
#include "StepParityDatastructs.h"

using namespace StepParity;


//
// Graph/Node methods
//



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
		n["cost"] = it->second;
		jsonNeighbors.append(n);
	}
		root["id"] = id;
		root["neighbors"] = jsonNeighbors;
		return root;
}


Json::Value Row::ToJsonRows(std::vector<Row> rows, bool useStrings)
{
	Json::Value root;
	for(Row row: rows)
	{
		root.append(row.ToJson(useStrings));
	}
	return root;
}

Json::Value Row::ParityRowsJson(std::vector<Row> rows)
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
	Json::Value root;
	for(auto node: nodes)
	{
		root.append(node->ToJson());
	}
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