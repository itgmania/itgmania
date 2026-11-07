#include "NotesLoaderSMA.h"

#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "Attack.h"
#include "BackgroundUtil.h"
#include "MsdFile.h"
#include "NoteTypes.h"
#include "NotesLoaderSM.h"  // may need this.
#include "RageLog.h"
#include "RageUtil.h"
#include "Song.h"
#include "SongManager.h"
#include "StdString.h"
#include "Steps.h"
#include "TimingSegments.h"

void SMALoader::ProcessMultipliers(
    TimingData& out, const int rows_per_beat, const std::string param) {
  std::vector<std::string> array_multiplier_expressions;
  split(param, ",", array_multiplier_expressions);

  for (unsigned f = 0; f < array_multiplier_expressions.size(); f++) {
    std::vector<std::string> array_multiplier_values;
    split(array_multiplier_expressions[f], "=", array_multiplier_values);
    unsigned size = array_multiplier_values.size();
    if (size < 2) {
      LOG->UserLog(
          "Song file", this->GetSongTitle(),
          "has an invalid #MULTIPLIER value \"%s\" (must have at least one "
          "'='), ignored.",
          array_multiplier_expressions[f].c_str());
      continue;
    }
    const float combo_beat =
        RowToBeat(array_multiplier_values[0], rows_per_beat);
    const int combos = StringToInt(array_multiplier_values[1]);  // always true.
    // hoping I'm right here: SMA files can use 6 values after the row/beat.
    const int misses =
        (size == 2 || size == 4 ? combos
                                : StringToInt(array_multiplier_values[2]));
    out.AddSegment(ComboSegment(BeatToNoteRow(combo_beat), combos, misses));
  }
}

void SMALoader::ProcessBeatsPerMeasure(
    TimingData& out, const std::string param) {
  std::vector<std::string> vs1;
  std::vector<TimeSignatureSegment> segments;
  split(param, ",", vs1);

  for (const std::string& s1 : vs1) {
    std::vector<std::string> vs2;
    split(s1, "=", vs2);

    if (vs2.size() < 2) {
      LOG->UserLog(
          "Song file", this->GetSongTitle(),
          "has an invalid beats per measure change with %i values.",
          static_cast<int>(vs2.size()));
      continue;
    }
    const float beat = StringToFloat(vs2[0]);
    const int numerator = StringToInt(vs2[1]);

    if (beat < 0) {
      LOG->UserLog(
          "Song file", this->GetSongTitle(),
          "has an invalid time signature change with beat %f.", beat);
      continue;
    }
    if (numerator < 1) {
      LOG->UserLog(
          "Song file", this->GetSongTitle(),
          "has an invalid time signature change with beat %f, iNumerator %i.",
          beat, numerator);
      continue;
    }
    segments.push_back(TimeSignatureSegment(BeatToNoteRow(beat), numerator));
  }

  // If there are any time signatures defined, but there isn't one
  // for the very first beat of the song, then add one.
  // Without it, calls to functions like TimingData::NoteRowToMeasureAndBeat
  // can fail for charts that are otherwise valid.
  if (segments.size() > 0 && segments[0].GetRow() > 0) {
    out.AddSegment(TimeSignatureSegment(0, 4, 4));
  }

  for (TimeSignatureSegment segment : segments) {
    out.AddSegment(segment);
  }
}

void SMALoader::ProcessSpeeds(
    TimingData& out, const std::string line, const int rows_per_beat) {
  std::vector<std::string> vs1;
  split(line, ",", vs1);

  for (std::vector<std::string>::const_iterator s1 = vs1.begin();
       s1 != vs1.end(); ++s1) {
    std::vector<std::string> vs2;
    vs2.clear();  // trying something.
    std::string loop_tmp = *s1;
    Trim(loop_tmp);
    split(loop_tmp, "=", vs2);

    if (vs2.size() == 2)  // First one always seems to have 2.
    {
      // Aldo_MX: 4 is the default value in SMA, although SM5 requires 0 for the
      // first segment :/
      vs2.push_back(s1 == vs1.begin() ? "0" : "4");
    }

    if (vs2.size() < 3) {
      LOG->UserLog(
          "Song file", this->GetSongTitle(),
          "has an speed change with %i values.", static_cast<int>(vs2.size()));
      continue;
    }

    const float beat = RowToBeat(vs2[0], rows_per_beat);

    std::string backup = vs2[2];
    Trim(vs2[2], "s");
    Trim(vs2[2], "S");

    const float ratio = StringToFloat(vs2[1]);
    const float delay = StringToFloat(vs2[2]);

    SpeedSegment::BaseUnit unit =
        ((backup != vs2[2]) ? SpeedSegment::UNIT_SECONDS
                            : SpeedSegment::UNIT_BEATS);

    if (beat < 0) {
      LOG->UserLog(
          "Song file", this->GetSongTitle(),
          "has an speed change with beat %f.", beat);
      continue;
    }

    if (delay < 0) {
      LOG->UserLog(
          "Song file", this->GetSongTitle(),
          "has an speed change with beat %f, length %f.", beat, delay);
      continue;
    }

    out.AddSegment(SpeedSegment(BeatToNoteRow(beat), ratio, delay, unit));
  }
}

bool SMALoader::LoadFromSimfile(const std::string& path, Song& out) {
  LOG->Trace("Song::LoadFromSMAFile(%s)", path.c_str());

  MsdFile msd;
  if (!msd.ReadFile(path, true))  // unescape
  {
    LOG->UserLog(
        "Song file", path, "couldn't be opened: %s", msd.GetError().c_str());
    return false;
  }

  out.song_timing_.file_ = path;  // songs still have their fallback timing.
  out.song_file_name_ = path;

  TimingData* timing = &out.song_timing_;
  int rows_per_beat = -1;  // Start with an invalid value: needed for checking.
  std::vector<std::pair<float, float>> bpm_changes, stops;

  for (unsigned i = 0; i < msd.GetNumValues(); i++) {
    int num_params = msd.GetNumParams(i);
    const MsdFile::value_t& params = msd.GetValue(i);
    std::string value_name = params[0];
    MakeUpper(value_name);

    // handle the data
    /* Don't use GetMainAndSubTitlesFromFullTitle; that's only for heuristically
     * splitting other formats that *don't* natively support #SUBTITLE. */
    if (value_name == "TITLE") {
      out.main_title_ = params[1];
      this->SetSongTitle(params[1]);
    }

    else if (value_name == "SUBTITLE") {
      out.sub_title_ = params[1];
    }

    else if (value_name == "ARTIST") {
      out.artist_ = params[1];
    }

    else if (value_name == "TITLETRANSLIT") {
      out.main_title_translit_ = params[1];
    }

    else if (value_name == "SUBTITLETRANSLIT") {
      out.sub_title_translit_ = params[1];
    }

    else if (value_name == "ARTISTTRANSLIT") {
      out.artist_translit_ = params[1];
    }

    else if (value_name == "GENRE") {
      out.genre_ = params[1];
    }

    else if (value_name == "CREDIT") {
      out.credit_ = params[1];
    }

    else if (value_name == "BANNER") {
      out.banner_file_ = params[1];
    }

    else if (value_name == "BACKGROUND") {
      out.background_file_ = params[1];
    }

    else if (value_name == "PREVIEW") {
      out.preview_vid_file_ = params[1];
    }

    // Save "#LYRICS" for later, so we can add an internal lyrics tag.
    else if (value_name == "LYRICSPATH") {
      out.lyrics_file_ = params[1];
    }

    else if (value_name == "CDTITLE") {
      out.cd_title_file_ = params[1];
    }

    else if (value_name == "MUSIC") {
      out.music_file_ = params[1];
    }

    else if (value_name == "MUSICLENGTH") {
      continue;
    }

    else if (value_name == "LASTBEATHINT") {
      // can't identify at this position: ignore.
    } else if (value_name == "MUSICBYTES")
      ; /* ignore */

    // Cache tags: ignore.
    else if (
        value_name == "FIRSTBEAT" || value_name == "LASTBEAT" ||
        value_name == "SONGFILENAME" || value_name == "HASMUSIC" ||
        value_name == "HASBANNER") {
      ;
    }

    else if (value_name == "SAMPLESTART") {
      out.music_sample_start_seconds_ = HHMMSSToSeconds(params[1]);
    }

    else if (value_name == "SAMPLELENGTH") {
      out.music_sample_length_seconds_ = HHMMSSToSeconds(params[1]);
    }

    // SamplePath is used when the song has a separate preview clip. -aj
    // else if( sValueName=="SAMPLEPATH" )
    // out.m_sMusicSamplePath = sParams[1];

    else if (value_name == "LISTSORT") {
      ;
    }

    else if (value_name == "DISPLAYBPM") {
      // #DISPLAYBPM:[xxx][xxx:xxx]|[*];
      if (params[1] == "*") {
        out.display_bpm_type_ = DISPLAY_BPM_RANDOM;
      } else {
        out.display_bpm_type_ = DISPLAY_BPM_SPECIFIED;
        out.specified_bpm_min_ = StringToFloat(params[1]);
        if (params[2].empty()) {
          out.specified_bpm_max_ = out.specified_bpm_min_;
        } else {
          out.specified_bpm_max_ = StringToFloat(params[2]);
        }
      }
    }

    else if (value_name == "SMAVERSION") {
      ;  // ignore it.
    }

    else if (value_name == "ROWSPERBEAT") {
      /* This value is used to help translate the timings
       * the SMA format uses. Starting with the second
       * appearance, it delimits NoteData. Right now, this
       * value doesn't seem to be editable in SMA. When it
       * becomes so, make adjustments to this code. */
      if (rows_per_beat < 0) {
        std::vector<std::string> array_beat_change_expressions;
        split(params[1], ",", array_beat_change_expressions);

        std::vector<std::string> array_beat_change_values;
        split(array_beat_change_expressions[0], "=", array_beat_change_values);
        rows_per_beat = StringToInt(array_beat_change_values[1]);
      } else {
        // This should generally return song timing
        ProcessBPMsAndStops(*timing, bpm_changes, stops);
      }
    }

    else if (value_name == "BEATSPERMEASURE") {
      ProcessBeatsPerMeasure(*timing, params[1]);
    }

    else if (value_name == "SELECTABLE") {
      if (EqualsNoCase(params[1], "YES")) {
        out.selection_display_ = out.SHOW_ALWAYS;
      } else if (EqualsNoCase(params[1], "NO")) {
        out.selection_display_ = out.SHOW_NEVER;
      }
      // ROULETTE from 3.9. It was removed since UnlockManager can serve
      // the same purpose somehow. This, of course, assumes you're using
      // unlocks. -aj
      else if (EqualsNoCase(params[1], "ROULETTE")) {
        out.selection_display_ = out.SHOW_ALWAYS;
      }
      /* The following two cases are just fixes to make sure simfiles that
       * used 3.9+ features are not excluded here */
      else if (
          EqualsNoCase(params[1], "ES") || EqualsNoCase(params[1], "OMES")) {
        out.selection_display_ = out.SHOW_ALWAYS;
      } else if (StringToInt(params[1]) > 0) {
        out.selection_display_ = out.SHOW_ALWAYS;
      } else {
        LOG->UserLog(
            "Song file", path,
            "has an unknown #SELECTABLE value, \"%s\"; ignored.",
            params[1].c_str());
      }
    }

    else if (
        Left(value_name, strlen("BGCHANGES")) == "BGCHANGES" ||
        value_name == "ANIMATIONS") {
      SMLoader::ProcessBGChanges(out, value_name, path, params[1]);
    }

    else if (value_name == "FGCHANGES") {
      std::vector<std::vector<std::string>> fg_changes;
      ParseBGChangesString(params[1], fg_changes, out.GetSongDir());

      for (const auto& b : fg_changes) {
        BackgroundChange change;
        if (LoadFromBGChangesVector(change, b)) {
          out.AddForegroundChange(change);
        }
      }
    }

    else if (value_name == "OFFSET") {
      timing->beat0_offset_in_seconds_ = StringToFloat(params[1]);
    }

    else if (value_name == "BPMS") {
      bpm_changes.clear();
      ParseBPMs(bpm_changes, params[1], rows_per_beat);
    }

    else if (value_name == "STOPS" || value_name == "FREEZES") {
      stops.clear();
      ParseStops(stops, params[1], rows_per_beat);
    }

    else if (value_name == "DELAYS") {
      ProcessDelays(*timing, params[1], rows_per_beat);
    }

    else if (value_name == "TICKCOUNT") {
      ProcessTickcounts(*timing, params[1], rows_per_beat);
    }

    else if (value_name == "SPEED") {
      std::string tmp = params[1];
      Trim(tmp);
      ProcessSpeeds(*timing, tmp, rows_per_beat);
    }

    else if (value_name == "MULTIPLIER") {
      ProcessMultipliers(*timing, rows_per_beat, params[1]);
    }

    else if (value_name == "FAKES") {
      ProcessFakes(*timing, params[1], rows_per_beat);
    }

    else if (value_name == "METERTYPE") {
      ;  // We don't use this...yet.
    }

    else if (value_name == "KEYSOUNDS") {
      split(params[1], ",", out.keysound_file_);
    }

    // Attacks loaded from file
    else if (value_name == "ATTACKS") {
      ProcessAttackString(out.attack_string_, params);
      ProcessAttacks(out.attacks_, params);
    }

    else if (value_name == "NOTES" || value_name == "NOTES2") {
      if (num_params < 7) {
        LOG->UserLog(
            "Song file", path,
            "has %d fields in a #NOTES tag, but should have at least 7.",
            num_params);
        continue;
      }

      Steps* new_notes = new Steps(&out);

      LoadFromTokens(
          params[1], params[2], params[3], params[4], params[5], params[6],
          *new_notes);
      new_notes->SetFilename(path);
      out.AddSteps(new_notes);

      // Handle timing changes and convert negative bpms/stops
      timing = &new_notes->timing_;
      ProcessBPMsAndStops(*timing, bpm_changes, stops);
    } else if (value_name == "TIMESIGNATURES" || value_name == "LEADTRACK")
      ;
    else {
      LOG->UserLog(
          "Song file", path, "has an unexpected value named \"%s\".",
          value_name.c_str());
    }
  }
  out.FixupBackgroundChanges();
  return true;
}

/**
 * @file
 * @author Aldo Fregoso, Jason Felds (c) 2009-2011
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
