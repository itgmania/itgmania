#include "global.h"

#include "CourseLoaderCRS.h"

#include <cfloat>
#include <vector>

#include "Course.h"
#include "CourseUtil.h"
#include "CourseWriterCRS.h"
#include "ImageCache.h"
#include "MsdFile.h"
#include "PlayerOptions.h"
#include "PrefsManager.h"
#include "RageFileManager.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "SongCacheIndex.h"
#include "SongManager.h"
#include "TitleSubstitution.h"

//Edit courses can only be so big before they are rejected.
const int MAX_EDIT_COURSE_SIZE_BYTES = 32 * 1024;  // 32KB

// The list of difficulty names for courses.
const char* g_CRSDifficultyNames[] = {
    "Beginner", "Easy", "Regular", "Difficult", "Challenge", "Edit",
};

// Retrieve the course difficulty based on the string name.
static CourseDifficulty CRSStringToDifficulty(const RString& difficulty_name) {
  FOREACH_ENUM(Difficulty, i)
  if (!difficulty_name.CompareNoCase(g_CRSDifficultyNames[i])) {
    return i;
  }
  return Difficulty_Invalid;
}

bool CourseLoaderCRS::LoadFromBuffer(
    const RString& path, const RString& buffer, Course& out) {
  MsdFile msd;
  msd.ReadFromString(buffer, /*unescape=*/false);
  return LoadFromMsd(path, msd, out, true);
}

bool CourseLoaderCRS::LoadFromMsd(
    const RString& path, const MsdFile& msd, Course& out, bool bFromCache) {
  AttackArray attacks;
  float gain_seconds = 0;
  for (unsigned i = 0; i < msd.GetNumValues(); i++) {
    RString value_name = msd.GetParam(i, 0);
    const MsdFile::value_t& params = msd.GetValue(i);

    // Handle the data.
    if (value_name.EqualsNoCase("COURSE")) {
      out.m_sMainTitle = params[1];
    } else if (value_name.EqualsNoCase("COURSETRANSLIT")) {
      out.m_sMainTitleTranslit = params[1];
    } else if (value_name.EqualsNoCase("SCRIPTER")) {
      out.m_sScripter = params[1];
    } else if (value_name.EqualsNoCase("DESCRIPTION")) {
      out.m_sDescription = params[1];
    } else if (value_name.EqualsNoCase("REPEAT")) {
      RString str = params[1];
      str.MakeLower();
      if (str.find("yes") != std::string::npos) {
        out.m_bRepeat = true;
      }
    }

    else if (value_name.EqualsNoCase("BANNER")) {
      out.m_sBannerPath = params[1];
    } else if (value_name.EqualsNoCase("BACKGROUND")) {
      out.m_sBackgroundPath = params[1];
    } else if (value_name.EqualsNoCase("LIVES")) {
      out.m_iLives = std::max(StringToInt(params[1]), 0);
    } else if (value_name.EqualsNoCase("GAINSECONDS")) {
      gain_seconds = StringToFloat(params[1]);
    } else if (value_name.EqualsNoCase("METER")) {
      if (params.params.size() == 2) {
        out.m_iCustomMeter[Difficulty_Medium] =
            std::max(StringToInt(params[1]), 0); /* compat */
      } else if (params.params.size() == 3) {
        const CourseDifficulty cd = CRSStringToDifficulty(params[1]);
        if (cd == Difficulty_Invalid) {
          LOG->UserLog(
              "Course file", path, "contains an invalid #METER string: \"%s\"",
              params[1].c_str());
          continue;
        }
        out.m_iCustomMeter[cd] = std::max(StringToInt(params[2]), 0);
      }
    }

    else if (value_name.EqualsNoCase("MODS")) {
      Attack attack;
      float end = -9999;
      for (unsigned j = 1; j < params.params.size(); ++j) {
        std::vector<RString> sBits;
        split(params[j], "=", sBits, false);
        if (sBits.size() < 2) {
          continue;
        }

        Trim(sBits[0]);
        if (!sBits[0].CompareNoCase("TIME")) {
          attack.fStartSecond = std::max(StringToFloat(sBits[1]), 0.0f);
        } else if (!sBits[0].CompareNoCase("LEN")) {
          attack.fSecsRemaining = StringToFloat(sBits[1]);
        } else if (!sBits[0].CompareNoCase("END")) {
          end = StringToFloat(sBits[1]);
        } else if (!sBits[0].CompareNoCase("MODS")) {
          attack.sModifiers = sBits[1];

          if (end != -9999) {
            attack.fSecsRemaining = end - attack.fStartSecond;
            end = -9999;
          }

          if (attack.fSecsRemaining <= 0.0f) {
            LOG->UserLog(
                "Course file", path,
                "has an attack with a nonpositive length: %s",
                sBits[1].c_str());
            attack.fSecsRemaining = 0.0f;
          }

          // warn on invalid so we catch typos on load
          CourseUtil::WarnOnInvalidMods(attack.sModifiers);

          attacks.push_back(attack);
        } else {
          LOG->UserLog(
              "Course file", path, "has an unexpected value named '%s'",
              sBits[0].c_str());
        }
      }

    } else if (value_name.EqualsNoCase("SONG")) {
      CourseEntry new_entry;

      // infer entry::Type from the first param
      // todo: make sure these aren't generating bogus entries due
      // to a lack of songs. -aj
      int num_songs = SONGMAN->GetNumSongs();
      // Most played.
      if (params[1].Left(strlen("BEST")) == "BEST") {
        int choose_index =
            StringToInt(params[1].Right(params[1].size() - strlen("BEST"))) -
            1;
        if (choose_index > num_songs) {
          // Looking up a song that doesn't exist.
          LOG->UserLog(
              "Course file", path,
              "is trying to load BEST%i with only %i songs installed. "
              "This entry will be ignored.",
              choose_index, num_songs);
          out.m_bIncomplete = true;
					// Skip this #SONG
          continue;
        }

        new_entry.choose_index_ = choose_index;
        CLAMP(new_entry.choose_index_, 0, 500);
        new_entry.song_sort_ = SongSort_MostPlays;
      }
      // Least played.
      else if (params[1].Left(strlen("WORST")) == "WORST") {
        int choose_index =
            StringToInt(params[1].Right(params[1].size() - strlen("WORST"))) -
            1;
        if (choose_index > num_songs) {
          // Looking up a song that doesn't exist.
          LOG->UserLog(
              "Course file", path,
              "is trying to load WORST%i with only %i songs installed. "
              "This entry will be ignored.",
              choose_index, num_songs);
          out.m_bIncomplete = true;
					// Skip this #SONG
          continue;
        }

        new_entry.choose_index_ = choose_index;
        CLAMP(new_entry.choose_index_, 0, 500);
        new_entry.song_sort_ = SongSort_FewestPlays;
      }
      // Best grades.
      else if (params[1].Left(strlen("GRADEBEST")) == "GRADEBEST") {
        new_entry.choose_index_ =
            StringToInt(
                params[1].Right(params[1].size() - strlen("GRADEBEST"))) -
            1;
        CLAMP(new_entry.choose_index_, 0, 500);
        new_entry.song_sort_ = SongSort_TopGrades;
      }
      // Worst grades.
      else if (params[1].Left(strlen("GRADEWORST")) == "GRADEWORST") {
        new_entry.choose_index_ =
            StringToInt(
                params[1].Right(params[1].size() - strlen("GRADEWORST"))) -
            1;
        CLAMP(new_entry.choose_index_, 0, 500);
        new_entry.song_sort_ = SongSort_LowestGrades;
      } else if (params[1] == "*") {
        // new_entry.bSecret = true;
      }
      // group random
      else if (params[1].Right(1) == "*") {
        // new_entry.bSecret = true;
        RString song_str = params[1];
        song_str.Replace("\\", "/");
        std::vector<RString> bits;
        split(song_str, "/", bits);
        if (bits.size() == 2) {
          new_entry.song_criteria_.m_sGroupName = bits[0];
        } else {
          LOG->UserLog(
              "Course file", path,
              "contains a random_within_group entry \"%s\" that is invalid. "
              "Song should be in the format \"<group>/*\".",
              song_str.c_str());
        }

        if (!SONGMAN->DoesSongGroupExist(
                new_entry.song_criteria_.m_sGroupName)) {
          LOG->UserLog(
              "Course file", path,
              "random_within_group entry \"%s\" specifies a group that doesn't "
              "exist. "
              "This entry will be ignored.",
              song_str.c_str());
          out.m_bIncomplete = true;
          continue;  // skip this #SONG
        }
      } else {
        RString song_str = params[1];
        song_str.Replace("\\", "/");
        std::vector<RString> bits;
        split(song_str, "/", bits);

        Song* song = nullptr;
        if (bits.size() == 2) {
          new_entry.song_criteria_.m_sGroupName = bits[0];
          song = SONGMAN->FindSong(bits[0], bits[1]);
        } else if (bits.size() == 1) {
          song = SONGMAN->FindSong("", song_str);
        }
        new_entry.song_id_.FromSong(song);

        if (song == nullptr) {
          LOG->UserLog(
              "Course file", path,
              "contains a fixed song entry \"%s\" that does not exist. "
              "This entry will be ignored.",
              song_str.c_str());
          out.m_bIncomplete = true;
          continue;  // skip this #SONG
        }
      }

      new_entry.steps_criteria_.m_difficulty =
          OldStyleStringToDifficulty(params[2]);
      // most CRS files use old-style difficulties, but Difficulty enum values
      // can be used in SM5. Test for those too.
      if (new_entry.steps_criteria_.m_difficulty == Difficulty_Invalid) {
        new_entry.steps_criteria_.m_difficulty = StringToDifficulty(params[2]);
      }
      if (new_entry.steps_criteria_.m_difficulty == Difficulty_Invalid) {
        int retval = sscanf(
            params[2], "%d..%d", &new_entry.steps_criteria_.m_iLowMeter,
            &new_entry.steps_criteria_.m_iHighMeter);
        if (retval == 1) {
          new_entry.steps_criteria_.m_iHighMeter =
              new_entry.steps_criteria_.m_iLowMeter;
        } else if (retval != 2) {
          LOG->UserLog(
              "Course file", path,
              "contains an invalid difficulty setting: \"%s\", 3..6 used "
              "instead",
              params[2].c_str());
          new_entry.steps_criteria_.m_iLowMeter = 3;
          new_entry.steps_criteria_.m_iHighMeter = 6;
        }
        new_entry.steps_criteria_.m_iLowMeter =
            std::max(new_entry.steps_criteria_.m_iLowMeter, 1);
        new_entry.steps_criteria_.m_iHighMeter = std::max(
            new_entry.steps_criteria_.m_iHighMeter,
            new_entry.steps_criteria_.m_iLowMeter);
      }

      {
        // If "showcourse" or "noshowcourse" is in the list, force
        // new_entry.secret on or off.
        std::vector<RString> mods;
        split(params[3], ",", mods, true);
        for (int j = (int)mods.size() - 1; j >= 0; --j) {
          RString& mod = mods[j];
          TrimLeft(mod);
          TrimRight(mod);
          if (!mod.CompareNoCase("showcourse")) {
            new_entry.is_secret_ = false;
          } else if (!mod.CompareNoCase("noshowcourse")) {
            new_entry.is_secret_ = true;
          } else if (!mod.CompareNoCase("nodifficult")) {
            new_entry.no_difficult_ = true;
          } else if (
              mod.length() > 5 && !mod.Left(5).CompareNoCase("award")) {
            new_entry.gain_lives_ = StringToInt(mod.substr(5));
          } else {
            continue;
          }
          mods.erase(mods.begin() + j);
        }
        new_entry.modifiers_ = join(",", mods);
      }

      new_entry.attacks_ = attacks;
      new_entry.gain_seconds_ = gain_seconds;
      attacks.clear();

      out.m_vEntries.push_back(new_entry);
    } else if (
        !value_name.EqualsNoCase("DISPLAYCOURSE") ||
        !value_name.EqualsNoCase("COMBO") ||
        !value_name.EqualsNoCase("COMBOMODE")) {
      // Ignore
    }

    else if (bFromCache && !value_name.EqualsNoCase("RADAR")) {
      StepsType steps_type = (StepsType)StringToInt(params[1]);
      CourseDifficulty course_difficulty = (CourseDifficulty)StringToInt(params[2]);

      RadarValues radar_values;
      radar_values.FromString(params[3]);
      out.m_RadarCache[Course::CacheEntry(steps_type, course_difficulty)] = radar_values;
    } else if (value_name.EqualsNoCase("STYLE")) {
      RString styles_str = params[1];
      std::vector<RString> styles;
      split(styles_str, ",", styles);
      for (const RString& style : styles) {
        out.m_setStyles.insert(style);
      }

    } else {
      LOG->UserLog(
          "Course file", path, "contains an unexpected value named \"%s\"",
          value_name.c_str());
    }
  }

  if (out.m_sBannerPath.empty()) {
    const RString sFName = SetExtension(out.m_sPath, "");

    std::vector<RString> array_possible_banners;
    GetDirListing(sFName + "*.png", array_possible_banners, false, false);
    GetDirListing(sFName + "*.jpg", array_possible_banners, false, false);
    GetDirListing(sFName + "*.jpeg", array_possible_banners, false, false);
    GetDirListing(sFName + "*.bmp", array_possible_banners, false, false);
    GetDirListing(sFName + "*.gif", array_possible_banners, false, false);
    if (!array_possible_banners.empty()) {
      out.m_sBannerPath = array_possible_banners[0];
    }
  }

  static TitleSubst tsub("Courses");

  TitleFields title;
  title.Title = out.m_sMainTitle;
  title.TitleTranslit = out.m_sMainTitleTranslit;
  tsub.Subst(title);
  out.m_sMainTitle = title.Title;
  out.m_sMainTitleTranslit = title.TitleTranslit;

  // Cache and load the course banner. Only bother doing this if at least one
  // song was found in the course.
  if (out.m_vEntries.empty()) {
    return true;
  }
  if (out.m_sBannerPath != "") {
    IMAGECACHE->CacheImage("Banner", out.GetBannerPath());
  }

  // Cache each trail RadarValues that's slow to load, so we
  // don't have to do it at runtime.
  if (!bFromCache) {
    out.CalculateRadarValues();
  }

  return true;
}

bool CourseLoaderCRS::LoadFromCRSFile(const RString& path, Course& out) {
  RString sPath = path;

  out.Init();

  out.m_sPath = sPath;  // save path

  // save group name
  {
    std::vector<RString> parts;
    split(sPath, "/", parts, false);
    if (parts.size() >= 4) {  // e.g. "/Courses/blah/fun.crs"
      out.m_sGroupName = parts[parts.size() - 2];
    }
  }

  bool use_cache = true;
  {
    // First look in the cache for this course. Don't bother honoring
    // FastLoad for checking the course hash, since courses are normally
    // grouped into a few directories, not one directory per course.
    unsigned hash = SONGINDEX->GetCacheHash(out.m_sPath);
    if (!DoesFileExist(out.GetCacheFilePath())) {
      use_cache = false;
    }
    // XXX: if !FastLoad, regen cache if the used songs have changed
    if (!PREFSMAN->m_bFastLoad && GetHashForFile(out.m_sPath) != hash) {
      use_cache = false;  // this cache is out of date
    }
  }

  if (use_cache) {
    RString sCacheFile = out.GetCacheFilePath();
    LOG->Trace(
        "CourseLoaderCRS::LoadFromCRSFile(\"%s\") (\"%s\")", sPath.c_str(),
        sCacheFile.c_str());
    sPath = sCacheFile;
  } else {
    LOG->Trace("CourseLoaderCRS::LoadFromCRSFile(\"%s\")", sPath.c_str());
  }

  MsdFile msd;
  if (!msd.ReadFile(sPath, false))  // don't unescape
  {
    LOG->UserLog(
        "Course file", sPath, "couldn't be opened: %s.",
        msd.GetError().c_str());
    return false;
  }

  if (!LoadFromMsd(sPath, msd, out, use_cache)) {
    return false;
  }

  if (!use_cache) {
    // If we have any cache data, write the cache file.
    if (out.m_RadarCache.size()) {
      RString sCachePath = out.GetCacheFilePath();
      if (CourseWriterCRS::Write(out, sCachePath, true)) {
        SONGINDEX->AddCacheIndex(out.m_sPath, GetHashForFile(out.m_sPath));
      }
    }
  }

  return true;
}

bool CourseLoaderCRS::LoadEditFromFile(
    const RString& edit_file_path, ProfileSlot slot) {
  LOG->Trace("CourseLoaderCRS::LoadEdit(%s)", edit_file_path.c_str());

  int bytes = FILEMAN->GetFileSizeInBytes(edit_file_path);
  if (bytes > MAX_EDIT_COURSE_SIZE_BYTES) {
    LOG->UserLog(
        "Edit file", edit_file_path,
        "is unreasonably large. It won't be loaded.");
    return false;
  }

  MsdFile msd;
  if (!msd.ReadFile(edit_file_path, false))  // don't unescape
  {
    LOG->UserLog(
        "Edit file", edit_file_path, "couldn't be opened: %s",
        msd.GetError().c_str());
    return false;
  }
  Course* course = new Course;

  course->m_sPath = edit_file_path;
  LoadFromMsd(edit_file_path, msd, *course, true);

  course->m_LoadedFromProfile = slot;

  SONGMAN->AddCourse(course);
  return true;
}

bool CourseLoaderCRS::LoadEditFromBuffer(
    const RString& buffer, const RString& path, ProfileSlot slot) {
  Course* course = new Course;
  if (!LoadFromBuffer(path, buffer, *course)) {
    delete course;
    return false;
  }

  course->m_LoadedFromProfile = slot;

  SONGMAN->AddCourse(course);
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
