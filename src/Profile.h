#ifndef Profile_H
#define Profile_H

#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "CourseUtil.h"  // for CourseID
#include "DateTime.h"
#include "Difficulty.h"
#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "Grade.h"
#include "Group.h"
#include "HighScore.h"
#include "LuaReference.h"
#include "PlayerNumber.h"
#include "RageUtil.h"
#include "SongUtil.h"   // for SongID
#include "StepsUtil.h"  // for StepsID
#include "StyleUtil.h"  // for StyleID
#include "TrailUtil.h"  // for TrailID

class XNode;
struct lua_State;
class Character;

// Current file versions
extern const std::string STATS_XML;

/**
 * @brief The filename where one can edit their personal profile data.
 *
 * Editable data is an INI because the default INI file association on Windows
 * systems will open the ini file in an editor.  The default association for
 * XML will open in IE.  Users have a much better chance of discovering how to
 * edit this data if they don't have to fight against the file associations. */
extern const std::string EDITABLE_INI;

/**
 * @brief The filename containing the signature for STATS_XML's signature.
 *
 *
 * The "don't share" file is something that the user should always keep private.
 * They can safely share STATS_XML with STATS_XML's signature so that others
 * can authenticate the STATS_XML data.  However, others can't copy that data
 * to their own profile for use in the game unless they also have the "don't
 * share" file.  DontShare contains a piece of information that we can
 * construct using STATS_XML but the user can't construct using STATS_XML. */
extern const std::string DONT_SHARE_SIG;

extern const std::string PUBLIC_KEY_FILE;
extern const std::string SCREENSHOTS_SUBDIR;
extern const std::string EDIT_STEPS_SUBDIR;
extern const std::string EDIT_COURSES_SUBDIR;
// extern const std::string RIVAL_SUBDIR;

/** @brief The max number of characters that can be used in a profile. */
const unsigned int PROFILE_MAX_DISPLAY_NAME_LENGTH = 32;

class Style;

class Song;
class Steps;
class Course;
struct Game;

// Profile types exist for distinguishing profiles and facilitating sorting.
// Guest profiles at the top, test at the bottom.
enum ProfileType {
  ProfileType_Guest,
  ProfileType_Normal,
  ProfileType_Test,
  NUM_ProfileType,
  ProfileType_Invalid
};

/**
 * @brief Player data that persists between sessions.
 *
 * This can be stored on a local disk or on a memory card. */
class Profile {
 public:
  /**
   * @brief Set up the Profile with default values.
   *
   * Note: there are probably a lot of variables. */
  // When adding new score related data, add logic for handling it to
  // MergeScoresFromOtherProfile. -Kyz
  // When adding any new fields, add them to SwapExceptPriority.  Anything not
  // added to SwapExceptPriority won't be swapped correctly when the user
  // changes the list priority of a profile. -Kyz
  Profile()
      : type_(ProfileType_Normal),
        list_priority_(0),
        display_name_(""),
        character_id_(""),
        last_used_high_score_name_(""),
        weight_pounds_(0),
        voomax_(0),
        birth_year_(0),
        ignore_step_count_calories_(false),
        is_male_(true),
        guid_(MakeGuid()),
        default_modifiers_(),
        sort_order_(SortOrder_Invalid),
        songs_{},
        groups_{},
        last_difficulty_(Difficulty_Invalid),
        last_course_difficulty_(Difficulty_Invalid),
        last_steps_type_(StepsType_Invalid),
        last_song_(),
        last_course_(),
        current_combo_(0),
        total_sessions_(0),
        total_session_seconds_(0),
        total_gameplay_seconds_(0),
        total_calories_burned_(0),
        goal_type_(GoalType_Calories),
        goal_calories_(0),
        goal_seconds_(0),
        total_dance_points_(0),
        num_extra_stages_passed_(0),
        num_extra_stages_failed_(0),
        num_toasties_(0),
        total_taps_and_holds_(0),
        total_jumps_(0),
        total_holds_(0),
        total_rolls_(0),
        total_mines_(0),
        total_hands_(0),
        total_lifts_(0),
        new_profile_(false),
        unlocked_entry_ids_(),
        last_played_machine_guid_(""),
        creation_time_(),
        last_played_date_(),
        num_songs_played_by_style_(),
        num_total_songs_played_(0),
        user_table_(),
        song_high_scores_(),
        course_high_scores_(),
        screenshots_(),
        map_day_to_calories_burned_() {
    last_song_.Unset();
    last_course_.Unset();

    creation_time_.Init();
    last_played_date_.Init();

    FOREACH_ENUM(PlayMode, i)
    num_songs_played_by_play_mode_[i] = 0;
    FOREACH_ENUM(Difficulty, i)
    num_songs_played_by_difficulty_[i] = 0;
    for (int i = 0; i < MAX_METER + 1; i++) {
      num_songs_played_by_meter_[i] = 0;
    }

    ZERO(num_stages_passed_by_play_mode_);
    ZERO(num_stages_passed_by_grade_);
    user_table_.Unset();

    FOREACH_ENUM(StepsType, st)
    FOREACH_ENUM(RankingCategory, rc)
    category_high_scores_[st][rc].Init();
  }

  ~Profile();
  void ClearSongs();

  // smart accessors
  std::string GetDisplayNameOrHighScoreName() const;
  Character* GetCharacter() const;
  void SetCharacter(const std::string sCharacterID);
  std::string GetDisplayTotalCaloriesBurned()
      const;  // remove me and use Lua instead
  int GetCalculatedWeightPounds()
      const;           // returns a default value if m_iWeightPounds isn't set
  int GetAge() const;  // returns a default value if m_Age isn't set
  float GetCaloriesBurnedToday() const;
  int GetTotalNumSongsPassed() const;
  int GetTotalStepsWithTopGrade(StepsType st, Difficulty d, Grade g) const;
  int GetTotalTrailsWithTopGrade(
      StepsType st, CourseDifficulty d, Grade g) const;
  float GetSongsPossible(StepsType st, Difficulty dc) const;
  float GetCoursesPossible(StepsType st, CourseDifficulty cd) const;
  float GetSongsActual(StepsType st, Difficulty dc) const;
  float GetCoursesActual(StepsType st, CourseDifficulty cd) const;
  float GetSongsPercentComplete(StepsType st, Difficulty dc) const;
  float GetCoursesPercentComplete(StepsType st, CourseDifficulty cd) const;
  float GetSongsAndCoursesPercentCompleteAllDifficulties(StepsType st) const;
  bool GetDefaultModifiers(
      const Game* pGameType, std::string& sModifiersOut) const;
  void SetDefaultModifiers(
      const Game* pGameType, const std::string& sModifiers);
  bool IsCodeUnlocked(std::string sUnlockEntryID) const;
  Song* GetMostPopularSong() const;
  Course* GetMostPopularCourse() const;

  void AddStepTotals(
      int iNumTapsAndHolds, int iNumJumps, int iNumHolds, int iNumRolls,
      int iNumMines, int iNumHands, int iNumLifts, float fCaloriesBurned);
  void AddCaloriesToDailyTotal(float cals);
  float CalculateCaloriesFromHeartRate(float HeartRate, float Duration);

  bool IsMachine() const;

  ProfileType type_;
  // Profiles of the same type and priority are sorted by dir name.
  int list_priority_;

  // Editable data
  std::string display_name_;
  std::string character_id_;
  /**
   * @brief The last used name for high scoring purposes.
   *
   * This really shouldn't be in "editable", but it's needed in the smaller
   * editable file so that it can be ready quickly. */
  std::string last_used_high_score_name_;
  int weight_pounds_;  // 0 == not set
  // Voomax and BirthYear are used for calculating calories from heart rate.
  float voomax_;    // 0 == not set
  int birth_year_;  // 0 == not set
  // m_IgnoreStepCountCalories is so that the step count based calorie
  // counter can be ignored in favor of calculating calories from heart rate
  // and voomax.
  bool ignore_step_count_calories_;
  bool is_male_;  // Used solely for calculating calories from heart rate.
  // std::string m_sProfileImageName;	// todo: add a default image -aj

  // General data
  static std::string MakeGuid();

  std::string guid_;
  std::map<std::string, std::string> default_modifiers_;
  SortOrder sort_order_;
  std::vector<Song*> songs_;
  std::vector<Group*> groups_;
  std::unordered_map<std::string, std::vector<Song*>> songs_groups_;
  Difficulty last_difficulty_;
  CourseDifficulty last_course_difficulty_;
  StepsType last_steps_type_;
  SongID last_song_;
  CourseID last_course_;
  int current_combo_;
  int total_sessions_;
  int total_session_seconds_;
  int total_gameplay_seconds_;
  float total_calories_burned_;
  GoalType goal_type_;
  int goal_calories_;
  int goal_seconds_;
  int total_dance_points_;
  int num_extra_stages_passed_;
  int num_extra_stages_failed_;
  int num_toasties_;
  int total_taps_and_holds_;
  int total_jumps_;
  int total_holds_;
  int total_rolls_;
  int total_mines_;
  int total_hands_;
  int total_lifts_;
  /** @brief Is this a brand new profile? */
  bool new_profile_;
  std::set<std::string> unlocked_entry_ids_;
  /**
   * @brief Which machine did we play on last, based on the Guid?
   *
   * This is mutable because it's overwritten on save, but is usually
   * const everywhere else. It was decided to keep const on the whole
   * save chain and keep this mutable. -Chris */
  mutable std::string last_played_machine_guid_;
  mutable DateTime creation_time_;
  mutable DateTime last_played_date_;
  /* These stats count twice in the machine profile if two players are playing;
   * that's the only approach that makes sense for ByDifficulty and ByMeter. */
  int num_songs_played_by_play_mode_[NUM_PlayMode];
  std::map<StyleID, int> num_songs_played_by_style_;
  int num_songs_played_by_difficulty_[NUM_Difficulty];
  int num_songs_played_by_meter_[MAX_METER + 1];
  /**
   * @brief Count the total number of songs played.
   *
   * This stat counts once per song, even if two players are active. */
  int num_total_songs_played_;
  int num_stages_passed_by_play_mode_[NUM_PlayMode];
  int num_stages_passed_by_grade_[NUM_Grade];

  /* store arbitrary data for the theme within a profile */
  LuaTable user_table_;

  // Song high scores
  struct HighScoresForASteps {
    HighScoreList hsl_;
    HighScoresForASteps() : hsl_() {}
  };
  struct HighScoresForASong {
    std::map<StepsID, HighScoresForASteps> steps_high_scores_;
    int GetNumTimesPlayed() const;
    HighScoresForASong() : steps_high_scores_() {}
  };
  std::map<SongID, HighScoresForASong> song_high_scores_;

  void AddStepsHighScore(
      const Song* pSong, const Steps* pSteps, HighScore hs, int& iIndexOut);
  const HighScoreList& GetStepsHighScoreList(
      const Song* pSong, const Steps* pSteps) const;
  HighScoreList& GetStepsHighScoreList(const Song* pSong, const Steps* pSteps);
  void IncrementStepsPlayCount(const Song* pSong, const Steps* pSteps);
  void GetGrades(const Song* pSong, StepsType st, int iCounts[NUM_Grade]) const;
  int GetSongNumTimesPlayed(const Song* pSong) const;
  int GetSongNumTimesPlayed(const SongID& songID) const;
  DateTime GetSongLastPlayedDateTime(const Song* pSong) const;
  bool HasPassedSteps(const Song* pSong, const Steps* pSteps) const;
  bool HasPassedAnyStepsInSong(const Song* pSong) const;

  // Course high scores
  // struct was a typedef'd array of HighScores, but VC6 freaks out
  // in processing the templates for map::operator[].
  struct HighScoresForATrail {
    HighScoreList hsl_;
    HighScoresForATrail() : hsl_() {}
  };
  struct HighScoresForACourse {
    std::map<TrailID, HighScoresForATrail> trail_high_scores_;
    int GetNumTimesPlayed() const;
    HighScoresForACourse() : trail_high_scores_() {}
  };
  std::map<CourseID, HighScoresForACourse> course_high_scores_;

  void AddCourseHighScore(
      const Course* pCourse, const Trail* pTrail, HighScore hs, int& iIndexOut);
  HighScoreList& GetCourseHighScoreList(
      const Course* pCourse, const Trail* pTrail);
  const HighScoreList& GetCourseHighScoreList(
      const Course* pCourse, const Trail* pTrail) const;
  int GetCourseNumTimesPlayed(const Course* pCourse) const;
  int GetCourseNumTimesPlayed(const CourseID& courseID) const;
  DateTime GetCourseLastPlayedDateTime(const Course* pCourse) const;
  void IncrementCoursePlayCount(const Course* pCourse, const Trail* pTrail);

  void GetAllUsedHighScoreNames(std::set<std::string>& names);

  void MergeScoresFromOtherProfile(
      Profile* other, bool skip_totals, const std::string& from_dir,
      const std::string& to_dir);

  // Category high scores
  HighScoreList category_high_scores_[NUM_StepsType][NUM_RankingCategory];

  void AddCategoryHighScore(
      StepsType st, RankingCategory rc, HighScore hs, int& iIndexOut);
  HighScoreList& GetCategoryHighScoreList(StepsType st, RankingCategory rc);
  const HighScoreList& GetCategoryHighScoreList(
      StepsType st, RankingCategory rc) const;
  int GetCategoryNumTimesPlayed(StepsType st) const;
  void IncrementCategoryPlayCount(StepsType st, RankingCategory rc);

  std::vector<Screenshot> screenshots_;
  void AddScreenshot(const Screenshot& screenshot);

  /**
   * @brief The basics for Calorie Data.
   *
   * Why track calories in a map, and not in a static sized array like
   * Bookkeeping?  The machine's clock is not guaranteed to be set correctly.
   * If calorie array is in a static sized array, playing on a machine with
   * a mis-set clock could wipe out all your past data.  With this scheme,
   * the worst that could happen is that playing on a mis-set machine will
   * insert some garbage entries into the map. */
  struct Calories {
    Calories() : cals_(0) {}
    float cals_;
  };
  std::map<DateTime, Calories> map_day_to_calories_burned_;
  float GetCaloriesBurnedForDay(DateTime day) const;

  /*
          // RecentSongScores
          struct HighScoreForASongAndSteps
          {
                  StepsID stepsID;
                  SongID songID;
                  HighScore hs;

                  HighScoreForASongAndSteps() { Unset(); }
                  void Unset() { stepsID.Unset(); songID.Unset(); hs.Unset(); }

                  XNode* CreateNode() const;
          };

          void SaveStepsRecentScore( const Song* pSong, const Steps* pSteps,
     HighScore hs );

          // RecentCourseScores
          struct HighScoreForACourseAndTrail
          {
                  CourseID courseID;
                  TrailID	trailID;
                  HighScore hs;

                  HighScoreForACourseAndTrail() { Unset(); }
                  void Unset() { courseID.Unset(); hs.Unset(); }

                  XNode* CreateNode() const;
          };

          void SaveCourseRecentScore( const Course* pCourse, const Trail*
     pTrail, HighScore hs );
  */
  // Init'ing
  void InitAll() {
    InitEditableData();
    InitGeneralData();
    InitSongScores();
    InitCourseScores();
    InitCategoryScores();
    InitScreenshotData();
    InitCalorieData();
    ClearSongs();
  }
  void InitEditableData();
  void InitGeneralData();
  void InitSongScores();
  void InitCourseScores();
  void InitCategoryScores();
  void InitScreenshotData();
  void InitCalorieData();
  void ClearStats();

  void swap(Profile& other);

  // Loading and saving
  bool HandleStatsPrefixChange(std::string dir, bool require_signature);
  ProfileLoadResult LoadAllFromDir(std::string sDir, bool bRequireSignature);
  ProfileLoadResult LoadStatsFromDir(std::string dir, bool require_signature);
  void LoadSongsFromDir(
      const std::string& dir, ProfileSlot prof_slot, bool isMemoryCard = true);
  void LoadTypeFromDir(std::string dir);
  void LoadCustomFunction(std::string sDir, PlayerNumber pn);
  bool SaveAllToDir(std::string sDir, bool bSignData) const;

  ProfileLoadResult LoadEditableDataFromDir(std::string sDir);
  ProfileLoadResult LoadStatsXmlFromNode(
      const XNode* pNode, bool bIgnoreEditable = true);
  void LoadGeneralDataFromNode(const XNode* pNode);
  void LoadSongScoresFromNode(const XNode* pNode);
  void LoadCourseScoresFromNode(const XNode* pNode);
  void LoadCategoryScoresFromNode(const XNode* pNode);
  void LoadScreenshotDataFromNode(const XNode* pNode);
  void LoadCalorieDataFromNode(const XNode* pNode);

  void SaveTypeToDir(std::string dir) const;
  void SaveEditableDataToDir(std::string sDir) const;
  bool SaveStatsXmlToDir(std::string sDir, bool bSignData) const;
  XNode* SaveStatsXmlCreateNode() const;
  XNode* SaveGeneralDataCreateNode() const;
  XNode* SaveSongScoresCreateNode() const;
  XNode* SaveCourseScoresCreateNode() const;
  XNode* SaveCategoryScoresCreateNode() const;
  XNode* SaveScreenshotDataCreateNode() const;
  XNode* SaveCalorieDataCreateNode() const;

  XNode* SaveCoinDataCreateNode() const;

  void SaveStatsWebPageToDir(std::string sDir) const;

  static void MoveBackupToDir(std::string sFromDir, std::string sToDir);
  static std::string MakeUniqueFileNameNoExtension(
      std::string sDir, std::string sFileNameBeginning);
  static std::string MakeFileNameNoExtension(
      std::string sFileNameBeginning, int iIndex);

  std::string GetCustomSongsGroupNamePrefix() const;
  bool IsCustomSongGroup(std::string sSongGroup) const;
  // Lua
  void PushSelf(lua_State* L);

 private:
  const HighScoresForASong* GetHighScoresForASong(const SongID& songID) const;
  const HighScoresForACourse* GetHighScoresForACourse(
      const CourseID& courseID) const;
};

#endif

/**
 * @file
 * @author Chris Danford (c) 2001-2004
 * @section LICENSE
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
