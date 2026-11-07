#ifndef COLUMN_CUES_H
#define COLUMN_CUES_H

#include <vector>

#include "NoteData.h"
#include "NoteTypes.h"
#include "PlayerNumber.h"
#include "TimingData.h"

/* ColumnCues are used to indicate to the player which column the next note will
 occur after a long gap in the stepchart. This info is made available to the
 theme via lua functions in Steps.cpp
 */
struct ColumnCueColumn {
  int col_num_;
  TapNoteType note_type_;
  ColumnCueColumn(int c, TapNoteType n) {
    col_num_ = c;
    note_type_ = n;
  }
};

struct ColumnCue {
  float start_time_;
  float duration_;
  std::vector<ColumnCueColumn> columns_;

  ColumnCue() {
    start_time_ = -1;
    duration_ = -1;
  }

  ColumnCue(float s, float d, std::vector<ColumnCueColumn> c) {
    start_time_ = s;
    duration_ = d;
    columns_.assign(c.begin(), c.end());
  }
  /** @brief Calculates the set of ColumnCues for the given NoteData. Each "cue"
   * is for any note that has a minimum of minDuration seconds between it and
   * the previous note on that same column.
   */
  static void CalculateColumnCues(
      const NoteData& in, TimingData* timing, std::vector<ColumnCue>& out,
      PlayerNumber pn, float min_duration, StepsType steps_type);
};

#endif
