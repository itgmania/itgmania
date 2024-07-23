#include "global.h"
#include "MeasureInfo.h"
#include "NoteData.h"
#include "RageLog.h"
#include "LocalizedString.h"
#include "LuaBinding.h"
#include "TimingData.h"
#include "GameState.h"


RString MeasureInfo::ToString() const
{
	std::vector<RString> asMeasureInfo;
	for (unsigned i = 0; i < npsPerMeasure.size(); i++)
	{
		asMeasureInfo.push_back(ssprintf("%.6f", npsPerMeasure[i]));
	}

	for (unsigned i = 0; i < notesPerMeasure.size(); i++)
	{
		asMeasureInfo.push_back(ssprintf("%d", notesPerMeasure[i]));
	}

	return join(",", asMeasureInfo);
}

void MeasureInfo::FromString(RString sValues)
{
	std::vector<RString> asValues;
	split( sValues, ",", asValues, true );
	int half_size = static_cast<int>(asValues.size()) / 2;

	float peak_nps = 0;
	for (int i = 0; i < half_size; i++)
	{
		float nps = StringToFloat(asValues[i]);
		this->npsPerMeasure.push_back(nps);
		if(nps > peak_nps)
		{
			peak_nps = nps;
		}
	}
	for (unsigned i = half_size; i < asValues.size(); i++)
	{
		this->notesPerMeasure.push_back(StringToInt(asValues[i]));
	}
	this->measureCount = half_size;
	this->peakNps = peak_nps;
}

void MeasureInfo::CalculateMeasureInfo(const NoteData &in, MeasureInfo &out)
{
	int lastRow = in.GetLastRow();
	int lastRowMeasureIndex = 0;
	int lastRowBeatIndex = 0;
	int lastRowRemainder = 0;
	TimingData *timing = GAMESTATE->GetProcessedTimingData();
	timing->NoteRowToMeasureAndBeat(lastRow, lastRowMeasureIndex, lastRowBeatIndex, lastRowRemainder);

	int totalMeasureCount = lastRowMeasureIndex + 1;
	
	out.notesPerMeasure.clear();
	out.npsPerMeasure.clear();
	
	out.notesPerMeasure.resize(totalMeasureCount, 0);
	out.npsPerMeasure.resize(totalMeasureCount, 0);
	
	NoteData::all_tracks_const_iterator curr_note = in.GetTapNoteRangeAllTracks(0, MAX_NOTE_ROW);

	int iMeasureIndexOut = 0;
	int iBeatIndexOut = 0;
	int iRowsRemainder = 0;

	float peak_nps = 0;
	int curr_row = -1;
	int notes_this_row = 0;

	while (!curr_note.IsAtEnd())
	{
		if(curr_note.Row() != curr_row)
		{
			// Before moving on to a new row, update the row count for the "current" measure
			// Note that we're only add
			out.notesPerMeasure[iMeasureIndexOut] += notes_this_row;
			// Update iMeasureIndex for the current row
			timing->NoteRowToMeasureAndBeat(curr_note.Row(), iMeasureIndexOut, iBeatIndexOut, iRowsRemainder);
			curr_row = curr_note.Row();
			notes_this_row = 0;
		}
		// Update tap and mine count for the current measure
		// Regardless of how many notes are on this row, it's only considered 1 "note" when we want to
		// calculate nps. So jumps/brackets don't inflate the nps value. Maybe we should call it
		// "steps per second" or something like that?
		if (curr_note->type == TapNoteType_Tap || curr_note->type == TapNoteType_HoldHead)
		{
			notes_this_row = 1;
		}
		++curr_note;
	}
	
	// And handle the final note...
	out.notesPerMeasure[iMeasureIndexOut] += notes_this_row;

	// Now that all of the notes have been parsed, calculate nps for each measure
	for (int m = 0; m < totalMeasureCount; m++)
	{
		if(out.notesPerMeasure[m] == 0)
		{
			out.npsPerMeasure[m] = 0;
			continue;
		}
		
		float measureDuration = timing->GetElapsedTimeFromBeat(4 * (m+1)) - timing->GetElapsedTimeFromBeat(4 * m);
		float nps = out.notesPerMeasure[m] / measureDuration;
		
		if(measureDuration < 0.12)
		{
			out.npsPerMeasure[m] = 0;
		}
		else
		{
			out.npsPerMeasure[m] = nps;
		}

		if(nps > peak_nps)
		{
			peak_nps = nps;
		}
	}

	out.peakNps = peak_nps;
	out.measureCount = totalMeasureCount;
}
