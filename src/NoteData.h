#ifndef NOTE_DATA_H
#define NOTE_DATA_H

#include <deque>
#include <set>
#include <utility>
#include <vector>

#include "NoteTypes.h"
#include "global.h"

/** @brief Act on each non empty row in the specific track. */
#define FOREACH_NONEMPTY_ROW_IN_TRACK(nd, track, row) \
  for (int row = -1; (nd).GetNextTapNoteRowForTrack(track, row);)
/** @brief Act on each non empty row in the specified track within the specified
 * range. */
#define FOREACH_NONEMPTY_ROW_IN_TRACK_RANGE(nd, track, row, start, last) \
  for (int row = start - 1;                                              \
       (nd).GetNextTapNoteRowForTrack(track, row) && row < (last);)
/** @brief Act on each non empty row in the specified track within the specified
 range, going in reverse order. */
#define FOREACH_NONEMPTY_ROW_IN_TRACK_RANGE_REVERSE( \
    nd, track, row, start, last)                     \
  for (int row = last;                               \
       (nd).GetPrevTapNoteRowForTrack(track, row) && row >= (start);)
/** @brief Act on each non empty row for all of the tracks. */
#define FOREACH_NONEMPTY_ROW_ALL_TRACKS(nd, row) \
  for (int row = -1; (nd).GetNextTapNoteRowForAllTracks(row);)
/** @brief Act on each non empty row for all of the tracks within the specified
 * range. */
#define FOREACH_NONEMPTY_ROW_ALL_TRACKS_RANGE(nd, row, start, last) \
  for (int row = start - 1;                                         \
       (nd).GetNextTapNoteRowForAllTracks(row) && row < (last);)

/** @brief Holds data about the notes that the player is supposed to hit. */
class NoteData {
 public:
  typedef std::pair<int, TapNote> RowTapNote;
  typedef std::deque<RowTapNote> TrackMap;
  typedef TrackMap::iterator iterator;
  typedef TrackMap::const_iterator const_iterator;

  NoteData() : tap_notes_() {}

  iterator begin(int track) { return tap_notes_[track].begin(); }
  const_iterator begin(int track) const { return tap_notes_[track].begin(); }
  iterator end(int track) { return tap_notes_[track].end(); }
  const_iterator end(int track) const { return tap_notes_[track].end(); }
  iterator lower_bound(int track, int row);
  const_iterator lower_bound(int track, int row) const;
  iterator upper_bound(int track, int row);
  const_iterator upper_bound(int track, int row) const;
  void swap(NoteData& nd) {
    tap_notes_.swap(nd.tap_notes_);
    atis_.swap(nd.atis_);
    const_atis_.swap(nd.const_atis_);
  }

  // This is ugly to make it templated but I don't want to have to write the
  // same class twice.
  template <typename ND, typename iter, typename TN>
  class _all_tracks_iterator {
    ND* note_data_;
    std::vector<iter> begin_iters_;

    /* There isn't a "past the beginning" iterator so this is hard to make a
     * true bidirectional iterator. Use the "past the end" iterator in place of
     * the "past the beginning" iterator when in reverse. */
    std::vector<iter> current_iters_;

    std::vector<iter> end_iters_;
    int track_;
    bool reverse_;

    // These exist so that the iterator can be revalidated if the NoteData is
    // transformed during this iterator's lifetime.
    std::vector<int> prev_current_rows_;
    bool inclusive_;
    int start_row_;
    int end_row_;

    void Find(bool reverse);

   public:
    _all_tracks_iterator(
        ND& nd, int start_row, int end_row, bool reverse, bool inclusive);
    _all_tracks_iterator(const _all_tracks_iterator& other);
    ~_all_tracks_iterator();
    _all_tracks_iterator& operator=(const _all_tracks_iterator& other);
    _all_tracks_iterator& operator++();          // preincrement
    _all_tracks_iterator operator++(int dummy);  // postincrement
    //_all_tracks_iterator &operator--();		// predecrement
    //_all_tracks_iterator operator--( int dummy );	// postdecrement
    inline int Track() const { return track_; }
    inline int Row() const { return current_iters_[track_]->first; }
    inline bool IsAtEnd() const { return track_ == -1; }
    inline TN& operator*() {
      DEBUG_ASSERT(!IsAtEnd());
      return current_iters_[track_]->second;
    }
    inline TN* operator->() {
      DEBUG_ASSERT(!IsAtEnd());
      return &current_iters_[track_]->second;
    }
    inline const TN& operator*() const {
      DEBUG_ASSERT(!IsAtEnd());
      return current_iters_[track_]->second;
    }
    inline const TN* operator->() const {
      DEBUG_ASSERT(!IsAtEnd());
      return &current_iters_[track_]->second;
    }
    // Use when transforming the NoteData.
    void Revalidate(
        ND* notedata, const std::vector<int>& added_or_removed_tracks,
        bool added);
  };
  typedef _all_tracks_iterator<NoteData, NoteData::iterator, TapNote>
      all_tracks_iterator;
  typedef _all_tracks_iterator<
      const NoteData, NoteData::const_iterator, const TapNote>
      all_tracks_const_iterator;
  typedef all_tracks_iterator all_tracks_reverse_iterator;
  friend class _all_tracks_iterator<NoteData, NoteData::iterator, TapNote>;
  friend class _all_tracks_iterator<
      const NoteData, NoteData::const_iterator, const TapNote>;

 private:
  // There's no point in inserting empty notes into the map.
  // Any blank space in the map is defined to be empty.
  std::vector<TrackMap> tap_notes_;

  /**
   * @brief Determine whether this note is for Player 1 or Player 2.
   * @param track the track/column the note is in.
   * @param tn the note in question. Required for routine mode.
   * @return true if it's for player 1, false for player 2. */
  bool IsPlayer1(const int track, const TapNote& tn) const;

  /**
   * @brief Determine if the note in question should be counted as a tap.
   * @param tn the note in question.
   * @param row the row it lives in.
   * @return true if it's a tap, false otherwise. */
  bool IsTap(const TapNote& tn, const int row) const;

  /**
   * @brief Determine if the note in question should be counted as a mine.
   * @param tn the note in question.
   * @param row the row it lives in.
   * @return true if it's a mine, false otherwise. */
  bool IsMine(const TapNote& tn, const int row) const;

  /**
   * @brief Determine if the note in question should be counted as a lift.
   * @param tn the note in question.
   * @param row the row it lives in.
   * @return true if it's a lift, false otherwise. */
  bool IsLift(const TapNote& tn, const int row) const;

  /**
   * @brief Determine if the note in question should be counted as a fake.
   * @param tn the note in question.
   * @param row the row it lives in.
   * @return true if it's a fake, false otherwise. */
  bool IsFake(const TapNote& tn, const int row) const;

  std::pair<int, int> GetNumRowsWithSimultaneousTapsTwoPlayer(
      int min_taps = 2, int start_row = 0, int end_row = MAX_NOTE_ROW) const;

  // These exist so that they can be revalidated when something that transforms
  // the NoteData occurs. -Kyz
  mutable std::set<all_tracks_iterator*> atis_;
  mutable std::set<all_tracks_const_iterator*> const_atis_;

  void AddATIToList(all_tracks_iterator* iter) const;
  void AddATIToList(all_tracks_const_iterator* iter) const;
  void RemoveATIFromList(all_tracks_iterator* iter) const;
  void RemoveATIFromList(all_tracks_const_iterator* iter) const;

 public:
  void Init();

  int GetNumTracks() const { return tap_notes_.size(); }
  void SetNumTracks(int new_num_tracks);
  bool IsComposite() const;

  /* Return the note at the given track and row.  Row may be out of
   * range; pretend the song goes on with TAP_EMPTYs indefinitely. */
  inline const TapNote& GetTapNote(unsigned track, int row) const {
    const_iterator iter = FindTapNote(track, row);
    if (iter != end(track)) {
      return iter->second;
    } else {
      return TAP_EMPTY;
    }
  }

  iterator FindTapNote(unsigned track, int row);
  const_iterator FindTapNote(unsigned track, int row) const;
  iterator RemoveTapNote(unsigned track, iterator it);

  /**
   * @brief Return an iterator range for [rowBegin,rowEnd).
   *
   * This can be used to efficiently iterate trackwise over a range of notes.
   * It's like FOREACH_NONEMPTY_ROW_IN_TRACK_RANGE, except it only requires
   * two map searches (iterating is constant time), but the iterators will
   * become invalid if the notes they represent disappear, so you need to
   * pay attention to how you modify the data.
   * @param iTrack the column to use.
   * @param iStartRow the starting point.
   * @param iEndRow the ending point.
   * @param begin the eventual beginning point of the range.
   * @param end the eventual end point of the range. */
  void GetTapNoteRange(
      int track, int start_row, int end_row, TrackMap::const_iterator& begin,
      TrackMap::const_iterator& end) const;
  /**
   * @brief Return a constant iterator range for [rowBegin,rowEnd).
   * @param iTrack the column to use.
   * @param iStartRow the starting point.
   * @param iEndRow the ending point.
   * @param begin the eventual beginning point of the range.
   * @param end the eventual end point of the range. */
  void GetTapNoteRange(
      int track, int start_row, int end_row, TrackMap::iterator& begin,
      TrackMap::iterator& end);
  all_tracks_iterator GetTapNoteRangeAllTracks(
      int start_row, int end_row, bool inclusive = false) {
    return all_tracks_iterator(*this, start_row, end_row, false, inclusive);
  }
  all_tracks_const_iterator GetTapNoteRangeAllTracks(
      int start_row, int end_row, bool inclusive = false) const {
    return all_tracks_const_iterator(
        *this, start_row, end_row, false, inclusive);
  }
  all_tracks_reverse_iterator GetTapNoteRangeAllTracksReverse(
      int start_row, int end_row, bool inclusive = false) {
    return all_tracks_iterator(*this, start_row, end_row, true, inclusive);
  }

  // Call this after using any transform that changes the NoteData.
  void RevalidateATIs(
      const std::vector<int>& added_or_removed_tracks, bool added);

  /* Return an iterator range include iStartRow to iEndRow.  Extend the range to
   * include hold notes overlapping the boundary. */
  void GetTapNoteRangeInclusive(
      int track, int start_row, int end_row, TrackMap::const_iterator& begin,
      TrackMap::const_iterator& end, bool include_adjacent = false) const;
  void GetTapNoteRangeInclusive(
      int track, int start_row, int end_row, TrackMap::iterator& begin,
      TrackMap::iterator& end, bool include_adjacent = false);

  /* Return an iterator range include iStartRow to iEndRow.  Shrink the range to
   * exclude hold notes overlapping the boundary. */
  void GetTapNoteRangeExclusive(
      int track, int start_row, int end_row, TrackMap::const_iterator& begin,
      TrackMap::const_iterator& end) const;

  /* Returns the row of the first TapNote on the track that has a row greater
   * than rowInOut. */
  bool GetNextTapNoteRowForTrack(
      int track, int& row_in_out, bool ignore_key_sounds = false) const;
  bool GetNextTapNoteRowForAllTracks(int& row_in_out) const;
  bool GetPrevTapNoteRowForTrack(int track, int& row_in_out) const;
  bool GetPrevTapNoteRowForAllTracks(int& row_in_out) const;

  void SetTapNote(int track, int row, const TapNote& tn);
  /**
   * @brief Add a hold note, merging other overlapping holds and destroying
   * tap notes underneath.
   * @param iTrack the column to work with.
   * @param iStartRow the starting row.
   * @param iEndRow the ending row.
   * @param tn the tap note. */
  void AddHoldNote(int track, int start_row, int end_row, TapNote tn);

  void ClearRangeForTrack(int row_begin, int row_end, int track);
  void ClearRange(int row_begin, int row_end);
  void ClearAll();
  void CopyRange(
      const NoteData& from, int row_from_begin, int row_from_end,
      int row_to_begin = 0);
  void CopyAll(const NoteData& from);

  bool IsRowEmpty(int row) const;
  bool IsRangeEmpty(int track, int row_begin, int row_end) const;
  int GetNumTapNonEmptyTracks(int row) const;
  void GetTapNonEmptyTracks(int row, std::set<int>& add_to) const;
  bool GetTapFirstNonEmptyTrack(int row, int& non_empty_track_out)
      const;  // return false if no non-empty tracks at row
  int GetNumTracksWithTap(int row) const;
  int GetNumTracksWithTapOrHoldHead(int row) const;
  int GetFirstTrackWithTap(int row) const;
  int GetFirstTrackWithTapOrHoldHead(int row) const;

  inline bool IsThereATapAtRow(int row) const {
    return GetFirstTrackWithTap(row) != -1;
  }
  inline bool IsThereATapOrHoldHeadAtRow(int row) const {
    return GetFirstTrackWithTapOrHoldHead(row) != -1;
  }
  void GetTracksHeldAtRow(int row, std::set<int>& add_to);
  int GetNumTracksHeldAtRow(int row);

  bool IsHoldNoteAtRow(int track, int row, int* head_row = nullptr) const;
  bool IsHoldHeadOrBodyAtRow(int track, int row, int* head_row) const;

  // statistics
  bool IsEmpty() const;
  bool IsTrackEmpty(int track) const { return tap_notes_[track].empty(); }
  int GetFirstRow() const;  // return the beat number of the first note
  int GetLastRow() const;   // return the beat number of the last note
  float GetFirstBeat() const { return NoteRowToBeat(GetFirstRow()); }
  float GetLastBeat() const { return NoteRowToBeat(GetLastRow()); }
  int GetNumTapNotes(int start_index = 0, int end_index = MAX_NOTE_ROW) const;
  int GetNumTapNotesNoTiming(
      int start_index = 0, int end_index = MAX_NOTE_ROW) const;
  int GetNumTapNotesInRow(int row) const;
  int GetNumMines(int start_index = 0, int end_index = MAX_NOTE_ROW) const;
  int GetNumRowsWithTapOrHoldHead(
      int start_index = 0, int end_index = MAX_NOTE_ROW) const;
  /* Optimization: for the default of start to end, use the second (faster).
   * XXX: Second what? -- Steve */
  int GetNumHoldNotes(int start_index = 0, int end_index = MAX_NOTE_ROW) const;
  int GetNumRolls(int start_index = 0, int end_index = MAX_NOTE_ROW) const;

  // Count rows that contain iMinTaps or more taps.
  int GetNumRowsWithSimultaneousTaps(
      int min_taps, int start_index = 0, int end_index = MAX_NOTE_ROW) const;
  int GetNumJumps(int start_index = 0, int end_index = MAX_NOTE_ROW) const {
    return GetNumRowsWithSimultaneousTaps(2, start_index, end_index);
  }

  // This row needs at least iMinSimultaneousPresses either tapped or held.
  bool RowNeedsAtLeastSimultaneousPresses(
      int min_simultaneous_presses, int row) const;

  // Count rows that need iMinSimultaneousPresses either tapped or held.
  int GetNumRowsWithSimultaneousPresses(
      int min_simultaneous_presses, int start_index = 0,
      int end_index = MAX_NOTE_ROW) const;
  int GetNumHands(int start_index = 0, int end_index = MAX_NOTE_ROW) const {
    return GetNumRowsWithSimultaneousPresses(3, start_index, end_index);
  }
  int GetNumQuads(int start_index = 0, int end_index = MAX_NOTE_ROW) const {
    return GetNumRowsWithSimultaneousPresses(4, start_index, end_index);
  }

  // and the other notetypes
  int GetNumLifts(int start_index = 0, int end_index = MAX_NOTE_ROW) const;
  int GetNumFakes(int start_index = 0, int end_index = MAX_NOTE_ROW) const;

  // the couple/routine style variants of the above.
  std::pair<int, int> GetNumTapNotesTwoPlayer(
      int start_row = 0, int end_row = MAX_NOTE_ROW) const;

  std::pair<int, int> GetNumJumpsTwoPlayer(
      int start_row = 0, int end_row = MAX_NOTE_ROW) const;

  std::pair<int, int> GetNumHandsTwoPlayer(
      int start_row = 0, int end_row = MAX_NOTE_ROW) const;

  std::pair<int, int> GetNumQuadsTwoPlayer(
      int start_row = 0, int end_row = MAX_NOTE_ROW) const;

  std::pair<int, int> GetNumHoldNotesTwoPlayer(
      int start_row = 0, int end_row = MAX_NOTE_ROW) const;

  std::pair<int, int> GetNumMinesTwoPlayer(
      int start_row = 0, int end_row = MAX_NOTE_ROW) const;

  std::pair<int, int> GetNumRollsTwoPlayer(
      int start_row = 0, int end_row = MAX_NOTE_ROW) const;

  std::pair<int, int> GetNumLiftsTwoPlayer(
      int start_row = 0, int end_row = MAX_NOTE_ROW) const;

  std::pair<int, int> GetNumFakesTwoPlayer(
      int start_row = 0, int end_row = MAX_NOTE_ROW) const;

  // Transformations
  void LoadTransformed(
      const NoteData& original, int new_num_tracks,
      const int
          original_track_to_take_from[]);  // -1 for iOriginalTracksToTakeFrom
                                           // means no track

  // XML
  XNode* CreateNode() const;
};

/** @brief Allow a quick way to swap notedata. */
namespace std {
template <>
inline void swap<NoteData>(NoteData& nd1, NoteData& nd2) noexcept {
  nd1.swap(nd2);
}
}  // namespace std

#endif

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
