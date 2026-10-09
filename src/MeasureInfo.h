#ifndef MEASURE_INFO_H
#define MEASURE_INFO_H

#include <string>
#include <vector>

#include "NoteData.h"
#include "TimingData.h"

// TODO: Handle non-4/4 time signatures. For now every measure is assumed to be
// 4 beats long, so charts with other time signatures have their notes/NPS
// bucketed into 4-beat "measures" rather than their true measures.
constexpr int BEATS_PER_MEASURE = 4;

struct MeasureInfo {
  int measureCount;
  float peakNps;
  std::vector<float> npsPerMeasure;
  std::vector<int> notesPerMeasure;

  MeasureInfo() { Zero(); }

  void Zero() {
    measureCount = 0;
    peakNps = 0;
    npsPerMeasure.clear();
    notesPerMeasure.clear();
  }

  static void CalculateMeasureInfo(
      const NoteData& in, TimingData* timing, MeasureInfo& out);
};

#endif
