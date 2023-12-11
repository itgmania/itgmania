#include "global.h"

#include "BackgroundUtil.h"

#include <set>
#include <vector>

#include "ActorUtil.h"
#include "Background.h"
#include "IniFile.h"
#include "RageFileManager.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "Song.h"

bool BackgroundDef::operator<(const BackgroundDef& other) const {
#define COMPARE(x)          \
  if (x < other.x) {        \
    return true;            \
  } else if (x > other.x) { \
    return false;           \
  }
  COMPARE(effect_);
  COMPARE(file1_);
  COMPARE(file2_);
  COMPARE(color1_);
  COMPARE(color2_);
#undef COMPARE
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

RString BackgroundChange::GetTextDescription() const {
  std::vector<RString> parts;
  if (!background_def_.file1_.empty()) {
    parts.push_back(background_def_.file1_);
  }
  if (!background_def_.file2_.empty()) {
    parts.push_back(background_def_.file2_);
  }
  if (rate_ != 1.0f) {
    parts.push_back(ssprintf("%.2f%%", rate_ * 100));
  }
  if (!transition_.empty()) {
    parts.push_back(transition_);
  }
  if (!background_def_.effect_.empty()) {
    parts.push_back(background_def_.effect_);
  }
  if (!background_def_.color1_.empty()) {
    parts.push_back(background_def_.color1_);
  }
  if (!background_def_.color2_.empty()) {
    parts.push_back(background_def_.color2_);
  }

  if (parts.empty()) {
    parts.push_back("(empty)");
  }

  RString s = join("\n", parts);
  return s;
}

RString BackgroundChange::ToString() const {
  // TODO: Technically we need to double-escape the filename (because it might
  // contain '=') and then unescape the value returned by the MsdFile.
  return ssprintf(
      "%.3f=%s=%.3f=%d=%d=%d=%s=%s=%s=%s=%s", start_beat_,
      SmEscape(background_def_.file1_).c_str(), rate_,
      transition_ == SBT_CrossFade,                    // backward compat
      background_def_.effect_ == SBE_StretchRewind,  // backward compat
      background_def_.effect_ != SBE_StretchNoLoop,  // backward compat
      background_def_.effect_.c_str(), background_def_.file2_.c_str(),
      transition_.c_str(),
      SmEscape(RageColor::NormalizeColorString(background_def_.color1_))
          .c_str(),
      SmEscape(RageColor::NormalizeColorString(background_def_.color2_))
          .c_str());
}

const RString BACKGROUND_EFFECTS_DIR = "BackgroundEffects/";
const RString BACKGROUND_TRANSITIONS_DIR = "BackgroundTransitions/";
const RString BG_ANIMS_DIR = "BGAnimations/";
const RString VISUALIZATIONS_DIR = "Visualizations/";
const RString RANDOMMOVIES_DIR = "RandomMovies/";
const RString SONG_MOVIES_DIR = "SongMovies/";

const RString RANDOM_BACKGROUND_FILE = "-random-";
const RString NO_SONG_BG_FILE = "-nosongbg-";
const RString SONG_BACKGROUND_FILE = "songbackground";

const RString SBE_UpperLeft = "UpperLeft";
const RString SBE_Centered = "Centered";
const RString SBE_StretchNormal = "StretchNormal";
const RString SBE_StretchNoLoop = "StretchNoLoop";
const RString SBE_StretchRewind = "StretchRewind";
const RString SBT_CrossFade = "CrossFade";

static void StripCvsAndSvn(
    std::vector<RString>& paths_to_strip,
    std::vector<RString>& names_to_strip) {
  ASSERT(paths_to_strip.size() == names_to_strip.size());
  for (unsigned i = 0; i < names_to_strip.size(); ++i) {
    if (names_to_strip[i].Right(3).CompareNoCase("CVS") == 0 ||
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
  std::sort(
      background_changes.begin(), background_changes.end(),
      CompareBackgroundChanges);
}

void BackgroundUtil::AddBackgroundChange(
    std::vector<BackgroundChange>& background_changes, BackgroundChange seg) {
  std::vector<BackgroundChange>::iterator it;
  it = std::upper_bound(
      background_changes.begin(), background_changes.end(), seg,
      CompareBackgroundChanges);
  background_changes.insert(it, seg);
}

void BackgroundUtil::GetBackgroundEffects(
    const RString& _name, std::vector<RString>& paths_out,
    std::vector<RString>& names_out) {
  RString name = _name;
  if (name == "") {
    name = "*";
  }

  paths_out.clear();
  GetDirListing(BACKGROUND_EFFECTS_DIR + name + ".lua", paths_out, false, true);

  names_out.clear();
  for (const RString& s : paths_out) {
    names_out.push_back(GetFileNameWithoutExtension(s));
  }

  StripCvsAndSvn(paths_out, names_out);
}

void BackgroundUtil::GetBackgroundTransitions(
    const RString& _name, std::vector<RString>& paths_out,
    std::vector<RString>& names_out) {
  RString name = _name;
  if (name == "") {
    name = "*";
  }

  paths_out.clear();
  GetDirListing(
      BACKGROUND_TRANSITIONS_DIR + name + ".xml", paths_out, false, true);
  GetDirListing(
      BACKGROUND_TRANSITIONS_DIR + name + ".lua", paths_out, false, true);

  names_out.clear();
  for (const RString& s : paths_out) {
    names_out.push_back(GetFileNameWithoutExtension(s));
  }

  StripCvsAndSvn(paths_out, names_out);
}

void BackgroundUtil::GetSongBGAnimations(
    const Song* song, const RString& match, std::vector<RString>& paths_out,
    std::vector<RString>& names_out) {
  paths_out.clear();
  if (match.empty()) {
    GetDirListing(song->GetSongDir() + "*", paths_out, true, true);
  } else {
    GetDirListing(song->GetSongDir() + match, paths_out, true, true);
  }

  names_out.clear();
  for (const RString& s : paths_out) {
    names_out.push_back(Basename(s));
  }

  StripCvsAndSvn(paths_out, names_out);
}

void BackgroundUtil::GetSongMovies(
    const Song* song, const RString& match, std::vector<RString>& paths_out,
    std::vector<RString>& names_out) {
  paths_out.clear();
  if (match.empty()) {
    FILEMAN->GetDirListingWithMultipleExtensions(
        song->GetSongDir() + match, ActorUtil::GetTypeExtensionList(FT_Movie),
        paths_out, false, true);
  } else {
    GetDirListing(song->GetSongDir() + match, paths_out, false, true);
  }

  names_out.clear();
  for (const RString& s : paths_out) {
    names_out.push_back(Basename(s));
  }

  StripCvsAndSvn(paths_out, names_out);
}

void BackgroundUtil::GetSongBitmaps(
    const Song* song, const RString& match, std::vector<RString>& paths_out,
    std::vector<RString>& names_out) {
  paths_out.clear();
  if (match.empty()) {
    FILEMAN->GetDirListingWithMultipleExtensions(
        song->GetSongDir() + match, ActorUtil::GetTypeExtensionList(FT_Bitmap),
        paths_out, false, true);
  } else {
    GetDirListing(song->GetSongDir() + match, paths_out, false, true);
  }

  names_out.clear();
  for (const RString& s : paths_out) {
    names_out.push_back(Basename(s));
  }

  StripCvsAndSvn(paths_out, names_out);
}

static void GetFilterToFileNames(
    const RString base_dir, const Song* song,
    std::set<RString>& possible_file_names_out) {
  possible_file_names_out.clear();

  if (song->m_sGenre.empty()) {
    return;
  }

  ASSERT(!song->m_sGroupName.empty());
  IniFile ini;
  RString path = base_dir + song->m_sGroupName + "/" + "BackgroundMapping.ini";
  ini.ReadFile(path);

  RString section;
  bool success = ini.GetValue("GenreToSection", song->m_sGenre, section);
  if (!success) {
    return;
  }

  XNode* section_node = ini.GetChild(section);
  if (section_node == nullptr) {
    ASSERT_M(
        0, ssprintf(
               "File '%s' refers to a section '%s' that is missing.",
               path.c_str(), section.c_str()));
    return;
  }

  FOREACH_CONST_Attr(section_node, p) {
    possible_file_names_out.insert(p->first);
  }
}

void BackgroundUtil::GetGlobalBGAnimations(
    const Song* song, const RString& match, std::vector<RString>& paths_out,
    std::vector<RString>& names_out) {
  paths_out.clear();
  GetDirListing(BG_ANIMS_DIR + match + "*", paths_out, true, true);
  if (true) {
    GetDirListing(BG_ANIMS_DIR + match + "*.xml", paths_out, false, true);
  }

  names_out.clear();
  for (const RString& s : paths_out) {
    names_out.push_back(Basename(s));
  }

  StripCvsAndSvn(paths_out, names_out);
}

namespace {

// Fetches the appropriate path(s) for global random movies.
void GetGlobalRandomMoviePaths(
    const Song* song, const RString& match, std::vector<RString>& paths_out,
    bool try_inside_of_song_group_and_genre_first,
    bool try_inside_of_song_group_first) {
  // Check for an exact match
  if (!match.empty()) {
    GetDirListing(
        SONG_MOVIES_DIR + song->m_sGroupName + "/" + match, paths_out, false,
        true);  // search in SongMovies/SongGroupName/ first
    GetDirListing(SONG_MOVIES_DIR + match, paths_out, false, true);
    GetDirListing(RANDOMMOVIES_DIR + match, paths_out, false, true);
    if (paths_out.empty() && match != NO_SONG_BG_FILE) {
      LOG->Warn("Background missing: %s", match.c_str());
    }
    return;
  }

  // Search for the most appropriate background
  std::set<RString> file_name_allow_list;
  if (try_inside_of_song_group_and_genre_first && song &&
      !song->m_sGenre.empty()) {
    GetFilterToFileNames(RANDOMMOVIES_DIR, song, file_name_allow_list);
  }

  std::vector<RString> dirs_to_try;
  if (try_inside_of_song_group_first && song) {
    ASSERT(!song->m_sGroupName.empty());
    dirs_to_try.push_back(RANDOMMOVIES_DIR + song->m_sGroupName + "/");
  }
  dirs_to_try.push_back(RANDOMMOVIES_DIR);

  for (const RString& dir : dirs_to_try) {
    GetDirListing(dir + "*.ogv", paths_out, false, true);
    GetDirListing(dir + "*.avi", paths_out, false, true);
    GetDirListing(dir + "*.mpg", paths_out, false, true);
    GetDirListing(dir + "*.mpeg", paths_out, false, true);

    if (!file_name_allow_list.empty()) {
      std::vector<RString> matches;
      for (const RString& s : paths_out) {
        RString sBasename = Basename(s);
        bool found =
            file_name_allow_list.find(sBasename) != file_name_allow_list.end();
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
    const Song* song, const RString& match, std::vector<RString>& paths_out,
    std::vector<RString>& names_out,
    bool try_inside_of_song_group_and_genre_first,
    bool try_inside_of_song_group_first) {
  paths_out.clear();
  names_out.clear();

  GetGlobalRandomMoviePaths(
      song, match, paths_out, try_inside_of_song_group_and_genre_first,
      try_inside_of_song_group_first);

  for (const RString& s : paths_out) {
    RString sName = s.Right(s.size() - RANDOMMOVIES_DIR.size() - 1);
    names_out.push_back(sName);
  }
  StripCvsAndSvn(paths_out, names_out);
}

void BackgroundUtil::BakeAllBackgroundChanges(Song* song) {
  Background bg;
  bg.LoadFromSong(song);
  std::vector<BackgroundChange>* bg_changes[NUM_BackgroundLayer];
  FOREACH_BackgroundLayer(i) { bg_changes[i] = &song->GetBackgroundChanges(i); }
  bg.GetLoadedBackgroundChanges(bg_changes);
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
