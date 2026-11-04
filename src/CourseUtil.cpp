#include "CourseUtil.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "Course.h"
#include "CourseWriterCRS.h"
#include "Difficulty.h"
#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "GameState.h"
#include "LocalizedString.h"
#include "PlayerNumber.h"
#include "Profile.h"
#include "ProfileManager.h"
#include "RageFileManager.h"
#include "RageLog.h"
#include "RageTimer.h"
#include "RageUtil.h"
#include "RageUtil/RandomNumbers.h"
#include "SongManager.h"
#include "SongOptions.h"
#include "StdString.h"
#include "Style.h"
#include "ThemeManager.h"
#include "XmlFile.h"
#include "arch/Dialog/Dialog.h"
#include "global.h"

// Sorting stuff
static bool CompareCoursePointersByName(
    const Course* pCourse1, const Course* pCourse2) {
  std::string sName1 = pCourse1->GetDisplayFullTitle();
  std::string sName2 = pCourse2->GetDisplayFullTitle();
  return CompareNoCase(sName1, sName2) < 0;
}

static bool CompareCoursePointersByAutogen(
    const Course* pCourse1, const Course* pCourse2) {
  int b1 = pCourse1->is_autogen_;
  int b2 = pCourse2->is_autogen_;
  if (b1 < b2) {
    return true;
  } else if (b1 > b2) {
    return false;
  } else {
    return CompareCoursePointersByName(pCourse1, pCourse2);
  }
}

static bool CompareCoursePointersByDifficulty(
    const Course* pCourse1, const Course* pCourse2) {
  int iNum1 = pCourse1->GetEstimatedNumStages();
  int iNum2 = pCourse2->GetEstimatedNumStages();
  if (iNum1 < iNum2) {
    return true;
  } else if (iNum1 > iNum2) {
    return false;
  } else {  // iNum1 == iNum2
    return CompareCoursePointersByAutogen(pCourse1, pCourse2);
  }
}

static bool CompareCoursePointersByTotalDifficulty(
    const Course* pCourse1, const Course* pCourse2) {
  int iNum1 = pCourse1->sort_order_total_difficulty_;
  int iNum2 = pCourse2->sort_order_total_difficulty_;

  if (iNum1 == iNum2) {
    return CompareCoursePointersByAutogen(pCourse1, pCourse2);
  }
  return iNum1 < iNum2;
}

static bool MovePlayersBestToEnd(
    const Course* pCourse1, const Course* pCourse2) {
  bool C1HasBest = pCourse1->CourseHasBestOrWorst();
  bool C2HasBest = pCourse2->CourseHasBestOrWorst();
  if (!C1HasBest && !C2HasBest) {
    return false;
  }
  if (C1HasBest && !C2HasBest) {
    return false;
  }
  if (!C1HasBest && C2HasBest) {
    return true;
  }

  return CompareCoursePointersByName(pCourse1, pCourse2);
}

static bool CompareRandom(const Course* pCourse1, const Course* pCourse2) {
  return (pCourse1->AllSongsAreFixed() && !pCourse2->AllSongsAreFixed());
}

static bool CompareCoursePointersByRanking(
    const Course* pCourse1, const Course* pCourse2) {
  int iNum1 = pCourse1->sort_order_ranking_;
  int iNum2 = pCourse2->sort_order_ranking_;

  if (iNum1 == iNum2) {
    return CompareCoursePointersByAutogen(pCourse1, pCourse2);
  }
  return iNum1 < iNum2;
}

void CourseUtil::SortCoursePointerArrayByDifficulty(
    std::vector<Course*>& vpCoursesInOut) {
  sort(
      vpCoursesInOut.begin(), vpCoursesInOut.end(),
      CompareCoursePointersByDifficulty);
}

void CourseUtil::SortCoursePointerArrayByRanking(
    std::vector<Course*>& vpCoursesInOut) {
  for (unsigned i = 0; i < vpCoursesInOut.size(); i++) {
    vpCoursesInOut[i]->UpdateCourseStats(
        GAMESTATE->GetCurrentStyle(PLAYER_INVALID)->steps_type_);
  }
  sort(
      vpCoursesInOut.begin(), vpCoursesInOut.end(),
      CompareCoursePointersByRanking);
}

void CourseUtil::SortCoursePointerArrayByTotalDifficulty(
    std::vector<Course*>& vpCoursesInOut) {
  for (unsigned i = 0; i < vpCoursesInOut.size(); i++) {
    vpCoursesInOut[i]->UpdateCourseStats(
        GAMESTATE->GetCurrentStyle(PLAYER_INVALID)->steps_type_);
  }
  sort(
      vpCoursesInOut.begin(), vpCoursesInOut.end(),
      CompareCoursePointersByTotalDifficulty);
}

static bool CompareCoursePointersByType(
    const Course* pCourse1, const Course* pCourse2) {
  return pCourse1->GetPlayMode() < pCourse2->GetPlayMode();
}

void CourseUtil::SortCoursePointerArrayByType(
    std::vector<Course*>& vpCoursesInOut) {
  stable_sort(
      vpCoursesInOut.begin(), vpCoursesInOut.end(),
      CompareCoursePointersByType);
}

void CourseUtil::MoveRandomToEnd(std::vector<Course*>& vpCoursesInOut) {
  stable_sort(vpCoursesInOut.begin(), vpCoursesInOut.end(), CompareRandom);
}

static std::map<const Course*, std::string> course_sort_val;

bool CompareCoursePointersBySortValueAscending(
    const Course* pSong1, const Course* pSong2) {
  return course_sort_val[pSong1] < course_sort_val[pSong2];
}

bool CompareCoursePointersBySortValueDescending(
    const Course* pSong1, const Course* pSong2) {
  return course_sort_val[pSong1] > course_sort_val[pSong2];
}

bool CompareCoursePointersByTitle(
    const Course* pCourse1, const Course* pCourse2) {
  return CompareCoursePointersByName(pCourse1, pCourse2);
}

void CourseUtil::SortCoursePointerArrayByTitle(
    std::vector<Course*>& vpCoursesInOut) {
  sort(
      vpCoursesInOut.begin(), vpCoursesInOut.end(),
      CompareCoursePointersByTitle);
}

void CourseUtil::SortCoursePointerArrayByAvgDifficulty(
    std::vector<Course*>& vpCoursesInOut) {
  RageTimer foo;
  course_sort_val.clear();
  for (unsigned i = 0; i < vpCoursesInOut.size(); ++i) {
    int iMeter = vpCoursesInOut[i]->GetMeter(
        GAMESTATE->GetCurrentStyle(PLAYER_INVALID)->steps_type_,
        Difficulty_Medium);
    course_sort_val[vpCoursesInOut[i]] = ssprintf("%06i", iMeter);
  }
  sort(
      vpCoursesInOut.begin(), vpCoursesInOut.end(),
      CompareCoursePointersByTitle);
  stable_sort(
      vpCoursesInOut.begin(), vpCoursesInOut.end(),
      CompareCoursePointersBySortValueAscending);

  stable_sort(
      vpCoursesInOut.begin(), vpCoursesInOut.end(), MovePlayersBestToEnd);
}

void CourseUtil::SortCoursePointerArrayByNumPlays(
    std::vector<Course*>& vpCoursesInOut, ProfileSlot slot, bool bDescending) {
  if (!PROFILEMAN->IsPersistentProfile(slot)) {
    return;  // nothing to do since we don't have data
  }
  Profile* pProfile = PROFILEMAN->GetProfile(slot);
  SortCoursePointerArrayByNumPlays(vpCoursesInOut, pProfile, bDescending);
}

void CourseUtil::SortCoursePointerArrayByNumPlays(
    std::vector<Course*>& vpCoursesInOut, const Profile* pProfile,
    bool bDescending) {
  ASSERT(pProfile != nullptr);
  for (unsigned i = 0; i < vpCoursesInOut.size(); ++i) {
    course_sort_val[vpCoursesInOut[i]] =
        ssprintf("%09i", pProfile->GetCourseNumTimesPlayed(vpCoursesInOut[i]));
  }
  stable_sort(
      vpCoursesInOut.begin(), vpCoursesInOut.end(),
      bDescending ? CompareCoursePointersBySortValueDescending
                  : CompareCoursePointersBySortValueAscending);
  course_sort_val.clear();
}

void CourseUtil::SortByMostRecentlyPlayedForMachine(
    std::vector<Course*>& vpCoursesInOut) {
  Profile* pProfile = PROFILEMAN->GetMachineProfile();

  for (const Course* c : vpCoursesInOut) {
    int iNumTimesPlayed = pProfile->GetCourseNumTimesPlayed(c);
    std::string val = iNumTimesPlayed
                          ? pProfile->GetCourseLastPlayedDateTime(c).GetString()
                          : std::string("9999999999999");
    course_sort_val[c] = val;
  }

  stable_sort(
      vpCoursesInOut.begin(), vpCoursesInOut.end(),
      CompareCoursePointersBySortValueAscending);
  course_sort_val.clear();
}

//////////////////////////////////
// Autogen
//////////////////////////////////

void CourseUtil::AutogenEndlessFromGroup(
    const std::string& sGroupName, Difficulty diff, Course& out) {
  out.is_autogen_ = true;
  out.repeat_ = true;
  out.shuffle_ = true;
  out.lives_ = -1;
  FOREACH_ENUM(Difficulty, dc)
  out.custom_meter_[dc] = -1;

  if (sGroupName == "") {
    out.main_title_ = "All Songs";
    // this sounds reasonable... -aj
    out.banner_path_ = THEME->GetPathG("Banner", "all music");
  } else {
    out.main_title_ = SONGMAN->ShortenGroupName(sGroupName);
    out.banner_path_ = SONGMAN->GetSongGroupBannerPath(sGroupName);
  }

  // We want multiple songs, so we can try to prevent repeats during
  // gameplay. (We might still get a repeat at the repeat boundary,
  // but that'd be rare.) -glenn
  CourseEntry e;
  e.song_criteria_.group_names_.push_back(sGroupName);
  e.steps_criteria_.difficulty_ = diff;
  e.secret_ = true;

  // Insert a copy of e for each song in the group.
  out.entries_.insert(
      out.entries_.end(), SONGMAN->GetSongs(sGroupName).size(), e);
}

void CourseUtil::AutogenNonstopFromGroup(
    const std::string& sGroupName, Difficulty diff, Course& out) {
  AutogenEndlessFromGroup(sGroupName, diff, out);

  out.repeat_ = false;

  out.main_title_ += " Random";

  // resize to 4
  while (out.entries_.size() < 4) {
    out.entries_.push_back(out.entries_[0]);
  }
  while (out.entries_.size() > 4) {
    out.entries_.pop_back();
  }
}

void CourseUtil::AutogenOniFromArtist(
    const std::string& sArtistName, std::string sArtistNameTranslit,
    std::vector<Song*> aSongs, Difficulty dc, Course& out) {
  out.is_autogen_ = true;
  out.repeat_ = false;
  out.shuffle_ = true;
  out.sort_by_meter_ = true;

  out.lives_ = 4;
  FOREACH_ENUM(Difficulty, cd)
  out.custom_meter_[cd] = -1;

  ASSERT(sArtistName != "");
  ASSERT(aSongs.size() > 0);

  /* "Artist Oni" is a little repetitive; "by Artist" stands out less, and
   * lowercasing "by" puts more emphasis on the artist's name. It also sorts
   * them together. */
  out.main_title_ = "by " + sArtistName;
  if (sArtistNameTranslit != sArtistName) {
    out.main_title_translit_ = "by " + sArtistNameTranslit;
  }

  // How would we handle Artist Oni course banners, anyways? -aj
  // m_sBannerPath = ""; // XXX

  /* Shuffle the list to determine which songs we'll use. Shuffle it
   * deterministically, so we always get the same set of songs unless the
   * song set changes. */
  {
    RandomGen rng(GetHashForString(sArtistName) + aSongs.size());
    std::shuffle(aSongs.begin(), aSongs.end(), rng);
  }

  // Only use up to four songs.
  if (aSongs.size() > 4) {
    aSongs.erase(aSongs.begin() + 4, aSongs.end());
  }

  CourseEntry e;
  e.steps_criteria_.difficulty_ = dc;

  for (unsigned i = 0; i < aSongs.size(); ++i) {
    e.song_id_.FromSong(aSongs[i]);
    out.entries_.push_back(e);
  }
}

void CourseUtil::WarnOnInvalidMods(std::string sMods) {
  PlayerOptions po;
  SongOptions so;
  std::vector<std::string> vs;
  split(sMods, ",", vs, true);
  for (const std::string& s : vs) {
    bool bValid = false;
    std::string sErrorDetail;
    bValid |= po.FromOneModString(s, sErrorDetail);
    bValid |= so.FromOneModString(s, sErrorDetail);
    /* ==Invalid options that used to be valid==
     * all noteskins (solo, note, foon, &c.)
     * protiming (done in Lua now)
     * ==Things I've seen in real course files==
     * 900% BRINK (damnit japan)
     * TISPY
     * 9000% OVERCOMING (what?)
     * 9200% TORNADE (it's like a grenade but a tornado)
     * 50% PROTIMING (HOW THE HELL DOES 50% PROTIMING EVEN WORK)
     * BREAK
     */
    if (!bValid) {
      std::string sFullError =
          ssprintf("Error processing '%s' in '%s'", s.c_str(), sMods.c_str());
      if (!sErrorDetail.empty()) {
        sFullError += ": " + sErrorDetail;
      }
      LOG->UserLog("", "", "%s", sFullError.c_str());
      Dialog::OK(sFullError, "INVALID_PLAYER_OPTION_WARNING");
    }
  }
}

int EditCourseUtil::MAX_NAME_LENGTH = 16;
int EditCourseUtil::MAX_PER_PROFILE = 32;
int EditCourseUtil::MIN_WORKOUT_MINUTES = 4;
int EditCourseUtil::MAX_WORKOUT_MINUTES = 90;
bool EditCourseUtil::s_bNewCourseNeedsName = false;

bool EditCourseUtil::Save(Course* pCourse) {
  return EditCourseUtil::RenameAndSave(pCourse, pCourse->GetDisplayFullTitle());
}

bool EditCourseUtil::RenameAndSave(Course* pCourse, std::string sNewName) {
  ASSERT(!sNewName.empty());

  EditCourseUtil::s_bNewCourseNeedsName = false;

  std::string sNewFilePath;
  if (pCourse->IsAnEdit()) {
    sNewFilePath = PROFILEMAN->GetProfileDir(ProfileSlot_Machine) +
                   EDIT_COURSES_SUBDIR + sNewName + ".crs";
  } else {
    std::string sDir, sName, sExt;
    splitpath(pCourse->path_, sDir, sName, sExt);
    sNewFilePath = sDir + sNewName + sExt;
  }

  // remove the old file if the name is changing
  if (!pCourse->path_.empty() && sNewFilePath != pCourse->path_) {
    FILEMAN->Remove(pCourse->path_);  // not fatal if this fails
  }

  pCourse->main_title_ = sNewName;
  pCourse->path_ = sNewFilePath;
  return CourseWriterCRS::Write(*pCourse, pCourse->path_, false);
}

bool EditCourseUtil::RemoveAndDeleteFile(Course* pCourse) {
  if (!FILEMAN->Remove(pCourse->path_)) {
    return false;
  }
  FILEMAN->Remove(pCourse->GetCacheFilePath());
  if (pCourse->IsAnEdit()) {
    PROFILEMAN->LoadMachineProfile();
  } else {
    SONGMAN->DeleteCourse(pCourse);
    delete pCourse;
  }
  return true;
}

static LocalizedString YOU_MUST_SUPPLY_NAME(
    "CourseUtil", "You must supply a name for your course.");
static LocalizedString EDIT_NAME_CONFLICTS(
    "CourseUtil",
    "The name you chose conflicts with another course. Please use a different "
    "name.");
static LocalizedString EDIT_NAME_CANNOT_CONTAIN(
    "CourseUtil",
    "The course name cannot contain any of the following characters: %s");
bool EditCourseUtil::ValidateEditCourseName(
    const std::string& sAnswer, std::string& sErrorOut) {
  if (sAnswer.empty()) {
    sErrorOut = YOU_MUST_SUPPLY_NAME;
    return false;
  }

  static const std::string sInvalidChars = "\\/:*?\"<>|";
  if (strpbrk(sAnswer.c_str(), sInvalidChars.c_str()) != nullptr) {
    sErrorOut = ssprintf(
        EDIT_NAME_CANNOT_CONTAIN.GetValue().c_str(), sInvalidChars.c_str());
    return false;
  }

  // Check for name conflicts
  std::vector<Course*> vpCourses;
  EditCourseUtil::GetAllEditCourses(vpCourses);
  for (const Course* p : vpCourses) {
    if (GAMESTATE->cur_course_ == p) {
      continue;  // don't comepare name against ourself
    }

    if (p->GetDisplayFullTitle() == sAnswer) {
      sErrorOut = EDIT_NAME_CONFLICTS;
      return false;
    }
  }

  return true;
}

void EditCourseUtil::UpdateAndSetTrail() {
  ASSERT(GAMESTATE->GetCurrentStyle(PLAYER_INVALID) != nullptr);
  StepsType st = GAMESTATE->GetCurrentStyle(PLAYER_INVALID)->steps_type_;
  Trail* pTrail = nullptr;
  if (GAMESTATE->cur_course_) {
    pTrail = GAMESTATE->cur_course_->GetTrailForceRegenCache(st);
  }
  GAMESTATE->cur_trail_[PLAYER_1].Set(pTrail);
}

void EditCourseUtil::PrepareForPlay() {
  GAMESTATE->cur_song_.Set(
      nullptr);  // CurSong will be set if we back out.  Set it back to nullptr
                 // so that ScreenStage won't show the last song.
  GAMESTATE->play_mode_.Set(PLAY_MODE_ENDLESS);
  GAMESTATE->side_is_joined_[0] = true;

  PROFILEMAN->GetProfile(ProfileSlot_Player1)->goal_type_ = GoalType_Time;
  Course* pCourse = GAMESTATE->cur_course_;
  PROFILEMAN->GetProfile(ProfileSlot_Player1)->goal_seconds_ =
      static_cast<int>(pCourse->goal_seconds_);
}

void EditCourseUtil::GetAllEditCourses(std::vector<Course*>& vpCoursesOut) {
  std::vector<Course*> vpCoursesTemp;
  SONGMAN->GetAllCourses(vpCoursesTemp, false);
  for (Course* c : vpCoursesTemp) {
    if (c->GetLoadedFromProfileSlot() != ProfileSlot_Invalid) {
      vpCoursesOut.push_back(c);
    }
  }
}

void EditCourseUtil::LoadDefaults(Course& out) {
  out = Course();

  out.goal_seconds_ = 0;

  // pick a default name
  // XXX: Make this localizable
  for (int i = 0; i < 10000; i++) {
    out.main_title_ = ssprintf("Workout %d", i + 1);

    std::vector<Course*> vpCourses;
    EditCourseUtil::GetAllEditCourses(vpCourses);

    if (std::any_of(vpCourses.begin(), vpCourses.end(), [&](const Course* p) {
          return out.main_title_ == p->main_title_;
        })) {
      break;
    }
  }

  std::vector<Song*> vpSongs;
  SONGMAN->GetPreferredSortSongs(vpSongs);
  for (int i = 0; i < (int)vpSongs.size() && i < 6; i++) {
    CourseEntry ce;
    ce.song_id_.FromSong(vpSongs[i]);
    ce.steps_criteria_.difficulty_ = Difficulty_Easy;
    out.entries_.push_back(ce);
  }
}

//////////////////////////////////
// CourseID
//////////////////////////////////

void CourseID::FromCourse(const Course* p) {
  if (p) {
    if (p->is_autogen_) {
      path_ = "";
      full_title_ = p->GetTranslitFullTitle();
    } else {
      path_ = p->path_;
      full_title_ = "";
    }
  } else {
    path_ = "";
    full_title_ = "";
  }

  // HACK for backwards compatibility:
  // Strip off leading "/".  2005/05/21 file layer changes added a leading
  // slash.
  if (Left(path_, 1) == "/") {
    path_.erase(path_.begin());
  }
}

Course* CourseID::ToCourse() const {
  Course* pCourse = nullptr;
  if (!path_.empty()) {
    // HACK for backwards compatibility:
    // Re-add the leading "/".  2005/05/21 file layer changes added a leading
    // slash.
    std::string slash_path = path_;
    if (Left(slash_path, 1) != "/") {
      slash_path = "/" + slash_path;
    }

    pCourse = SONGMAN->GetCourseFromPath(slash_path);
  }

  if (pCourse == nullptr && !full_title_.empty()) {
    pCourse = SONGMAN->GetCourseFromName(full_title_);
  }

  return pCourse;
}

XNode* CourseID::CreateNode() const {
  XNode* pNode = new XNode("Course");
  ;

  if (!path_.empty()) {
    pNode->AppendAttr("Path", path_);
  }
  if (!full_title_.empty()) {
    pNode->AppendAttr("FullTitle", full_title_);
  }

  return pNode;
}

void CourseID::LoadFromNode(const XNode* pNode) {
  ASSERT(pNode->GetName() == "Course");
  full_title_ = std::string();
  path_ = std::string();
  if (!pNode->GetAttrValue("Path", path_)) {
    pNode->GetAttrValue("FullTitle", full_title_);
  }

  // HACK for backwards compatibility: /AdditionalCourses has been merged into
  // /Courses
  if (Left(path_, 18) == "AdditionalCourses/") {
    path_.replace(0, 18, "Courses/");
  }
}

bool CourseID::IsValid() const {
  return !path_.empty() || !full_title_.empty();
}

/*
 * (c) 2001-2004 Chris Danford
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
