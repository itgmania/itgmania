#ifndef GAMESTATE_H
#define GAMESTATE_H

#include <cstddef>
#include <deque>
#include <set>
#include <string>
#include <vector>

#include "Attack.h"
#include "Difficulty.h"
#include "GameConstantsAndTypes.h"
#include "Grade.h"
#include "MessageManager.h"
#include "ModsGroup.h"
#include "PlayerNumber.h"
#include "PlayerOptions.h"
#include "Preference.h"
#include "RageTimer.h"
#include "SongOptions.h"
#include "SongPosition.h"

class Character;
class Course;
struct Game;
struct lua_State;
class LuaTable;
class PlayerState;
class PlayerOptions;
class Profile;
class Song;
class Steps;
class StageStats;
class Style;
class TimingData;
class Trail;

SortOrder GetDefaultSort();

/** @brief Holds game data that is not saved between sessions. */
class GameState {
  /** @brief The player number used with Styles where one player controls both
   * sides. */
  PlayerNumber master_player_number_;
  /** @brief The TimingData that is used for processing certain functions. */
  TimingData* processed_timing_;
  /** @brief Whether the player has score data on their profile from playing
   * a song this game. */
  bool player_has_committed_gameplay_[NUM_PLAYERS];

 public:
  /** @brief Set up the GameState with initial values. */
  GameState();
  ~GameState();
  /** @brief Reset the GameState back to initial values. */
  void Reset();
  void ResetPlayer(PlayerNumber pn);
  void ResetPlayerOptions(PlayerNumber pn);
  void ApplyCmdline();  // called by Reset
  void ApplyGameCommand(
      const std::string& command, PlayerNumber pn = PLAYER_INVALID);
  /** @brief Start the game when the first player joins in. */
  void BeginGame();
  void JoinPlayer(PlayerNumber pn);
  void UnjoinPlayer(PlayerNumber pn);
  bool JoinInput(PlayerNumber pn);
  bool JoinPlayers();
  bool CanAcceptProfile(PlayerNumber pn) const;
  bool LoadProfileOverGuest(PlayerNumber pn, bool load_edits = true);
  void LoadProfiles(bool load_edits = true);
  void SavePlayerProfiles();
  void SavePlayerProfile(PlayerNumber pn);
  bool HaveProfileToLoad(PlayerNumber pn) const;
  bool HaveProfileToLoad();
  bool HaveProfileToSave();
  void SaveLocalData();
  void AddStageToPlayer(PlayerNumber pn);
  void LoadCurrentSettingsFromProfile(PlayerNumber pn);
  /**
   * @brief Save the specified player's settings to his/her profile.
   *
   * This is called at the beginning of each stage.
   * @param pn the PlayerNumber to save the stats to. */
  void SaveCurrentSettingsToProfile(PlayerNumber pn);
  Song* GetDefaultSong() const;

  bool CanSafelyEnterGameplay(std::string& reason);
  void SetCompatibleStylesForPlayers();
  void ForceSharedSidesMatch();
  void ForceOtherPlayersToCompatibleSteps(PlayerNumber main);

  void Update(float delta);

  // Main state info

  /**
   * @brief State what the current game is.
   *
   * Call this instead of m_pCurGame.Set to make sure that
   * PREFSMAN->m_sCurrentGame stays in sync.
   * @param pGame the game to start using. */
  void SetCurGame(const Game* game);
  BroadcastOnChangePtr<const Game> cur_game_;

 private:  // DO NOT access directly.  Use Get/SetCurrentStyle.
  BroadcastOnChangePtr<const Style> cur_style_;
  // Only used if the Game specifies that styles are separate.
  const Style* separated_styles_[NUM_PlayerNumber];

 public:
  /** @brief Determine which side is joined.
   *
   * The left side is player 1, and the right side is player 2. */
  bool side_is_joined_[NUM_PLAYERS];       // left side, right side
  BroadcastOnChange<PlayMode> play_mode_;  // many screens display different
                                           // info depending on this value
  /**
   * @brief The number of coins presently in the machine.
   *
   * Note that coins are not "credits". One may have to put in two coins
   * to get one credit, only to have to put in another four coins to get
   * the three credits needed to begin the game. */
  BroadcastOnChange<int> coins_;
  bool DifficultiesLocked() const;
  bool ChangePreferredDifficultyAndStepsType(
      PlayerNumber pn, Difficulty dc, StepsType st);
  bool ChangePreferredDifficulty(PlayerNumber pn, int dir);
  bool ChangePreferredCourseDifficultyAndStepsType(
      PlayerNumber pn, CourseDifficulty cd, StepsType st);
  bool ChangePreferredCourseDifficulty(PlayerNumber pn, int dir);
  bool IsCourseDifficultyShown(CourseDifficulty cd);
  Difficulty GetClosestShownDifficulty(PlayerNumber pn) const;
  Difficulty GetEasiestStepsDifficulty() const;
  Difficulty GetHardestStepsDifficulty() const;
  RageTimer
      time_game_started_;  // from the moment the first player pressed Start
  LuaTable* environment_;

  // This is set to a random number per-game/round; it can be used for a random
  // seed.
  int game_seed_, stage_seed_;
  std::string stage_guid_;

  void SetNewStageSeed();

  /**
   * @brief Determine if a second player can join in at this time.
   * @return true if a player can still enter the game, false otherwise. */
  bool PlayersCanJoin() const;
  int GetCoinsNeededToJoin() const;
  bool EnoughCreditsToJoin() const { return coins_ >= GetCoinsNeededToJoin(); }
  int GetNumSidesJoined() const;

  const Game* GetCurrentGame() const;
  const Style* GetCurrentStyle(PlayerNumber pn) const;
  void SetCurrentStyle(const Style* style, PlayerNumber pn);
  bool SetCompatibleStyle(StepsType stype, PlayerNumber pn);

  void GetPlayerInfo(PlayerNumber pn, bool& is_enabled_out, bool& is_human_out);
  bool IsPlayerEnabled(PlayerNumber pn) const;
  bool IsPlayerEnabled(const PlayerState* player_state) const;
  int GetNumPlayersEnabled() const;

  /**
   * @brief Is the specified Player a human Player?
   * @param pn the numbered Player to check.
   * @return true if it's a human Player, or false otherwise. */
  bool IsHumanPlayer(PlayerNumber pn) const;
  int GetNumHumanPlayers() const;
  PlayerNumber GetFirstHumanPlayer() const;
  bool IsCpuPlayer(PlayerNumber pn) const;
  bool AnyPlayersAreCpu() const;

  /**
   * @brief Retrieve the present master player number.
   * @return The master player number. */
  PlayerNumber GetMasterPlayerNumber() const;

  /**
   * @brief Set the master player number.
   * @param p the master player number. */
  void SetMasterPlayerNumber(const PlayerNumber p);

  /**
   * @brief Retrieve the present timing data being processed.
   * @return the timing data pointer. */
  TimingData* GetProcessedTimingData() const;

  /**
   * @brief Set the timing data to be used with processing.
   * @param t the timing data. */
  void SetProcessedTimingData(TimingData* t);

  bool IsCourseMode() const;
  bool IsBattleMode() const;  // not Rave

  /**
   * @brief Do we show the W1 timing judgment?
   * @return true if we do, or false otherwise. */
  bool ShowW1() const;

  BroadcastOnChange<std::string>
      preferred_song_group_;  // GROUP_ALL denotes no preferred group
  BroadcastOnChange<std::string>
      preferred_course_group_;         // GROUP_ALL denotes no preferred group
  bool fail_type_was_explicitly_set_;  // true if FailType was changed in the
                                       // song options screen
  BroadcastOnChange<StepsType> preferred_steps_type_;
  BroadcastOnChange1D<Difficulty, NUM_PLAYERS> preferred_difficulty_;
  BroadcastOnChange1D<CourseDifficulty, NUM_PLAYERS>
      preferred_course_difficulty_;          // used in nonstop
  BroadcastOnChange<SortOrder> sort_order_;  // set by MusicWheel
  SortOrder preferred_sort_order_;           // used by MusicWheel
  EditMode edit_mode_;
  bool IsEditing() const { return edit_mode_ != EditMode_Invalid; }
  /**
   * @brief Are we in the demonstration or jukebox mode?
   *
   * ScreenGameplay often does special things when this is set to true. */
  bool demonstration_or_jukebox_;
  bool jukebox_uses_modifiers_;
  int num_stages_of_this_song_;
  /**
   * @brief Increase this every stage while not resetting on a continue.
   *
   * This is cosmetic: it's not use for Stage or Screen branching logic. */
  int current_stage_index_;
  /**
   * @brief The number of stages available for the players.
   *
   * This resets whenever a player joins or continues. */
  int player_stage_tokens_[NUM_PLAYERS];
  // This is necessary so that IsFinalStageForEveryHumanPlayer knows to
  // adjust for the current song cost.
  bool adjust_tokens_by_song_cost_for_final_stage_check_;

  std::string expanded_section_name_;
  std::string expanded_parent_section_name_;

  std::string last_open_section_;

  static int GetNumStagesMultiplierForSong(const Song* song);
  int GetNumStagesForCurrentSongAndStepsOrCourse() const;

  void BeginStage();
  void CancelStage();
  void CommitStageStats();
  void FinishStage();
  int GetNumStagesLeft(PlayerNumber pn) const;
  int GetSmallestNumStagesLeftForAnyHumanPlayer() const;
  bool IsFinalStageForEveryHumanPlayer() const;
  bool IsAnExtraStage() const;
  bool IsAnExtraStageAndSelectionLocked() const;
  bool IsExtraStage() const;
  bool IsExtraStage2() const;
  Stage GetCurrentStage() const;
  int GetCourseSongIndex() const;
  std::string GetPlayerDisplayName(PlayerNumber pn) const;

  void ActivatePremiumFree();
  bool IsPremiumFreeActive() const { return premium_free_active_; }
  void StartPremiumFreeTimer();
  float GetPremiumFreeSecondsLeft() const;
  bool IsPremiumFreeExpired() const;
  bool IsSongAllowedByPremiumFree(const Song* song) const;

  bool loading_next_song_;
  int GetLoadingCourseSongIndex() const;

  int prepare_song_for_gameplay();

  // State Info used during gameplay

  // nullptr on ScreenSelectMusic if the currently selected wheel item isn't a
  // Song.
  BroadcastOnChangePtr<Song> cur_song_;
  // The last Song that the user manually changed to.
  Song* preferred_song_;
  BroadcastOnChangePtr1D<Steps, NUM_PLAYERS> cur_steps_;

  // nullptr on ScreenSelectMusic if the currently selected wheel item isn't a
  // Course.
  BroadcastOnChangePtr<Course> cur_course_;
  // The last Course that the user manually changed to.
  Course* preferred_course_;
  BroadcastOnChangePtr1D<Trail, NUM_PLAYERS> cur_trail_;

  bool backed_out_of_final_stage_;

  bool premium_free_active_;
  int premium_free_minutes_;
  RageTimer premium_free_start_time_;

  // Music statistics:
  SongPosition position_;

  BroadcastOnChange<bool> gameplay_lead_in_;

  // if re-adding noteskin changes in courses, add functions and such here -aj
  void GetAllUsedNoteSkins(std::vector<std::string>& out) const;

  static const float kMusicSecondsInvalid;

  void ResetMusicStatistics();  // Call this when it's time to play a new song.
                                // Clears the values above.
  void SetPaused(bool p) { paused_ = p; }
  void UpdateSongPosition(
      float position_seconds, const TimingData& timing,
      const RageTimer& timestamp = RageZeroTimer);
  float GetSongPercent(float beat) const;

  bool AllAreInDangerOrWorse() const;
  bool OneIsHot() const;

  // Haste
  float haste_rate_;  // [-1,+1]; 0 = normal speed
  float last_haste_update_music_seconds_;
  float accumulated_haste_seconds_;

  // used by themes that support heart rate entry.
  RageTimer dance_start_time_;
  float dance_duration_;

  // Random Attacks & Attack Mines
  std::vector<std::string> random_attacks_;

  // used in PLAY_MODE_BATTLE
  float opponent_health_percent_;

  // used in PLAY_MODE_RAVE
  float tug_life_percent_p1_;

  // used in workout
  bool goal_complete_[NUM_PLAYERS];
  bool workout_goal_complete_;

  /** @brief Primarily called at the end of a song to stop all attacks. */
  void RemoveAllActiveAttacks();
  StageResult GetStageResult(PlayerNumber pn) const;

  /** @brief Call this function when it's time to play a new stage. */
  void ResetStageStatistics();

  // Options stuff
  ModsGroup<SongOptions> song_options_;

  /**
   * @brief Did the current game mode change the default Noteskin?
   *
   * This is true if it has: see Edit/Sync Songs for a common example.
   * Note: any mode that wants to use this must set it explicitly. */
  bool did_mode_change_note_skin_;

  void GetDefaultPlayerOptions(PlayerOptions& po);
  void GetDefaultSongOptions(SongOptions& so);
  void ResetToDefaultSongOptions(ModsLevel l);
  void ApplyPreferredModifiers(PlayerNumber pn, std::string modifiers);
  void ApplyStageModifiers(PlayerNumber pn, std::string modifiers);
  void ClearStageModifiersIllegalForCourse();

  bool CurrentOptionsDisqualifyPlayer(PlayerNumber pn);
  bool PlayerIsUsingModifier(PlayerNumber pn, const std::string& modifier);

  FailType GetPlayerFailType(const PlayerState* player_state) const;
  FailType GetPlayerFailType(PlayerNumber pn) const {
    return GetPlayerFailType(player_state_[pn]);
  }

  // character stuff
  Character* cur_characters_[NUM_PLAYERS];

  bool HasEarnedExtraStage() const { return earned_extra_stage_; }
  EarnedExtraStage GetEarnedExtraStage() const {
    return CalculateEarnedExtraStage();
  }

  // Ranking Stuff
  struct RankingFeat {
    enum { SONG, COURSE, CATEGORY } type_;
    Song* song_;      // valid if Type == SONG
    Steps* steps_;    // valid if Type == SONG
    Course* course_;  // valid if Type == COURSE
    Grade grade_;
    int score_;
    float percent_dp_;
    std::string banner_;
    std::string feat_;
    std::string* string_to_fill_;
  };

  void GetRankingFeats(
      PlayerNumber pn, std::vector<RankingFeat>& feats_out) const;
  bool AnyPlayerHasRankingFeats() const;
  void StoreRankingName(
      PlayerNumber pn, std::string name);  // Called by name entry screens
  std::vector<std::string*>
      names_that_were_filled_;  // filled on StoreRankingName,
  std::set<PlayerNumber>
      players_that_were_filled_;  // filled on StoreRankingName,

  // Award stuff
  // lowest priority in front, highest priority at the back.
  std::deque<StageAward> last_stage_awards_[NUM_PLAYERS];
  std::deque<PeakComboAward> last_peak_combo_awards_[NUM_PLAYERS];

  // Attract stuff
  int num_times_through_attract_;  // negative means play regardless of
                                   // m_iAttractSoundFrequency setting
  bool IsTimeToPlayAttractSounds() const;
  void VisitAttractScreen(const std::string screen_name);

  // PlayerState
  /** @brief Allow access to each player's PlayerState. */
  PlayerState* player_state_[NUM_PLAYERS];

  // Preferences
  static Preference<bool> auto_join_;

  // These options have weird interactions depending on m_bEventMode,
  // so wrap them.
  bool temporary_event_mode_;
  bool IsEventMode() const;
  CoinMode GetCoinMode() const;
  Premium GetPremium() const;

  // Edit stuff

  /**
   * @brief Is the game right now using Song timing or Steps timing?
   *
   * Different options are available depending on this setting. */
  bool is_using_step_timing_;
  /**
   * @brief Are we presently in the Step Editor, where some rules apply
   * differently?
   *
   * TODO: Find a better way to implement this. */
  bool in_step_editor_;
  BroadcastOnChange<StepsType> st_edit_;
  BroadcastOnChange<CourseDifficulty> cd_edit_;
  BroadcastOnChangePtr<Steps> edit_source_steps_;
  BroadcastOnChange<StepsType> edit_source_;
  BroadcastOnChange<int> edit_course_entry_index_;
  BroadcastOnChange<std::string> edit_local_profile_id_;
  Profile* GetEditLocalProfile();

  // Workout stuff
  float GetGoalPercentComplete(PlayerNumber pn);
  bool IsGoalComplete(PlayerNumber pn) {
    return GetGoalPercentComplete(pn) >= 1;
  }

  bool dopefish_;

  // Autogen stuff.  This should probably be moved to its own singleton or
  // something when autogen is generalized and more customizable. -Kyz
  float GetAutoGenFarg(size_t i) {
    if (i >= autogen_fargs_.size()) {
      return 0.0f;
    }
    return autogen_fargs_[i];
  }
  std::vector<float> autogen_fargs_;

  // Lua
  void PushSelf(lua_State* L);

  // Keep extra stage logic internal to GameState.
 private:
  float GetPremiumFreeSeconds() const;
  float GetPremiumFreeSongGraceSeconds() const;
  EarnedExtraStage CalculateEarnedExtraStage() const;
  int awarded_extra_stages_[NUM_PLAYERS];
  bool earned_extra_stage_;

  // Timing position corrections
  RageTimer last_position_timer_;
  float last_position_seconds_;
  bool paused_;

  GameState(const GameState& rhs);
  GameState& operator=(const GameState& rhs);
};

PlayerNumber GetNextHumanPlayer(PlayerNumber pn);
PlayerNumber GetNextEnabledPlayer(PlayerNumber pn);
PlayerNumber GetNextCpuPlayer(PlayerNumber pn);
PlayerNumber GetNextPotentialCpuPlayer(PlayerNumber pn);

/** @brief A foreach loop to act on each human Player. */
#define FOREACH_HumanPlayer(pn)                                  \
  for (PlayerNumber pn = GetNextHumanPlayer((PlayerNumber) - 1); \
       pn != PLAYER_INVALID; pn = GetNextHumanPlayer(pn))
/** @brief A foreach loop to act on each enabled Player. */
#define FOREACH_EnabledPlayer(pn)                                  \
  for (PlayerNumber pn = GetNextEnabledPlayer((PlayerNumber) - 1); \
       pn != PLAYER_INVALID; pn = GetNextEnabledPlayer(pn))
/** @brief A foreach loop to act on each CPU Player. */
#define FOREACH_CpuPlayer(pn)                                  \
  for (PlayerNumber pn = GetNextCpuPlayer((PlayerNumber) - 1); \
       pn != PLAYER_INVALID; pn = GetNextCpuPlayer(pn))
/** @brief A foreach loop to act on each potential CPU Player. */
#define FOREACH_PotentialCpuPlayer(pn)                                  \
  for (PlayerNumber pn = GetNextPotentialCpuPlayer((PlayerNumber) - 1); \
       pn != PLAYER_INVALID; pn = GetNextPotentialCpuPlayer(pn))
extern GameState*
    GAMESTATE;  // global and accessible from anywhere in our program

#endif

/**
 * @file
 * @author Chris Danford, Glenn Maynard, Chris Gomez (c) 2001-2004
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
