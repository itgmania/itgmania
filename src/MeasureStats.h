#ifndef MEASURE_STATS_H
#define MEASURE_STATS_H

#include "GameConstantsAndTypes.h"
class NoteData;

/** @brief Container for stats on a given measure */

struct MeasureStat
{
	int startRow;
	int endRow;
	int rowCount;
	int tapCount;
	int mineCount;
	float nps;
	float duration;

	MeasureStat()
	{
		startRow = -1;
		endRow = -1;
		rowCount = 0;
		tapCount = 0;
		mineCount = 0;
		nps = 0;
		duration = 0;
	}

	bool operator==( const MeasureStat& other ) const
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

	bool operator!=(const MeasureStat& other ) const
	{
		return !this->operator==(other);
	}
};

struct MeasureStats
{
	int measureCount;
	float peakNps;
	std::vector<float> npsPerMeasure;
	std::vector<int> notesPerMeasure;

	MeasureStats()
	{
		Zero();
	}
	
	void Zero()
	{
		measureCount = 0;
		peakNps = 0;
		npsPerMeasure.clear();
		notesPerMeasure.clear();
	}

	RString ToString() const;
	void FromString( RString sValues );
	void PushSelf( lua_State *L );
};

namespace MeasureStatsCalculator
{
	void CalculateMeasureStats(const NoteData &in, MeasureStats &out);
	/** @brief Returns an array containing the count of notes for each measure.	*/
	std::vector<int> notesPerMeasure(std::vector<MeasureStat> stats);
	
	/** @brief Returns an array containing the notes-per-second for each measure. */
	std::vector<float> npsPerMeasure(std::vector<MeasureStat> stats);
};

#endif