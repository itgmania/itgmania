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
    const std::string& sSource, const std::string& sDelimitor,
    std::vector<std::string>& asAddit) {
  std::string sourceCopy = sSource;
  std::string escaped_delim = "||escaped-delim||";
  std::string regular_delim = "||regular-delim||";
  Replace(sourceCopy, "\\" + sDelimitor, escaped_delim);
  Replace(sourceCopy, sDelimitor, regular_delim);
  Replace(sourceCopy, escaped_delim, "\\" + sDelimitor);
  split(sourceCopy, regular_delim, asAddit);
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
    const std::string& sPath, const MsdFile& msd, Course& out,
    bool bFromCache) {
  AttackArray attacks;
  float fGainSeconds = 0;
  for (unsigned i = 0; i < msd.GetNumValues(); i++) {
    std::string sValueName = msd.GetParam(i, 0);
    const MsdFile::value_t& sParams = msd.GetValue(i);

    // handle the data
    if (EqualsNoCase(sValueName, "COURSE")) {
      out.main_title_ = sParams[1];
    } else if (EqualsNoCase(sValueName, "COURSETRANSLIT")) {
      out.main_title_translit_ = sParams[1];
    } else if (EqualsNoCase(sValueName, "SCRIPTER")) {
      out.scripter_ = sParams[1];
    } else if (EqualsNoCase(sValueName, "DESCRIPTION")) {
      out.description_ = sParams[1];
    } else if (EqualsNoCase(sValueName, "REPEAT")) {
      std::string str = sParams[1];
      MakeLower(str);
      if (str.find("yes") != std::string::npos) {
        out.repeat_ = true;
      }
    }

    else if (EqualsNoCase(sValueName, "BANNER")) {
      out.banner_path_ = sParams[1];
    } else if (EqualsNoCase(sValueName, "BACKGROUND")) {
      out.background_path_ = sParams[1];
    } else if (EqualsNoCase(sValueName, "LIVES")) {
      out.lives_ = std::max(StringToInt(sParams[1]), 0);
    } else if (EqualsNoCase(sValueName, "GAINSECONDS")) {
      fGainSeconds = StringToFloat(sParams[1]);
    } else if (EqualsNoCase(sValueName, "METER")) {
      if (sParams.params_.size() == 2) {
        out.custom_meter_[Difficulty_Medium] =
            std::max(StringToInt(sParams[1]), 0); /* compat */
      } else if (sParams.params_.size() == 3) {
        const CourseDifficulty cd = CRSStringToDifficulty(sParams[1]);
        if (cd == Difficulty_Invalid) {
          LOG->UserLog(
              "Course file", sPath, "contains an invalid #METER string: \"%s\"",
              sParams[1].c_str());
          continue;
        }
        out.custom_meter_[cd] = std::max(StringToInt(sParams[2]), 0);
      }
    }

    else if (EqualsNoCase(sValueName, "MODS")) {
      CourseLoaderCRS::ParseCourseMods(sParams, attacks, sPath);
    } else if (EqualsNoCase(sValueName, "SONG")) {
      CourseEntry new_entry;
      if (CourseLoaderCRS::ParseCourseSong(sParams, new_entry, sPath) ==
          false) {
        out.incomplete_ = true;
        continue;  // Skip this #SONG
      }

      new_entry.attacks_ = attacks;
      new_entry.gain_seconds_ = fGainSeconds;
      attacks.clear();
      out.entries_.push_back(new_entry);
    } else if (EqualsNoCase(sValueName, "SONGSELECT")) {
      CourseEntry new_entry;
      new_entry.use_song_select_ = true;
      if (CourseLoaderCRS::ParseCourseSongSelect(sParams, new_entry, sPath) ==
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
        !EqualsNoCase(sValueName, "DISPLAYCOURSE") ||
        !EqualsNoCase(sValueName, "COMBO") ||
        !EqualsNoCase(sValueName, "COMBOMODE")) {
      // Ignore
    }

    else if (bFromCache && !EqualsNoCase(sValueName, "RADAR")) {
      StepsType st = (StepsType)StringToInt(sParams[1]);
      CourseDifficulty cd = (CourseDifficulty)StringToInt(sParams[2]);

      RadarValues rv;
      rv.FromString(sParams[3]);
      out.radar_cache_[Course::CacheEntry(st, cd)] = rv;
    } else if (EqualsNoCase(sValueName, "STYLE")) {
      std::string sStyles = sParams[1];
      std::vector<std::string> asStyles;
      split(sStyles, ",", asStyles);
      for (const std::string& s : asStyles) {
        out.set_styles_.insert(s);
      }

    } else {
      LOG->UserLog(
          "Course file", sPath, "contains an unexpected value named \"%s\"",
          sValueName.c_str());
    }
  }

  if (out.banner_path_.empty()) {
    const std::string sFName = SetExtension(out.path_, "");

    std::vector<std::string> arrayPossibleBanners;
    GetDirListing(sFName + "*.png", arrayPossibleBanners, false, false);
    GetDirListing(sFName + "*.jpg", arrayPossibleBanners, false, false);
    GetDirListing(sFName + "*.jpeg", arrayPossibleBanners, false, false);
    GetDirListing(sFName + "*.bmp", arrayPossibleBanners, false, false);
    GetDirListing(sFName + "*.gif", arrayPossibleBanners, false, false);
    if (!arrayPossibleBanners.empty()) {
      out.banner_path_ = arrayPossibleBanners[0];
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
  if (!bFromCache) {
    out.CalculateRadarValues();
  }

  return true;
}

bool CourseLoaderCRS::LoadFromCRSFile(const std::string& _sPath, Course& out) {
  std::string sPath = _sPath;

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

  bool bUseCache = true;
  {
    /* First look in the cache for this course. Don't bother honoring
     * FastLoad for checking the course hash, since courses are normally
     * grouped into a few directories, not one directory per course. */
    unsigned uHash = SONGINDEX->GetCacheHash(out.path_);
    if (!DoesFileExist(out.GetCacheFilePath())) {
      bUseCache = false;
    }
    // XXX: if !FastLoad, regen cache if the used songs have changed
    if (!PREFSMAN->fast_load_ && GetHashForFile(out.path_) != uHash) {
      bUseCache = false;  // this cache is out of date
    }
  }

  if (bUseCache) {
    std::string sCacheFile = out.GetCacheFilePath();
    LOG->Trace(
        "CourseLoaderCRS::LoadFromCRSFile(\"%s\") (\"%s\")", sPath.c_str(),
        sCacheFile.c_str());
    sPath = sCacheFile;
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

  if (!LoadFromMsd(sPath, msd, out, bUseCache)) {
    return false;
  }

  if (!bUseCache) {
    // If we have any cache data, write the cache file.
    if (out.radar_cache_.size()) {
      std::string sCachePath = out.GetCacheFilePath();
      if (CourseWriterCRS::Write(out, sCachePath, true)) {
        SONGINDEX->AddCacheIndex(out.path_, GetHashForFile(out.path_));
      }
    }
  }

  return true;
}

bool CourseLoaderCRS::LoadEditFromFile(
    const std::string& sEditFilePath, ProfileSlot slot) {
  LOG->Trace("CourseLoaderCRS::LoadEdit(%s)", sEditFilePath.c_str());

  int iBytes = FILEMAN->GetFileSizeInBytes(sEditFilePath);
  if (iBytes > MAX_EDIT_COURSE_SIZE_BYTES) {
    LOG->UserLog(
        "Edit file", sEditFilePath,
        "is unreasonably large. It won't be loaded.");
    return false;
  }

  MsdFile msd;
  if (!msd.ReadFile(
          sEditFilePath, false))  // don't unescape here, it gets handled later
  {
    LOG->UserLog(
        "Edit file", sEditFilePath, "couldn't be opened: %s",
        msd.GetError().c_str());
    return false;
  }
  Course* pCourse = new Course;

  pCourse->path_ = sEditFilePath;
  LoadFromMsd(sEditFilePath, msd, *pCourse, true);

  pCourse->loaded_from_profile_ = slot;

  SONGMAN->AddCourse(pCourse);
  return true;
}

bool CourseLoaderCRS::ParseCourseMods(
    const MsdFile::value_t& sParams, AttackArray& attacks,
    const std::string& sPath) {
  Attack attack;
  float end = -9999;
  for (unsigned j = 1; j < sParams.params_.size(); ++j) {
    std::vector<std::string> sBits;
    split(sParams[j], "=", sBits, false);
    if (sBits.size() < 2) {
      continue;
    }

    Trim(sBits[0]);
    if (!CompareNoCase(sBits[0], "TIME")) {
      attack.start_second_ = std::max(StringToFloat(sBits[1]), 0.0f);
    } else if (!CompareNoCase(sBits[0], "LEN")) {
      attack.secs_remaining_ = StringToFloat(sBits[1]);
    } else if (!CompareNoCase(sBits[0], "END")) {
      end = StringToFloat(sBits[1]);
    } else if (!CompareNoCase(sBits[0], "MODS")) {
      attack.modifiers_ = sBits[1];

      if (end != -9999) {
        attack.secs_remaining_ = end - attack.start_second_;
        end = -9999;
      }

      if (attack.secs_remaining_ <= 0.0f) {
        LOG->UserLog(
            "Course file", sPath, "has an attack with a nonpositive length: %s",
            sBits[1].c_str());
        attack.secs_remaining_ = 0.0f;
      }

      // warn on invalid so we catch typos on load
      CourseUtil::WarnOnInvalidMods(attack.modifiers_);

      attacks.push_back(attack);
    } else {
      LOG->UserLog(
          "Course file", sPath, "has an unexpected value named '%s'",
          sBits[0].c_str());
    }
  }
  return true;
}

bool CourseLoaderCRS::ParseCourseSong(
    const MsdFile::value_t& sParams, CourseEntry& new_entry,
    const std::string& sPath) {
  // infer entry::Type from the first param
  // todo: make sure these aren't generating bogus entries due
  // to a lack of songs. -aj
  int iNumSongs = SONGMAN->GetNumSongs();
  // most played
  if (Left(sParams[1], strlen("BEST")) == "BEST") {
    int iChooseIndex =
        StringToInt(Right(sParams[1], sParams[1].size() - strlen("BEST"))) - 1;
    if (iChooseIndex > iNumSongs) {
      // looking up a song that doesn't exist.
      LOG->UserLog(
          "Course file", sPath,
          "is trying to load BEST%i with only %i songs installed. "
          "This entry will be ignored.",
          iChooseIndex, iNumSongs);
      return false;  // skip this #SONG
    }

    new_entry.choose_index_ = iChooseIndex;
    rage_clamp(new_entry.choose_index_, 0, 500);
    new_entry.song_sort_ = SongSort_MostPlays;
  }
  // least played
  else if (Left(sParams[1], strlen("WORST")) == "WORST") {
    int iChooseIndex =
        StringToInt(Right(sParams[1], sParams[1].size() - strlen("WORST"))) - 1;
    if (iChooseIndex > iNumSongs) {
      // looking up a song that doesn't exist.
      LOG->UserLog(
          "Course file", sPath,
          "is trying to load WORST%i with only %i songs installed. "
          "This entry will be ignored.",
          iChooseIndex, iNumSongs);
      return false;  // skip this #SONG
    }

    new_entry.choose_index_ = iChooseIndex;
    rage_clamp(new_entry.choose_index_, 0, 500);
    new_entry.song_sort_ = SongSort_FewestPlays;
  }
  // best grades
  else if (Left(sParams[1], strlen("GRADEBEST")) == "GRADEBEST") {
    new_entry.choose_index_ =
        StringToInt(
            Right(sParams[1], sParams[1].size() - strlen("GRADEBEST"))) -
        1;
    rage_clamp(new_entry.choose_index_, 0, 500);
    new_entry.song_sort_ = SongSort_TopGrades;
  }
  // worst grades
  else if (Left(sParams[1], strlen("GRADEWORST")) == "GRADEWORST") {
    new_entry.choose_index_ =
        StringToInt(
            Right(sParams[1], sParams[1].size() - strlen("GRADEWORST"))) -
        1;
    rage_clamp(new_entry.choose_index_, 0, 500);
    new_entry.song_sort_ = SongSort_LowestGrades;
  } else if (sParams[1] == "*") {
    new_entry.secret_ = true;
  }
  // group random
  else if (Right(sParams[1], 1) == "*") {
    new_entry.secret_ = true;
    std::string sSong = sParams[1];
    Replace(sSong, "\\", "/");
    std::vector<std::string> bits;
    split(sSong, "/", bits);
    if (bits.size() == 2) {
      new_entry.song_criteria_.group_names_.push_back(bits[0]);
    } else {
      LOG->UserLog(
          "Course file", sPath,
          "contains a random_within_group entry \"%s\" that is invalid. "
          "Song should be in the format \"<group>/*\".",
          sSong.c_str());
    }

    if (!SONGMAN->DoesSongGroupExist(bits[0])) {
      LOG->UserLog(
          "Course file", sPath,
          "random_within_group entry \"%s\" specifies a group that doesn't "
          "exist. "
          "This entry will be ignored.",
          sSong.c_str());
      return false;  // skip this #SONG
    }
  } else {
    std::string sSong = sParams[1];
    Replace(sSong, "\\", "/");
    std::vector<std::string> bits;
    split(sSong, "/", bits);

    Song* pSong = nullptr;
    if (bits.size() == 2) {
      new_entry.song_criteria_.group_names_.push_back(bits[0]);
      pSong = SONGMAN->FindSong(bits[0], bits[1]);
    } else if (bits.size() == 1) {
      pSong = SONGMAN->FindSong("", sSong);
    }
    new_entry.song_id_.FromSong(pSong);

    if (pSong == nullptr) {
      LOG->UserLog(
          "Course file", sPath,
          "contains a fixed song entry \"%s\" that does not exist. "
          "This entry will be ignored.",
          sSong.c_str());
      return false;  // skip this #SONG
    }
  }

  new_entry.steps_criteria_.difficulty_ =
      OldStyleStringToDifficulty(sParams[2]);
  // most CRS files use old-style difficulties, but Difficulty enum values can
  // be used in SM5. Test for those too.
  if (new_entry.steps_criteria_.difficulty_ == Difficulty_Invalid) {
    new_entry.steps_criteria_.difficulty_ = StringToDifficulty(sParams[2]);
  }

  if (new_entry.steps_criteria_.difficulty_ == Difficulty_Invalid) {
    int retval = sscanf(
        sParams[2].c_str(), "%d..%d", &new_entry.steps_criteria_.low_meter_,
        &new_entry.steps_criteria_.high_meter_);
    if (retval == 1) {
      new_entry.steps_criteria_.high_meter_ =
          new_entry.steps_criteria_.low_meter_;
    } else if (retval != 2) {
      LOG->UserLog(
          "Course file", sPath,
          "contains an invalid difficulty setting: \"%s\", 3..6 used instead",
          sParams[2].c_str());
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
    split(sParams[3], ",", mods, true);
    for (int j = (int)mods.size() - 1; j >= 0; --j) {
      std::string& sMod = mods[j];
      TrimLeft(sMod);
      TrimRight(sMod);
      if (!CompareNoCase(sMod, "showcourse")) {
        new_entry.secret_ = false;
      } else if (!CompareNoCase(sMod, "noshowcourse")) {
        new_entry.secret_ = true;
      } else if (!CompareNoCase(sMod, "nodifficult")) {
        new_entry.no_difficult_ = true;
      } else if (sMod.length() > 5 && !CompareNoCase(Left(sMod, 5), "award")) {
        new_entry.gain_lives_ = StringToInt(sMod.substr(5));
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
    const MsdFile::value_t& sParams, CourseEntry& new_entry,
    const std::string& sPath) {
  for (unsigned i = 1; i < sParams.params_.size(); ++i) {
    std::vector<std::string> sParamParts;
    split_minding_escaped_delims(sParams[i], "=", sParamParts);

    if (sParamParts.size() != 2) {
      LOG->UserLog(
          "Course file", sPath,
          "has an invalid SONGSELECT sub-parameter, \"%s\"",
          sParams[i].c_str());
      return false;
    }
    std::string sParamName = sParamParts[0];
    std::string sParamValue = sParamParts[1];

    // For params that accept multiple items, if someone were to define it twice
    // in one #SONGSELECT, should we overwrite the first, or append? Currently,
    // it just appends it all together.

    if (EqualsNoCase(sParamName, "TITLE")) {
      if (ParseCommaSeparatedList(
              sParamValue, new_entry.song_criteria_.song_names_, sParamName,
              sPath) == false) {
        return false;
      }
    } else if (EqualsNoCase(sParamName, "GROUP")) {
      if (ParseCommaSeparatedList(
              sParamValue, new_entry.song_criteria_.group_names_, sParamName,
              sPath) == false) {
        return false;
      }
    } else if (EqualsNoCase(sParamName, "ARTIST")) {
      if (ParseCommaSeparatedList(
              sParamValue, new_entry.song_criteria_.artist_names_, sParamName,
              sPath) == false) {
        return false;
      }
    } else if (EqualsNoCase(sParamName, "GENRE")) {
      if (ParseCommaSeparatedList(
              sParamValue, new_entry.song_criteria_.song_genre_allowed_list_,
              sParamName, sPath) == false) {
        return false;
      }
      new_entry.song_criteria_.use_song_genre_allowed_list_ = true;
    } else if (EqualsNoCase(sParamName, "DIFFICULTY")) {
      std::vector<std::string> difficultyStrs;
      std::vector<Difficulty> difficulties;
      split(sParamValue, ",", difficultyStrs);
      for (unsigned d = 0; d < difficultyStrs.size(); d++) {
        Difficulty diff = OldStyleStringToDifficulty(difficultyStrs[d]);
        // most CRS files use old-style difficulties, but Difficulty enum values
        // can be used in SM5. Test for those too.
        if (diff == Difficulty_Invalid) {
          diff = StringToDifficulty(difficultyStrs[d]);
        }
        if (diff != Difficulty_Invalid) {
          difficulties.push_back(diff);
        } else {
          LOG->UserLog(
              "Course file", sPath,
              "has an invalid DIFFICULTY sub-parameter, \"%s\"",
              difficultyStrs[d].c_str());
        }
      }
      new_entry.steps_criteria_.difficulties_.insert(
          new_entry.steps_criteria_.difficulties_.end(), difficulties.begin(),
          difficulties.end());
    } else if (EqualsNoCase(sParamName, "SORT")) {
      std::vector<std::string> sortParams;
      split(sParamValue, ",", sortParams);
      if (sortParams.size() != 2) {
        LOG->UserLog(
            "Course file", sPath, "has an invalid SORT parameter, \"%s\"",
            sParams[i].c_str());
        return false;
      }

      SongSort sort = StringToSongSort(sortParams[0]);
      if (sort == SongSort_Invalid) {
        sort = OldStyleStringToSongSort(sortParams[0]);
      }
      if (sort == SongSort_Invalid) {
        LOG->UserLog(
            "Course file", sPath, "has an invalid SORT parameter, \"%s\"",
            sParams[i].c_str());
        return false;
      }
      int index = StringToInt(sortParams[1]) - 1;

      if (CourseLoaderCRS::SetCourseSongSort(new_entry, sort, index, sPath) ==
          false) {
        return false;
      }
    } else if (EqualsNoCase(sParamName, "DURATION")) {
      if (ParseRangedValue(
              sParamValue, new_entry.song_criteria_.min_duration_seconds_,
              new_entry.song_criteria_.max_duration_seconds_, sParamName,
              sPath) == false) {
        return false;
      }
    } else if (EqualsNoCase(sParamName, "BPMRANGE")) {
      if (ParseRangedValue(
              sParamValue, new_entry.song_criteria_.min_bpm_,
              new_entry.song_criteria_.max_bpm_, sParamName, sPath) == false) {
        return false;
      }
    } else if (EqualsNoCase(sParamName, "METER")) {
      if (ParseRangedValue(
              sParamValue, new_entry.steps_criteria_.low_meter_,
              new_entry.steps_criteria_.high_meter_, sParamName,
              sPath) == false) {
        return false;
      }
    } else if (EqualsNoCase(sParamName, "GAINLIVES")) {
      new_entry.gain_lives_ = StringToInt(sParamValue);
    } else if (EqualsNoCase(sParamName, "GAINSECONDS")) {
      new_entry.gain_seconds_ = StringToInt(sParamValue);
    } else if (EqualsNoCase(sParamName, "MODS")) {
      std::vector<std::string> mods;
      split(sParamValue, ",", mods, true);
      for (int j = (int)mods.size() - 1; j >= 0; --j) {
        std::string& sMod = mods[j];
        TrimLeft(sMod);
        TrimRight(sMod);
        if (!CompareNoCase(sMod, "showcourse")) {
          new_entry.secret_ = false;
        } else if (!CompareNoCase(sMod, "noshowcourse")) {
          new_entry.secret_ = true;
        } else if (!CompareNoCase(sMod, "nodifficult")) {
          new_entry.no_difficult_ = true;
        } else {
          continue;
        }
        mods.erase(mods.begin() + j);
      }
      new_entry.modifiers_ = join(",", mods);
    } else {
      LOG->UserLog(
          "Course file", sPath,
          "has an unexpected parameter named '%s', ignoring.",
          sParamName.c_str());
    }
  }
  return true;
}

bool CourseLoaderCRS::ParseCommaSeparatedList(
    const std::string& sParamValue, std::vector<std::string>& dest,
    const std::string& sParamName, const std::string& sPath) {
  std::vector<std::string> items;
  split_minding_escaped_delims(sParamValue, ",", items);
  if (items.size() == 0) {
    LOG->UserLog(
        "Course file", sPath,
        "has an invalid %s parameter, expected at least one sub-parameter, but "
        "found none.",
        sParamName.c_str());
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
    const std::string& sParamValue, T& minValue, T& maxValue,
    const std::string& sParamName, const std::string& sPath) {
  std::vector<std::string> values;
  split(sParamValue, "-", values);
  if (values.size() == 0) {
    LOG->UserLog(
        "Course file", sPath,
        "has an invalid %s parameter, expected at least one value, but found "
        "none.",
        sParamName.c_str());
    return false;
  } else if (values.size() > 2) {
    LOG->UserLog(
        "Course file", sPath,
        "has an invalid %s parameter, expected at most 2, value, but found "
        "%zu.",
        sParamName.c_str(), values.size());
    return false;
  }
  float first_val = StringToFloat(values.front());
  float last_val = StringToFloat(values.back());
  if (first_val > last_val) {
    LOG->UserLog(
        "Course file", sPath,
        "has an invalid %s parameter value '%s': first value %f should be less "
        "than %f ",
        sParamName.c_str(), sParamValue.c_str(), first_val, last_val);
    return false;
  }
  minValue = static_cast<T>(first_val);
  maxValue = static_cast<T>(last_val);
  return true;
}

bool CourseLoaderCRS::SetCourseSongSort(
    CourseEntry& new_entry, SongSort sort, int index,
    const std::string& sPath) {
  if (sort == SongSort_Invalid) {
    LOG->UserLog(
        "Course file", sPath,
        "is trying to explicitly set a SongSort value of SongSort_Invalid.");
    return false;
  }
  if (sort == SongSort_Randomize) {
    new_entry.song_sort_ = sort;
    return true;
  }

  int iNumSongs = SONGMAN->GetNumSongs();
  int iChooseIndex = index;
  if (iChooseIndex > iNumSongs &&
      (sort == SongSort_MostPlays || sort == SongSort_FewestPlays)) {
    LOG->UserLog(
        "Course file", sPath,
        "is trying to load %s%i with only %i songs installed. "
        "This entry will be ignored.",
        SongSortToString(sort).c_str(), iChooseIndex, iNumSongs);
    return false;  // skip this #SONG
  }

  rage_clamp(iChooseIndex, 0, 500);
  new_entry.choose_index_ = iChooseIndex;
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
