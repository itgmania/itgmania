#ifndef TECH_STATS_H
#define TECH_STATS_H

#include "GameConstantsAndTypes.h"
class NoteData;

enum Foot
{
	Foot_None = -1,
	Foot_Left = 0,
	Foot_Right
};


// This could probably be an enum?
typedef int StepDirection;
const StepDirection StepDirection_None = 0;
const StepDirection StepDirection_Left = 1;
const StepDirection StepDirection_Down = 2;
const StepDirection StepDirection_Up = 4;
const StepDirection StepDirection_Right = 8;

const StepDirection StepDirection_LR = StepDirection_Left | StepDirection_Right;
const StepDirection StepDirection_UD = StepDirection_Up | StepDirection_Down;
const StepDirection StepDirection_LD = StepDirection_Left | StepDirection_Down;
const StepDirection StepDirection_LU = StepDirection_Left | StepDirection_Up;
const StepDirection StepDirection_RD = StepDirection_Right | StepDirection_Down;
const StepDirection StepDirection_RU = StepDirection_Right | StepDirection_Up;

/** @brief Converts a track index into a StepDirection. Note that it only supports 4-panel play at the moment. */
inline int TrackIntToStepDirection(int track)
{
	return ((track > 3 || track < 0) ? -1 : pow(2, track) );
}

/** @brief convenience method for checking if the given StepDirection is a single arrow */
inline bool IsSingleStep(StepDirection s)
{
	return ((s == StepDirection_Left) || (s == StepDirection_Down) || (s == StepDirection_Up) || (s == StepDirection_Right));
}

/** @brief convenience method for checking if the given StepDirection is a jump */
inline bool IsJump(StepDirection s)
{
	return ( (s == StepDirection_LR) || (s == StepDirection_UD) || (s == StepDirection_LD) || (s == StepDirection_LU) || (s == StepDirection_RD) || (s == StepDirection_RU) );
}

/** @brief convenience method for checking if `instep` contains `step`. 
 * Examples:
 * instep = StepDirection_LR, step = StepDirection_Left => true
 * instep = StepDirection_Left, step = StepDirection_LR => false
 * instep = StepDirectio_LR, step = StepDirection_LU => false
*/
inline bool StepContainsStep(StepDirection instep, StepDirection step)
{
	return (instep & step) == step;
}

/** @brief convenience method for "switching" the value for the given Foot. If f == Foot_None, this will return Foot_Right. */
inline Foot SwitchFeet(Foot f) 
{
	return ((f == Foot_None || f == Foot_Left) ? Foot_Right : Foot_Left);
}

enum TechStatsCategory
{
	TechStatsCategory_Crossovers = 0,
	TechStatsCategory_Footswitches,
	TechStatsCategory_Sideswitches,
	TechStatsCategory_Jacks,
	TechStatsCategory_Brackets,
	TechStatsCategory_PeakNps,
	TechStatsCategory_MeasureCount,
	TechStatsCategory_NotesPerMeasure,
	TechStatsCategory_NpsPerMeasure,
	NUM_TechStatsCategory,
	TechStatsCategory_Invalid
};

const RString& TechStatsCategoryToString( TechStatsCategory cat );
/**
 * @brief Turn the radar category into a proper localized string.
 * @param cat the radar category.
 * @return the localized string version of the radar category.
 */
const RString& TechStatsCategoryToLocalizedString( TechStatsCategory cat );
LuaDeclareType( TechStatsCategory );

struct lua_State;

/** @brief Container for stats on a given measure */

struct MeasureStats
{
	int startRow;
	int endRow;
	int rowCount;
	int tapCount;
	int mineCount;
	float nps;
	float duration;

	MeasureStats()
	{
		startRow = -1;
		endRow = -1;
		rowCount = 0;
		tapCount = 0;
		mineCount = 0;
		nps = 0;
		duration = 0;
	}

	bool operator==( const MeasureStats& other ) const
	{
		return (this->startRow == other.startRow
			&& this->endRow == other.endRow
			&& this->rowCount == other.rowCount
			&& this->tapCount == other.tapCount
			&& this->mineCount == other.mineCount
			&& this->nps == other.nps
			&& this->duration == other.duration
			);
	}

	bool operator!=(const MeasureStats& other ) const
	{
		return !this->operator==(other);
	}
};

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
	void PushSelf( lua_State *L );
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

	void PushSelf( lua_State *L );
};

/** @brief Technical statistics */
struct TechStats
{
public:
	int crossovers; // Number of crossovers in song
	int footswitches; // Number of footswitches in song
	int sideswitches; // Number of sideswitches in song
	int jacks; // Number of jacks in song
	int brackets; // Number of brackets in song
	int measureCount; // Number of measures in song
	float peakNps; // peak notes-per-second

	std::vector<MeasureStats> measureInfo; // Contains info for each measure in song
	std::vector<ColumnCue> columnCues;

	TechStats()
	{
		crossovers = 0;
		footswitches = 0;
		sideswitches = 0;
		jacks = 0;
		brackets = 0;
		measureCount = 0;
		peakNps = 0;
		measureInfo.clear();
	}

	void Zero() {
		crossovers = 0;
		footswitches = 0;
		sideswitches = 0;
		jacks = 0;
		brackets = 0;
		measureCount = 0;
		peakNps = 0;
		measureInfo.clear();
		columnCues.clear();
	}

	void updatePeakNps()
	{
		peakNps = 0;
		for (MeasureStats entry : measureInfo)
		{
			if(entry.nps > peakNps)
			{
				peakNps = entry.nps;
			}
		}
	}

	/** @brief Returns an array containing the count of notes for each measure.	*/
	std::vector<int> notesPerMeasure();
	
	/** @brief Returns an array containing the notes-per-second for each measure. */
	std::vector<float> npsPerMeasure();

	TechStats& operator+=( const TechStats& other )
	{
		this->crossovers += other.crossovers;
		this->footswitches += other.footswitches;
		this->sideswitches += other.sideswitches;
		this->jacks += this->jacks;
		this->brackets += other.brackets;
		this->measureCount += other.measureCount;
		this->measureInfo.insert(this->measureInfo.end(), other.measureInfo.begin(), other.measureInfo.end());
		this->columnCues.insert(this->columnCues.end(), other.columnCues.begin(), other.columnCues.end());
		this->updatePeakNps();
		return *this;
	}

	bool operator==( const TechStats& other ) const
	{
		return (this->crossovers == other.crossovers 
			&& this->footswitches == other.footswitches 
			&& this->sideswitches == other.sideswitches 
			&& this->jacks == other.jacks
			&& this->brackets == other.brackets
			&& this->measureInfo == other.measureInfo
			);
	}

	bool operator!=( const TechStats& other ) const
	{
		return !operator==( other );
	}

	RString stringDescription();

	void PushSelf( lua_State *L );
};

/** @brief a counter used to keep track of state while parsing data in TechStatsCalculator::CalculateTechStats*/
struct TechStatsCounter
{

	bool wasLastStreamFlipped;
	bool anyStepsSinceLastCommitStream;
	bool justBracketed;
	bool lastFlip;

	std::vector<bool> stepsLr;

	Foot lastFoot;
	Foot trueLastFoot;

	StepDirection lastStep;
	StepDirection lastRepeatedFoot;
	StepDirection lastArrowL;
	StepDirection lastArrowR;

	StepDirection trueLastArrowL;
	StepDirection trueLastArrowR;

	int recursionCount;
	TechStatsCounter()
	{
		lastFoot = Foot_Left;
		wasLastStreamFlipped = false;
		lastStep = StepDirection_None;
		lastRepeatedFoot = StepDirection_None;

		anyStepsSinceLastCommitStream = false;
		
		lastArrowL = StepDirection_None;
		lastArrowR = StepDirection_None;
		
		trueLastArrowL = StepDirection_None;
		trueLastArrowR = StepDirection_None;

		trueLastFoot = Foot_None;
		justBracketed = false;
		lastFlip = false;

		recursionCount = 0;
	}
};


namespace TechStatsCalculator
{
	void CalculateTechStats(const NoteData &in, TechStats &out);
	void UpdateTechStats(TechStats &stats, TechStatsCounter &counter, StepDirection currentStep);
	void CommitStream(TechStats &stats, TechStatsCounter &counter, Foot tieBreaker);
	void CalculateMeasureInfo(const NoteData &in, TechStats &stats);
};

#endif

/**
 * @file
 * @author Michael Votaw (c) 2023
 * @section LICENSE
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, provided that the above
 * copyright notice(s) and this permission notice appear in all copies of
 * the Software and that both the above copyright notice(s) and this
 * permission notice appear in supporting documentation.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
 * INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
