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
    const std::vector<Steps*>& vpSteps = GAMESTATE->cur_song_->GetAllSteps();
    for (const Steps* s : vpSteps) {
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
  const std::vector<Steps*>& vpSteps = GAMESTATE->cur_song_->GetAllSteps();
  for (Steps* s : vpSteps) {
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
void AdjustSync::HandleAutosync(float fNoteOffBySeconds, float fStepTime) {
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
          std::make_pair(fStepTime, fNoteOffBySeconds));
      break;
    }
    case AutosyncType_Machine:
    case AutosyncType_Song: {
      autosync_offset_[autosync_offset_sample_] = fNoteOffBySeconds;
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
        const std::vector<Steps*>& vpSteps =
            GAMESTATE->cur_song_->GetAllSteps();
        for (Steps* s : vpSteps) {
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
  float fSlope = 0.0f;
  float fIntercept = 0.0f;
  if (!CalcLeastSquares(
          autosync_tempo_data_, fSlope, fIntercept, average_error_)) {
    autosync_tempo_data_.clear();
    return;
  }

  if (average_error_ < kErrorTooHigh) {
    /* Here we filter out any steps that are too far off.
     * If it turns out that we want to be even more selective, we can keep
     * only a fraction of the data, such as the 80% with the lowest error.
     * However, throwing away the ones with high error should be enough
     * in most cases. */
    float fFilteredError = 0;
    steps_filtered_ = autosync_tempo_data_.size();
    FilterHighErrorPoints(
        autosync_tempo_data_, fSlope, fIntercept, kErrorTooHigh);
    steps_filtered_ -= autosync_tempo_data_.size();

    if (!CalcLeastSquares(
            autosync_tempo_data_, fSlope, fIntercept, fFilteredError)) {
      return;
    }

    GAMESTATE->cur_song_->song_timing_.beat0_offset_in_seconds_ += fIntercept;
    const float fScaleBPM = 1.0f / (1.0f - fSlope);
    TimingData& timing = GAMESTATE->cur_song_->song_timing_;

    const std::vector<TimingSegment*>& bpms =
        timing.GetTimingSegments(SEGMENT_BPM);
    for (unsigned i = 0; i < bpms.size(); i++) {
      const BPMSegment* b = ToBPM(bpms[i]);
      timing.AddSegment(BPMSegment(b->GetRow(), b->GetBPM() * fScaleBPM));
    }

    /* We assume that the stops were measured as a number of beats.
     * Therefore, if we change the bpms, we need to make a similar
     * change to the stops. */
    const std::vector<TimingSegment*>& stops =
        timing.GetTimingSegments(SEGMENT_STOP);
    for (unsigned i = 0; i < stops.size(); i++) {
      const StopSegment* s = ToStop(stops[i]);
      timing.AddSegment(
          StopSegment(s->GetRow(), s->GetPause() * (1.0f - fSlope)));
    }
    // Do the same for delays.
    const std::vector<TimingSegment*>& delays =
        timing.GetTimingSegments(SEGMENT_DELAY);
    for (unsigned i = 0; i < delays.size(); i++) {
      const DelaySegment* s = ToDelay(delays[i]);
      timing.AddSegment(
          DelaySegment(s->GetRow(), s->GetPause() * (1.0f - fSlope)));
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

void AdjustSync::GetSyncChangeTextGlobal(std::vector<std::string>& vsAddTo) {
  {
    float fOld = Quantize(AdjustSync::global_offset_seconds_original_, 0.001f);
    float fNew = Quantize(PREFSMAN->global_offset_seconds_, 0.001f);
    float fDelta = fNew - fOld;

    if (std::abs(fDelta) > 0.0001f) {
      vsAddTo.push_back(ssprintf(
          GLOBAL_OFFSET_FROM.GetValue().c_str(), fOld, fNew,
          (fDelta > 0 ? EARLIER : LATER).GetValue().c_str()));
    }
  }
}

// XXX: needs cleanup still -- vyhd
void AdjustSync::GetSyncChangeTextSong(std::vector<std::string>& vsAddTo) {
  if (GAMESTATE->cur_song_.Get()) {
#define SEGMENTS_MISMATCH_MESSAGE(orig, test, segments_name)                 \
  if (orig.size() != test.size()) {                                          \
    LuaHelpers::ReportScriptError(                                           \
        "The sync overlay's " #segments_name                                 \
        " segment list is a different size from the song's.  Please report " \
        "this bug with steps to reproduce it.");                             \
  }

    unsigned int iOriginalSize = vsAddTo.size();
    TimingData& original = timing_data_original_[0];
    TimingData& testing = GAMESTATE->cur_song_->song_timing_;

    {
      float fOld = Quantize(original.beat0_offset_in_seconds_, 0.001f);
      float fNew = Quantize(testing.beat0_offset_in_seconds_, 0.001f);
      float fDelta = fNew - fOld;

      if (std::abs(fDelta) > 0.0001f) {
        vsAddTo.push_back(ssprintf(
            SONG_OFFSET_FROM.GetValue().c_str(), fOld, fNew,
            (fDelta > 0 ? EARLIER : LATER).GetValue().c_str()));
      }
    }

    const std::vector<TimingSegment*>& bpmTest =
        testing.GetTimingSegments(SEGMENT_BPM);
    const std::vector<TimingSegment*>& bpmOrig =
        original.GetTimingSegments(SEGMENT_BPM);
    SEGMENTS_MISMATCH_MESSAGE(bpmOrig, bpmTest, bpm);
    for (size_t i = 0; i < bpmTest.size() && i < bpmOrig.size(); i++) {
      float fNew = Quantize(ToBPM(bpmTest[i])->GetBPM(), 0.001f);
      float fOld = Quantize(ToBPM(bpmOrig[i])->GetBPM(), 0.001f);

      if (std::abs(fNew - fOld) < 1e-4) {
        continue;
      }

      if (i >= 4) {
        vsAddTo.push_back(ETC.GetValue());
        break;
      }

      std::string s = ssprintf(
          TEMPO_SEGMENT_FROM.GetValue().c_str(),
          FormatNumberAndSuffix(i + 1).c_str(), fOld, fNew);

      vsAddTo.push_back(s);
    }

    const std::vector<TimingSegment*>& stopTest =
        testing.GetTimingSegments(SEGMENT_STOP);
    const std::vector<TimingSegment*>& stopOrig =
        original.GetTimingSegments(SEGMENT_STOP);

    SEGMENTS_MISMATCH_MESSAGE(stopOrig, stopTest, stop);
    for (size_t i = 0; i < stopTest.size() && i < stopOrig.size(); i++) {
      float fOld = Quantize(ToStop(stopOrig[i])->GetPause(), 0.001f);
      float fNew = Quantize(ToStop(stopTest[i])->GetPause(), 0.001f);
      float fDelta = fNew - fOld;

      if (std::abs(fDelta) < 1e-4) {
        continue;
      }

      if (i >= 4) {
        vsAddTo.push_back(ETC.GetValue());
        break;
      }

      std::string s =
          ssprintf(CHANGED_STOP.GetValue().c_str(), i + 1, fOld, fNew, fDelta);
      vsAddTo.push_back(s);
    }

    const std::vector<TimingSegment*>& delyTest =
        testing.GetTimingSegments(SEGMENT_DELAY);
    const std::vector<TimingSegment*>& delyOrig =
        original.GetTimingSegments(SEGMENT_DELAY);

    SEGMENTS_MISMATCH_MESSAGE(delyOrig, delyTest, delay);
    for (size_t i = 0; i < delyTest.size() && i < delyOrig.size(); i++) {
      if (delyTest[i] == delyOrig[i]) {
        continue;
      }

      float fOld = Quantize(ToDelay(delyOrig[i])->GetPause(), 0.001f);
      float fNew = Quantize(ToDelay(delyTest[i])->GetPause(), 0.001f);
      float fDelta = fNew - fOld;

      if (std::abs(fDelta) < 1e-4) {
        continue;
      }

      if (i >= 4) {
        vsAddTo.push_back(ETC.GetValue());
        break;
      }

      std::string s =
          ssprintf(CHANGED_STOP.GetValue().c_str(), i + 1, fOld, fNew, fDelta);
      vsAddTo.push_back(s);
    }

    if (vsAddTo.size() > iOriginalSize && average_error_ > 0.0f) {
      vsAddTo.push_back(ssprintf(ERROR.GetValue().c_str(), average_error_));
    }
    if (vsAddTo.size() > iOriginalSize && steps_filtered_ > 0) {
      vsAddTo.push_back(
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
