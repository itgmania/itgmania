#include "ColumnCues.h"

#include <vector>

#include "GameConstantsAndTypes.h"
#include "GameState.h"
#include "NoteData.h"
#include "NoteDataUtil.h"
#include "NoteTypes.h"

namespace {
void CalculateColumnCuesOnePlayer(
    const NoteData& in, TimingData* timing, std::vector<ColumnCue>& out,
    float min_duration) {
  NoteData::all_tracks_const_iterator curr_note =
      in.GetTapNoteRangeAllTracks(0, kMaxNoteRow);
  int curr_row = -1;

  std::vector<ColumnCue> all_column_cues;
  ColumnCue current_cue;

  while (!curr_note.IsAtEnd()) {
    if (curr_note.Row() != curr_row) {
      if (current_cue.start_time_ != -1) {
        all_column_cues.push_back(current_cue);
      }
      current_cue = ColumnCue();
      current_cue.start_time_ =
          timing->GetElapsedTimeFromBeat(NoteRowToBeat(curr_note.Row()));
      curr_row = curr_note.Row();
    }

    if (curr_note->type_ == TapNoteType_Tap ||
        curr_note->type_ == TapNoteType_HoldHead ||
        curr_note->type_ == TapNoteType_Mine ||
        curr_note->type_ == TapNoteType_Lift) {
      current_cue.columns_.emplace_back(
          curr_note.Track() + 1, curr_note->type_);
    }

    ++curr_note;
  }

  // If there's a remaining columnCue from the last row, add it
  if (current_cue.start_time_ != -1) {
    all_column_cues.push_back(current_cue);
  }

  float previous_cue_time = 0;
  out.clear();
  for (ColumnCue column_cue : all_column_cues) {
    float duration = column_cue.start_time_ - previous_cue_time;
    if (duration > min_duration || previous_cue_time == 0) {
      out.emplace_back(previous_cue_time, duration, column_cue.columns_);
    }
    previous_cue_time = column_cue.start_time_;
  }
}
}  // namespace

void ColumnCue::CalculateColumnCues(
    const NoteData& in, TimingData* timing, std::vector<ColumnCue>& out,
    PlayerNumber pn, float min_duration, StepsType steps_type) {
  std::vector<NoteData> split_note_data;
  if (NoteDataUtil::SplitCompositeOrStackedNoteData(
          in, split_note_data, steps_type)) {
    CalculateColumnCuesOnePlayer(
        split_note_data[pn], timing, out, min_duration);
  } else {
    CalculateColumnCuesOnePlayer(in, timing, out, min_duration);
  }
}
