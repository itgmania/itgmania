/*
 * AdjustSync defines two methods for fixing the sync.
 *
 * The first method adjusts either the song or the machine by the
 * average offset of the user's steps.  In other words, if the user
 * averages to step early by 10 ms, either the song or the global
 * offset is adjusted by 10 ms to compensate for that.  These
 * adjustments only require a small set of data, so this method
 * updates the offset while the song is playing.
 *
 * The second method adjusts both the offset and the tempo of an
 * individual song.  It records all of the steps during a play of
 * the song and uses linear least squares regression to minimize the
 * error of those steps.  It makes one adjustment for the tempo of
 * the entire song, rather than adding many different tempo segments
 * to match the steps.  If there are already several tempo segments
 * in the stepfile, this method makes a proportional change to each
 * of them.  For example, if it changes 100 bpm to 101 bpm, it will
 * also change 200 bpm to 202 bpm.  This method also adjusts the stops.
 * It assumes that a given stop is measured in terms of beats and makes
 * the appropriate change.
 *
 * If we use this method on a small set of data late in the song, it
 * can have very chaotic effects on the early settings.  For example,
 * it may change the offset by several hundred milliseconds and make a
 * large change to the BPM to compensate if that would minimize the
 * error.  This problem occurs especially when the user makes a couple
 * steps that are significantly off beat.  The way to avoid this is to
 * perform the least squares regression once on all of the data
 * collected, rather than adjusting the sync every time we get another
 * 50 or so data points.  In fact, if we are playing in edit mode and
 * the user loops through the song more than once, we use all of the
 * steps made.
 */

#include "AdjustSync.h"

#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "GameState.h"
#include "LocalizedString.h"
#include "LuaManager.h"
#include "MessageManager.h"
#include "PrefsManager.h"
#include "RageUtil.h"
#include "ScreenManager.h"
#include "Song.h"
#include "SongOptions.h"
#include "Steps.h"
#include "TimingData.h"
#include "TimingSegments.h"
#include "global.h"

std::vector<TimingData> AdjustSync::timing_data_original_;
float AdjustSync::global_offset_seconds_original_ = 0.0f;
int AdjustSync::autosync_offset_sample_ = 0;
float AdjustSync::autosync_offset_[AdjustSync::kOffsetSampleCount];
float AdjustSync::standard_deviation_ = 0.0f;
std::vector<std::pair<float, float>> AdjustSync::autosync_tempo_data_;
float AdjustSync::average_error_ = 0.0f;
const float AdjustSync::kErrorTooHigh = 0.025f;
int AdjustSync::steps_filtered_ = 0;

void AdjustSync::ResetOriginalSyncData() {
  timing_data_original_.clear();

  if (GAMESTATE->cur_song_) {
    timing_data_original_.push_back(GAMESTATE->cur_song_->song_timing_);
    const std::vector<Steps*>& steps = GAMESTATE->cur_song_->GetAllSteps();
    for (const Steps* s : steps) {
      timing_data_original_.push_back(s->timing_);
    }
  } else {
    timing_data_original_.push_back(TimingData());
  }
  global_offset_seconds_original_ = PREFSMAN->global_offset_seconds_;

  ResetAutosync();
}

void AdjustSync::ResetAutosync() {
  autosync_offset_sample_ = 0;
  autosync_tempo_data_.clear();
}

bool AdjustSync::IsSyncDataChanged() {
  // Can't sync in course modes
  if (GAMESTATE->IsCourseMode()) {
    return false;
  }
  std::vector<std::string> vs;
  AdjustSync::GetSyncChangeTextGlobal(vs);
  AdjustSync::GetSyncChangeTextSong(vs);
  return !vs.empty();
}

void AdjustSync::SaveSyncChanges() {
  if (GAMESTATE->IsCourseMode()) {
    return;
  }

  /* TODO: Save all of the timing data changes.
   * Luckily, only the song timing data needs comparing here. */
  if (GAMESTATE->cur_song_ &&
      timing_data_original_[0] != GAMESTATE->cur_song_->song_timing_) {
    if (GAMESTATE->IsEditing()) {
      MESSAGEMAN->Broadcast(Message_SongModified);
    } else {
      GAMESTATE->cur_song_->Save();
    }
  }
  if (global_offset_seconds_original_ != PREFSMAN->global_offset_seconds_) {
    PREFSMAN->SavePrefsToDisk();
  }
  ResetOriginalSyncData();
  standard_deviation_ = 0.0f;
  average_error_ = 0.0f;
}

void AdjustSync::RevertSyncChanges() {
  if (GAMESTATE->IsCourseMode()) {
    return;
  }
  PREFSMAN->global_offset_seconds_.Set(global_offset_seconds_original_);

  // The first one is ALWAYS the song timing.
  GAMESTATE->cur_song_->song_timing_ = timing_data_original_[0];

  unsigned location = 1;
  const std::vector<Steps*>& steps = GAMESTATE->cur_song_->GetAllSteps();
  for (Steps* s : steps) {
    s->timing_ = timing_data_original_[location];
    location++;
  }

  ResetOriginalSyncData();
  standard_deviation_ = 0.0f;
  average_error_ = 0.0f;
}

static LocalizedString AUTOSYNC_CORRECTION_APPLIED(
    "AdjustSync", "Autosync: Correction applied.");
static LocalizedString AUTOSYNC_CORRECTION_NOT_APPLIED(
    "AdjustSync", "Autosync: Correction NOT applied. Deviation too high.");
void AdjustSync::HandleAutosync(float note_off_by_seconds, float step_time) {
  if (GAMESTATE->IsCourseMode()) {
    return;
  }
  AutosyncType type = GAMESTATE->song_options_.GetCurrent().autosync_type_;
  switch (type) {
    case AutosyncType_Off:
      return;
    case AutosyncType_Tempo: {
      // We collect all of the data and process it at the end
      autosync_tempo_data_.push_back(
          std::make_pair(step_time, note_off_by_seconds));
      break;
    }
    case AutosyncType_Machine:
    case AutosyncType_Song: {
      autosync_offset_[autosync_offset_sample_] = note_off_by_seconds;
      ++autosync_offset_sample_;

      if (autosync_offset_sample_ < kOffsetSampleCount) {
        break;  // need more
      }

      AutosyncOffset();
      break;
    }
    default:
      FAIL_M(ssprintf("Invalid autosync type: %i", type));
  }
}

void AdjustSync::HandleSongEnd() {
  if (GAMESTATE->IsCourseMode()) {
    return;
  }
  if (GAMESTATE->song_options_.GetCurrent().autosync_type_ ==
      AutosyncType_Tempo) {
    AutosyncTempo();
  }

  // all other states don't care
}

void AdjustSync::AutosyncOffset() {
  const float mean =
      calc_mean(autosync_offset_, autosync_offset_ + kOffsetSampleCount);
  const float stddev =
      calc_stddev(autosync_offset_, autosync_offset_ + kOffsetSampleCount);

  AutosyncType type = GAMESTATE->song_options_.GetCurrent().autosync_type_;

  if (stddev < .03f)  // If they stepped with less than .03 error
  {
    switch (type) {
      case AutosyncType_Song: {
        GAMESTATE->cur_song_->song_timing_.beat0_offset_in_seconds_ += mean;
        const std::vector<Steps*>& steps = GAMESTATE->cur_song_->GetAllSteps();
        for (Steps* s : steps) {
          // Empty TimingData means it's inherited
          // from the song and is already changed.
          if (s->timing_.empty()) {
            continue;
          }
          s->timing_.beat0_offset_in_seconds_ += mean;
        }
        break;
      }
      case AutosyncType_Machine:
        // Step timing is not needed for this operation.
        PREFSMAN->global_offset_seconds_.Set(
            PREFSMAN->global_offset_seconds_ + mean);
        break;
      default:
        FAIL_M(ssprintf("Invalid autosync type: %i", type));
    }

    SCREENMAN->SystemMessage(AUTOSYNC_CORRECTION_APPLIED.GetValue());
  } else {
    SCREENMAN->SystemMessage(AUTOSYNC_CORRECTION_NOT_APPLIED.GetValue());
  }

  autosync_offset_sample_ = 0;
  standard_deviation_ = stddev;
}

void AdjustSync::AutosyncTempo() {
  float slope = 0.0f;
  float intercept = 0.0f;
  if (!CalcLeastSquares(
          autosync_tempo_data_, slope, intercept, average_error_)) {
    autosync_tempo_data_.clear();
    return;
  }

  if (average_error_ < kErrorTooHigh) {
    /* Here we filter out any steps that are too far off.
     * If it turns out that we want to be even more selective, we can keep
     * only a fraction of the data, such as the 80% with the lowest error.
     * However, throwing away the ones with high error should be enough
     * in most cases. */
    float filtered_error = 0;
    steps_filtered_ = autosync_tempo_data_.size();
    FilterHighErrorPoints(
        autosync_tempo_data_, slope, intercept, kErrorTooHigh);
    steps_filtered_ -= autosync_tempo_data_.size();

    if (!CalcLeastSquares(
            autosync_tempo_data_, slope, intercept, filtered_error)) {
      return;
    }

    GAMESTATE->cur_song_->song_timing_.beat0_offset_in_seconds_ += intercept;
    const float scale_bpm = 1.0f / (1.0f - slope);
    TimingData& timing = GAMESTATE->cur_song_->song_timing_;

    const std::vector<TimingSegment*>& bpms =
        timing.GetTimingSegments(SEGMENT_BPM);
    for (unsigned i = 0; i < bpms.size(); i++) {
      const BPMSegment* b = ToBPM(bpms[i]);
      timing.AddSegment(BPMSegment(b->GetRow(), b->GetBPM() * scale_bpm));
    }

    /* We assume that the stops were measured as a number of beats.
     * Therefore, if we change the bpms, we need to make a similar
     * change to the stops. */
    const std::vector<TimingSegment*>& stops =
        timing.GetTimingSegments(SEGMENT_STOP);
    for (unsigned i = 0; i < stops.size(); i++) {
      const StopSegment* s = ToStop(stops[i]);
      timing.AddSegment(
          StopSegment(s->GetRow(), s->GetPause() * (1.0f - slope)));
    }
    // Do the same for delays.
    const std::vector<TimingSegment*>& delays =
        timing.GetTimingSegments(SEGMENT_DELAY);
    for (unsigned i = 0; i < delays.size(); i++) {
      const DelaySegment* s = ToDelay(delays[i]);
      timing.AddSegment(
          DelaySegment(s->GetRow(), s->GetPause() * (1.0f - slope)));
    }

    SCREENMAN->SystemMessage(AUTOSYNC_CORRECTION_APPLIED.GetValue());
  } else {
    // deviation... error... close enough for an error message
    SCREENMAN->SystemMessage(AUTOSYNC_CORRECTION_NOT_APPLIED.GetValue());
  }

  autosync_tempo_data_.clear();
}

static LocalizedString EARLIER("AdjustSync", "earlier");
static LocalizedString LATER("AdjustSync", "later");
static LocalizedString GLOBAL_OFFSET_FROM(
    "AdjustSync", "Global Offset from %+.3f to %+.3f (notes %s)");
// We need to limit the length of lines so each one fits on one line of the SM
// console. The tempo and stop change message can get very long in a complicated
// song, and at a low resolution, the keep/revert menu would be pushed off the
// bottom of the screen if we didn't limit the length of the message.  Keeping
// the lines short lets us fit more information on the screen.
static LocalizedString SONG_OFFSET_FROM(
    "AdjustSync", "Song offset from %+.3f to %+.3f (notes %s)");
static LocalizedString TEMPO_SEGMENT_FROM(
    "AdjustSync", "%s BPM from %.3f BPM to %.3f BPM.");
static LocalizedString CHANGED_STOP(
    "AdjustSync",
    "The stop segment #%d changed from %+.3fs to %+.3fs (change of %+.3f).");
static LocalizedString ERROR("AdjustSync", "Average Error %.5fs");
static LocalizedString ETC("AdjustSync", "Etc.");
static LocalizedString TAPS_IGNORED("AdjustSync", "%d taps ignored.");

void AdjustSync::GetSyncChangeTextGlobal(std::vector<std::string>& add_to) {
  {
    float old = Quantize(AdjustSync::global_offset_seconds_original_, 0.001f);
    float f_new = Quantize(PREFSMAN->global_offset_seconds_, 0.001f);
    float delta = f_new - old;

    if (std::abs(delta) > 0.0001f) {
      add_to.push_back(ssprintf(
          GLOBAL_OFFSET_FROM.GetValue().c_str(), old, f_new,
          (delta > 0 ? EARLIER : LATER).GetValue().c_str()));
    }
  }
}

// XXX: needs cleanup still -- vyhd
void AdjustSync::GetSyncChangeTextSong(std::vector<std::string>& add_to) {
  if (GAMESTATE->cur_song_.Get()) {
#define SEGMENTS_MISMATCH_MESSAGE(orig, test, segments_name)                 \
  if (orig.size() != test.size()) {                                          \
    LuaHelpers::ReportScriptError(                                           \
        "The sync overlay's " #segments_name                                 \
        " segment list is a different size from the song's.  Please report " \
        "this bug with steps to reproduce it.");                             \
  }

    unsigned int original_size = add_to.size();
    TimingData& original = timing_data_original_[0];
    TimingData& testing = GAMESTATE->cur_song_->song_timing_;

    {
      float old = Quantize(original.beat0_offset_in_seconds_, 0.001f);
      float f_new = Quantize(testing.beat0_offset_in_seconds_, 0.001f);
      float delta = f_new - old;

      if (std::abs(delta) > 0.0001f) {
        add_to.push_back(ssprintf(
            SONG_OFFSET_FROM.GetValue().c_str(), old, f_new,
            (delta > 0 ? EARLIER : LATER).GetValue().c_str()));
      }
    }

    const std::vector<TimingSegment*>& bpm_test =
        testing.GetTimingSegments(SEGMENT_BPM);
    const std::vector<TimingSegment*>& bpm_orig =
        original.GetTimingSegments(SEGMENT_BPM);
    SEGMENTS_MISMATCH_MESSAGE(bpm_orig, bpm_test, bpm);
    for (size_t i = 0; i < bpm_test.size() && i < bpm_orig.size(); i++) {
      float f_new = Quantize(ToBPM(bpm_test[i])->GetBPM(), 0.001f);
      float old = Quantize(ToBPM(bpm_orig[i])->GetBPM(), 0.001f);

      if (std::abs(f_new - old) < 1e-4) {
        continue;
      }

      if (i >= 4) {
        add_to.push_back(ETC.GetValue());
        break;
      }

      std::string s = ssprintf(
          TEMPO_SEGMENT_FROM.GetValue().c_str(),
          FormatNumberAndSuffix(i + 1).c_str(), old, f_new);

      add_to.push_back(s);
    }

    const std::vector<TimingSegment*>& stop_test =
        testing.GetTimingSegments(SEGMENT_STOP);
    const std::vector<TimingSegment*>& stop_orig =
        original.GetTimingSegments(SEGMENT_STOP);

    SEGMENTS_MISMATCH_MESSAGE(stop_orig, stop_test, stop);
    for (size_t i = 0; i < stop_test.size() && i < stop_orig.size(); i++) {
      float old = Quantize(ToStop(stop_orig[i])->GetPause(), 0.001f);
      float f_new = Quantize(ToStop(stop_test[i])->GetPause(), 0.001f);
      float delta = f_new - old;

      if (std::abs(delta) < 1e-4) {
        continue;
      }

      if (i >= 4) {
        add_to.push_back(ETC.GetValue());
        break;
      }

      std::string s =
          ssprintf(CHANGED_STOP.GetValue().c_str(), i + 1, old, f_new, delta);
      add_to.push_back(s);
    }

    const std::vector<TimingSegment*>& dely_test =
        testing.GetTimingSegments(SEGMENT_DELAY);
    const std::vector<TimingSegment*>& dely_orig =
        original.GetTimingSegments(SEGMENT_DELAY);

    SEGMENTS_MISMATCH_MESSAGE(dely_orig, dely_test, delay);
    for (size_t i = 0; i < dely_test.size() && i < dely_orig.size(); i++) {
      if (dely_test[i] == dely_orig[i]) {
        continue;
      }

      float old = Quantize(ToDelay(dely_orig[i])->GetPause(), 0.001f);
      float f_new = Quantize(ToDelay(dely_test[i])->GetPause(), 0.001f);
      float delta = f_new - old;

      if (std::abs(delta) < 1e-4) {
        continue;
      }

      if (i >= 4) {
        add_to.push_back(ETC.GetValue());
        break;
      }

      std::string s =
          ssprintf(CHANGED_STOP.GetValue().c_str(), i + 1, old, f_new, delta);
      add_to.push_back(s);
    }

    if (add_to.size() > original_size && average_error_ > 0.0f) {
      add_to.push_back(ssprintf(ERROR.GetValue().c_str(), average_error_));
    }
    if (add_to.size() > original_size && steps_filtered_ > 0) {
      add_to.push_back(
          ssprintf(TAPS_IGNORED.GetValue().c_str(), steps_filtered_));
    }
#undef SEGMENTS_MISMATCH_MESSAGE
  }
}

/*
 * (c) 2003-2006 Chris Danford, John Bauer
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
