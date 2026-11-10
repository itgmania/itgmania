/*
 * NoteData is organized by:
 *  track - corresponds to different columns of notes on the screen
 *  row/index - corresponds to subdivisions of beats
 */

#include "NoteData.h"

#include <algorithm>
#include <climits>
#include <cstddef>
#include <set>
#include <utility>
#include <vector>

#include "GameState.h"  // blame radar calculations.
#include "NoteTypes.h"
#include "PlayerNumber.h"
#include "RageUtil.h"
#include "XmlFile.h"
#include "global.h"

void NoteData::Init() {
  tap_notes_ = std::vector<TrackMap>();  // ensure that the memory is freed
}

void NoteData::SetNumTracks(int new_num_tracks) {
  ASSERT(new_num_tracks > 0);

  tap_notes_.resize(new_num_tracks);
}

bool NoteData::IsComposite() const {
  for (int track = 0; track < GetNumTracks(); ++track) {
    for (const RowTapNote& tn : tap_notes_[track]) {
      if (tn.second.pn_ != PLAYER_INVALID) {
        return true;
      }
    }
  }
  return false;
}

// Clear (rowBegin,rowEnd).
void NoteData::ClearRangeForTrack(int row_begin, int row_end, int track) {
  // Optimization: if the range encloses everything, just clear the whole maps.
  if (row_begin == 0 && row_end == MAX_NOTE_ROW) {
    tap_notes_[track].clear();
    return;
  }

  /* If the range is empty, don't do anything. Otherwise, an empty range will
   * cause hold notes to be split when they shouldn't be. */
  if (row_begin == row_end) {
    return;
  }

  NoteData::TrackMap::iterator l_begin, l_end;
  GetTapNoteRangeInclusive(track, row_begin, row_end, l_begin, l_end);

  if (l_begin != l_end && l_begin->first < row_begin &&
      l_begin->first + l_begin->second.duration_ > row_end) {
    /* A hold note overlaps the whole range. Truncate it, and add the
     * remainder to the end. */
    TapNote tn1 = l_begin->second;
    TapNote tn2 = tn1;

    int end_row = l_begin->first + tn1.duration_;
    int row = l_begin->first;

    tn1.duration_ = row_begin - row;
    tn2.duration_ = end_row - row_end;

    SetTapNote(track, row, tn1);
    SetTapNote(track, row_end, tn2);

    // We may have invalidated our iterators.
    GetTapNoteRangeInclusive(track, row_begin, row_end, l_begin, l_end);
  } else if (l_begin != l_end && l_begin->first < row_begin) {
    // A hold note overlaps the beginning of the range.  Truncate it.
    TapNote& tn1 = l_begin->second;
    int row = l_begin->first;
    tn1.duration_ = row_begin - row;

    ++l_begin;
  }

  if (l_begin != l_end) {
    NoteData::TrackMap::iterator prev = l_end;
    --prev;
    TapNote tn = prev->second;
    int row = prev->first;
    if (tn.type_ == TapNoteType_HoldHead && row + tn.duration_ > row_end) {
      // A hold note overlaps the end of the range.  Separate it.
      SetTapNote(track, row, TAP_EMPTY);

      int add = row_end - row;
      tn.duration_ -= add;
      row += add;
      SetTapNote(track, row, tn);
      l_end = prev;
    }

    // We may have invalidated our iterators.
    GetTapNoteRangeInclusive(track, row_begin, row_end, l_begin, l_end);
  }

  tap_notes_[track].erase(l_begin, l_end);
}

void NoteData::ClearRange(int row_begin, int row_end) {
  for (int t = 0; t < GetNumTracks(); ++t) {
    ClearRangeForTrack(row_begin, row_end, t);
  }
}

void NoteData::ClearAll() {
  for (int t = 0; t < GetNumTracks(); t++) {
    tap_notes_[t].clear();
  }
}

/* Copy [rowFromBegin,rowFromEnd) from pFrom to this. (Note that this does
 * *not* overlay; all data in the range is overwritten.) */
void NoteData::CopyRange(
    const NoteData& from, int row_from_begin, int row_from_end,
    int row_to_begin) {
  ASSERT(from.GetNumTracks() == GetNumTracks());

  if (row_from_begin > row_from_end) {
    return;  // empty range
  }

  const int row_to_end = (row_from_end - row_from_begin) + row_to_begin;
  const int move_by = row_to_begin - row_from_begin;

  // Clear the region.
  ClearRange(row_to_begin, row_to_end);

  for (int t = 0; t < GetNumTracks(); t++) {
    NoteData::TrackMap::const_iterator l_begin, l_end;
    from.GetTapNoteRangeInclusive(
        t, row_from_begin, row_from_end, l_begin, l_end);
    for (; l_begin != l_end; ++l_begin) {
      TapNote head = l_begin->second;
      if (head.type_ == TapNoteType_Empty) {
        continue;
      }

      if (head.type_ == TapNoteType_HoldHead) {
        int start_row = l_begin->first + move_by;
        int end_row = start_row + head.duration_;

        start_row = std::clamp(start_row, row_to_begin, row_to_end);
        end_row = std::clamp(end_row, row_to_begin, row_to_end);

        this->AddHoldNote(t, start_row, end_row, head);
      } else {
        int to = l_begin->first + move_by;
        if (to >= row_to_begin && to <= row_to_end) {
          this->SetTapNote(t, to, head);
        }
      }
    }
  }
}

void NoteData::CopyAll(const NoteData& from) { *this = from; }

bool NoteData::IsRowEmpty(int row) const {
  for (int t = 0; t < GetNumTracks(); t++) {
    if (GetTapNote(t, row).type_ != TapNoteType_Empty) {
      return false;
    }
  }
  return true;
}

bool NoteData::IsRangeEmpty(int track, int row_begin, int row_end) const {
  ASSERT(track < GetNumTracks());

  FOREACH_NONEMPTY_ROW_IN_TRACK_RANGE(*this, track, r, row_begin, row_end)
  if (GetTapNote(track, r).type_ != TapNoteType_Empty) {
    return false;
  }
  return true;
}

int NoteData::GetNumTapNonEmptyTracks(int row) const {
  int num = 0;
  for (int t = 0; t < GetNumTracks(); t++) {
    if (GetTapNote(t, row).type_ != TapNoteType_Empty) {
      num++;
    }
  }
  return num;
}

void NoteData::GetTapNonEmptyTracks(int row, std::set<int>& add_to) const {
  for (int t = 0; t < GetNumTracks(); t++) {
    if (GetTapNote(t, row).type_ != TapNoteType_Empty) {
      add_to.insert(t);
    }
  }
}

bool NoteData::GetTapFirstNonEmptyTrack(
    int row, int& non_empty_track_out) const {
  for (int t = 0; t < GetNumTracks(); t++) {
    if (GetTapNote(t, row).type_ != TapNoteType_Empty) {
      non_empty_track_out = t;
      return true;
    }
  }
  return false;
}

int NoteData::GetNumTracksWithTap(int row) const {
  int num = 0;
  for (int t = 0; t < GetNumTracks(); t++) {
    const TapNote& tn = GetTapNote(t, row);
    if (tn.type_ == TapNoteType_Tap || tn.type_ == TapNoteType_Lift) {
      num++;
    }
  }
  return num;
}

int NoteData::GetNumTracksWithTapOrHoldHead(int row) const {
  int num = 0;
  for (int t = 0; t < GetNumTracks(); t++) {
    const TapNote& tn = GetTapNote(t, row);
    if (tn.type_ == TapNoteType_Tap || tn.type_ == TapNoteType_Lift ||
        tn.type_ == TapNoteType_HoldHead) {
      num++;
    }
  }
  return num;
}

int NoteData::GetFirstTrackWithTap(int row) const {
  for (int t = 0; t < GetNumTracks(); t++) {
    const TapNote& tn = GetTapNote(t, row);
    if (tn.type_ == TapNoteType_Tap || tn.type_ == TapNoteType_Lift) {
      return t;
    }
  }
  return -1;
}

int NoteData::GetFirstTrackWithTapOrHoldHead(int row) const {
  for (int t = 0; t < GetNumTracks(); t++) {
    const TapNote& tn = GetTapNote(t, row);
    if (tn.type_ == TapNoteType_Tap || tn.type_ == TapNoteType_Lift ||
        tn.type_ == TapNoteType_HoldHead) {
      return t;
    }
  }
  return -1;
}

void NoteData::AddHoldNote(int track, int start_row, int end_row, TapNote tn) {
  ASSERT(start_row >= 0 && end_row >= 0);
  ASSERT_M(
      end_row >= start_row,
      ssprintf("EndRow %d < StartRow %d", end_row, start_row));

  /* Include adjacent (non-overlapping) hold notes, since we need to merge with
   * them. */
  NoteData::TrackMap::iterator l_begin, l_end;
  GetTapNoteRangeInclusive(track, start_row, end_row, l_begin, l_end, true);

  // Look for other hold notes that overlap and merge them into add.
  for (iterator it = l_begin; it != l_end; ++it) {
    int other_row = it->first;
    const TapNote& other = it->second;
    if (other.type_ == TapNoteType_HoldHead) {
      start_row = std::min(start_row, other_row);
      end_row = std::max(end_row, other_row + other.duration_);
    }
  }

  tn.duration_ = end_row - start_row;

  // Remove everything in the range.
  tap_notes_[track].erase(l_begin, l_end);

  /* Additionally, if there's a tap note lying at the end of our range,
   * remove it too. */
  SetTapNote(track, end_row, TAP_EMPTY);

  // add a tap note at the start of this hold
  SetTapNote(track, start_row, tn);
}

/* Determine if a hold note lies on the given spot.  Return true if so.  If
 * pHeadRow is non-nullptr, return the row of the head. */
bool NoteData::IsHoldHeadOrBodyAtRow(int track, int row, int* head_row) const {
  const TapNote& tn = GetTapNote(track, row);
  if (tn.type_ == TapNoteType_HoldHead) {
    if (head_row != nullptr) {
      *head_row = row;
    }
    return true;
  }

  return IsHoldNoteAtRow(track, row, head_row);
}

/* Determine if a hold note lies on the given spot. Return true if so.  If
 * pHeadRow is non-nullptr, return the row of the head. (Note that this returns
 * false if a hold head lies on iRow itself.) */
/* XXX: rename this to IsHoldBodyAtRow */
bool NoteData::IsHoldNoteAtRow(int track, int row, int* head_row) const {
  int dummy;
  if (head_row == nullptr) {
    head_row = &dummy;
  }

  /* Starting at iRow, search upwards. If we find a TapNoteType_HoldHead, we're
   * within a hold. If we find a tap, mine or attack, we're not--those never lie
   * within hold notes. Ignore autoKeysound. */
  FOREACH_NONEMPTY_ROW_IN_TRACK_RANGE_REVERSE(*this, track, r, 0, row) {
    const TapNote& tn = GetTapNote(track, r);
    switch (tn.type_) {
      case TapNoteType_HoldHead:
        if (tn.duration_ + r < row) {
          return false;
        }
        *head_row = r;
        return true;

      case TapNoteType_Tap:
      case TapNoteType_Mine:
      case TapNoteType_Attack:
      case TapNoteType_Lift:
      case TapNoteType_Fake:
        return false;

      case TapNoteType_Empty:
      case TapNoteType_AutoKeysound:
        // ignore
        continue;
        DEFAULT_FAIL(tn.type_);
    }
  }

  return false;
}

bool NoteData::IsEmpty() const {
  for (int t = 0; t < GetNumTracks(); t++) {
    int row = -1;
    if (!GetNextTapNoteRowForTrack(t, row)) {
      continue;
    }

    return false;
  }

  return true;
}

int NoteData::GetFirstRow() const {
  int earliest_row_found_so_far = -1;

  for (int t = 0; t < GetNumTracks(); t++) {
    int row = -1;
    if (!GetNextTapNoteRowForTrack(t, row, true)) {
      continue;
    }

    if (earliest_row_found_so_far == -1) {
      earliest_row_found_so_far = row;
    } else {
      earliest_row_found_so_far = std::min(earliest_row_found_so_far, row);
    }
  }

  if (earliest_row_found_so_far == -1) {  // there are no notes
    return 0;
  }

  return earliest_row_found_so_far;
}

int NoteData::GetLastRow() const {
  int oldest_row_found_so_far = 0;

  for (int t = 0; t < GetNumTracks(); t++) {
    int row = MAX_NOTE_ROW;
    if (!GetPrevTapNoteRowForTrack(t, row)) {
      continue;
    }

    /* XXX: We might have a hold note near the end with autoplay sounds
     * after it.  Do something else with autoplay sounds ... */
    const TapNote& tn = GetTapNote(t, row);
    if (tn.type_ == TapNoteType_HoldHead) {
      row += tn.duration_;
    }

    oldest_row_found_so_far = std::max(oldest_row_found_so_far, row);
  }

  return oldest_row_found_so_far;
}

bool NoteData::IsTap(const TapNote& tn, const int row) const {
  return (
      tn.type_ != TapNoteType_Empty && tn.type_ != TapNoteType_Mine &&
      tn.type_ != TapNoteType_Lift && tn.type_ != TapNoteType_Fake &&
      tn.type_ != TapNoteType_AutoKeysound &&
      GAMESTATE->GetProcessedTimingData()->IsJudgableAtRow(row));
}

bool NoteData::IsMine(const TapNote& tn, const int row) const {
  return (
      tn.type_ == TapNoteType_Mine &&
      GAMESTATE->GetProcessedTimingData()->IsJudgableAtRow(row));
}

bool NoteData::IsLift(const TapNote& tn, const int row) const {
  return (
      tn.type_ == TapNoteType_Lift &&
      GAMESTATE->GetProcessedTimingData()->IsJudgableAtRow(row));
}

bool NoteData::IsFake(const TapNote& tn, const int row) const {
  return (
      tn.type_ == TapNoteType_Fake ||
      !GAMESTATE->GetProcessedTimingData()->IsJudgableAtRow(row));
}

int NoteData::GetNumTapNotes(int start_index, int end_index) const {
  int num_notes = 0;
  for (int t = 0; t < GetNumTracks(); t++) {
    FOREACH_NONEMPTY_ROW_IN_TRACK_RANGE(*this, t, r, start_index, end_index) {
      if (this->IsTap(GetTapNote(t, r), r)) {
        num_notes++;
      }
    }
  }

  return num_notes;
}

int NoteData::GetNumTapNotesNoTiming(int start_index, int end_index) const {
  int num_notes = 0;
  for (int t = 0; t < GetNumTracks(); t++) {
    FOREACH_NONEMPTY_ROW_IN_TRACK_RANGE(*this, t, r, start_index, end_index) {
      if (GetTapNote(t, r).type_ != TapNoteType_Empty) {
        num_notes++;
      }
    }
  }

  return num_notes;
}

int NoteData::GetNumTapNotesInRow(int row) const {
  int num_notes = 0;
  for (int t = 0; t < GetNumTracks(); t++) {
    if (this->IsTap(GetTapNote(t, row), row)) {
      num_notes++;
    }
  }
  return num_notes;
}

int NoteData::GetNumMines(int start_index, int end_index) const {
  int num_mines = 0;

  for (int t = 0; t < GetNumTracks(); t++) {
    FOREACH_NONEMPTY_ROW_IN_TRACK_RANGE(*this, t, r, start_index, end_index)
    if (this->IsMine(GetTapNote(t, r), r)) {
      num_mines++;
    }
  }

  return num_mines;
}

int NoteData::GetNumRowsWithTapOrHoldHead(
    int start_index, int end_index) const {
  int num_notes = 0;
  FOREACH_NONEMPTY_ROW_ALL_TRACKS_RANGE(*this, r, start_index, end_index)
  if (IsThereATapOrHoldHeadAtRow(r) &&
      GAMESTATE->GetProcessedTimingData()->IsJudgableAtRow(r)) {
    num_notes++;
  }

  return num_notes;
}

bool NoteData::RowNeedsAtLeastSimultaneousPresses(
    int min_simultaneous_presses, const int row) const {
  int num_notes_this_index = 0;
  for (int t = 0; t < GetNumTracks(); t++) {
    const TapNote& tn = GetTapNote(t, row);
    switch (tn.type_) {
      case TapNoteType_Mine:
      case TapNoteType_Empty:
      case TapNoteType_Fake:
      case TapNoteType_Lift:  // you don't "press" on a lift.
      case TapNoteType_AutoKeysound:
        continue;  // skip these types - they don't count
      default:
        break;
    }
    ++num_notes_this_index;
  }

  /* We must have at least one tap or hold head at this row to count it. */
  if (!num_notes_this_index) {
    return false;
  }

  if (num_notes_this_index < min_simultaneous_presses) {
    /* We have at least one, but not enough.  Count holds.  Do count adjacent
     * holds. */
    for (int t = 0; t < GetNumTracks(); ++t) {
      if (IsHoldNoteAtRow(t, row)) {
        ++num_notes_this_index;
      }
    }
  }

  return num_notes_this_index >= min_simultaneous_presses;
}

int NoteData::GetNumRowsWithSimultaneousPresses(
    int min_simultaneous_presses, int start_index, int end_index) const {
  /* Count the number of times you have to use your hands.  This includes
   * three taps at the same time, a tap while two hold notes are being held,
   * etc.  Only count rows that have at least one tap note (hold heads count).
   * Otherwise, every row of hold notes counts, so three simultaneous hold
   * notes will count as hundreds of "hands". */
  int num = 0;
  FOREACH_NONEMPTY_ROW_ALL_TRACKS_RANGE(*this, r, start_index, end_index) {
    if (!RowNeedsAtLeastSimultaneousPresses(min_simultaneous_presses, r)) {
      continue;
    }
    if (!GAMESTATE->GetProcessedTimingData()->IsJudgableAtRow(r)) {
      continue;
    }
    num++;
  }

  return num;
}

int NoteData::GetNumRowsWithSimultaneousTaps(
    int min_taps, int start_index, int end_index) const {
  int num = 0;
  FOREACH_NONEMPTY_ROW_ALL_TRACKS_RANGE(*this, r, start_index, end_index) {
    if (!GAMESTATE->GetProcessedTimingData()->IsJudgableAtRow(r)) {
      continue;
    }
    int num_notes_this_index = 0;
    for (int t = 0; t < GetNumTracks(); t++) {
      const TapNote& tn = GetTapNote(t, r);
      if (tn.type_ != TapNoteType_Mine &&  // mines don't count.
          tn.type_ != TapNoteType_Empty && tn.type_ != TapNoteType_Fake &&
          tn.type_ != TapNoteType_AutoKeysound) {
        num_notes_this_index++;
      }
    }
    if (num_notes_this_index >= min_taps) {
      num++;
    }
  }

  return num;
}

int NoteData::GetNumHoldNotes(int start_index, int end_index) const {
  int num_holds = 0;
  for (int t = 0; t < GetNumTracks(); ++t) {
    NoteData::TrackMap::const_iterator l_begin, l_end;
    GetTapNoteRangeExclusive(t, start_index, end_index, l_begin, l_end);
    for (; l_begin != l_end; ++l_begin) {
      if (l_begin->second.type_ != TapNoteType_HoldHead ||
          l_begin->second.sub_type_ != TapNoteSubType_Hold) {
        continue;
      }
      if (!GAMESTATE->GetProcessedTimingData()->IsJudgableAtRow(
              l_begin->first)) {
        continue;
      }
      num_holds++;
    }
  }
  return num_holds;
}

int NoteData::GetNumRolls(int start_index, int end_index) const {
  int num_rolls = 0;
  for (int t = 0; t < GetNumTracks(); ++t) {
    NoteData::TrackMap::const_iterator l_begin, l_end;
    GetTapNoteRangeExclusive(t, start_index, end_index, l_begin, l_end);
    for (; l_begin != l_end; ++l_begin) {
      if (l_begin->second.type_ != TapNoteType_HoldHead ||
          l_begin->second.sub_type_ != TapNoteSubType_Roll) {
        continue;
      }
      if (!GAMESTATE->GetProcessedTimingData()->IsJudgableAtRow(
              l_begin->first)) {
        continue;
      }
      num_rolls++;
    }
  }
  return num_rolls;
}

int NoteData::GetNumLifts(int start_index, int end_index) const {
  int num_lifts = 0;

  for (int t = 0; t < GetNumTracks(); t++) {
    FOREACH_NONEMPTY_ROW_IN_TRACK_RANGE(*this, t, r, start_index, end_index)
    if (this->IsLift(GetTapNote(t, r), r)) {
      num_lifts++;
    }
  }

  return num_lifts;
}

int NoteData::GetNumFakes(int start_index, int end_index) const {
  int num_fakes = 0;

  for (int t = 0; t < GetNumTracks(); t++) {
    FOREACH_NONEMPTY_ROW_IN_TRACK_RANGE(*this, t, r, start_index, end_index)
    if (this->IsFake(GetTapNote(t, r), r)) {
      num_fakes++;
    }
  }

  return num_fakes;
}

bool NoteData::IsPlayer1(const int track, const TapNote& tn) const {
  if (this->IsComposite()) {
    return tn.pn_ == PLAYER_1;
  }
  return track < (this->GetNumTracks() / 2);
}

std::pair<int, int> NoteData::GetNumTapNotesTwoPlayer(
    int start_index, int end_index) const {
  std::pair<int, int> num(0, 0);
  for (int t = 0; t < GetNumTracks(); t++) {
    FOREACH_NONEMPTY_ROW_IN_TRACK_RANGE(*this, t, r, start_index, end_index) {
      const TapNote& tn = GetTapNote(t, r);
      if (this->IsTap(tn, r)) {
        if (this->IsPlayer1(t, tn)) {
          num.first++;
        } else {
          num.second++;
        }
      }
    }
  }
  return num;
}

std::pair<int, int> NoteData::GetNumRowsWithSimultaneousTapsTwoPlayer(
    int min_taps, int start_row, int end_row) const {
  std::pair<int, int> num(0, 0);
  FOREACH_NONEMPTY_ROW_ALL_TRACKS_RANGE(*this, r, start_row, end_row) {
    std::pair<int, int> found(0, 0);
    for (int t = 0; t < GetNumTracks(); t++) {
      const TapNote& tn = GetTapNote(t, r);
      if (this->IsTap(tn, r)) {
        if (this->IsPlayer1(t, tn)) {
          found.first++;
        } else {
          found.second++;
        }
      }
    }
    if (found.first >= min_taps) {
      num.first++;
    }
    if (found.second >= min_taps) {
      num.second++;
    }
  }
  return num;
}

std::pair<int, int> NoteData::GetNumJumpsTwoPlayer(
    int start_index, int end_index) const {
  return GetNumRowsWithSimultaneousTapsTwoPlayer(2, start_index, end_index);
}

std::pair<int, int> NoteData::GetNumHandsTwoPlayer(
    int start_index, int end_index) const {
  return GetNumRowsWithSimultaneousTapsTwoPlayer(3, start_index, end_index);
}

std::pair<int, int> NoteData::GetNumQuadsTwoPlayer(
    int start_index, int end_index) const {
  return GetNumRowsWithSimultaneousTapsTwoPlayer(4, start_index, end_index);
}

std::pair<int, int> NoteData::GetNumHoldNotesTwoPlayer(
    int start_index, int end_index) const {
  std::pair<int, int> num(0, 0);
  for (int t = 0; t < GetNumTracks(); ++t) {
    NoteData::TrackMap::const_iterator l_begin, l_end;
    GetTapNoteRangeExclusive(t, start_index, end_index, l_begin, l_end);
    for (; l_begin != l_end; ++l_begin) {
      if (l_begin->second.type_ != TapNoteType_HoldHead ||
          l_begin->second.sub_type_ != TapNoteSubType_Hold) {
        continue;
      }
      if (!GAMESTATE->GetProcessedTimingData()->IsJudgableAtRow(
              l_begin->first)) {
        continue;
      }
      if (this->IsPlayer1(t, l_begin->second)) {
        num.first++;
      } else {
        num.second++;
      }
    }
  }
  return num;
}

std::pair<int, int> NoteData::GetNumMinesTwoPlayer(
    int start_index, int end_index) const {
  std::pair<int, int> num(0, 0);
  for (int t = 0; t < GetNumTracks(); t++) {
    FOREACH_NONEMPTY_ROW_IN_TRACK_RANGE(*this, t, r, start_index, end_index) {
      const TapNote& tn = GetTapNote(t, r);
      if (this->IsMine(tn, r)) {
        if (this->IsPlayer1(t, tn)) {
          num.first++;
        } else {
          num.second++;
        }
      }
    }
  }
  return num;
}

std::pair<int, int> NoteData::GetNumRollsTwoPlayer(
    int start_index, int end_index) const {
  std::pair<int, int> num(0, 0);
  for (int t = 0; t < GetNumTracks(); ++t) {
    NoteData::TrackMap::const_iterator l_begin, l_end;
    GetTapNoteRangeExclusive(t, start_index, end_index, l_begin, l_end);
    for (; l_begin != l_end; ++l_begin) {
      if (l_begin->second.type_ != TapNoteType_HoldHead ||
          l_begin->second.sub_type_ != TapNoteSubType_Roll) {
        continue;
      }
      if (!GAMESTATE->GetProcessedTimingData()->IsJudgableAtRow(
              l_begin->first)) {
        continue;
      }
      if (this->IsPlayer1(t, l_begin->second)) {
        num.first++;
      } else {
        num.second++;
      }
    }
  }
  return num;
}

std::pair<int, int> NoteData::GetNumLiftsTwoPlayer(
    int start_index, int end_index) const {
  std::pair<int, int> num(0, 0);
  for (int t = 0; t < GetNumTracks(); t++) {
    FOREACH_NONEMPTY_ROW_IN_TRACK_RANGE(*this, t, r, start_index, end_index) {
      const TapNote& tn = GetTapNote(t, r);
      if (this->IsLift(tn, r)) {
        if (this->IsPlayer1(t, tn)) {
          num.first++;
        } else {
          num.second++;
        }
      }
    }
  }
  return num;
}

std::pair<int, int> NoteData::GetNumFakesTwoPlayer(
    int start_index, int end_index) const {
  std::pair<int, int> num(0, 0);
  for (int t = 0; t < GetNumTracks(); t++) {
    FOREACH_NONEMPTY_ROW_IN_TRACK_RANGE(*this, t, r, start_index, end_index) {
      const TapNote& tn = GetTapNote(t, r);
      if (this->IsFake(tn, r)) {
        if (this->IsPlayer1(t, tn)) {
          num.first++;
        } else {
          num.second++;
        }
      }
    }
  }
  return num;
}

// -1 for iOriginalTracksToTakeFrom means no track
void NoteData::LoadTransformed(
    const NoteData& in, int new_num_tracks,
    const int original_track_to_take_from[]) {
  // reset all notes
  Init();

  SetNumTracks(new_num_tracks);

  // copy tracks
  for (int t = 0; t < GetNumTracks(); t++) {
    const int original_track = original_track_to_take_from[t];
    ASSERT_M(
        original_track < in.GetNumTracks(),
        ssprintf(
            "from OriginalTrack %i >= %i (#tracks) (taking from %i)",
            original_track, in.GetNumTracks(), original_track_to_take_from[t]));

    if (original_track == -1) {
      continue;
    }
    tap_notes_[t] = in.tap_notes_[original_track];
  }
}

void NoteData::SetTapNote(int track, int row, const TapNote& t) {
  DEBUG_ASSERT(track >= 0 && track < GetNumTracks());

  if (row < 0) {
    return;
  }

  TrackMap& track_map = tap_notes_[track];
  auto it = lower_bound(track, row);
  bool found = (it != track_map.end() && it->first == row);

  if (t == TAP_EMPTY) {
    if (found) {
      track_map.erase(it);
    }
  } else {
    if (found) {
      it->second = t;
    } else {
      track_map.insert(it, {row, t});
    }
  }
}

void NoteData::GetTracksHeldAtRow(int row, std::set<int>& add_to) {
  for (int t = 0; t < GetNumTracks(); ++t) {
    if (IsHoldNoteAtRow(t, row)) {
      add_to.insert(t);
    }
  }
}

int NoteData::GetNumTracksHeldAtRow(int row) {
  static std::set<int> tracks;
  tracks.clear();
  GetTracksHeldAtRow(row, tracks);
  return tracks.size();
}

bool NoteData::GetNextTapNoteRowForTrack(
    int track, int& row_in_out, bool ignore_auto_keysounds) const {
  const TrackMap& map_track = tap_notes_[track];

  // lower_bound and upper_bound have the same effect here because duplicate
  // keys aren't allowed.

  // lower_bound "finds the first element whose key is not less than k" (>=);
  // upper_bound "finds the first element whose key greater than k".  They don't
  // have the same effect, but lower_bound(row+1) should equal upper_bound(row).
  // -glenn
  TrackMap::const_iterator iter = lower_bound(track, row_in_out + 1);
  if (iter == map_track.end()) {
    return false;
  }

  ASSERT(iter->first > row_in_out);

  // If we want to ignore autokeysounds, keep going until we find a real note.
  if (ignore_auto_keysounds) {
    while (iter->second.type_ == TapNoteType_AutoKeysound) {
      iter++;
      if (iter == map_track.end()) {
        return false;
      }
    }
  }
  row_in_out = iter->first;
  return true;
}

bool NoteData::GetPrevTapNoteRowForTrack(int track, int& row_in_out) const {
  const TrackMap& map_track = tap_notes_[track];

  // Find the first note >= rowInOut.
  TrackMap::const_iterator iter = lower_bound(track, row_in_out);

  // If we're at the beginning, we can't move back any more.
  if (iter == map_track.begin()) {
    return false;
  }

  // Move back by one.
  --iter;
  ASSERT(iter->first < row_in_out);
  row_in_out = iter->first;
  return true;
}

void NoteData::GetTapNoteRange(
    int track, int start_row, int end_row, TrackMap::iterator& l_begin,
    TrackMap::iterator& l_end) {
  ASSERT_M(track < GetNumTracks(), ssprintf("%i,%i", track, GetNumTracks()));
  TrackMap& map_track = tap_notes_[track];

  if (start_row > end_row) {
    l_begin = l_end = map_track.end();
    return;
  }

  if (start_row <= 0) {
    l_begin = map_track.begin();  // optimization
  } else if (start_row >= MAX_NOTE_ROW) {
    l_begin = map_track.end();  // optimization
  } else {
    l_begin = lower_bound(track, start_row);
  }

  if (end_row <= 0) {
    l_end = map_track.begin();  // optimization
  } else if (end_row >= MAX_NOTE_ROW) {
    l_end = map_track.end();  // optimization
  } else {
    l_end = lower_bound(track, end_row);
  }
}

/* Include hold notes that overlap the edges.  If a hold note completely
 * surrounds the given range, included it, too.  If bIncludeAdjacent is true,
 * also include hold notes adjacent to, but not overlapping, the edge. */
void NoteData::GetTapNoteRangeInclusive(
    int track, int start_row, int end_row, TrackMap::iterator& l_begin,
    TrackMap::iterator& l_end, bool include_adjacent) {
  GetTapNoteRange(track, start_row, end_row, l_begin, l_end);

  if (l_begin != this->begin(track)) {
    iterator prev = Decrement(l_begin);

    const TapNote& tn = prev->second;
    if (tn.type_ == TapNoteType_HoldHead) {
      int hold_start_row = prev->first;
      int hold_end_row = hold_start_row + tn.duration_;
      if (include_adjacent) {
        ++hold_end_row;
      }
      if (hold_end_row > start_row) {
        // The previous note is a hold.
        l_begin = prev;
      }
    }
  }

  if (include_adjacent && l_end != this->end(track)) {
    // Include the next note if it's a hold and starts on iEndRow.
    const TapNote& tn = l_end->second;
    int hold_start_row = l_end->first;
    if (tn.type_ == TapNoteType_HoldHead && hold_start_row == end_row) {
      ++l_end;
    }
  }
}

void NoteData::GetTapNoteRange(
    int track, int start_row, int end_row, TrackMap::const_iterator& l_begin,
    TrackMap::const_iterator& l_end) const {
  TrackMap::iterator const_begin, const_end;
  const_cast<NoteData*>(this)->GetTapNoteRange(
      track, start_row, end_row, const_begin, const_end);
  l_begin = const_begin;
  l_end = const_end;
}

void NoteData::GetTapNoteRangeInclusive(
    int track, int start_row, int end_row, TrackMap::const_iterator& l_begin,
    TrackMap::const_iterator& l_end, bool include_adjacent) const {
  TrackMap::iterator const_begin, const_end;
  const_cast<NoteData*>(this)->GetTapNoteRangeInclusive(
      track, start_row, end_row, const_begin, const_end, include_adjacent);
  l_begin = const_begin;
  l_end = const_end;
}

void NoteData::GetTapNoteRangeExclusive(
    int track, int start_row, int end_row, TrackMap::const_iterator& l_begin,
    TrackMap::const_iterator& l_end) const {
  TrackMap::iterator const_begin, const_end;
  const_cast<NoteData*>(this)->GetTapNoteRange(
      track, start_row, end_row, const_begin, const_end);
  l_begin = const_begin;
  l_end = const_end;
}

bool NoteData::GetNextTapNoteRowForAllTracks(int& row_in_out) const {
  int closest_next_row = MAX_NOTE_ROW;
  bool any_have_next_note = false;
  for (int t = 0; t < GetNumTracks(); t++) {
    int new_row_this_track = row_in_out;
    if (GetNextTapNoteRowForTrack(t, new_row_this_track)) {
      any_have_next_note = true;
      ASSERT(new_row_this_track < MAX_NOTE_ROW);
      closest_next_row = std::min(closest_next_row, new_row_this_track);
    }
  }

  if (any_have_next_note) {
    row_in_out = closest_next_row;
    return true;
  } else {
    return false;
  }
}

bool NoteData::GetPrevTapNoteRowForAllTracks(int& row_in_out) const {
  int closest_prev_row = 0;
  bool any_have_prev_note = false;
  for (int t = 0; t < GetNumTracks(); t++) {
    int new_row_this_track = row_in_out;
    if (GetPrevTapNoteRowForTrack(t, new_row_this_track)) {
      any_have_prev_note = true;
      ASSERT(new_row_this_track < MAX_NOTE_ROW);
      closest_prev_row = std::max(closest_prev_row, new_row_this_track);
    }
  }

  if (any_have_prev_note) {
    row_in_out = closest_prev_row;
    return true;
  } else {
    return false;
  }
}

XNode* NoteData::CreateNode() const {
  XNode* p = new XNode("NoteData");

  all_tracks_const_iterator iter = GetTapNoteRangeAllTracks(0, GetLastRow());

  for (; !iter.IsAtEnd(); ++iter) {
    XNode* p2 = iter->CreateNode();

    p2->AppendAttr("Track", iter.Track());
    p2->AppendAttr("Row", iter.Row());
    p->AppendChild(p2);
  }
  return p;
}

void NoteData::AddATIToList(all_tracks_iterator* iter) const {
  atis_.insert(iter);
}

void NoteData::AddATIToList(all_tracks_const_iterator* iter) const {
  const_atis_.insert(iter);
}

void NoteData::RemoveATIFromList(all_tracks_iterator* iter) const {
  std::set<all_tracks_iterator*>::iterator pos = atis_.find(iter);
  if (pos != atis_.end()) {
    atis_.erase(pos);
  }
}

void NoteData::RemoveATIFromList(all_tracks_const_iterator* iter) const {
  std::set<all_tracks_const_iterator*>::iterator pos = const_atis_.find(iter);
  if (pos != const_atis_.end()) {
    const_atis_.erase(pos);
  }
}

void NoteData::RevalidateATIs(
    const std::vector<int>& added_or_removed_tracks, bool added) {
  for (std::set<all_tracks_iterator*>::iterator cur = atis_.begin();
       cur != atis_.end(); ++cur) {
    (*cur)->Revalidate(this, added_or_removed_tracks, added);
  }
  for (std::set<all_tracks_const_iterator*>::iterator cur = const_atis_.begin();
       cur != const_atis_.end(); ++cur) {
    (*cur)->Revalidate(this, added_or_removed_tracks, added);
  }
}

template <typename ND, typename iter, typename TN>
void NoteData::_all_tracks_iterator<ND, iter, TN>::Find(bool reverse) {
  // If no notes can be found in the range, m_iTrack will stay -1 and IsAtEnd()
  // will return true.
  track_ = -1;
  if (reverse) {
    int max_row = INT_MIN;
    for (int track = note_data_->GetNumTracks() - 1; track >= 0; --track) {
      iter& i(current_iters_[track]);
      const iter& end = end_iters_[track];
      if (i != end && i->first > max_row) {
        max_row = i->first;
        track_ = track;
      }
    }
  } else {
    int min_row = INT_MAX;
    for (int track = 0; track < note_data_->GetNumTracks(); ++track) {
      iter& i = current_iters_[track];
      const iter& end = end_iters_[track];
      if (i != end && i->first < min_row) {
        min_row = i->first;
        track_ = track;
      }
    }
  }
}

template <typename ND, typename iter, typename TN>
NoteData::_all_tracks_iterator<ND, iter, TN>::_all_tracks_iterator(
    ND& nd, int start_row, int end_row, bool reverse, bool inclusive)
    : note_data_(&nd), track_(0), reverse_(reverse) {
  ASSERT(note_data_->GetNumTracks() > 0);

  start_row_ = start_row;
  end_row_ = end_row;

  for (int track = 0; track < note_data_->GetNumTracks(); ++track) {
    iter begin, end;
    inclusive_ = inclusive;
    if (inclusive) {
      note_data_->GetTapNoteRangeInclusive(
          track, start_row, end_row, begin, end);
    } else {
      note_data_->GetTapNoteRange(track, start_row, end_row, begin, end);
    }

    begin_iters_.push_back(begin);
    end_iters_.push_back(end);
    prev_current_rows_.push_back(0);

    iter cur;
    if (reverse_) {
      cur = end;
      if (cur != begin) {
        cur--;
      }
    } else {
      cur = begin;
    }
    current_iters_.push_back(cur);
  }
  note_data_->AddATIToList(this);

  Find(reverse);
}

template <typename ND, typename iter, typename TN>
NoteData::_all_tracks_iterator<ND, iter, TN>::_all_tracks_iterator(
    const _all_tracks_iterator& other)
    :
#define COPY_OTHER(x) x(other.x)
      COPY_OTHER(note_data_),
      COPY_OTHER(begin_iters_),
      COPY_OTHER(current_iters_),
      COPY_OTHER(end_iters_),
      COPY_OTHER(track_),
      COPY_OTHER(reverse_),
      COPY_OTHER(prev_current_rows_),
      COPY_OTHER(start_row_),
      COPY_OTHER(end_row_)
#undef COPY_OTHER
{
  note_data_->AddATIToList(this);
}

template <typename ND, typename iter, typename TN>
NoteData::_all_tracks_iterator<ND, iter, TN>::~_all_tracks_iterator() {
  if (note_data_ != nullptr) {
    note_data_->RemoveATIFromList(this);
  }
}

template <typename ND, typename iter, typename TN>
NoteData::_all_tracks_iterator<ND, iter, TN>&
NoteData::_all_tracks_iterator<ND, iter, TN>::operator=(
    const _all_tracks_iterator& other) {
  _all_tracks_iterator tmp(other);

#define SWAP_OTHER(x) std::swap(x, tmp.x)
  SWAP_OTHER(note_data_);
  SWAP_OTHER(begin_iters_);
  SWAP_OTHER(current_iters_);
  SWAP_OTHER(end_iters_);
  SWAP_OTHER(track_);
  SWAP_OTHER(reverse_);
  SWAP_OTHER(prev_current_rows_);
  SWAP_OTHER(start_row_);
  SWAP_OTHER(end_row_);
#undef SWAP_OTHER
  note_data_->AddATIToList(this);

  return *this;
}

template <typename ND, typename iter, typename TN>
NoteData::_all_tracks_iterator<ND, iter, TN>&
NoteData::_all_tracks_iterator<ND, iter, TN>::operator++()  // preincrement
{
  prev_current_rows_[track_] = Row();
  if (reverse_) {
    if (current_iters_[track_] == begin_iters_[track_]) {
      current_iters_[track_] = end_iters_[track_];
    } else {
      --current_iters_[track_];
    }
  } else {
    ++current_iters_[track_];
  }
  Find(reverse_);
  return *this;
}

template <typename ND, typename iter, typename TN>
NoteData::_all_tracks_iterator<ND, iter, TN>
NoteData::_all_tracks_iterator<ND, iter, TN>::operator++(int)  // postincrement
{
  _all_tracks_iterator<ND, iter, TN> ret(*this);
  operator++();
  return ret;
}

template <typename ND, typename iter, typename TN>
void NoteData::_all_tracks_iterator<ND, iter, TN>::Revalidate(
    ND* notedata, const std::vector<int>& added_or_removed_tracks, bool added) {
  note_data_ = notedata;
  ASSERT(note_data_->GetNumTracks() > 0);
  if (!added_or_removed_tracks.empty()) {
    if (added) {
      int avg_row = 0;
      for (size_t p = 0; p < prev_current_rows_.size(); ++p) {
        avg_row += prev_current_rows_[p];
      }
      avg_row /= prev_current_rows_.size();
      for (size_t a = 0; a < added_or_removed_tracks.size(); ++a) {
        int track_id = added_or_removed_tracks[a];
        prev_current_rows_.insert(
            prev_current_rows_.begin() + track_id, avg_row);
      }
      begin_iters_.resize(note_data_->GetNumTracks());
      current_iters_.resize(note_data_->GetNumTracks());
      end_iters_.resize(note_data_->GetNumTracks());
    } else {
      for (size_t a = 0; a < added_or_removed_tracks.size(); ++a) {
        int track_id = added_or_removed_tracks[a];
        prev_current_rows_.erase(prev_current_rows_.begin() + track_id);
      }
      begin_iters_.resize(note_data_->GetNumTracks());
      current_iters_.resize(note_data_->GetNumTracks());
      end_iters_.resize(note_data_->GetNumTracks());
    }
  }
  for (int track = 0; track < note_data_->GetNumTracks(); ++track) {
    iter begin, end;
    if (inclusive_) {
      note_data_->GetTapNoteRangeInclusive(
          track, start_row_, end_row_, begin, end);
    } else {
      note_data_->GetTapNoteRange(track, start_row_, end_row_, begin, end);
    }
    begin_iters_[track] = begin;
    end_iters_[track] = end;
    iter cur;
    if (reverse_) {
      cur = note_data_->upper_bound(track, prev_current_rows_[track]);
    } else {
      cur = note_data_->lower_bound(track, prev_current_rows_[track]);
    }
    current_iters_[track] = cur;
  }
  Find(reverse_);
}

/* XXX: This doesn't satisfy the requirements that ++iter; --iter; is a no-op so
 * it cannot be bidirectional for now. */
#if 0
template<typename ND, typename iter, typename TN>
NoteData::_all_tracks_iterator<ND, iter, TN> &NoteData::_all_tracks_iterator<ND, iter, TN>::operator--() // predecrement
{
	if( reverse_ )
	{
		++current_iters_[track_];
	}
	else
	{
		if( current_iters_[track_] == end_iters_[track_] )
			current_iters_[track_] = end_iters_[track_];
		else
			--current_iters_[track_];
	}
	Find( !reverse_ );
	return *this;
}

template<typename ND, typename iter, typename TN>
NoteData::_all_tracks_iterator<ND, iter, TN> NoteData::_all_tracks_iterator<ND, iter, TN>::operator--( int dummy ) // postdecrement
{
	_all_tracks_iterator<ND, iter, TN> ret( *this );
	operator--();
	return ret;
}
#endif

NoteData::iterator NoteData::lower_bound(int track, int row) {
  return std::lower_bound(
      tap_notes_[track].begin(), tap_notes_[track].end(), row,
      [](const RowTapNote& a, int b) { return a.first < b; });
}

NoteData::const_iterator NoteData::lower_bound(int track, int row) const {
  return std::lower_bound(
      tap_notes_[track].begin(), tap_notes_[track].end(), row,
      [](const RowTapNote& a, int b) { return a.first < b; });
}

NoteData::iterator NoteData::upper_bound(int track, int row) {
  return std::upper_bound(
      tap_notes_[track].begin(), tap_notes_[track].end(), row,
      [](int a, const RowTapNote& b) { return a < b.first; });
}

NoteData::const_iterator NoteData::upper_bound(int track, int row) const {
  return std::upper_bound(
      tap_notes_[track].begin(), tap_notes_[track].end(), row,
      [](int a, const RowTapNote& b) { return a < b.first; });
}

NoteData::iterator NoteData::FindTapNote(unsigned track, int row) {
  auto it = lower_bound(track, row);
  if (it != tap_notes_[track].end() && it->first == row) {
    return it;
  }
  return tap_notes_[track].end();
}

NoteData::const_iterator NoteData::FindTapNote(unsigned track, int row) const {
  auto it = lower_bound(track, row);
  if (it != tap_notes_[track].end() && it->first == row) {
    return it;
  }
  return tap_notes_[track].end();
}

NoteData::iterator NoteData::RemoveTapNote(unsigned track, iterator it) {
  return tap_notes_[track].erase(it);
}

// Explicit instantiation.
template class NoteData::_all_tracks_iterator<
    NoteData, NoteData::iterator, TapNote>;
template class NoteData::_all_tracks_iterator<
    const NoteData, NoteData::const_iterator, const TapNote>;

/*
 * (c) 2001-2004 Chris Danford, Glenn Maynard
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
