#include "NotesWriterSM.h"

#include <cerrno>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "BackgroundUtil.h"
#include "Difficulty.h"
#include "GameConstantsAndTypes.h"
#include "LocalizedString.h"
#include "NoteTypes.h"
#include "PlayerNumber.h"
#include "Profile.h"
#include "ProfileManager.h"
#include "RadarValues.h"
#include "RageFile.h"
#include "RageFileManager.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "Song.h"
#include "Steps.h"
#include "ThemeMetric.h"
#include "TimingData.h"
#include "TimingSegments.h"
#include "global.h"

ThemeMetric<bool> USE_CREDIT("NotesWriterSM", "DescriptionUsesCreditField");

/**
 * @brief Write out the common tags for .SM files.
 * @param f the file in question.
 * @param out the Song in question. */
static void WriteGlobalTags(RageFileBasic& f, Song& out) {
  TimingData& timing = out.song_timing_;
  f.PutLine(ssprintf("#TITLE:%s;", SmEscape(out.main_title_).c_str()));
  f.PutLine(ssprintf("#SUBTITLE:%s;", SmEscape(out.sub_title_).c_str()));
  f.PutLine(ssprintf("#ARTIST:%s;", SmEscape(out.artist_).c_str()));
  f.PutLine(ssprintf(
      "#TITLETRANSLIT:%s;", SmEscape(out.main_title_translit_).c_str()));
  f.PutLine(ssprintf(
      "#SUBTITLETRANSLIT:%s;", SmEscape(out.sub_title_translit_).c_str()));
  f.PutLine(
      ssprintf("#ARTISTTRANSLIT:%s;", SmEscape(out.artist_translit_).c_str()));
  f.PutLine(ssprintf("#GENRE:%s;", SmEscape(out.genre_).c_str()));
  f.PutLine(ssprintf("#CREDIT:%s;", SmEscape(out.credit_).c_str()));
  f.PutLine(ssprintf("#BANNER:%s;", SmEscape(out.banner_file_).c_str()));
  f.PutLine(
      ssprintf("#BACKGROUND:%s;", SmEscape(out.background_file_).c_str()));
  f.PutLine(ssprintf("#LYRICSPATH:%s;", SmEscape(out.lyrics_file_).c_str()));
  f.PutLine(ssprintf("#CDTITLE:%s;", SmEscape(out.cd_title_file_).c_str()));
  f.PutLine(ssprintf("#MUSIC:%s;", SmEscape(out.music_file_).c_str()));
  f.PutLine(
      ssprintf("#OFFSET:%.6f;", out.song_timing_.beat0_offset_in_seconds_));
  f.PutLine(ssprintf("#SAMPLESTART:%.6f;", out.music_sample_start_seconds_));
  f.PutLine(ssprintf("#SAMPLELENGTH:%.6f;", out.music_sample_length_seconds_));
  float spec_beat = out.GetSpecifiedLastBeat();
  if (spec_beat > 0) {
    f.PutLine(ssprintf("#LASTBEATHINT:%.6f;", spec_beat));
  }

  f.Write("#SELECTABLE:");
  switch (out.selection_display_) {
    default:
      FAIL_M(ssprintf("Invalid selection display: %i", out.selection_display_));
    case Song::SHOW_ALWAYS:
      f.Write("YES");
      break;
      // case Song::SHOW_NONSTOP:	f.Write( "NONSTOP" );	break;
    case Song::SHOW_NEVER:
      f.Write("NO");
      break;
  }
  f.PutLine(";");

  switch (out.display_bpm_type_) {
    case DISPLAY_BPM_ACTUAL:
      // write nothing
      break;
    case DISPLAY_BPM_SPECIFIED:
      if (out.specified_bpm_min_ == out.specified_bpm_max_) {
        f.PutLine(ssprintf("#DISPLAYBPM:%.6f;", out.specified_bpm_min_));
      } else {
        f.PutLine(ssprintf(
            "#DISPLAYBPM:%.6f:%.6f;", out.specified_bpm_min_,
            out.specified_bpm_max_));
      }
      break;
    case DISPLAY_BPM_RANDOM:
      f.PutLine(ssprintf("#DISPLAYBPM:*;"));
      break;
    default:
      break;
  }

  f.Write("#BPMS:");
  const std::vector<TimingSegment*>& bpms =
      timing.GetTimingSegments(SEGMENT_BPM);
  for (unsigned i = 0; i < bpms.size(); i++) {
    const BPMSegment* bs = ToBPM(bpms[i]);

    f.PutLine(ssprintf("%.6f=%.6f", bs->GetBeat(), bs->GetBPM()));
    if (i != bpms.size() - 1) {
      f.Write(",");
    }
  }
  f.PutLine(";");

  const std::vector<TimingSegment*>& stops =
      timing.GetTimingSegments(SEGMENT_STOP);
  const std::vector<TimingSegment*>& delays =
      timing.GetTimingSegments(SEGMENT_DELAY);

  std::map<float, float> all_pauses;
  const std::vector<TimingSegment*>& warps =
      timing.GetTimingSegments(SEGMENT_WARP);
  unsigned size = warps.size();
  if (size > 0) {
    for (unsigned i = 0; i < size; i++) {
      const WarpSegment* ws = static_cast<WarpSegment*>(warps[i]);
      int row = ws->GetRow();
      float bps = 60 / out.song_timing_.GetBPMAtRow(row);
      float skip = bps * ws->GetLength();
      all_pauses.insert(std::pair<float, float>(ws->GetBeat(), -skip));
    }
  }

  for (unsigned i = 0; i < stops.size(); i++) {
    const StopSegment* fs = ToStop(stops[i]);
    // Handle warps on the same row by summing the values.  Not sure this
    // plays out the same. -Kyz
    std::map<float, float>::iterator already_exists =
        all_pauses.find(fs->GetBeat());
    if (already_exists != all_pauses.end()) {
      already_exists->second += fs->GetPause();
    } else {
      all_pauses.insert(std::pair<float, float>(fs->GetBeat(), fs->GetPause()));
    }
  }
  // Delays can't be negative: thus, no effect.
  for (const TimingSegment* ss : delays) {
    float beat = NoteRowToBeat(ss->GetRow() - 1);
    float pause = ToDelay(ss)->GetPause();
    std::map<float, float>::iterator already_exists = all_pauses.find(beat);
    if (already_exists != all_pauses.end()) {
      already_exists->second += pause;
    } else {
      all_pauses.insert(std::pair<float, float>(beat, pause));
    }
  }

  f.Write("#STOPS:");
  std::vector<std::string> stop_lines;
  for (std::pair<const float&, const float&> ap : all_pauses) {
    stop_lines.push_back(ssprintf("%.6f=%.6f", ap.first, ap.second));
  }
  f.PutLine(join(",\n", stop_lines));

  f.PutLine(";");

  FOREACH_BackgroundLayer(b) {
    if (b == 0) {
      f.Write("#BGCHANGES:");
    } else if (out.GetBackgroundChanges(b).empty()) {
      continue;  // skip
    } else {
      f.Write(ssprintf("#BGCHANGES%d:", b + 1));
    }

    for (const BackgroundChange& bgc : out.GetBackgroundChanges(b)) {
      f.PutLine(bgc.ToString() + ",");
    }

    /* If there's an animation plan at all, add a dummy "-nosongbg-" tag to
     * indicate that this file doesn't want a song BG entry added at the end.
     * See SMLoader::TidyUpData. This tag will be removed on load.  Add it at a
     * very high beat, so it won't cause problems if loaded in older versions.
     */
    if (b == 0 && !out.GetBackgroundChanges(b).empty()) {
      f.PutLine(
          "99999=-nosongbg-=1.000=0=0=0 // don't automatically add "
          "-songbackground-");
    }
    f.PutLine(";");
  }

  if (out.GetForegroundChanges().size()) {
    f.Write("#FGCHANGES:");
    for (const BackgroundChange& bgc : out.GetForegroundChanges()) {
      f.PutLine(bgc.ToString() + ",");
    }
    f.PutLine(";");
  }

  f.Write("#KEYSOUNDS:");
  for (unsigned i = 0; i < out.keysound_file_.size(); i++) {
    f.Write(out.keysound_file_[i]);
    if (i != out.keysound_file_.size() - 1) {
      f.Write(",");
    }
  }
  f.PutLine(";");

  f.PutLine(ssprintf("#ATTACKS:%s;", out.GetAttackString().c_str()));
}

/**
 * @brief Turn a vector of lines into a single line joined by newline
 * characters.
 * @param lines the list of lines to join.
 * @return the joined lines. */
static std::string JoinLineList(std::vector<std::string>& lines) {
  for (unsigned i = 0; i < lines.size(); ++i) {
    TrimRight(lines[i]);
  }

  /* Skip leading blanks. */
  unsigned j = 0;
  while (j < lines.size() && lines.size() == 0) {
    ++j;
  }

  return join("\r\n", lines.begin() + j, lines.end());
}

/**
 * @brief Retrieve the notes from the #NOTES tag.
 * @param song the Song in question.
 * @param in the Steps in question.
 * @return the #NOTES tag. */
static std::string GetSMNotesTag(const Song& song, const Steps& in) {
  std::vector<std::string> lines;

  lines.push_back("");
  // Escape to prevent some clown from making a comment of "\r\n;"
  lines.push_back(ssprintf(
      "//---------------%s - %s----------------", in.steps_type_str_.c_str(),
      SmEscape(in.GetDescription()).c_str()));
  lines.push_back(song.keysound_file_.empty() ? "#NOTES:" : "#NOTES2:");
  lines.push_back(ssprintf("     %s:", in.steps_type_str_.c_str()));
  std::string desc = (USE_CREDIT ? in.GetCredit() : in.GetChartName());
  lines.push_back(ssprintf("     %s:", SmEscape(desc).c_str()));
  lines.push_back(
      ssprintf("     %s:", DifficultyToString(in.GetDifficulty()).c_str()));
  lines.push_back(ssprintf("     %d:", in.GetMeter()));

  std::vector<std::string> radar_values;
  // OpenITG simfiles use 11 radar categories.
  int categories = 11;
  FOREACH_PlayerNumber(pn) {
    const RadarValues& rv = in.GetRadarValues(pn);
    // Can't use the foreach anymore due to flexible radar lines.
    for (RadarCategory rc = (RadarCategory)0; rc < categories;
         enum_add<RadarCategory>(rc, 1)) {
      radar_values.push_back(ssprintf("%.6f", rv[rc]));
    }
  }
  lines.push_back(ssprintf("     %s:", join(",", radar_values).c_str()));

  std::string note_data;
  in.GetSMNoteData(note_data);

  split(note_data, "\n", lines, true);
  lines.push_back(";");

  return JoinLineList(lines);
}

bool NotesWriterSM::Write(
    std::string path, Song& out, const std::vector<Steps*>& steps_to_save) {
  int flags = RageFile::WRITE;

  flags |= RageFile::SLOW_FLUSH;

  RageFile f;
  if (!f.Open(path, flags)) {
    LOG->UserLog(
        "Song file", path, "couldn't be opened for writing: %s",
        f.GetError().c_str());
    return false;
  }

  return Write(f, out, steps_to_save);
}

bool NotesWriterSM::Write(
    RageFileBasic& f, Song& out, const std::vector<Steps*>& steps_to_save) {
  WriteGlobalTags(f, out);

  for (const Steps* steps : steps_to_save) {
    std::string tag = GetSMNotesTag(out, *steps);
    f.PutLine(tag);
  }
  if (f.Flush() == -1) {
    return false;
  }

  return true;
}

void NotesWriterSM::GetEditFileContents(
    const Song* song, const Steps* steps, std::string& out) {
  out = "";
  std::string dir = song->GetSongDir();

  // "Songs/foo/bar"; strip off "Songs/".
  std::vector<std::string> parts;
  split(dir, "/", parts);
  if (parts.size()) {
    dir = join("/", parts.begin() + 1, parts.end());
  }
  out += ssprintf("#SONG:%s;\r\n", dir.c_str());
  out += GetSMNotesTag(*song, *steps);
}

std::string NotesWriterSM::GetEditFileName(
    const Song* song, const Steps* steps) {
  /* Try to make a unique name. This isn't guaranteed. Edit descriptions are
   * case-sensitive, filenames on disk are usually not, and we decimate certain
   * characters for FAT filesystems. */
  std::string file =
      song->GetTranslitFullTitle() + " - " + steps->GetDescription();

  // HACK:
  if (steps->steps_type_ == StepsType_dance_double) {
    file += " (doubles)";
  }

  file += ".edit";

  MakeValidFilename(file);
  return file;
}

static LocalizedString DESTINATION_ALREADY_EXISTS(
    "NotesWriterSM",
    "Error renaming file.  Destination file '%s' already exists.");
static LocalizedString ERROR_WRITING_FILE(
    "NotesWriterSM", "Error writing file '%s'.");
bool NotesWriterSM::WriteEditFileToMachine(
    const Song* song, Steps* steps, std::string& error_out) {
  std::string dir =
      PROFILEMAN->GetProfileDir(ProfileSlot_Machine) + kEditStepsSubdir;

  std::string path = dir + GetEditFileName(song, steps);

  // Check to make sure that we're not clobering an existing file before
  // opening.
  bool file_name_changing =
      steps->GetSavedToDisk() && steps->GetFilename() != path;
  if (file_name_changing && DoesFileExist(path)) {
    error_out =
        ssprintf(DESTINATION_ALREADY_EXISTS.GetValue().c_str(), path.c_str());
    return false;
  }

  RageFile f;
  if (!f.Open(path, RageFile::WRITE | RageFile::SLOW_FLUSH)) {
    error_out = ssprintf(ERROR_WRITING_FILE.GetValue().c_str(), path.c_str());
    return false;
  }

  std::string tag;
  GetEditFileContents(song, steps, tag);
  if (f.PutLine(tag) == -1 || f.Flush() == -1) {
    error_out = ssprintf(ERROR_WRITING_FILE.GetValue().c_str(), path.c_str());
    return false;
  }

  /* If the file name of the edit has changed since the last save, then delete
   * the old file after saving the new one. If we delete it first, then we'll
   * lose data on error. */

  if (file_name_changing) {
    FILEMAN->Remove(steps->GetFilename());
  }
  steps->SetFilename(path);

  return true;
}

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
