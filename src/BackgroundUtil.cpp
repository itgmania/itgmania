#include "BackgroundUtil.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "ActorUtil.h"
#include "Background.h"
#include "IniFile.h"
#include "RageFileManager.h"
#include "RageLog.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "Song.h"
#include "StdString.h"
#include "XmlFile.h"
#include "global.h"

bool BackgroundDef::operator<(const BackgroundDef& other) const {
  if (effect_ != other.effect_) {
    return effect_ < other.effect_;
  }
  if (file1_ != other.file1_) {
    return file1_ < other.file1_;
  }
  if (file2_ != other.file2_) {
    return file2_ < other.file2_;
  }
  if (color1_ != other.color1_) {
    return color1_ < other.color1_;
  }
  if (color2_ != other.color2_) {
    return color2_ < other.color2_;
  }
  return false;
}

bool BackgroundDef::operator==(const BackgroundDef& other) const {
  return effect_ == other.effect_ && file1_ == other.file1_ &&
         file2_ == other.file2_ && color1_ == other.color1_ &&
         color2_ == other.color2_;
}

XNode* BackgroundDef::CreateNode() const {
  XNode* node = new XNode("BackgroundDef");

  if (!effect_.empty()) {
    node->AppendAttr("Effect", effect_);
  }
  if (!file1_.empty()) {
    node->AppendAttr("File1", file1_);
  }
  if (!file2_.empty()) {
    node->AppendAttr("File2", file2_);
  }
  if (!color1_.empty()) {
    node->AppendAttr("Color1", color1_);
  }
  if (!color2_.empty()) {
    node->AppendAttr("Color2", color2_);
  }

  return node;
}

std::string BackgroundChange::GetTextDescription() const {
  std::vector<std::string> parts;
  if (!def_.file1_.empty()) {
    parts.push_back(def_.file1_);
  }
  if (!def_.file2_.empty()) {
    parts.push_back(def_.file2_);
  }
  if (rate_ != 1.0f) {
    parts.push_back(ssprintf("%.2f%%", rate_ * 100));
  }
  if (!transition_.empty()) {
    parts.push_back(transition_);
  }
  if (!def_.effect_.empty()) {
    parts.push_back(def_.effect_);
  }
  if (!def_.color1_.empty()) {
    parts.push_back(def_.color1_);
  }
  if (!def_.color2_.empty()) {
    parts.push_back(def_.color2_);
  }

  if (parts.empty()) {
    parts.push_back("(empty)");
  }

  std::string s = join("\n", parts);
  return s;
}

std::string BackgroundChange::ToString() const {
  /* TODO:  Technically we need to double-escape the filename
   * (because it might contain '=') and then unescape the value
   * returned by the MsdFile. */
  return ssprintf(
      "%.3f=%s=%.3f=%d=%d=%d=%s=%s=%s=%s=%s", this->start_beat_,
      SmEscape(this->def_.file1_).c_str(), this->rate_,
      this->transition_ == kSbtCrossFade,       // backward compat
      this->def_.effect_ == kSbeStretchRewind,  // backward compat
      this->def_.effect_ != kSbeStretchNoLoop,  // backward compat
      this->def_.effect_.c_str(), this->def_.file2_.c_str(),
      this->transition_.c_str(),
      SmEscape(RageColor::NormalizeColorString(this->def_.color1_)).c_str(),
      SmEscape(RageColor::NormalizeColorString(this->def_.color2_)).c_str());
}

const std::string kBackgroundEffectsDir = "BackgroundEffects/";
const std::string kBackgroundTransitionsDir = "BackgroundTransitions/";
const std::string kBgAnimsDir = "BGAnimations/";
const std::string kRandommoviesDir = "RandomMovies/";
const std::string kSongMoviesDir = "SongMovies/";

const std::string kRandomBackgroundFile = "-random-";
const std::string kNoSongBgFile = "-nosongbg-";
const std::string kSongBackgroundFile = "songbackground";

const std::string kSbeUpperLeft = "UpperLeft";
const std::string kSbeCentered = "Centered";
const std::string kSbeStretchNormal = "StretchNormal";
const std::string kSbeStretchNoLoop = "StretchNoLoop";
const std::string kSbeStretchRewind = "StretchRewind";
const std::string kSbtCrossFade = "CrossFade";

static void StripCvsAndSvn(
    std::vector<std::string>& paths_to_strip,
    std::vector<std::string>& names_to_strip) {
  ASSERT(paths_to_strip.size() == names_to_strip.size());
  for (unsigned i = 0; i < names_to_strip.size(); i++) {
    if (CompareNoCase(Right(names_to_strip[i], 3), "CVS") == 0 ||
        names_to_strip[i] == ".svn") {
      paths_to_strip.erase(paths_to_strip.begin() + i);
      names_to_strip.erase(names_to_strip.begin() + i);
    }
  }
}

int CompareBackgroundChanges(
    const BackgroundChange& seg1, const BackgroundChange& seg2) {
  return seg1.start_beat_ < seg2.start_beat_;
}

void BackgroundUtil::SortBackgroundChangesArray(
    std::vector<BackgroundChange>& background_changes) {
  sort(
      background_changes.begin(), background_changes.end(),
      CompareBackgroundChanges);
}

void BackgroundUtil::AddBackgroundChange(
    std::vector<BackgroundChange>& background_changes, BackgroundChange seg) {
  std::vector<BackgroundChange>::iterator it;
  it = upper_bound(
      background_changes.begin(), background_changes.end(), seg,
      CompareBackgroundChanges);
  background_changes.insert(it, seg);
}

void BackgroundUtil::GetBackgroundEffects(
    const std::string& s_name, std::vector<std::string>& paths_out,
    std::vector<std::string>& names_out) {
  std::string sName = s_name;
  if (sName == "") {
    sName = "*";
  }

  paths_out.clear();
  GetDirListing(kBackgroundEffectsDir + sName + ".lua", paths_out, false, true);

  names_out.clear();
  for (const std::string& s : paths_out) {
    names_out.push_back(GetFileNameWithoutExtension(s));
  }

  StripCvsAndSvn(paths_out, names_out);
}

void BackgroundUtil::GetBackgroundTransitions(
    const std::string& s_name, std::vector<std::string>& paths_out,
    std::vector<std::string>& names_out) {
  std::string sName = s_name;
  if (sName == "") {
    sName = "*";
  }

  paths_out.clear();
  if (true) {
    GetDirListing(
        kBackgroundTransitionsDir + sName + ".xml", paths_out, false, true);
  }
  GetDirListing(
      kBackgroundTransitionsDir + sName + ".lua", paths_out, false, true);

  names_out.clear();
  for (const std::string& s : paths_out) {
    names_out.push_back(GetFileNameWithoutExtension(s));
  }

  StripCvsAndSvn(paths_out, names_out);
}

void BackgroundUtil::GetSongBGAnimations(
    const Song* song, const std::string& match,
    std::vector<std::string>& paths_out, std::vector<std::string>& names_out) {
  paths_out.clear();
  if (match.empty()) {
    GetDirListing(song->GetSongDir() + "*", paths_out, true, true);
  } else {
    GetDirListing(song->GetSongDir() + match, paths_out, true, true);
  }

  names_out.clear();
  for (const std::string& s : paths_out) {
    names_out.push_back(Basename(s));
  }

  StripCvsAndSvn(paths_out, names_out);
}

void BackgroundUtil::GetSongMovies(
    const Song* song, const std::string& match,
    std::vector<std::string>& paths_out, std::vector<std::string>& names_out) {
  paths_out.clear();
  if (match.empty()) {
    FILEMAN->GetDirListingWithMultipleExtensions(
        song->GetSongDir() + match, ActorUtil::GetTypeExtensionList(FT_Movie),
        paths_out, false, true);
  } else {
    GetDirListing(song->GetSongDir() + match, paths_out, false, true);
  }

  names_out.clear();
  for (const std::string& s : paths_out) {
    names_out.push_back(Basename(s));
  }

  StripCvsAndSvn(paths_out, names_out);
}

void BackgroundUtil::GetSongBitmaps(
    const Song* song, const std::string& match,
    std::vector<std::string>& paths_out, std::vector<std::string>& names_out) {
  paths_out.clear();
  if (match.empty()) {
    FILEMAN->GetDirListingWithMultipleExtensions(
        song->GetSongDir() + match, ActorUtil::GetTypeExtensionList(FT_Bitmap),
        paths_out, false, true);
  } else {
    GetDirListing(song->GetSongDir() + match, paths_out, false, true);
  }

  names_out.clear();
  for (const std::string& s : paths_out) {
    names_out.push_back(Basename(s));
  }

  StripCvsAndSvn(paths_out, names_out);
}

static void GetFilterToFileNames(
    const std::string base_dir, const Song* song,
    std::set<std::string>& possible_file_names_out) {
  possible_file_names_out.clear();

  if (song->genre_.empty()) {
    return;
  }

  ASSERT(!song->group_name_.empty());
  IniFile ini;
  std::string path =
      base_dir + song->group_name_ + "/" + "BackgroundMapping.ini";
  ini.ReadFile(path);

  std::string s_section;
  bool success = ini.GetValue("GenreToSection", song->genre_, s_section);
  if (!success) {
    // LOG->Warn( "Genre '%s' isn't mapped", pSong->m_sGenre.c_str() );
    return;
  }

  XNode* p_section = ini.GetChild(s_section);
  if (p_section == nullptr) {
    ASSERT_M(
        0, ssprintf(
               "File '%s' refers to a section '%s' that is missing.",
               path.c_str(), s_section.c_str()));
    return;
  }

  FOREACH_CONST_Attr(p_section, p) possible_file_names_out.insert(p->first);
}

void BackgroundUtil::GetGlobalBGAnimations(
    const Song* song, const std::string& match,
    std::vector<std::string>& paths_out, std::vector<std::string>& names_out) {
  paths_out.clear();
  GetDirListing(kBgAnimsDir + match + "*", paths_out, true, true);
  if (true) {
    GetDirListing(kBgAnimsDir + match + "*.xml", paths_out, false, true);
  }

  names_out.clear();
  for (const std::string& s : paths_out) {
    names_out.push_back(Basename(s));
  }

  StripCvsAndSvn(paths_out, names_out);
}

namespace {
/**
 * @brief Fetches the appropriate path(s) for global random movies.
 */
void GetGlobalRandomMoviePaths(
    const Song* song, const std::string& match,
    std::vector<std::string>& paths_out,
    bool try_inside_of_song_group_and_genre_first,
    bool try_inside_of_song_group_first) {
  // Check for an exact match
  if (!match.empty()) {
    GetDirListing(
        kSongMoviesDir + song->group_name_ + "/" + match, paths_out, false,
        true);  // search in SongMovies/SongGroupName/ first
    GetDirListing(kSongMoviesDir + match, paths_out, false, true);
    GetDirListing(kRandommoviesDir + match, paths_out, false, true);
    if (paths_out.empty() && match != kNoSongBgFile) {
      LOG->Warn("Background missing: %s", match.c_str());
    }
    return;
  }

  // Search for the most appropriate background
  std::set<std::string> file_name_whitelist;
  if (try_inside_of_song_group_and_genre_first && song &&
      !song->genre_.empty()) {
    GetFilterToFileNames(kRandommoviesDir, song, file_name_whitelist);
  }

  std::vector<std::string> dirs_to_try;
  if (try_inside_of_song_group_first && song) {
    ASSERT(!song->group_name_.empty());
    dirs_to_try.push_back(kRandommoviesDir + song->group_name_ + "/");
  }
  dirs_to_try.push_back(kRandommoviesDir);

  for (const std::string& dir : dirs_to_try) {
    paths_out.clear();
    GetDirListing(dir + "*", paths_out, false, true);

    size_t write_index = 0;
    for (size_t bytes_read = 0; bytes_read < paths_out.size(); ++bytes_read) {
      if (ActorUtil::GetFileType(paths_out[bytes_read]) == FT_Movie) {
        if (write_index != bytes_read) {
          paths_out[write_index] = paths_out[bytes_read];
        }
        ++write_index;
      }
    }
    paths_out.resize(write_index);

    if (!file_name_whitelist.empty()) {
      std::vector<std::string> matches;
      for (const std::string& s : paths_out) {
        std::string basename = Basename(s);
        bool found =
            file_name_whitelist.find(basename) != file_name_whitelist.end();
        if (found) {
          matches.push_back(s);
        }
      }
      // If we found any that match the whitelist, use only them.
      // If none match the whitelist, ignore the whitelist..
      if (!matches.empty()) {
        paths_out = matches;
      }
    }

    if (!paths_out.empty()) {
      // Return only the first directory found
      return;
    }
  }
}

}  // namespace

void BackgroundUtil::GetGlobalRandomMovies(
    const Song* song, const std::string& match,
    std::vector<std::string>& paths_out, std::vector<std::string>& names_out,
    bool try_inside_of_song_group_and_genre_first,
    bool try_inside_of_song_group_first) {
  paths_out.clear();
  names_out.clear();

  GetGlobalRandomMoviePaths(
      song, match, paths_out, try_inside_of_song_group_and_genre_first,
      try_inside_of_song_group_first);

  for (const std::string& s : paths_out) {
    std::string name = Right(s, s.size() - kRandommoviesDir.size() - 1);
    names_out.push_back(name);
  }
  StripCvsAndSvn(paths_out, names_out);
}

/*
 * (c) 2001-2004 Chris Danford, Ben Nordstrom
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
