#include "global.h"
#include "ColumnCues.h"
#include "GameState.h"
#include "LuaBinding.h"
#include "NoteData.h"
#include "TimingData.h"

void ColumnCues::CalculateColumnCues(const NoteData &in, ColumnCues &out, float minDuration)
{
	TimingData *timing = GAMESTATE->GetProcessedTimingData();
	NoteData::all_tracks_const_iterator curr_note = in.GetTapNoteRangeAllTracks(0, MAX_NOTE_ROW);
	int curr_row = -1;

	std::vector<ColumnCue> allColumnCues;
	ColumnCue currentCue = ColumnCue();

	while (!curr_note.IsAtEnd())
	{
		if(curr_note.Row() != curr_row)
		{
			if (currentCue.startTime != -1 )
			{
				allColumnCues.push_back(currentCue);
			}
			currentCue = ColumnCue();
			currentCue.startTime = timing->GetElapsedTimeFromBeat(NoteRowToBeat(curr_note.Row()));
			curr_row = curr_note.Row();
		}

		if (curr_note->type == TapNoteType_Tap || curr_note->type == TapNoteType_HoldHead)
		{
			currentCue.columns.push_back(ColumnCueColumn(curr_note.Track() + 1, false));
		}
		else if(curr_note->type == TapNoteType_Mine)
		{
			currentCue.columns.push_back(ColumnCueColumn(curr_note.Track() + 1, true));
		}
		
		++curr_note;
	}

	// If there's a remaining columnCue from the last row, add it
	if( currentCue.startTime != -1)
	{
		allColumnCues.push_back(currentCue);
	}

	float previousCueTime = 0;
	std::vector<ColumnCue> columnCues;
	for( ColumnCue columnCue : allColumnCues )
	{
		float duration = columnCue.startTime - previousCueTime;
		if( duration > minDuration || previousCueTime == 0)
		{
			columnCues.push_back(ColumnCue(previousCueTime, duration, columnCue.columns));
		}
		previousCueTime = columnCue.startTime;
	}

	out.columnCues.clear();
	out.columnCues.assign(columnCues.begin(), columnCues.end());
}



class LunaColumnCues: public Luna<ColumnCues>
{
	
public:
	// Build an array that matches the structure of ColumnCues that's returned
	// by GetMeasureInfo() in SL-ChartParser. This way we don't need to touch
	// anywhere else that uses this data.
	static int get_column_cues(T *p, lua_State *L)
	{
		lua_createtable(L, p->columnCues.size(), 0);

		for (unsigned i = 0; i < p->columnCues.size(); i++)
		{
			lua_newtable(L);
			lua_pushstring(L, "startTime");
			lua_pushnumber(L, p->columnCues[i].startTime);
			lua_settable(L, -3);

			lua_pushstring(L, "duration");
			lua_pushnumber(L, p->columnCues[i].duration);
			lua_settable(L, -3);

			lua_pushstring(L, "columns");
			lua_createtable(L, p->columnCues[i].columns.size(), 0);

			for (unsigned c = 0; c < p->columnCues[i].columns.size(); c++)
			{
				lua_newtable(L);
				lua_pushstring(L, "colNum");
				lua_pushinteger(L, p->columnCues[i].columns[c].colNum);
				lua_settable(L, -3);

				lua_pushstring(L, "isMine");
				lua_pushboolean(L, p->columnCues[i].columns[c].isMine);
				lua_settable(L, -3);

				lua_rawseti(L, -2, c + 1);
			}

			lua_settable(L, -3);
			lua_rawseti(L, -2, i + 1);
		}
		return 1;
	}

	LunaColumnCues()
	{
		ADD_METHOD(get_column_cues);
	}
};

LUA_REGISTER_CLASS( ColumnCues )