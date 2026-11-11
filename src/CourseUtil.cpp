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
    const Course* course1, const Course* course2) {
  std::string name1 = course1->GetDisplayFullTitle();
  std::string name2 = course2->GetDisplayFullTitle();
  return CompareNoCase(name1, name2) < 0;
}

static bool CompareCoursePointersByAutogen(
    const Course* course1, const Course* course2) {
  int b1 = course1->is_autogen_;
  int b2 = course2->is_autogen_;
  if (b1 < b2) {
    return true;
  } else if (b1 > b2) {
    return false;
  } else {
    return CompareCoursePointersByName(course1, course2);
  }
}

static bool CompareCoursePointersByDifficulty(
    const Course* course1, const Course* course2) {
  int num1 = course1->GetEstimatedNumStages();
  int num2 = course2->GetEstimatedNumStages();
  if (num1 < num2) {
    return true;
  } else if (num1 > num2) {
    return false;
  } else {  // iNum1 == iNum2
    return CompareCoursePointersByAutogen(course1, course2);
  }
}

static bool CompareCoursePointersByTotalDifficulty(
    const Course* course1, const Course* course2) {
  int num1 = course1->sort_order_total_difficulty_;
  int num2 = course2->sort_order_total_difficulty_;

  if (num1 == num2) {
    return CompareCoursePointersByAutogen(course1, course2);
  }
  return num1 < num2;
}

static bool MovePlayersBestToEnd(const Course* course1, const Course* course2) {
  bool c1_has_best = course1->CourseHasBestOrWorst();
  bool c2_has_best = course2->CourseHasBestOrWorst();
  if (!c1_has_best && !c2_has_best) {
    return false;
  }
  if (c1_has_best && !c2_has_best) {
    return false;
  }
  if (!c1_has_best && c2_has_best) {
    return true;
  }

  return CompareCoursePointersByName(course1, course2);
}

static bool CompareRandom(const Course* course1, const Course* course2) {
  return (course1->AllSongsAreFixed() && !course2->AllSongsAreFixed());
}

static bool CompareCoursePointersByRanking(
    const Course* course1, const Course* course2) {
  int num1 = course1->sort_order_ranking_;
  int num2 = course2->sort_order_ranking_;

  if (num1 == num2) {
    return CompareCoursePointersByAutogen(course1, course2);
  }
  return num1 < num2;
}

void CourseUtil::SortCoursePointerArrayByDifficulty(
    std::vector<Course*>& courses_in_out) {
  sort(
      courses_in_out.begin(), courses_in_out.end(),
      CompareCoursePointersByDifficulty);
}

void CourseUtil::SortCoursePointerArrayByRanking(
    std::vector<Course*>& courses_in_out) {
  for (unsigned i = 0; i < courses_in_out.size(); i++) {
    courses_in_out[i]->UpdateCourseStats(
        GAMESTATE->GetCurrentStyle(kPlayerInvalid)->steps_type_);
  }
  sort(
      courses_in_out.begin(), courses_in_out.end(),
      CompareCoursePointersByRanking);
}

void CourseUtil::SortCoursePointerArrayByTotalDifficulty(
    std::vector<Course*>& courses_in_out) {
  for (unsigned i = 0; i < courses_in_out.size(); i++) {
    courses_in_out[i]->UpdateCourseStats(
        GAMESTATE->GetCurrentStyle(kPlayerInvalid)->steps_type_);
  }
  sort(
      courses_in_out.begin(), courses_in_out.end(),
      CompareCoursePointersByTotalDifficulty);
}

static bool CompareCoursePointersByType(
    const Course* course1, const Course* course2) {
  return course1->GetPlayMode() < course2->GetPlayMode();
}

void CourseUtil::SortCoursePointerArrayByType(
    std::vector<Course*>& courses_in_out) {
  stable_sort(
      courses_in_out.begin(), courses_in_out.end(),
      CompareCoursePointersByType);
}

void CourseUtil::MoveRandomToEnd(std::vector<Course*>& courses_in_out) {
  stable_sort(courses_in_out.begin(), courses_in_out.end(), CompareRandom);
}

static std::map<const Course*, std::string> course_sort_val;

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

void CourseUtil::SortCoursePointerArrayByTitle(
    std::vector<Course*>& courses_in_out) {
  sort(
      courses_in_out.begin(), courses_in_out.end(),
      CompareCoursePointersByTitle);
}

void CourseUtil::SortCoursePointerArrayByAvgDifficulty(
    std::vector<Course*>& courses_in_out) {
  RageTimer foo;
  course_sort_val.clear();
  for (unsigned i = 0; i < courses_in_out.size(); ++i) {
    int meter = courses_in_out[i]->GetMeter(
        GAMESTATE->GetCurrentStyle(kPlayerInvalid)->steps_type_,
        Difficulty_Medium);
    course_sort_val[courses_in_out[i]] = ssprintf("%06i", meter);
  }
  sort(
      courses_in_out.begin(), courses_in_out.end(),
      CompareCoursePointersByTitle);
  stable_sort(
      courses_in_out.begin(), courses_in_out.end(),
      CompareCoursePointersBySortValueAscending);

  stable_sort(
      courses_in_out.begin(), courses_in_out.end(), MovePlayersBestToEnd);
}

void CourseUtil::SortCoursePointerArrayByNumPlays(
    std::vector<Course*>& courses_in_out, ProfileSlot slot, bool descending) {
  if (!PROFILEMAN->IsPersistentProfile(slot)) {
    return;  // nothing to do since we don't have data
  }
  Profile* profile = PROFILEMAN->GetProfile(slot);
  SortCoursePointerArrayByNumPlays(courses_in_out, profile, descending);
}

void CourseUtil::SortCoursePointerArrayByNumPlays(
    std::vector<Course*>& courses_in_out, const Profile* profile,
    bool descending) {
  ASSERT(profile != nullptr);
  for (unsigned i = 0; i < courses_in_out.size(); ++i) {
    course_sort_val[courses_in_out[i]] =
        ssprintf("%09i", profile->GetCourseNumTimesPlayed(courses_in_out[i]));
  }
  stable_sort(
      courses_in_out.begin(), courses_in_out.end(),
      descending ? CompareCoursePointersBySortValueDescending
                 : CompareCoursePointersBySortValueAscending);
  course_sort_val.clear();
}

void CourseUtil::SortByMostRecentlyPlayedForMachine(
    std::vector<Course*>& courses_in_out) {
  Profile* profile = PROFILEMAN->GetMachineProfile();

  for (const Course* c : courses_in_out) {
    int num_times_played = profile->GetCourseNumTimesPlayed(c);
    std::string val = num_times_played
                          ? profile->GetCourseLastPlayedDateTime(c).GetString()
                          : std::string("9999999999999");
    course_sort_val[c] = val;
  }

  stable_sort(
      courses_in_out.begin(), courses_in_out.end(),
      CompareCoursePointersBySortValueAscending);
  course_sort_val.clear();
}

//////////////////////////////////
// Autogen
//////////////////////////////////

void CourseUtil::AutogenEndlessFromGroup(
    const std::string& group_name, Difficulty diff, Course& out) {
  out.is_autogen_ = true;
  out.repeat_ = true;
  out.shuffle_ = true;
  out.lives_ = -1;
  FOREACH_ENUM(Difficulty, dc)
  out.custom_meter_[dc] = -1;

  if (group_name == "") {
    out.main_title_ = "All Songs";
    // this sounds reasonable... -aj
    out.banner_path_ = THEME->GetPathG("Banner", "all music");
  } else {
    out.main_title_ = SONGMAN->ShortenGroupName(group_name);
    out.banner_path_ = SONGMAN->GetSongGroupBannerPath(group_name);
  }

  // We want multiple songs, so we can try to prevent repeats during
  // gameplay. (We might still get a repeat at the repeat boundary,
  // but that'd be rare.) -glenn
  CourseEntry e;
  e.song_criteria_.group_names_.push_back(group_name);
  e.steps_criteria_.difficulty_ = diff;
  e.secret_ = true;

  // Insert a copy of e for each song in the group.
  out.entries_.insert(
      out.entries_.end(), SONGMAN->GetSongs(group_name).size(), e);
}

void CourseUtil::AutogenNonstopFromGroup(
    const std::string& group_name, Difficulty diff, Course& out) {
  AutogenEndlessFromGroup(group_name, diff, out);

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
    const std::string& artist_name, std::string artist_name_translit,
    std::vector<Song*> songs, Difficulty dc, Course& out) {
  out.is_autogen_ = true;
  out.repeat_ = false;
  out.shuffle_ = true;
  out.sort_by_meter_ = true;

  out.lives_ = 4;
  FOREACH_ENUM(Difficulty, cd)
  out.custom_meter_[cd] = -1;

  ASSERT(artist_name != "");
  ASSERT(songs.size() > 0);

  /* "Artist Oni" is a little repetitive; "by Artist" stands out less, and
   * lowercasing "by" puts more emphasis on the artist's name. It also sorts
   * them together. */
  out.main_title_ = "by " + artist_name;
  if (artist_name_translit != artist_name) {
    out.main_title_translit_ = "by " + artist_name_translit;
  }

  // How would we handle Artist Oni course banners, anyways? -aj
  // m_sBannerPath = ""; // XXX

  /* Shuffle the list to determine which songs we'll use. Shuffle it
   * deterministically, so we always get the same set of songs unless the
   * song set changes. */
  {
    RandomGen rng(GetHashForString(artist_name) + songs.size());
    std::shuffle(songs.begin(), songs.end(), rng);
  }

  // Only use up to four songs.
  if (songs.size() > 4) {
    songs.erase(songs.begin() + 4, songs.end());
  }

  CourseEntry e;
  e.steps_criteria_.difficulty_ = dc;

  for (unsigned i = 0; i < songs.size(); ++i) {
    e.song_id_.FromSong(songs[i]);
    out.entries_.push_back(e);
  }
}

void CourseUtil::WarnOnInvalidMods(std::string mods) {
  PlayerOptions po;
  SongOptions so;
  std::vector<std::string> vs;
  split(mods, ",", vs, true);
  for (const std::string& s : vs) {
    bool valid = false;
    std::string error_detail;
    valid |= po.FromOneModString(s, error_detail);
    valid |= so.FromOneModString(s, error_detail);
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
    if (!valid) {
      std::string full_error =
          ssprintf("Error processing '%s' in '%s'", s.c_str(), mods.c_str());
      if (!error_detail.empty()) {
        full_error += ": " + error_detail;
      }
      LOG->UserLog("", "", "%s", full_error.c_str());
      Dialog::OK(full_error, "INVALID_PLAYER_OPTION_WARNING");
    }
  }
}

int EditCourseUtil::max_name_length = 16;
int EditCourseUtil::max_per_profile = 32;
int EditCourseUtil::min_workout_minutes = 4;
int EditCourseUtil::max_workout_minutes = 90;
bool EditCourseUtil::new_course_needs_name = false;

bool EditCourseUtil::Save(Course* course) {
  return EditCourseUtil::RenameAndSave(course, course->GetDisplayFullTitle());
}

bool EditCourseUtil::RenameAndSave(Course* course, std::string new_name) {
  ASSERT(!new_name.empty());

  EditCourseUtil::new_course_needs_name = false;

  std::string new_file_path;
  if (course->IsAnEdit()) {
    new_file_path = PROFILEMAN->GetProfileDir(ProfileSlot_Machine) +
                    kEditCoursesSubdir + new_name + ".crs";
  } else {
    std::string dir, name, ext;
    splitpath(course->path_, dir, name, ext);
    new_file_path = dir + new_name + ext;
  }

  // remove the old file if the name is changing
  if (!course->path_.empty() && new_file_path != course->path_) {
    FILEMAN->Remove(course->path_);  // not fatal if this fails
  }

  course->main_title_ = new_name;
  course->path_ = new_file_path;
  return CourseWriterCRS::Write(*course, course->path_, false);
}

bool EditCourseUtil::RemoveAndDeleteFile(Course* course) {
  if (!FILEMAN->Remove(course->path_)) {
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
    const std::string& answer, std::string& error_out) {
  if (answer.empty()) {
    error_out = YOU_MUST_SUPPLY_NAME;
    return false;
  }

  static const std::string kInvalidChars = "\\/:*?\"<>|";
  if (strpbrk(answer.c_str(), kInvalidChars.c_str()) != nullptr) {
    error_out = ssprintf(
        EDIT_NAME_CANNOT_CONTAIN.GetValue().c_str(), kInvalidChars.c_str());
    return false;
  }

  // Check for name conflicts
  std::vector<Course*> courses;
  EditCourseUtil::GetAllEditCourses(courses);
  for (const Course* p : courses) {
    if (GAMESTATE->cur_course_ == p) {
      continue;  // don't comepare name against ourself
    }

    if (p->GetDisplayFullTitle() == answer) {
      error_out = EDIT_NAME_CONFLICTS;
      return false;
    }
  }

  return true;
}

void EditCourseUtil::UpdateAndSetTrail() {
  ASSERT(GAMESTATE->GetCurrentStyle(kPlayerInvalid) != nullptr);
  StepsType st = GAMESTATE->GetCurrentStyle(kPlayerInvalid)->steps_type_;
  Trail* trail = nullptr;
  if (GAMESTATE->cur_course_) {
    trail = GAMESTATE->cur_course_->GetTrailForceRegenCache(st);
  }
  GAMESTATE->cur_trail_[PLAYER_1].Set(trail);
}

void EditCourseUtil::PrepareForPlay() {
  GAMESTATE->cur_song_.Set(
      nullptr);  // CurSong will be set if we back out.  Set it back to nullptr
                 // so that ScreenStage won't show the last song.
  GAMESTATE->play_mode_.Set(PLAY_MODE_ENDLESS);
  GAMESTATE->side_is_joined_[0] = true;

  PROFILEMAN->GetProfile(ProfileSlot_Player1)->goal_type_ = GoalType_Time;
  Course* course = GAMESTATE->cur_course_;
  PROFILEMAN->GetProfile(ProfileSlot_Player1)->goal_seconds_ =
      static_cast<int>(course->goal_seconds_);
}

void EditCourseUtil::GetAllEditCourses(std::vector<Course*>& courses_out) {
  std::vector<Course*> courses_temp;
  SONGMAN->GetAllCourses(courses_temp, false);
  for (Course* c : courses_temp) {
    if (c->GetLoadedFromProfileSlot() != ProfileSlot_Invalid) {
      courses_out.push_back(c);
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

    std::vector<Course*> courses;
    EditCourseUtil::GetAllEditCourses(courses);

    if (std::any_of(courses.begin(), courses.end(), [&](const Course* p) {
          return out.main_title_ == p->main_title_;
        })) {
      break;
    }
  }

  std::vector<Song*> songs;
  SONGMAN->GetPreferredSortSongs(songs);
  for (int i = 0; i < (int)songs.size() && i < 6; i++) {
    CourseEntry ce;
    ce.song_id_.FromSong(songs[i]);
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
  Course* course = nullptr;
  if (!path_.empty()) {
    // HACK for backwards compatibility:
    // Re-add the leading "/".  2005/05/21 file layer changes added a leading
    // slash.
    std::string slash_path = path_;
    if (Left(slash_path, 1) != "/") {
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
  full_title_ = std::string();
  path_ = std::string();
  if (!node->GetAttrValue("Path", path_)) {
    node->GetAttrValue("FullTitle", full_title_);
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
