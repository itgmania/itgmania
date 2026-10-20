#include "Profile.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "Bookkeeper.h"
#include "Character.h"
#include "CharacterManager.h"
#include "Course.h"
#include "CourseUtil.h"
#include "CryptManager.h"
#include "DateTime.h"
#include "Difficulty.h"
#include "EnumHelper.h"
#include "Game.h"
#include "GameConstantsAndTypes.h"
#include "GameManager.h"
#include "GameState.h"
#include "Grade.h"
#include "Group.h"
#include "HighScore.h"
#include "IniFile.h"
#include "LuaManager.h"
#include "PlayerNumber.h"
#include "Preference.h"
#include "PrefsManager.h"
#include "ProfileManager.h"
#include "RageFile.h"
#include "RageFileDriverDeflate.h"
#include "RageFileManager.h"
#include "RageLog.h"
#include "RageThreads.h"
#include "RageTimer.h"
#include "RageUtil.h"
#include "RageUtil/ConvertValue.h"
#include "RageUtil/Regex.h"
#include "Song.h"
#include "SongManager.h"
#include "SongUtil.h"
#include "StdString.h"
#include "Steps.h"
#include "StyleUtil.h"
#include "ThemeManager.h"
#include "ThemeMetric.h"
#include "TrailUtil.h"
#include "UnlockManager.h"
#include "XmlFile.h"
#include "XmlFileUtil.h"
#include "global.h"

const std::string STATS_XML = "Stats.xml";
const std::string STATS_XML_GZ = "Stats.xml.gz";
/** @brief The filename for where one can edit their personal profile
 * information. */
const std::string EDITABLE_INI = "Editable.ini";
/** @brief A tiny file containing the type and list priority. */
const std::string TYPE_INI = "Type.ini";
/** @brief The filename containing the signature for STATS_XML's signature. */
const std::string DONT_SHARE_SIG = "DontShare.sig";
const std::string PUBLIC_KEY_FILE = "public.key";
const std::string SCREENSHOTS_SUBDIR = "Screenshots/";
const std::string EDIT_STEPS_SUBDIR = "Edits/";
const std::string EDIT_COURSES_SUBDIR = "EditCourses/";
// const std::string UPLOAD_SUBDIR         = "Upload/";
const std::string RIVAL_SUBDIR = "Rivals/";
const std::string SONGS_SUBDIR = "Songs/";
const std::string PACKS_SUBDIR = "Packs/";

ThemeMetric<bool> SHOW_COIN_DATA("Profile", "ShowCoinData");
static Preference<bool> g_bProfileDataCompress("ProfileDataCompress", false);
static ThemeMetric<std::string> UNLOCK_AUTH_STRING(
    "Profile", "UnlockAuthString");
#define GUID_SIZE_BYTES 8

#define MAX_EDITABLE_INI_SIZE_BYTES 2 * 1024               // 2KB
#define MAX_PLAYER_STATS_XML_SIZE_BYTES 100 * 1024 * 1024  // 100MB

const int DEFAULT_WEIGHT_POUNDS = 120;
const float DEFAULT_BIRTH_YEAR = 1995;

#if defined(_MSC_VER)
#pragma warning(disable : 4706)  // assignment within conditional expression
#endif

static const char* ProfileTypeNames[] = {
    "Guest",
    "Normal",
    "Test",
};
XToString(ProfileType);
StringToX(ProfileType);
LuaXType(ProfileType);

Profile::~Profile() { ClearSongs(); }

void Profile::ClearSongs() {
  if (songs_.empty()) {
    return;
  }
  Song* gamestate_curr_song = GAMESTATE->cur_song_;
  for (size_t i = 0; i < songs_.size(); ++i) {
    Song* curr_song = songs_[i];
    if (curr_song == gamestate_curr_song) {
      GAMESTATE->cur_song_.Set(nullptr);
    }
    delete curr_song;
  }
  songs_.clear();
  groups_.clear();
  songs_groups_.clear();
}

int Profile::HighScoresForASong::GetNumTimesPlayed() const {
  int count = 0;
  for (const std::pair<const StepsID, HighScoresForASteps>& i :
       steps_high_scores_) {
    count += i.second.hsl_.GetNumTimesPlayed();
  }
  return count;
}

int Profile::HighScoresForACourse::GetNumTimesPlayed() const {
  int count = 0;
  for (const std::pair<const TrailID, HighScoresForATrail>& i :
       trail_high_scores_) {
    count += i.second.hsl_.GetNumTimesPlayed();
  }
  return count;
}

void Profile::InitEditableData() {
  display_name_ = "";
  character_id_ = "";
  last_used_high_score_name_ = "";
  weight_pounds_ = 0;
  voomax_ = 0;
  birth_year_ = 0;
  ignore_step_count_calories_ = false;
  is_male_ = true;
}

void Profile::ClearStats() {
  // don't reset the Guid
  std::string guid = guid_;
  InitAll();
  guid_ = guid;
}

std::string Profile::MakeGuid() {
  std::string s;
  s.reserve(GUID_SIZE_BYTES * 2);
  unsigned char buf[GUID_SIZE_BYTES];
  CryptManager::GetRandomBytes(buf, GUID_SIZE_BYTES);
  for (unsigned i = 0; i < GUID_SIZE_BYTES; i++) {
    s += ssprintf("%02x", buf[i]);
  }
  return s;
}

void Profile::InitGeneralData() {
  guid_ = MakeGuid();

  sort_order_ = SortOrder_Invalid;
  last_difficulty_ = Difficulty_Invalid;
  last_course_difficulty_ = Difficulty_Invalid;
  last_steps_type_ = StepsType_Invalid;
  last_song_.Unset();
  last_course_.Unset();
  current_combo_ = 0;
  total_sessions_ = 0;
  total_session_seconds_ = 0;
  total_gameplay_seconds_ = 0;
  total_calories_burned_ = 0;
  goal_type_ = (GoalType)0;
  goal_calories_ = 0;
  goal_seconds_ = 0;
  total_dance_points_ = 0;
  num_extra_stages_passed_ = 0;
  num_extra_stages_failed_ = 0;
  num_toasties_ = 0;
  unlocked_entry_ids_.clear();
  last_played_machine_guid_ = "";
  last_played_date_.Init();
  total_taps_and_holds_ = 0;
  total_jumps_ = 0;
  total_holds_ = 0;
  total_rolls_ = 0;
  total_mines_ = 0;
  total_hands_ = 0;
  total_lifts_ = 0;

  FOREACH_ENUM(PlayMode, i)
  num_songs_played_by_play_mode_[i] = 0;
  num_songs_played_by_style_.clear();
  FOREACH_ENUM(Difficulty, i)
  num_songs_played_by_difficulty_[i] = 0;
  for (int i = 0; i < MAX_METER + 1; i++) {
    num_songs_played_by_meter_[i] = 0;
  }
  num_total_songs_played_ = 0;
  ZERO(num_stages_passed_by_play_mode_);
  ZERO(num_stages_passed_by_grade_);

  user_table_.Unset();
}

void Profile::InitSongScores() { song_high_scores_.clear(); }

void Profile::InitCourseScores() { course_high_scores_.clear(); }

void Profile::InitCategoryScores() {
  FOREACH_ENUM(StepsType, st)
  FOREACH_ENUM(RankingCategory, rc)
  category_high_scores_[st][rc].Init();
}

void Profile::InitScreenshotData() { screenshots_.clear(); }

void Profile::InitCalorieData() { map_day_to_calories_burned_.clear(); }

std::string Profile::GetDisplayNameOrHighScoreName() const {
  if (!display_name_.empty()) {
    return display_name_;
  } else if (!last_used_high_score_name_.empty()) {
    return last_used_high_score_name_;
  } else {
    return std::string();
  }
}

Character* Profile::GetCharacter() const {
  std::vector<Character*> characters;
  CHARMAN->GetCharacters(characters);
  for (Character* c : characters) {
    if (CompareNoCase(c->character_id_, character_id_) == 0) {
      return c;
    }
  }
  return CHARMAN->GetDefaultCharacter();
}

void Profile::SetCharacter(const std::string character_id) {
  if (CHARMAN->GetCharacterFromID(character_id)) {
    character_id_ = character_id;
  }
}

static std::string FormatCalories(float cals) {
  return Commify((int)cals) + " Cal";
}

int Profile::GetCalculatedWeightPounds() const {
  if (weight_pounds_ == 0) {  // weight not entered
    return DEFAULT_WEIGHT_POUNDS;
  } else {
    return weight_pounds_;
  }
}

int Profile::GetAge() const {
  if (birth_year_ == 0) {
    return (GetLocalTime().tm_year + 1900) - DEFAULT_BIRTH_YEAR;
  }
  return (GetLocalTime().tm_year + 1900) - birth_year_;
}

std::string Profile::GetDisplayTotalCaloriesBurned() const {
  return FormatCalories(total_calories_burned_);
}

float Profile::GetCaloriesBurnedToday() const {
  DateTime now = DateTime::GetNowDate();
  return GetCaloriesBurnedForDay(now);
}

int Profile::GetTotalNumSongsPassed() const {
  int total = 0;
  FOREACH_ENUM(PlayMode, i)
  total += num_stages_passed_by_play_mode_[i];
  return total;
}

int Profile::GetTotalStepsWithTopGrade(
    StepsType st, Difficulty d, Grade g) const {
  int count = 0;

  for (Song* song : SONGMAN->GetAllSongs()) {
    if (!song->NormallyDisplayed()) {
      continue;  // skip
    }

    for (Steps* steps : song->GetAllSteps()) {
      if (steps->steps_type_ != st) {
        continue;  // skip
      }

      if (steps->GetDifficulty() != d) {
        continue;  // skip
      }

      const HighScoreList& hsl = GetStepsHighScoreList(song, steps);
      if (hsl.high_scores_.empty()) {
        continue;  // skip
      }

      if (hsl.high_scores_[0].GetGrade() == g) {
        count++;
      }
    }
  }

  return count;
}

int Profile::GetTotalTrailsWithTopGrade(
    StepsType st, CourseDifficulty d, Grade g) const {
  int count = 0;

  // add course high scores
  std::vector<Course*> courses;
  SONGMAN->GetAllCourses(courses, false);
  for (const Course* course : courses) {
    // Don't count any course that has any entries that change over time.
    if (!course->AllSongsAreFixed()) {
      continue;
    }

    std::vector<Trail*> trails;
    Trail* trail = course->GetTrail(st, d);
    if (trail == nullptr) {
      continue;
    }

    const HighScoreList& hsl = GetCourseHighScoreList(course, trail);
    if (hsl.high_scores_.empty()) {
      continue;  // skip
    }

    if (hsl.high_scores_[0].GetGrade() == g) {
      count++;
    }
  }

  return count;
}

float Profile::GetSongsPossible(StepsType st, Difficulty dc) const {
  int total_steps = 0;

  // add steps high scores
  const std::vector<Song*>& songs = SONGMAN->GetAllSongs();
  for (unsigned i = 0; i < songs.size(); i++) {
    Song* song = songs[i];

    if (!song->NormallyDisplayed()) {
      continue;  // skip
    }

    std::vector<Steps*> v_steps = song->GetAllSteps();
    for (unsigned j = 0; j < v_steps.size(); j++) {
      Steps* p_steps = v_steps[j];

      if (p_steps->steps_type_ != st) {
        continue;  // skip
      }

      if (p_steps->GetDifficulty() != dc) {
        continue;  // skip
      }

      total_steps++;
    }
  }

  return (float)total_steps;
}

float Profile::GetSongsActual(StepsType st, Difficulty dc) const {
  CHECKPOINT_M(ssprintf("Profile::GetSongsActual(%d,%d)", st, dc));

  float total_percents = 0;

  // add steps high scores
  for (const std::pair<const SongID, HighScoresForASong>& id :
       song_high_scores_) {
    Song* song = id.first.ToSong();

    CHECKPOINT_M(
        ssprintf("Profile::GetSongsActual: %p", static_cast<void*>(song)));

    // If the Song isn't loaded on the current machine, then we can't
    // get radar values to compute dance points.
    if (song == nullptr) {
      continue;
    }

    if (!song->NormallyDisplayed()) {
      continue;  // skip
    }

    CHECKPOINT_M(ssprintf(
        "Profile::GetSongsActual: song %s", song->GetSongDir().c_str()));
    const HighScoresForASong& hsfas = id.second;

    for (const std::pair<const StepsID, HighScoresForASteps>& j :
         hsfas.steps_high_scores_) {
      const StepsID& sid = j.first;
      Steps* steps = sid.ToSteps(song, true);
      CHECKPOINT_M(ssprintf(
          "Profile::GetSongsActual: song %p, steps %p",
          static_cast<void*>(song), static_cast<void*>(steps)));

      // If the Steps isn't loaded on the current machine, then we can't
      // get radar values to compute dance points.
      if (steps == nullptr) {
        continue;
      }

      if (steps->steps_type_ != st) {
        continue;
      }

      CHECKPOINT_M(ssprintf(
          "Profile::GetSongsActual: n %s = %p", sid.ToString().c_str(),
          static_cast<void*>(steps)));
      if (steps->GetDifficulty() != dc) {
        continue;  // skip
      }

      CHECKPOINT_M(ssprintf(
          "Profile::GetSongsActual: difficulty %s is correct",
          DifficultyToString(dc).c_str()));

      const HighScoresForASteps& h = j.second;
      const HighScoreList& hsl = h.hsl_;

      total_percents += hsl.GetTopScore().GetPercentDP();
    }
  }

  return total_percents;
}

float Profile::GetSongsPercentComplete(StepsType st, Difficulty dc) const {
  return GetSongsActual(st, dc) / GetSongsPossible(st, dc);
}

static void GetHighScoreCourses(std::vector<Course*>& courses_out) {
  courses_out.clear();

  std::vector<Course*> courses;
  SONGMAN->GetAllCourses(courses, false);

  for (Course* c : courses) {
    // Don't count any course that has any entries that change over time.
    if (!c->AllSongsAreFixed()) {
      continue;
    }

    courses_out.push_back(c);
  }
}

float Profile::GetCoursesPossible(StepsType st, CourseDifficulty cd) const {
  std::vector<Course*> courses;
  GetHighScoreCourses(courses);
  return std::count_if(courses.begin(), courses.end(), [&](const Course* c) {
    return c->GetTrail(st, cd) != nullptr;
  });
}

float Profile::GetCoursesActual(StepsType st, CourseDifficulty cd) const {
  float total_percents = 0;

  std::vector<Course*> courses;
  GetHighScoreCourses(courses);
  for (const Course* c : courses) {
    Trail* trail = c->GetTrail(st, cd);
    if (trail == nullptr) {
      continue;
    }

    const HighScoreList& hsl = GetCourseHighScoreList(c, trail);
    total_percents += hsl.GetTopScore().GetPercentDP();
  }

  return total_percents;
}

float Profile::GetCoursesPercentComplete(
    StepsType st, CourseDifficulty cd) const {
  return GetCoursesActual(st, cd) / GetCoursesPossible(st, cd);
}

float Profile::GetSongsAndCoursesPercentCompleteAllDifficulties(
    StepsType st) const {
  float actual = 0;
  float possible = 0;
  FOREACH_ENUM(Difficulty, d) {
    actual += GetSongsActual(st, d);
    possible += GetSongsPossible(st, d);
  }
  FOREACH_ENUM(CourseDifficulty, d) {
    actual += GetCoursesActual(st, d);
    possible += GetCoursesPossible(st, d);
  }
  return actual / possible;
}

int Profile::GetSongNumTimesPlayed(const Song* song) const {
  SongID song_id;
  song_id.FromSong(song);
  return GetSongNumTimesPlayed(song_id);
}

int Profile::GetSongNumTimesPlayed(const SongID& song_id) const {
  const HighScoresForASong* song = GetHighScoresForASong(song_id);
  if (song == nullptr) {
    return 0;
  }

  int total_num_times_played = 0;
  for (const std::pair<const StepsID, HighScoresForASteps>& j :
       song->steps_high_scores_) {
    const HighScoresForASteps& steps = j.second;

    total_num_times_played += steps.hsl_.GetNumTimesPlayed();
  }
  return total_num_times_played;
}

/*
 * Get the profile default modifiers.  Return true if set, in which case
 * sModifiersOut will be set.  Return false if no modifier string is set, in
 * which case the theme defaults should be used.  Note that the null string
 * means "no modifiers are active", which is distinct from no modifier string
 * being set at all.
 *
 * In practice, we get the default modifiers from the theme the first time a
 * game is played, and from the profile every time thereafter.
 */
bool Profile::GetDefaultModifiers(
    const Game* game_type, std::string& modifiers_out) const {
  std::map<std::string, std::string>::const_iterator it;
  it = default_modifiers_.find(game_type->name_);
  if (it == default_modifiers_.end()) {
    return false;
  }
  modifiers_out = it->second;
  return true;
}

void Profile::SetDefaultModifiers(
    const Game* game_type, const std::string& modifiers) {
  if (modifiers == "") {
    default_modifiers_.erase(game_type->name_);
  } else {
    default_modifiers_[game_type->name_] = modifiers;
  }
}

bool Profile::IsCodeUnlocked(std::string unlock_entry_id) const {
  return unlocked_entry_ids_.find(unlock_entry_id) != unlocked_entry_ids_.end();
}

Song* Profile::GetMostPopularSong() const {
  int max_num_times_played = 0;
  SongID id;
  for (const std::pair<const SongID, HighScoresForASong>& i :
       song_high_scores_) {
    int num_times_played = i.second.GetNumTimesPlayed();
    if (i.first.ToSong() != nullptr &&
        num_times_played > max_num_times_played) {
      id = i.first;
      max_num_times_played = num_times_played;
    }
  }

  return id.ToSong();
}

Course* Profile::GetMostPopularCourse() const {
  int max_num_times_played = 0;
  CourseID id;
  for (const std::pair<const CourseID, HighScoresForACourse>& i :
       course_high_scores_) {
    int num_times_played = i.second.GetNumTimesPlayed();
    if (i.first.ToCourse() != nullptr &&
        num_times_played > max_num_times_played) {
      id = i.first;
      max_num_times_played = num_times_played;
    }
  }

  return id.ToCourse();
}

// Steps high scores
void Profile::AddStepsHighScore(
    const Song* song, const Steps* steps, HighScore hs, int& index_out) {
  GetStepsHighScoreList(song, steps).AddHighScore(hs, index_out, IsMachine());
}

const HighScoreList& Profile::GetStepsHighScoreList(
    const Song* song, const Steps* steps) const {
  return ((Profile*)this)->GetStepsHighScoreList(song, steps);
}

HighScoreList& Profile::GetStepsHighScoreList(
    const Song* p_song, const Steps* p_steps) {
  SongID song_id;
  song_id.FromSong(p_song);

  StepsID steps_id;
  steps_id.FromSteps(p_steps);

  HighScoresForASong& hs_song =
      song_high_scores_[song_id];  // operator[] inserts into map
  HighScoresForASteps& hs_steps =
      hs_song.steps_high_scores_[steps_id];  // operator[] inserts into map

  return hs_steps.hsl_;
}

DateTime Profile::GetSongLastPlayedDateTime(const Song* song) const {
  SongID id;
  id.FromSong(song);
  std::map<SongID, HighScoresForASong>::const_iterator iter =
      song_high_scores_.find(id);

  // don't call this unless has been played once
  ASSERT(iter != song_high_scores_.end());
  ASSERT(!iter->second.steps_high_scores_.empty());

  DateTime latest;  // starts out zeroed
  for (const std::pair<const StepsID, HighScoresForASteps>& i :
       iter->second.steps_high_scores_) {
    const HighScoreList& hsl = i.second.hsl_;
    if (hsl.GetNumTimesPlayed() == 0) {
      continue;
    }
    if (latest < hsl.GetLastPlayed()) {
      latest = hsl.GetLastPlayed();
    }
  }
  return latest;
}

bool Profile::HasPassedSteps(const Song* song, const Steps* steps) const {
  const HighScoreList& hsl = GetStepsHighScoreList(song, steps);
  Grade grade = hsl.GetTopScore().GetGrade();
  switch (grade) {
    case Grade_Failed:
    case Grade_NoData:
      return false;
    default:
      return true;
  }
}

bool Profile::HasPassedAnyStepsInSong(const Song* song) const {
  const auto& steps = song->GetAllSteps();
  return std::any_of(steps.begin(), steps.end(), [&](const Steps* s) {
    return HasPassedSteps(song, s);
  });
}

void Profile::IncrementStepsPlayCount(const Song* song, const Steps* steps) {
  DateTime now = DateTime::GetNowDate();
  GetStepsHighScoreList(song, steps).IncrementPlayCount(now);
}

void Profile::GetGrades(
    const Song* p_song, StepsType st, int counts[NUM_Grade]) const {
  SongID song_id;
  song_id.FromSong(p_song);

  memset(counts, 0, sizeof(int) * NUM_Grade);
  const HighScoresForASong* hs_song = GetHighScoresForASong(song_id);
  if (hs_song == nullptr) {
    return;
  }

  FOREACH_ENUM(Grade, g) {
    for (const std::pair<const StepsID, HighScoresForASteps>& it :
         hs_song->steps_high_scores_) {
      const StepsID& id = it.first;
      if (!id.MatchesStepsType(st)) {
        continue;
      }

      const HighScoresForASteps& steps = it.second;
      if (steps.hsl_.GetTopScore().GetGrade() == g) {
        counts[g]++;
      }
    }
  }
}

// Course high scores
void Profile::AddCourseHighScore(
    const Course* course, const Trail* trail, HighScore hs, int& index_out) {
  GetCourseHighScoreList(course, trail)
      .AddHighScore(hs, index_out, IsMachine());
}

const HighScoreList& Profile::GetCourseHighScoreList(
    const Course* course, const Trail* trail) const {
  return ((Profile*)this)->GetCourseHighScoreList(course, trail);
}

HighScoreList& Profile::GetCourseHighScoreList(
    const Course* p_course, const Trail* p_trail) {
  CourseID course_id;
  course_id.FromCourse(p_course);

  TrailID trail_id;
  trail_id.FromTrail(p_trail);

  HighScoresForACourse& hs_course =
      course_high_scores_[course_id];  // operator[] inserts into map
  HighScoresForATrail& hs_trail =
      hs_course.trail_high_scores_[trail_id];  // operator[] inserts into map

  return hs_trail.hsl_;
}

int Profile::GetCourseNumTimesPlayed(const Course* course) const {
  CourseID course_id;
  course_id.FromCourse(course);

  return GetCourseNumTimesPlayed(course_id);
}

int Profile::GetCourseNumTimesPlayed(const CourseID& course_id) const {
  const HighScoresForACourse* course = GetHighScoresForACourse(course_id);
  if (course == nullptr) {
    return 0;
  }

  int total_num_times_played = 0;
  for (const std::pair<const TrailID, HighScoresForATrail>& j :
       course->trail_high_scores_) {
    const HighScoresForATrail& trail = j.second;

    total_num_times_played += trail.hsl_.GetNumTimesPlayed();
  }
  return total_num_times_played;
}

DateTime Profile::GetCourseLastPlayedDateTime(const Course* course) const {
  CourseID id;
  id.FromCourse(course);
  std::map<CourseID, HighScoresForACourse>::const_iterator iter =
      course_high_scores_.find(id);

  // don't call this unless has been played once
  ASSERT(iter != course_high_scores_.end());
  ASSERT(!iter->second.trail_high_scores_.empty());

  DateTime latest;  // starts out zeroed
  for (const std::pair<const TrailID, HighScoresForATrail>& i :
       iter->second.trail_high_scores_) {
    const HighScoreList& hsl = i.second.hsl_;
    if (hsl.GetNumTimesPlayed() == 0) {
      continue;
    }
    if (latest < hsl.GetLastPlayed()) {
      latest = hsl.GetLastPlayed();
    }
  }
  return latest;
}

void Profile::IncrementCoursePlayCount(
    const Course* course, const Trail* trail) {
  DateTime now = DateTime::GetNowDate();
  GetCourseHighScoreList(course, trail).IncrementPlayCount(now);
}

void Profile::GetAllUsedHighScoreNames(std::set<std::string>& names) {
#define GET_NAMES_FROM_MAP(                                                \
    main_member, main_key_type, main_value_type, sub_member, sub_key_type, \
    sub_value_type)                                                        \
  for (std::map<main_key_type, main_value_type>::iterator main_entry =     \
           main_member.begin();                                            \
       main_entry != main_member.end(); ++main_entry) {                    \
    for (std::map<sub_key_type, sub_value_type>::iterator sub_entry =      \
             main_entry->second.sub_member.begin();                        \
         sub_entry != main_entry->second.sub_member.end(); ++sub_entry) {  \
      for (std::vector<HighScore>::iterator high_score =                   \
               sub_entry->second.hsl_.high_scores_.begin();                \
           high_score != sub_entry->second.hsl_.high_scores_.end();        \
           ++high_score) {                                                 \
        if (high_score->GetName().size() > 0) {                            \
          names.insert(high_score->GetName());                             \
        }                                                                  \
      }                                                                    \
    }                                                                      \
  }
  GET_NAMES_FROM_MAP(
      song_high_scores_, SongID, HighScoresForASong, steps_high_scores_,
      StepsID, HighScoresForASteps);
  GET_NAMES_FROM_MAP(
      course_high_scores_, CourseID, HighScoresForACourse, trail_high_scores_,
      TrailID, HighScoresForATrail);
#undef GET_NAMES_FROM_MAP
}

// MergeScoresFromOtherProfile has three intended use cases:
// 1.  Restoring scores to the machine profile that were deleted because the
//   songs were not loaded.
// 2.  Migrating a profile from an older version of Stepmania, and adding its
//   scores to the machine profile.
// 3.  Merging two profiles that were separate together.
// In case 1, the various total numbers are still correct, so they should be
//   skipped.  This is why the skip_totals arg exists.
// -Kyz
void Profile::MergeScoresFromOtherProfile(
    Profile* other, bool skip_totals, const std::string& from_dir,
    const std::string& to_dir) {
  if (!skip_totals) {
#define MERGE_FIELD(field_name) field_name += other->field_name;
    MERGE_FIELD(total_sessions_);
    MERGE_FIELD(total_session_seconds_);
    MERGE_FIELD(total_gameplay_seconds_);
    MERGE_FIELD(total_calories_burned_);
    MERGE_FIELD(total_dance_points_);
    MERGE_FIELD(num_extra_stages_passed_);
    MERGE_FIELD(num_extra_stages_failed_);
    MERGE_FIELD(num_toasties_);
    MERGE_FIELD(total_taps_and_holds_);
    MERGE_FIELD(total_jumps_);
    MERGE_FIELD(total_holds_);
    MERGE_FIELD(total_rolls_);
    MERGE_FIELD(total_mines_);
    MERGE_FIELD(total_hands_);
    MERGE_FIELD(total_lifts_);
    FOREACH_ENUM(PlayMode, i) {
      MERGE_FIELD(num_songs_played_by_play_mode_[i]);
      MERGE_FIELD(num_stages_passed_by_play_mode_[i]);
    }
    FOREACH_ENUM(Difficulty, i) {
      MERGE_FIELD(num_songs_played_by_difficulty_[i]);
    }
    for (int i = 0; i < MAX_METER; ++i) {
      MERGE_FIELD(num_songs_played_by_meter_[i]);
    }
    MERGE_FIELD(num_total_songs_played_);
    FOREACH_ENUM(Grade, i) { MERGE_FIELD(num_stages_passed_by_grade_[i]); }
#undef MERGE_FIELD
    for (std::map<DateTime, Calories>::iterator other_cal =
             other->map_day_to_calories_burned_.begin();
         other_cal != other->map_day_to_calories_burned_.end(); ++other_cal) {
      std::map<DateTime, Calories>::iterator this_cal =
          map_day_to_calories_burned_.find(other_cal->first);
      if (this_cal == map_day_to_calories_burned_.end()) {
        map_day_to_calories_burned_[other_cal->first] = other_cal->second;
      } else {
        this_cal->second.cals_ += other_cal->second.cals_;
      }
    }
  }
#define MERGE_SCORES_IN_MEMBER(                                                \
    main_member, main_key_type, main_value_type, sub_member, sub_key_type,     \
    sub_value_type)                                                            \
  for (std::map<main_key_type, main_value_type>::iterator main_entry =         \
           other->main_member.begin();                                         \
       main_entry != other->main_member.end(); ++main_entry) {                 \
    std::map<main_key_type, main_value_type>::iterator this_entry =            \
        main_member.find(main_entry->first);                                   \
    if (this_entry == main_member.end()) {                                     \
      main_member[main_entry->first] = main_entry->second;                     \
    } else {                                                                   \
      for (std::map<sub_key_type, sub_value_type>::iterator sub_entry =        \
               main_entry->second.sub_member.begin();                          \
           sub_entry != main_entry->second.sub_member.end(); ++sub_entry) {    \
        std::map<sub_key_type, sub_value_type>::iterator this_sub =            \
            this_entry->second.sub_member.find(sub_entry->first);              \
        if (this_sub == this_entry->second.sub_member.end()) {                 \
          this_entry->second.sub_member[sub_entry->first] = sub_entry->second; \
        } else {                                                               \
          this_sub->second.hsl_.MergeFromOtherHSL(                             \
              sub_entry->second.hsl_, IsMachine());                            \
        }                                                                      \
      }                                                                        \
    }                                                                          \
  }
  MERGE_SCORES_IN_MEMBER(
      song_high_scores_, SongID, HighScoresForASong, steps_high_scores_,
      StepsID, HighScoresForASteps);
  MERGE_SCORES_IN_MEMBER(
      course_high_scores_, CourseID, HighScoresForACourse, trail_high_scores_,
      TrailID, HighScoresForATrail);
#undef MERGE_SCORES_IN_MEMBER
  // I think the machine profile should not have screenshots merged into it
  // because the intended use case is someone whose profile scores were
  // deleted off the machine by mishap, or a profile being migrated from an
  // older version of Stepmania.  Either way, the screenshots should stay
  // with the profile they came from.
  // In the case where two local profiles are being merged together, the user
  // is probably planning to delete the old profile after the merge, so we
  // want to copy the screenshots over. -Kyz
  if (!IsMachine()) {
    // The old screenshot count is stored so we know where to start in the
    // list when copying the screenshot images.
    size_t old_count = screenshots_.size();
    screenshots_.insert(
        screenshots_.end(), other->screenshots_.begin(),
        other->screenshots_.end());
    for (size_t sid = old_count; sid < screenshots_.size(); ++sid) {
      std::string old_path =
          from_dir + "Screenshots/" + screenshots_[sid].file_name_;
      std::string new_path =
          to_dir + "Screenshots/" + screenshots_[sid].file_name_;
      // Only move the old screenshot over if it exists and won't stomp an
      // existing screenshot.
      if (FILEMAN->DoesFileExist(old_path) &&
          (!FILEMAN->DoesFileExist(new_path))) {
        FILEMAN->Move(old_path, new_path);
      }
    }
    // The screenshots are kept sorted by date for ease of use, and
    // duplicates are removed because they come from the user mistakenly
    // merging a second time. -Kyz
    std::sort(screenshots_.begin(), screenshots_.end());
    std::vector<Screenshot>::iterator unique_end =
        std::unique(screenshots_.begin(), screenshots_.end());
    screenshots_.erase(unique_end, screenshots_.end());
  }
}

void Profile::swap(Profile& other) {
  // Type is skipped because this is meant to be used only on matching types,
  // to move profiles after the priorities have been assigned. -Kyz
  // A bit of a misnomer, since it actually works on any type that has its
  // own swap function, which includes the standard containers.
#define SWAP_STR_MEMBER(member_name) member_name.swap(other.member_name)
#define SWAP_GENERAL(member_name) std::swap(member_name, other.member_name)
#define SWAP_ARRAY(member_name, size)                \
  for (int i = 0; i < size; ++i) {                   \
    std::swap(member_name[i], other.member_name[i]); \
  }                                                  \
  SWAP_GENERAL(list_priority_);
  SWAP_STR_MEMBER(display_name_);
  SWAP_STR_MEMBER(character_id_);
  SWAP_STR_MEMBER(last_used_high_score_name_);
  SWAP_GENERAL(weight_pounds_);
  SWAP_GENERAL(voomax_);
  SWAP_GENERAL(birth_year_);
  SWAP_GENERAL(ignore_step_count_calories_);
  SWAP_GENERAL(is_male_);
  SWAP_STR_MEMBER(guid_);
  SWAP_GENERAL(current_combo_);
  SWAP_GENERAL(total_sessions_);
  SWAP_GENERAL(total_session_seconds_);
  SWAP_GENERAL(total_gameplay_seconds_);
  SWAP_GENERAL(total_calories_burned_);
  SWAP_GENERAL(goal_type_);
  SWAP_GENERAL(goal_calories_);
  SWAP_GENERAL(goal_seconds_);
  SWAP_GENERAL(total_dance_points_);
  SWAP_GENERAL(num_extra_stages_passed_);
  SWAP_GENERAL(num_extra_stages_failed_);
  SWAP_GENERAL(num_toasties_);
  SWAP_GENERAL(total_taps_and_holds_);
  SWAP_GENERAL(total_jumps_);
  SWAP_GENERAL(total_holds_);
  SWAP_GENERAL(total_rolls_);
  SWAP_GENERAL(total_mines_);
  SWAP_GENERAL(total_hands_);
  SWAP_GENERAL(total_lifts_);
  SWAP_GENERAL(new_profile_);
  SWAP_STR_MEMBER(unlocked_entry_ids_);
  SWAP_STR_MEMBER(last_played_machine_guid_);
  SWAP_GENERAL(creation_time_);
  SWAP_GENERAL(last_played_date_);
  SWAP_ARRAY(num_songs_played_by_play_mode_, NUM_PlayMode);
  SWAP_STR_MEMBER(num_songs_played_by_style_);
  SWAP_ARRAY(num_songs_played_by_difficulty_, NUM_Difficulty);
  SWAP_ARRAY(num_songs_played_by_meter_, MAX_METER + 1);
  SWAP_GENERAL(num_total_songs_played_);
  SWAP_ARRAY(num_stages_passed_by_play_mode_, NUM_PlayMode);
  SWAP_ARRAY(num_stages_passed_by_grade_, NUM_Grade);
  SWAP_GENERAL(user_table_);
  SWAP_STR_MEMBER(song_high_scores_);
  SWAP_STR_MEMBER(course_high_scores_);
  for (int st = 0; st < NUM_StepsType; ++st) {
    SWAP_ARRAY(category_high_scores_[st], NUM_RankingCategory);
  }
  SWAP_STR_MEMBER(screenshots_);
  SWAP_STR_MEMBER(map_day_to_calories_burned_);
#undef SWAP_STR_MEMBER
#undef SWAP_GENERAL
#undef SWAP_ARRAY
}

// Category high scores
void Profile::AddCategoryHighScore(
    StepsType st, RankingCategory rc, HighScore hs, int& index_out) {
  category_high_scores_[st][rc].AddHighScore(hs, index_out, IsMachine());
}

const HighScoreList& Profile::GetCategoryHighScoreList(
    StepsType st, RankingCategory rc) const {
  return ((Profile*)this)->category_high_scores_[st][rc];
}

HighScoreList& Profile::GetCategoryHighScoreList(
    StepsType st, RankingCategory rc) {
  return category_high_scores_[st][rc];
}

int Profile::GetCategoryNumTimesPlayed(StepsType st) const {
  int num_times_played = 0;
  FOREACH_ENUM(RankingCategory, rc)
  num_times_played += category_high_scores_[st][rc].GetNumTimesPlayed();
  return num_times_played;
}

void Profile::IncrementCategoryPlayCount(StepsType st, RankingCategory rc) {
  DateTime now = DateTime::GetNowDate();
  category_high_scores_[st][rc].IncrementPlayCount(now);
}

// Loading and saving
#define WARN_PARSER \
  ShowWarningOrTrace(__FILE__, __LINE__, "Error parsing file.", true)
#define WARN_AND_RETURN \
  {                     \
    WARN_PARSER;        \
    return;             \
  }
#define WARN_AND_CONTINUE \
  {                       \
    WARN_PARSER;          \
    continue;             \
  }
#define WARN_AND_BREAK \
  {                    \
    WARN_PARSER;       \
    break;             \
  }
#define WARN_M(m)     \
  ShowWarningOrTrace( \
      __FILE__, __LINE__, std::string("Error parsing file: ") + (m), true)
#define WARN_AND_CONTINUE_M(m) \
  {                            \
    WARN_M(m);                 \
    continue;                  \
  }
#define LOAD_NODE(X)                           \
  {                                            \
    const XNode* X = xml->GetChild(#X);        \
    if (X == nullptr)                          \
      LOG->Warn("Failed to read section " #X); \
    else                                       \
      Load##X##FromNode(X);                    \
  }

void Profile::LoadCustomFunction(std::string dir, PlayerNumber pn) {
  /* Get the theme's custom load function:
   *   [Profile]
   *   CustomLoadFunction=function(profile, profileDir) ... end
   */
  Lua* L = LUA->Get();
  LuaReference custom_load_func =
      THEME->GetMetricR("Profile", "CustomLoadFunction");
  custom_load_func.PushSelf(L);
  ASSERT_M(!lua_isnil(L, -1), "CustomLoadFunction not defined");

  // Pass profile and profile directory as arguments
  this->PushSelf(L);
  LuaHelpers::Push(L, dir);
  if (pn == PlayerNumber_Invalid) {
    lua_pushnil(L);
  } else {
    Enum::Push(L, pn);
  }

  // Run it
  std::string error = "Error running CustomLoadFunction: ";
  LuaHelpers::RunScriptOnStack(L, error, 3, 0, true);

  LUA->Release(L);
}

bool Profile::HandleStatsPrefixChange(std::string dir, bool require_signature) {
  // Temp variables to preserve stuff across the reload.
  // Some stuff intentionally left out because the original reason for the
  // stats prefix was to allow scores from different game types to coexist.
  std::string display_name = display_name_;
  std::string character_id = character_id_;
  std::string last_high_score_name = last_used_high_score_name_;
  int weight = weight_pounds_;
  float voomax = voomax_;
  int birth_year = birth_year_;
  bool ignore_step_cal = ignore_step_count_calories_;
  bool male = is_male_;
  ProfileType type = type_;
  int priority = list_priority_;
  std::string guid = guid_;
  std::map<std::string, std::string> default_mods = default_modifiers_;
  SortOrder sort_order = sort_order_;
  Difficulty last_diff = last_difficulty_;
  CourseDifficulty last_course_diff = last_course_difficulty_;
  StepsType last_stepstype = last_steps_type_;
  SongID last_song = last_song_;
  CourseID last_course = last_course_;
  int total_sessions = total_sessions_;
  int total_session_seconds = total_session_seconds_;
  int total_gameplay_seconds = total_gameplay_seconds_;
  float total_calories_burned = total_calories_burned_;
  LuaTable user_table = user_table_;
  bool need_to_create_file = false;
  if (IsAFile(dir + PROFILEMAN->GetStatsPrefix() + STATS_XML)) {
    LoadAllFromDir(dir, require_signature);
  } else {
    ClearStats();
    need_to_create_file = true;
  }
  display_name_ = display_name;
  character_id_ = character_id;
  last_used_high_score_name_ = last_high_score_name;
  weight_pounds_ = weight;
  voomax_ = voomax;
  birth_year_ = birth_year;
  ignore_step_count_calories_ = ignore_step_cal;
  is_male_ = male;
  type_ = type;
  list_priority_ = priority;
  guid_ = guid;
  default_modifiers_ = default_mods;
  sort_order_ = sort_order;
  last_difficulty_ = last_diff;
  last_course_difficulty_ = last_course_diff;
  last_steps_type_ = last_stepstype;
  last_song_ = last_song;
  total_sessions_ = total_sessions;
  total_session_seconds_ = total_session_seconds;
  total_gameplay_seconds_ = total_gameplay_seconds;
  total_calories_burned_ = total_calories_burned;
  user_table_ = user_table;
  if (need_to_create_file) {
    return SaveAllToDir(dir, require_signature);
  }

  return true;
}

ProfileLoadResult Profile::LoadAllFromDir(
    std::string dir, bool require_signature) {
  LOG->Trace("Profile::LoadAllFromDir( %s )", dir.c_str());

  ASSERT(Right(dir, 1) == "/");

  InitAll();

  LoadTypeFromDir(dir);
  // Not critical if this fails
  LoadEditableDataFromDir(dir);

  ProfileLoadResult ret = LoadStatsFromDir(dir, require_signature);
  if (ret != ProfileLoadResult_Success) {
    return ret;
  }

  LoadCustomFunction(dir, PlayerNumber_Invalid);

  return ProfileLoadResult_Success;
}

// Custom songs are not stored with all the normal songs because walking the
// entire song list to remove custom songs when unloading the profile is
// wasteful. -Kyz

void Profile::LoadSongsFromDir(
    const std::string& dir, ProfileSlot prof_slot, bool is_memory_card) {
  // Custom songs not enabled or not loading from MemoryCard
  if (!PREFSMAN->custom_songs_enable_ || !is_memory_card) {
    return;
  }

  RageTimer song_load_start_time;
  song_load_start_time.Touch();

  groups_.clear();

  // Songs contains individual songs, Packs contains packs of songs
  groups_.push_back(new Group(dir + "Songs", "", prof_slot));
  std::vector<std::string> pack_dirs;
  FILEMAN->GetDirListing(dir + PACKS_SUBDIR + "*", pack_dirs, true, true);
  StripCvsAndSvn(pack_dirs);
  StripMacResourceForks(pack_dirs);
  for (const std::string& pack_dir : pack_dirs) {
    groups_.push_back(new Group(pack_dir, Basename(pack_dir), prof_slot));
  }

  // Only songs that are successfully loaded count towards the limit. -Kyz
  auto load_limit_reached = [&]() {
    return songs_.size() >= PREFSMAN->custom_songs_max_count_ ||
           song_load_start_time.Ago() > PREFSMAN->custom_songs_load_timeout_;
  };

  for (const Group* group : groups_) {
    // Don't keep listing pack directories once no more songs can load.
    if (load_limit_reached()) {
      break;
    }

    std::vector<std::string> song_dirs;
    FILEMAN->GetDirListing(group->GetPath() + "/*", song_dirs, true, true);
    StripCvsAndSvn(song_dirs);
    StripMacResourceForks(song_dirs);
    LOG->Trace(
        "Found %zu song directories in profile group %s.", song_dirs.size(),
        group->GetGroupName().c_str());

    for (const std::string& song_dir : song_dirs) {
      if (load_limit_reached()) {
        break;
      }

      auto song = std::make_unique<Song>();
      if (!song->LoadFromSongDir(
              song_dir, false, prof_slot, group->GetGroupName())) {
        LOG->Trace("Song %s failed to load.", song_dir.c_str());
        continue;
      }

      song->SetEnabled(true);
      songs_.push_back(song.get());

      // link songs with groupName to find them easily
      songs_groups_[song->group_name_].push_back(song.release());
    }
  }

  float load_time = song_load_start_time.Ago();
  LOG->Trace(
      "Successfully loaded %zu songs in %.6f from profile.", songs_.size(),
      load_time);

  // Remove groups with no songs
  for (int i = int(groups_.size()) - 1; i >= 0; --i) {
    Group* group = groups_[i];

    if (songs_groups_.find(group->GetGroupName()) == songs_groups_.end()) {
      delete group;
      groups_.erase(groups_.begin() + i);
    }
  }
}

ProfileLoadResult Profile::LoadStatsFromDir(
    std::string dir, bool require_signature) {
  dir = dir + PROFILEMAN->GetStatsPrefix();
  // Check for the existance of stats.xml
  std::string fn = dir + STATS_XML;
  bool compressed = false;
  if (!IsAFile(fn)) {
    // Check for the existance of stats.xml.gz
    fn = dir + STATS_XML_GZ;
    compressed = true;
    if (!IsAFile(fn)) {
      return ProfileLoadResult_FailedNoProfile;
    }
  }

  int i_error;
  std::unique_ptr<RageFileBasic> file(
      FILEMAN->Open(fn, RageFile::READ, i_error));
  if (file.get() == nullptr) {
    LOG->Trace("Error opening %s: %s", fn.c_str(), strerror(i_error));
    return ProfileLoadResult_FailedTampered;
  }

  if (compressed) {
    std::string s_error;
    uint32_t crc32;
    RageFileObjInflate* inflate = GunzipFile(file.release(), s_error, &crc32);
    if (inflate == nullptr) {
      LOG->Trace("Error opening %s: %s", fn.c_str(), s_error.c_str());
      return ProfileLoadResult_FailedTampered;
    }

    file.reset(inflate);
  }

  // Don't load unreasonably large stats.xml files.
  if (!IsMachine())  // only check stats coming from the player
  {
    int bytes = file->GetFileSize();
    if (bytes > MAX_PLAYER_STATS_XML_SIZE_BYTES) {
      LuaHelpers::ReportScriptErrorFmt(
          "The file '%s' is unreasonably large.  It won't be loaded.",
          fn.c_str());
      return ProfileLoadResult_FailedTampered;
    }
  }

  if (require_signature) {
    std::string stats_xml_sig_file = fn + SIGNATURE_APPEND;
    std::string dont_share_file = dir + DONT_SHARE_SIG;

    LOG->Trace(
        "Verifying don't share signature \"%s\" against \"%s\"",
        dont_share_file.c_str(), stats_xml_sig_file.c_str());
    // verify the stats.xml signature with the "don't share" file
    if (!CryptManager::VerifyFileWithFile(
            stats_xml_sig_file, dont_share_file)) {
      LuaHelpers::ReportScriptErrorFmt(
          "The don't share check for '%s' failed.  Data will be ignored.",
          stats_xml_sig_file.c_str());
      return ProfileLoadResult_FailedTampered;
    }
    LOG->Trace("Done.");

    // verify stats.xml
    LOG->Trace("Verifying stats.xml signature");
    if (!CryptManager::VerifyFileWithFile(fn, stats_xml_sig_file)) {
      LuaHelpers::ReportScriptErrorFmt(
          "The signature check for '%s' failed.  Data will be ignored.",
          fn.c_str());
      return ProfileLoadResult_FailedTampered;
    }
    LOG->Trace("Done.");
  }

  LOG->Trace("Loading %s", fn.c_str());
  XNode xml;
  if (!XmlFileUtil::LoadFromFileShowErrors(xml, *file.get())) {
    return ProfileLoadResult_FailedTampered;
  }
  LOG->Trace("Done.");

  return LoadStatsXmlFromNode(&xml);
}

void Profile::LoadTypeFromDir(std::string dir) {
  type_ = ProfileType_Normal;
  list_priority_ = 0;
  creation_time_.Init();
  last_played_date_.Init();
  std::string fn = dir + TYPE_INI;
  if (FILEMAN->DoesFileExist(fn)) {
    IniFile ini;
    if (ini.ReadFile(fn)) {
      const XNode* data = ini.GetChild("ListPosition");
      if (data != nullptr) {
        std::string type_str;
        if (data->GetAttrValue("Type", type_str)) {
          type_ = StringToProfileType(type_str);
          if (type_ >= NUM_ProfileType) {
            type_ = ProfileType_Normal;
          }
        }
        data->GetAttrValue("Priority", list_priority_);
        std::string creation_date_str;
        if (data->GetAttrValue("CreationTime", creation_date_str)) {
          creation_time_.FromString(creation_date_str);
        }
        std::string date_str;
        if (data->GetAttrValue("LastPlayedDate", date_str)) {
          last_played_date_.FromString(date_str);
        }
        // Backward compatibility for profiles created before CreationTime
        // existed in Type.ini.
        if (creation_time_ == DateTime()) {
          creation_time_ = last_played_date_;
        }
      }
    }
  }
}

ProfileLoadResult Profile::LoadStatsXmlFromNode(
    const XNode* xml, bool ignore_editable) {
  /* The placeholder stats.xml file has an <html> tag. Don't load it,
   * but don't warn about it. */
  if (xml->GetName() == "html") {
    return ProfileLoadResult_FailedNoProfile;
  }

  if (xml->GetName() != "Stats") {
    WARN_M(xml->GetName());
    return ProfileLoadResult_FailedTampered;
  }

  // These are loaded from Editable, so we usually want to ignore them here.
  std::string name = display_name_;
  std::string character_id = character_id_;
  std::string last_used_high_score_name = last_used_high_score_name_;
  int weight_pounds = weight_pounds_;
  float voomax = voomax_;
  int birth_year = birth_year_;
  bool ignore_step_count_calories = ignore_step_count_calories_;
  bool is_male = is_male_;

  LOAD_NODE(GeneralData);
  LOAD_NODE(SongScores);
  LOAD_NODE(CourseScores);
  LOAD_NODE(CategoryScores);
  LOAD_NODE(ScreenshotData);
  LOAD_NODE(CalorieData);

  if (ignore_editable) {
    display_name_ = name;
    character_id_ = character_id;
    last_used_high_score_name_ = last_used_high_score_name;
    weight_pounds_ = weight_pounds;
    voomax_ = voomax;
    birth_year_ = birth_year;
    ignore_step_count_calories_ = ignore_step_count_calories;
    is_male_ = is_male;
  }

  return ProfileLoadResult_Success;
}

bool Profile::SaveAllToDir(std::string dir, bool sign_data) const {
  last_played_machine_guid_ = PROFILEMAN->GetMachineProfile()->guid_;
  last_played_date_ = DateTime::GetNowDate();

  SaveTypeToDir(dir);
  // Save editable.ini
  SaveEditableDataToDir(dir);

  bool saved = SaveStatsXmlToDir(dir, sign_data);

  SaveStatsWebPageToDir(dir);

  // Empty directories if none exist.
  if (ProfileManager::profile_step_edits_) {
    FILEMAN->CreateDir(dir + EDIT_STEPS_SUBDIR);
  }
  if (ProfileManager::profile_course_edits_) {
    FILEMAN->CreateDir(dir + EDIT_COURSES_SUBDIR);
  }
  if (PREFSMAN->custom_songs_enable_) {
    FILEMAN->CreateDir(dir + SONGS_SUBDIR);
    FILEMAN->CreateDir(dir + PACKS_SUBDIR + "My Pack/");
  }
  FILEMAN->CreateDir(dir + SCREENSHOTS_SUBDIR);
  FILEMAN->CreateDir(dir + RIVAL_SUBDIR);

  /* Get the theme's custom save function:
   *   [Profile]
   *   CustomSaveFunction=function(profile, profileDir) ... end
   */
  Lua* L = LUA->Get();
  LuaReference custom_save_func =
      THEME->GetMetricR("Profile", "CustomSaveFunction");
  custom_save_func.PushSelf(L);
  ASSERT_M(!lua_isnil(L, -1), "CustomSaveFunction not defined");

  // Pass profile and profile directory as arguments
  const_cast<Profile*>(this)->PushSelf(L);
  LuaHelpers::Push(L, dir);

  // Run it
  std::string error = "Error running CustomSaveFunction: ";
  LuaHelpers::RunScriptOnStack(L, error, 2, 0, true);

  LUA->Release(L);

  return saved;
}

XNode* Profile::SaveStatsXmlCreateNode() const {
  XNode* xml = new XNode("Stats");

  xml->AppendChild(SaveGeneralDataCreateNode());
  xml->AppendChild(SaveSongScoresCreateNode());
  xml->AppendChild(SaveCourseScoresCreateNode());
  xml->AppendChild(SaveCategoryScoresCreateNode());
  xml->AppendChild(SaveScreenshotDataCreateNode());
  xml->AppendChild(SaveCalorieDataCreateNode());
  if (SHOW_COIN_DATA.GetValue() && IsMachine()) {
    xml->AppendChild(SaveCoinDataCreateNode());
  }

  return xml;
}

bool Profile::SaveStatsXmlToDir(std::string dir, bool sign_data) const {
  LOG->Trace("SaveStatsXmlToDir: %s", dir.c_str());
  std::unique_ptr<XNode> xml(SaveStatsXmlCreateNode());

  dir = dir + PROFILEMAN->GetStatsPrefix();
  // Save stats.xml
  std::string fn = dir + (g_bProfileDataCompress ? STATS_XML_GZ : STATS_XML);

  {
    std::string error;
    RageFile f;
    if (!f.Open(fn, RageFile::WRITE)) {
      LuaHelpers::ReportScriptErrorFmt(
          "Couldn't open %s for writing: %s", fn.c_str(), f.GetError().c_str());
      return false;
    }

    if (g_bProfileDataCompress) {
      RageFileObjGzip gzip(&f);
      gzip.Start();
      if (!XmlFileUtil::SaveToFile(xml.get(), gzip, "", false)) {
        return false;
      }

      if (gzip.Finish() == -1) {
        return false;
      }

      /* After successfully saving STATS_XML_GZ, remove any stray STATS_XML. */
      if (FILEMAN->IsAFile(dir + STATS_XML)) {
        FILEMAN->Remove(dir + STATS_XML);
      }
    } else {
      if (!XmlFileUtil::SaveToFile(xml.get(), f, "", false)) {
        return false;
      }

      /* After successfully saving STATS_XML, remove any stray STATS_XML_GZ. */
      if (FILEMAN->IsAFile(dir + STATS_XML_GZ)) {
        FILEMAN->Remove(dir + STATS_XML_GZ);
      }
    }
  }

  if (sign_data) {
    std::string stats_xml_sig_file = fn + SIGNATURE_APPEND;
    CryptManager::SignFileToFile(fn, stats_xml_sig_file);

    // Save the "don't share" file
    std::string dont_share_file = dir + DONT_SHARE_SIG;
    CryptManager::SignFileToFile(stats_xml_sig_file, dont_share_file);
  }

  return true;
}

void Profile::SaveTypeToDir(std::string dir) const {
  IniFile ini;
  if (creation_time_ == DateTime()) {
    creation_time_ = DateTime::GetNowDateTime();
  }
  ini.SetValue("ListPosition", "Type", ProfileTypeToString(type_));
  ini.SetValue("ListPosition", "Priority", list_priority_);
  ini.SetValue("ListPosition", "CreationTime", creation_time_.GetString());
  ini.SetValue(
      "ListPosition", "LastPlayedDate", DateTime::GetNowDateTime().GetString());
  ini.WriteFile(dir + TYPE_INI);
}

void Profile::SaveEditableDataToDir(std::string dir) const {
  IniFile ini;

  ini.SetValue("Editable", "DisplayName", display_name_);
  ini.SetValue("Editable", "CharacterID", character_id_);
  ini.SetValue("Editable", "LastUsedHighScoreName", last_used_high_score_name_);
  ini.SetValue("Editable", "WeightPounds", weight_pounds_);
  ini.SetValue("Editable", "Voomax", voomax_);
  ini.SetValue("Editable", "BirthYear", birth_year_);
  ini.SetValue(
      "Editable", "IgnoreStepCountCalories", ignore_step_count_calories_);
  ini.SetValue("Editable", "IsMale", is_male_);

  ini.WriteFile(dir + EDITABLE_INI);
}

XNode* Profile::SaveGeneralDataCreateNode() const {
  XNode* general_data_node = new XNode("GeneralData");

  // TRICKY: These are write-only elements that are normally never read again.
  // This data is required by other apps (like internet ranking), but is
  // redundant to the game app.
  general_data_node->AppendChild(
      "DisplayName", GetDisplayNameOrHighScoreName());
  general_data_node->AppendChild("CharacterID", character_id_);
  general_data_node->AppendChild(
      "LastUsedHighScoreName", last_used_high_score_name_);
  general_data_node->AppendChild("WeightPounds", weight_pounds_);
  general_data_node->AppendChild("Voomax", voomax_);
  general_data_node->AppendChild("BirthYear", birth_year_);
  general_data_node->AppendChild(
      "IgnoreStepCountCalories", ignore_step_count_calories_);
  general_data_node->AppendChild("IsMale", is_male_);

  general_data_node->AppendChild("IsMachine", IsMachine());

  general_data_node->AppendChild("Guid", guid_);
  general_data_node->AppendChild("SortOrder", SortOrderToString(sort_order_));
  general_data_node->AppendChild(
      "LastDifficulty", DifficultyToString(last_difficulty_));
  general_data_node->AppendChild(
      "LastCourseDifficulty", DifficultyToString(last_course_difficulty_));
  if (last_steps_type_ != StepsType_Invalid) {
    general_data_node->AppendChild(
        "LastStepsType", GAMEMAN->GetStepsTypeInfo(last_steps_type_).name_);
  }
  general_data_node->AppendChild(last_song_.CreateNode());
  general_data_node->AppendChild(last_course_.CreateNode());
  general_data_node->AppendChild("CurrentCombo", current_combo_);
  general_data_node->AppendChild("TotalSessions", total_sessions_);
  general_data_node->AppendChild("TotalSessionSeconds", total_session_seconds_);
  general_data_node->AppendChild(
      "TotalGameplaySeconds", total_gameplay_seconds_);
  general_data_node->AppendChild("TotalCaloriesBurned", total_calories_burned_);
  general_data_node->AppendChild("GoalType", goal_type_);
  general_data_node->AppendChild("GoalCalories", goal_calories_);
  general_data_node->AppendChild("GoalSeconds", goal_seconds_);
  general_data_node->AppendChild(
      "LastPlayedMachineGuid", last_played_machine_guid_);
  general_data_node->AppendChild(
      "LastPlayedDate", last_played_date_.GetString());
  general_data_node->AppendChild("TotalDancePoints", total_dance_points_);
  general_data_node->AppendChild(
      "NumExtraStagesPassed", num_extra_stages_passed_);
  general_data_node->AppendChild(
      "NumExtraStagesFailed", num_extra_stages_failed_);
  general_data_node->AppendChild("NumToasties", num_toasties_);
  general_data_node->AppendChild("TotalTapsAndHolds", total_taps_and_holds_);
  general_data_node->AppendChild("TotalJumps", total_jumps_);
  general_data_node->AppendChild("TotalHolds", total_holds_);
  general_data_node->AppendChild("TotalRolls", total_rolls_);
  general_data_node->AppendChild("TotalMines", total_mines_);
  general_data_node->AppendChild("TotalHands", total_hands_);
  general_data_node->AppendChild("TotalLifts", total_lifts_);

  // Keep declared variables in a very local scope so they aren't
  // accidentally used where they're not intended.  There's a lot of
  // copying and pasting in this code.

  {
    XNode* default_modifiers =
        general_data_node->AppendChild("DefaultModifiers");
    for (std::pair<const std::string&, std::string> it : default_modifiers_) {
      default_modifiers->AppendChild(it.first, it.second);
    }
  }

  {
    XNode* unlocks = general_data_node->AppendChild("Unlocks");
    for (const std::string& unlock_entry : unlocked_entry_ids_) {
      XNode* entry = unlocks->AppendChild("UnlockEntry");
      entry->AppendAttr("UnlockEntryID", unlock_entry);
      if (!UNLOCK_AUTH_STRING.GetValue().empty()) {
        std::string unlock_auth = BinaryToHex(CRYPTMAN->GetMD5ForString(
            unlock_entry + UNLOCK_AUTH_STRING.GetValue()));
        entry->AppendAttr("Auth", unlock_auth);
      }
    }
  }

  {
    XNode* num_songs_played_by_play_mode =
        general_data_node->AppendChild("NumSongsPlayedByPlayMode");
    FOREACH_ENUM(PlayMode, pm) {
      // Don't save unplayed PlayModes.
      if (!num_songs_played_by_play_mode_[pm]) {
        continue;
      }
      num_songs_played_by_play_mode->AppendChild(
          PlayModeToString(pm), num_songs_played_by_play_mode_[pm]);
    }
  }

  {
    XNode* num_songs_played_by_style =
        general_data_node->AppendChild("NumSongsPlayedByStyle");
    for (const std::pair<const StyleID, int>& iter :
         num_songs_played_by_style_) {
      const StyleID& s = iter.first;
      int num_plays = iter.second;

      XNode* style_node = s.CreateNode();
      style_node->AppendAttr(XNode::kTextAttribute, num_plays);

      num_songs_played_by_style->AppendChild(style_node);
    }
  }

  {
    XNode* num_songs_played_by_difficulty =
        general_data_node->AppendChild("NumSongsPlayedByDifficulty");
    FOREACH_ENUM(Difficulty, dc) {
      if (!num_songs_played_by_difficulty_[dc]) {
        continue;
      }
      num_songs_played_by_difficulty->AppendChild(
          DifficultyToString(dc), num_songs_played_by_difficulty_[dc]);
    }
  }

  {
    XNode* num_songs_played_by_meter =
        general_data_node->AppendChild("NumSongsPlayedByMeter");
    for (int i = 0; i < MAX_METER + 1; i++) {
      if (!num_songs_played_by_meter_[i]) {
        continue;
      }
      num_songs_played_by_meter->AppendChild(
          ssprintf("Meter%d", i), num_songs_played_by_meter_[i]);
    }
  }

  general_data_node->AppendChild(
      "NumTotalSongsPlayed", num_total_songs_played_);

  {
    XNode* num_stages_passed_by_play_mode =
        general_data_node->AppendChild("NumStagesPassedByPlayMode");
    FOREACH_ENUM(PlayMode, pm) {
      // Don't save unplayed PlayModes.
      if (!num_stages_passed_by_play_mode_[pm]) {
        continue;
      }
      num_stages_passed_by_play_mode->AppendChild(
          PlayModeToString(pm), num_stages_passed_by_play_mode_[pm]);
    }
  }

  {
    XNode* num_stages_passed_by_grade =
        general_data_node->AppendChild("NumStagesPassedByGrade");
    FOREACH_ENUM(Grade, g) {
      if (!num_stages_passed_by_grade_[g]) {
        continue;
      }
      num_stages_passed_by_grade->AppendChild(
          GradeToString(g), num_stages_passed_by_grade_[g]);
    }
  }

  // Load Lua UserTable from profile
  if (!IsMachine() && user_table_.IsSet()) {
    Lua* L = LUA->Get();
    user_table_.PushSelf(L);
    XNode* user_table = XmlFileUtil::XNodeFromTable(L);
    LUA->Release(L);

    // XXX: XNodeFromTable returns a root node with the name "Layer".
    user_table->name_ = "UserTable";
    general_data_node->AppendChild(user_table);
  }

  return general_data_node;
}

ProfileLoadResult Profile::LoadEditableDataFromDir(std::string dir) {
  std::string fn = dir + EDITABLE_INI;

  // Don't load unreasonably large editable.xml files.
  int bytes = FILEMAN->GetFileSizeInBytes(fn);
  if (bytes > MAX_EDITABLE_INI_SIZE_BYTES) {
    LuaHelpers::ReportScriptErrorFmt(
        "The file '%s' is unreasonably large. It won't be loaded.", fn.c_str());
    return ProfileLoadResult_FailedTampered;
  }

  if (!IsAFile(fn)) {
    return ProfileLoadResult_FailedNoProfile;
  }

  IniFile ini;
  ini.ReadFile(fn);

  ini.GetValue("Editable", "DisplayName", display_name_);
  ini.GetValue("Editable", "CharacterID", character_id_);
  ini.GetValue("Editable", "LastUsedHighScoreName", last_used_high_score_name_);
  ini.GetValue("Editable", "WeightPounds", weight_pounds_);
  ini.GetValue("Editable", "Voomax", voomax_);
  ini.GetValue("Editable", "BirthYear", birth_year_);
  ini.GetValue(
      "Editable", "IgnoreStepCountCalories", ignore_step_count_calories_);
  ini.GetValue("Editable", "IsMale", is_male_);

  // This is data that the user can change, so we have to validate it.
  std::wstring wstr = RStringToWstring(display_name_);
  if (wstr.size() > PROFILE_MAX_DISPLAY_NAME_LENGTH) {
    wstr = wstr.substr(0, PROFILE_MAX_DISPLAY_NAME_LENGTH);
  }
  display_name_ = WStringToRString(wstr);
  // TODO: strip invalid chars?
  if (weight_pounds_ != 0) {
    rage_clamp(weight_pounds_, 20, 1000);
  }

  return ProfileLoadResult_Success;
}

void Profile::LoadGeneralDataFromNode(const XNode* node) {
  ASSERT(node->GetName() == "GeneralData");

  std::string s;
  const XNode* temp;

  node->GetChildValue("DisplayName", display_name_);
  node->GetChildValue("CharacterID", character_id_);
  node->GetChildValue("LastUsedHighScoreName", last_used_high_score_name_);
  node->GetChildValue("WeightPounds", weight_pounds_);
  node->GetChildValue("Voomax", voomax_);
  node->GetChildValue("BirthYear", birth_year_);
  node->GetChildValue("IgnoreStepCountCalories", ignore_step_count_calories_);
  node->GetChildValue("IsMale", is_male_);
  node->GetChildValue("Guid", guid_);
  node->GetChildValue("SortOrder", s);
  sort_order_ = StringToSortOrder(s);
  node->GetChildValue("LastDifficulty", s);
  last_difficulty_ = StringToDifficulty(s);
  node->GetChildValue("LastCourseDifficulty", s);
  last_course_difficulty_ = StringToDifficulty(s);
  node->GetChildValue("LastStepsType", s);
  last_steps_type_ = GAMEMAN->StringToStepsType(s);
  temp = node->GetChild("Song");
  if (temp) {
    last_song_.LoadFromNode(temp);
  }
  temp = node->GetChild("Course");
  if (temp) {
    last_course_.LoadFromNode(temp);
  }
  node->GetChildValue("CurrentCombo", current_combo_);
  node->GetChildValue("TotalSessions", total_sessions_);
  node->GetChildValue("TotalSessionSeconds", total_session_seconds_);
  node->GetChildValue("TotalGameplaySeconds", total_gameplay_seconds_);
  node->GetChildValue("TotalCaloriesBurned", total_calories_burned_);
  node->GetChildValue("GoalType", *ConvertValue<int>(&goal_type_));
  node->GetChildValue("GoalCalories", goal_calories_);
  node->GetChildValue("GoalSeconds", goal_seconds_);
  node->GetChildValue("LastPlayedMachineGuid", last_played_machine_guid_);
  node->GetChildValue("LastPlayedDate", s);
  last_played_date_.FromString(s);
  node->GetChildValue("TotalDancePoints", total_dance_points_);
  node->GetChildValue("NumExtraStagesPassed", num_extra_stages_passed_);
  node->GetChildValue("NumExtraStagesFailed", num_extra_stages_failed_);
  node->GetChildValue("NumToasties", num_toasties_);
  node->GetChildValue("TotalTapsAndHolds", total_taps_and_holds_);
  node->GetChildValue("TotalJumps", total_jumps_);
  node->GetChildValue("TotalHolds", total_holds_);
  node->GetChildValue("TotalRolls", total_rolls_);
  node->GetChildValue("TotalMines", total_mines_);
  node->GetChildValue("TotalHands", total_hands_);
  node->GetChildValue("TotalLifts", total_lifts_);

  {
    const XNode* default_modifiers = node->GetChild("DefaultModifiers");
    if (default_modifiers) {
      FOREACH_CONST_Child(default_modifiers, game_type) {
        game_type->GetTextValue(default_modifiers_[game_type->GetName()]);
      }
    }
  }

  {
    const XNode* unlocks = node->GetChild("Unlocks");
    if (unlocks) {
      FOREACH_CONST_Child(unlocks, unlock) {
        std::string unlock_entry_id;
        if (!unlock->GetAttrValue("UnlockEntryID", unlock_entry_id)) {
          continue;
        }

        if (!UNLOCK_AUTH_STRING.GetValue().empty()) {
          std::string unlock_auth;
          if (!unlock->GetAttrValue("Auth", unlock_auth)) {
            continue;
          }

          std::string expected_unlock_auth =
              BinaryToHex(CRYPTMAN->GetMD5ForString(
                  unlock_entry_id + UNLOCK_AUTH_STRING.GetValue()));
          if (unlock_auth != expected_unlock_auth) {
            continue;
          }
        }

        unlocked_entry_ids_.insert(unlock_entry_id);
      }
    }
  }

  {
    const XNode* num_songs_played_by_play_mode =
        node->GetChild("NumSongsPlayedByPlayMode");
    if (num_songs_played_by_play_mode) {
      FOREACH_ENUM(PlayMode, pm)
      num_songs_played_by_play_mode->GetChildValue(
          PlayModeToString(pm), num_songs_played_by_play_mode_[pm]);
    }
  }

  {
    const XNode* num_songs_played_by_style =
        node->GetChild("NumSongsPlayedByStyle");
    if (num_songs_played_by_style) {
      FOREACH_CONST_Child(num_songs_played_by_style, style) {
        if (style->GetName() != "Style") {
          continue;
        }

        StyleID id;
        id.LoadFromNode(style);

        if (!id.IsValid()) {
          WARN_AND_CONTINUE;
        }

        style->GetTextValue(num_songs_played_by_style_[id]);
      }
    }
  }

  {
    const XNode* num_songs_played_by_difficulty =
        node->GetChild("NumSongsPlayedByDifficulty");
    if (num_songs_played_by_difficulty) {
      FOREACH_ENUM(Difficulty, dc)
      num_songs_played_by_difficulty->GetChildValue(
          DifficultyToString(dc), num_songs_played_by_difficulty_[dc]);
    }
  }

  {
    const XNode* num_songs_played_by_meter =
        node->GetChild("NumSongsPlayedByMeter");
    if (num_songs_played_by_meter) {
      for (int i = 0; i < MAX_METER + 1; i++) {
        num_songs_played_by_meter->GetChildValue(
            ssprintf("Meter%d", i), num_songs_played_by_meter_[i]);
      }
    }
  }

  node->GetChildValue("NumTotalSongsPlayed", num_total_songs_played_);

  {
    const XNode* num_stages_passed_by_grade =
        node->GetChild("NumStagesPassedByGrade");
    if (num_stages_passed_by_grade) {
      FOREACH_ENUM(Grade, g)
      num_stages_passed_by_grade->GetChildValue(
          GradeToString(g), num_stages_passed_by_grade_[g]);
    }
  }

  {
    const XNode* num_stages_passed_by_play_mode =
        node->GetChild("NumStagesPassedByPlayMode");
    if (num_stages_passed_by_play_mode) {
      FOREACH_ENUM(PlayMode, pm)
      num_stages_passed_by_play_mode->GetChildValue(
          PlayModeToString(pm), num_stages_passed_by_play_mode_[pm]);
    }
  }

  // Build the custom data table from the existing XNode.
  if (!IsMachine()) {
    const XNode* user_table = node->GetChild("UserTable");

    Lua* L = LUA->Get();

    // If we have custom data, load it. Otherwise, make a blank table.
    if (user_table) {
      LuaHelpers::CreateTableFromXNode(L, user_table);
    } else {
      lua_newtable(L);
    }

    user_table_.SetFromStack(L);
    LUA->Release(L);
  }
}

void Profile::AddStepTotals(
    int total_taps_and_holds, int total_jumps, int total_holds, int total_rolls,
    int total_mines, int total_hands, int total_lifts, float calories_burned) {
  total_taps_and_holds_ += total_taps_and_holds;
  total_jumps_ += total_jumps;
  total_holds_ += total_holds;
  total_rolls_ += total_rolls;
  total_mines_ += total_mines;
  total_hands_ += total_hands;
  total_lifts_ += total_lifts;

  if (!ignore_step_count_calories_) {
    total_calories_burned_ += calories_burned;
    DateTime date = DateTime::GetNowDate();
    map_day_to_calories_burned_[date].cals_ += calories_burned;
  }
}

// It's a bit unclean to have this flag for routing around the old step count
// based calorie calculation, but I can't think of a better way to do it.
// AddStepTotals is called (through a couple layers) by CommitStageStats at
// the end of ScreenGameplay, so it can't be moved to somewhere else.  The
// player can't put in their heart rate for calculation until after
// ScreenGameplay
void Profile::AddCaloriesToDailyTotal(float cals) {
  total_calories_burned_ += cals;
  DateTime date = DateTime::GetNowDate();
  map_day_to_calories_burned_[date].cals_ += cals;
}

float Profile::CalculateCaloriesFromHeartRate(
    float heart_rate, float duration) {
  // Copied from
  // http://www.shapesense.com/fitness-exercise/calculators/heart-rate-based-calorie-burn-calculator.aspx
  /*
          Male: ((-55.0969 + (0.6309 x HR) + (0.1988 x W) + (0.2017 x A))/4.184)
     x T Female: ((-20.4022 + (0.4472 x HR) - (0.1263 x W) + (0.074 x A))/4.184)
     x T where

          HR = Heart rate (in beats/minute)
          W = Weight (in kilograms)
          A = Age (in years)
          T = Exercise duration time (in minutes)

          Equations for Determination of Calorie Burn if VO2max is Known

          Male: ((-95.7735 + (0.634 x HR) + (0.404 x VO2max) + (0.394 x W) +
     (0.271 x A))/4.184) x T Female: ((-59.3954 + (0.45 x HR) + (0.380 x VO2max)
     + (0.103 x W) + (0.274 x A))/4.184) x T where

          HR = Heart rate (in beats/minute)
          VO2max = Maximal oxygen consumption (in mL•kg-1•min-1)
          W = Weight (in kilograms)
          A = Age (in years)
          T = Exercise duration time (in minutes)
  */
  // Duration passed in is in seconds.  Convert it to minutes to make the code
  // match the equations from the website.
  duration = duration / 60.0;
  float kilos = GetCalculatedWeightPounds() / 2.205;
  float age = GetAge();

  // Names for the constants in the equations.
  // Assumes male and unknown voomax.
  float gender_factor = -55.0969f;
  float heart_factor = 0.6309f;
  float voo_factor = 0.0f;
  float weight_factor = 0.1988f;
  float age_factor = 0.2017f;
  if (voomax_ > 0) {
    if (is_male_) {
      gender_factor = -95.7735f;
      heart_factor = 0.634f;
      voo_factor = 0.404f;
      weight_factor = 0.394f;
      age_factor = 0.271f;
    } else {
      gender_factor = -59.3954f;
      heart_factor = 0.45f;
      voo_factor = 0.380f;
      weight_factor = 0.103f;
      age_factor = 0.274f;
    }
  } else if (!is_male_) {
    gender_factor = -20.4022f;
    heart_factor = 0.6309f;
    weight_factor = 0.1988f;
    age_factor = 0.2017f;
  }
  return ((gender_factor + (heart_factor * heart_rate) +
           (voo_factor * voomax_) + (weight_factor * kilos) +
           (age_factor + age)) /
          4.184) *
         duration;
}

XNode* Profile::SaveSongScoresCreateNode() const {
  CHECKPOINT_M("Getting the node to save song scores.");

  const Profile* profile = this;
  ASSERT(profile != nullptr);

  XNode* node = new XNode("SongScores");

  for (const std::pair<const SongID, HighScoresForASong>& i :
       song_high_scores_) {
    const SongID& song_id = i.first;
    const HighScoresForASong& song = i.second;

    // skip songs that have never been played
    if (profile->GetSongNumTimesPlayed(song_id) == 0) {
      continue;
    }

    XNode* song_node = node->AppendChild(song_id.CreateNode());

    int j_check2 = song.steps_high_scores_.size();
    int j_check1 = 0;
    for (const std::pair<const StepsID, HighScoresForASteps>& j :
         song.steps_high_scores_) {
      j_check1++;
      ASSERT(j_check1 <= j_check2);
      const StepsID& steps_id = j.first;
      const HighScoresForASteps& steps = j.second;

      const HighScoreList& hsl = steps.hsl_;

      // skip steps that have never been played
      if (hsl.GetNumTimesPlayed() == 0) {
        continue;
      }

      XNode* steps_node = song_node->AppendChild(steps_id.CreateNode());

      steps_node->AppendChild(hsl.CreateNode());
    }
  }

  return node;
}

void Profile::LoadSongScoresFromNode(const XNode* song_scores) {
  CHECKPOINT_M("Loading the node that contains song scores.");

  ASSERT(song_scores->GetName() == "SongScores");

  FOREACH_CONST_Child(song_scores, song) {
    if (song->GetName() != "Song") {
      continue;
    }

    SongID song_id;
    song_id.LoadFromNode(song);
    // Allow invalid songs so that scores aren't deleted for people that use
    // AdditionalSongsFolders and change it frequently. -Kyz
    // if( !songID.IsValid() )
    //	continue;

    FOREACH_CONST_Child(song, steps) {
      if (steps->GetName() != "Steps") {
        continue;
      }

      StepsID steps_id;
      steps_id.LoadFromNode(steps);
      if (!steps_id.IsValid()) {
        WARN_AND_CONTINUE;
      }

      const XNode* high_score_list_node = steps->GetChild("HighScoreList");
      if (high_score_list_node == nullptr) {
        WARN_AND_CONTINUE;
      }

      HighScoreList& hsl =
          song_high_scores_[song_id].steps_high_scores_[steps_id].hsl_;
      hsl.LoadFromNode(high_score_list_node);
    }
  }
}

XNode* Profile::SaveCourseScoresCreateNode() const {
  CHECKPOINT_M("Getting the node to save course scores.");

  const Profile* profile = this;
  ASSERT(profile != nullptr);

  XNode* node = new XNode("CourseScores");

  for (const std::pair<const CourseID, HighScoresForACourse>& i :
       course_high_scores_) {
    const CourseID& course_id = i.first;
    const HighScoresForACourse& course = i.second;

    // skip courses that have never been played
    if (profile->GetCourseNumTimesPlayed(course_id) == 0) {
      continue;
    }

    XNode* course_node = node->AppendChild(course_id.CreateNode());

    for (const std::pair<const TrailID, HighScoresForATrail>& j :
         course.trail_high_scores_) {
      const TrailID& trail_id = j.first;
      const HighScoresForATrail& trail = j.second;

      const HighScoreList& hsl = trail.hsl_;

      // skip steps that have never been played
      if (hsl.GetNumTimesPlayed() == 0) {
        continue;
      }

      XNode* trail_node = course_node->AppendChild(trail_id.CreateNode());

      trail_node->AppendChild(hsl.CreateNode());
    }
  }

  return node;
}

void Profile::LoadCourseScoresFromNode(const XNode* course_scores) {
  CHECKPOINT_M("Loading the node that contains course scores.");

  ASSERT(course_scores->GetName() == "CourseScores");

  std::vector<Course*> all_courses;
  SONGMAN->GetAllCourses(all_courses, true);

  FOREACH_CONST_Child(course_scores, course) {
    if (course->GetName() != "Course") {
      continue;
    }

    CourseID course_id;
    course_id.LoadFromNode(course);
    // Allow invalid courses so that scores aren't deleted for people that use
    // AdditionalCoursesFolders and change it frequently. -Kyz
    // if( !courseID.IsValid() )
    //	WARN_AND_CONTINUE;

    // Backward compatability hack to fix importing scores of old style
    // courses that weren't in group folder but have now been moved into
    // a group folder:
    // If the courseID doesn't resolve, then take the file name part of sPath
    // and search for matches of just the file name.
    {
      Course* p_c = course_id.ToCourse();
      if (p_c == nullptr) {
        std::string dir, f_name, ext;
        splitpath(course_id.GetPath(), dir, f_name, ext);
        std::string full_file_name = f_name + ext;

        for (Course* c : all_courses) {
          std::string other = Right(c->path_, full_file_name.size());

          if (CompareNoCase(full_file_name, other) == 0) {
            p_c = c;
            course_id.FromCourse(p_c);
            break;
          }
        }
      }
    }

    FOREACH_CONST_Child(course, trail) {
      if (trail->GetName() != "Trail") {
        continue;
      }

      TrailID trail_id;
      trail_id.LoadFromNode(trail);
      if (!trail_id.IsValid()) {
        WARN_AND_CONTINUE;
      }

      const XNode* high_score_list_node = trail->GetChild("HighScoreList");
      if (high_score_list_node == nullptr) {
        WARN_AND_CONTINUE;
      }

      HighScoreList& hsl =
          course_high_scores_[course_id].trail_high_scores_[trail_id].hsl_;
      hsl.LoadFromNode(high_score_list_node);
    }
  }
}

XNode* Profile::SaveCategoryScoresCreateNode() const {
  CHECKPOINT_M("Getting the node that saves category scores.");

  const Profile* profile = this;
  ASSERT(profile != nullptr);

  XNode* node = new XNode("CategoryScores");

  FOREACH_ENUM(StepsType, st) {
    // skip steps types that have never been played
    if (profile->GetCategoryNumTimesPlayed(st) == 0) {
      continue;
    }

    XNode* steps_type_node = node->AppendChild("StepsType");
    steps_type_node->AppendAttr("Type", GAMEMAN->GetStepsTypeInfo(st).name_);

    FOREACH_ENUM(RankingCategory, rc) {
      // skip steps types/categories that have never been played
      if (profile->GetCategoryHighScoreList(st, rc).GetNumTimesPlayed() == 0) {
        continue;
      }

      XNode* ranking_category_node =
          steps_type_node->AppendChild("RankingCategory");
      ranking_category_node->AppendAttr("Type", RankingCategoryToString(rc));

      const HighScoreList& hsl =
          profile->GetCategoryHighScoreList((StepsType)st, (RankingCategory)rc);

      ranking_category_node->AppendChild(hsl.CreateNode());
    }
  }

  return node;
}

void Profile::LoadCategoryScoresFromNode(const XNode* category_scores) {
  CHECKPOINT_M("Loading the node that contains category scores.");

  ASSERT(category_scores->GetName() == "CategoryScores");

  FOREACH_CONST_Child(category_scores, steps_type) {
    if (steps_type->GetName() != "StepsType") {
      continue;
    }

    std::string str;
    if (!steps_type->GetAttrValue("Type", str)) {
      WARN_AND_CONTINUE;
    }
    StepsType st = GAMEMAN->StringToStepsType(str);
    if (st == StepsType_Invalid) {
      WARN_AND_CONTINUE_M(str);
    }

    FOREACH_CONST_Child(steps_type, radar_category) {
      if (radar_category->GetName() != "RankingCategory") {
        continue;
      }

      if (!radar_category->GetAttrValue("Type", str)) {
        WARN_AND_CONTINUE;
      }
      RankingCategory rc = StringToRankingCategory(str);
      if (rc == RankingCategory_Invalid) {
        WARN_AND_CONTINUE_M(str);
      }

      const XNode* high_score_list_node =
          radar_category->GetChild("HighScoreList");
      if (high_score_list_node == nullptr) {
        WARN_AND_CONTINUE;
      }

      HighScoreList& hsl = this->GetCategoryHighScoreList(st, rc);
      hsl.LoadFromNode(high_score_list_node);
    }
  }
}

void Profile::SaveStatsWebPageToDir(std::string) const {
  ASSERT(PROFILEMAN != nullptr);
}

void Profile::AddScreenshot(const Screenshot& screenshot) {
  screenshots_.push_back(screenshot);
}

void Profile::LoadScreenshotDataFromNode(const XNode* screenshot_data) {
  CHECKPOINT_M("Loading the node containing screenshot data.");

  ASSERT(screenshot_data->GetName() == "ScreenshotData");
  FOREACH_CONST_Child(screenshot_data, screenshot) {
    if (screenshot->GetName() != "Screenshot") {
      WARN_AND_CONTINUE_M(screenshot->GetName());
    }

    Screenshot ss;
    ss.LoadFromNode(screenshot);

    screenshots_.push_back(ss);
  }
}

XNode* Profile::SaveScreenshotDataCreateNode() const {
  CHECKPOINT_M("Getting the node containing screenshot data.");

  const Profile* profile = this;
  ASSERT(profile != nullptr);

  XNode* node = new XNode("ScreenshotData");

  for (const Screenshot& ss : screenshots_) {
    node->AppendChild(ss.CreateNode());
  }

  return node;
}

void Profile::LoadCalorieDataFromNode(const XNode* calorie_data) {
  CHECKPOINT_M("Loading the node containing calorie data.");

  ASSERT(calorie_data->GetName() == "CalorieData");
  FOREACH_CONST_Child(calorie_data, p_calories_burned) {
    if (p_calories_burned->GetName() != "CaloriesBurned") {
      WARN_AND_CONTINUE_M(p_calories_burned->GetName());
    }

    std::string s_date;
    if (!p_calories_burned->GetAttrValue("Date", s_date)) {
      WARN_AND_CONTINUE;
    }
    DateTime date;
    if (!date.FromString(s_date)) {
      WARN_AND_CONTINUE_M(s_date);
    }

    float f_calories_burned = 0;

    p_calories_burned->GetTextValue(f_calories_burned);

    map_day_to_calories_burned_[date].cals_ = f_calories_burned;
  }
}

XNode* Profile::SaveCalorieDataCreateNode() const {
  CHECKPOINT_M("Getting the node containing calorie data.");

  const Profile* profile = this;
  ASSERT(profile != nullptr);

  XNode* node = new XNode("CalorieData");

  for (const std::pair<const DateTime, Calories>& i :
       map_day_to_calories_burned_) {
    XNode* calories_burned =
        node->AppendChild("CaloriesBurned", i.second.cals_);

    calories_burned->AppendAttr("Date", i.first.GetString());
  }

  return node;
}

float Profile::GetCaloriesBurnedForDay(DateTime day) const {
  day.StripTime();
  std::map<DateTime, Calories>::const_iterator i =
      map_day_to_calories_burned_.find(day);
  if (i == map_day_to_calories_burned_.end()) {
    return 0;
  } else {
    return i->second.cals_;
  }
}

/*
static void SaveRecentScore( XNode* xml )
{
        std::string sDate = DateTime::GetNowDate().GetString();
        sDate.Replace(":","-");

        std::string sFileNameNoExtension =
Profile::MakeUniqueFileNameNoExtension(UPLOAD_SUBDIR, sDate ); std::string fn =
UPLOAD_SUBDIR + sFileNameNoExtension + ".xml";

        std::string sStatsXmlSigFile = fn+SIGNATURE_APPEND;
        CryptManager::SignFileToFile(fn, sStatsXmlSigFile);
}

XNode* Profile::HighScoreForASongAndSteps::CreateNode() const
{
        XNode* pNode = new XNode( "HighScoreForASongAndSteps" );

        pNode->AppendChild( songID.CreateNode() );
        pNode->AppendChild( stepsID.CreateNode() );
        pNode->AppendChild( hs.CreateNode() );

        return pNode;
}

void Profile::SaveStepsRecentScore( const Song* pSong, const Steps* pSteps,
HighScore hs )
{
        ASSERT( pSong );
        ASSERT( pSteps );
        HighScoreForASongAndSteps h;
        h.songID.FromSong( pSong );
        ASSERT( h.songID.IsValid() );
        h.stepsID.FromSteps( pSteps );
        ASSERT( h.stepsID.IsValid() );
        h.hs = hs;

        std::unique_ptr<XNode> xml( new XNode("Stats") );
        xml->AppendChild( "MachineGuid",
PROFILEMAN->GetMachineProfile()->m_sGuid ); XNode *recent = xml->AppendChild(
new XNode("RecentSongScores") ); recent->AppendChild( h.CreateNode() );

        SaveRecentScore( xml.get() );
}


XNode* Profile::HighScoreForACourseAndTrail::CreateNode() const
{
        XNode* pNode = new XNode( "HighScoreForACourseAndTrail" );

        pNode->AppendChild( courseID.CreateNode() );
        pNode->AppendChild( trailID.CreateNode() );
        pNode->AppendChild( hs.CreateNode() );

        return pNode;
}

void Profile::SaveCourseRecentScore( const Course* pCourse, const Trail* pTrail,
HighScore hs )
{
        HighScoreForACourseAndTrail h;
        h.courseID.FromCourse( pCourse );
        h.trailID.FromTrail( pTrail );
        h.hs = hs;

        std::unique_ptr<XNode> xml( new XNode("Stats") );
        xml->AppendChild( "MachineGuid",
PROFILEMAN->GetMachineProfile()->m_sGuid ); XNode *recent = xml->AppendChild(
new XNode("RecentCourseScores") ); recent->AppendChild( h.CreateNode() );
        SaveRecentScore( xml.get() );
}
*/
const Profile::HighScoresForASong* Profile::GetHighScoresForASong(
    const SongID& song_id) const {
  std::map<SongID, HighScoresForASong>::const_iterator it;
  it = song_high_scores_.find(song_id);
  if (it == song_high_scores_.end()) {
    return nullptr;
  }
  return &it->second;
}

const Profile::HighScoresForACourse* Profile::GetHighScoresForACourse(
    const CourseID& course_id) const {
  std::map<CourseID, HighScoresForACourse>::const_iterator it;
  it = course_high_scores_.find(course_id);
  if (it == course_high_scores_.end()) {
    return nullptr;
  }
  return &it->second;
}

bool Profile::IsMachine() const {
  // TODO: Think of a better way to handle this
  return this == PROFILEMAN->GetMachineProfile();
}

XNode* Profile::SaveCoinDataCreateNode() const {
  CHECKPOINT_M("Getting the node containing coin data.");

  const Profile* profile = this;
  ASSERT(profile != nullptr);

  XNode* node = new XNode("CoinData");

  {
    int coins[NUM_LAST_DAYS];
    BOOKKEEPER->GetCoinsLastDays(coins);
    XNode* p = node->AppendChild("LastDays");
    for (int i = 0; i < NUM_LAST_DAYS; i++) {
      p->AppendChild(LastDayToString(i), coins[i]);
    }
  }
  {
    int coins[NUM_LAST_WEEKS];
    BOOKKEEPER->GetCoinsLastWeeks(coins);
    XNode* p = node->AppendChild("LastWeeks");
    for (int i = 0; i < NUM_LAST_WEEKS; i++) {
      p->AppendChild(LastWeekToString(i), coins[i]);
    }
  }
  {
    int coins[DAYS_IN_WEEK];
    BOOKKEEPER->GetCoinsByDayOfWeek(coins);
    XNode* p = node->AppendChild("DayOfWeek");
    for (int i = 0; i < DAYS_IN_WEEK; i++) {
      p->AppendChild(DayOfWeekToString(i), coins[i]);
    }
  }
  {
    int coins[HOURS_IN_DAY];
    BOOKKEEPER->GetCoinsByHour(coins);
    XNode* p = node->AppendChild("Hour");
    for (int i = 0; i < HOURS_IN_DAY; i++) {
      p->AppendChild(HourInDayToString(i), coins[i]);
    }
  }

  return node;
}

void Profile::MoveBackupToDir(std::string from_dir, std::string to_dir) {
  std::string stats_file;
  std::string old_stats_file;
  if (FILEMAN->IsAFile(from_dir + STATS_XML)) {
    stats_file = STATS_XML;
    old_stats_file = STATS_XML_GZ;
  } else if (FILEMAN->IsAFile(from_dir + STATS_XML_GZ)) {
    stats_file = STATS_XML_GZ;
    old_stats_file = STATS_XML;
  }

  if (!stats_file.empty()) {
    bool backup_complete =
        FILEMAN->Move(from_dir + stats_file, to_dir + stats_file);
    const std::string signature_file = stats_file + SIGNATURE_APPEND;
    if (FILEMAN->IsAFile(from_dir + signature_file)) {
      const bool signature_moved =
          FILEMAN->Move(from_dir + signature_file, to_dir + signature_file);
      backup_complete = backup_complete && signature_moved;
    }

    // We should not leave an outdated file in LastGood for an alternate format
    // after the new backup succeeds.
    // Otherwise the wrong backup file might be loaded if LastGood gets used.
    if (backup_complete) {
      FILEMAN->Remove(to_dir + old_stats_file);
      FILEMAN->Remove(to_dir + old_stats_file + SIGNATURE_APPEND);
    }
  }

  if (FILEMAN->IsAFile(from_dir + EDITABLE_INI)) {
    FILEMAN->Move(from_dir + EDITABLE_INI, to_dir + EDITABLE_INI);
  }
  if (FILEMAN->IsAFile(from_dir + DONT_SHARE_SIG)) {
    FILEMAN->Move(from_dir + DONT_SHARE_SIG, to_dir + DONT_SHARE_SIG);
  }
}

std::string Profile::MakeUniqueFileNameNoExtension(
    std::string dir, std::string file_name_beginning) {
  FILEMAN->FlushDirCache(dir);
  // Find a file name for the screenshot
  std::vector<std::string> files;
  GetDirListing(dir + file_name_beginning + "*", files, false, false);
  sort(files.begin(), files.end());

  int index = 0;

  for (int i = files.size() - 1; i >= 0; --i) {
    static Regex re("^" + file_name_beginning + "([0-9]{5})\\....$");
    std::vector<std::string> matches;
    if (!re.Compare(files[i], matches)) {
      continue;
    }

    ASSERT(matches.size() == 1);
    index = StringToInt(matches[0]) + 1;
    break;
  }

  return MakeFileNameNoExtension(file_name_beginning, index);
}

std::string Profile::MakeFileNameNoExtension(
    std::string file_name_beginning, int index) {
  return file_name_beginning + ssprintf("%05d", index);
}

std::string Profile::GetCustomSongsGroupNamePrefix() const {
  std::string group_name = THEME->GetString("Profile", "ProfileCustomSongs");

  return group_name.replace(
      group_name.find("%s"), sizeof("%s") - 1, GetDisplayNameOrHighScoreName());
}

bool Profile::IsCustomSongGroup(std::string song_group) const {
  std::string prefix = GetCustomSongsGroupNamePrefix();

  return song_group.compare(0, prefix.length(), prefix) == 0;
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the Profile. */
class LunaProfile : public Luna<Profile> {
 public:
  static int AddScreenshot(T* p, lua_State* L) {
    HighScore* hs = Luna<HighScore>::check(L, 1);
    std::string filename = SArg(2);
    Screenshot screenshot;
    screenshot.file_name_ = filename;
    screenshot.md5_ = BinaryToHex(CRYPTMAN->GetMD5ForFile(filename));
    screenshot.high_score_ = *hs;
    p->AddScreenshot(screenshot);
    COMMON_RETURN_SELF;
  }
  DEFINE_METHOD(GetType, type_);
  DEFINE_METHOD(GetPriority, list_priority_);

  static int GetDisplayName(T* p, lua_State* L) {
    lua_pushstring(L, p->display_name_.c_str());
    return 1;
  }
  static int SetDisplayName(T* p, lua_State* L) {
    p->display_name_ = SArg(1);
    COMMON_RETURN_SELF;
  }
  static int GetLastUsedHighScoreName(T* p, lua_State* L) {
    lua_pushstring(L, p->last_used_high_score_name_.c_str());
    return 1;
  }
  static int SetLastUsedHighScoreName(T* p, lua_State* L) {
    p->last_used_high_score_name_ = SArg(1);
    COMMON_RETURN_SELF;
  }
  static int GetHighScoreList(T* p, lua_State* L) {
    if (LuaBinding::CheckLuaObjectType(L, 1, "Song")) {
      const Song* song = Luna<Song>::check(L, 1);
      const Steps* steps = Luna<Steps>::check(L, 2);
      HighScoreList& hsl = p->GetStepsHighScoreList(song, steps);
      hsl.PushSelf(L);
      return 1;
    } else if (LuaBinding::CheckLuaObjectType(L, 1, "Course")) {
      const Course* course = Luna<Course>::check(L, 1);
      const Trail* trail = Luna<Trail>::check(L, 2);
      HighScoreList& hsl = p->GetCourseHighScoreList(course, trail);
      hsl.PushSelf(L);
      return 1;
    }

    luaL_typerror(L, 1, "Song or Course");
    COMMON_RETURN_SELF;
  }
  static int GetCategoryHighScoreList(T* p, lua_State* L) {
    StepsType steps_type = Enum::Check<StepsType>(L, 1);
    RankingCategory rank_cat = Enum::Check<RankingCategory>(L, 2);
    HighScoreList& hsl = p->GetCategoryHighScoreList(steps_type, rank_cat);
    hsl.PushSelf(L);
    return 1;
  }

  static int GetHighScoreListIfExists(T* p, lua_State* L) {
#define GET_IF_EXISTS(arga_type, argb_type, arga_scores, argb_scores)   \
  const arga_type* parga = Luna<arga_type>::check(L, 1);                \
  const argb_type* pargb = Luna<argb_type>::check(L, 2);                \
  arga_type##ID arga_id;                                                \
  arga_id.From##arga_type(parga);                                       \
  argb_type##ID argb_id;                                                \
  argb_id.From##argb_type(pargb);                                       \
  std::map<arga_type##ID, Profile::HighScoresForA##arga_type>::iterator \
      main_scores = p->arga_scores.find(arga_id);                       \
  if (main_scores == p->arga_scores.end()) {                            \
    lua_pushnil(L);                                                     \
    return 1;                                                           \
  }                                                                     \
  std::map<argb_type##ID, Profile::HighScoresForA##argb_type>::iterator \
      sub_scores = main_scores->second.argb_scores.find(argb_id);       \
  if (sub_scores == main_scores->second.argb_scores.end()) {            \
    lua_pushnil(L);                                                     \
    return 1;                                                           \
  }                                                                     \
  sub_scores->second.hsl_.PushSelf(L);                                  \
  return 1;

    if (LuaBinding::CheckLuaObjectType(L, 1, "Song")) {
      GET_IF_EXISTS(Song, Steps, song_high_scores_, steps_high_scores_);
    } else if (LuaBinding::CheckLuaObjectType(L, 1, "Course")) {
      GET_IF_EXISTS(Course, Trail, course_high_scores_, trail_high_scores_);
    }
    luaL_typerror(L, 1, "Song or Course");
    return 0;
#undef GET_IF_EXISTS
  }

  static int GetAllUsedHighScoreNames(T* p, lua_State* L) {
    std::set<std::string> names;
    p->GetAllUsedHighScoreNames(names);
    lua_createtable(L, names.size(), 0);
    int next_name_index = 1;
    for (std::set<std::string>::iterator name = names.begin();
         name != names.end(); ++name) {
      lua_pushstring(L, name->c_str());
      lua_rawseti(L, -2, next_name_index);
      ++next_name_index;
    }
    return 1;
  }

  static int GetCharacter(T* p, lua_State* L) {
    p->GetCharacter()->PushSelf(L);
    return 1;
  }
  static int SetCharacter(T* p, lua_State* L) {
    p->SetCharacter(SArg(1));
    COMMON_RETURN_SELF;
  }
  static int GetWeightPounds(T* p, lua_State* L) {
    lua_pushnumber(L, p->weight_pounds_);
    return 1;
  }
  static int SetWeightPounds(T* p, lua_State* L) {
    p->weight_pounds_ = IArg(1);
    COMMON_RETURN_SELF;
  }
  DEFINE_METHOD(GetVoomax, voomax_);
  DEFINE_METHOD(GetAge, GetAge());
  DEFINE_METHOD(GetBirthYear, birth_year_);
  DEFINE_METHOD(GetIgnoreStepCountCalories, ignore_step_count_calories_);
  DEFINE_METHOD(GetIsMale, is_male_);
  static int SetVoomax(T* p, lua_State* L) {
    p->voomax_ = FArg(1);
    COMMON_RETURN_SELF;
  }
  static int SetBirthYear(T* p, lua_State* L) {
    p->birth_year_ = IArg(1);
    COMMON_RETURN_SELF;
  }
  static int SetIgnoreStepCountCalories(T* p, lua_State* L) {
    p->ignore_step_count_calories_ = BArg(1);
    COMMON_RETURN_SELF;
  }
  static int SetIsMale(T* p, lua_State* L) {
    p->is_male_ = BArg(1);
    COMMON_RETURN_SELF;
  }
  static int AddCaloriesToDailyTotal(T* p, lua_State* L) {
    p->AddCaloriesToDailyTotal(FArg(1));
    COMMON_RETURN_SELF;
  }
  DEFINE_METHOD(
      CalculateCaloriesFromHeartRate,
      CalculateCaloriesFromHeartRate(FArg(1), FArg(2)));
  static int GetGoalType(T* p, lua_State* L) {
    lua_pushnumber(L, p->goal_type_);
    return 1;
  }
  static int SetGoalType(T* p, lua_State* L) {
    p->goal_type_ = Enum::Check<GoalType>(L, 1);
    COMMON_RETURN_SELF;
  }
  static int GetGoalCalories(T* p, lua_State* L) {
    lua_pushnumber(L, p->goal_calories_);
    return 1;
  }
  static int SetGoalCalories(T* p, lua_State* L) {
    p->goal_calories_ = IArg(1);
    COMMON_RETURN_SELF;
  }
  static int GetGoalSeconds(T* p, lua_State* L) {
    lua_pushnumber(L, p->goal_seconds_);
    return 1;
  }
  static int SetGoalSeconds(T* p, lua_State* L) {
    p->goal_seconds_ = IArg(1);
    COMMON_RETURN_SELF;
  }
  static int GetCaloriesBurnedToday(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetCaloriesBurnedToday());
    return 1;
  }
  static int GetTotalNumSongsPlayed(T* p, lua_State* L) {
    lua_pushnumber(L, p->num_total_songs_played_);
    return 1;
  }
  static int IsCodeUnlocked(T* p, lua_State* L) {
    lua_pushboolean(L, p->IsCodeUnlocked(SArg(1)));
    return 1;
  }
  static int GetSongsActual(T* p, lua_State* L) {
    lua_pushnumber(
        L, p->GetSongsActual(
               Enum::Check<StepsType>(L, 1), Enum::Check<Difficulty>(L, 2)));
    return 1;
  }
  static int GetCoursesActual(T* p, lua_State* L) {
    lua_pushnumber(
        L, p->GetCoursesActual(
               Enum::Check<StepsType>(L, 1), Enum::Check<Difficulty>(L, 2)));
    return 1;
  }
  static int GetSongsPossible(T* p, lua_State* L) {
    lua_pushnumber(
        L, p->GetSongsPossible(
               Enum::Check<StepsType>(L, 1), Enum::Check<Difficulty>(L, 2)));
    return 1;
  }
  static int GetCoursesPossible(T* p, lua_State* L) {
    lua_pushnumber(
        L, p->GetCoursesPossible(
               Enum::Check<StepsType>(L, 1), Enum::Check<Difficulty>(L, 2)));
    return 1;
  }
  static int GetSongsPercentComplete(T* p, lua_State* L) {
    lua_pushnumber(
        L, p->GetSongsPercentComplete(
               Enum::Check<StepsType>(L, 1), Enum::Check<Difficulty>(L, 2)));
    return 1;
  }
  static int GetCoursesPercentComplete(T* p, lua_State* L) {
    lua_pushnumber(
        L, p->GetCoursesPercentComplete(
               Enum::Check<StepsType>(L, 1), Enum::Check<Difficulty>(L, 2)));
    return 1;
  }
  static int GetTotalStepsWithTopGrade(T* p, lua_State* L) {
    lua_pushnumber(
        L, p->GetTotalStepsWithTopGrade(
               Enum::Check<StepsType>(L, 1), Enum::Check<Difficulty>(L, 2),
               Enum::Check<Grade>(L, 3)));
    return 1;
  }
  static int GetTotalTrailsWithTopGrade(T* p, lua_State* L) {
    lua_pushnumber(
        L, p->GetTotalTrailsWithTopGrade(
               Enum::Check<StepsType>(L, 1), Enum::Check<Difficulty>(L, 2),
               Enum::Check<Grade>(L, 3)));
    return 1;
  }
  static int GetNumTotalSongsPlayed(T* p, lua_State* L) {
    lua_pushnumber(L, p->num_total_songs_played_);
    return 1;
  }
  static int GetTotalSessions(T* p, lua_State* L) {
    lua_pushnumber(L, p->total_sessions_);
    return 1;
  }
  static int GetTotalSessionSeconds(T* p, lua_State* L) {
    lua_pushnumber(L, p->total_session_seconds_);
    return 1;
  }
  static int GetTotalGameplaySeconds(T* p, lua_State* L) {
    lua_pushnumber(L, p->total_gameplay_seconds_);
    return 1;
  }
  static int GetSongsAndCoursesPercentCompleteAllDifficulties(
      T* p, lua_State* L) {
    lua_pushnumber(
        L, p->GetSongsAndCoursesPercentCompleteAllDifficulties(
               Enum::Check<StepsType>(L, 1)));
    return 1;
  }
  static int GetTotalCaloriesBurned(T* p, lua_State* L) {
    lua_pushnumber(L, p->total_calories_burned_);
    return 1;
  }
  static int GetDisplayTotalCaloriesBurned(T* p, lua_State* L) {
    lua_pushstring(L, p->GetDisplayTotalCaloriesBurned().c_str());
    return 1;
  }
  static int GetMostPopularSong(T* p, lua_State* L) {
    Song* p2 = p->GetMostPopularSong();
    if (p2) {
      p2->PushSelf(L);
    } else {
      lua_pushnil(L);
    }
    return 1;
  }
  static int GetMostPopularCourse(T* p, lua_State* L) {
    Course* p2 = p->GetMostPopularCourse();
    if (p2) {
      p2->PushSelf(L);
    } else {
      lua_pushnil(L);
    }
    return 1;
  }
  static int GetSongNumTimesPlayed(T* p, lua_State* L) {
    ASSERT(!lua_isnil(L, 1));
    Song* s = Luna<Song>::check(L, 1);
    lua_pushnumber(L, p->GetSongNumTimesPlayed(s));
    return 1;
  }
  static int HasPassedAnyStepsInSong(T* p, lua_State* L) {
    ASSERT(!lua_isnil(L, 1));
    Song* s = Luna<Song>::check(L, 1);
    lua_pushboolean(L, p->HasPassedAnyStepsInSong(s));
    return 1;
  }
  static int GetNumToasties(T* p, lua_State* L) {
    lua_pushnumber(L, p->num_toasties_);
    return 1;
  }
  static int GetTotalTapsAndHolds(T* p, lua_State* L) {
    lua_pushnumber(L, p->total_taps_and_holds_);
    return 1;
  }
  static int GetTotalJumps(T* p, lua_State* L) {
    lua_pushnumber(L, p->total_jumps_);
    return 1;
  }
  static int GetTotalHolds(T* p, lua_State* L) {
    lua_pushnumber(L, p->total_holds_);
    return 1;
  }
  static int GetTotalRolls(T* p, lua_State* L) {
    lua_pushnumber(L, p->total_rolls_);
    return 1;
  }
  static int GetTotalMines(T* p, lua_State* L) {
    lua_pushnumber(L, p->total_mines_);
    return 1;
  }
  static int GetTotalHands(T* p, lua_State* L) {
    lua_pushnumber(L, p->total_hands_);
    return 1;
  }
  static int GetTotalLifts(T* p, lua_State* L) {
    lua_pushnumber(L, p->total_lifts_);
    return 1;
  }
  DEFINE_METHOD(GetTotalDancePoints, total_dance_points_);
  static int GetUserTable(T* p, lua_State* L) {
    p->user_table_.PushSelf(L);
    return 1;
  }
  static int GetLastPlayedSong(T* p, lua_State* L) {
    Song* s = p->last_song_.ToSong();
    if (s) {
      s->PushSelf(L);
    } else {
      lua_pushnil(L);
    }
    return 1;
  }
  static int GetLastPlayedCourse(T* p, lua_State* L) {
    Course* c = p->last_course_.ToCourse();
    if (c) {
      c->PushSelf(L);
    } else {
      lua_pushnil(L);
    }
    return 1;
  }
  DEFINE_METHOD(GetGUID, guid_);
  static int get_songs(T* p, lua_State* L) {
    lua_createtable(L, p->songs_.size(), 0);
    int song_tab = lua_gettop(L);
    for (size_t i = 0; i < p->songs_.size(); ++i) {
      p->songs_[i]->PushSelf(L);
      lua_rawseti(L, song_tab, i + 1);
    }
    return 1;
  }
  LunaProfile() {
    ADD_METHOD(AddScreenshot);
    ADD_METHOD(GetType);
    ADD_METHOD(GetPriority);
    ADD_METHOD(GetDisplayName);
    ADD_METHOD(SetDisplayName);
    ADD_METHOD(GetLastUsedHighScoreName);
    ADD_METHOD(SetLastUsedHighScoreName);
    ADD_METHOD(GetAllUsedHighScoreNames);
    ADD_METHOD(GetHighScoreListIfExists);
    ADD_METHOD(GetHighScoreList);
    ADD_METHOD(GetCategoryHighScoreList);
    ADD_METHOD(GetCharacter);
    ADD_METHOD(SetCharacter);
    ADD_METHOD(GetWeightPounds);
    ADD_METHOD(SetWeightPounds);
    ADD_METHOD(GetVoomax);
    ADD_METHOD(SetVoomax);
    ADD_METHOD(GetAge);
    ADD_METHOD(GetBirthYear);
    ADD_METHOD(SetBirthYear);
    ADD_METHOD(GetIgnoreStepCountCalories);
    ADD_METHOD(SetIgnoreStepCountCalories);
    ADD_METHOD(GetIsMale);
    ADD_METHOD(SetIsMale);
    ADD_METHOD(AddCaloriesToDailyTotal);
    ADD_METHOD(CalculateCaloriesFromHeartRate);
    ADD_METHOD(GetGoalType);
    ADD_METHOD(SetGoalType);
    ADD_METHOD(GetGoalCalories);
    ADD_METHOD(SetGoalCalories);
    ADD_METHOD(GetGoalSeconds);
    ADD_METHOD(SetGoalSeconds);
    ADD_METHOD(GetCaloriesBurnedToday);
    ADD_METHOD(GetTotalNumSongsPlayed);
    ADD_METHOD(IsCodeUnlocked);
    ADD_METHOD(GetSongsActual);
    ADD_METHOD(GetCoursesActual);
    ADD_METHOD(GetSongsPossible);
    ADD_METHOD(GetCoursesPossible);
    ADD_METHOD(GetSongsPercentComplete);
    ADD_METHOD(GetCoursesPercentComplete);
    ADD_METHOD(GetTotalStepsWithTopGrade);
    ADD_METHOD(GetTotalTrailsWithTopGrade);
    ADD_METHOD(GetNumTotalSongsPlayed);
    ADD_METHOD(GetTotalSessions);
    ADD_METHOD(GetTotalSessionSeconds);
    ADD_METHOD(GetTotalGameplaySeconds);
    ADD_METHOD(GetSongsAndCoursesPercentCompleteAllDifficulties);
    ADD_METHOD(GetTotalCaloriesBurned);
    ADD_METHOD(GetDisplayTotalCaloriesBurned);
    ADD_METHOD(GetMostPopularSong);
    ADD_METHOD(GetMostPopularCourse);
    ADD_METHOD(GetSongNumTimesPlayed);
    ADD_METHOD(HasPassedAnyStepsInSong);
    ADD_METHOD(GetNumToasties);
    ADD_METHOD(GetTotalTapsAndHolds);
    ADD_METHOD(GetTotalJumps);
    ADD_METHOD(GetTotalHolds);
    ADD_METHOD(GetTotalRolls);
    ADD_METHOD(GetTotalMines);
    ADD_METHOD(GetTotalHands);
    ADD_METHOD(GetTotalLifts);
    ADD_METHOD(GetTotalDancePoints);
    ADD_METHOD(GetUserTable);
    ADD_METHOD(GetLastPlayedSong);
    ADD_METHOD(GetLastPlayedCourse);
    ADD_METHOD(GetGUID);
    ADD_METHOD(get_songs);
  }
};

LUA_REGISTER_CLASS(Profile)
// lua end

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
