#include "MeasureInfo.h"

#include <string>
#include <vector>

#include "GameState.h"
#include "NoteData.h"
#include "NoteTypes.h"
#include "RageUtil.h"

static const int kRowsPerMeasure = kRowsPerBeat * kBeatsPerMeasure;

void MeasureInfo::CalculateMeasureInfo(
    const NoteData& in, TimingData* timing, MeasureInfo& out) {
  int last_row = in.GetLastRow();
  int total_measure_count = last_row / kRowsPerMeasure + 1;

  out.notes_per_measure_.clear();
  out.nps_per_measure_.clear();

  out.notes_per_measure_.resize(total_measure_count, 0);
  out.nps_per_measure_.resize(total_measure_count, 0);

  NoteData::all_tracks_const_iterator curr_note =
      in.GetTapNoteRangeAllTracks(0, kMaxNoteRow);

  int measure_index_out = 0;

  float peak_nps = 0;
  int curr_row = -1;
  int notes_this_row = 0;

  while (!curr_note.IsAtEnd()) {
    if (curr_note.Row() != curr_row) {
      if (timing->IsFakeAtRow(curr_row) || timing->IsWarpAtRow(curr_row)) {
        // Don't include fakes and warped-over notes in the count.
        notes_this_row = 0;
      }
      // Before moving on to a new row, update the notes per measure for the
      // current measure.
      out.notes_per_measure_[measure_index_out] += notes_this_row;
      // Update iMeasureIndex for the current row.
      measure_index_out = curr_note.Row() / kRowsPerMeasure;
      curr_row = curr_note.Row();
      notes_this_row = 0;
    }

    // Regardless of how many notes are on this row, it's only considered 1
    // "note" when we want to calculate nps. So jumps/brackets don't inflate the
    // nps value.
    if (curr_note->type_ == TapNoteType_Tap ||
        curr_note->type_ == TapNoteType_HoldHead) {
      notes_this_row = 1;
    }
    ++curr_note;
  }

  // And handle the final note...
  out.notes_per_measure_[measure_index_out] += notes_this_row;

  // Now that all of the notes have been parsed, calculate nps for each measure
  for (int m = 0; m < total_measure_count; m++) {
    if (out.notes_per_measure_[m] == 0) {
      out.nps_per_measure_[m] = 0;
      continue;
    }

    float measure_duration =
        timing->GetElapsedTimeFromBeat(kBeatsPerMeasure * (m + 1)) -
        timing->GetElapsedTimeFromBeat(kBeatsPerMeasure * m);

    // FIXME: We subtract the time at the current measure from the time at the
    // next measure to determine the duration of this measure in seconds, and
    // use that to calculate notes per second.
    //
    // Measures *normally* occur over some positive quantity of seconds.
    // Measures that use warps, negative BPMs, and negative stops are normally
    // reported by the SM5 engine as having a duration of 0 seconds, and when
    // that happens, we safely assume that there were 0 notes in that measure.
    //
    // This doesn't always hold true.  Measures 48 and 49 of "Mudkyp Korea/Can't
    // Nobody" use a properly timed negative stop, but the engine reports them
    // as having very small but positive durations which erroneously inflates
    // the notes per second calculation.
    //
    // As a hold over for this case, we check that the duration is <= 0.12
    // (instead of 0), so this only breaks for cases where charts are of 2,000
    // BPM (which are likely rarer than those with warps).
    float nps = measure_duration < 0.12
                    ? 0
                    : (out.notes_per_measure_[m] / measure_duration);
    out.nps_per_measure_[m] = nps;

    if (nps > peak_nps) {
      peak_nps = nps;
    }
  }

  out.peak_nps_ = peak_nps;
  out.measure_count_ = total_measure_count;
}
