#ifndef TIMING_DATA_H
#define TIMING_DATA_H

#include <array>
#include <cfloat>
#include <string>
#include <vector>

#include "EnumHelper.h"
#include "NoteTypes.h"
#include "RageUtil.h"
#include "TimingSegments.h"
#include "global.h"

struct lua_State;

#define TimingSegmentToXWithName(Seg, SegName, SegType)   \
  inline const Seg* To##SegName(const TimingSegment* t) { \
    ASSERT(t->GetType() == SegType);                      \
    return static_cast<const Seg*>(t);                    \
  }                                                       \
  inline Seg* To##SegName(TimingSegment* t) {             \
    ASSERT(t->GetType() == SegType);                      \
    return static_cast<Seg*>(t);                          \
  }

#define TimingSegmentToX(Seg, SegType) \
  TimingSegmentToXWithName(Seg##Segment, Seg, SEGMENT_##SegType)

/* ToBPM(TimingSegment*), ToTimeSignature(TimingSegment*), etc. */
TimingSegmentToX(BPM, BPM);
TimingSegmentToX(Stop, STOP);
TimingSegmentToX(Delay, DELAY);
TimingSegmentToX(TimeSignature, TIME_SIG);
TimingSegmentToX(Warp, WARP);
TimingSegmentToX(Label, LABEL);
TimingSegmentToX(Tickcount, TICKCOUNT);
TimingSegmentToX(Combo, COMBO);
TimingSegmentToX(Speed, SPEED);
TimingSegmentToX(Scroll, SCROLL);
TimingSegmentToX(Fake, FAKE);

#undef TimingSegmentToXWithName
#undef TimingSegmentToX

/**
 * @brief Holds data for translating beats<->seconds.
 */
class TimingData {
 public:
  /**
   * @brief Sets up initial timing data with a defined offset.
   * @param fOffset the offset from the 0th beat. */
  TimingData(float offset = 0);
  ~TimingData();

  void Copy(const TimingData& other);
  void Clear();
  bool IsSafeFullTiming();

  TimingData(const TimingData& cpy) { Copy(cpy); }
  TimingData& operator=(const TimingData& cpy) {
    Copy(cpy);
    return *this;
  }

  // Lookup tables for faster beat <-> time queries.
  struct GetBeatArgs {
    float elapsed_time_;
    float beat_;
    float bps_out_;
    float warp_dest_out_;
    int warp_begin_out_;
    bool freeze_out_;
    bool delay_out_;
    GetBeatArgs()
        : elapsed_time_(0),
          beat_(0),
          bps_out_(0),
          warp_dest_out_(0),
          warp_begin_out_(-1),
          freeze_out_(false),
          delay_out_(false) {}
  };
  struct GetBeatStarts {
    unsigned int bpm_;
    unsigned int warp_;
    unsigned int stop_;
    unsigned int delay_;
    int last_row_;
    float last_time_;
    float warp_destination_;
    bool is_warping_;
    // Row of the most recently entered warp.
    int warp_begin_row_;
    GetBeatStarts()
        : bpm_(0),
          warp_(0),
          stop_(0),
          delay_(0),
          last_row_(0),
          last_time_(0),
          warp_destination_(0),
          is_warping_(false),
          warp_begin_row_(-1) {}
  };
  struct lookup_item_t {
    float first_;
    GetBeatStarts second_;
    lookup_item_t(float f, GetBeatStarts& s) : first_(f), second_(s) {}
  };
  typedef std::vector<lookup_item_t> beat_start_lookup_t;
  mutable beat_start_lookup_t beat_start_lookup_;
  mutable beat_start_lookup_t time_start_lookup_;

  void EnsureLookups() const;
  void InvalidateLookups();
  int GetSegmentIndexAtRow(TimingSegmentType tst, int row) const;
  int GetSegmentIndexAtBeat(TimingSegmentType tst, float beat) const {
    return GetSegmentIndexAtRow(tst, BeatToNoteRow(beat));
  }

  float GetNextSegmentBeatAtRow(TimingSegmentType tst, int row) const;
  float GetNextSegmentBeatAtBeat(TimingSegmentType tst, float beat) const {
    return GetNextSegmentBeatAtRow(tst, BeatToNoteRow(beat));
  }

  float GetPreviousSegmentBeatAtRow(TimingSegmentType tst, int row) const;
  float GetPreviousSegmentBeatAtBeat(TimingSegmentType tst, float beat) const {
    return GetPreviousSegmentBeatAtRow(tst, BeatToNoteRow(beat));
  }

  bool empty() const;

  void CopyRange(
      int start_row, int end_row, TimingSegmentType copy_type, int dest_row,
      TimingData& dest) const;
  void ShiftRange(
      int start_row, int end_row, TimingSegmentType shift_type,
      int shift_amount);
  void ClearRange(int start_row, int end_row, TimingSegmentType clear_type);
  /**
   * @brief Gets the actual BPM of the song,
   * while respecting a limit.
   *
   * The high limit is due to the implementation of mMods.
   * @param fMinBPMOut the minimium specified BPM.
   * @param fMaxBPMOut the maximum specified BPM.
   * @param highest the highest allowed max BPM.
   */
  void GetActualBPM(
      float& min_bpm_out, float& max_bpm_out, float highest = FLT_MAX) const;

  /**
   * @brief Retrieve the TimingSegment at the specified row.
   * @param iNoteRow the row that has a TimingSegment.
   * @param tst the TimingSegmentType requested.
   * @return the segment in question.
   */
  const TimingSegment* GetSegmentAtRow(
      int note_row, TimingSegmentType tst) const;
  TimingSegment* GetSegmentAtRow(int note_row, TimingSegmentType tst);

  /**
   * @brief Retrieve the TimingSegment at the given beat.
   * @param fBeat the beat that has a TimingSegment.
   * @param tst the TimingSegmentType requested.
   * @return the segment in question.
   */
  const TimingSegment* GetSegmentAtBeat(
      float beat, TimingSegmentType tst) const {
    return GetSegmentAtRow(BeatToNoteRow(beat), tst);
  }
  TimingSegment* GetSegmentAtBeat(float beat, TimingSegmentType tst) {
    return GetSegmentAtRow(BeatToNoteRow(beat), tst);
  }

#define DefineSegmentWithName(Seg, SegName, SegType)             \
  const Seg* Get##Seg##AtRow(int iNoteRow) const {               \
    const TimingSegment* t = GetSegmentAtRow(iNoteRow, SegType); \
    return To##SegName(t);                                       \
  }                                                              \
  Seg* Get##Seg##AtRow(int iNoteRow) {                           \
    return const_cast<Seg*>(                                     \
        ((const TimingData*)this)->Get##Seg##AtRow(iNoteRow));   \
  }                                                              \
  const Seg* Get##Seg##AtBeat(float fBeat) const {               \
    return Get##Seg##AtRow(BeatToNoteRow(fBeat));                \
  }                                                              \
  Seg* Get##Seg##AtBeat(float fBeat) {                           \
    return const_cast<Seg*>(                                     \
        ((const TimingData*)this)->Get##Seg##AtBeat(fBeat));     \
  }                                                              \
  void AddSegment(const Seg& seg) { AddSegment(&seg); }

// "XXX: this comment (and quote mark) exists so nano won't
// display the rest of this file as one giant string

// (TimeSignature,TIME_SIG) -> (TimeSignatureSegment,SEGMENT_TIME_SIG)
#define DefineSegment(Seg, SegType) \
  DefineSegmentWithName(Seg##Segment, Seg, SEGMENT_##SegType)

  DefineSegment(BPM, BPM);
  DefineSegment(Stop, STOP);
  DefineSegment(Delay, DELAY);
  DefineSegment(Warp, WARP);
  DefineSegment(Label, LABEL);
  DefineSegment(Tickcount, TICKCOUNT);
  DefineSegment(Combo, COMBO);
  DefineSegment(Speed, SPEED);
  DefineSegment(Scroll, SCROLL);
  DefineSegment(Fake, FAKE);
  DefineSegment(TimeSignature, TIME_SIG);

#undef DefineSegmentWithName
#undef DefineSegment

  /* convenience aliases (Set functions are deprecated) */
  float GetBPMAtRow(int note_row) const {
    return GetBPMSegmentAtRow(note_row)->GetBPM();
  }
  float GetBPMAtBeat(float beat) const {
    return GetBPMAtRow(BeatToNoteRow(beat));
  }
  void SetBPMAtRow(int note_row, float bpm) {
    AddSegment(BPMSegment(note_row, bpm));
  }

  float GetStopAtRow(int note_row) const {
    return GetStopSegmentAtRow(note_row)->GetPause();
  }
  float GetStopAtBeat(float beat) const {
    return GetStopAtRow(BeatToNoteRow(beat));
  }
  void SetStopAtRow(int note_row, float seconds) {
    AddSegment(StopSegment(note_row, seconds));
  }
  void SetStopAtBeat(float beat, float seconds) {
    SetStopAtRow(BeatToNoteRow(beat), seconds);
  }

  float GetDelayAtRow(int note_row) const {
    return GetDelaySegmentAtRow(note_row)->GetPause();
  }
  float GetDelayAtBeat(float beat) const {
    return GetDelayAtRow(BeatToNoteRow(beat));
  }
  void SetDelayAtRow(int note_row, float seconds) {
    AddSegment(DelaySegment(note_row, seconds));
  }

  float GetWarpAtRow(int note_row) const {
    return GetWarpSegmentAtRow(note_row)->GetLength();
  }
  float GetWarpAtBeat(float beat) const {
    return GetWarpAtRow(BeatToNoteRow(beat));
  }
  void SetWarpAtBeat(float beat, float length) {
    AddSegment(WarpSegment(BeatToNoteRow(beat), length));
  }

  int GetTickcountAtRow(int note_row) const {
    return GetTickcountSegmentAtRow(note_row)->GetTicks();
  }
  int GetTickcountAtBeat(float beat) const {
    return GetTickcountAtRow(BeatToNoteRow(beat));
  }

  int GetComboAtRow(int note_row) const {
    return GetComboSegmentAtRow(note_row)->GetCombo();
  }
  int GetMissComboAtRow(int note_row) const {
    return GetComboSegmentAtRow(note_row)->GetMissCombo();
  }

  const std::string& GetLabelAtRow(int note_row) const {
    return GetLabelSegmentAtRow(note_row)->GetLabel();
  }
  const std::string& GetLabelAtBeat(float beat) const {
    return GetLabelAtRow(BeatToNoteRow(beat));
  }
  bool DoesLabelExist(const std::string& label) const;

  float GetSpeedPercentAtRow(int note_row) const {
    return GetSpeedSegmentAtRow(note_row)->GetRatio();
  }

  float GetSpeedWaitAtRow(int note_row) const {
    return GetSpeedSegmentAtRow(note_row)->GetDelay();
  }

  // XXX: is there any point to having specific unit types?
  SpeedSegment::BaseUnit GetSpeedModeAtRow(int note_row) const {
    return GetSpeedSegmentAtRow(note_row)->GetUnit();
  }

  void SetSpeedAtRow(
      int note_row, float percent, float wait, SpeedSegment::BaseUnit unit) {
    AddSegment(SpeedSegment(note_row, percent, wait, unit));
  }

  void SetSpeedPercentAtRow(int note_row, float percent) {
    const SpeedSegment* seg = GetSpeedSegmentAtRow(note_row);
    SetSpeedAtRow(note_row, percent, seg->GetDelay(), seg->GetUnit());
  }

  void SetSpeedWaitAtRow(int note_row, float wait) {
    const SpeedSegment* seg = GetSpeedSegmentAtRow(note_row);
    SetSpeedAtRow(note_row, seg->GetRatio(), wait, seg->GetUnit());
  }

  void SetSpeedModeAtRow(int note_row, SpeedSegment::BaseUnit unit) {
    const SpeedSegment* seg = GetSpeedSegmentAtRow(note_row);
    SetSpeedAtRow(note_row, seg->GetRatio(), seg->GetDelay(), unit);
  }

  void SetSpeedPercentAtBeat(float beat, float percent) {
    SetSpeedPercentAtRow(BeatToNoteRow(beat), percent);
  }
  void SetSpeedWaitAtBeat(float beat, float wait) {
    SetSpeedWaitAtRow(BeatToNoteRow(beat), wait);
  }
  void SetSpeedModeAtBeat(float beat, SpeedSegment::BaseUnit unit) {
    SetSpeedModeAtRow(BeatToNoteRow(beat), unit);
  }

  float GetDisplayedSpeedPercent(float beat, float music_seconds) const;

  float GetScrollAtRow(int note_row) const {
    return GetScrollSegmentAtRow(note_row)->GetRatio();
  }

  void SetScrollAtRow(int note_row, float percent) {
    AddSegment(ScrollSegment(note_row, percent));
  }
  void SetScrollAtBeat(float beat, float percent) {
    SetScrollAtRow(BeatToNoteRow(beat), percent);
  }

  float GetFakeAtRow(int row) const {
    return GetFakeSegmentAtRow(row)->GetLength();
  }
  float GetFakeAtBeat(float beat) const {
    return GetFakeAtRow(BeatToNoteRow(beat));
  }

  bool IsWarpAtRow(int row) const;
  bool IsFakeAtRow(int row) const;

  /**
   * @brief Determine if this notes on this row can be judged.
   * @param row the row to focus on.
   * @return true if the row can be judged, false otherwise. */
  bool IsJudgableAtRow(int row) const {
    return !IsWarpAtRow(row) && !IsFakeAtRow(row);
  }

  /**
   * @brief Get measure and beat at a note row.
   * @param iNoteRow the note row
   * @param iMeasureIndexOut returns the number of full measures from the
   *                         start of the song to iNoteRow
   * @param iBeatIndexOut returns the number of full beats from the start
   *                      of the last full measure to iNoteRow
   * @param iRowsRemainder returns the number of rows from the start of
   *                       the last full beat to iNoteRow
   */
  void NoteRowToMeasureAndBeat(
      int note_row, int& measure_index_out, int& beat_index_out,
      int& rows_remainder) const;

  void GetBeatInternal(GetBeatStarts& start, GetBeatArgs& args) const;
  float GetElapsedTimeInternal(GetBeatStarts& start, float beat) const;
  void GetBeatAndBPSFromElapsedTime(GetBeatArgs& args) const;
  float GetBeatFromElapsedTime(float elapsed_time)
      const  // shortcut for places that care only about the beat
  {
    GetBeatArgs args;
    args.elapsed_time_ = elapsed_time;
    GetBeatAndBPSFromElapsedTime(args);
    return args.beat_;
  }
  float GetElapsedTimeFromBeat(float beat) const;

  void GetBeatAndBPSFromElapsedTimeNoOffset(GetBeatArgs& args) const;
  float GetBeatFromElapsedTimeNoOffset(float elapsed_time)
      const  // shortcut for places that care only about the beat
  {
    GetBeatArgs args;
    args.elapsed_time_ = elapsed_time;
    GetBeatAndBPSFromElapsedTimeNoOffset(args);
    return args.beat_;
  }
  float GetElapsedTimeFromBeatNoOffset(float beat) const;

  bool HasBpmChanges() const {
    return GetTimingSegments(SEGMENT_BPM).size() > 1;
  }
  bool HasStops() const { return !GetTimingSegments(SEGMENT_STOP).empty(); }
  bool HasDelays() const { return !GetTimingSegments(SEGMENT_DELAY).empty(); }
  bool HasWarps() const { return !GetTimingSegments(SEGMENT_WARP).empty(); }
  bool HasFakes() const { return !GetTimingSegments(SEGMENT_FAKE).empty(); }

  bool HasSpeedChanges() const;
  bool HasScrollChanges() const;

  /**
   * @brief Compare two sets of timing data to see if they are equal.
   * @param other the other TimingData.
   * @return the equality or lack thereof of the two TimingData.
   */
  bool operator==(const TimingData& other) const {
    FOREACH_ENUM(TimingSegmentType, tst) {
      const std::vector<TimingSegment*>& us = timing_segments_[tst];
      const std::vector<TimingSegment*>& them = other.timing_segments_[tst];

      // optimization: check vector sizes before contents
      if (us.size() != them.size()) {
        return false;
      }

      for (unsigned i = 0; i < us.size(); ++i) {
        /* UGLY: since TimingSegment's comparison compares base data,
         * and the derived versions only compare derived data, we must
         * manually call each. */
        if (!(*us[i]).TimingSegment::operator==(*them[i])) {
          return false;
        }
        if (!(*us[i]).operator==(*them[i])) {
          return false;
        }
      }
    }

    return this->beat0_offset_in_seconds_ == other.beat0_offset_in_seconds_ &&
           this->beat0_group_offset_in_seconds_ ==
               other.beat0_group_offset_in_seconds_;
  }

  /**
   * @brief Compare two sets of timing data to see if they are not equal.
   * @param other the other TimingData.
   * @return the inequality or lack thereof of the two TimingData.
   */
  bool operator!=(const TimingData& other) const { return !operator==(other); }

  void ScaleRegion(
      float scale = 1, int start_row = 0, int end_row = MAX_NOTE_ROW,
      bool adjust_bpm = false);
  void InsertRows(int start_row, int rows_to_add);
  void DeleteRows(int start_row, int rows_to_delete);

  const std::vector<TimingSegment*>& GetTimingSegments(
      TimingSegmentType tst) const {
    return const_cast<TimingData*>(this)->GetTimingSegments(tst);
  }
  std::vector<TimingSegment*>& GetTimingSegments(TimingSegmentType tst) {
    return timing_segments_[tst];
  }

  /**
   * @brief Tidy up the timing data, e.g. provide default BPMs, labels,
   * tickcounts.
   * @param allowEmpty true if completely empty TimingData should be left
   *                   alone, false if it should be changed
   */
  void TidyUpData(bool allow_empty);

  // Lua
  void PushSelf(lua_State* L);

  /**
   * @brief The file of the song/steps that use this TimingData.
   *
   * This is for informational purposes only.
   */
  std::string file_;

  /** @brief The initial offset of a song. */
  float beat0_offset_in_seconds_;

  /** @brief The sync offset of this group */
  float beat0_group_offset_in_seconds_;

  // XXX: this breaks encapsulation. get rid of it ASAP
  std::vector<std::string> ToVectorString(
      TimingSegmentType tst, int dec = 6) const;

 protected:
  // don't call this directly; use the derived-type overloads.
  void AddSegment(const TimingSegment* seg);

  // All of the following vectors must be sorted before gameplay.
  std::array<std::vector<TimingSegment*>, NUM_TimingSegmentType>
      timing_segments_;
};

#endif

/**
 * @file
 * @author Chris Danford, Glenn Maynard (c) 2001-2004
 * @section LICENSE
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
