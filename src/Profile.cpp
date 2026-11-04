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
  int iCount = 0;
  for (const std::pair<const StepsID, HighScoresForASteps>& i :
       steps_high_scores_) {
    iCount += i.second.hsl_.GetNumTimesPlayed();
  }
  return iCount;
}

int Profile::HighScoresForACourse::GetNumTimesPlayed() const {
  int iCount = 0;
  for (const std::pair<const TrailID, HighScoresForATrail>& i :
       trail_high_scores_) {
    iCount += i.second.hsl_.GetNumTimesPlayed();
  }
  return iCount;
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
  std::string sGuid = guid_;
  InitAll();
  guid_ = sGuid;
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
  std::vector<Character*> vpCharacters;
  CHARMAN->GetCharacters(vpCharacters);
  for (Character* c : vpCharacters) {
    if (CompareNoCase(c->character_id_, character_id_) == 0) {
      return c;
    }
  }
  return CHARMAN->GetDefaultCharacter();
}

void Profile::SetCharacter(const std::string sCharacterID) {
  if (CHARMAN->GetCharacterFromID(sCharacterID)) {
    character_id_ = sCharacterID;
  }
}

static std::string FormatCalories(float fCals) {
  return Commify((int)fCals) + " Cal";
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
  int iTotal = 0;
  FOREACH_ENUM(PlayMode, i)
  iTotal += num_stages_passed_by_play_mode_[i];
  return iTotal;
}

int Profile::GetTotalStepsWithTopGrade(
    StepsType st, Difficulty d, Grade g) const {
  int iCount = 0;

  for (Song* pSong : SONGMAN->GetAllSongs()) {
    if (!pSong->NormallyDisplayed()) {
      continue;  // skip
    }

    for (Steps* pSteps : pSong->GetAllSteps()) {
      if (pSteps->steps_type_ != st) {
        continue;  // skip
      }

      if (pSteps->GetDifficulty() != d) {
        continue;  // skip
      }

      const HighScoreList& hsl = GetStepsHighScoreList(pSong, pSteps);
      if (hsl.high_scores_.empty()) {
        continue;  // skip
      }

      if (hsl.high_scores_[0].GetGrade() == g) {
        iCount++;
      }
    }
  }

  return iCount;
}

int Profile::GetTotalTrailsWithTopGrade(
    StepsType st, CourseDifficulty d, Grade g) const {
  int iCount = 0;

  // add course high scores
  std::vector<Course*> vCourses;
  SONGMAN->GetAllCourses(vCourses, false);
  for (const Course* pCourse : vCourses) {
    // Don't count any course that has any entries that change over time.
    if (!pCourse->AllSongsAreFixed()) {
      continue;
    }

    std::vector<Trail*> vTrails;
    Trail* pTrail = pCourse->GetTrail(st, d);
    if (pTrail == nullptr) {
      continue;
    }

    const HighScoreList& hsl = GetCourseHighScoreList(pCourse, pTrail);
    if (hsl.high_scores_.empty()) {
      continue;  // skip
    }

    if (hsl.high_scores_[0].GetGrade() == g) {
      iCount++;
    }
  }

  return iCount;
}

float Profile::GetSongsPossible(StepsType st, Difficulty dc) const {
  int iTotalSteps = 0;

  // add steps high scores
  const std::vector<Song*>& vSongs = SONGMAN->GetAllSongs();
  for (unsigned i = 0; i < vSongs.size(); i++) {
    Song* pSong = vSongs[i];

    if (!pSong->NormallyDisplayed()) {
      continue;  // skip
    }

    std::vector<Steps*> vSteps = pSong->GetAllSteps();
    for (unsigned j = 0; j < vSteps.size(); j++) {
      Steps* pSteps = vSteps[j];

      if (pSteps->steps_type_ != st) {
        continue;  // skip
      }

      if (pSteps->GetDifficulty() != dc) {
        continue;  // skip
      }

      iTotalSteps++;
    }
  }

  return (float)iTotalSteps;
}

float Profile::GetSongsActual(StepsType st, Difficulty dc) const {
  CHECKPOINT_M(ssprintf("Profile::GetSongsActual(%d,%d)", st, dc));

  float fTotalPercents = 0;

  // add steps high scores
  for (const std::pair<const SongID, HighScoresForASong>& id :
       song_high_scores_) {
    Song* pSong = id.first.ToSong();

    CHECKPOINT_M(
        ssprintf("Profile::GetSongsActual: %p", static_cast<void*>(pSong)));

    // If the Song isn't loaded on the current machine, then we can't
    // get radar values to compute dance points.
    if (pSong == nullptr) {
      continue;
    }

    if (!pSong->NormallyDisplayed()) {
      continue;  // skip
    }

    CHECKPOINT_M(ssprintf(
        "Profile::GetSongsActual: song %s", pSong->GetSongDir().c_str()));
    const HighScoresForASong& hsfas = id.second;

    for (const std::pair<const StepsID, HighScoresForASteps>& j :
         hsfas.steps_high_scores_) {
      const StepsID& sid = j.first;
      Steps* pSteps = sid.ToSteps(pSong, true);
      CHECKPOINT_M(ssprintf(
          "Profile::GetSongsActual: song %p, steps %p",
          static_cast<void*>(pSong), static_cast<void*>(pSteps)));

      // If the Steps isn't loaded on the current machine, then we can't
      // get radar values to compute dance points.
      if (pSteps == nullptr) {
        continue;
      }

      if (pSteps->steps_type_ != st) {
        continue;
      }

      CHECKPOINT_M(ssprintf(
          "Profile::GetSongsActual: n %s = %p", sid.ToString().c_str(),
          static_cast<void*>(pSteps)));
      if (pSteps->GetDifficulty() != dc) {
        continue;  // skip
      }

      CHECKPOINT_M(ssprintf(
          "Profile::GetSongsActual: difficulty %s is correct",
          DifficultyToString(dc).c_str()));

      const HighScoresForASteps& h = j.second;
      const HighScoreList& hsl = h.hsl_;

      fTotalPercents += hsl.GetTopScore().GetPercentDP();
    }
  }

  return fTotalPercents;
}

float Profile::GetSongsPercentComplete(StepsType st, Difficulty dc) const {
  return GetSongsActual(st, dc) / GetSongsPossible(st, dc);
}

static void GetHighScoreCourses(std::vector<Course*>& vpCoursesOut) {
  vpCoursesOut.clear();

  std::vector<Course*> vpCourses;
  SONGMAN->GetAllCourses(vpCourses, false);

  for (Course* c : vpCourses) {
    // Don't count any course that has any entries that change over time.
    if (!c->AllSongsAreFixed()) {
      continue;
    }

    vpCoursesOut.push_back(c);
  }
}

float Profile::GetCoursesPossible(StepsType st, CourseDifficulty cd) const {
  std::vector<Course*> vpCourses;
  GetHighScoreCourses(vpCourses);
  return std::count_if(
      vpCourses.begin(), vpCourses.end(),
      [&](const Course* c) { return c->GetTrail(st, cd) != nullptr; });
}

float Profile::GetCoursesActual(StepsType st, CourseDifficulty cd) const {
  float fTotalPercents = 0;

  std::vector<Course*> vpCourses;
  GetHighScoreCourses(vpCourses);
  for (const Course* c : vpCourses) {
    Trail* pTrail = c->GetTrail(st, cd);
    if (pTrail == nullptr) {
      continue;
    }

    const HighScoreList& hsl = GetCourseHighScoreList(c, pTrail);
    fTotalPercents += hsl.GetTopScore().GetPercentDP();
  }

  return fTotalPercents;
}

float Profile::GetCoursesPercentComplete(
    StepsType st, CourseDifficulty cd) const {
  return GetCoursesActual(st, cd) / GetCoursesPossible(st, cd);
}

float Profile::GetSongsAndCoursesPercentCompleteAllDifficulties(
    StepsType st) const {
  float fActual = 0;
  float fPossible = 0;
  FOREACH_ENUM(Difficulty, d) {
    fActual += GetSongsActual(st, d);
    fPossible += GetSongsPossible(st, d);
  }
  FOREACH_ENUM(CourseDifficulty, d) {
    fActual += GetCoursesActual(st, d);
    fPossible += GetCoursesPossible(st, d);
  }
  return fActual / fPossible;
}

int Profile::GetSongNumTimesPlayed(const Song* pSong) const {
  SongID songID;
  songID.FromSong(pSong);
  return GetSongNumTimesPlayed(songID);
}

int Profile::GetSongNumTimesPlayed(const SongID& songID) const {
  const HighScoresForASong* hsSong = GetHighScoresForASong(songID);
  if (hsSong == nullptr) {
    return 0;
  }

  int iTotalNumTimesPlayed = 0;
  for (const std::pair<const StepsID, HighScoresForASteps>& j :
       hsSong->steps_high_scores_) {
    const HighScoresForASteps& hsSteps = j.second;

    iTotalNumTimesPlayed += hsSteps.hsl_.GetNumTimesPlayed();
  }
  return iTotalNumTimesPlayed;
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
    const Game* pGameType, std::string& sModifiersOut) const {
  std::map<std::string, std::string>::const_iterator it;
  it = default_modifiers_.find(pGameType->name_);
  if (it == default_modifiers_.end()) {
    return false;
  }
  sModifiersOut = it->second;
  return true;
}

void Profile::SetDefaultModifiers(
    const Game* pGameType, const std::string& sModifiers) {
  if (sModifiers == "") {
    default_modifiers_.erase(pGameType->name_);
  } else {
    default_modifiers_[pGameType->name_] = sModifiers;
  }
}

bool Profile::IsCodeUnlocked(std::string sUnlockEntryID) const {
  return unlocked_entry_ids_.find(sUnlockEntryID) != unlocked_entry_ids_.end();
}

Song* Profile::GetMostPopularSong() const {
  int iMaxNumTimesPlayed = 0;
  SongID id;
  for (const std::pair<const SongID, HighScoresForASong>& i :
       song_high_scores_) {
    int iNumTimesPlayed = i.second.GetNumTimesPlayed();
    if (i.first.ToSong() != nullptr && iNumTimesPlayed > iMaxNumTimesPlayed) {
      id = i.first;
      iMaxNumTimesPlayed = iNumTimesPlayed;
    }
  }

  return id.ToSong();
}

Course* Profile::GetMostPopularCourse() const {
  int iMaxNumTimesPlayed = 0;
  CourseID id;
  for (const std::pair<const CourseID, HighScoresForACourse>& i :
       course_high_scores_) {
    int iNumTimesPlayed = i.second.GetNumTimesPlayed();
    if (i.first.ToCourse() != nullptr && iNumTimesPlayed > iMaxNumTimesPlayed) {
      id = i.first;
      iMaxNumTimesPlayed = iNumTimesPlayed;
    }
  }

  return id.ToCourse();
}

// Steps high scores
void Profile::AddStepsHighScore(
    const Song* pSong, const Steps* pSteps, HighScore hs, int& iIndexOut) {
  GetStepsHighScoreList(pSong, pSteps).AddHighScore(hs, iIndexOut, IsMachine());
}

const HighScoreList& Profile::GetStepsHighScoreList(
    const Song* pSong, const Steps* pSteps) const {
  return ((Profile*)this)->GetStepsHighScoreList(pSong, pSteps);
}

HighScoreList& Profile::GetStepsHighScoreList(
    const Song* pSong, const Steps* pSteps) {
  SongID songID;
  songID.FromSong(pSong);

  StepsID stepsID;
  stepsID.FromSteps(pSteps);

  HighScoresForASong& hsSong =
      song_high_scores_[songID];  // operator[] inserts into map
  HighScoresForASteps& hsSteps =
      hsSong.steps_high_scores_[stepsID];  // operator[] inserts into map

  return hsSteps.hsl_;
}

DateTime Profile::GetSongLastPlayedDateTime(const Song* pSong) const {
  SongID id;
  id.FromSong(pSong);
  std::map<SongID, HighScoresForASong>::const_iterator iter =
      song_high_scores_.find(id);

  // don't call this unless has been played once
  ASSERT(iter != song_high_scores_.end());
  ASSERT(!iter->second.steps_high_scores_.empty());

  DateTime dtLatest;  // starts out zeroed
  for (const std::pair<const StepsID, HighScoresForASteps>& i :
       iter->second.steps_high_scores_) {
    const HighScoreList& hsl = i.second.hsl_;
    if (hsl.GetNumTimesPlayed() == 0) {
      continue;
    }
    if (dtLatest < hsl.GetLastPlayed()) {
      dtLatest = hsl.GetLastPlayed();
    }
  }
  return dtLatest;
}

bool Profile::HasPassedSteps(const Song* pSong, const Steps* pSteps) const {
  const HighScoreList& hsl = GetStepsHighScoreList(pSong, pSteps);
  Grade grade = hsl.GetTopScore().GetGrade();
  switch (grade) {
    case Grade_Failed:
    case Grade_NoData:
      return false;
    default:
      return true;
  }
}

bool Profile::HasPassedAnyStepsInSong(const Song* pSong) const {
  const auto& steps = pSong->GetAllSteps();
  return std::any_of(steps.begin(), steps.end(), [&](const Steps* s) {
    return HasPassedSteps(pSong, s);
  });
}

void Profile::IncrementStepsPlayCount(const Song* pSong, const Steps* pSteps) {
  DateTime now = DateTime::GetNowDate();
  GetStepsHighScoreList(pSong, pSteps).IncrementPlayCount(now);
}

void Profile::GetGrades(
    const Song* pSong, StepsType st, int iCounts[NUM_Grade]) const {
  SongID songID;
  songID.FromSong(pSong);

  memset(iCounts, 0, sizeof(int) * NUM_Grade);
  const HighScoresForASong* hsSong = GetHighScoresForASong(songID);
  if (hsSong == nullptr) {
    return;
  }

  FOREACH_ENUM(Grade, g) {
    for (const std::pair<const StepsID, HighScoresForASteps>& it :
         hsSong->steps_high_scores_) {
      const StepsID& id = it.first;
      if (!id.MatchesStepsType(st)) {
        continue;
      }

      const HighScoresForASteps& hsSteps = it.second;
      if (hsSteps.hsl_.GetTopScore().GetGrade() == g) {
        iCounts[g]++;
      }
    }
  }
}

// Course high scores
void Profile::AddCourseHighScore(
    const Course* pCourse, const Trail* pTrail, HighScore hs, int& iIndexOut) {
  GetCourseHighScoreList(pCourse, pTrail)
      .AddHighScore(hs, iIndexOut, IsMachine());
}

const HighScoreList& Profile::GetCourseHighScoreList(
    const Course* pCourse, const Trail* pTrail) const {
  return ((Profile*)this)->GetCourseHighScoreList(pCourse, pTrail);
}

HighScoreList& Profile::GetCourseHighScoreList(
    const Course* pCourse, const Trail* pTrail) {
  CourseID courseID;
  courseID.FromCourse(pCourse);

  TrailID trailID;
  trailID.FromTrail(pTrail);

  HighScoresForACourse& hsCourse =
      course_high_scores_[courseID];  // operator[] inserts into map
  HighScoresForATrail& hsTrail =
      hsCourse.trail_high_scores_[trailID];  // operator[] inserts into map

  return hsTrail.hsl_;
}

int Profile::GetCourseNumTimesPlayed(const Course* pCourse) const {
  CourseID courseID;
  courseID.FromCourse(pCourse);

  return GetCourseNumTimesPlayed(courseID);
}

int Profile::GetCourseNumTimesPlayed(const CourseID& courseID) const {
  const HighScoresForACourse* hsCourse = GetHighScoresForACourse(courseID);
  if (hsCourse == nullptr) {
    return 0;
  }

  int iTotalNumTimesPlayed = 0;
  for (const std::pair<const TrailID, HighScoresForATrail>& j :
       hsCourse->trail_high_scores_) {
    const HighScoresForATrail& hsTrail = j.second;

    iTotalNumTimesPlayed += hsTrail.hsl_.GetNumTimesPlayed();
  }
  return iTotalNumTimesPlayed;
}

DateTime Profile::GetCourseLastPlayedDateTime(const Course* pCourse) const {
  CourseID id;
  id.FromCourse(pCourse);
  std::map<CourseID, HighScoresForACourse>::const_iterator iter =
      course_high_scores_.find(id);

  // don't call this unless has been played once
  ASSERT(iter != course_high_scores_.end());
  ASSERT(!iter->second.trail_high_scores_.empty());

  DateTime dtLatest;  // starts out zeroed
  for (const std::pair<const TrailID, HighScoresForATrail>& i :
       iter->second.trail_high_scores_) {
    const HighScoreList& hsl = i.second.hsl_;
    if (hsl.GetNumTimesPlayed() == 0) {
      continue;
    }
    if (dtLatest < hsl.GetLastPlayed()) {
      dtLatest = hsl.GetLastPlayed();
    }
  }
  return dtLatest;
}

void Profile::IncrementCoursePlayCount(
    const Course* pCourse, const Trail* pTrail) {
  DateTime now = DateTime::GetNowDate();
  GetCourseHighScoreList(pCourse, pTrail).IncrementPlayCount(now);
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
    StepsType st, RankingCategory rc, HighScore hs, int& iIndexOut) {
  category_high_scores_[st][rc].AddHighScore(hs, iIndexOut, IsMachine());
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
  int iNumTimesPlayed = 0;
  FOREACH_ENUM(RankingCategory, rc)
  iNumTimesPlayed += category_high_scores_[st][rc].GetNumTimesPlayed();
  return iNumTimesPlayed;
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

void Profile::LoadCustomFunction(std::string sDir, PlayerNumber pn) {
  /* Get the theme's custom load function:
   *   [Profile]
   *   CustomLoadFunction=function(profile, profileDir) ... end
   */
  Lua* L = LUA->Get();
  LuaReference customLoadFunc =
      THEME->GetMetricR("Profile", "CustomLoadFunction");
  customLoadFunc.PushSelf(L);
  ASSERT_M(!lua_isnil(L, -1), "CustomLoadFunction not defined");

  // Pass profile and profile directory as arguments
  this->PushSelf(L);
  LuaHelpers::Push(L, sDir);
  if (pn == PlayerNumber_Invalid) {
    lua_pushnil(L);
  } else {
    Enum::Push(L, pn);
  }

  // Run it
  std::string Error = "Error running CustomLoadFunction: ";
  LuaHelpers::RunScriptOnStack(L, Error, 3, 0, true);

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
    std::string sDir, bool bRequireSignature) {
  LOG->Trace("Profile::LoadAllFromDir( %s )", sDir.c_str());

  ASSERT(Right(sDir, 1) == "/");

  InitAll();

  LoadTypeFromDir(sDir);
  // Not critical if this fails
  LoadEditableDataFromDir(sDir);

  ProfileLoadResult ret = LoadStatsFromDir(sDir, bRequireSignature);
  if (ret != ProfileLoadResult_Success) {
    return ret;
  }

  LoadCustomFunction(sDir, PlayerNumber_Invalid);

  return ProfileLoadResult_Success;
}

// Custom songs are not stored with all the normal songs because walking the
// entire song list to remove custom songs when unloading the profile is
// wasteful. -Kyz

void Profile::LoadSongsFromDir(
    const std::string& dir, ProfileSlot prof_slot, bool isMemoryCard) {
  // Custom songs not enabled or not loading from MemoryCard
  if (!PREFSMAN->custom_songs_enable_ || !isMemoryCard) {
    return;
  }

  std::string songs_folder = dir + "Songs";

  if (!FILEMAN->DoesFileExist(songs_folder)) {
    LOG->Trace("No songs folder in profile.");
    return;
  }

  LOG->Trace("Found songs folder in profile.");
  RageTimer song_load_start_time;
  song_load_start_time.Touch();

  // first level -> can be packs or single songs
  std::vector<std::string> first_level_folders;
  FILEMAN->GetDirListing(songs_folder + "/*", first_level_folders, true, true);

  // Songs empty
  if (first_level_folders.empty()) {
    LOG->Trace("No custom songs in profile.");
    return;
  }

  StripCvsAndSvn(first_level_folders);
  StripMacResourceForks(first_level_folders);

  groups_.clear();
  groups_.emplace_back(new Group(songs_folder, "", prof_slot));

  std::vector<std::string> all_songs_dirs;

  for (const std::string& folder_first_level : first_level_folders) {
    std::vector<std::string> second_level_folders;

    FILEMAN->GetDirListing(
        folder_first_level + "/*", second_level_folders, true, true);

    // no second level -> is a song folder
    if (second_level_folders.empty()) {
      all_songs_dirs.push_back(folder_first_level);
    }
    // is a pack
    else {
      // create group -> need to check at the end if has songs
      groups_.emplace_back(new Group(
          folder_first_level, Basename(folder_first_level), prof_slot));

      all_songs_dirs.insert(
          all_songs_dirs.end(), second_level_folders.begin(),
          second_level_folders.end());
    }
  }

  LOG->Trace("Found %i songs in profile.", int(all_songs_dirs.size()));

  // Only songs that are successfully loaded count towards the limit. -Kyz
  for (std::string& song_dir : all_songs_dirs) {
    if (songs_.size() >= PREFSMAN->custom_songs_max_count_) {
      break;
    }
    if (song_load_start_time.Ago() > PREFSMAN->custom_songs_load_timeout_) {
      break;
    }

    auto song = std::make_unique<Song>();
    if (!song->LoadFromSongDir(song_dir, false, prof_slot)) {
      LOG->Trace("Song %s failed to load.", song_dir.c_str());
      continue;
    }

    song->SetEnabled(true);
    songs_.push_back(song.get());

    // link songs with groupName to find them easily
    songs_groups_[song->group_name_].push_back(song.release());
  }

  float load_time = song_load_start_time.Ago();
  LOG->Trace(
      "Successfully loaded %zu songs in %.6f from profile.", songs_.size(),
      load_time);

  // no songs
  if (songs_.empty()) {
    groups_.clear();
    LOG->Trace("No valid songs were loaded.");
    return;
  }

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

  int iError;
  std::unique_ptr<RageFileBasic> pFile(
      FILEMAN->Open(fn, RageFile::READ, iError));
  if (pFile.get() == nullptr) {
    LOG->Trace("Error opening %s: %s", fn.c_str(), strerror(iError));
    return ProfileLoadResult_FailedTampered;
  }

  if (compressed) {
    std::string sError;
    uint32_t iCRC32;
    RageFileObjInflate* pInflate = GunzipFile(pFile.release(), sError, &iCRC32);
    if (pInflate == nullptr) {
      LOG->Trace("Error opening %s: %s", fn.c_str(), sError.c_str());
      return ProfileLoadResult_FailedTampered;
    }

    pFile.reset(pInflate);
  }

  // Don't load unreasonably large stats.xml files.
  if (!IsMachine())  // only check stats coming from the player
  {
    int iBytes = pFile->GetFileSize();
    if (iBytes > MAX_PLAYER_STATS_XML_SIZE_BYTES) {
      LuaHelpers::ReportScriptErrorFmt(
          "The file '%s' is unreasonably large.  It won't be loaded.",
          fn.c_str());
      return ProfileLoadResult_FailedTampered;
    }
  }

  if (require_signature) {
    std::string sStatsXmlSigFile = fn + SIGNATURE_APPEND;
    std::string sDontShareFile = dir + DONT_SHARE_SIG;

    LOG->Trace(
        "Verifying don't share signature \"%s\" against \"%s\"",
        sDontShareFile.c_str(), sStatsXmlSigFile.c_str());
    // verify the stats.xml signature with the "don't share" file
    if (!CryptManager::VerifyFileWithFile(sStatsXmlSigFile, sDontShareFile)) {
      LuaHelpers::ReportScriptErrorFmt(
          "The don't share check for '%s' failed.  Data will be ignored.",
          sStatsXmlSigFile.c_str());
      return ProfileLoadResult_FailedTampered;
    }
    LOG->Trace("Done.");

    // verify stats.xml
    LOG->Trace("Verifying stats.xml signature");
    if (!CryptManager::VerifyFileWithFile(fn, sStatsXmlSigFile)) {
      LuaHelpers::ReportScriptErrorFmt(
          "The signature check for '%s' failed.  Data will be ignored.",
          fn.c_str());
      return ProfileLoadResult_FailedTampered;
    }
    LOG->Trace("Done.");
  }

  LOG->Trace("Loading %s", fn.c_str());
  XNode xml;
  if (!XmlFileUtil::LoadFromFileShowErrors(xml, *pFile.get())) {
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
    const XNode* xml, bool bIgnoreEditable) {
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
  std::string sName = display_name_;
  std::string sCharacterID = character_id_;
  std::string sLastUsedHighScoreName = last_used_high_score_name_;
  int iWeightPounds = weight_pounds_;
  float Voomax = voomax_;
  int BirthYear = birth_year_;
  bool IgnoreStepCountCalories = ignore_step_count_calories_;
  bool IsMale = is_male_;

  LOAD_NODE(GeneralData);
  LOAD_NODE(SongScores);
  LOAD_NODE(CourseScores);
  LOAD_NODE(CategoryScores);
  LOAD_NODE(ScreenshotData);
  LOAD_NODE(CalorieData);

  if (bIgnoreEditable) {
    display_name_ = sName;
    character_id_ = sCharacterID;
    last_used_high_score_name_ = sLastUsedHighScoreName;
    weight_pounds_ = iWeightPounds;
    voomax_ = Voomax;
    birth_year_ = BirthYear;
    ignore_step_count_calories_ = IgnoreStepCountCalories;
    is_male_ = IsMale;
  }

  return ProfileLoadResult_Success;
}

bool Profile::SaveAllToDir(std::string sDir, bool bSignData) const {
  last_played_machine_guid_ = PROFILEMAN->GetMachineProfile()->guid_;
  last_played_date_ = DateTime::GetNowDate();

  SaveTypeToDir(sDir);
  // Save editable.ini
  SaveEditableDataToDir(sDir);

  bool bSaved = SaveStatsXmlToDir(sDir, bSignData);

  SaveStatsWebPageToDir(sDir);

  // Empty directories if none exist.
  if (ProfileManager::profile_step_edits_) {
    FILEMAN->CreateDir(sDir + EDIT_STEPS_SUBDIR);
  }
  if (ProfileManager::profile_course_edits_) {
    FILEMAN->CreateDir(sDir + EDIT_COURSES_SUBDIR);
  }
  if (PREFSMAN->custom_songs_enable_) {
    FILEMAN->CreateDir(sDir + SONGS_SUBDIR);
  }
  FILEMAN->CreateDir(sDir + SCREENSHOTS_SUBDIR);
  FILEMAN->CreateDir(sDir + RIVAL_SUBDIR);

  /* Get the theme's custom save function:
   *   [Profile]
   *   CustomSaveFunction=function(profile, profileDir) ... end
   */
  Lua* L = LUA->Get();
  LuaReference customSaveFunc =
      THEME->GetMetricR("Profile", "CustomSaveFunction");
  customSaveFunc.PushSelf(L);
  ASSERT_M(!lua_isnil(L, -1), "CustomSaveFunction not defined");

  // Pass profile and profile directory as arguments
  const_cast<Profile*>(this)->PushSelf(L);
  LuaHelpers::Push(L, sDir);

  // Run it
  std::string Error = "Error running CustomSaveFunction: ";
  LuaHelpers::RunScriptOnStack(L, Error, 2, 0, true);

  LUA->Release(L);

  return bSaved;
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

bool Profile::SaveStatsXmlToDir(std::string sDir, bool bSignData) const {
  LOG->Trace("SaveStatsXmlToDir: %s", sDir.c_str());
  std::unique_ptr<XNode> xml(SaveStatsXmlCreateNode());

  sDir = sDir + PROFILEMAN->GetStatsPrefix();
  // Save stats.xml
  std::string fn = sDir + (g_bProfileDataCompress ? STATS_XML_GZ : STATS_XML);

  {
    std::string sError;
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
      if (FILEMAN->IsAFile(sDir + STATS_XML)) {
        FILEMAN->Remove(sDir + STATS_XML);
      }
    } else {
      if (!XmlFileUtil::SaveToFile(xml.get(), f, "", false)) {
        return false;
      }

      /* After successfully saving STATS_XML, remove any stray STATS_XML_GZ. */
      if (FILEMAN->IsAFile(sDir + STATS_XML_GZ)) {
        FILEMAN->Remove(sDir + STATS_XML_GZ);
      }
    }
  }

  if (bSignData) {
    std::string sStatsXmlSigFile = fn + SIGNATURE_APPEND;
    CryptManager::SignFileToFile(fn, sStatsXmlSigFile);

    // Save the "don't share" file
    std::string sDontShareFile = sDir + DONT_SHARE_SIG;
    CryptManager::SignFileToFile(sStatsXmlSigFile, sDontShareFile);
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

void Profile::SaveEditableDataToDir(std::string sDir) const {
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

  ini.WriteFile(sDir + EDITABLE_INI);
}

XNode* Profile::SaveGeneralDataCreateNode() const {
  XNode* pGeneralDataNode = new XNode("GeneralData");

  // TRICKY: These are write-only elements that are normally never read again.
  // This data is required by other apps (like internet ranking), but is
  // redundant to the game app.
  pGeneralDataNode->AppendChild("DisplayName", GetDisplayNameOrHighScoreName());
  pGeneralDataNode->AppendChild("CharacterID", character_id_);
  pGeneralDataNode->AppendChild(
      "LastUsedHighScoreName", last_used_high_score_name_);
  pGeneralDataNode->AppendChild("WeightPounds", weight_pounds_);
  pGeneralDataNode->AppendChild("Voomax", voomax_);
  pGeneralDataNode->AppendChild("BirthYear", birth_year_);
  pGeneralDataNode->AppendChild(
      "IgnoreStepCountCalories", ignore_step_count_calories_);
  pGeneralDataNode->AppendChild("IsMale", is_male_);

  pGeneralDataNode->AppendChild("IsMachine", IsMachine());

  pGeneralDataNode->AppendChild("Guid", guid_);
  pGeneralDataNode->AppendChild("SortOrder", SortOrderToString(sort_order_));
  pGeneralDataNode->AppendChild(
      "LastDifficulty", DifficultyToString(last_difficulty_));
  pGeneralDataNode->AppendChild(
      "LastCourseDifficulty", DifficultyToString(last_course_difficulty_));
  if (last_steps_type_ != StepsType_Invalid) {
    pGeneralDataNode->AppendChild(
        "LastStepsType", GAMEMAN->GetStepsTypeInfo(last_steps_type_).name_);
  }
  pGeneralDataNode->AppendChild(last_song_.CreateNode());
  pGeneralDataNode->AppendChild(last_course_.CreateNode());
  pGeneralDataNode->AppendChild("CurrentCombo", current_combo_);
  pGeneralDataNode->AppendChild("TotalSessions", total_sessions_);
  pGeneralDataNode->AppendChild("TotalSessionSeconds", total_session_seconds_);
  pGeneralDataNode->AppendChild(
      "TotalGameplaySeconds", total_gameplay_seconds_);
  pGeneralDataNode->AppendChild("TotalCaloriesBurned", total_calories_burned_);
  pGeneralDataNode->AppendChild("GoalType", goal_type_);
  pGeneralDataNode->AppendChild("GoalCalories", goal_calories_);
  pGeneralDataNode->AppendChild("GoalSeconds", goal_seconds_);
  pGeneralDataNode->AppendChild(
      "LastPlayedMachineGuid", last_played_machine_guid_);
  pGeneralDataNode->AppendChild(
      "LastPlayedDate", last_played_date_.GetString());
  pGeneralDataNode->AppendChild("TotalDancePoints", total_dance_points_);
  pGeneralDataNode->AppendChild(
      "NumExtraStagesPassed", num_extra_stages_passed_);
  pGeneralDataNode->AppendChild(
      "NumExtraStagesFailed", num_extra_stages_failed_);
  pGeneralDataNode->AppendChild("NumToasties", num_toasties_);
  pGeneralDataNode->AppendChild("TotalTapsAndHolds", total_taps_and_holds_);
  pGeneralDataNode->AppendChild("TotalJumps", total_jumps_);
  pGeneralDataNode->AppendChild("TotalHolds", total_holds_);
  pGeneralDataNode->AppendChild("TotalRolls", total_rolls_);
  pGeneralDataNode->AppendChild("TotalMines", total_mines_);
  pGeneralDataNode->AppendChild("TotalHands", total_hands_);
  pGeneralDataNode->AppendChild("TotalLifts", total_lifts_);

  // Keep declared variables in a very local scope so they aren't
  // accidentally used where they're not intended.  There's a lot of
  // copying and pasting in this code.

  {
    XNode* pDefaultModifiers =
        pGeneralDataNode->AppendChild("DefaultModifiers");
    for (std::pair<const std::string&, std::string> it : default_modifiers_) {
      pDefaultModifiers->AppendChild(it.first, it.second);
    }
  }

  {
    XNode* pUnlocks = pGeneralDataNode->AppendChild("Unlocks");
    for (const std::string& unlockEntry : unlocked_entry_ids_) {
      XNode* pEntry = pUnlocks->AppendChild("UnlockEntry");
      pEntry->AppendAttr("UnlockEntryID", unlockEntry);
      if (!UNLOCK_AUTH_STRING.GetValue().empty()) {
        std::string sUnlockAuth = BinaryToHex(CRYPTMAN->GetMD5ForString(
            unlockEntry + UNLOCK_AUTH_STRING.GetValue()));
        pEntry->AppendAttr("Auth", sUnlockAuth);
      }
    }
  }

  {
    XNode* pNumSongsPlayedByPlayMode =
        pGeneralDataNode->AppendChild("NumSongsPlayedByPlayMode");
    FOREACH_ENUM(PlayMode, pm) {
      // Don't save unplayed PlayModes.
      if (!num_songs_played_by_play_mode_[pm]) {
        continue;
      }
      pNumSongsPlayedByPlayMode->AppendChild(
          PlayModeToString(pm), num_songs_played_by_play_mode_[pm]);
    }
  }

  {
    XNode* pNumSongsPlayedByStyle =
        pGeneralDataNode->AppendChild("NumSongsPlayedByStyle");
    for (const std::pair<const StyleID, int>& iter :
         num_songs_played_by_style_) {
      const StyleID& s = iter.first;
      int iNumPlays = iter.second;

      XNode* pStyleNode = s.CreateNode();
      pStyleNode->AppendAttr(XNode::kTextAttribute, iNumPlays);

      pNumSongsPlayedByStyle->AppendChild(pStyleNode);
    }
  }

  {
    XNode* pNumSongsPlayedByDifficulty =
        pGeneralDataNode->AppendChild("NumSongsPlayedByDifficulty");
    FOREACH_ENUM(Difficulty, dc) {
      if (!num_songs_played_by_difficulty_[dc]) {
        continue;
      }
      pNumSongsPlayedByDifficulty->AppendChild(
          DifficultyToString(dc), num_songs_played_by_difficulty_[dc]);
    }
  }

  {
    XNode* pNumSongsPlayedByMeter =
        pGeneralDataNode->AppendChild("NumSongsPlayedByMeter");
    for (int i = 0; i < MAX_METER + 1; i++) {
      if (!num_songs_played_by_meter_[i]) {
        continue;
      }
      pNumSongsPlayedByMeter->AppendChild(
          ssprintf("Meter%d", i), num_songs_played_by_meter_[i]);
    }
  }

  pGeneralDataNode->AppendChild("NumTotalSongsPlayed", num_total_songs_played_);

  {
    XNode* pNumStagesPassedByPlayMode =
        pGeneralDataNode->AppendChild("NumStagesPassedByPlayMode");
    FOREACH_ENUM(PlayMode, pm) {
      // Don't save unplayed PlayModes.
      if (!num_stages_passed_by_play_mode_[pm]) {
        continue;
      }
      pNumStagesPassedByPlayMode->AppendChild(
          PlayModeToString(pm), num_stages_passed_by_play_mode_[pm]);
    }
  }

  {
    XNode* pNumStagesPassedByGrade =
        pGeneralDataNode->AppendChild("NumStagesPassedByGrade");
    FOREACH_ENUM(Grade, g) {
      if (!num_stages_passed_by_grade_[g]) {
        continue;
      }
      pNumStagesPassedByGrade->AppendChild(
          GradeToString(g), num_stages_passed_by_grade_[g]);
    }
  }

  // Load Lua UserTable from profile
  if (!IsMachine() && user_table_.IsSet()) {
    Lua* L = LUA->Get();
    user_table_.PushSelf(L);
    XNode* pUserTable = XmlFileUtil::XNodeFromTable(L);
    LUA->Release(L);

    // XXX: XNodeFromTable returns a root node with the name "Layer".
    pUserTable->name_ = "UserTable";
    pGeneralDataNode->AppendChild(pUserTable);
  }

  return pGeneralDataNode;
}

ProfileLoadResult Profile::LoadEditableDataFromDir(std::string sDir) {
  std::string fn = sDir + EDITABLE_INI;

  // Don't load unreasonably large editable.xml files.
  int iBytes = FILEMAN->GetFileSizeInBytes(fn);
  if (iBytes > MAX_EDITABLE_INI_SIZE_BYTES) {
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

void Profile::LoadGeneralDataFromNode(const XNode* pNode) {
  ASSERT(pNode->GetName() == "GeneralData");

  std::string s;
  const XNode* pTemp;

  pNode->GetChildValue("DisplayName", display_name_);
  pNode->GetChildValue("CharacterID", character_id_);
  pNode->GetChildValue("LastUsedHighScoreName", last_used_high_score_name_);
  pNode->GetChildValue("WeightPounds", weight_pounds_);
  pNode->GetChildValue("Voomax", voomax_);
  pNode->GetChildValue("BirthYear", birth_year_);
  pNode->GetChildValue("IgnoreStepCountCalories", ignore_step_count_calories_);
  pNode->GetChildValue("IsMale", is_male_);
  pNode->GetChildValue("Guid", guid_);
  pNode->GetChildValue("SortOrder", s);
  sort_order_ = StringToSortOrder(s);
  pNode->GetChildValue("LastDifficulty", s);
  last_difficulty_ = StringToDifficulty(s);
  pNode->GetChildValue("LastCourseDifficulty", s);
  last_course_difficulty_ = StringToDifficulty(s);
  pNode->GetChildValue("LastStepsType", s);
  last_steps_type_ = GAMEMAN->StringToStepsType(s);
  pTemp = pNode->GetChild("Song");
  if (pTemp) {
    last_song_.LoadFromNode(pTemp);
  }
  pTemp = pNode->GetChild("Course");
  if (pTemp) {
    last_course_.LoadFromNode(pTemp);
  }
  pNode->GetChildValue("CurrentCombo", current_combo_);
  pNode->GetChildValue("TotalSessions", total_sessions_);
  pNode->GetChildValue("TotalSessionSeconds", total_session_seconds_);
  pNode->GetChildValue("TotalGameplaySeconds", total_gameplay_seconds_);
  pNode->GetChildValue("TotalCaloriesBurned", total_calories_burned_);
  pNode->GetChildValue("GoalType", *ConvertValue<int>(&goal_type_));
  pNode->GetChildValue("GoalCalories", goal_calories_);
  pNode->GetChildValue("GoalSeconds", goal_seconds_);
  pNode->GetChildValue("LastPlayedMachineGuid", last_played_machine_guid_);
  pNode->GetChildValue("LastPlayedDate", s);
  last_played_date_.FromString(s);
  pNode->GetChildValue("TotalDancePoints", total_dance_points_);
  pNode->GetChildValue("NumExtraStagesPassed", num_extra_stages_passed_);
  pNode->GetChildValue("NumExtraStagesFailed", num_extra_stages_failed_);
  pNode->GetChildValue("NumToasties", num_toasties_);
  pNode->GetChildValue("TotalTapsAndHolds", total_taps_and_holds_);
  pNode->GetChildValue("TotalJumps", total_jumps_);
  pNode->GetChildValue("TotalHolds", total_holds_);
  pNode->GetChildValue("TotalRolls", total_rolls_);
  pNode->GetChildValue("TotalMines", total_mines_);
  pNode->GetChildValue("TotalHands", total_hands_);
  pNode->GetChildValue("TotalLifts", total_lifts_);

  {
    const XNode* pDefaultModifiers = pNode->GetChild("DefaultModifiers");
    if (pDefaultModifiers) {
      FOREACH_CONST_Child(pDefaultModifiers, game_type) {
        game_type->GetTextValue(default_modifiers_[game_type->GetName()]);
      }
    }
  }

  {
    const XNode* pUnlocks = pNode->GetChild("Unlocks");
    if (pUnlocks) {
      FOREACH_CONST_Child(pUnlocks, unlock) {
        std::string sUnlockEntryID;
        if (!unlock->GetAttrValue("UnlockEntryID", sUnlockEntryID)) {
          continue;
        }

        if (!UNLOCK_AUTH_STRING.GetValue().empty()) {
          std::string sUnlockAuth;
          if (!unlock->GetAttrValue("Auth", sUnlockAuth)) {
            continue;
          }

          std::string sExpectedUnlockAuth =
              BinaryToHex(CRYPTMAN->GetMD5ForString(
                  sUnlockEntryID + UNLOCK_AUTH_STRING.GetValue()));
          if (sUnlockAuth != sExpectedUnlockAuth) {
            continue;
          }
        }

        unlocked_entry_ids_.insert(sUnlockEntryID);
      }
    }
  }

  {
    const XNode* pNumSongsPlayedByPlayMode =
        pNode->GetChild("NumSongsPlayedByPlayMode");
    if (pNumSongsPlayedByPlayMode) {
      FOREACH_ENUM(PlayMode, pm)
      pNumSongsPlayedByPlayMode->GetChildValue(
          PlayModeToString(pm), num_songs_played_by_play_mode_[pm]);
    }
  }

  {
    const XNode* pNumSongsPlayedByStyle =
        pNode->GetChild("NumSongsPlayedByStyle");
    if (pNumSongsPlayedByStyle) {
      FOREACH_CONST_Child(pNumSongsPlayedByStyle, style) {
        if (style->GetName() != "Style") {
          continue;
        }

        StyleID sID;
        sID.LoadFromNode(style);

        if (!sID.IsValid()) {
          WARN_AND_CONTINUE;
        }

        style->GetTextValue(num_songs_played_by_style_[sID]);
      }
    }
  }

  {
    const XNode* pNumSongsPlayedByDifficulty =
        pNode->GetChild("NumSongsPlayedByDifficulty");
    if (pNumSongsPlayedByDifficulty) {
      FOREACH_ENUM(Difficulty, dc)
      pNumSongsPlayedByDifficulty->GetChildValue(
          DifficultyToString(dc), num_songs_played_by_difficulty_[dc]);
    }
  }

  {
    const XNode* pNumSongsPlayedByMeter =
        pNode->GetChild("NumSongsPlayedByMeter");
    if (pNumSongsPlayedByMeter) {
      for (int i = 0; i < MAX_METER + 1; i++) {
        pNumSongsPlayedByMeter->GetChildValue(
            ssprintf("Meter%d", i), num_songs_played_by_meter_[i]);
      }
    }
  }

  pNode->GetChildValue("NumTotalSongsPlayed", num_total_songs_played_);

  {
    const XNode* pNumStagesPassedByGrade =
        pNode->GetChild("NumStagesPassedByGrade");
    if (pNumStagesPassedByGrade) {
      FOREACH_ENUM(Grade, g)
      pNumStagesPassedByGrade->GetChildValue(
          GradeToString(g), num_stages_passed_by_grade_[g]);
    }
  }

  {
    const XNode* pNumStagesPassedByPlayMode =
        pNode->GetChild("NumStagesPassedByPlayMode");
    if (pNumStagesPassedByPlayMode) {
      FOREACH_ENUM(PlayMode, pm)
      pNumStagesPassedByPlayMode->GetChildValue(
          PlayModeToString(pm), num_stages_passed_by_play_mode_[pm]);
    }
  }

  // Build the custom data table from the existing XNode.
  if (!IsMachine()) {
    const XNode* pUserTable = pNode->GetChild("UserTable");

    Lua* L = LUA->Get();

    // If we have custom data, load it. Otherwise, make a blank table.
    if (pUserTable) {
      LuaHelpers::CreateTableFromXNode(L, pUserTable);
    } else {
      lua_newtable(L);
    }

    user_table_.SetFromStack(L);
    LUA->Release(L);
  }
}

void Profile::AddStepTotals(
    int iTotalTapsAndHolds, int iTotalJumps, int iTotalHolds, int iTotalRolls,
    int iTotalMines, int iTotalHands, int iTotalLifts, float fCaloriesBurned) {
  total_taps_and_holds_ += iTotalTapsAndHolds;
  total_jumps_ += iTotalJumps;
  total_holds_ += iTotalHolds;
  total_rolls_ += iTotalRolls;
  total_mines_ += iTotalMines;
  total_hands_ += iTotalHands;
  total_lifts_ += iTotalLifts;

  if (!ignore_step_count_calories_) {
    total_calories_burned_ += fCaloriesBurned;
    DateTime date = DateTime::GetNowDate();
    map_day_to_calories_burned_[date].cals_ += fCaloriesBurned;
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

float Profile::CalculateCaloriesFromHeartRate(float HeartRate, float Duration) {
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
  Duration = Duration / 60.0;
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
  return ((gender_factor + (heart_factor * HeartRate) + (voo_factor * voomax_) +
           (weight_factor * kilos) + (age_factor + age)) /
          4.184) *
         Duration;
}

XNode* Profile::SaveSongScoresCreateNode() const {
  CHECKPOINT_M("Getting the node to save song scores.");

  const Profile* pProfile = this;
  ASSERT(pProfile != nullptr);

  XNode* pNode = new XNode("SongScores");

  for (const std::pair<const SongID, HighScoresForASong>& i :
       song_high_scores_) {
    const SongID& songID = i.first;
    const HighScoresForASong& hsSong = i.second;

    // skip songs that have never been played
    if (pProfile->GetSongNumTimesPlayed(songID) == 0) {
      continue;
    }

    XNode* pSongNode = pNode->AppendChild(songID.CreateNode());

    int jCheck2 = hsSong.steps_high_scores_.size();
    int jCheck1 = 0;
    for (const std::pair<const StepsID, HighScoresForASteps>& j :
         hsSong.steps_high_scores_) {
      jCheck1++;
      ASSERT(jCheck1 <= jCheck2);
      const StepsID& stepsID = j.first;
      const HighScoresForASteps& hsSteps = j.second;

      const HighScoreList& hsl = hsSteps.hsl_;

      // skip steps that have never been played
      if (hsl.GetNumTimesPlayed() == 0) {
        continue;
      }

      XNode* pStepsNode = pSongNode->AppendChild(stepsID.CreateNode());

      pStepsNode->AppendChild(hsl.CreateNode());
    }
  }

  return pNode;
}

void Profile::LoadSongScoresFromNode(const XNode* pSongScores) {
  CHECKPOINT_M("Loading the node that contains song scores.");

  ASSERT(pSongScores->GetName() == "SongScores");

  FOREACH_CONST_Child(pSongScores, pSong) {
    if (pSong->GetName() != "Song") {
      continue;
    }

    SongID songID;
    songID.LoadFromNode(pSong);
    // Allow invalid songs so that scores aren't deleted for people that use
    // AdditionalSongsFolders and change it frequently. -Kyz
    // if( !songID.IsValid() )
    //	continue;

    FOREACH_CONST_Child(pSong, pSteps) {
      if (pSteps->GetName() != "Steps") {
        continue;
      }

      StepsID stepsID;
      stepsID.LoadFromNode(pSteps);
      if (!stepsID.IsValid()) {
        WARN_AND_CONTINUE;
      }

      const XNode* pHighScoreListNode = pSteps->GetChild("HighScoreList");
      if (pHighScoreListNode == nullptr) {
        WARN_AND_CONTINUE;
      }

      HighScoreList& hsl =
          song_high_scores_[songID].steps_high_scores_[stepsID].hsl_;
      hsl.LoadFromNode(pHighScoreListNode);
    }
  }
}

XNode* Profile::SaveCourseScoresCreateNode() const {
  CHECKPOINT_M("Getting the node to save course scores.");

  const Profile* pProfile = this;
  ASSERT(pProfile != nullptr);

  XNode* pNode = new XNode("CourseScores");

  for (const std::pair<const CourseID, HighScoresForACourse>& i :
       course_high_scores_) {
    const CourseID& courseID = i.first;
    const HighScoresForACourse& hsCourse = i.second;

    // skip courses that have never been played
    if (pProfile->GetCourseNumTimesPlayed(courseID) == 0) {
      continue;
    }

    XNode* pCourseNode = pNode->AppendChild(courseID.CreateNode());

    for (const std::pair<const TrailID, HighScoresForATrail>& j :
         hsCourse.trail_high_scores_) {
      const TrailID& trailID = j.first;
      const HighScoresForATrail& hsTrail = j.second;

      const HighScoreList& hsl = hsTrail.hsl_;

      // skip steps that have never been played
      if (hsl.GetNumTimesPlayed() == 0) {
        continue;
      }

      XNode* pTrailNode = pCourseNode->AppendChild(trailID.CreateNode());

      pTrailNode->AppendChild(hsl.CreateNode());
    }
  }

  return pNode;
}

void Profile::LoadCourseScoresFromNode(const XNode* pCourseScores) {
  CHECKPOINT_M("Loading the node that contains course scores.");

  ASSERT(pCourseScores->GetName() == "CourseScores");

  std::vector<Course*> vpAllCourses;
  SONGMAN->GetAllCourses(vpAllCourses, true);

  FOREACH_CONST_Child(pCourseScores, pCourse) {
    if (pCourse->GetName() != "Course") {
      continue;
    }

    CourseID courseID;
    courseID.LoadFromNode(pCourse);
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
      Course* pC = courseID.ToCourse();
      if (pC == nullptr) {
        std::string sDir, sFName, sExt;
        splitpath(courseID.GetPath(), sDir, sFName, sExt);
        std::string sFullFileName = sFName + sExt;

        for (Course* c : vpAllCourses) {
          std::string sOther = Right(c->path_, sFullFileName.size());

          if (CompareNoCase(sFullFileName, sOther) == 0) {
            pC = c;
            courseID.FromCourse(pC);
            break;
          }
        }
      }
    }

    FOREACH_CONST_Child(pCourse, pTrail) {
      if (pTrail->GetName() != "Trail") {
        continue;
      }

      TrailID trailID;
      trailID.LoadFromNode(pTrail);
      if (!trailID.IsValid()) {
        WARN_AND_CONTINUE;
      }

      const XNode* pHighScoreListNode = pTrail->GetChild("HighScoreList");
      if (pHighScoreListNode == nullptr) {
        WARN_AND_CONTINUE;
      }

      HighScoreList& hsl =
          course_high_scores_[courseID].trail_high_scores_[trailID].hsl_;
      hsl.LoadFromNode(pHighScoreListNode);
    }
  }
}

XNode* Profile::SaveCategoryScoresCreateNode() const {
  CHECKPOINT_M("Getting the node that saves category scores.");

  const Profile* pProfile = this;
  ASSERT(pProfile != nullptr);

  XNode* pNode = new XNode("CategoryScores");

  FOREACH_ENUM(StepsType, st) {
    // skip steps types that have never been played
    if (pProfile->GetCategoryNumTimesPlayed(st) == 0) {
      continue;
    }

    XNode* pStepsTypeNode = pNode->AppendChild("StepsType");
    pStepsTypeNode->AppendAttr("Type", GAMEMAN->GetStepsTypeInfo(st).name_);

    FOREACH_ENUM(RankingCategory, rc) {
      // skip steps types/categories that have never been played
      if (pProfile->GetCategoryHighScoreList(st, rc).GetNumTimesPlayed() == 0) {
        continue;
      }

      XNode* pRankingCategoryNode =
          pStepsTypeNode->AppendChild("RankingCategory");
      pRankingCategoryNode->AppendAttr("Type", RankingCategoryToString(rc));

      const HighScoreList& hsl = pProfile->GetCategoryHighScoreList(
          (StepsType)st, (RankingCategory)rc);

      pRankingCategoryNode->AppendChild(hsl.CreateNode());
    }
  }

  return pNode;
}

void Profile::LoadCategoryScoresFromNode(const XNode* pCategoryScores) {
  CHECKPOINT_M("Loading the node that contains category scores.");

  ASSERT(pCategoryScores->GetName() == "CategoryScores");

  FOREACH_CONST_Child(pCategoryScores, pStepsType) {
    if (pStepsType->GetName() != "StepsType") {
      continue;
    }

    std::string str;
    if (!pStepsType->GetAttrValue("Type", str)) {
      WARN_AND_CONTINUE;
    }
    StepsType st = GAMEMAN->StringToStepsType(str);
    if (st == StepsType_Invalid) {
      WARN_AND_CONTINUE_M(str);
    }

    FOREACH_CONST_Child(pStepsType, pRadarCategory) {
      if (pRadarCategory->GetName() != "RankingCategory") {
        continue;
      }

      if (!pRadarCategory->GetAttrValue("Type", str)) {
        WARN_AND_CONTINUE;
      }
      RankingCategory rc = StringToRankingCategory(str);
      if (rc == RankingCategory_Invalid) {
        WARN_AND_CONTINUE_M(str);
      }

      const XNode* pHighScoreListNode =
          pRadarCategory->GetChild("HighScoreList");
      if (pHighScoreListNode == nullptr) {
        WARN_AND_CONTINUE;
      }

      HighScoreList& hsl = this->GetCategoryHighScoreList(st, rc);
      hsl.LoadFromNode(pHighScoreListNode);
    }
  }
}

void Profile::SaveStatsWebPageToDir(std::string) const {
  ASSERT(PROFILEMAN != nullptr);
}

void Profile::AddScreenshot(const Screenshot& screenshot) {
  screenshots_.push_back(screenshot);
}

void Profile::LoadScreenshotDataFromNode(const XNode* pScreenshotData) {
  CHECKPOINT_M("Loading the node containing screenshot data.");

  ASSERT(pScreenshotData->GetName() == "ScreenshotData");
  FOREACH_CONST_Child(pScreenshotData, pScreenshot) {
    if (pScreenshot->GetName() != "Screenshot") {
      WARN_AND_CONTINUE_M(pScreenshot->GetName());
    }

    Screenshot ss;
    ss.LoadFromNode(pScreenshot);

    screenshots_.push_back(ss);
  }
}

XNode* Profile::SaveScreenshotDataCreateNode() const {
  CHECKPOINT_M("Getting the node containing screenshot data.");

  const Profile* pProfile = this;
  ASSERT(pProfile != nullptr);

  XNode* pNode = new XNode("ScreenshotData");

  for (const Screenshot& ss : screenshots_) {
    pNode->AppendChild(ss.CreateNode());
  }

  return pNode;
}

void Profile::LoadCalorieDataFromNode(const XNode* pCalorieData) {
  CHECKPOINT_M("Loading the node containing calorie data.");

  ASSERT(pCalorieData->GetName() == "CalorieData");
  FOREACH_CONST_Child(pCalorieData, pCaloriesBurned) {
    if (pCaloriesBurned->GetName() != "CaloriesBurned") {
      WARN_AND_CONTINUE_M(pCaloriesBurned->GetName());
    }

    std::string sDate;
    if (!pCaloriesBurned->GetAttrValue("Date", sDate)) {
      WARN_AND_CONTINUE;
    }
    DateTime date;
    if (!date.FromString(sDate)) {
      WARN_AND_CONTINUE_M(sDate);
    }

    float fCaloriesBurned = 0;

    pCaloriesBurned->GetTextValue(fCaloriesBurned);

    map_day_to_calories_burned_[date].cals_ = fCaloriesBurned;
  }
}

XNode* Profile::SaveCalorieDataCreateNode() const {
  CHECKPOINT_M("Getting the node containing calorie data.");

  const Profile* pProfile = this;
  ASSERT(pProfile != nullptr);

  XNode* pNode = new XNode("CalorieData");

  for (const std::pair<const DateTime, Calories>& i :
       map_day_to_calories_burned_) {
    XNode* pCaloriesBurned =
        pNode->AppendChild("CaloriesBurned", i.second.cals_);

    pCaloriesBurned->AppendAttr("Date", i.first.GetString());
  }

  return pNode;
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
    const SongID& songID) const {
  std::map<SongID, HighScoresForASong>::const_iterator it;
  it = song_high_scores_.find(songID);
  if (it == song_high_scores_.end()) {
    return nullptr;
  }
  return &it->second;
}

const Profile::HighScoresForACourse* Profile::GetHighScoresForACourse(
    const CourseID& courseID) const {
  std::map<CourseID, HighScoresForACourse>::const_iterator it;
  it = course_high_scores_.find(courseID);
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

  const Profile* pProfile = this;
  ASSERT(pProfile != nullptr);

  XNode* pNode = new XNode("CoinData");

  {
    int coins[NUM_LAST_DAYS];
    BOOKKEEPER->GetCoinsLastDays(coins);
    XNode* p = pNode->AppendChild("LastDays");
    for (int i = 0; i < NUM_LAST_DAYS; i++) {
      p->AppendChild(LastDayToString(i), coins[i]);
    }
  }
  {
    int coins[NUM_LAST_WEEKS];
    BOOKKEEPER->GetCoinsLastWeeks(coins);
    XNode* p = pNode->AppendChild("LastWeeks");
    for (int i = 0; i < NUM_LAST_WEEKS; i++) {
      p->AppendChild(LastWeekToString(i), coins[i]);
    }
  }
  {
    int coins[DAYS_IN_WEEK];
    BOOKKEEPER->GetCoinsByDayOfWeek(coins);
    XNode* p = pNode->AppendChild("DayOfWeek");
    for (int i = 0; i < DAYS_IN_WEEK; i++) {
      p->AppendChild(DayOfWeekToString(i), coins[i]);
    }
  }
  {
    int coins[HOURS_IN_DAY];
    BOOKKEEPER->GetCoinsByHour(coins);
    XNode* p = pNode->AppendChild("Hour");
    for (int i = 0; i < HOURS_IN_DAY; i++) {
      p->AppendChild(HourInDayToString(i), coins[i]);
    }
  }

  return pNode;
}

void Profile::MoveBackupToDir(std::string sFromDir, std::string sToDir) {
  std::string stats_file;
  std::string old_stats_file;
  if (FILEMAN->IsAFile(sFromDir + STATS_XML)) {
    stats_file = STATS_XML;
    old_stats_file = STATS_XML_GZ;
  } else if (FILEMAN->IsAFile(sFromDir + STATS_XML_GZ)) {
    stats_file = STATS_XML_GZ;
    old_stats_file = STATS_XML;
  }

  if (!stats_file.empty()) {
    bool backup_complete =
        FILEMAN->Move(sFromDir + stats_file, sToDir + stats_file);
    const std::string signature_file = stats_file + SIGNATURE_APPEND;
    if (FILEMAN->IsAFile(sFromDir + signature_file)) {
      const bool signature_moved =
          FILEMAN->Move(sFromDir + signature_file, sToDir + signature_file);
      backup_complete = backup_complete && signature_moved;
    }

    // We should not leave an outdated file in LastGood for an alternate format
    // after the new backup succeeds.
    // Otherwise the wrong backup file might be loaded if LastGood gets used.
    if (backup_complete) {
      FILEMAN->Remove(sToDir + old_stats_file);
      FILEMAN->Remove(sToDir + old_stats_file + SIGNATURE_APPEND);
    }
  }

  if (FILEMAN->IsAFile(sFromDir + EDITABLE_INI)) {
    FILEMAN->Move(sFromDir + EDITABLE_INI, sToDir + EDITABLE_INI);
  }
  if (FILEMAN->IsAFile(sFromDir + DONT_SHARE_SIG)) {
    FILEMAN->Move(sFromDir + DONT_SHARE_SIG, sToDir + DONT_SHARE_SIG);
  }
}

std::string Profile::MakeUniqueFileNameNoExtension(
    std::string sDir, std::string sFileNameBeginning) {
  FILEMAN->FlushDirCache(sDir);
  // Find a file name for the screenshot
  std::vector<std::string> files;
  GetDirListing(sDir + sFileNameBeginning + "*", files, false, false);
  sort(files.begin(), files.end());

  int iIndex = 0;

  for (int i = files.size() - 1; i >= 0; --i) {
    static Regex re("^" + sFileNameBeginning + "([0-9]{5})\\....$");
    std::vector<std::string> matches;
    if (!re.Compare(files[i], matches)) {
      continue;
    }

    ASSERT(matches.size() == 1);
    iIndex = StringToInt(matches[0]) + 1;
    break;
  }

  return MakeFileNameNoExtension(sFileNameBeginning, iIndex);
}

std::string Profile::MakeFileNameNoExtension(
    std::string sFileNameBeginning, int iIndex) {
  return sFileNameBeginning + ssprintf("%05d", iIndex);
}

std::string Profile::GetCustomSongsGroupNamePrefix() const {
  std::string groupName = THEME->GetString("Profile", "ProfileCustomSongs");

  return groupName.replace(
      groupName.find("%s"), sizeof("%s") - 1, GetDisplayNameOrHighScoreName());
}

bool Profile::IsCustomSongGroup(std::string sSongGroup) const {
  std::string prefix = GetCustomSongsGroupNamePrefix();

  return sSongGroup.compare(0, prefix.length(), prefix) == 0;
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
      const Song* pSong = Luna<Song>::check(L, 1);
      const Steps* pSteps = Luna<Steps>::check(L, 2);
      HighScoreList& hsl = p->GetStepsHighScoreList(pSong, pSteps);
      hsl.PushSelf(L);
      return 1;
    } else if (LuaBinding::CheckLuaObjectType(L, 1, "Course")) {
      const Course* pCourse = Luna<Course>::check(L, 1);
      const Trail* pTrail = Luna<Trail>::check(L, 2);
      HighScoreList& hsl = p->GetCourseHighScoreList(pCourse, pTrail);
      hsl.PushSelf(L);
      return 1;
    }

    luaL_typerror(L, 1, "Song or Course");
    COMMON_RETURN_SELF;
  }
  static int GetCategoryHighScoreList(T* p, lua_State* L) {
    StepsType pStepsType = Enum::Check<StepsType>(L, 1);
    RankingCategory pRankCat = Enum::Check<RankingCategory>(L, 2);
    HighScoreList& hsl = p->GetCategoryHighScoreList(pStepsType, pRankCat);
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
    Song* pS = Luna<Song>::check(L, 1);
    lua_pushnumber(L, p->GetSongNumTimesPlayed(pS));
    return 1;
  }
  static int HasPassedAnyStepsInSong(T* p, lua_State* L) {
    ASSERT(!lua_isnil(L, 1));
    Song* pS = Luna<Song>::check(L, 1);
    lua_pushboolean(L, p->HasPassedAnyStepsInSong(pS));
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
    Song* pS = p->last_song_.ToSong();
    if (pS) {
      pS->PushSelf(L);
    } else {
      lua_pushnil(L);
    }
    return 1;
  }
  static int GetLastPlayedCourse(T* p, lua_State* L) {
    Course* pC = p->last_course_.ToCourse();
    if (pC) {
      pC->PushSelf(L);
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
