#include "TimingData.h"

#include <algorithm>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include "GameState.h"
#include "LuaManager.h"
#include "NoteTypes.h"
#include "PrefsManager.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "TimingSegments.h"
#include "global.h"

static void EraseSegment(
    std::vector<TimingSegment*>& segs, int index, TimingSegment* cur);
static const int kInvalidIndex = -1;

TimingSegment* GetSegmentAtRow(int note_row, TimingSegmentType tst);

TimingData::TimingData(float offset)
    : beat0_offset_in_seconds_(offset), beat0_group_offset_in_seconds_(0.0f) {}

void TimingData::Copy(const TimingData& cpy) {
  /* de-allocate any old pointers we had */
  Clear();

  beat0_offset_in_seconds_ = cpy.beat0_offset_in_seconds_;
  beat0_group_offset_in_seconds_ = cpy.beat0_group_offset_in_seconds_;
  file_ = cpy.file_;

  FOREACH_TimingSegmentType(tst) {
    const std::vector<TimingSegment*>& segs = cpy.timing_segments_[tst];

    for (unsigned i = 0; i < segs.size(); ++i) {
      AddSegment(segs[i]);
    }
  }
}

void TimingData::Clear() {
  InvalidateLookups();
  /* Delete all pointers owned by this TimingData. */
  FOREACH_TimingSegmentType(tst) {
    std::vector<TimingSegment*>& segs = timing_segments_[tst];
    for (unsigned i = 0; i < segs.size(); ++i) {
      RageUtil::SafeDelete(segs[i]);
    }

    segs.clear();
  }
}

bool TimingData::IsSafeFullTiming() {
  static std::vector<TimingSegmentType> needed_segments;
  if (needed_segments.empty()) {
    needed_segments.push_back(SEGMENT_BPM);
    needed_segments.push_back(SEGMENT_TIME_SIG);
    needed_segments.push_back(SEGMENT_TICKCOUNT);
    needed_segments.push_back(SEGMENT_COMBO);
    needed_segments.push_back(SEGMENT_LABEL);
    needed_segments.push_back(SEGMENT_SPEED);
    needed_segments.push_back(SEGMENT_SCROLL);
  }
  for (size_t s = 0; s < needed_segments.size(); ++s) {
    if (timing_segments_[needed_segments[s]].empty()) {
      return false;
    }
  }
  return true;
}

TimingData::~TimingData() { Clear(); }

static const TimingData::GetBeatStarts& LookupResumeStart(
    const TimingData::beat_start_lookup_t& lookup, float key) {
  TimingData::beat_start_lookup_t::const_iterator it = std::lower_bound(
      lookup.begin(), lookup.end(), key,
      [](const TimingData::lookup_item_t& item, float k) {
        return item.first_ < k;
      });
  if (it != lookup.begin()) {
    --it;
  }
  return it->second_;
}

bool TimingData::empty() const {
  FOREACH_TimingSegmentType(
      tst) if (!GetTimingSegments(tst).empty()) return false;

  return true;
}

void TimingData::CopyRange(
    int start_row, int end_row, TimingSegmentType copy_type, int dest_row,
    TimingData& dest) const {
  int row_offset = dest_row - start_row;
  FOREACH_TimingSegmentType(seg_type) {
    if (seg_type == copy_type || copy_type == TimingSegmentType_Invalid) {
      const std::vector<TimingSegment*>& segs = GetTimingSegments(seg_type);
      for (size_t i = 0; i < segs.size(); ++i) {
        if (segs[i]->GetRow() >= start_row && segs[i]->GetRow() <= end_row) {
          TimingSegment* copy = segs[i]->Copy();
          copy->SetRow(segs[i]->GetRow() + row_offset);
          dest.AddSegment(copy);
          // TimingSegment::Copy creates a new segment with new, and
          // AddSegment copies it again, so delete the temp. -Kyz
          delete copy;
        }
      }
    }
  }
}

void TimingData::ShiftRange(
    int start_row, int end_row, TimingSegmentType shift_type,
    int shift_amount) {
  InvalidateLookups();
  FOREACH_TimingSegmentType(seg_type) {
    if (seg_type == shift_type || shift_type == TimingSegmentType_Invalid) {
      std::vector<TimingSegment*>& segs = GetTimingSegments(seg_type);
      int first_row = std::min(start_row, start_row + shift_amount);
      int last_row = std::max(end_row, end_row + shift_amount);
      int first_affected = GetSegmentIndexAtRow(seg_type, first_row);
      int last_affected = GetSegmentIndexAtRow(seg_type, last_row);
      if (first_affected == kInvalidIndex || last_affected == kInvalidIndex) {
        continue;
      }
      // Prance through the affected area twice.  The first time, changing
      // the rows of the segments, the second time removing segments that
      // have been run over by a segment being moved.  Attempts to combine
      // both operations into a single loop were error prone. -Kyz
      for (size_t i = first_affected;
           i <= static_cast<size_t>(last_affected) && i < segs.size(); ++i) {
        int seg_row = segs[i]->GetRow();
        if (seg_row > 0 && seg_row >= start_row && seg_row <= end_row) {
          int dest_row = std::max(seg_row + shift_amount, 0);
          segs[i]->SetRow(dest_row);
        }
      }
      for (size_t i = first_affected;
           i <= static_cast<size_t>(last_affected) && i < segs.size(); ++i) {
        bool erased = false;
        int seg_row = segs[i]->GetRow();
        if (i < segs.size() - 1) {
          int next_row = segs[i + 1]->GetRow();
          // This is a loop so that it will go back through and remove all
          // segments that were run over. -Kyz
          while (seg_row >= next_row && seg_row < start_row) {
            if (segs.size() > 1) {
              EraseSegment(segs, i, segs[i]);
              --i;
              --last_affected;
              erased = true;
            }
            if (i < segs.size()) {
              seg_row = segs[i]->GetRow();
            } else {
              seg_row = -1;
            }
          }
        }
        if (!erased && i > 0) {
          int prev_row = segs[i - 1]->GetRow();
          if (prev_row >= seg_row) {
            if (segs.size() > 1) {
              EraseSegment(segs, i, segs[i]);
              --i;
              --last_affected;
              erased = true;
            }
          }
        }
      }
    }
  }
}

void TimingData::ClearRange(
    int start_row, int end_row, TimingSegmentType clear_type) {
  InvalidateLookups();
  FOREACH_TimingSegmentType(seg_type) {
    if (seg_type == clear_type || clear_type == TimingSegmentType_Invalid) {
      std::vector<TimingSegment*>& segs = GetTimingSegments(seg_type);
      int first_affected = GetSegmentIndexAtRow(seg_type, start_row);
      int last_affected = GetSegmentIndexAtRow(seg_type, end_row);
      if (first_affected == kInvalidIndex) {
        continue;
      }
      for (int index = last_affected; index >= first_affected; --index) {
        int seg_row = segs[index]->GetRow();
        if (segs.size() > 1 && seg_row > 0 && seg_row >= start_row &&
            seg_row <= end_row) {
          EraseSegment(segs, index, segs[index]);
        }
      }
    }
  }
}

void TimingData::GetActualBPM(
    float& min_bpm_out, float& max_bpm_out, float highest) const {
  min_bpm_out = FLT_MAX;
  max_bpm_out = 0;
  const std::vector<TimingSegment*>& bpms = GetTimingSegments(SEGMENT_BPM);

  for (unsigned i = 0; i < bpms.size(); i++) {
    const float bpm = ToBPM(bpms[i])->GetBPM();
    max_bpm_out = std::clamp(std::max(bpm, max_bpm_out), 0.0f, highest);
    min_bpm_out = std::min(bpm, min_bpm_out);
  }
}

float TimingData::GetNextSegmentBeatAtRow(
    TimingSegmentType tst, int row) const {
  const std::vector<TimingSegment*> segs = GetTimingSegments(tst);
  for (unsigned i = 0; i < segs.size(); i++) {
    if (segs[i]->GetRow() <= row) {
      continue;
    }
    return segs[i]->GetBeat();
  }
  return NoteRowToBeat(row);
}

float TimingData::GetPreviousSegmentBeatAtRow(
    TimingSegmentType tst, int row) const {
  float backup = -1;
  const std::vector<TimingSegment*> segs = GetTimingSegments(tst);
  for (unsigned i = 0; i < segs.size(); i++) {
    if (segs[i]->GetRow() >= row) {
      break;
    }
    backup = segs[i]->GetBeat();
  }
  return (backup > -1) ? backup : NoteRowToBeat(row);
}

int TimingData::GetSegmentIndexAtRow(TimingSegmentType tst, int row) const {
  const std::vector<TimingSegment*>& segs = GetTimingSegments(tst);

  if (segs.empty()) {
    return kInvalidIndex;
  }

  int min = 0, max = segs.size() - 1;
  int l = min, r = max;
  while (l <= r) {
    int m = (l + r) / 2;
    if ((m == min || segs[m]->GetRow() <= row) &&
        (m == max || row < segs[m + 1]->GetRow())) {
      return m;
    } else if (segs[m]->GetRow() <= row) {
      l = m + 1;
    } else {
      r = m - 1;
    }
  }

  // iRow is before the first segment of type tst
  return kInvalidIndex;
}

struct ts_less {
  bool operator()(const TimingSegment* x, const TimingSegment* y) const {
    return (*x) < (*y);
  }
};

bool TimingData::IsWarpAtRow(int note_row) const {
  const std::vector<TimingSegment*>& warps = GetTimingSegments(SEGMENT_WARP);
  if (warps.empty()) {
    return false;
  }

  int i = GetSegmentIndexAtRow(SEGMENT_WARP, note_row);
  if (i == -1) {
    return false;
  }
  const WarpSegment* s = ToWarp(warps[i]);
  float beat_row = NoteRowToBeat(note_row);
  if (s->GetBeat() <= beat_row && beat_row < (s->GetBeat() + s->GetLength())) {
    // Allow stops inside warps to allow things like stop, warp, stop, warp,
    // stop, and so on.
    if (GetTimingSegments(SEGMENT_STOP).empty() &&
        GetTimingSegments(SEGMENT_DELAY).empty()) {
      return true;
    }
    if (GetStopAtRow(note_row) != 0.0f || GetDelayAtRow(note_row) != 0.0f) {
      return false;
    }
    return true;
  }
  return false;
}

bool TimingData::IsFakeAtRow(int note_row) const {
  const std::vector<TimingSegment*>& fakes = GetTimingSegments(SEGMENT_FAKE);
  if (fakes.empty()) {
    return false;
  }

  int i = GetSegmentIndexAtRow(SEGMENT_FAKE, note_row);
  if (i == -1) {
    return false;
  }
  const FakeSegment* s = ToFake(fakes[i]);
  float beat_row = NoteRowToBeat(note_row);
  if (s->GetBeat() <= beat_row && beat_row < (s->GetBeat() + s->GetLength())) {
    return true;
  }
  return false;
}

/* DummySegments: since our model relies on being able to get a segment at will,
 * whether one exists or not, we have a bunch of dummies to return if there is
 * no segment. It's kind of kludgy, but when we have functions making
 * indiscriminate calls to get segments at arbitrary rows, I think it's the
 * best solution we've got for now.
 *
 * Note that types whose SegmentEffectAreas are "Indefinite" are nullptr here,
 * because they should never need to be used; we always have at least one such
 * segment in the TimingData, and if not, we'll crash anyway. -- vyhd */
static const TimingSegment* dummy_segments[NUM_TimingSegmentType] = {
    nullptr,  // BPMSegment
    new StopSegment, new DelaySegment,
    nullptr,  // TimeSignatureSegment
    new WarpSegment,
    nullptr,  // LabelSegment
    nullptr,  // TickcountSegment
    nullptr,  // ComboSegment
    nullptr,  // SpeedSegment
    nullptr,  // ScrollSegment
    new FakeSegment};

const TimingSegment* TimingData::GetSegmentAtRow(
    int note_row, TimingSegmentType tst) const {
  const std::vector<TimingSegment*>& segments = GetTimingSegments(tst);

  if (segments.empty()) {
    return dummy_segments[tst];
  }

  int index = GetSegmentIndexAtRow(tst, note_row);
  const TimingSegment* seg = segments[index];

  switch (seg->GetEffectType()) {
    case SegmentEffectType_Indefinite: {
      // this segment is in effect at this row
      return seg;
    }
    default: {
      // if the returned segment isn't exactly on this row,
      // we don't want it, return a dummy instead
      if (seg->GetRow() == note_row) {
        return seg;
      } else {
        return dummy_segments[tst];
      }
    }
  }
}

TimingSegment* TimingData::GetSegmentAtRow(
    int note_row, TimingSegmentType tst) {
  return const_cast<TimingSegment*>(
      static_cast<const TimingData*>(this)->GetSegmentAtRow(note_row, tst));
}

static void EraseSegment(
    std::vector<TimingSegment*>& segs, int index, TimingSegment* cur) {
  segs.erase(segs.begin() + index);
  RageUtil::SafeDelete(cur);
}

// NOTE: the pointer we're passed is a reference to a temporary,
// so we must deep-copy it (with ::Copy) for new allocations.
void TimingData::AddSegment(const TimingSegment* seg) {
  InvalidateLookups();

  TimingSegmentType tst = seg->GetType();
  std::vector<TimingSegment*>& segs = timing_segments_[tst];

  // OPTIMIZATION: if this is our first segment, push and return.
  if (segs.empty()) {
    segs.push_back(seg->Copy());
    return;
  }

  int index = GetSegmentIndexAtRow(tst, seg->GetRow());
  ASSERT(index != kInvalidIndex);
  TimingSegment* cur = segs[index];

  bool is_notable = seg->IsNotable();
  bool on_same_row = seg->GetRow() == cur->GetRow();

  // ignore changes that are zero and don't overwrite an existing segment
  if (!is_notable && !on_same_row) {
    return;
  }

  switch (seg->GetEffectType()) {
    case SegmentEffectType_Row:
    case SegmentEffectType_Range: {
      // if we're overwriting a change with a non-notable
      // one, take it to mean deleting the existing segment
      if (on_same_row && !is_notable) {
        EraseSegment(segs, index, cur);
        return;
      }

      break;
    }
    case SegmentEffectType_Indefinite: {
      TimingSegment* prev = cur;

      // get the segment before last; if we're on the same
      // row, get the segment in effect before 'cur'
      if (on_same_row && index > 0) {
        prev = segs[index - 1];
      }

      // If there is another segment after this one, it might become
      // redundant when this one is inserted.
      // If the next segment is redundant, we want to move its starting row
      // to the row the new segment is being added at instead of erasing it
      // and adding the new segment.
      // If the new segment is also redundant, erase the next segment because
      // that effectively moves it back to the prev segment. -Kyz
      if (static_cast<size_t>(index) < segs.size() - 1) {
        TimingSegment* next = segs[index + 1];
        if ((*seg) == (*next)) {
          // The segment after this new one is redundant.
          if ((*seg) == (*prev)) {
            // This new segment is redundant.  Erase the next segment and
            // ignore this new one.
            EraseSegment(segs, index + 1, next);
            if (prev != cur) {
              EraseSegment(segs, index, cur);
            }
            return;
          } else {
            // Move the next segment's start back to this row.
            next->SetRow(seg->GetRow());
            if (prev != cur) {
              EraseSegment(segs, index, cur);
            }
            return;
          }
        } else {
          // if true, this is redundant segment change
          if ((*prev) == (*seg)) {
            if (prev != cur) {
              EraseSegment(segs, index, cur);
            }
            return;
          }
        }
      } else {
        // if true, this is redundant segment change
        if ((*prev) == (*seg)) {
          if (prev != cur) {
            EraseSegment(segs, index, cur);
          }
          return;
        }
      }
      break;
    }
    default:
      break;
  }

  // the segment at or before this row is equal to the new one; ignore it
  if (on_same_row && (*cur) == (*seg)) {
    return;
  }

  // Copy() the segment (which allocates a new segment), assign it
  // to the position of the old one, then delete the old pointer.
  TimingSegment* cpy = seg->Copy();

  if (on_same_row) {
    // delete the existing pointer and replace it
    RageUtil::SafeDelete(cur);
    segs[index] = cpy;
  } else {
    // copy and insert a new segment
    std::vector<TimingSegment*>::iterator it;
    it = upper_bound(segs.begin(), segs.end(), cpy, ts_less());
    segs.insert(it, cpy);
  }
}

bool TimingData::DoesLabelExist(const std::string& label) const {
  const std::vector<TimingSegment*>& labels = GetTimingSegments(SEGMENT_LABEL);
  for (unsigned i = 0; i < labels.size(); i++) {
    if (ToLabel(labels[i])->GetLabel() == label) {
      return true;
    }
  }
  return false;
}

void TimingData::GetBeatAndBPSFromElapsedTime(GetBeatArgs& args) const {
  args.elapsed_time_ += GAMESTATE->song_options_.GetCurrent().music_rate_ *
                        PREFSMAN->global_offset_seconds_;
  GetBeatAndBPSFromElapsedTimeNoOffset(args);
}

enum {
  FOUND_WARP,
  FOUND_WARP_DESTINATION,
  FOUND_BPM_CHANGE,
  FOUND_STOP,
  FOUND_DELAY,
  FOUND_MARKER,
  NOT_FOUND
};

void FindEvent(
    int& event_row, int& event_type, TimingData::GetBeatStarts& start,
    float beat, bool find_marker, const std::vector<TimingSegment*>& bpms,
    const std::vector<TimingSegment*>& warps,
    const std::vector<TimingSegment*>& stops,
    const std::vector<TimingSegment*>& delays) {
  if (start.is_warping_ && BeatToNoteRow(start.warp_destination_) < event_row) {
    event_row = BeatToNoteRow(start.warp_destination_);
    event_type = FOUND_WARP_DESTINATION;
  }
  if (start.bpm_ < bpms.size() && bpms[start.bpm_]->GetRow() < event_row) {
    event_row = bpms[start.bpm_]->GetRow();
    event_type = FOUND_BPM_CHANGE;
  }
  if (start.delay_ < delays.size() &&
      delays[start.delay_]->GetRow() < event_row) {
    event_row = delays[start.delay_]->GetRow();
    event_type = FOUND_DELAY;
  }
  if (find_marker && BeatToNoteRow(beat) < event_row) {
    event_row = BeatToNoteRow(beat);
    event_type = FOUND_MARKER;
  }
  if (start.stop_ < stops.size() && stops[start.stop_]->GetRow() < event_row) {
    event_row = stops[start.stop_]->GetRow();
    event_type = FOUND_STOP;
  }
  if (start.warp_ < warps.size() && warps[start.warp_]->GetRow() < event_row) {
    event_row = warps[start.warp_]->GetRow();
    event_type = FOUND_WARP;
  }
}

void TimingData::InvalidateLookups() {
  time_start_lookup_.clear();
  beat_start_lookup_.clear();
}

void TimingData::EnsureLookups() const {
  if (!time_start_lookup_.empty()) {
    return;
  }

  const std::vector<TimingSegment*>& bpms = timing_segments_[SEGMENT_BPM];
  const std::vector<TimingSegment*>& warps = timing_segments_[SEGMENT_WARP];
  const std::vector<TimingSegment*>& stops = timing_segments_[SEGMENT_STOP];
  const std::vector<TimingSegment*>& delays = timing_segments_[SEGMENT_DELAY];
  size_t event_count =
      bpms.size() + warps.size() + stops.size() + delays.size();
  time_start_lookup_.reserve(event_count + 1);
  beat_start_lookup_.reserve(event_count + 1);

  GetBeatStarts start;
  time_start_lookup_.push_back(
      lookup_item_t(NoteRowToBeat(start.last_row_), start));
  beat_start_lookup_.push_back(lookup_item_t(start.last_time_, start));
  float bps = GetBPMAtRow(start.last_row_) / 60.0f;
  while (true) {
    int event_row = INT_MAX;
    int event_type = NOT_FOUND;
    FindEvent(
        event_row, event_type, start, FLT_MAX, false, bpms, warps, stops,
        delays);
    if (event_type == NOT_FOUND) {
      break;
    }
    start.last_time_ += start.is_warping_
                            ? 0
                            : NoteRowToBeat(event_row - start.last_row_) / bps;
    switch (event_type) {
      case FOUND_WARP_DESTINATION:
        start.is_warping_ = false;
        break;
      case FOUND_BPM_CHANGE:
        bps = ToBPM(bpms[start.bpm_])->GetBPS();
        ++start.bpm_;
        break;
      case FOUND_STOP:
        start.last_time_ += ToStop(stops[start.stop_])->GetPause();
        ++start.stop_;
        break;
      case FOUND_DELAY:
        start.last_time_ += ToDelay(delays[start.delay_])->GetPause();
        ++start.delay_;
        break;
      case FOUND_WARP: {
        start.is_warping_ = true;
        start.warp_begin_row_ = event_row;
        WarpSegment* ws = ToWarp(warps[start.warp_]);
        float warp_sum = ws->GetLength() + ws->GetBeat();
        if (warp_sum > start.warp_destination_) {
          start.warp_destination_ = warp_sum;
        }
        ++start.warp_;
        break;
      }
    }
    start.last_row_ = event_row;
    time_start_lookup_.push_back(
        lookup_item_t(NoteRowToBeat(start.last_row_), start));
    beat_start_lookup_.push_back(lookup_item_t(start.last_time_, start));
  }
}

void TimingData::GetBeatInternal(
    GetBeatStarts& start, GetBeatArgs& args) const {
  const std::vector<TimingSegment*>& bpms = timing_segments_[SEGMENT_BPM];
  const std::vector<TimingSegment*>& warps = timing_segments_[SEGMENT_WARP];
  const std::vector<TimingSegment*>& stops = timing_segments_[SEGMENT_STOP];
  const std::vector<TimingSegment*>& delays = timing_segments_[SEGMENT_DELAY];

  float bps = GetBPMAtRow(start.last_row_) / 60.0f;
  while (true) {
    int event_row = INT_MAX;
    int event_type = NOT_FOUND;
    FindEvent(
        event_row, event_type, start, 0, false, bpms, warps, stops, delays);
    if (event_type == NOT_FOUND) {
      break;
    }
    float time_to_next_event =
        start.is_warping_ ? 0
                          : NoteRowToBeat(event_row - start.last_row_) / bps;
    float next_event_time = start.last_time_ + time_to_next_event;
    if (args.elapsed_time_ < next_event_time) {
      break;
    }
    start.last_time_ = next_event_time;
    switch (event_type) {
      case FOUND_WARP_DESTINATION:
        start.is_warping_ = false;
        break;
      case FOUND_BPM_CHANGE:
        bps = ToBPM(bpms[start.bpm_])->GetBPS();
        ++start.bpm_;
        break;
      case FOUND_DELAY: {
        const DelaySegment* ss = ToDelay(delays[start.delay_]);
        time_to_next_event = ss->GetPause();
        next_event_time = start.last_time_ + time_to_next_event;
        if (args.elapsed_time_ < next_event_time) {
          args.freeze_out_ = false;
          args.delay_out_ = true;
          args.beat_ = ss->GetBeat();
          args.bps_out_ = bps;
          return;
        }
        start.last_time_ = next_event_time;
        ++start.delay_;
        break;
      }
      case FOUND_STOP: {
        const StopSegment* ss = ToStop(stops[start.stop_]);
        time_to_next_event = ss->GetPause();
        next_event_time = start.last_time_ + time_to_next_event;
        if (args.elapsed_time_ < next_event_time) {
          args.freeze_out_ = true;
          args.delay_out_ = false;
          args.beat_ = ss->GetBeat();
          args.bps_out_ = bps;
          return;
        }
        start.last_time_ = next_event_time;
        ++start.stop_;
        break;
      }
      case FOUND_WARP: {
        start.is_warping_ = true;
        const WarpSegment* ws = ToWarp(warps[start.warp_]);
        float warp_sum = ws->GetLength() + ws->GetBeat();
        if (warp_sum > start.warp_destination_) {
          start.warp_destination_ = warp_sum;
        }
        args.warp_begin_out_ = event_row;
        args.warp_dest_out_ = start.warp_destination_;
        ++start.warp_;
        break;
      }
    }
    start.last_row_ = event_row;
  }
  args.beat_ = NoteRowToBeat(start.last_row_) +
               (args.elapsed_time_ - start.last_time_) * bps;
  args.bps_out_ = bps;
}

void TimingData::GetBeatAndBPSFromElapsedTimeNoOffset(GetBeatArgs& args) const {
  EnsureLookups();
  float offset = beat0_offset_in_seconds_ + beat0_group_offset_in_seconds_;
  args.elapsed_time_ += offset;
  GetBeatStarts start =
      LookupResumeStart(beat_start_lookup_, args.elapsed_time_);
  args.warp_begin_out_ = start.warp_begin_row_;
  args.warp_dest_out_ = start.warp_destination_;
  GetBeatInternal(start, args);
  args.elapsed_time_ -= offset;
}

float TimingData::GetElapsedTimeInternal(
    GetBeatStarts& start, float beat) const {
  const std::vector<TimingSegment*>& bpms = timing_segments_[SEGMENT_BPM];
  const std::vector<TimingSegment*>& warps = timing_segments_[SEGMENT_WARP];
  const std::vector<TimingSegment*>& stops = timing_segments_[SEGMENT_STOP];
  const std::vector<TimingSegment*>& delays = timing_segments_[SEGMENT_DELAY];

  float bps = GetBPMAtRow(start.last_row_) / 60.0f;

  while (true) {
    int event_row = INT_MAX;
    int event_type = NOT_FOUND;
    FindEvent(
        event_row, event_type, start, beat, true, bpms, warps, stops, delays);
    float time_to_next_event =
        start.is_warping_ ? 0
                          : NoteRowToBeat(event_row - start.last_row_) / bps;
    float next_event_time = start.last_time_ + time_to_next_event;
    start.last_time_ = next_event_time;
    switch (event_type) {
      case FOUND_WARP_DESTINATION:
        start.is_warping_ = false;
        break;
      case FOUND_BPM_CHANGE:
        bps = ToBPM(bpms[start.bpm_])->GetBPS();
        ++start.bpm_;
        break;
      case FOUND_STOP:
        time_to_next_event = ToStop(stops[start.stop_])->GetPause();
        next_event_time = start.last_time_ + time_to_next_event;
        start.last_time_ = next_event_time;
        ++start.stop_;
        break;
      case FOUND_DELAY:
        time_to_next_event = ToDelay(delays[start.delay_])->GetPause();
        next_event_time = start.last_time_ + time_to_next_event;
        start.last_time_ = next_event_time;
        ++start.delay_;
        break;
      case FOUND_MARKER:
        return start.last_time_;
      case FOUND_WARP: {
        start.is_warping_ = true;
        WarpSegment* ws = ToWarp(warps[start.warp_]);
        float warp_sum = ws->GetLength() + ws->GetBeat();
        if (warp_sum > start.warp_destination_) {
          start.warp_destination_ = warp_sum;
        }
        ++start.warp_;
        break;
      }
    }
    start.last_row_ = event_row;
  }
}

float TimingData::GetElapsedTimeFromBeat(float beat) const {
  return TimingData::GetElapsedTimeFromBeatNoOffset(beat) -
         GAMESTATE->song_options_.GetCurrent().music_rate_ *
             PREFSMAN->global_offset_seconds_;
}

float TimingData::GetElapsedTimeFromBeatNoOffset(float beat) const {
  EnsureLookups();
  GetBeatStarts start = LookupResumeStart(time_start_lookup_, beat);
  GetElapsedTimeInternal(start, beat);
  return start.last_time_ - beat0_offset_in_seconds_ -
         beat0_group_offset_in_seconds_;
}

void TimingData::ScaleRegion(
    float scale, int start_index, int end_index, bool adjust_bpm) {
  ASSERT(scale > 0);
  ASSERT(start_index >= 0);
  ASSERT(start_index < end_index);

  InvalidateLookups();

  int length = end_index - start_index;
  int new_length = std::lrint(scale * length);

  FOREACH_TimingSegmentType(tst) for (unsigned j = 0;
                                      j < timing_segments_[tst].size(); j++)
      timing_segments_[tst][j]
          ->Scale(start_index, length, new_length);

  // adjust BPM changes to preserve timing
  if (adjust_bpm) {
    int new_end_index = start_index + new_length;
    float end_bpm_before_scaling = GetBPMAtRow(new_end_index);
    std::vector<TimingSegment*>& bpms = timing_segments_[SEGMENT_BPM];

    // adjust BPM changes "between" iStartIndex and iNewEndIndex
    for (unsigned i = 0; i < bpms.size(); i++) {
      BPMSegment* bpm = ToBPM(bpms[i]);
      const int seg_start = bpm->GetRow();
      if (seg_start <= start_index) {
        continue;
      } else if (seg_start >= new_end_index) {
        continue;
      } else {
        bpm->SetBPM(bpm->GetBPM() * scale);
      }
    }

    // set BPM at iStartIndex and iNewEndIndex.
    SetBPMAtRow(start_index, GetBPMAtRow(start_index) * scale);
    SetBPMAtRow(new_end_index, end_bpm_before_scaling);
  }
}

void TimingData::InsertRows(int start_row, int rows_to_add) {
  InvalidateLookups();
  FOREACH_TimingSegmentType(tst) {
    std::vector<TimingSegment*>& segs = timing_segments_[tst];
    for (unsigned j = 0; j < segs.size(); j++) {
      TimingSegment* seg = segs[j];
      if (seg->GetRow() < start_row) {
        continue;
      }
      seg->SetRow(seg->GetRow() + rows_to_add);
    }
  }

  if (start_row == 0) {
    /* If we're shifting up at the beginning, we just shifted up the first
     * BPMSegment. That segment must always begin at 0. */
    std::vector<TimingSegment*>& bpms = timing_segments_[SEGMENT_BPM];
    ASSERT_M(
        bpms.size() > 0,
        "There must be at least one BPM Segment in the chart!");
    bpms[0]->SetRow(0);
  }
}

// Delete timing changes in [iStartRow, iStartRow + iRowsToDelete) and shift up.
void TimingData::DeleteRows(int start_row, int rows_to_delete) {
  InvalidateLookups();
  FOREACH_TimingSegmentType(tst) {
    // Don't delete the indefinite segments that are still in effect
    // at the end row; rather, shift them so they start there.
    TimingSegment* end = GetSegmentAtRow(start_row + rows_to_delete, tst);
    if (end != nullptr &&
        end->GetEffectType() == SegmentEffectType_Indefinite &&
        start_row <= end->GetRow() &&
        end->GetRow() < start_row + rows_to_delete) {
      // The iRowsToDelete will eventually be subtracted out
      LOG->Trace(
          "Segment at row %d shifted to %d", end->GetRow(),
          start_row + rows_to_delete);
      end->SetRow(start_row + rows_to_delete);
    }

    // Now delete and shift up
    std::vector<TimingSegment*>& segs = timing_segments_[tst];
    for (unsigned j = 0; j < segs.size(); j++) {
      TimingSegment* seg = segs[j];
      // Before deleted region:
      if (seg->GetRow() < start_row) {
        continue;
      }
      // Inside deleted region:
      if (seg->GetRow() < start_row + rows_to_delete) {
        segs.erase(segs.begin() + j, segs.begin() + j + 1);
        --j;
        continue;
      }

      // After deleted regions:
      seg->SetRow(seg->GetRow() - rows_to_delete);
    }
  }
}

float TimingData::GetDisplayedSpeedPercent(
    float song_beat, float music_seconds) const {
  const std::vector<TimingSegment*>& speeds = GetTimingSegments(SEGMENT_SPEED);
  if (speeds.size() == 0) {
#ifdef DEBUG
    LOG->Trace("No speed segments found: using default value.");
#endif
    return 1.0f;
  }

  const int index = GetSegmentIndexAtBeat(SEGMENT_SPEED, song_beat);

  const SpeedSegment* seg = ToSpeed(speeds[index]);
  float start_beat = seg->GetBeat();
  float start_time =
      GetElapsedTimeFromBeat(start_beat) - GetDelayAtBeat(start_beat);
  float end_time;
  float cur_time = music_seconds;

  if (seg->GetUnit() == SpeedSegment::UNIT_SECONDS) {
    end_time = start_time + seg->GetDelay();
  } else {
    end_time = GetElapsedTimeFromBeat(start_beat + seg->GetDelay()) -
               GetDelayAtBeat(start_beat + seg->GetDelay());
  }

  SpeedSegment* first = ToSpeed(speeds[0]);

  if ((index == 0 && first->GetDelay() > 0.0) && cur_time < start_time) {
    return 1.0f;
  } else if (end_time >= cur_time && (index > 0 || first->GetDelay() > 0.0)) {
    const float prior_speed =
        (index == 0) ? 1 : ToSpeed(speeds[index - 1])->GetRatio();

    float time_used = cur_time - start_time;
    float duration = end_time - start_time;
    float ratio_used = duration == 0.0 ? 1 : time_used / duration;

    float distance = prior_speed - seg->GetRatio();
    float ratio_need = ratio_used * -distance;
    return (prior_speed + ratio_need);
  } else {
    return seg->GetRatio();
  }
}

void TimingData::TidyUpData(bool allow_empty) {
  // Empty TimingData is used to implement steps with no timing of their
  // own.  Don't override this.
  if (allow_empty && empty()) {
    return;
  }
  InvalidateLookups();

  // If there are no BPM segments, provide a default.
  auto& segs = timing_segments_;
  if (segs[SEGMENT_BPM].empty()) {
    LOG->UserLog("Song file", file_, "has no BPM segments, default provided.");
    AddSegment(BPMSegment(0, 60));
  }

  // Make sure the first BPM segment starts at beat 0.
  if (segs[SEGMENT_BPM][0]->GetRow() != 0) {
    segs[SEGMENT_BPM][0]->SetRow(0);
  }

  // If no time signature specified, assume default time for the whole song.
  if (segs[SEGMENT_TIME_SIG].empty()) {
    AddSegment(TimeSignatureSegment(0));
  }

  // Likewise, if no tickcount signature is specified, assume 4 ticks
  // per beat for the entire song. The default of 4 is chosen more
  // for compatibility with the main Pump series than anything else.
  // (TickcountSegment's constructor handles that now. -- vyhd)
  if (segs[SEGMENT_TICKCOUNT].empty()) {
    AddSegment(TickcountSegment(0));
  }

  // Have a default combo segment of one just in case.
  if (segs[SEGMENT_COMBO].empty()) {
    AddSegment(ComboSegment(0));
  }

  // Have a default label segment just in case.
  if (segs[SEGMENT_LABEL].empty()) {
    AddSegment(LabelSegment(0, "Song Start"));
  }

  // Always be sure there is a starting speed.
  if (segs[SEGMENT_SPEED].empty()) {
    AddSegment(SpeedSegment(0));
  }

  // Always be sure there is a starting scrolling factor.
  if (segs[SEGMENT_SCROLL].empty()) {
    AddSegment(ScrollSegment(0));
  }
}

bool TimingData::HasSpeedChanges() const {
  const std::vector<TimingSegment*>& speeds = GetTimingSegments(SEGMENT_SPEED);
  return (speeds.size() > 1 || ToSpeed(speeds[0])->GetRatio() != 1);
}

bool TimingData::HasScrollChanges() const {
  const std::vector<TimingSegment*>& scrolls =
      GetTimingSegments(SEGMENT_SCROLL);
  return (scrolls.size() > 1 || ToScroll(scrolls[0])->GetRatio() != 1);
}

void TimingData::NoteRowToMeasureAndBeat(
    int note_row, int& measure_index_out, int& beat_index_out,
    int& rows_remainder) const {
  measure_index_out = 0;
  const std::vector<TimingSegment*>& t_sigs =
      GetTimingSegments(SEGMENT_TIME_SIG);
  for (unsigned i = 0; i < t_sigs.size(); i++) {
    TimeSignatureSegment* cur_sig = ToTimeSignature(t_sigs[i]);
    int segment_end_row =
        (i + 1 == t_sigs.size()) ? INT_MAX : t_sigs[i + 1]->GetRow();

    int rows_per_measure_this_segment = cur_sig->GetNoteRowsPerMeasure();

    if (note_row < segment_end_row) {
      // iNoteRow lands in this segment
      int num_rows_this_segment = note_row - cur_sig->GetRow();
      int num_measures_this_segment =
          (num_rows_this_segment) /
          rows_per_measure_this_segment;  // don't round up
      measure_index_out += num_measures_this_segment;
      int num_rows_this_measure =
          num_rows_this_segment % rows_per_measure_this_segment;
      beat_index_out = num_rows_this_measure / kRowsPerBeat;
      rows_remainder = num_rows_this_measure % kRowsPerBeat;
      return;
    } else {
      // iNoteRow lands after this segment
      int num_rows_this_segment = segment_end_row - cur_sig->GetRow();
      int num_measures_this_segment =
          (num_rows_this_segment + rows_per_measure_this_segment - 1) /
          rows_per_measure_this_segment;  // round up
      measure_index_out += num_measures_this_segment;
    }
  }

  FAIL_M("Failed to get measure and beat for note row");
}

std::vector<std::string> TimingData::ToVectorString(
    TimingSegmentType tst, int dec) const {
  const std::vector<TimingSegment*> segs = GetTimingSegments(tst);
  std::vector<std::string> ret;

  for (unsigned i = 0; i < segs.size(); i++) {
    ret.push_back(segs[i]->ToString(dec));
  }
  return ret;
}

// lua start
#include "LuaBinding.h"

#define TIMING_DATA_RETURNS_NUMBERS \
  THEME->GetMetricB("TimingData", "GetReturnsNumbers")

// This breaks encapsulation just as much as TimingData::ToVectorString does.
// But, it exists solely for the purpose of providing lua access, so it's as
// okay as all the other lua stuff that reaches past the encapsulation.
void TimingSegmentSetToLuaTable(
    TimingData* td, TimingSegmentType tst, lua_State* L);
void TimingSegmentSetToLuaTable(
    TimingData* td, TimingSegmentType tst, lua_State* L) {
  const std::vector<TimingSegment*> segs = td->GetTimingSegments(tst);
  lua_createtable(L, segs.size(), 0);
  if (tst == SEGMENT_LABEL) {
    for (size_t i = 0; i < segs.size(); ++i) {
      lua_createtable(L, 2, 0);
      lua_pushnumber(L, segs[i]->GetBeat());
      lua_rawseti(L, -2, 1);
      lua_pushstring(L, (ToLabel(segs[i]))->GetLabel().c_str());
      lua_rawseti(L, -2, 2);
      lua_rawseti(L, -2, i + 1);
    }
  } else {
    for (size_t i = 0; i < segs.size(); ++i) {
      std::vector<float> values = segs[i]->GetValues();
      lua_createtable(L, values.size() + 1, 0);
      lua_pushnumber(L, segs[i]->GetBeat());
      lua_rawseti(L, -2, 1);
      for (size_t v = 0; v < values.size(); ++v) {
        lua_pushnumber(L, values[v]);
        lua_rawseti(L, -2, v + 2);
      }
      lua_rawseti(L, -2, i + 1);
    }
  }
}

/** @brief Allow Lua to have access to the TimingData. */
class LunaTimingData : public Luna<TimingData> {
 public:
  static int HasStops(T* p, lua_State* L) {
    lua_pushboolean(L, p->HasStops());
    return 1;
  }

  static int HasDelays(T* p, lua_State* L) {
    lua_pushboolean(L, p->HasDelays());
    return 1;
  }

  static int HasBPMChanges(T* p, lua_State* L) {
    lua_pushboolean(L, p->HasBpmChanges());
    return 1;
  }

  static int HasWarps(T* p, lua_State* L) {
    lua_pushboolean(L, p->HasWarps());
    return 1;
  }

  static int HasFakes(T* p, lua_State* L) {
    lua_pushboolean(L, p->HasFakes());
    return 1;
  }

  static int HasSpeedChanges(T* p, lua_State* L) {
    lua_pushboolean(L, p->HasSpeedChanges());
    return 1;
  }

  static int HasScrollChanges(T* p, lua_State* L) {
    lua_pushboolean(L, p->HasScrollChanges());
    return 1;
  }

  static int GetWarps(T* p, lua_State* L) {
    if (lua_toboolean(L, 1)) {
      TimingSegmentSetToLuaTable(p, SEGMENT_WARP, L);
    } else {
      LuaHelpers::CreateTableFromArray(p->ToVectorString(SEGMENT_WARP), L);
    }
    return 1;
  }

  static int GetFakes(T* p, lua_State* L) {
    if (lua_toboolean(L, 1)) {
      TimingSegmentSetToLuaTable(p, SEGMENT_FAKE, L);
    } else {
      LuaHelpers::CreateTableFromArray(p->ToVectorString(SEGMENT_FAKE), L);
    }
    return 1;
  }

  static int GetScrolls(T* p, lua_State* L) {
    if (lua_toboolean(L, 1)) {
      TimingSegmentSetToLuaTable(p, SEGMENT_SCROLL, L);
    } else {
      LuaHelpers::CreateTableFromArray(p->ToVectorString(SEGMENT_SCROLL), L);
    }
    return 1;
  }

  static int GetSpeeds(T* p, lua_State* L) {
    if (lua_toboolean(L, 1)) {
      TimingSegmentSetToLuaTable(p, SEGMENT_SPEED, L);
    } else {
      LuaHelpers::CreateTableFromArray(p->ToVectorString(SEGMENT_SPEED), L);
    }
    return 1;
  }

  static int GetTimeSignatures(T* p, lua_State* L) {
    if (lua_toboolean(L, 1)) {
      TimingSegmentSetToLuaTable(p, SEGMENT_TIME_SIG, L);
    } else {
      LuaHelpers::CreateTableFromArray(p->ToVectorString(SEGMENT_TIME_SIG), L);
    }
    return 1;
  }

  static int GetCombos(T* p, lua_State* L) {
    if (lua_toboolean(L, 1)) {
      TimingSegmentSetToLuaTable(p, SEGMENT_COMBO, L);
    } else {
      LuaHelpers::CreateTableFromArray(p->ToVectorString(SEGMENT_COMBO), L);
    }
    return 1;
  }

  static int GetTickcounts(T* p, lua_State* L) {
    if (lua_toboolean(L, 1)) {
      TimingSegmentSetToLuaTable(p, SEGMENT_TICKCOUNT, L);
    } else {
      LuaHelpers::CreateTableFromArray(p->ToVectorString(SEGMENT_TICKCOUNT), L);
    }
    return 1;
  }

  static int GetStops(T* p, lua_State* L) {
    if (lua_toboolean(L, 1)) {
      TimingSegmentSetToLuaTable(p, SEGMENT_STOP, L);
    } else {
      LuaHelpers::CreateTableFromArray(p->ToVectorString(SEGMENT_STOP), L);
    }
    return 1;
  }

  static int GetDelays(T* p, lua_State* L) {
    if (lua_toboolean(L, 1)) {
      TimingSegmentSetToLuaTable(p, SEGMENT_DELAY, L);
    } else {
      LuaHelpers::CreateTableFromArray(p->ToVectorString(SEGMENT_DELAY), L);
    }
    return 1;
  }

  static int GetLabels(T* p, lua_State* L) {
    if (lua_toboolean(L, 1)) {
      TimingSegmentSetToLuaTable(p, SEGMENT_LABEL, L);
    } else {
      LuaHelpers::CreateTableFromArray(p->ToVectorString(SEGMENT_LABEL), L);
    }
    return 1;
  }

  static int GetBPMsAndTimes(T* p, lua_State* L) {
    if (lua_toboolean(L, 1)) {
      TimingSegmentSetToLuaTable(p, SEGMENT_BPM, L);
    } else {
      LuaHelpers::CreateTableFromArray(p->ToVectorString(SEGMENT_BPM), L);
    }
    return 1;
  }

  static int GetBPMs(T* p, lua_State* L) {
    std::vector<float> bp_ms;
    const std::vector<TimingSegment*>& bpms = p->GetTimingSegments(SEGMENT_BPM);

    for (TimingSegment* bpm : bpms) {
      bp_ms.push_back(ToBPM(bpm)->GetBPM());
    }

    LuaHelpers::CreateTableFromArray(bp_ms, L);
    return 1;
  }
  static int GetActualBPM(T* p, lua_State* L) {
    // certainly there's a better way to do it than this? -aj
    float min_bpm, max_bpm;
    p->GetActualBPM(min_bpm, max_bpm);
    std::vector<float> bp_ms;
    bp_ms.push_back(min_bpm);
    bp_ms.push_back(max_bpm);
    LuaHelpers::CreateTableFromArray(bp_ms, L);
    return 1;
  }
  static int HasNegativeBPMs(T* p, lua_State* L) {
    lua_pushboolean(L, p->HasWarps());
    return 1;
  }
  // formerly in Song.cpp in sm-ssc private beta 1.x:
  static int GetBPMAtBeat(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetBPMAtBeat(FArg(1)));
    return 1;
  }
  static int GetBeatFromElapsedTime(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetBeatFromElapsedTime(FArg(1)));
    return 1;
  }
  static int GetElapsedTimeFromBeat(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetElapsedTimeFromBeat(FArg(1)));
    return 1;
  }

  LunaTimingData() {
    ADD_METHOD(HasStops);
    ADD_METHOD(HasDelays);
    ADD_METHOD(HasBPMChanges);
    ADD_METHOD(HasWarps);
    ADD_METHOD(HasFakes);
    ADD_METHOD(HasSpeedChanges);
    ADD_METHOD(HasScrollChanges);
    ADD_METHOD(GetStops);
    ADD_METHOD(GetDelays);
    ADD_METHOD(GetBPMs);
    ADD_METHOD(GetWarps);
    ADD_METHOD(GetFakes);
    ADD_METHOD(GetTimeSignatures);
    ADD_METHOD(GetTickcounts);
    ADD_METHOD(GetSpeeds);
    ADD_METHOD(GetScrolls);
    ADD_METHOD(GetCombos);
    ADD_METHOD(GetLabels);
    ADD_METHOD(GetBPMsAndTimes);
    ADD_METHOD(GetActualBPM);
    ADD_METHOD(HasNegativeBPMs);
    // formerly in Song.cpp in sm-ssc private beta 1.x:
    ADD_METHOD(GetBPMAtBeat);
    ADD_METHOD(GetBeatFromElapsedTime);
    ADD_METHOD(GetElapsedTimeFromBeat);
  }
};

LUA_REGISTER_CLASS(TimingData)
// lua end

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
