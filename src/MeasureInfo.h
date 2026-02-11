#ifndef MEASURE_INFO_H
#define MEASURE_INFO_H

#include <string>
#include <vector>

#include "NoteData.h"
#include "TimingData.h"

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

	std::string ToString() const;
	void FromString(const std::string& sValues );
	static void CalculateMeasureInfo(const NoteData &in, TimingData * timing, MeasureInfo &out);
};

#endif
