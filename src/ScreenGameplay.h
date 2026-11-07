#ifndef SCREEN_GAMEPLAY_H
#define SCREEN_GAMEPLAY_H

#include <string>
#include <vector>

#include "Actor.h"
#include "Attack.h"
#include "AutoActor.h"
#include "AutoKeysounds.h"
#include "BeginnerHelper.h"
#include "BitmapText.h"
#include "GameInput.h"
#include "GameplayAssist.h"
#include "InputEventPlus.h"
#include "LocalizedString.h"
#include "LyricDisplay.h"
#include "MessageManager.h"
#include "PlayerNumber.h"
#include "PlayerStageStats.h"
#include "PlayerState.h"
#include "RageSound.h"
#include "RageTimer.h"
#include "RageUtil.h"
#include "Screen.h"
#include "ScreenMessage.h"
#include "ScreenWithMenuElements.h"
#include "SoundEffectControl.h"
#include "ThemeMetric.h"
#include "Transition.h"

class LyricsLoader;
class ActiveAttackList;
class CombinedLifeMeter;
class Player;
class LifeMeter;
class ScoreDisplay;
class StepsDisplay;
class ScoreKeeper;
class Background;
class Foreground;

AutoScreenMessage(SM_NotesEnded);
AutoScreenMessage(SM_BeginFailed);
AutoScreenMessage(SM_LeaveGameplay);

class PlayerInfo {
 public:
  PlayerInfo();
  ~PlayerInfo();

  void Load(PlayerNumber pn, bool show_note_field);

  /** @brief The player has lost all of their lives: show the special game over.
   */
  void ShowOniGameOver();
  /**
   * @brief Retrieve the player's state and stage stats index.
   * @return the player's state and stage stats index.
   */
  PlayerNumber GetPlayerStateAndStageStatsIndex() {
    return pn_ == PLAYER_INVALID ? PLAYER_1 : pn_;
  }
  PlayerState* GetPlayerState();
  PlayerStageStats* GetPlayerStageStats();
  PlayerNumber GetStepsAndTrailIndex() {
    return pn_ == PLAYER_INVALID ? PLAYER_1 : pn_;
  }
  /**
   * @brief Determine if the player information is enabled.
   * @return its success or failure. */
  bool IsEnabled();
  /**
   * @brief Retrieve the name of the Player based on the mode.
   * @return the name of the Player. */
  std::string GetName() const {
    if (is_dummy_) {
      return ssprintf("Dummy%d", dummy_index_);
    }
    return PlayerNumberToString(pn_);
  }

  // Lua
  void PushSelf(lua_State* L);

  /** @brief The present Player's number. */
  PlayerNumber pn_;
  bool is_dummy_;
  int dummy_index_;
  bool player_enabled_;  // IsEnabled cache for iterators
  PlayerState player_state_dummy_;
  PlayerStageStats player_stage_stats_dummy_;
  SoundEffectControl sound_effect_control_;

  /**
   * @brief The list of Steps a player has to go through in this set.
   *
   * The size may be greater than 1 if playing a course. */
  std::vector<Steps*> steps_queue_;
  /**
   * @brief The list of attack modifiers a player has to go through in this set.
   *
   * The size may be greater than 1 if playing a course. */
  std::vector<AttackArray> modifiers_queue_;

  /** @brief The LifeMeter showing a Player's health. */
  LifeMeter* life_meter_;
  /** @brief The current Song number in a Course. */
  BitmapText* text_course_song_number_;
  /** @brief The description of the current Steps. */
  BitmapText* text_steps_description_;

  /** @brief The display for the primary ScoreKeeper. */
  ScoreDisplay* primary_score_display_;
  /** @brief The display for the secondary ScoreKeeper. */
  ScoreDisplay* secondary_score_display_;
  /** @brief The primary ScoreKeeper for keeping track of the score. */
  ScoreKeeper* primary_score_keeper_;
  /** @brief The secondary ScoreKeeper. Only used in PLAY_MODE_RAVE. */
  ScoreKeeper* secondary_score_keeper_;
  /** @brief The current PlayerOptions that are activated. */
  BitmapText* text_player_options_;
  /** @brief The current attack modifiers that are in play for the moment. */
  ActiveAttackList* active_attack_list_;

  /** @brief The NoteData the Player has to get through. */
  NoteData note_data_;
  /** @brief The specific Player that is going to play. */
  Player* player_;

  StepsDisplay* steps_display_;

  AutoActor spr_oni_game_over_;
};

/** @brief The music plays, the notes scroll, and the Player is pressing
 * buttons. */
class ScreenGameplay : public ScreenWithMenuElements {
 public:
  ScreenGameplay();
  virtual void Init();
  virtual ~ScreenGameplay();
  virtual void BeginScreen();

  virtual void Update(float delta_time);
  virtual bool Input(const InputEventPlus& input);
  virtual void HandleScreenMessage(const ScreenMessage sm);
  virtual void HandleMessage(const Message& msg);
  virtual void Cancel(ScreenMessage send_when_done);

  virtual void DrawPrimitives();

  /**
   * @brief Retrieve the current ScreenType.
   * @return the gameplay ScreenType. */
  virtual ScreenType GetScreenType() const { return gameplay; }

  /**
   * @brief Determine if we are to center the columns for just one player.
   * @return true if we center the solo player, false otherwise. */
  bool Center1Player() const;

  bool MenuRestart(const InputEventPlus& input);

  // Lua
  virtual void PushSelf(lua_State* L);
  Song* GetNextCourseSong() const;
  LifeMeter* GetLifeMeter(PlayerNumber pn);
  CombinedLifeMeter* GetCombinedLifeMeter() const {
    return combined_life_meter_;
  }
  PlayerInfo* GetPlayerInfo(PlayerNumber pn);
  PlayerInfo* GetDummyPlayerInfo(int dummy_index);
  void Pause(bool pause) { PauseGame(pause); }
  bool IsPaused() const { return paused_; }
  float GetHasteRate();

  void FailFadeRemovePlayer(PlayerInfo* pi);
  void FailFadeRemovePlayer(PlayerNumber pn);
  void BeginBackingOutFromGameplay();

  std::vector<float> haste_turning_points_;  // Values at which the meaning of
                                             // GAMESTATE->m_fHasteRate changes.
  std::vector<float>
      haste_add_amounts_;  // Amounts that are added to speed depending on what
                           // turning point has been passed.
  float haste_time_between_updates_;  // Seconds between haste updates.
  float haste_life_switch_point_;     // Life amount below which
                                      // GAMESTATE->m_fHasteRate is based on the
                                      // life amount.

 protected:
  virtual bool UseSongBackgroundAndForeground() const { return true; }

  ThemeMetric<std::string> PLAYER_TYPE;
  ThemeMetric<apActorCommands> PLAYER_INIT_COMMAND;
  LocalizedString GIVE_UP_START_TEXT;
  LocalizedString GIVE_UP_BACK_TEXT;
  LocalizedString GIVE_UP_ABORTED_TEXT;
  LocalizedString SKIP_SONG_TEXT;
  ThemeMetric<float> GIVE_UP_SECONDS;
  ThemeMetric<float> MUSIC_FADE_OUT_SECONDS;
  ThemeMetric<float> OUT_TRANSITION_LENGTH;
  ThemeMetric<float> COURSE_TRANSITION_LENGTH;
  ThemeMetric<float> BEGIN_FAILED_DELAY;
  ThemeMetric<float> MIN_SECONDS_TO_STEP;
  ThemeMetric<float> MIN_SECONDS_TO_MUSIC;
  ThemeMetric<float> MIN_SECONDS_TO_STEP_NEXT_SONG;
  ThemeMetric<bool> START_GIVES_UP;
  ThemeMetric<bool> BACK_GIVES_UP;
  ThemeMetric<bool> SELECT_SKIPS_SONG;
  ThemeMetric<bool> GIVING_UP_GOES_TO_PREV_SCREEN;
  /** @brief The miss combo a player needs to fail out of a song. */
  ThemeMetric<int> FAIL_ON_MISS_COMBO;
  ThemeMetric<bool> ALLOW_CENTER_1_PLAYER;
  ThemeMetric<bool> UNPAUSE_WITH_START;
  ThemeMetric<std::string> SONG_NUMBER_FORMAT;
  ThemeMetric<bool> SURVIVAL_MOD_OVERRIDE;

  bool IsLastSong();
  void SetupSong(int song_index);
  void ReloadCurrentSong();
  virtual void LoadNextSong();
  void StartPlayingSong(float min_time_to_notes, float min_time_to_music);
  void GetMusicEndTiming(
      float& seconds_to_start_fading_out_music,
      float& seconds_to_start_transitioning_out);
  void LoadLights();
  void PauseGame(bool pause, GameController gc = GameController_Invalid);
  void PlayAnnouncer(
      const std::string& type, float seconds, float* delta_seconds);
  void PlayAnnouncer(const std::string& type, float seconds) {
    PlayAnnouncer(type, seconds, &time_since_last_dancing_comment_);
  }
  void UpdateLights();
  void SendCrossedMessages();

  void PlayTicks();
  void UpdateSongPosition(float delta_time);
  void SongFinished();
  virtual void SaveStats();
  virtual void StageFinished(bool backed_out);
  void SaveReplay();
  bool OneFailed();

  virtual void InitSongQueues();

  void UpdateHasteRate();
  float curr_haste_rate_;
  // These exist so that the haste rate isn't recalculated every time
  // GetHasteRate is called, which is at least once per frame. -Kyz

  /** @brief The different game states of ScreenGameplay. */
  enum DancingState {
    STATE_INTRO =
        0,         /**< The starting state, pressing Back isn't allowed here. */
    STATE_DANCING, /**< The main state where notes have to be pressed. */
    STATE_OUTRO,   /**< The ending state, pressing Back isn't allowed here. */
    NUM_DANCING_STATES
  }
  /** @brief The specific point within ScreenGameplay. */ dancing_state_;

 private:
  bool paused_;
  // set_paused_internal exists because GameState's pause variable needs to
  // be kept in sync with ScreenGameplay's.
  void set_paused_internal(bool p);

 protected:
  GameController pause_controller_;
  /**
   * @brief The songs left to play.
   *
   * The size can be greater than 1 if playing a course. */
  std::vector<Song*> songs_queue_;

  float time_since_last_dancing_comment_;  // this counter is only running while
                                           // STATE_DANCING

  LyricDisplay lyric_display_;

  Background* song_background_;
  Foreground* song_foreground_;

  /** @brief Used between songs in a course to show the next song. */
  Transition next_song_;

  CombinedLifeMeter* combined_life_meter_;

  BitmapText text_song_options_;

  BitmapText text_debug_;

  RageTimer give_up_timer_;
  bool gave_up_;
  RageTimer skip_song_timer_;
  bool skipped_song_;
  void AbortGiveUpText(bool show_abort_text);
  void AbortSkipSong(bool show_text);
  void AbortGiveUp(bool show_text);
  void ResetGiveUpTimers(bool show_text);

  Transition ready_;
  Transition go_;
  /** @brief The transition to use when all players have failed. */
  Transition failed_;
  /** @brief The transition to use when one player earns an easter egg. */
  Transition toasty_;

  /**
   * @brief How much time has the player survived in the extra stage?
   *
   * TODO: Move this into a BGA. */
  BitmapText text_survive_time_;

  AutoKeysounds auto_keysounds_;

  bool zero_delta_on_next_update_;

  GameplayAssist gameplay_assist_;
  RageSound* sound_music_;

  BeginnerHelper beginner_helper_;

  /** @brief The NoteData that controls the lights on an arcade cabinet. */
  NoteData cabinet_lights_note_data_;

  std::vector<PlayerInfo>
      player_info_;  // filled by SGameplay derivatives in FillPlayerInfo
  /** @brief Shared routine scorekeeper instance for shared-sides play. */
  ScoreKeeper* routine_shared_score_keeper_;
  virtual void FillPlayerInfo(std::vector<PlayerInfo>& player_info_out) = 0;
  virtual PlayerInfo& GetPlayerInfoForInput(const InputEventPlus& iep) {
    return player_info_[iep.pn_];
  }

  RageTimer timer_gameplay_seconds_;

  // m_delaying_ready_announce is for handling a case where the ready
  // announcer sound needs to be delayed.  See HandleScreenMessage for more.
  // -Kyz
  bool delaying_ready_announce_;
};

std::vector<PlayerInfo>::iterator GetNextEnabledPlayerInfo(
    std::vector<PlayerInfo>::iterator iter, std::vector<PlayerInfo>& v);
std::vector<PlayerInfo>::iterator GetNextEnabledPlayerInfoNotDummy(
    std::vector<PlayerInfo>::iterator iter, std::vector<PlayerInfo>& v);
std::vector<PlayerInfo>::iterator GetNextEnabledPlayerNumberInfo(
    std::vector<PlayerInfo>::iterator iter, std::vector<PlayerInfo>& v);
std::vector<PlayerInfo>::iterator GetNextPlayerNumberInfo(
    std::vector<PlayerInfo>::iterator iter, std::vector<PlayerInfo>& v);
std::vector<PlayerInfo>::iterator GetNextVisiblePlayerInfo(
    std::vector<PlayerInfo>::iterator iter, std::vector<PlayerInfo>& v);

/** @brief Get each enabled Player's info. */
#define FOREACH_EnabledPlayerInfo(v, pi)           \
  for (std::vector<PlayerInfo>::iterator pi =      \
           GetNextEnabledPlayerInfo(v.begin(), v); \
       pi != v.end(); pi = GetNextEnabledPlayerInfo(++pi, v))
/** @brief Get each enabled Player's info as long as it's not a dummy player. */
#define FOREACH_EnabledPlayerInfoNotDummy(v, pi)           \
  for (std::vector<PlayerInfo>::iterator pi =              \
           GetNextEnabledPlayerInfoNotDummy(v.begin(), v); \
       pi != v.end(); pi = GetNextEnabledPlayerInfoNotDummy(++pi, v))
/** @brief Get each enabled Player Number's info. */
#define FOREACH_EnabledPlayerNumberInfo(v, pi)           \
  for (std::vector<PlayerInfo>::iterator pi =            \
           GetNextEnabledPlayerNumberInfo(v.begin(), v); \
       pi != v.end(); pi = GetNextEnabledPlayerNumberInfo(++pi, v))
/** @brief Get each Player Number's info, regardless of whether it's enabled or
 * not. */
#define FOREACH_PlayerNumberInfo(v, pi)           \
  for (std::vector<PlayerInfo>::iterator pi =     \
           GetNextPlayerNumberInfo(v.begin(), v); \
       pi != v.end(); pi = GetNextPlayerNumberInfo(++pi, v))
/** @brief Get each visible Player's info. */
#define FOREACH_VisiblePlayerInfo(v, pi)           \
  for (std::vector<PlayerInfo>::iterator pi =      \
           GetNextVisiblePlayerInfo(v.begin(), v); \
       pi != v.end(); pi = GetNextVisiblePlayerInfo(++pi, v))

#endif

/**
 * @file
 * @author Chris Danford, Glenn Maynard (c) 2001-2004
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
