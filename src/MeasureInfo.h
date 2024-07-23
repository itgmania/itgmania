#ifndef MEASURE_INFO_H
#define MEASURE_INFO_H

#include "GameConstantsAndTypes.h"
class NoteData;

/** This is a container for per-measure stats of a stepchart.
 This data is calculated and saved to the song cache files as the #MEASUREINFO tag.
 This data is provided to the theme via lua functions in Steps.cpp and Trail.cpp,
 */

struct MeasureInfo
{
	int measureCount;
	float peakNps;
	std::vector<float> npsPerMeasure;
	std::vector<int> notesPerMeasure;

	MeasureInfo()
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
	static void CalculateMeasureInfo(const NoteData &in, MeasureInfo &out);
};

#endif
