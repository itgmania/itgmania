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
  XNode* pNode = new XNode("BackgroundDef");

  if (!effect_.empty()) {
    pNode->AppendAttr("Effect", effect_);
  }
  if (!file1_.empty()) {
    pNode->AppendAttr("File1", file1_);
  }
  if (!file2_.empty()) {
    pNode->AppendAttr("File2", file2_);
  }
  if (!color1_.empty()) {
    pNode->AppendAttr("Color1", color1_);
  }
  if (!color2_.empty()) {
    pNode->AppendAttr("Color2", color2_);
  }

  return pNode;
}

std::string BackgroundChange::GetTextDescription() const {
  std::vector<std::string> vsParts;
  if (!def_.file1_.empty()) {
    vsParts.push_back(def_.file1_);
  }
  if (!def_.file2_.empty()) {
    vsParts.push_back(def_.file2_);
  }
  if (rate_ != 1.0f) {
    vsParts.push_back(ssprintf("%.2f%%", rate_ * 100));
  }
  if (!transition_.empty()) {
    vsParts.push_back(transition_);
  }
  if (!def_.effect_.empty()) {
    vsParts.push_back(def_.effect_);
  }
  if (!def_.color1_.empty()) {
    vsParts.push_back(def_.color1_);
  }
  if (!def_.color2_.empty()) {
    vsParts.push_back(def_.color2_);
  }

  if (vsParts.empty()) {
    vsParts.push_back("(empty)");
  }

  std::string s = join("\n", vsParts);
  return s;
}

std::string BackgroundChange::ToString() const {
  /* TODO:  Technically we need to double-escape the filename
   * (because it might contain '=') and then unescape the value
   * returned by the MsdFile. */
  return ssprintf(
      "%.3f=%s=%.3f=%d=%d=%d=%s=%s=%s=%s=%s", this->start_beat_,
      SmEscape(this->def_.file1_).c_str(), this->rate_,
      this->transition_ == SBT_CrossFade,       // backward compat
      this->def_.effect_ == SBE_StretchRewind,  // backward compat
      this->def_.effect_ != SBE_StretchNoLoop,  // backward compat
      this->def_.effect_.c_str(), this->def_.file2_.c_str(),
      this->transition_.c_str(),
      SmEscape(RageColor::NormalizeColorString(this->def_.color1_)).c_str(),
      SmEscape(RageColor::NormalizeColorString(this->def_.color2_)).c_str());
}

const std::string BACKGROUND_EFFECTS_DIR = "BackgroundEffects/";
const std::string BACKGROUND_TRANSITIONS_DIR = "BackgroundTransitions/";
const std::string BG_ANIMS_DIR = "BGAnimations/";
const std::string RANDOMMOVIES_DIR = "RandomMovies/";
const std::string SONG_MOVIES_DIR = "SongMovies/";

const std::string RANDOM_BACKGROUND_FILE = "-random-";
const std::string NO_SONG_BG_FILE = "-nosongbg-";
const std::string SONG_BACKGROUND_FILE = "songbackground";

const std::string SBE_UpperLeft = "UpperLeft";
const std::string SBE_Centered = "Centered";
const std::string SBE_StretchNormal = "StretchNormal";
const std::string SBE_StretchNoLoop = "StretchNoLoop";
const std::string SBE_StretchRewind = "StretchRewind";
const std::string SBT_CrossFade = "CrossFade";

static void StripCvsAndSvn(
    std::vector<std::string>& vsPathsToStrip,
    std::vector<std::string>& vsNamesToStrip) {
  ASSERT(vsPathsToStrip.size() == vsNamesToStrip.size());
  for (unsigned i = 0; i < vsNamesToStrip.size(); i++) {
    if (CompareNoCase(Right(vsNamesToStrip[i], 3), "CVS") == 0 ||
        vsNamesToStrip[i] == ".svn") {
      vsPathsToStrip.erase(vsPathsToStrip.begin() + i);
      vsNamesToStrip.erase(vsNamesToStrip.begin() + i);
    }
  }
}

int CompareBackgroundChanges(
    const BackgroundChange& seg1, const BackgroundChange& seg2) {
  return seg1.start_beat_ < seg2.start_beat_;
}

void BackgroundUtil::SortBackgroundChangesArray(
    std::vector<BackgroundChange>& vBackgroundChanges) {
  sort(
      vBackgroundChanges.begin(), vBackgroundChanges.end(),
      CompareBackgroundChanges);
}

void BackgroundUtil::AddBackgroundChange(
    std::vector<BackgroundChange>& vBackgroundChanges, BackgroundChange seg) {
  std::vector<BackgroundChange>::iterator it;
  it = upper_bound(
      vBackgroundChanges.begin(), vBackgroundChanges.end(), seg,
      CompareBackgroundChanges);
  vBackgroundChanges.insert(it, seg);
}

void BackgroundUtil::GetBackgroundEffects(
    const std::string& _sName, std::vector<std::string>& vsPathsOut,
    std::vector<std::string>& vsNamesOut) {
  std::string sName = _sName;
  if (sName == "") {
    sName = "*";
  }

  vsPathsOut.clear();
  GetDirListing(
      BACKGROUND_EFFECTS_DIR + sName + ".lua", vsPathsOut, false, true);

  vsNamesOut.clear();
  for (const std::string& s : vsPathsOut) {
    vsNamesOut.push_back(GetFileNameWithoutExtension(s));
  }

  StripCvsAndSvn(vsPathsOut, vsNamesOut);
}

void BackgroundUtil::GetBackgroundTransitions(
    const std::string& _sName, std::vector<std::string>& vsPathsOut,
    std::vector<std::string>& vsNamesOut) {
  std::string sName = _sName;
  if (sName == "") {
    sName = "*";
  }

  vsPathsOut.clear();
  if (true) {
    GetDirListing(
        BACKGROUND_TRANSITIONS_DIR + sName + ".xml", vsPathsOut, false, true);
  }
  GetDirListing(
      BACKGROUND_TRANSITIONS_DIR + sName + ".lua", vsPathsOut, false, true);

  vsNamesOut.clear();
  for (const std::string& s : vsPathsOut) {
    vsNamesOut.push_back(GetFileNameWithoutExtension(s));
  }

  StripCvsAndSvn(vsPathsOut, vsNamesOut);
}

void BackgroundUtil::GetSongBGAnimations(
    const Song* pSong, const std::string& sMatch,
    std::vector<std::string>& vsPathsOut,
    std::vector<std::string>& vsNamesOut) {
  vsPathsOut.clear();
  if (sMatch.empty()) {
    GetDirListing(pSong->GetSongDir() + "*", vsPathsOut, true, true);
  } else {
    GetDirListing(pSong->GetSongDir() + sMatch, vsPathsOut, true, true);
  }

  vsNamesOut.clear();
  for (const std::string& s : vsPathsOut) {
    vsNamesOut.push_back(Basename(s));
  }

  StripCvsAndSvn(vsPathsOut, vsNamesOut);
}

void BackgroundUtil::GetSongMovies(
    const Song* pSong, const std::string& sMatch,
    std::vector<std::string>& vsPathsOut,
    std::vector<std::string>& vsNamesOut) {
  vsPathsOut.clear();
  if (sMatch.empty()) {
    FILEMAN->GetDirListingWithMultipleExtensions(
        pSong->GetSongDir() + sMatch, ActorUtil::GetTypeExtensionList(FT_Movie),
        vsPathsOut, false, true);
  } else {
    GetDirListing(pSong->GetSongDir() + sMatch, vsPathsOut, false, true);
  }

  vsNamesOut.clear();
  for (const std::string& s : vsPathsOut) {
    vsNamesOut.push_back(Basename(s));
  }

  StripCvsAndSvn(vsPathsOut, vsNamesOut);
}

void BackgroundUtil::GetSongBitmaps(
    const Song* pSong, const std::string& sMatch,
    std::vector<std::string>& vsPathsOut,
    std::vector<std::string>& vsNamesOut) {
  vsPathsOut.clear();
  if (sMatch.empty()) {
    FILEMAN->GetDirListingWithMultipleExtensions(
        pSong->GetSongDir() + sMatch,
        ActorUtil::GetTypeExtensionList(FT_Bitmap), vsPathsOut, false, true);
  } else {
    GetDirListing(pSong->GetSongDir() + sMatch, vsPathsOut, false, true);
  }

  vsNamesOut.clear();
  for (const std::string& s : vsPathsOut) {
    vsNamesOut.push_back(Basename(s));
  }

  StripCvsAndSvn(vsPathsOut, vsNamesOut);
}

static void GetFilterToFileNames(
    const std::string sBaseDir, const Song* pSong,
    std::set<std::string>& vsPossibleFileNamesOut) {
  vsPossibleFileNamesOut.clear();

  if (pSong->genre_.empty()) {
    return;
  }

  ASSERT(!pSong->group_name_.empty());
  IniFile ini;
  std::string sPath =
      sBaseDir + pSong->group_name_ + "/" + "BackgroundMapping.ini";
  ini.ReadFile(sPath);

  std::string sSection;
  bool bSuccess = ini.GetValue("GenreToSection", pSong->genre_, sSection);
  if (!bSuccess) {
    // LOG->Warn( "Genre '%s' isn't mapped", pSong->m_sGenre.c_str() );
    return;
  }

  XNode* pSection = ini.GetChild(sSection);
  if (pSection == nullptr) {
    ASSERT_M(
        0, ssprintf(
               "File '%s' refers to a section '%s' that is missing.",
               sPath.c_str(), sSection.c_str()));
    return;
  }

  FOREACH_CONST_Attr(pSection, p) vsPossibleFileNamesOut.insert(p->first);
}

void BackgroundUtil::GetGlobalBGAnimations(
    const Song* pSong, const std::string& sMatch,
    std::vector<std::string>& vsPathsOut,
    std::vector<std::string>& vsNamesOut) {
  vsPathsOut.clear();
  GetDirListing(BG_ANIMS_DIR + sMatch + "*", vsPathsOut, true, true);
  if (true) {
    GetDirListing(BG_ANIMS_DIR + sMatch + "*.xml", vsPathsOut, false, true);
  }

  vsNamesOut.clear();
  for (const std::string& s : vsPathsOut) {
    vsNamesOut.push_back(Basename(s));
  }

  StripCvsAndSvn(vsPathsOut, vsNamesOut);
}

namespace {
/**
 * @brief Fetches the appropriate path(s) for global random movies.
 */
void GetGlobalRandomMoviePaths(
    const Song* pSong, const std::string& sMatch,
    std::vector<std::string>& vsPathsOut,
    bool bTryInsideOfSongGroupAndGenreFirst, bool bTryInsideOfSongGroupFirst) {
  // Check for an exact match
  if (!sMatch.empty()) {
    GetDirListing(
        SONG_MOVIES_DIR + pSong->group_name_ + "/" + sMatch, vsPathsOut, false,
        true);  // search in SongMovies/SongGroupName/ first
    GetDirListing(SONG_MOVIES_DIR + sMatch, vsPathsOut, false, true);
    GetDirListing(RANDOMMOVIES_DIR + sMatch, vsPathsOut, false, true);
    if (vsPathsOut.empty() && sMatch != NO_SONG_BG_FILE) {
      LOG->Warn("Background missing: %s", sMatch.c_str());
    }
    return;
  }

  // Search for the most appropriate background
  std::set<std::string> ssFileNameWhitelist;
  if (bTryInsideOfSongGroupAndGenreFirst && pSong && !pSong->genre_.empty()) {
    GetFilterToFileNames(RANDOMMOVIES_DIR, pSong, ssFileNameWhitelist);
  }

  std::vector<std::string> vsDirsToTry;
  if (bTryInsideOfSongGroupFirst && pSong) {
    ASSERT(!pSong->group_name_.empty());
    vsDirsToTry.push_back(RANDOMMOVIES_DIR + pSong->group_name_ + "/");
  }
  vsDirsToTry.push_back(RANDOMMOVIES_DIR);

  for (const std::string& sDir : vsDirsToTry) {
    vsPathsOut.clear();
    GetDirListing(sDir + "*", vsPathsOut, false, true);

    size_t writeIndex = 0;
    for (size_t bytesRead = 0; bytesRead < vsPathsOut.size(); ++bytesRead) {
      if (ActorUtil::GetFileType(vsPathsOut[bytesRead]) == FT_Movie) {
        if (writeIndex != bytesRead) {
          vsPathsOut[writeIndex] = vsPathsOut[bytesRead];
        }
        ++writeIndex;
      }
    }
    vsPathsOut.resize(writeIndex);

    if (!ssFileNameWhitelist.empty()) {
      std::vector<std::string> vsMatches;
      for (const std::string& s : vsPathsOut) {
        std::string sBasename = Basename(s);
        bool bFound =
            ssFileNameWhitelist.find(sBasename) != ssFileNameWhitelist.end();
        if (bFound) {
          vsMatches.push_back(s);
        }
      }
      // If we found any that match the whitelist, use only them.
      // If none match the whitelist, ignore the whitelist..
      if (!vsMatches.empty()) {
        vsPathsOut = vsMatches;
      }
    }

    if (!vsPathsOut.empty()) {
      // Return only the first directory found
      return;
    }
  }
}

}  // namespace

void BackgroundUtil::GetGlobalRandomMovies(
    const Song* pSong, const std::string& sMatch,
    std::vector<std::string>& vsPathsOut, std::vector<std::string>& vsNamesOut,
    bool bTryInsideOfSongGroupAndGenreFirst, bool bTryInsideOfSongGroupFirst) {
  vsPathsOut.clear();
  vsNamesOut.clear();

  GetGlobalRandomMoviePaths(
      pSong, sMatch, vsPathsOut, bTryInsideOfSongGroupAndGenreFirst,
      bTryInsideOfSongGroupFirst);

  for (const std::string& s : vsPathsOut) {
    std::string sName = Right(s, s.size() - RANDOMMOVIES_DIR.size() - 1);
    vsNamesOut.push_back(sName);
  }
  StripCvsAndSvn(vsPathsOut, vsNamesOut);
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
