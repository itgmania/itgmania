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
  int measure_count_;
  float peak_nps_;
  std::vector<float> nps_per_measure_;
  std::vector<int> notes_per_measure_;

  MeasureInfo() { Zero(); }

  void Zero() {
    measure_count_ = 0;
    peak_nps_ = 0;
    nps_per_measure_.clear();
    notes_per_measure_.clear();
  }

  static void CalculateMeasureInfo(
      const NoteData& in, TimingData* timing, MeasureInfo& out);
};

#endif
