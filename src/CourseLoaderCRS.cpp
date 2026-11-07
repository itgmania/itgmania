#include "CourseLoaderCRS.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Attack.h"
#include "Course.h"
#include "CourseUtil.h"
#include "CourseWriterCRS.h"
#include "Difficulty.h"
#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "ImageCache.h"
#include "MsdFile.h"
#include "PlayerOptions.h"
#include "PrefsManager.h"
#include "RadarValues.h"
#include "RageFileManager.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "SongCacheIndex.h"
#include "SongManager.h"
#include "StdString.h"
#include "TitleSubstitution.h"

/** @brief Edit courses can only be so big before they are rejected. */
const int MAX_EDIT_COURSE_SIZE_BYTES = 32 * 1024;  // 32KB

/** @brief The list of difficulty names for courses. */
const char* g_CRSDifficultyNames[] = {
    "Beginner", "Easy", "Regular", "Difficult", "Challenge", "Edit",
};

// This is a convenience function to handle splitting a string that potentially
// has escaped delimitor characters. We need to split the string while still
// maintaining those escaped characters. So first, replace escaped characters
// with "||escaped-delim||" Then, replace any remaining delimitor characters
// with "||regular-delim||" Then, put the escaped characters back, by replacing
// "||escaped-delim||" with "\\" + sDelimitor And finally, split the string by
// instances of "||regular-delim||" So for instance, "Thing 1, Thing\,2" becomes
// "Thing 1||regular-delim||Thing \, 2"
void split_minding_escaped_delims(
    const std::string& source, const std::string& delimitor,
    std::vector<std::string>& addit) {
  std::string source_copy = source;
  std::string escaped_delim = "||escaped-delim||";
  std::string regular_delim = "||regular-delim||";
  Replace(source_copy, "\\" + delimitor, escaped_delim);
  Replace(source_copy, delimitor, regular_delim);
  Replace(source_copy, escaped_delim, "\\" + delimitor);
  split(source_copy, regular_delim, addit);
}

/**
 * @brief Retrieve the course difficulty based on the string name.
 * @param s the name of the difficulty.
 * @return the course difficulty.
 */
static CourseDifficulty CRSStringToDifficulty(const std::string& s) {
  FOREACH_ENUM(Difficulty, i)
  if (!CompareNoCase(s, g_CRSDifficultyNames[i])) {
    return i;
  }
  return Difficulty_Invalid;
}

bool CourseLoaderCRS::LoadFromMsd(
    const std::string& path, const MsdFile& msd, Course& out, bool from_cache) {
  AttackArray attacks;
  float gain_seconds = 0;
  for (unsigned i = 0; i < msd.GetNumValues(); i++) {
    std::string value_name = msd.GetParam(i, 0);
    const MsdFile::value_t& params = msd.GetValue(i);

    // handle the data
    if (EqualsNoCase(value_name, "COURSE")) {
      out.main_title_ = params[1];
    } else if (EqualsNoCase(value_name, "COURSETRANSLIT")) {
      out.main_title_translit_ = params[1];
    } else if (EqualsNoCase(value_name, "SCRIPTER")) {
      out.scripter_ = params[1];
    } else if (EqualsNoCase(value_name, "DESCRIPTION")) {
      out.description_ = params[1];
    } else if (EqualsNoCase(value_name, "REPEAT")) {
      std::string str = params[1];
      MakeLower(str);
      if (str.find("yes") != std::string::npos) {
        out.repeat_ = true;
      }
    }

    else if (EqualsNoCase(value_name, "BANNER")) {
      out.banner_path_ = params[1];
    } else if (EqualsNoCase(value_name, "BACKGROUND")) {
      out.background_path_ = params[1];
    } else if (EqualsNoCase(value_name, "LIVES")) {
      out.lives_ = std::max(StringToInt(params[1]), 0);
    } else if (EqualsNoCase(value_name, "GAINSECONDS")) {
      gain_seconds = StringToFloat(params[1]);
    } else if (EqualsNoCase(value_name, "METER")) {
      if (params.params_.size() == 2) {
        out.custom_meter_[Difficulty_Medium] =
            std::max(StringToInt(params[1]), 0); /* compat */
      } else if (params.params_.size() == 3) {
        const CourseDifficulty cd = CRSStringToDifficulty(params[1]);
        if (cd == Difficulty_Invalid) {
          LOG->UserLog(
              "Course file", path, "contains an invalid #METER string: \"%s\"",
              params[1].c_str());
          continue;
        }
        out.custom_meter_[cd] = std::max(StringToInt(params[2]), 0);
      }
    }

    else if (EqualsNoCase(value_name, "MODS")) {
      CourseLoaderCRS::ParseCourseMods(params, attacks, path);
    } else if (EqualsNoCase(value_name, "SONG")) {
      CourseEntry new_entry;
      if (CourseLoaderCRS::ParseCourseSong(params, new_entry, path) == false) {
        out.incomplete_ = true;
        continue;  // Skip this #SONG
      }

      new_entry.attacks_ = attacks;
      new_entry.gain_seconds_ = gain_seconds;
      attacks.clear();
      out.entries_.push_back(new_entry);
    } else if (EqualsNoCase(value_name, "SONGSELECT")) {
      CourseEntry new_entry;
      new_entry.use_song_select_ = true;
      if (CourseLoaderCRS::ParseCourseSongSelect(params, new_entry, path) ==
          false) {
        out.incomplete_ = true;
        continue;  // Skip this #SONGSELECT
      }
      new_entry.attacks_ = attacks;
      // new_entry.fGainSeconds = fGainSeconds; // SONGSELECT already has a
      // GAINSECONDS option, so we probably don't need this?
      attacks.clear();
      out.entries_.push_back(new_entry);
    } else if (
        !EqualsNoCase(value_name, "DISPLAYCOURSE") ||
        !EqualsNoCase(value_name, "COMBO") ||
        !EqualsNoCase(value_name, "COMBOMODE")) {
      // Ignore
    }

    else if (from_cache && !EqualsNoCase(value_name, "RADAR")) {
      StepsType st = (StepsType)StringToInt(params[1]);
      CourseDifficulty cd = (CourseDifficulty)StringToInt(params[2]);

      RadarValues rv;
      rv.FromString(params[3]);
      out.radar_cache_[Course::CacheEntry(st, cd)] = rv;
    } else if (EqualsNoCase(value_name, "STYLE")) {
      std::string s_styles = params[1];
      std::vector<std::string> as_styles;
      split(s_styles, ",", as_styles);
      for (const std::string& s : as_styles) {
        out.set_styles_.insert(s);
      }

    } else {
      LOG->UserLog(
          "Course file", path, "contains an unexpected value named \"%s\"",
          value_name.c_str());
    }
  }

  if (out.banner_path_.empty()) {
    const std::string f_name = SetExtension(out.path_, "");

    std::vector<std::string> array_possible_banners;
    GetDirListing(f_name + "*.png", array_possible_banners, false, false);
    GetDirListing(f_name + "*.jpg", array_possible_banners, false, false);
    GetDirListing(f_name + "*.jpeg", array_possible_banners, false, false);
    GetDirListing(f_name + "*.bmp", array_possible_banners, false, false);
    GetDirListing(f_name + "*.gif", array_possible_banners, false, false);
    if (!array_possible_banners.empty()) {
      out.banner_path_ = array_possible_banners[0];
    }
  }

  static TitleSubst tsub("Courses");

  TitleFields title;
  title.title_ = out.main_title_;
  title.title_translit_ = out.main_title_translit_;
  tsub.Subst(title);
  out.main_title_ = title.title_;
  out.main_title_translit_ = title.title_translit_;

  /* Cache and load the course banner. Only bother doing this if at least one
   * song was found in the course. */
  if (out.entries_.empty()) {
    return true;
  }
  if (out.banner_path_ != "") {
    IMAGECACHE->CacheImage("Banner", out.GetBannerPath());
  }

  /* Cache each trail RadarValues that's slow to load, so we
   * don't have to do it at runtime. */
  if (!from_cache) {
    out.CalculateRadarValues();
  }

  return true;
}

bool CourseLoaderCRS::LoadFromCRSFile(const std::string& s_path, Course& out) {
  std::string sPath = s_path;

  out.Init();

  out.path_ = sPath;  // save path

  // save group name
  {
    std::vector<std::string> parts;
    split(sPath, "/", parts, false);
    if (parts.size() >= 4) {  // e.g. "/Courses/blah/fun.crs"
      out.group_name_ = parts[parts.size() - 2];
    }
  }

  bool use_cache = true;
  {
    /* First look in the cache for this course. Don't bother honoring
     * FastLoad for checking the course hash, since courses are normally
     * grouped into a few directories, not one directory per course. */
    unsigned hash = SONGINDEX->GetCacheHash(out.path_);
    if (!DoesFileExist(out.GetCacheFilePath())) {
      use_cache = false;
    }
    // XXX: if !FastLoad, regen cache if the used songs have changed
    if (!PREFSMAN->fast_load_ && GetHashForFile(out.path_) != hash) {
      use_cache = false;  // this cache is out of date
    }
  }

  if (use_cache) {
    std::string cache_file = out.GetCacheFilePath();
    LOG->Trace(
        "CourseLoaderCRS::LoadFromCRSFile(\"%s\") (\"%s\")", sPath.c_str(),
        cache_file.c_str());
    sPath = cache_file;
  } else {
    LOG->Trace("CourseLoaderCRS::LoadFromCRSFile(\"%s\")", sPath.c_str());
  }

  MsdFile msd;
  if (!msd.ReadFile(
          sPath, false))  // don't unescape here, it gets handled later
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
    if (out.radar_cache_.size()) {
      std::string cache_path = out.GetCacheFilePath();
      if (CourseWriterCRS::Write(out, cache_path, true)) {
        SONGINDEX->AddCacheIndex(out.path_, GetHashForFile(out.path_));
      }
    }
  }

  return true;
}

bool CourseLoaderCRS::LoadEditFromFile(
    const std::string& edit_file_path, ProfileSlot slot) {
  LOG->Trace("CourseLoaderCRS::LoadEdit(%s)", edit_file_path.c_str());

  int bytes = FILEMAN->GetFileSizeInBytes(edit_file_path);
  if (bytes > MAX_EDIT_COURSE_SIZE_BYTES) {
    LOG->UserLog(
        "Edit file", edit_file_path,
        "is unreasonably large. It won't be loaded.");
    return false;
  }

  MsdFile msd;
  if (!msd.ReadFile(
          edit_file_path, false))  // don't unescape here, it gets handled later
  {
    LOG->UserLog(
        "Edit file", edit_file_path, "couldn't be opened: %s",
        msd.GetError().c_str());
    return false;
  }
  Course* course = new Course;

  course->path_ = edit_file_path;
  LoadFromMsd(edit_file_path, msd, *course, true);

  course->loaded_from_profile_ = slot;

  SONGMAN->AddCourse(course);
  return true;
}

bool CourseLoaderCRS::ParseCourseMods(
    const MsdFile::value_t& params, AttackArray& attacks,
    const std::string& path) {
  Attack attack;
  float end = -9999;
  for (unsigned j = 1; j < params.params_.size(); ++j) {
    std::vector<std::string> bits;
    split(params[j], "=", bits, false);
    if (bits.size() < 2) {
      continue;
    }

    Trim(bits[0]);
    if (!CompareNoCase(bits[0], "TIME")) {
      attack.start_second_ = std::max(StringToFloat(bits[1]), 0.0f);
    } else if (!CompareNoCase(bits[0], "LEN")) {
      attack.secs_remaining_ = StringToFloat(bits[1]);
    } else if (!CompareNoCase(bits[0], "END")) {
      end = StringToFloat(bits[1]);
    } else if (!CompareNoCase(bits[0], "MODS")) {
      attack.modifiers_ = bits[1];

      if (end != -9999) {
        attack.secs_remaining_ = end - attack.start_second_;
        end = -9999;
      }

      if (attack.secs_remaining_ <= 0.0f) {
        LOG->UserLog(
            "Course file", path, "has an attack with a nonpositive length: %s",
            bits[1].c_str());
        attack.secs_remaining_ = 0.0f;
      }

      // warn on invalid so we catch typos on load
      CourseUtil::WarnOnInvalidMods(attack.modifiers_);

      attacks.push_back(attack);
    } else {
      LOG->UserLog(
          "Course file", path, "has an unexpected value named '%s'",
          bits[0].c_str());
    }
  }
  return true;
}

bool CourseLoaderCRS::ParseCourseSong(
    const MsdFile::value_t& params, CourseEntry& new_entry,
    const std::string& path) {
  // infer entry::Type from the first param
  // todo: make sure these aren't generating bogus entries due
  // to a lack of songs. -aj
  int num_songs = SONGMAN->GetNumSongs();
  // most played
  if (Left(params[1], strlen("BEST")) == "BEST") {
    int choose_index =
        StringToInt(Right(params[1], params[1].size() - strlen("BEST"))) - 1;
    if (choose_index > num_songs) {
      // looking up a song that doesn't exist.
      LOG->UserLog(
          "Course file", path,
          "is trying to load BEST%i with only %i songs installed. "
          "This entry will be ignored.",
          choose_index, num_songs);
      return false;  // skip this #SONG
    }

    new_entry.choose_index_ = choose_index;
    rage_clamp(new_entry.choose_index_, 0, 500);
    new_entry.song_sort_ = SongSort_MostPlays;
  }
  // least played
  else if (Left(params[1], strlen("WORST")) == "WORST") {
    int choose_index =
        StringToInt(Right(params[1], params[1].size() - strlen("WORST"))) - 1;
    if (choose_index > num_songs) {
      // looking up a song that doesn't exist.
      LOG->UserLog(
          "Course file", path,
          "is trying to load WORST%i with only %i songs installed. "
          "This entry will be ignored.",
          choose_index, num_songs);
      return false;  // skip this #SONG
    }

    new_entry.choose_index_ = choose_index;
    rage_clamp(new_entry.choose_index_, 0, 500);
    new_entry.song_sort_ = SongSort_FewestPlays;
  }
  // best grades
  else if (Left(params[1], strlen("GRADEBEST")) == "GRADEBEST") {
    new_entry.choose_index_ =
        StringToInt(Right(params[1], params[1].size() - strlen("GRADEBEST"))) -
        1;
    rage_clamp(new_entry.choose_index_, 0, 500);
    new_entry.song_sort_ = SongSort_TopGrades;
  }
  // worst grades
  else if (Left(params[1], strlen("GRADEWORST")) == "GRADEWORST") {
    new_entry.choose_index_ =
        StringToInt(Right(params[1], params[1].size() - strlen("GRADEWORST"))) -
        1;
    rage_clamp(new_entry.choose_index_, 0, 500);
    new_entry.song_sort_ = SongSort_LowestGrades;
  } else if (params[1] == "*") {
    new_entry.secret_ = true;
  }
  // group random
  else if (Right(params[1], 1) == "*") {
    new_entry.secret_ = true;
    std::string s_song = params[1];
    Replace(s_song, "\\", "/");
    std::vector<std::string> bits;
    split(s_song, "/", bits);
    if (bits.size() == 2) {
      new_entry.song_criteria_.group_names_.push_back(bits[0]);
    } else {
      LOG->UserLog(
          "Course file", path,
          "contains a random_within_group entry \"%s\" that is invalid. "
          "Song should be in the format \"<group>/*\".",
          s_song.c_str());
    }

    if (!SONGMAN->DoesSongGroupExist(bits[0])) {
      LOG->UserLog(
          "Course file", path,
          "random_within_group entry \"%s\" specifies a group that doesn't "
          "exist. "
          "This entry will be ignored.",
          s_song.c_str());
      return false;  // skip this #SONG
    }
  } else {
    std::string s_song = params[1];
    Replace(s_song, "\\", "/");
    std::vector<std::string> bits;
    split(s_song, "/", bits);

    Song* p_song = nullptr;
    if (bits.size() == 2) {
      new_entry.song_criteria_.group_names_.push_back(bits[0]);
      p_song = SONGMAN->FindSong(bits[0], bits[1]);
    } else if (bits.size() == 1) {
      p_song = SONGMAN->FindSong("", s_song);
    }
    new_entry.song_id_.FromSong(p_song);

    if (p_song == nullptr) {
      LOG->UserLog(
          "Course file", path,
          "contains a fixed song entry \"%s\" that does not exist. "
          "This entry will be ignored.",
          s_song.c_str());
      return false;  // skip this #SONG
    }
  }

  new_entry.steps_criteria_.difficulty_ = OldStyleStringToDifficulty(params[2]);
  // most CRS files use old-style difficulties, but Difficulty enum values can
  // be used in SM5. Test for those too.
  if (new_entry.steps_criteria_.difficulty_ == Difficulty_Invalid) {
    new_entry.steps_criteria_.difficulty_ = StringToDifficulty(params[2]);
  }

  if (new_entry.steps_criteria_.difficulty_ == Difficulty_Invalid) {
    int retval = sscanf(
        params[2].c_str(), "%d..%d", &new_entry.steps_criteria_.low_meter_,
        &new_entry.steps_criteria_.high_meter_);
    if (retval == 1) {
      new_entry.steps_criteria_.high_meter_ =
          new_entry.steps_criteria_.low_meter_;
    } else if (retval != 2) {
      LOG->UserLog(
          "Course file", path,
          "contains an invalid difficulty setting: \"%s\", 3..6 used instead",
          params[2].c_str());
      new_entry.steps_criteria_.low_meter_ = 3;
      new_entry.steps_criteria_.high_meter_ = 6;
    }
    new_entry.steps_criteria_.low_meter_ =
        std::max(new_entry.steps_criteria_.low_meter_, 1);
    new_entry.steps_criteria_.high_meter_ = std::max(
        new_entry.steps_criteria_.high_meter_,
        new_entry.steps_criteria_.low_meter_);
  }

  {
    // If "showcourse" or "noshowcourse" is in the list, force
    // new_entry.secret on or off.
    std::vector<std::string> mods;
    split(params[3], ",", mods, true);
    for (int j = (int)mods.size() - 1; j >= 0; --j) {
      std::string& mod = mods[j];
      TrimLeft(mod);
      TrimRight(mod);
      if (!CompareNoCase(mod, "showcourse")) {
        new_entry.secret_ = false;
      } else if (!CompareNoCase(mod, "noshowcourse")) {
        new_entry.secret_ = true;
      } else if (!CompareNoCase(mod, "nodifficult")) {
        new_entry.no_difficult_ = true;
      } else if (mod.length() > 5 && !CompareNoCase(Left(mod, 5), "award")) {
        new_entry.gain_lives_ = StringToInt(mod.substr(5));
      } else {
        continue;
      }
      mods.erase(mods.begin() + j);
    }
    new_entry.modifiers_ = join(",", mods);
  }
  return true;
}

bool CourseLoaderCRS::ParseCourseSongSelect(
    const MsdFile::value_t& params, CourseEntry& new_entry,
    const std::string& path) {
  for (unsigned i = 1; i < params.params_.size(); ++i) {
    std::vector<std::string> param_parts;
    split_minding_escaped_delims(params[i], "=", param_parts);

    if (param_parts.size() != 2) {
      LOG->UserLog(
          "Course file", path,
          "has an invalid SONGSELECT sub-parameter, \"%s\"", params[i].c_str());
      return false;
    }
    std::string param_name = param_parts[0];
    std::string param_value = param_parts[1];

    // For params that accept multiple items, if someone were to define it twice
    // in one #SONGSELECT, should we overwrite the first, or append? Currently,
    // it just appends it all together.

    if (EqualsNoCase(param_name, "TITLE")) {
      if (ParseCommaSeparatedList(
              param_value, new_entry.song_criteria_.song_names_, param_name,
              path) == false) {
        return false;
      }
    } else if (EqualsNoCase(param_name, "GROUP")) {
      if (ParseCommaSeparatedList(
              param_value, new_entry.song_criteria_.group_names_, param_name,
              path) == false) {
        return false;
      }
    } else if (EqualsNoCase(param_name, "ARTIST")) {
      if (ParseCommaSeparatedList(
              param_value, new_entry.song_criteria_.artist_names_, param_name,
              path) == false) {
        return false;
      }
    } else if (EqualsNoCase(param_name, "GENRE")) {
      if (ParseCommaSeparatedList(
              param_value, new_entry.song_criteria_.song_genre_allowed_list_,
              param_name, path) == false) {
        return false;
      }
      new_entry.song_criteria_.use_song_genre_allowed_list_ = true;
    } else if (EqualsNoCase(param_name, "DIFFICULTY")) {
      std::vector<std::string> difficulty_strs;
      std::vector<Difficulty> difficulties;
      split(param_value, ",", difficulty_strs);
      for (unsigned d = 0; d < difficulty_strs.size(); d++) {
        Difficulty diff = OldStyleStringToDifficulty(difficulty_strs[d]);
        // most CRS files use old-style difficulties, but Difficulty enum values
        // can be used in SM5. Test for those too.
        if (diff == Difficulty_Invalid) {
          diff = StringToDifficulty(difficulty_strs[d]);
        }
        if (diff != Difficulty_Invalid) {
          difficulties.push_back(diff);
        } else {
          LOG->UserLog(
              "Course file", path,
              "has an invalid DIFFICULTY sub-parameter, \"%s\"",
              difficulty_strs[d].c_str());
        }
      }
      new_entry.steps_criteria_.difficulties_.insert(
          new_entry.steps_criteria_.difficulties_.end(), difficulties.begin(),
          difficulties.end());
    } else if (EqualsNoCase(param_name, "SORT")) {
      std::vector<std::string> sort_params;
      split(param_value, ",", sort_params);
      if (sort_params.size() != 2) {
        LOG->UserLog(
            "Course file", path, "has an invalid SORT parameter, \"%s\"",
            params[i].c_str());
        return false;
      }

      SongSort sort = StringToSongSort(sort_params[0]);
      if (sort == SongSort_Invalid) {
        sort = OldStyleStringToSongSort(sort_params[0]);
      }
      if (sort == SongSort_Invalid) {
        LOG->UserLog(
            "Course file", path, "has an invalid SORT parameter, \"%s\"",
            params[i].c_str());
        return false;
      }
      int index = StringToInt(sort_params[1]) - 1;

      if (CourseLoaderCRS::SetCourseSongSort(new_entry, sort, index, path) ==
          false) {
        return false;
      }
    } else if (EqualsNoCase(param_name, "DURATION")) {
      if (ParseRangedValue(
              param_value, new_entry.song_criteria_.min_duration_seconds_,
              new_entry.song_criteria_.max_duration_seconds_, param_name,
              path) == false) {
        return false;
      }
    } else if (EqualsNoCase(param_name, "BPMRANGE")) {
      if (ParseRangedValue(
              param_value, new_entry.song_criteria_.min_bpm_,
              new_entry.song_criteria_.max_bpm_, param_name, path) == false) {
        return false;
      }
    } else if (EqualsNoCase(param_name, "METER")) {
      if (ParseRangedValue(
              param_value, new_entry.steps_criteria_.low_meter_,
              new_entry.steps_criteria_.high_meter_, param_name,
              path) == false) {
        return false;
      }
    } else if (EqualsNoCase(param_name, "GAINLIVES")) {
      new_entry.gain_lives_ = StringToInt(param_value);
    } else if (EqualsNoCase(param_name, "GAINSECONDS")) {
      new_entry.gain_seconds_ = StringToInt(param_value);
    } else if (EqualsNoCase(param_name, "MODS")) {
      std::vector<std::string> mods;
      split(param_value, ",", mods, true);
      for (int j = (int)mods.size() - 1; j >= 0; --j) {
        std::string& mod = mods[j];
        TrimLeft(mod);
        TrimRight(mod);
        if (!CompareNoCase(mod, "showcourse")) {
          new_entry.secret_ = false;
        } else if (!CompareNoCase(mod, "noshowcourse")) {
          new_entry.secret_ = true;
        } else if (!CompareNoCase(mod, "nodifficult")) {
          new_entry.no_difficult_ = true;
        } else {
          continue;
        }
        mods.erase(mods.begin() + j);
      }
      new_entry.modifiers_ = join(",", mods);
    } else {
      LOG->UserLog(
          "Course file", path,
          "has an unexpected parameter named '%s', ignoring.",
          param_name.c_str());
    }
  }
  return true;
}

bool CourseLoaderCRS::ParseCommaSeparatedList(
    const std::string& param_value, std::vector<std::string>& dest,
    const std::string& param_name, const std::string& path) {
  std::vector<std::string> items;
  split_minding_escaped_delims(param_value, ",", items);
  if (items.size() == 0) {
    LOG->UserLog(
        "Course file", path,
        "has an invalid %s parameter, expected at least one sub-parameter, but "
        "found none.",
        param_name.c_str());
    return false;
  }
  for (unsigned long i = 0; i < items.size(); i++) {
    items[i] = SmUnescape(items[i]);
    Trim(items[i]);
  }
  dest.insert(dest.end(), items.begin(), items.end());
  return true;
}

template <typename T>
bool CourseLoaderCRS::ParseRangedValue(
    const std::string& param_value, T& min_value, T& max_value,
    const std::string& param_name, const std::string& path) {
  std::vector<std::string> values;
  split(param_value, "-", values);
  if (values.size() == 0) {
    LOG->UserLog(
        "Course file", path,
        "has an invalid %s parameter, expected at least one value, but found "
        "none.",
        param_name.c_str());
    return false;
  } else if (values.size() > 2) {
    LOG->UserLog(
        "Course file", path,
        "has an invalid %s parameter, expected at most 2, value, but found "
        "%zu.",
        param_name.c_str(), values.size());
    return false;
  }
  float first_val = StringToFloat(values.front());
  float last_val = StringToFloat(values.back());
  if (first_val > last_val) {
    LOG->UserLog(
        "Course file", path,
        "has an invalid %s parameter value '%s': first value %f should be less "
        "than %f ",
        param_name.c_str(), param_value.c_str(), first_val, last_val);
    return false;
  }
  min_value = static_cast<T>(first_val);
  max_value = static_cast<T>(last_val);
  return true;
}

bool CourseLoaderCRS::SetCourseSongSort(
    CourseEntry& new_entry, SongSort sort, int index, const std::string& path) {
  if (sort == SongSort_Invalid) {
    LOG->UserLog(
        "Course file", path,
        "is trying to explicitly set a SongSort value of SongSort_Invalid.");
    return false;
  }
  if (sort == SongSort_Randomize) {
    new_entry.song_sort_ = sort;
    return true;
  }

  int num_songs = SONGMAN->GetNumSongs();
  int choose_index = index;
  if (choose_index > num_songs &&
      (sort == SongSort_MostPlays || sort == SongSort_FewestPlays)) {
    LOG->UserLog(
        "Course file", path,
        "is trying to load %s%i with only %i songs installed. "
        "This entry will be ignored.",
        SongSortToString(sort).c_str(), choose_index, num_songs);
    return false;  // skip this #SONG
  }

  rage_clamp(choose_index, 0, 500);
  new_entry.choose_index_ = choose_index;
  new_entry.song_sort_ = sort;
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
