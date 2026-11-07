#include "CourseWriterCRS.h"

#include <string>
#include <vector>

#include "Attack.h"
#include "Course.h"
#include "Difficulty.h"
#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "RadarValues.h"
#include "RageFile.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "Song.h"
#include "global.h"

/** @brief Load the difficulty names from CourseLoaderCRS. */
extern const char* g_CRSDifficultyNames[];  // in CourseLoaderCRS

/**
 * @brief Get the string of the course difficulty.
 * @param iVal the course difficulty.
 * @return the string.
 */
static std::string DifficultyToCRSString(CourseDifficulty val) {
  return g_CRSDifficultyNames[val];
}

bool CourseWriterCRS::Write(
    const Course& course, const std::string& path, bool saving_cache) {
  RageFile f;
  if (!f.Open(path, RageFile::WRITE)) {
    LOG->UserLog(
        "Course file", path, "couldn't be written: %s", f.GetError().c_str());
    return false;
  }

  return CourseWriterCRS::Write(course, f, saving_cache);
}

bool CourseWriterCRS::Write(
    const Course& course, RageFileBasic& f, bool saving_cache) {
  ASSERT(!course.is_autogen_);

  f.PutLine(ssprintf("#COURSE:%s;", course.main_title_.c_str()));
  if (course.main_title_translit_ != "") {
    f.PutLine(
        ssprintf("#COURSETRANSLIT:%s;", course.main_title_translit_.c_str()));
  }
  if (course.scripter_ != "") {
    f.PutLine(ssprintf("#SCRIPTER:%s;", course.scripter_.c_str()));
  }
  if (course.description_ != "") {
    f.PutLine(ssprintf("#DESCRIPTION:%s;", course.description_.c_str()));
  }
  if (course.repeat_) {
    f.PutLine("#REPEAT:YES;");
  }
  if (course.lives_ != -1) {
    f.PutLine(ssprintf("#LIVES:%i;", course.lives_));
  }
  if (!course.banner_path_.empty()) {
    f.PutLine(ssprintf("#BANNER:%s;", course.banner_path_.c_str()));
  }

  if (!course.set_styles_.empty()) {
    std::vector<std::string> styles;
    styles.insert(
        styles.begin(), course.set_styles_.begin(), course.set_styles_.end());
    f.PutLine(ssprintf("#STYLE:%s;", join(",", styles).c_str()));
  }

  FOREACH_ENUM(CourseDifficulty, cd) {
    if (course.custom_meter_[cd] == -1) {
      continue;
    }
    f.PutLine(ssprintf(
        "#METER:%s:%i;", DifficultyToCRSString(cd).c_str(),
        course.custom_meter_[cd]));
  }

  if (saving_cache) {
    f.PutLine("// cache tags:");

    Course::RadarCache_t::const_iterator it;
    for (it = course.radar_cache_.begin(); it != course.radar_cache_.end();
         ++it) {
      // #RADAR:type:difficulty:value,value,value...;
      const Course::CacheEntry& entry = it->first;
      StepsType st = entry.first;
      CourseDifficulty cd = entry.second;

      std::vector<std::string> radar_values;
      const RadarValues& rv = it->second;
      for (int r = 0; r < NUM_RadarCategory; r++) {
        radar_values.push_back(ssprintf("%.3f", rv[r]));
      }
      std::string line = ssprintf("#RADAR:%i:%i:", st, cd);
      line += join(",", radar_values) + ";";
      f.PutLine(line);
    }
    f.PutLine("// end cache tags");
  }

  for (unsigned i = 0; i < course.entries_.size(); i++) {
    const CourseEntry& entry = course.entries_[i];
    if (entry.use_song_select_) {
      WriteSongSelectCourseEntry(entry, f);
    } else {
      WriteCourseEntry(entry, f);
    }
  }

  return true;
}

bool CourseWriterCRS::WriteCourseEntry(
    const CourseEntry& entry, RageFileBasic& f) {
  for (unsigned j = 0; j < entry.attacks_.size(); ++j) {
    if (j == 0) {
      f.PutLine("#MODS:");
    }

    const Attack& a = entry.attacks_[j];
    f.Write(ssprintf(
        "  TIME=%.2f:LEN=%.2f:MODS=%s", a.start_second_, a.secs_remaining_,
        a.modifiers_.c_str()));

    if (j + 1 < entry.attacks_.size()) {
      f.Write(":");
    } else {
      f.Write(";");
    }
    f.PutLine("");
  }

  if (entry.gain_seconds_ > 0) {
    f.PutLine(ssprintf("#GAINSECONDS:%f;", entry.gain_seconds_));
  }

  if (entry.song_sort_ == SongSort_MostPlays && entry.choose_index_ != -1) {
    f.Write(ssprintf("#SONG:BEST%d", entry.choose_index_ + 1));
  } else if (
      entry.song_sort_ == SongSort_FewestPlays && entry.choose_index_ != -1) {
    f.Write(ssprintf("#SONG:WORST%d", entry.choose_index_ + 1));
  } else if (
      entry.song_sort_ == SongSort_TopGrades && entry.choose_index_ != -1) {
    f.Write(ssprintf("#SONG:GRADEBEST%d", entry.choose_index_ + 1));
  } else if (
      entry.song_sort_ == SongSort_LowestGrades && entry.choose_index_ != -1) {
    f.Write(ssprintf("#SONG:GRADEWORST%d", entry.choose_index_ + 1));
  } else if (entry.song_id_.ToSong()) {
    Song* p_song = entry.song_id_.ToSong();
    const std::string& s_song = Basename(p_song->GetSongDir());

    f.Write("#SONG:");
    if (entry.song_criteria_.group_names_.size() > 0) {
      f.Write(entry.song_criteria_.group_names_[0] + '/');
    }
    f.Write(s_song);
  } else if (entry.song_criteria_.group_names_.size() > 0) {
    f.Write(
        ssprintf("#SONG:%s/*", entry.song_criteria_.group_names_[0].c_str()));
  } else {
    f.Write("#SONG:*");
  }

  f.Write(":");
  if (entry.steps_criteria_.difficulty_ != Difficulty_Invalid) {
    f.Write(DifficultyToString(entry.steps_criteria_.difficulty_));
  } else if (
      entry.steps_criteria_.low_meter_ != -1 &&
      entry.steps_criteria_.high_meter_ != -1) {
    f.Write(ssprintf(
        "%d..%d", entry.steps_criteria_.low_meter_,
        entry.steps_criteria_.high_meter_));
  }
  f.Write(":");

  std::string modifiers = entry.modifiers_;

  if (entry.secret_) {
    if (modifiers != "") {
      modifiers += ",";
    }
    modifiers += entry.secret_ ? "noshowcourse" : "showcourse";
  }

  if (entry.no_difficult_) {
    if (modifiers != "") {
      modifiers += ",";
    }
    modifiers += "nodifficult";
  }

  if (entry.gain_lives_ > -1) {
    if (!modifiers.empty()) {
      modifiers += ',';
    }
    modifiers += ssprintf("award%d", entry.gain_lives_);
  }

  f.Write(modifiers);

  f.PutLine(";");
  return true;
}

bool CourseWriterCRS::WriteSongSelectCourseEntry(
    const CourseEntry& entry, RageFileBasic& f) {
  for (unsigned j = 0; j < entry.attacks_.size(); ++j) {
    if (j == 0) {
      f.PutLine("#MODS:");
    }

    const Attack& a = entry.attacks_[j];
    f.Write(ssprintf(
        "  TIME=%.2f:LEN=%.2f:MODS=%s", a.start_second_, a.secs_remaining_,
        a.modifiers_.c_str()));

    if (j + 1 < entry.attacks_.size()) {
      f.Write(":");
    } else {
      f.Write(";");
    }
    f.PutLine("");
  }

  std::vector<std::string> song_select_params;

  // TODO: Re-escape everything
  if (entry.song_criteria_.song_names_.size() > 0) {
    std::string song_names = join(
        ",",
        SmEscape(
            entry.song_criteria_.song_names_, {'\\', ':', ';', '#', ',', '='}));
    song_select_params.push_back(ssprintf("TITLE=%s", song_names.c_str()));
  }
  if (entry.song_criteria_.group_names_.size() > 0) {
    std::string group_names = join(
        ",", SmEscape(
                 entry.song_criteria_.group_names_,
                 {'\\', ':', ';', '#', ',', '='}));
    song_select_params.push_back(ssprintf("GROUP=%s", group_names.c_str()));
  }
  if (entry.song_criteria_.artist_names_.size() > 0) {
    std::string artist_names = join(
        ",", SmEscape(
                 entry.song_criteria_.artist_names_,
                 {'\\', ':', ';', '#', ',', '='}));
    song_select_params.push_back(ssprintf("ARTIST=%s", artist_names.c_str()));
  }
  if (entry.song_criteria_.use_song_allowed_list_ &&
      entry.song_criteria_.song_genre_allowed_list_.size() > 0) {
    std::string genre_names = join(
        ",", SmEscape(
                 entry.song_criteria_.song_genre_allowed_list_,
                 {'\\', ':', ';', '#', ',', '='}));
    song_select_params.push_back(ssprintf("GENRE=%s", genre_names.c_str()));
  }
  if (entry.steps_criteria_.difficulties_.size() > 0) {
    std::vector<std::string> difficulties;
    for (unsigned d = 0; d < entry.steps_criteria_.difficulties_.size(); d++) {
      Difficulty diff = entry.steps_criteria_.difficulties_[d];
      if (diff != Difficulty_Invalid) {
        difficulties.push_back(DifficultyToString(diff));
      }
    }
    song_select_params.push_back("DIFFICULTY=" + join(",", difficulties));
  }
  if (entry.song_sort_ != SongSort_Randomize && entry.choose_index_ > -1) {
    std::string song_sort = SongSortToString(entry.song_sort_);
    song_select_params.push_back(
        ssprintf("SORT=%s,%d", song_sort.c_str(), entry.choose_index_ + 1));
  }
  if (entry.song_criteria_.min_duration_seconds_ > 0 &&
      entry.song_criteria_.max_duration_seconds_ > 0) {
    song_select_params.push_back(ssprintf(
        "DURATION=%d-%d", (int)entry.song_criteria_.min_duration_seconds_,
        (int)entry.song_criteria_.max_duration_seconds_));
  }
  if (entry.song_criteria_.min_bpm_ > 0 && entry.song_criteria_.max_bpm_ > 0) {
    song_select_params.push_back(ssprintf(
        "BPMRANGE=%f-%f", entry.song_criteria_.min_bpm_,
        entry.song_criteria_.max_bpm_));
  }
  if (entry.steps_criteria_.low_meter_ > 0 &&
      entry.steps_criteria_.high_meter_ > 0) {
    song_select_params.push_back(ssprintf(
        "METER=%d-%d", entry.steps_criteria_.low_meter_,
        entry.steps_criteria_.high_meter_));
  }
  if (entry.gain_lives_ > 0) {
    song_select_params.push_back(ssprintf("GAINLIVES=%d", entry.gain_lives_));
  }
  if (entry.gain_seconds_ > 0) {
    song_select_params.push_back(
        ssprintf("GAINSECONDS=%f", entry.gain_seconds_));
  }

  std::vector<std::string> mods;
  split(entry.modifiers_, ",", mods);

  if (entry.secret_) {
    mods.push_back("noshowcourse");
  } else {
    mods.push_back("showcourse");
  }
  if (entry.no_difficult_) {
    mods.push_back("nodifficult");
  }
  song_select_params.push_back("MODS=" + join(",", mods));

  std::string song_select = join(":", song_select_params);
  f.PutLine(ssprintf("#SONGSELECT:%s;", song_select.c_str()));

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
