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
	unsigned half_size = asValues.size() / 2;

	float peak_nps = 0;
	for (unsigned i = 0; i < half_size; i++)
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

void MeasureInfoCalculator::CalculateMeasureInfo(const NoteData &in, MeasureInfo &out)
{
	int lastRow = in.GetLastRow();
	int lastRowMeasureIndex = 0;
	int lastRowBeatIndex = 0;
	int lastRowRemainder = 0;
	TimingData *timing = GAMESTATE->GetProcessedTimingData();
	timing->NoteRowToMeasureAndBeat(lastRow, lastRowMeasureIndex, lastRowBeatIndex, lastRowRemainder);

	int totalMeasureCount = lastRowMeasureIndex + 1;

	std::vector<MeasureStat> counters(totalMeasureCount, MeasureStat());
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
			// Before moving on to a new row, update the row count, start and end rows for the "current" measure
			counters[iMeasureIndexOut].rowCount += 1;
			counters[iMeasureIndexOut].endRow = curr_row;
			counters[iMeasureIndexOut].tapCount += (notes_this_row > 0 ? 1 : 0);
			if (counters[iMeasureIndexOut].startRow == -1)
			{
				counters[iMeasureIndexOut].startRow = curr_row;
			}

			// Update iMeasureIndex for the current row
			timing->NoteRowToMeasureAndBeat(curr_note.Row(), iMeasureIndexOut, iBeatIndexOut, iRowsRemainder);
			curr_row = curr_note.Row();
			notes_this_row = 0;
		}
		// Update tap and mine count for the current measure
		if (curr_note->type == TapNoteType_Tap || curr_note->type == TapNoteType_HoldHead)
		{
			notes_this_row += 1;
		}
		else if(curr_note->type == TapNoteType_Mine)
		{
			counters[iMeasureIndexOut].mineCount += 1;
		}
		
		++curr_note;
	}
	// And handle the final note...
	counters[iMeasureIndexOut].rowCount += 1;
	counters[iMeasureIndexOut].endRow = curr_row;
	counters[iMeasureIndexOut].tapCount += (notes_this_row > 0 ? 1 : 0);

	// Now that all of the notes have been parsed, calculate nps for each measure
	for (unsigned m = 0; m < counters.size(); m++)
	{
		if(counters[m].rowCount == 0)
		{
			counters[m].nps = 0;
			continue;
		}
		
		float measureDuration = timing->GetElapsedTimeFromBeat(4 * (m+1)) - timing->GetElapsedTimeFromBeat(4 * m);
		counters[m].duration = measureDuration;
		if(measureDuration < 0.12)
		{
			counters[m].nps = 0;
		}
		else
		{
			counters[m].nps = counters[m].tapCount / measureDuration;
		}

		if(counters[m].nps > peak_nps)
		{
			peak_nps = counters[m].nps;
		}
	}

	out.peakNps = peak_nps;
	out.measureCount = totalMeasureCount;
	std::vector<int> notesPerMeasure = MeasureInfoCalculator::notesPerMeasure(counters);
	std::vector<float> npsPerMeasure = MeasureInfoCalculator::npsPerMeasure(counters);
	out.notesPerMeasure.clear();
	out.notesPerMeasure.assign(notesPerMeasure.begin(), notesPerMeasure.end());
	out.npsPerMeasure.clear();
	out.npsPerMeasure.assign(npsPerMeasure.begin(), npsPerMeasure.end());
}

std::vector<int> MeasureInfoCalculator::notesPerMeasure(std::vector<MeasureStat> stats)
{
	std::vector<int> notesPerMeasure(stats.size(), 0);
	for (unsigned i = 0; i < stats.size(); i++)
	{
		notesPerMeasure[i] = stats[i].tapCount;
	}

	return notesPerMeasure;
}

std::vector<float> MeasureInfoCalculator::npsPerMeasure(std::vector<MeasureStat> stats)
{
	std::vector<float> npsPerMeasure(stats.size(), 0);
	for (unsigned i = 0; i < stats.size(); i++)
	{
		npsPerMeasure[i] = stats[i].nps;
	}
	return npsPerMeasure;
}
