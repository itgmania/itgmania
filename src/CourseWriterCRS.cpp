#include "global.h"

#include "CourseWriterCRS.h"

#include <vector>

#include "Course.h"
#include "RageFile.h"
#include "RageFileDriverMemory.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "Song.h"

// Load the difficulty names from CourseLoaderCRS.
extern const char* g_CRSDifficultyNames[];  // in CourseLoaderCRS

// @brief Get the string of the course difficulty.
static RString DifficultyToCRSString(CourseDifficulty course_difficulty) {
  return g_CRSDifficultyNames[course_difficulty];
}

bool CourseWriterCRS::Write(
    const Course& course, const RString& path, bool saving_cache) {
  RageFile f;
  if (!f.Open(path, RageFile::WRITE)) {
    LOG->UserLog(
        "Course file", path, "couldn't be written: %s", f.GetError().c_str());
    return false;
  }

  return CourseWriterCRS::Write(course, f, saving_cache);
}

void CourseWriterCRS::GetEditFileContents(const Course* course, RString& out) {
  RageFileObjMem mem;
  CourseWriterCRS::Write(*course, mem, true);
  out = mem.GetString();
}

bool CourseWriterCRS::Write(
    const Course& course, RageFileBasic& f, bool saving_cache) {
  ASSERT(!course.m_bIsAutogen);

  f.PutLine(ssprintf("#COURSE:%s;", course.m_sMainTitle.c_str()));
  if (course.m_sMainTitleTranslit != "") {
    f.PutLine(
        ssprintf("#COURSETRANSLIT:%s;", course.m_sMainTitleTranslit.c_str()));
  }
  if (course.m_sScripter != "") {
    f.PutLine(ssprintf("#SCRIPTER:%s;", course.m_sScripter.c_str()));
  }
  if (course.m_sDescription != "") {
    f.PutLine(ssprintf("#DESCRIPTION:%s;", course.m_sDescription.c_str()));
  }
  if (course.m_bRepeat) {
    f.PutLine("#REPEAT:YES;");
  }
  if (course.m_iLives != -1) {
    f.PutLine(ssprintf("#LIVES:%i;", course.m_iLives));
  }
  if (!course.m_sBannerPath.empty()) {
    f.PutLine(ssprintf("#BANNER:%s;", course.m_sBannerPath.c_str()));
  }

  if (!course.m_setStyles.empty()) {
    std::vector<RString> styles;
    styles.insert(
        styles.begin(), course.m_setStyles.begin(), course.m_setStyles.end());
    f.PutLine(ssprintf("#STYLE:%s;", join(",", styles).c_str()));
  }

  FOREACH_ENUM(CourseDifficulty, cd) {
    if (course.m_iCustomMeter[cd] == -1) {
      continue;
    }
    f.PutLine(ssprintf(
        "#METER:%s:%i;", DifficultyToCRSString(cd).c_str(),
        course.m_iCustomMeter[cd]));
  }

  if (saving_cache) {
    f.PutLine("// cache tags:");

    for (auto it = course.m_RadarCache.begin(); it != course.m_RadarCache.end();
         ++it) {
      // #RADAR:type:difficulty:value,value,value...;
      const Course::CacheEntry& entry = it->first;
      StepsType steps_type = entry.first;
      CourseDifficulty course_difficulty = entry.second;

      std::vector<RString> all_radar_values;
      const RadarValues& radar_values = it->second;
      for (int r = 0; r < NUM_RadarCategory; ++r) {
        all_radar_values.push_back(ssprintf("%.3f", radar_values[r]));
      }
      RString line = ssprintf("#RADAR:%i:%i:", steps_type, course_difficulty);
      line += join(",", all_radar_values) + ";";
      f.PutLine(line);
    }
    f.PutLine("// end cache tags");
  }

  for (unsigned i = 0; i < course.m_vEntries.size(); ++i) {
    const CourseEntry& entry = course.m_vEntries[i];

    for (unsigned j = 0; j < entry.attacks_.size(); ++j) {
      if (j == 0) {
        f.PutLine("#MODS:");
      }

      const Attack& attack = entry.attacks_[j];
      f.Write(ssprintf(
          "  TIME=%.2f:LEN=%.2f:MODS=%s", attack.fStartSecond,
          attack.fSecsRemaining, attack.sModifiers.c_str()));

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
    } else if (entry.song_id_.ToSong()) {
      Song* song = entry.song_id_.ToSong();
      const RString& song_name = Basename(song->GetSongDir());

      f.Write("#SONG:");
      if (!entry.song_criteria_.m_sGroupName.empty()) {
        f.Write(entry.song_criteria_.m_sGroupName + '/');
      }
      f.Write(song_name);
    } else if (!entry.song_criteria_.m_sGroupName.empty()) {
      f.Write(
          ssprintf("#SONG:%s/*", entry.song_criteria_.m_sGroupName.c_str()));
    } else {
      f.Write("#SONG:*");
    }

    f.Write(":");
    if (entry.steps_criteria_.m_difficulty != Difficulty_Invalid) {
      f.Write(DifficultyToString(entry.steps_criteria_.m_difficulty));
    } else if (
        entry.steps_criteria_.m_iLowMeter != -1 &&
        entry.steps_criteria_.m_iHighMeter != -1) {
      f.Write(ssprintf(
          "%d..%d", entry.steps_criteria_.m_iLowMeter,
          entry.steps_criteria_.m_iHighMeter));
    }
    f.Write(":");

    RString modifiers = entry.modifiers_;

    if (entry.is_secret_) {
      if (modifiers != "") {
        modifiers += ",";
      }
      modifiers += entry.is_secret_ ? "noshowcourse" : "showcourse";
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
  }

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
