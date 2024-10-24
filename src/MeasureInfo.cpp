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
		asMeasureInfo.push_back(ssprintf("%.3f", npsPerMeasure[i]));
	}

	for (unsigned i = 0; i < notesPerMeasure.size(); i++)
	{
		asMeasureInfo.push_back(ssprintf("%d", notesPerMeasure[i]));
	}

	return join(",", asMeasureInfo);
}

void MeasureInfo::FromString(const RString& sValues)
{
	std::vector<RString> asValues;
	split( sValues, ",", asValues, true );
	int half_size = static_cast<int>(asValues.size()) / 2;

	float peak_nps = 0;
	for (int i = 0; i < half_size; i++)
	{
		float nps = StringToFloat(asValues[i]);
		npsPerMeasure.push_back(nps);
		if(nps > peak_nps)
		{
			peak_nps = nps;
		}
	}
	for (unsigned i = half_size; i < asValues.size(); i++)
	{
		notesPerMeasure.push_back(StringToInt(asValues[i]));
	}
	measureCount = half_size;
	peakNps = peak_nps;
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
			// Before moving on to a new row, update the notes per measure for the current measure.
			out.notesPerMeasure[iMeasureIndexOut] += notes_this_row;
			// Update iMeasureIndex for the current row
			timing->NoteRowToMeasureAndBeat(curr_note.Row(), iMeasureIndexOut, iBeatIndexOut, iRowsRemainder);
			curr_row = curr_note.Row();
			notes_this_row = 0;
		}
		
		// Regardless of how many notes are on this row, it's only considered 1 "note" when we want to
		// calculate nps. So jumps/brackets don't inflate the nps value.
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
		
		// FIXME: We subtract the time at the current measure from the time at the next measure to determine
		// the duration of this measure in seconds, and use that to calculate notes per second.
		//
		// Measures *normally* occur over some positive quantity of seconds.  Measures that use warps,
		// negative BPMs, and negative stops are normally reported by the SM5 engine as having a duration
		// of 0 seconds, and when that happens, we safely assume that there were 0 notes in that measure.
		//
		// This doesn't always hold true.  Measures 48 and 49 of "Mudkyp Korea/Can't Nobody" use a properly
		// timed negative stop, but the engine reports them as having very small but positive durations
		// which erroneously inflates the notes per second calculation.
		//
		// As a hold over for this case, we check that the duration is <= 0.12 (instead of 0), so this only
		// breaks for cases where charts are of 2,000 BPM (which are likely rarer than those with warps).
		
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
