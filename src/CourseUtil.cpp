#include "global.h"

#include "CourseUtil.h"

#include <vector>

#include "Course.h"
#include "CourseWriterCRS.h"
#include "GameState.h"
#include "LocalizedString.h"
#include "Profile.h"
#include "ProfileManager.h"
#include "RageFileManager.h"
#include "RageLog.h"
#include "RageTimer.h"
#include "SongManager.h"
#include "Style.h"
#include "XmlFile.h"
#include "arch/Dialog/Dialog.h"

// Sorting stuff
static bool CompareCoursePointersByName(
    const Course* course1, const Course* course2) {
  RString name1 = course1->GetDisplayFullTitle();
  RString name2 = course2->GetDisplayFullTitle();
  return name1.CompareNoCase(name2) < 0;
}

static bool CompareCoursePointersByAutogen(
    const Course* course1, const Course* course2) {
  int is_autogen1 = course1->m_bIsAutogen;
  int is_autogen2 = course2->m_bIsAutogen;
  if (is_autogen1 < is_autogen2) {
    return true;
  } else if (is_autogen1 > is_autogen2) {
    return false;
  } else {
    return CompareCoursePointersByName(course1, course2);
  }
}

static bool CompareCoursePointersByDifficulty(
    const Course* course1, const Course* course2) {
  int num_stages1 = course1->GetEstimatedNumStages();
  int num_stages2 = course2->GetEstimatedNumStages();
  if (num_stages1 < num_stages2) {
    return true;
  } else if (num_stages1 > num_stages2) {
    return false;
  } else {
    return CompareCoursePointersByAutogen(course1, course2);
  }
}

static bool CompareCoursePointersByTotalDifficulty(
    const Course* course1, const Course* course2) {
  int total_difficulty1 = course1->m_SortOrder_TotalDifficulty;
  int total_difficulty2 = course2->m_SortOrder_TotalDifficulty;

  if (total_difficulty1 == total_difficulty2) {
    return CompareCoursePointersByAutogen(course1, course2);
  }
  return total_difficulty1 < total_difficulty2;
}

static bool MovePlayersBestToEnd(const Course* course1, const Course* course2) {
  bool course1_has_best = course1->CourseHasBestOrWorst();
  bool course2_has_best = course2->CourseHasBestOrWorst();
  if (!course1_has_best && !course2_has_best) {
    return false;
  }
  if (course1_has_best && !course2_has_best) {
    return false;
  }
  if (!course1_has_best && course2_has_best) {
    return true;
  }

  return CompareCoursePointersByName(course1, course2);
}

static bool CompareRandom(const Course* course1, const Course* course2) {
  return (course1->AllSongsAreFixed() && !course2->AllSongsAreFixed());
}

static bool CompareCoursePointersByRanking(
    const Course* course1, const Course* course2) {
  int ranking1 = course1->m_SortOrder_Ranking;
  int ranking2 = course2->m_SortOrder_Ranking;

  if (ranking1 == ranking2) {
    return CompareCoursePointersByAutogen(course1, course2);
  }
  return ranking1 < ranking2;
}

void CourseUtil::SortCoursePointerArrayByDifficulty(
    std::vector<Course*>& courses) {
  sort(courses.begin(), courses.end(), CompareCoursePointersByDifficulty);
}

void CourseUtil::SortCoursePointerArrayByRanking(
    std::vector<Course*>& courses) {
  for (unsigned i = 0; i < courses.size(); i++) {
    courses[i]->UpdateCourseStats(
        GAMESTATE->GetCurrentStyle(PLAYER_INVALID)->m_StepsType);
  }
  sort(courses.begin(), courses.end(), CompareCoursePointersByRanking);
}

void CourseUtil::SortCoursePointerArrayByTotalDifficulty(
    std::vector<Course*>& courses) {
  for (unsigned i = 0; i < courses.size(); i++) {
    courses[i]->UpdateCourseStats(
        GAMESTATE->GetCurrentStyle(PLAYER_INVALID)->m_StepsType);
  }
  sort(courses.begin(), courses.end(), CompareCoursePointersByTotalDifficulty);
}

static bool CompareCoursePointersByType(
    const Course* course1, const Course* course2) {
  return course1->GetPlayMode() < course2->GetPlayMode();
}

void CourseUtil::SortCoursePointerArrayByType(std::vector<Course*>& courses) {
  stable_sort(courses.begin(), courses.end(), CompareCoursePointersByType);
}

void CourseUtil::MoveRandomToEnd(std::vector<Course*>& courses) {
  stable_sort(courses.begin(), courses.end(), CompareRandom);
}

static std::map<const Course*, RString> course_sort_val;

bool CompareCoursePointersBySortValueAscending(
    const Course* song1, const Course* song2) {
  return course_sort_val[song1] < course_sort_val[song2];
}

bool CompareCoursePointersBySortValueDescending(
    const Course* song1, const Course* song2) {
  return course_sort_val[song1] > course_sort_val[song2];
}

bool CompareCoursePointersByTitle(
    const Course* course1, const Course* course2) {
  return CompareCoursePointersByName(course1, course2);
}

void CourseUtil::SortCoursePointerArrayByTitle(std::vector<Course*>& courses) {
  sort(courses.begin(), courses.end(), CompareCoursePointersByTitle);
}

void CourseUtil::SortCoursePointerArrayByAvgDifficulty(
    std::vector<Course*>& courses) {
  course_sort_val.clear();
  for (unsigned i = 0; i < courses.size(); ++i) {
    int iMeter = courses[i]->GetMeter(
        GAMESTATE->GetCurrentStyle(PLAYER_INVALID)->m_StepsType,
        Difficulty_Medium);
    course_sort_val[courses[i]] = ssprintf("%06i", iMeter);
  }
  sort(courses.begin(), courses.end(), CompareCoursePointersByTitle);
  stable_sort(
      courses.begin(), courses.end(),
      CompareCoursePointersBySortValueAscending);

  stable_sort(courses.begin(), courses.end(), MovePlayersBestToEnd);
}

void CourseUtil::SortCoursePointerArrayByNumPlays(
    std::vector<Course*>& courses, ProfileSlot slot, bool descending) {
  if (!PROFILEMAN->IsPersistentProfile(slot)) {
    return;  // nothing to do since we don't have data
  }
  Profile* pProfile = PROFILEMAN->GetProfile(slot);
  SortCoursePointerArrayByNumPlays(courses, pProfile, descending);
}

void CourseUtil::SortCoursePointerArrayByNumPlays(
    std::vector<Course*>& courses, const Profile* pProfile, bool descending) {
  ASSERT(pProfile != nullptr);
  for (unsigned i = 0; i < courses.size(); ++i) {
    course_sort_val[courses[i]] =
        ssprintf("%09i", pProfile->GetCourseNumTimesPlayed(courses[i]));
  }
  stable_sort(
      courses.begin(), courses.end(),
      descending ? CompareCoursePointersBySortValueDescending
                 : CompareCoursePointersBySortValueAscending);
  course_sort_val.clear();
}

void CourseUtil::SortByMostRecentlyPlayedForMachine(
    std::vector<Course*>& courses) {
  Profile* pProfile = PROFILEMAN->GetMachineProfile();

  for (const Course* course : courses) {
    int num_times_played = pProfile->GetCourseNumTimesPlayed(course);
    RString val =
        num_times_played
            ? pProfile->GetCourseLastPlayedDateTime(course).GetString()
            : RString("9999999999999");
    course_sort_val[course] = val;
  }

  stable_sort(
      courses.begin(), courses.end(),
      CompareCoursePointersBySortValueAscending);
  course_sort_val.clear();
}

void CourseUtil::MakeDefaultEditCourseEntry(CourseEntry& out) {
  out.song_id_.FromSong(GAMESTATE->GetDefaultSong());
  out.steps_criteria_.m_difficulty = Difficulty_Medium;
}

//////////////////////////////////
// Autogen
//////////////////////////////////

void CourseUtil::AutogenEndlessFromGroup(
    const RString& group_name, Difficulty diff, Course& out) {
  out.m_bIsAutogen = true;
  out.m_bRepeat = true;
  out.m_bShuffle = true;
  out.m_iLives = -1;
  FOREACH_ENUM(Difficulty, dc)
  out.m_iCustomMeter[dc] = -1;

  if (group_name == "") {
    out.m_sMainTitle = "All Songs";
    // NOTE(aj): this sounds reasonable...
    out.m_sBannerPath = THEME->GetPathG("Banner", "all music");
  } else {
    out.m_sMainTitle = SONGMAN->ShortenGroupName(group_name);
    out.m_sBannerPath = SONGMAN->GetSongGroupBannerPath(group_name);
  }

  // NOTE(glenn): We want multiple songs, so we can try to prevent repeats
  // during gameplay. (We might still get a repeat at the repeat boundary,
  // but that'd be rare.)
  CourseEntry e;
  e.song_criteria_.m_sGroupName = group_name;
  e.steps_criteria_.m_difficulty = diff;
  e.is_secret_ = true;

  // Insert a copy of e for each song in the group.
  out.m_vEntries.insert(
      out.m_vEntries.end(), SONGMAN->GetSongs(group_name).size(), e);
}

void CourseUtil::AutogenNonstopFromGroup(
    const RString& group_name, Difficulty diff, Course& out) {
  AutogenEndlessFromGroup(group_name, diff, out);

  out.m_bRepeat = false;

  out.m_sMainTitle += " Random";

  // resize to 4
  while (out.m_vEntries.size() < 4) {
    out.m_vEntries.push_back(out.m_vEntries[0]);
  }
  while (out.m_vEntries.size() > 4) {
    out.m_vEntries.pop_back();
  }
}

void CourseUtil::AutogenOniFromArtist(
    const RString& artist_mame, RString artist_name_translit,
    std::vector<Song*> songs, Difficulty difficulty, Course& out) {
  out.m_bIsAutogen = true;
  out.m_bRepeat = false;
  out.m_bShuffle = true;
  out.m_bSortByMeter = true;

  out.m_iLives = 4;
  FOREACH_ENUM(Difficulty, diff)
  out.m_iCustomMeter[diff] = -1;

  ASSERT(artist_mame != "");
  ASSERT(songs.size() > 0);

  // "Artist Oni" is a little repetitive; "by Artist" stands out less, and
  // lowercasing "by" puts more emphasis on the artist's name. It also sorts
  // them together.
  out.m_sMainTitle = "by " + artist_mame;
  if (artist_name_translit != artist_mame) {
    out.m_sMainTitleTranslit = "by " + artist_name_translit;
  }

  // NOTE(aj): How would we handle Artist Oni course banners, anyways?
  // m_sBannerPath = ""; // XXX

  // Shuffle the list to determine which songs we'll use. Shuffle it
  // deterministically, so we always get the same set of songs unless the
  // song set changes.
  {
    RandomGen rng(GetHashForString(artist_mame) + songs.size());
    std::shuffle(songs.begin(), songs.end(), rng);
  }

  // Only use up to four songs.
  if (songs.size() > 4) {
    songs.erase(songs.begin() + 4, songs.end());
  }

  CourseEntry course_entry;
  course_entry.steps_criteria_.m_difficulty = difficulty;

  for (unsigned i = 0; i < songs.size(); ++i) {
    course_entry.song_id_.FromSong(songs[i]);
    out.m_vEntries.push_back(course_entry);
  }
}

void CourseUtil::WarnOnInvalidMods(RString mods) {
  PlayerOptions player_options;
  SongOptions song_options;
  std::vector<RString> vs;
  split(mods, ",", vs, true);
  for (const RString& s : vs) {
    bool bValid = false;
    RString sErrorDetail;
    bValid |= player_options.FromOneModString(s, sErrorDetail);
    bValid |= song_options.FromOneModString(s, sErrorDetail);
    // ==Invalid options that used to be valid==
    // - all noteskins (solo, note, foon, &c.)
    // - protiming (done in Lua now)
    // ==Things I've seen in real course files==
    // - 900% BRINK (damnit japan)
    // - TISPY
    // - 9000% OVERCOMING (what?)
    // - 9200% TORNADE (it's like a grenade but a tornado)
    // - 50% PROTIMING (HOW THE HELL DOES 50% PROTIMING EVEN WORK)
    // - BREAK
    if (!bValid) {
      RString full_error =
          ssprintf("Error processing '%s' in '%s'", s.c_str(), mods.c_str());
      if (!sErrorDetail.empty()) {
        full_error += ": " + sErrorDetail;
      }
      LOG->UserLog("", "", "%s", full_error.c_str());
      Dialog::OK(full_error, "INVALID_PLAYER_OPTION_WARNING");
    }
  }
}

int EditCourseUtil::MAX_NAME_LENGTH = 16;
int EditCourseUtil::MAX_PER_PROFILE = 32;
int EditCourseUtil::MIN_WORKOUT_MINUTES = 4;
int EditCourseUtil::MAX_WORKOUT_MINUTES = 90;
bool EditCourseUtil::s_bNewCourseNeedsName = false;

bool EditCourseUtil::Save(Course* course) {
  return EditCourseUtil::RenameAndSave(course, course->GetDisplayFullTitle());
}

bool EditCourseUtil::RenameAndSave(Course* course, RString new_name) {
  ASSERT(!new_name.empty());

  EditCourseUtil::s_bNewCourseNeedsName = false;

  RString new_file_path;
  if (course->IsAnEdit()) {
    new_file_path = PROFILEMAN->GetProfileDir(ProfileSlot_Machine) +
                    EDIT_COURSES_SUBDIR + new_name + ".crs";
  } else {
    RString dir, name, ext;
    splitpath(course->m_sPath, dir, name, ext);
    new_file_path = dir + new_name + ext;
  }

  // remove the old file if the name is changing
  if (!course->m_sPath.empty() && new_file_path != course->m_sPath) {
    FILEMAN->Remove(course->m_sPath);  // not fatal if this fails
  }

  course->m_sMainTitle = new_name;
  course->m_sPath = new_file_path;
  return CourseWriterCRS::Write(*course, course->m_sPath, false);
}

bool EditCourseUtil::RemoveAndDeleteFile(Course* course) {
  if (!FILEMAN->Remove(course->m_sPath)) {
    return false;
  }
  FILEMAN->Remove(course->GetCacheFilePath());
  if (course->IsAnEdit()) {
    PROFILEMAN->LoadMachineProfile();
  } else {
    SONGMAN->DeleteCourse(course);
    delete course;
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
    const RString& answer, RString& error) {
  if (answer.empty()) {
    error = YOU_MUST_SUPPLY_NAME;
    return false;
  }

  static const RString invalid_chars = "\\/:*?\"<>|";
  if (strpbrk(answer, invalid_chars) != nullptr) {
    error =
        ssprintf(EDIT_NAME_CANNOT_CONTAIN.GetValue(), invalid_chars.c_str());
    return false;
  }

  // Check for name conflicts
  std::vector<Course*> courses;
  EditCourseUtil::GetAllEditCourses(courses);
  for (const Course* course : courses) {
    if (GAMESTATE->m_pCurCourse == course) {
      continue;  // don't comepare name against ourself
    }

    if (course->GetDisplayFullTitle() == answer) {
      error = EDIT_NAME_CONFLICTS;
      return false;
    }
  }

  return true;
}

void EditCourseUtil::UpdateAndSetTrail() {
  ASSERT(GAMESTATE->GetCurrentStyle(PLAYER_INVALID) != nullptr);
  StepsType steps_type =
      GAMESTATE->GetCurrentStyle(PLAYER_INVALID)->m_StepsType;
  Trail* trail = nullptr;
  if (GAMESTATE->m_pCurCourse) {
    trail = GAMESTATE->m_pCurCourse->GetTrailForceRegenCache(steps_type);
  }
  GAMESTATE->m_pCurTrail[PLAYER_1].Set(trail);
}

void EditCourseUtil::PrepareForPlay() {
  GAMESTATE->m_pCurSong.Set(
      nullptr);  // CurSong will be set if we back out.  Set it back to nullptr
                 // so that ScreenStage won't show the last song.
  GAMESTATE->m_PlayMode.Set(PLAY_MODE_ENDLESS);
  GAMESTATE->m_bSideIsJoined[0] = true;

  PROFILEMAN->GetProfile(ProfileSlot_Player1)->m_GoalType = GoalType_Time;
  Course* course = GAMESTATE->m_pCurCourse;
  PROFILEMAN->GetProfile(ProfileSlot_Player1)->m_iGoalSeconds =
      static_cast<int>(course->m_fGoalSeconds);
}

void EditCourseUtil::GetAllEditCourses(std::vector<Course*>& courses) {
  std::vector<Course*> courses_temp;
  SONGMAN->GetAllCourses(courses_temp, false);
  for (Course* course : courses_temp) {
    if (course->GetLoadedFromProfileSlot() != ProfileSlot_Invalid) {
      courses.push_back(course);
    }
  }
}

void EditCourseUtil::LoadDefaults(Course& out) {
  out = Course();

  out.m_fGoalSeconds = 0;

  // pick a default name
  // XXX: Make this localizable
  for (int i = 0; i < 10000; ++i) {
    out.m_sMainTitle = ssprintf("Workout %d", i + 1);

    std::vector<Course*> courses;
    EditCourseUtil::GetAllEditCourses(courses);

    if (std::any_of(courses.begin(), courses.end(), [&](const Course* course) {
          return out.m_sMainTitle == course->m_sMainTitle;
        })) {
      break;
    }
  }

  std::vector<Song*> songs;
  SONGMAN->GetPreferredSortSongs(songs);
  for (int i = 0; i < (int)songs.size() && i < 6; ++i) {
    CourseEntry course_entry;
    course_entry.song_id_.FromSong(songs[i]);
    course_entry.steps_criteria_.m_difficulty = Difficulty_Easy;
    out.m_vEntries.push_back(course_entry);
  }
}

//////////////////////////////////
// CourseID
//////////////////////////////////

void CourseID::FromCourse(const Course* course) {
  if (course) {
    if (course->m_bIsAutogen) {
      path_ = "";
      full_title_ = course->GetTranslitFullTitle();
    } else {
      path_ = course->m_sPath;
      full_title_ = "";
    }
  } else {
    path_ = "";
    full_title_ = "";
  }

  // HACK for backwards compatibility:
  // Strip off leading "/".  2005/05/21 file layer changes added a leading
  // slash.
  if (path_.Left(1) == "/") {
    path_.erase(path_.begin());
  }
}

Course* CourseID::ToCourse() const {
  Course* course = nullptr;
  if (!path_.empty()) {
    // HACK for backwards compatibility:
    // Re-add the leading "/".  2005/05/21 file layer changes added a leading
    // slash.
    RString slash_path = path_;
    if (slash_path.Left(1) != "/") {
      slash_path = "/" + slash_path;
    }

    course = SONGMAN->GetCourseFromPath(slash_path);
  }

  if (course == nullptr && !full_title_.empty()) {
    course = SONGMAN->GetCourseFromName(full_title_);
  }

  return course;
}

XNode* CourseID::CreateNode() const {
  XNode* node = new XNode("Course");
  ;

  if (!path_.empty()) {
    node->AppendAttr("Path", path_);
  }
  if (!full_title_.empty()) {
    node->AppendAttr("FullTitle", full_title_);
  }

  return node;
}

void CourseID::LoadFromNode(const XNode* node) {
  ASSERT(node->GetName() == "Course");
  full_title_ = RString();
  path_ = RString();
  if (!node->GetAttrValue("Path", path_)) {
    node->GetAttrValue("FullTitle", full_title_);
  }

  // HACK for backwards compatibility: /AdditionalCourses has been merged into
  // /Courses
  if (path_.Left(18) == "AdditionalCourses/") {
    path_.replace(0, 18, "Courses/");
  }
}

RString CourseID::ToString() const {
  if (!path_.empty()) {
    return path_;
  }
  if (!full_title_.empty()) {
    return full_title_;
  }
  return RString();
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
