#ifndef COLUMN_CUES_H
#define COLUMN_CUES_H

#include "GameConstantsAndTypes.h"
class NoteData;

struct ColumnCueColumn
{
	int colNum;
	bool isMine;
	ColumnCueColumn()
	{
		colNum = 0;
		isMine = false;
	}
	ColumnCueColumn(int c, bool m)
	{
		colNum = c;
		isMine = m;
	}
};

struct ColumnCue
{
	float startTime;
	float duration;
	std::vector<ColumnCueColumn> columns;

	ColumnCue()
	{
		startTime = -1;
		duration = -1;
	}

	ColumnCue(float s, float d, std::vector<ColumnCueColumn> c)
	{
		startTime = s;
		duration = d;
		columns.assign(c.begin(), c.end());
	}
};

namespace ColumnCueCalculator
{
	/** @brief Calculates the set of ColumnCues for the given NoteData. Each "cue" is for any note that has a 
	 * minimum of minDuration seconds between it and the previous note on that same column.
	*/
	void CalculateColumnCues(const NoteData &in, std::vector<ColumnCue> &out, float minDuration);
};

#endif