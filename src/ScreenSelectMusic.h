/* ScreenSelectMusic - Choose a Song and Steps. */

#ifndef SCREEN_SELECT_MUSIC_H
#define SCREEN_SELECT_MUSIC_H

#include <string>
#include <vector>

#include "AutoActor.h"
#include "BitmapText.h"
#include "Difficulty.h"
#include "FadingBanner.h"
#include "GameConstantsAndTypes.h"
#include "GameInput.h"
#include "InputEventPlus.h"
#include "MessageManager.h"
#include "MusicWheel.h"
#include "OptionsList.h"
#include "PlayerNumber.h"
#include "RageSound.h"
#include "RageTexturePreloader.h"
#include "RageTimer.h"
#include "RageUtil_BackgroundLoader.h"
#include "ScreenMessage.h"
#include "ScreenWithMenuElements.h"
#include "Sprite.h"
#include "Steps.h"
#include "ThemeMetric.h"
#include "TimingData.h"
#include "Trail.h"
#include "global.h"

enum SelectionState {
  SelectionState_SelectingSong,
  SelectionState_SelectingSteps,
  SelectionState_Finalized,
  NUM_SelectionState,
};
const std::string& SelectionStateToString(SelectionState ss);

class ScreenSelectMusic : public ScreenWithMenuElements {
 public:
  virtual ~ScreenSelectMusic();
  virtual void Init();
  virtual void BeginScreen();

  virtual void Update(float delta_time);
  virtual bool Input(const InputEventPlus& input);
  virtual void HandleMessage(const Message& msg);
  virtual void HandleScreenMessage(const ScreenMessage sm);
  virtual bool AllowLateJoin() const { return true; }

  virtual bool MenuStart(const InputEventPlus& input);
  virtual bool MenuBack(const InputEventPlus& input);
  virtual void Cancel(ScreenMessage send_when_done);
  virtual void ConfirmedCancel(ScreenMessage send_when_done);

  // ScreenWithMenuElements override: never play music here; we do it ourself.
  virtual void StartPlayingMusic() {}

  bool GetGoToOptions() const { return go_to_options_; }
  MusicWheel* GetMusicWheel() { return &music_wheel_; }

  void OpenOptionsList(PlayerNumber pn);
  void OnConfirmSongDeletion();

  bool can_open_options_list(PlayerNumber pn);

  // Lua
  virtual void PushSelf(lua_State* L);

 protected:
  void UpdateSelectButton(PlayerNumber pn, bool being_pressed);

  void ChangeSteps(PlayerNumber pn, int dir);
  void AfterStepsOrTrailChange(const std::vector<PlayerNumber>& vpns);
  void SwitchPlayerStepDifficulty(PlayerNumber pn, Difficulty d);
  void SwitchPlayerCourseDifficulty(PlayerNumber pn, Difficulty d);
  void SwitchToDifficulty(Difficulty d);
  void SwitchToPreferredDifficulty();
  void AfterMusicChange();

  void CheckBackgroundRequests(bool force);
  bool DetectCodes(const InputEventPlus& input);
  bool LoadPlayerProfile(PlayerNumber pn);
  bool LoadReadyMemoryCards();
  bool ShouldWaitForMemoryCard() const;

  std::vector<Steps*> steps_;
  std::vector<Trail*> trails_;
  int selection_[NUM_PLAYERS];

  RageTimer timer_idle_comment_;
  ThemeMetric<float> IDLE_COMMENT_SECONDS;

  ThemeMetric<float> SAMPLE_MUSIC_DELAY_INIT;
  ThemeMetric<float> SAMPLE_MUSIC_DELAY;
  ThemeMetric<bool> SAMPLE_MUSIC_LOOPS;
  ThemeMetric<bool> SAMPLE_MUSIC_STARTS_IMMEDIATELY;
  ThemeMetric<SampleMusicPreviewMode> SAMPLE_MUSIC_PREVIEW_MODE;
  ThemeMetric<float> SAMPLE_MUSIC_FALLBACK_FADE_IN_SECONDS;
  ThemeMetric<float> SAMPLE_MUSIC_FADE_OUT_SECONDS;
  ThemeMetric<bool> DO_ROULETTE_ON_MENU_TIMER;
  ThemeMetric<float> ROULETTE_TIMER_SECONDS;
  ThemeMetric<bool> ALIGN_MUSIC_BEATS;
  ThemeMetric<std::string> CODES;
  ThemeMetric<std::string> MUSIC_WHEEL_TYPE;
  ThemeMetric<bool> OPTIONS_MENU_AVAILABLE;
  ThemeMetric<bool> SELECT_MENU_AVAILABLE;
  ThemeMetric<bool> MODE_MENU_AVAILABLE;
  ThemeMetric<bool> USE_OPTIONS_LIST;
  ThemeMetric<float> OPTIONS_LIST_TIMEOUT;
  ThemeMetric<bool> USE_PLAYER_SELECT_MENU;
  ThemeMetric<std::string> SELECT_MENU_NAME;
  ThemeMetric<bool> SELECT_MENU_CHANGES_DIFFICULTY;
  ThemeMetric<bool> TWO_PART_SELECTION;
  ThemeMetric<bool> TWO_PART_CONFIRMS_ONLY;
  ThemeMetric<float> TWO_PART_TIMER_SECONDS;
  ThemeMetric<bool> WRAP_CHANGE_STEPS;
  ThemeMetric<bool> CHANGE_STEPS_WITH_GAME_BUTTONS;
  ThemeMetric<bool> CHANGE_GROUPS_WITH_GAME_BUTTONS;
  ThemeMetric<std::string> NULL_SCORE_STRING;
  ThemeMetric<bool> PLAY_SOUND_ON_ENTERING_OPTIONS_MENU;
  ThemeMetric<bool> CONFIRM_EXIT;

  bool CanChangeSong() const {
    return selection_state_ == SelectionState_SelectingSong;
  }
  SelectionState GetNextSelectionState() const {
    switch (selection_state_) {
      case SelectionState_SelectingSong:
        return TWO_PART_SELECTION ? SelectionState_SelectingSteps
                                  : SelectionState_Finalized;
      case SelectionState_SelectingSteps:
        return SelectionState_Finalized;
        DEFAULT_FAIL(selection_state_);
    }
  }

  GameButton game_button_previous_song_;
  GameButton game_button_next_song_;
  GameButton game_button_previous_difficulty_;
  GameButton game_button_next_difficulty_;
  GameButton game_button_previous_group_;
  GameButton game_button_next_group_;
  GameButton game_button_cancel_two_part1_;
  GameButton game_button_cancel_two_part2_;

  std::string section_music_path_;
  std::string sort_music_path_;
  std::string roulette_music_path_;
  std::string random_music_path_;
  std::string course_music_path_;
  std::string loop_music_path_;
  std::string fallback_cd_title_path_;

  FadingBanner banner_;
  Sprite spr_cd_title_front_, spr_cd_title_back_;
  AutoActor spr_high_score_frame_[NUM_PLAYERS];
  BitmapText text_high_score_[NUM_PLAYERS];
  MusicWheel music_wheel_;
  OptionsList options_list_[NUM_PLAYERS];

  SelectionState selection_state_;
  bool steps_chosen_[NUM_PLAYERS];  // only used in
                                    // SelectionState_SelectingSteps
  bool go_to_options_;
  std::string sample_music_to_play_;
  TimingData* sample_music_timing_data_;
  float sample_start_seconds_, sample_length_seconds_;
  bool allow_options_menu_, allow_options_menu_repeat_;
  bool select_is_down_[NUM_PLAYERS];
  bool accept_select_release_[NUM_PLAYERS];

  bool preview_disabled_;

  RageSound sound_start_;
  RageSound sound_difficulty_easier_;
  RageSound sound_difficulty_harder_;
  RageSound sound_options_change_;
  RageSound sound_locked_;

  BackgroundLoader background_loader_;
  RageTexturePreloader texture_preload_;

  Song* song_awaiting_deletion_confirmation_;
};

#endif

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
