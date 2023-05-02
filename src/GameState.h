#ifndef GAMESTATE_H
#define GAMESTATE_H

#include <cstddef>
#include <deque>
#include <map>
#include <set>
#include <vector>

#include "Attack.h"
#include "Character.h"
#include "Course.h"
#include "Difficulty.h"
#include "Game.h"
#include "GameConstantsAndTypes.h"
#include "Grade.h"
#include "LuaReference.h"
#include "MessageManager.h"
#include "ModsGroup.h"
#include "PlayerOptions.h"
#include "PlayerState.h"
#include "Preference.h"
#include "Profile.h"
#include "RageTimer.h"
#include "Song.h"
#include "SongOptions.h"
#include "SongPosition.h"
#include "StageStats.h"
#include "Steps.h"
#include "Style.h"
#include "TimingData.h"
#include "Trail.h"

struct lua_State;

SortOrder GetDefaultSort();

// Holds game data that is not saved between sessions.
class GameState {
 public:
  // Set up the GameState with initial values.
  GameState();
  ~GameState();
  // Reset the GameState back to initial values.
  void Reset();
  void ResetPlayer(PlayerNumber pn);
  void ResetPlayerOptions(PlayerNumber pn);
  void ApplyCmdline();  // called by Reset
  void ApplyGameCommand(
      const RString& command, PlayerNumber pn = PLAYER_INVALID);
  // Start the game when the first player joins in.
  void BeginGame();
  void JoinPlayer(PlayerNumber pn);
  void UnjoinPlayer(PlayerNumber pn);
  bool JoinInput(PlayerNumber pn);
  bool JoinPlayers();
  void LoadProfiles(bool load_edits = true);
  void SavePlayerProfiles();
  void SavePlayerProfile(PlayerNumber pn);
  bool HaveProfileToLoad();
  bool HaveProfileToSave();
  void SaveLocalData();
  void AddStageToPlayer(PlayerNumber pn);
  void LoadCurrentSettingsFromProfile(PlayerNumber pn);
  // Save the specified player's settings to his/her profile.
  // This is called at the beginning of each stage.
  void SaveCurrentSettingsToProfile(PlayerNumber pn);
  Song* GetDefaultSong() const;

  bool CanSafelyEnterGameplay(RString& reason);
  void SetCompatibleStylesForPlayers();
  void ForceSharedSidesMatch();
  void ForceOtherPlayersToCompatibleSteps(PlayerNumber main);

  void Update(float delta);

  // Main state info

  // State what the current game is.
  // Call this instead of m_pCurGame.Set to make sure that
  // PREFSMAN->m_sCurrentGame stays in sync.
  void SetCurGame(const Game* game);
  BroadcastOnChangePtr<const Game> cur_game_;

  // Determine which side is joined.
  // The left side is player 1, and the right side is player 2.
  bool side_is_joined_[NUM_PLAYERS];  // left side, right side
  MultiPlayerStatus multiplayer_status_[NUM_MultiPlayer];
	// Many screens display different info depending on this value.
  BroadcastOnChange<PlayMode> play_mode_;
  // The number of coins presently in the machine.
  //
  // Note that coins are not "credits". One may have to put in two coins
  // to get one credit, only to have to put in another four coins to get
  // the three credits needed to begin the game.
  BroadcastOnChange<int> coins_;
  bool multiplayer_;
  int num_multiplayer_note_fields_;
  bool DifficultiesLocked() const;
  bool ChangePreferredDifficultyAndStepsType(
      PlayerNumber pn, Difficulty difficulty, StepsType steps_type);
  bool ChangePreferredDifficulty(PlayerNumber pn, int dir);
  bool ChangePreferredCourseDifficultyAndStepsType(
      PlayerNumber pn, CourseDifficulty course_difficulty,
			StepsType steps_type);
  bool ChangePreferredCourseDifficulty(PlayerNumber pn, int dir);
  bool IsCourseDifficultyShown(CourseDifficulty course_difficulty);
  Difficulty GetClosestShownDifficulty(PlayerNumber pn) const;
  Difficulty GetEasiestStepsDifficulty() const;
  Difficulty GetHardestStepsDifficulty() const;
	// From the moment the first player pressed Start.
  RageTimer time_game_started_;
  LuaTable* environment_;

  // This is set to a random number per-game/round; it can be used for a random
  // seed.
  int game_seed_, stage_seed_;
  RString stage_guid_;

  void SetNewStageSeed();

  // Determine if a second player can join in at this time.
  bool PlayersCanJoin() const;
  int GetCoinsNeededToJoin() const;
  bool EnoughCreditsToJoin() const {
    return coins_ >= GetCoinsNeededToJoin();
  }
  int GetNumSidesJoined() const;

  const Game* GetCurrentGame() const;
  const Style* GetCurrentStyle(PlayerNumber pn) const;
  void SetCurrentStyle(const Style* style, PlayerNumber pn);
  bool SetCompatibleStyle(StepsType steps_type, PlayerNumber pn);

  void GetPlayerInfo(PlayerNumber pn, bool& is_enabled_out, bool& is_human_out);
  bool IsPlayerEnabled(PlayerNumber pn) const;
  bool IsMultiPlayerEnabled(MultiPlayer mp) const;
  bool IsPlayerEnabled(const PlayerState* player_state) const;
  int GetNumPlayersEnabled() const;

  // Is the specified Player a human Player
  bool IsHumanPlayer(PlayerNumber pn) const;
  int GetNumHumanPlayers() const;
  PlayerNumber GetFirstHumanPlayer() const;
  PlayerNumber GetFirstDisabledPlayer() const;
  bool IsCpuPlayer(PlayerNumber pn) const;
  bool AnyPlayersAreCpu() const;

  // Retrieve the present master player number.
  PlayerNumber GetMasterPlayerNumber() const;

  // Set the master player number.
  void SetMasterPlayerNumber(const PlayerNumber pn);

  // Retrieve the present timing data being processed.
  TimingData* GetProcessedTimingData() const;

  // Set the timing data to be used with processing.
  void SetProcessedTimingData(TimingData* timing_data);

  bool IsCourseMode() const;
  bool IsBattleMode() const;  // not Rave

  // Do we show the W1 timing judgment?
  bool ShowW1() const;

	// GROUP_ALL denotes no preferred group.
  BroadcastOnChange<RString> preferred_song_group_;
	// GROUP_ALL denotes no preferred group.
  BroadcastOnChange<RString> preferred_course_group_;
	// true if FailType was changed in the song options screen.
  bool fail_type_was_explicitly_set_;
  BroadcastOnChange<StepsType> preferred_steps_type_;
  BroadcastOnChange1D<Difficulty, NUM_PLAYERS> preferred_difficulty_;
	// Used in nonstop mode.
  BroadcastOnChange1D<CourseDifficulty, NUM_PLAYERS>
      preferred_course_difficulty_;
	// Set by MusicWheel. 
  BroadcastOnChange<SortOrder> sort_order_;
	// Used by MusicWheel.
  SortOrder preferred_sort_order_;
  EditMode edit_mode_;
  bool IsEditing() const { return edit_mode_ != EditMode_Invalid; }
  // Are we in the demonstration or jukebox mode?
  // ScreenGameplay often does special things when this is set to true.
  bool demonstration_or_jukebox_;
  bool jukebox_uses_modifiers_;
  int num_stages_of_this_song_;
  // Increase this every stage while not resetting on a continue.
  // This is cosmetic: it's not use for Stage or Screen branching logic.
  int current_stage_index_;
  // The number of stages available for the players.
  // This resets whenever a player joins or continues.
  int player_stage_tokens_[NUM_PLAYERS];
  // This is necessary so that IsFinalStageForEveryHumanPlayer knows to
  // adjust for the current song cost.
	bool adjust_tokens_by_song_cost_for_final_stage_check;

  RString expanded_section_name_;

  static int GetNumStagesMultiplierForSong(const Song* song);
  static int GetNumStagesForSongAndStyleType(
			const Song* song, StyleType style_type);
  int GetNumStagesForCurrentSongAndStepsOrCourse() const;

  void BeginStage();
  void CancelStage();
  void CommitStageStats();
  void FinishStage();
  int GetNumStagesLeft(PlayerNumber pn) const;
  int GetSmallestNumStagesLeftForAnyHumanPlayer() const;
  bool IsFinalStageForAnyHumanPlayer() const;
  bool IsFinalStageForEveryHumanPlayer() const;
  bool IsAnExtraStage() const;
  bool IsAnExtraStageAndSelectionLocked() const;
  bool IsExtraStage() const;
  bool IsExtraStage2() const;
  Stage GetCurrentStage() const;
  int GetCourseSongIndex() const;
  RString GetPlayerDisplayName(PlayerNumber pn) const;

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

  // Music statistics:
  SongPosition position_;

  BroadcastOnChange<bool> gameplay_lead_in_;

  // if re-adding noteskin changes in courses, add functions and such here -aj
  void GetAllUsedNoteSkins(std::vector<RString>& out) const;

  static const float MUSIC_SECONDS_INVALID;

	// Call this when it's time to play a new song. Clears the values above.
  void ResetMusicStatistics();
  void SetPaused(bool is_paused) { is_paused_ = is_paused; }
  bool GetPaused() { return is_paused_; }
  void UpdateSongPosition(
      float fPositionSeconds, const TimingData& timing,
      const RageTimer& timestamp = RageZeroTimer);
  float GetSongPercent(float beat) const;

  bool AllAreInDangerOrWorse() const;
  bool OneIsHot() const;

  // Haste
  float haste_rate_;  // [-1,+1]; 0 = normal speed
  float last_haste_update_music_seconds_;
  float accumulated_haste_seconds_;

  // Used by themes that support heart rate entry.
  RageTimer dance_start_time_;
  float dance_duration_;

  // Random Attacks & Attack Mines
  std::vector<RString> random_attacks_;

  // Used in PLAY_MODE_BATTLE
  float opponent_health_percent_;

  // Used in PLAY_MODE_RAVE
  float tug_life_percent_p1_;

  // Used in workout
  bool goal_complete_[NUM_PLAYERS];
  bool workout_goal_complete_;

  // Primarily called at the end of a song to stop all attacks.
  void RemoveAllActiveAttacks();
  PlayerNumber GetBestPlayer() const;
  StageResult GetStageResult(PlayerNumber pn) const;

  // Call this function when it's time to play a new stage.
  void ResetStageStatistics();

  // Options stuff
  ModsGroup<SongOptions> song_options_;

  // Did the current game mode change the default Noteskin?
  //
  // This is true if it has: see Edit/Sync Songs for a common example.
  // NOTE: any mode that wants to use this must set it explicitly.
  bool did_mode_change_noteskin_;

  void GetDefaultPlayerOptions(PlayerOptions& player_options);
  void GetDefaultSongOptions(SongOptions& song_options);
  void ResetToDefaultSongOptions(ModsLevel mods_level);
  void ApplyPreferredModifiers(PlayerNumber pn, RString modifiers);
  void ApplyStageModifiers(PlayerNumber pn, RString modifiers);
  void ClearStageModifiersIllegalForCourse();
  void ResetOptions();

  bool CurrentOptionsDisqualifyPlayer(PlayerNumber pn);
  bool PlayerIsUsingModifier(PlayerNumber pn, const RString& modifier);

  FailType GetPlayerFailType(const PlayerState* player_state) const;

  Character* cur_characters_[NUM_PLAYERS];

  bool HasEarnedExtraStage() const { return earned_extra_stage_; }
  EarnedExtraStage GetEarnedExtraStage() const {
    return CalculateEarnedExtraStage();
  }

  struct RankingFeat {
    enum { SONG, COURSE, CATEGORY } type;
    Song* song;      // valid if type == SONG
    Steps* steps;    // valid if type == SONG
    Course* course;  // valid if type == COURSE
    Grade grade;
    int score;
    float percent_dp;
    RString banner;
    RString feat;
    RString* string_to_fill;
  };

  void GetRankingFeats(
      PlayerNumber pn, std::vector<RankingFeat>& feats_out) const;
  bool AnyPlayerHasRankingFeats() const;
	// Called by name entry screens
  void StoreRankingName(PlayerNumber pn, RString name);
	// Filled on StoreRankingName,
  std::vector<RString*> names_that_were_filled_;
	// Filled on StoreRankingName,
  std::set<PlayerNumber> players_that_were_filled_;

  // Lowest priority in front, highest priority at the back.
  std::deque<StageAward> last_stage_awards_[NUM_PLAYERS];
  std::deque<PeakComboAward> last_peak_combo_awards_[NUM_PLAYERS];

  // Attract stuff
	// Negative means play regardless of m_iAttractSoundFrequency setting
  int num_times_through_attract_;
  bool IsTimeToPlayAttractSounds() const;
  void VisitAttractScreen(const RString screen_name);

  // PlayerState
  // Allow access to each player's PlayerState.
  PlayerState* player_state_[NUM_PLAYERS];
  PlayerState* multiplayer_state_[NUM_MultiPlayer];

  // Preferences
  static Preference<bool> auto_join_;

  // These options have weird interactions depending on m_bEventMode,
  // so wrap them.
  bool temporary_event_mode_;
  bool IsEventMode() const;
  CoinMode GetCoinMode() const;
  Premium GetPremium() const;

  // Edit stuff

  // Is the game right now using Song timing or Steps timing?
  // Different options are available depending on this setting.
  bool is_using_step_timing_;
  // Are we presently in the Step Editor, where some rules apply differently?
  // TODO: Find a better way to implement this.
  bool in_step_editor_;
  BroadcastOnChange<StepsType> steps_type_edit_;
  BroadcastOnChange<CourseDifficulty> course_difficulty_edit_;
  BroadcastOnChangePtr<Steps> edit_source_steps_;
  BroadcastOnChange<StepsType> edit_source_steps_type_;
  BroadcastOnChange<int> edit_course_entry_index_;
  BroadcastOnChange<RString> edit_local_profile_id_;
  Profile* GetEditLocalProfile();

  // Workout stuff
  float GetGoalPercentComplete(PlayerNumber pn);
  bool IsGoalComplete(PlayerNumber pn) {
    return GetGoalPercentComplete(pn) >= 1;
  }

  bool dopefish_;

  // Autogen stuff.
	// TODO(kyz): This should probably be moved to its own singleton or
  // something when autogen is generalized and more customizable.
  float GetAutoGenFarg(std::size_t i) {
    if (i >= autogen_args_.size()) {
      return 0.0f;
    }
    return autogen_args_[i];
  }
  std::vector<float> autogen_args_;

  // Lua
  void PushSelf(lua_State* L);

 private:
  // The player number used with Styles where one player controls both sides.
  PlayerNumber master_player_number_;
  // The TimingData that is used for processing certain functions.
  TimingData* processed_timing_;

 	// DO NOT access directly.  Use Get/SetCurrentStyle.
  BroadcastOnChangePtr<const Style> cur_style_;
  // Only used if the Game specifies that styles are separate.
  const Style* separated_styles_[NUM_PlayerNumber];

  // Keep extra stage logic internal to GameState.
  EarnedExtraStage CalculateEarnedExtraStage() const;
  int awarded_extra_stages_[NUM_PLAYERS];
  bool earned_extra_stage_;

  // Timing position corrections
  RageTimer last_position_timer_;
  float last_position_seconds_;
  bool is_paused_;

  GameState(const GameState& rhs);
  GameState& operator=(const GameState& rhs);
};

PlayerNumber GetNextHumanPlayer(PlayerNumber pn);
PlayerNumber GetNextEnabledPlayer(PlayerNumber pn);
PlayerNumber GetNextCpuPlayer(PlayerNumber pn);
PlayerNumber GetNextPotentialCpuPlayer(PlayerNumber pn);
MultiPlayer GetNextEnabledMultiPlayer(MultiPlayer mp);

// A foreach loop to act on each human Player.
#define FOREACH_HumanPlayer(pn)                                \
  for (PlayerNumber pn = GetNextHumanPlayer((PlayerNumber)-1); \
       pn != PLAYER_INVALID; pn = GetNextHumanPlayer(pn))

// A foreach loop to act on each enabled Player.
#define FOREACH_EnabledPlayer(pn)                                \
  for (PlayerNumber pn = GetNextEnabledPlayer((PlayerNumber)-1); \
       pn != PLAYER_INVALID; pn = GetNextEnabledPlayer(pn))

// A foreach loop to act on each CPU Player.
#define FOREACH_CpuPlayer(pn)                                \
  for (PlayerNumber pn = GetNextCpuPlayer((PlayerNumber)-1); \
       pn != PLAYER_INVALID; pn = GetNextCpuPlayer(pn))

// A foreach loop to act on each potential CPU Player.
#define FOREACH_PotentialCpuPlayer(pn)                                \
  for (PlayerNumber pn = GetNextPotentialCpuPlayer((PlayerNumber)-1); \
       pn != PLAYER_INVALID; pn = GetNextPotentialCpuPlayer(pn))

// A foreach loop to act on each Player in MultiPlayer.
#define FOREACH_EnabledMultiPlayer(mp)                              \
  for (MultiPlayer mp = GetNextEnabledMultiPlayer((MultiPlayer)-1); \
       mp != MultiPlayer_Invalid; mp = GetNextEnabledMultiPlayer(mp))

// Global and accessible from anywhere in our program.
extern GameState* GAMESTATE;

#endif  // GAMESTATE_H

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
