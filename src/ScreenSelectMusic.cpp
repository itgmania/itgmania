#include "ScreenSelectMusic.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "ActorUtil.h"
#include "Banner.h"
#include "CodeDetector.h"
#include "CommonMetrics.h"
#include "Course.h"
#include "Difficulty.h"
#include "EnumHelper.h"
#include "Game.h"
#include "GameConstantsAndTypes.h"
#include "GameInput.h"
#include "GameManager.h"
#include "GameSoundManager.h"
#include "GameState.h"
#include "ImageCache.h"
#include "InputEventPlus.h"
#include "InputFilter.h"
#include "InputMapper.h"
#include "LocalizedString.h"
#include "LuaManager.h"
#include "MemoryCardManager.h"
#include "MenuTimer.h"
#include "MessageManager.h"
#include "ModsGroup.h"
#include "OptionsList.h"
#include "PlayerNumber.h"
#include "PlayerOptions.h"
#include "PlayerState.h"
#include "PrefsManager.h"
#include "Profile.h"
#include "ProfileManager.h"
#include "RageFileManager.h"
#include "RageInput.h"
#include "RageInputDevice.h"
#include "RageLog.h"
#include "RageSound.h"
#include "RageTextureManager.h"
#include "RageTimer.h"
#include "RageUtil.h"
#include "Screen.h"
#include "ScreenManager.h"
#include "ScreenMessage.h"
#include "ScreenPrompt.h"
#include "ScreenWithMenuElements.h"
#include "Song.h"
#include "SongManager.h"
#include "SongUtil.h"
#include "Sprite.h"
#include "StatsManager.h"
#include "Steps.h"
#include "StepsUtil.h"
#include "Style.h"
#include "ThemeManager.h"
#include "ThemeMetric.h"
#include "WheelItemBase.h"
#include "global.h"

static const char* SelectionStateNames[] = {
    "SelectingSong", "SelectingSteps", "Finalized"};
XToString(SelectionState);

/** @brief The maximum number of digits for the ScoreDisplay. */
const int NUM_SCORE_DIGITS = 9;
/** @brief Period to block leaving ScreenSelectMusic while late joining. */
const float CARD_LATE_JOIN_BLOCK_SECONDS = 5.0f;

#define SHOW_OPTIONS_MESSAGE_SECONDS \
  THEME->GetMetricF(name_, "ShowOptionsMessageSeconds")

static const ThemeMetric<int> HARD_COMMENT_METER(
    "ScreenSelectMusic", "HardCommentMeter");

AutoScreenMessage(SM_AllowOptionsMenuRepeat);
AutoScreenMessage(SM_SongChanged);
AutoScreenMessage(SM_SortOrderChanging);
AutoScreenMessage(SM_SortOrderChanged);
AutoScreenMessage(SM_BackFromPlayerOptions);
AutoScreenMessage(SM_ConfirmDeleteSong);
AutoScreenMessage(SM_ConfirmExit);

static std::string g_sCDTitlePath;
static bool g_bWantFallbackCdTitle;
static bool g_bCDTitleWaiting = false;
static std::string g_sBannerPath;
static bool g_bBannerWaiting = false;
static bool g_bSampleMusicWaiting = false;
static RageTimer g_StartedLoadingAt(RageZeroTimer);
static RageTimer g_ScreenStartedLoadingAt(RageZeroTimer);
RageTimer g_CanOpenOptionsList(RageZeroTimer);

static LocalizedString PERMANENTLY_DELETE(
    "ScreenSelectMusic", "PermanentlyDelete");
static LocalizedString PROMPT_BEFORE_EXITING(
    "ScreenSelectMusic", "PromptBeforeExiting");

REGISTER_SCREEN_CLASS(ScreenSelectMusic);
void ScreenSelectMusic::Init() {
  g_ScreenStartedLoadingAt.Touch();
  if (PREFSMAN->test_initial_screen_.Get() == name_) {
    GAMESTATE->play_mode_.Set(PLAY_MODE_REGULAR);
    GAMESTATE->SetCurrentStyle(
        GAMEMAN->GameAndStringToStyle(GAMEMAN->GetDefaultGame(), "versus"),
        PLAYER_INVALID);
    GAMESTATE->JoinPlayer(PLAYER_1);
    GAMESTATE->SetMasterPlayerNumber(PLAYER_1);
  }

  IDLE_COMMENT_SECONDS.Load(name_, "IdleCommentSeconds");
  SAMPLE_MUSIC_DELAY_INIT.Load(name_, "SampleMusicDelayInit");
  SAMPLE_MUSIC_DELAY.Load(name_, "SampleMusicDelay");
  SAMPLE_MUSIC_LOOPS.Load(name_, "SampleMusicLoops");
  SAMPLE_MUSIC_STARTS_IMMEDIATELY.Load(name_, "SampleMusicStartsImmediately");
  SAMPLE_MUSIC_PREVIEW_MODE.Load(name_, "SampleMusicPreviewMode");
  SAMPLE_MUSIC_FALLBACK_FADE_IN_SECONDS.Load(
      name_, "SampleMusicFallbackFadeInSeconds");
  SAMPLE_MUSIC_FADE_OUT_SECONDS.Load(name_, "SampleMusicFadeOutSeconds");
  DO_ROULETTE_ON_MENU_TIMER.Load(name_, "DoRouletteOnMenuTimer");
  ROULETTE_TIMER_SECONDS.Load(name_, "RouletteTimerSeconds");
  ALIGN_MUSIC_BEATS.Load(name_, "AlignMusicBeat");
  CODES.Load(name_, "Codes");
  MUSIC_WHEEL_TYPE.Load(name_, "MusicWheelType");
  SELECT_MENU_AVAILABLE.Load(name_, "SelectMenuAvailable");
  MODE_MENU_AVAILABLE.Load(name_, "ModeMenuAvailable");
  USE_OPTIONS_LIST.Load(name_, "UseOptionsList");
  USE_PLAYER_SELECT_MENU.Load(name_, "UsePlayerSelectMenu");
  SELECT_MENU_NAME.Load(name_, "SelectMenuScreenName");
  OPTIONS_LIST_TIMEOUT.Load(name_, "OptionsListTimeout");
  SELECT_MENU_CHANGES_DIFFICULTY.Load(name_, "SelectMenuChangesDifficulty");
  TWO_PART_SELECTION.Load(name_, "TwoPartSelection");
  TWO_PART_CONFIRMS_ONLY.Load(name_, "TwoPartConfirmsOnly");
  TWO_PART_TIMER_SECONDS.Load(name_, "TwoPartTimerSeconds");
  WRAP_CHANGE_STEPS.Load(name_, "WrapChangeSteps");
  NULL_SCORE_STRING.Load(name_, "NullScoreString");
  PLAY_SOUND_ON_ENTERING_OPTIONS_MENU.Load(
      name_, "PlaySoundOnEnteringOptionsMenu");
  // To allow changing steps with gamebuttons -DaisuMaster
  CHANGE_STEPS_WITH_GAME_BUTTONS.Load(name_, "ChangeStepsWithGameButtons");
  CHANGE_GROUPS_WITH_GAME_BUTTONS.Load(name_, "ChangeGroupsWithGameButtons");
  CONFIRM_EXIT.Load(name_, "ConfirmExit");

  preview_disabled_ = !SAMPLE_MUSIC_STARTS_IMMEDIATELY;

  game_button_previous_song_ = INPUTMAPPER->GetInputScheme()->ButtonNameToIndex(
      THEME->GetMetric(name_, "PreviousSongButton"));
  game_button_next_song_ = INPUTMAPPER->GetInputScheme()->ButtonNameToIndex(
      THEME->GetMetric(name_, "NextSongButton"));

  // Ask for those only if changing steps with gamebuttons is allowed
  // -DaisuMaster Use for TwoPartSelection too.
  if (CHANGE_STEPS_WITH_GAME_BUTTONS || TWO_PART_SELECTION) {
    game_button_previous_difficulty_ =
        INPUTMAPPER->GetInputScheme()->ButtonNameToIndex(
            THEME->GetMetric(name_, "PreviousDifficultyButton"));
    game_button_next_difficulty_ =
        INPUTMAPPER->GetInputScheme()->ButtonNameToIndex(
            THEME->GetMetric(name_, "NextDifficultyButton"));
  }
  if (TWO_PART_SELECTION) {
    game_button_cancel_two_part1_ =
        INPUTMAPPER->GetInputScheme()->ButtonNameToIndex(
            THEME->GetMetric(name_, "CancelTwoPartSelectButton1"));
    game_button_cancel_two_part2_ =
        INPUTMAPPER->GetInputScheme()->ButtonNameToIndex(
            THEME->GetMetric(name_, "CancelTwoPartSelectButton2"));
  }
  // same here but for groups -DaisuMaster
  if (CHANGE_GROUPS_WITH_GAME_BUTTONS) {
    game_button_previous_group_ =
        INPUTMAPPER->GetInputScheme()->ButtonNameToIndex(
            THEME->GetMetric(name_, "PreviousGroupButton"));
    game_button_next_group_ = INPUTMAPPER->GetInputScheme()->ButtonNameToIndex(
        THEME->GetMetric(name_, "NextGroupButton"));
  }

  FOREACH_ENUM(PlayerNumber, p) {
    select_is_down_[p] = false;  // used by UpdateSelectButton
    accept_select_release_[p] = false;
  }

  ScreenWithMenuElements::Init();

  this->SubscribeToMessage(Message_PlayerJoined);
  this->SubscribeToMessage(Message_PlayerProfileSet);
  this->SubscribeToMessage(Message_StorageDevicesChanged);

  // Cache these values
  // Marking for change -- Midiman (why? -aj)
  section_music_path_ = THEME->GetPathS(name_, "section music");
  sort_music_path_ = THEME->GetPathS(name_, "sort music");
  roulette_music_path_ = THEME->GetPathS(name_, "roulette music");
  random_music_path_ = THEME->GetPathS(name_, "random music");
  course_music_path_ = THEME->GetPathS(name_, "course music");
  loop_music_path_ = THEME->GetPathS(name_, "loop music");
  fallback_cd_title_path_ = THEME->GetPathG(name_, "fallback cdtitle");

  texture_preload_.Load(fallback_cd_title_path_);

  // load banners
  if (PREFSMAN->image_cache_ != IMGCACHE_OFF) {
    texture_preload_.Load(
        Banner::SongBannerTexture(THEME->GetPathG("Banner", "all music")));
    texture_preload_.Load(
        Banner::SongBannerTexture(
            THEME->GetPathG("Common", "fallback banner")));
    texture_preload_.Load(
        Banner::SongBannerTexture(THEME->GetPathG("Banner", "roulette")));
    texture_preload_.Load(
        Banner::SongBannerTexture(THEME->GetPathG("Banner", "random")));
    texture_preload_.Load(
        Banner::SongBannerTexture(THEME->GetPathG("Banner", "mode")));
  }

  // Load low-res banners and backgrounds if needed.
  IMAGECACHE->Demand("Banner");

  music_wheel_.SetName("MusicWheel");
  music_wheel_.Load(MUSIC_WHEEL_TYPE);
  LOAD_ALL_COMMANDS_AND_SET_XY(music_wheel_);
  this->AddChild(&music_wheel_);

  if (USE_OPTIONS_LIST) {
    FOREACH_PlayerNumber(p) {
      options_list_[p].SetName("OptionsList" + PlayerNumberToString(p));
      options_list_[p].Load("OptionsList", p);
      options_list_[p].SetDrawOrder(100);
      ActorUtil::LoadAllCommands(options_list_[p], name_);
      this->AddChild(&options_list_[p]);
    }
    options_list_[PLAYER_1].Link(&options_list_[PLAYER_2]);
    options_list_[PLAYER_2].Link(&options_list_[PLAYER_1]);
  }

  // this is loaded SetSong and TweenToSong
  banner_.SetName("Banner");
  LOAD_ALL_COMMANDS_AND_SET_XY(banner_);
  this->AddChild(&banner_);

  spr_cd_title_front_.SetName("CDTitle");
  spr_cd_title_front_.Load(THEME->GetPathG(name_, "fallback cdtitle"));
  LOAD_ALL_COMMANDS_AND_SET_XY(spr_cd_title_front_);
  COMMAND(spr_cd_title_front_, "Front");
  this->AddChild(&spr_cd_title_front_);

  spr_cd_title_back_.SetName("CDTitle");
  spr_cd_title_back_.Load(THEME->GetPathG(name_, "fallback cdtitle"));
  LOAD_ALL_COMMANDS_AND_SET_XY(spr_cd_title_back_);
  COMMAND(spr_cd_title_back_, "Back");
  this->AddChild(&spr_cd_title_back_);

  FOREACH_ENUM(PlayerNumber, p) {
    spr_high_score_frame_[p].Load(
        THEME->GetPathG(name_, ssprintf("ScoreFrame P%d", p + 1)));
    spr_high_score_frame_[p]->SetName(ssprintf("ScoreFrameP%d", p + 1));
    LOAD_ALL_COMMANDS_AND_SET_XY(spr_high_score_frame_[p]);
    this->AddChild(spr_high_score_frame_[p]);

    text_high_score_[p].SetName(ssprintf("ScoreP%d", p + 1));
    text_high_score_[p].LoadFromFont(THEME->GetPathF(name_, "score"));
    LOAD_ALL_COMMANDS_AND_SET_XY(text_high_score_[p]);
    this->AddChild(&text_high_score_[p]);
  }

  RageSoundLoadParams sound_params;
  sound_params.support_pan_ = true;

  sound_start_.Load(THEME->GetPathS(name_, "start"));
  sound_difficulty_easier_.Load(
      THEME->GetPathS(name_, "difficulty easier"), false, &sound_params);
  sound_difficulty_harder_.Load(
      THEME->GetPathS(name_, "difficulty harder"), false, &sound_params);
  sound_options_change_.Load(THEME->GetPathS(name_, "options"));
  sound_locked_.Load(THEME->GetPathS(name_, "locked"));

  this->SortByDrawOrder();
}

void ScreenSelectMusic::BeginScreen() {
  g_ScreenStartedLoadingAt.Touch();
  timer_idle_comment_.GetDeltaTime();

  if (CommonMetrics::AUTO_SET_STYLE) {
    GAMESTATE->SetCompatibleStylesForPlayers();
  }

  if (GAMESTATE->GetCurrentStyle(PLAYER_INVALID) == nullptr) {
    LuaHelpers::ReportScriptError(
        "The Style has not been set.  A theme must set the Style before "
        "loading ScreenSelectMusic.");
    // Instead of crashing, set the first compatible style.
    std::vector<StepsType> vst;
    GAMEMAN->GetStepsTypesForGame(GAMESTATE->cur_game_, vst);
    const Style* style = GAMEMAN->GetFirstCompatibleStyle(
        GAMESTATE->cur_game_, GAMESTATE->GetNumSidesJoined(), vst[0]);
    if (style == nullptr) {
      FAIL_M(ssprintf(
          "No compatible styles for %s with %d player%s.",
          GAMESTATE->cur_game_->name_, GAMESTATE->GetNumSidesJoined(),
          GAMESTATE->GetNumSidesJoined() == 1 ? "" : "s"));
    }
    GAMESTATE->SetCurrentStyle(style, PLAYER_INVALID);
  }

  if (GAMESTATE->play_mode_ == PlayMode_Invalid) {
    // Instead of crashing here, let's just set the PlayMode to regular
    GAMESTATE->play_mode_.Set(PLAY_MODE_REGULAR);
    LOG->Trace("PlayMode not set, setting as regular.");
  }
  FOREACH_ENUM(PlayerNumber, pn) {
    if (GAMESTATE->IsHumanPlayer(pn)) {
      continue;
    }
    spr_high_score_frame_[pn]->SetVisible(false);
    text_high_score_[pn].SetVisible(false);
  }

  OPTIONS_MENU_AVAILABLE.Load(name_, "OptionsMenuAvailable");
  PlayCommand("Mods");
  if (GAMESTATE->IsPremiumFreeActive()) {
    GAMESTATE->StartPremiumFreeTimer();
  }
  music_wheel_.BeginScreen();

  selection_state_ = SelectionState_SelectingSong;
  ZERO(steps_chosen_);
  go_to_options_ = false;
  allow_options_menu_ = allow_options_menu_repeat_ = false;
  ZERO(selection_);

  if (USE_OPTIONS_LIST) {
    FOREACH_PlayerNumber(pn) options_list_[pn].Reset();
  }

  AfterMusicChange();

  SOUND->PlayOnceFromAnnouncer("select music intro");

  ScreenWithMenuElements::BeginScreen();
}

ScreenSelectMusic::~ScreenSelectMusic() {
  LOG->Trace("ScreenSelectMusic::~ScreenSelectMusic()");
  IMAGECACHE->Undemand("Banner");
}

// If bForce is true, the next request will be started even if it might cause a
// skip.
void ScreenSelectMusic::CheckBackgroundRequests(bool force) {
  if (g_bCDTitleWaiting) {
    // The CDTitle is normally very small, so we don't bother waiting to display
    // it.
    std::string path;
    if (!background_loader_.IsCacheFileFinished(g_sCDTitlePath, path)) {
      return;
    }

    g_bCDTitleWaiting = false;

    std::string cd_title_path = path;

    if (cd_title_path.empty() || !IsAFile(cd_title_path)) {
      cd_title_path =
          g_bWantFallbackCdTitle ? fallback_cd_title_path_ : std::string("");
    }

    if (!cd_title_path.empty()) {
      TEXTUREMAN->DisableOddDimensionWarning();
      spr_cd_title_front_.Load(cd_title_path);
      spr_cd_title_back_.Load(cd_title_path);
      TEXTUREMAN->EnableOddDimensionWarning();
    }

    background_loader_.FinishedWithCachedFile(g_sCDTitlePath);
  }

  /* Loading the rest can cause small skips, so don't do it until the wheel
   * settles. Do load if we're transitioning out, though, so we don't miss
   * starting the music for the options screen if a song is selected quickly.
   * Also, don't do this if the wheel is locked, since we're just bouncing
   * around after selecting TYPE_RANDOM, and it'll take a while before the wheel
   * will settle. */
  if (!music_wheel_.IsSettled() && !music_wheel_.WheelIsLocked() && !force) {
    return;
  }

  if (g_bBannerWaiting) {
    if (banner_.GetTweenTimeLeft() > 0) {
      return;
    }

    std::string path;
    bool free_cache = false;
    if (TEXTUREMAN->IsTextureRegistered(
            Sprite::SongBannerTexture(g_sBannerPath))) {
      /* If the file is already loaded into a texture, it's finished,
       * and we only do this to honor the HighQualTime value. */
      path = g_sBannerPath;
    } else {
      if (!background_loader_.IsCacheFileFinished(g_sBannerPath, path)) {
        return;
      }
      free_cache = true;
    }

    g_bBannerWaiting = false;
    banner_.Load(path, true);

    if (free_cache) {
      background_loader_.FinishedWithCachedFile(g_sBannerPath);
    }
  }

  // Nothing else is going.  Start the music, if we haven't yet.
  if (g_bSampleMusicWaiting) {
    if (g_ScreenStartedLoadingAt.Ago() < SAMPLE_MUSIC_DELAY_INIT) {
      return;
    }

    if (preview_disabled_) {
      preview_disabled_ = false;
      g_bSampleMusicWaiting = false;
      return;
    }

    // Don't start the music sample when moving fast.
    if (g_StartedLoadingAt.Ago() < SAMPLE_MUSIC_DELAY && !force) {
      return;
    }

    g_bSampleMusicWaiting = false;

    GameSoundManager::PlayMusicParams play_params;
    play_params.file_ = HandleLuaMusicFile(sample_music_to_play_);
    play_params.timing_ = sample_music_timing_data_;
    play_params.force_loop_ = SAMPLE_MUSIC_LOOPS;
    play_params.start_second_ = sample_start_seconds_;
    play_params.length_seconds_ = sample_length_seconds_;
    play_params.fade_out_length_seconds_ = SAMPLE_MUSIC_FADE_OUT_SECONDS;
    play_params.align_beat_ = ALIGN_MUSIC_BEATS;
    play_params.apply_music_rate_ = true;

    GameSoundManager::PlayMusicParams fallback_music;
    fallback_music.file_ = loop_music_path_;
    fallback_music.fade_in_length_seconds_ =
        SAMPLE_MUSIC_FALLBACK_FADE_IN_SECONDS;
    fallback_music.align_beat_ = ALIGN_MUSIC_BEATS;

    SOUND->PlayMusic(play_params, fallback_music);
  }
}

void ScreenSelectMusic::Update(float delta_time) {
  if (!IsTransitioning()) {
    if (IDLE_COMMENT_SECONDS > 0 &&
        timer_idle_comment_.Ago() >= IDLE_COMMENT_SECONDS) {
      SOUND->PlayOnceFromAnnouncer(name_ + " IdleComment");
      timer_idle_comment_.GetDeltaTime();
    }
  }

  ScreenWithMenuElements::Update(delta_time);

  CheckBackgroundRequests(false);
}

bool ScreenSelectMusic::Input(const InputEventPlus& input) {
  // HACK: This screen eats mouse inputs if we don't check for them first.
  bool mouse_evt = false;
  for (int i = MOUSE_LEFT; i <= MOUSE_WHEELDOWN; i++) {
    if (input.device_i_ == DeviceInput(DEVICE_MOUSE, (DeviceButton)i)) {
      mouse_evt = true;
    }
  }
  if (mouse_evt && !(selection_state_ == SelectionState_Finalized &&
                     input.menu_i_ == GAME_BUTTON_START)) {
    return ScreenWithMenuElements::Input(input);
  }
  //	LOG->Trace( "ScreenSelectMusic::Input()" );

  // reset announcer timer
  timer_idle_comment_.GetDeltaTime();

  // debugging?
  // I just like being able to see untransliterated titles occasionally.
  if (input.device_i_.device_ == DEVICE_KEYBOARD &&
      input.device_i_.button_ == KEY_F9) {
    if (input.type_ != IET_FIRST_PRESS) {
      return false;
    }
    PREFSMAN->show_native_language_.Set(!PREFSMAN->show_native_language_);
    MESSAGEMAN->Broadcast("DisplayLanguageChanged");
    music_wheel_.RebuildWheelItems();
    return true;
  }

  if (!IsTransitioning() && selection_state_ != SelectionState_Finalized) {
    bool holding_ctrl =
        INPUTFILTER->IsBeingPressed(DeviceInput(DEVICE_KEYBOARD, KEY_LCTRL)) ||
        INPUTFILTER->IsBeingPressed(DeviceInput(DEVICE_KEYBOARD, KEY_RCTRL));

    bool holding_shift =
        INPUTFILTER->IsBeingPressed(DeviceInput(DEVICE_KEYBOARD, KEY_LSHIFT)) ||
        INPUTFILTER->IsBeingPressed(DeviceInput(DEVICE_KEYBOARD, KEY_RSHIFT));

    wchar_t c = INPUTMAN->DeviceInputToChar(input.device_i_, false);
    MakeUpper(&c, 1);

    if (holding_shift && holding_ctrl && c == 'R' && music_wheel_.IsSettled()) {
      // Reload the currently selected song. -Kyz
      Song* to_reload = music_wheel_.GetSelectedSong();
      if (to_reload) {
        if (to_reload->ReloadFromSongDir()) {
          // Refresh wheel item on success so that users can see the updates
          // immediately without needing to move the wheel.
          music_wheel_.ReloadSongList();
        }
        return true;
      }
    } else if (holding_ctrl && (c >= 'A') && (c <= 'Z')) {
      // Only allow changing the sort order if the wheel is not locked
      // and we're not in course mode. -aj
      if (!music_wheel_.WheelIsLocked() && !GAMESTATE->IsCourseMode()) {
        SortOrder so = GAMESTATE->sort_order_;
        // When in Artist sort, this means first letter of the artist.
        // Otherwise, if not in Title sort already, switch to Title sort.
        if (so != SORT_ARTIST) {
          so = SORT_TITLE;
        }

        GAMESTATE->preferred_sort_order_ = so;
        GAMESTATE->sort_order_.Set(so);
        // Odd, changing the sort order requires us to call SetOpenSection more
        // than once
        music_wheel_.ChangeSort(so);
        music_wheel_.SetOpenSection(ssprintf("%c", c));

        music_wheel_.SelectSection(ssprintf("%c", c));
        music_wheel_.ChangeSort(so);
        music_wheel_.SetOpenSection(ssprintf("%c", c));
        AfterMusicChange();
        return true;
      }
    } else if (
        input.device_i_.device_ == DEVICE_KEYBOARD && holding_ctrl &&
        input.device_i_.button_ == KEY_BACK && input.type_ == IET_FIRST_PRESS &&
        music_wheel_.IsSettled()) {
      // Keyboard shortcut to delete a song from disk (ctrl + backspace)
      Song* song_to_delete = music_wheel_.GetSelectedSong();
      if (song_to_delete && PREFSMAN->allow_song_deletion_.Get()) {
        song_awaiting_deletion_confirmation_ = song_to_delete;
        ScreenPrompt::Prompt(
            SM_ConfirmDeleteSong,
            ssprintf(
                PERMANENTLY_DELETE.GetValue().c_str(),
                song_to_delete->main_title_.c_str(),
                song_to_delete->GetSongDir().c_str()),
            PROMPT_YES_NO);
        return true;
      }
    }
  }

  if (!input.game_i_.IsValid()) {
    return false;  // don't care
  }

  // Handle late joining
  if (input.menu_i_ == GAME_BUTTON_START && input.type_ == IET_FIRST_PRESS &&
      selection_state_ != SelectionState_Finalized) {
    // If the other player is allowed to join on the extra stage, then the
    // summary screen will crash on invalid stage stats. -Kyz
    if (!GAMESTATE->IsAnExtraStage() && GAMESTATE->JoinInput(input.pn_)) {
      return true;
    }

    // Give eligible memory cards time to finish if they are still checking.
    // Also load any late ready cards that got past the
    // Message_StorageDevicesChanged handler load.
    if (GAMESTATE->IsHumanPlayer(input.pn_) &&
        GetNextSelectionState() == SelectionState_Finalized &&
        (LoadReadyMemoryCards() || ShouldWaitForMemoryCard())) {
      return true;
    }
  }

  if (!GAMESTATE->IsHumanPlayer(input.pn_)) {
    return false;
  }

  // Check for "Press START again for options" button press
  if (selection_state_ == SelectionState_Finalized &&
      input.menu_i_ == GAME_BUTTON_START && input.type_ != IET_RELEASE &&
      OPTIONS_MENU_AVAILABLE.GetValue()) {
    if (go_to_options_) {
      return false;  // got it already
    }
    if (!allow_options_menu_) {
      return false;  // not allowed
    }

    if (!allow_options_menu_repeat_ && input.type_ == IET_REPEAT) {
      return false;  // not allowed yet
    }

    go_to_options_ = true;
    if (PLAY_SOUND_ON_ENTERING_OPTIONS_MENU) {
      sound_start_.Play(true);
    }
    this->PlayCommand("ShowEnteringOptions");

    // Re-queue SM_BeginFadingOut, since ShowEnteringOptions may have
    // short-circuited animations.
    this->ClearMessageQueue(SM_BeginFadingOut);
    this->PostScreenMessage(SM_BeginFadingOut, this->GetTweenTimeLeft());

    return true;
  }

  if (IsTransitioning()) {
    return false;  // ignore
  }

  // Handle unselect steps
  // xxx: select button could conflict with OptionsList here -aj
  if (selection_state_ == SelectionState_SelectingSteps &&
      steps_chosen_[input.pn_] && input.menu_i_ == GAME_BUTTON_SELECT &&
      input.type_ == IET_FIRST_PRESS) {
    Message msg("StepsUnchosen");
    msg.SetParam("Player", input.pn_);
    MESSAGEMAN->Broadcast(msg);
    steps_chosen_[input.pn_] = false;
    return true;
  }

  if (selection_state_ == SelectionState_Finalized ||
      steps_chosen_[input.pn_]) {
    return false;  // ignore
  }

  if (USE_PLAYER_SELECT_MENU) {
    if (input.type_ == IET_RELEASE && input.menu_i_ == GAME_BUTTON_SELECT) {
      SCREENMAN->AddNewScreenToTop(SELECT_MENU_NAME, SM_BackFromPlayerOptions);
    }
  }

  // handle OptionsList input
  if (USE_OPTIONS_LIST) {
    PlayerNumber pn = input.pn_;
    if (pn != PLAYER_INVALID) {
      if (options_list_[pn].IsOpened()) {
        return options_list_[pn].Input(input);
      } else {
        if (input.type_ == IET_RELEASE && input.menu_i_ == GAME_BUTTON_SELECT &&
            accept_select_release_[pn]) {
          options_list_[pn].Open();
        }
      }
    }
  }

  if (input.menu_i_ == GAME_BUTTON_SELECT && input.type_ != IET_REPEAT) {
    accept_select_release_[input.pn_] = (input.type_ == IET_FIRST_PRESS);
  }

  if (SELECT_MENU_AVAILABLE && input.menu_i_ == GAME_BUTTON_SELECT &&
      input.type_ != IET_REPEAT) {
    UpdateSelectButton(input.pn_, input.type_ == IET_FIRST_PRESS);
  }

  if (SELECT_MENU_AVAILABLE && select_is_down_[input.pn_]) {
    if (input.type_ == IET_FIRST_PRESS && SELECT_MENU_CHANGES_DIFFICULTY) {
      switch (input.menu_i_) {
        case GAME_BUTTON_LEFT:
          ChangeSteps(input.pn_, -1);
          accept_select_release_[input.pn_] = false;
          break;
        case GAME_BUTTON_RIGHT:
          ChangeSteps(input.pn_, +1);
          accept_select_release_[input.pn_] = false;
          break;
        case GAME_BUTTON_START:
          accept_select_release_[input.pn_] = false;
          if (MODE_MENU_AVAILABLE) {
            music_wheel_.NextSort();
          } else {
            sound_locked_.Play(true);
          }
          break;
        default:
          break;
      }
    }
    if (input.type_ == IET_FIRST_PRESS && input.menu_i_ != GAME_BUTTON_SELECT) {
      Message msg("SelectMenuInput");
      msg.SetParam("Player", input.pn_);
      msg.SetParam(
          "Button",
          GameButtonToString(INPUTMAPPER->GetInputScheme(), input.menu_i_));
      MESSAGEMAN->Broadcast(msg);
      accept_select_release_[input.pn_] = false;
    }
    if (input.type_ == IET_FIRST_PRESS) {
      g_CanOpenOptionsList.Touch();
    }
    if (g_CanOpenOptionsList.Ago() > OPTIONS_LIST_TIMEOUT) {
      accept_select_release_[input.pn_] = false;
    }
    return true;
  }

  if (selection_state_ == SelectionState_SelectingSong &&
      (input.menu_i_ == game_button_next_song_ ||
       input.menu_i_ == game_button_previous_song_ ||
       input.menu_i_ == GAME_BUTTON_SELECT)) {
    {
      // If we're rouletting, hands off.
      if (music_wheel_.IsRouletting()) {
        return false;
      }

      bool left_is_down = false;
      bool right_is_down = false;

      FOREACH_HumanPlayer(p) {
        if (options_list_[p].IsOpened()) {
          continue;
        }
        if (SELECT_MENU_AVAILABLE &&
            INPUTMAPPER->IsBeingPressed(GAME_BUTTON_SELECT, p)) {
          continue;
        }

        left_is_down |=
            INPUTMAPPER->IsBeingPressed(game_button_previous_song_, p);
        right_is_down |= INPUTMAPPER->IsBeingPressed(game_button_next_song_, p);
      }

      bool both_down = left_is_down && right_is_down;
      bool neither_down = !left_is_down && !right_is_down;

      if (neither_down) {
        // Both buttons released.
        music_wheel_.Move(0);
      } else if (both_down) {
        music_wheel_.Move(0);
        if (input.type_ == IET_FIRST_PRESS) {
          if (input.menu_i_ == game_button_previous_song_) {
            music_wheel_.ChangeMusicUnlessLocked(-1);
          } else if (input.menu_i_ == game_button_next_song_) {
            music_wheel_.ChangeMusicUnlessLocked(+1);
          }
        }
      } else if (left_is_down) {
        if (input.type_ != IET_RELEASE) {
          MESSAGEMAN->Broadcast("PreviousSong");
          music_wheel_.Move(-1);
        }
      } else if (right_is_down) {
        if (input.type_ != IET_RELEASE) {
          MESSAGEMAN->Broadcast("NextSong");
          music_wheel_.Move(+1);
        }
      } else {
        FAIL_M("Logic bug: L/R keys in an impossible state?");
      }

      // Reset the repeat timer when the button is released.
      // This fixes jumping when you release Left and Right after entering the
      // sort code at the same if L & R aren't released at the exact same time.
      if (input.type_ == IET_RELEASE) {
        INPUTMAPPER->ResetKeyRepeat(game_button_previous_song_, input.pn_);
        INPUTMAPPER->ResetKeyRepeat(game_button_next_song_, input.pn_);
      }
    }
  }

  // To allow changing steps with gamebuttons, NOT WITH THE GODDAMN CODEDETECTOR
  // yeah, wanted this since a while ago... -DaisuMaster
  if (CHANGE_STEPS_WITH_GAME_BUTTONS) {
    // Avoid any event not being first press
    if (input.type_ != IET_FIRST_PRESS) {
      return false;
    }

    if (selection_state_ == SelectionState_SelectingSong) {
      if (input.menu_i_ == game_button_previous_difficulty_) {
        if (GAMESTATE->IsAnExtraStageAndSelectionLocked()) {
          sound_locked_.Play(true);
        } else {
          ChangeSteps(input.pn_, -1);
        }
      } else if (input.menu_i_ == game_button_next_difficulty_) {
        if (GAMESTATE->IsAnExtraStageAndSelectionLocked()) {
          sound_locked_.Play(true);
        } else {
          ChangeSteps(input.pn_, +1);
        }
      }
    }
  }

  // Actually I don't like to just copy and paste code because it may go
  // wrong if something goes overlooked -DaisuMaster
  if (CHANGE_GROUPS_WITH_GAME_BUTTONS) {
    if (input.type_ != IET_FIRST_PRESS) {
      return false;
    }

    if (selection_state_ == SelectionState_SelectingSong) {
      if (input.menu_i_ == game_button_previous_group_) {
        if (GAMESTATE->IsAnExtraStageAndSelectionLocked()) {
          sound_locked_.Play(true);
        } else {
          std::string new_group = music_wheel_.JumpToPrevGroup();
          music_wheel_.SelectSection(new_group);
          music_wheel_.SetOpenSection(new_group);
          MESSAGEMAN->Broadcast("PreviousGroup");
          AfterMusicChange();
        }
      } else if (input.menu_i_ == game_button_next_group_) {
        if (GAMESTATE->IsAnExtraStageAndSelectionLocked()) {
          sound_locked_.Play(true);
        } else {
          std::string new_group = music_wheel_.JumpToNextGroup();
          music_wheel_.SelectSection(new_group);
          music_wheel_.SetOpenSection(new_group);
          MESSAGEMAN->Broadcast("NextGroup");
          AfterMusicChange();
        }
      }
    }
  }

  // two part confirms only means we can actually change songs here,
  // so we need some hackery. -aj
  if (TWO_PART_CONFIRMS_ONLY) {
    if (selection_state_ == SelectionState_SelectingSteps) {
      if (input.menu_i_ == game_button_previous_song_) {
        if (GAMESTATE->IsAnExtraStageAndSelectionLocked()) {
          sound_locked_.Play(true);
        } else {
          selection_state_ = SelectionState_SelectingSong;
          MESSAGEMAN->Broadcast("TwoPartConfirmCanceled");
          MESSAGEMAN->Broadcast("PreviousSong");
          music_wheel_.ChangeMusicUnlessLocked(-1);
        }
      } else if (input.menu_i_ == game_button_next_song_) {
        if (GAMESTATE->IsAnExtraStageAndSelectionLocked()) {
          sound_locked_.Play(true);
        } else {
          selection_state_ = SelectionState_SelectingSong;
          MESSAGEMAN->Broadcast("TwoPartConfirmCanceled");
          MESSAGEMAN->Broadcast("NextSong");
          music_wheel_.ChangeMusicUnlessLocked(+1);
        }
      }
      // added an entry for difficulty change with gamebuttons -DaisuMaster
      else if (input.menu_i_ == game_button_previous_difficulty_) {
        if (GAMESTATE->IsAnExtraStageAndSelectionLocked()) {
          sound_locked_.Play(true);
        } else {
          selection_state_ = SelectionState_SelectingSong;
          MESSAGEMAN->Broadcast("TwoPartConfirmCanceled");
          ChangeSteps(input.pn_, -1);
        }
      } else if (input.menu_i_ == game_button_next_difficulty_) {
        if (GAMESTATE->IsAnExtraStageAndSelectionLocked()) {
          sound_locked_.Play(true);
        } else {
          selection_state_ = SelectionState_SelectingSong;
          MESSAGEMAN->Broadcast("TwoPartConfirmCanceled");
          ChangeSteps(input.pn_, +1);
        }
      }
      // added also for groupchanges
      else if (input.menu_i_ == game_button_previous_group_) {
        if (GAMESTATE->IsAnExtraStageAndSelectionLocked()) {
          sound_locked_.Play(true);
        } else {
          std::string new_group = music_wheel_.JumpToPrevGroup();
          music_wheel_.SelectSection(new_group);
          music_wheel_.SetOpenSection(new_group);
          MESSAGEMAN->Broadcast("TwoPartConfirmCanceled");
          MESSAGEMAN->Broadcast("PreviousGroup");
          AfterMusicChange();
        }
      } else if (input.menu_i_ == game_button_next_group_) {
        if (GAMESTATE->IsAnExtraStageAndSelectionLocked()) {
          sound_locked_.Play(true);
        } else {
          std::string new_group = music_wheel_.JumpToNextGroup();
          music_wheel_.SelectSection(new_group);
          music_wheel_.SetOpenSection(new_group);
          MESSAGEMAN->Broadcast("TwoPartConfirmCanceled");
          MESSAGEMAN->Broadcast("NextGroup");
          AfterMusicChange();
        }
      }
    }
  } else {
    if (selection_state_ == SelectionState_SelectingSteps &&
        input.type_ == IET_FIRST_PRESS && !steps_chosen_[input.pn_]) {
      if (input.menu_i_ == game_button_previous_difficulty_ ||
          input.menu_i_ == game_button_next_difficulty_) {
        if (input.menu_i_ == game_button_previous_difficulty_) {
          if (GAMESTATE->IsAnExtraStageAndSelectionLocked()) {
            sound_locked_.Play(true);
          } else {
            ChangeSteps(input.pn_, -1);
          }
        } else if (input.menu_i_ == game_button_next_difficulty_) {
          if (GAMESTATE->IsAnExtraStageAndSelectionLocked()) {
            sound_locked_.Play(true);
          } else {
            ChangeSteps(input.pn_, +1);
          }
        }
      }
      // This should not be MENUP or MENUDOWN, different games use different
      // buttons to cancel.
      else if (
          input.menu_i_ == game_button_cancel_two_part1_ ||
          input.menu_i_ ==
              game_button_cancel_two_part2_)  // &&
                                              // TWO_PART_DESELECTS_WITH_MENUUPDOWN
      {
        if (GAMESTATE->IsAnExtraStageAndSelectionLocked()) {
          sound_locked_.Play(true);
        } else {
          // XXX: should this be called "TwoPartCancelled"?
          float seconds = menu_timer_->GetSeconds();
          if (seconds > 10) {
            Message msg("SongUnchosen");
            msg.SetParam("Player", input.pn_);
            MESSAGEMAN->Broadcast(msg);
            // unset all steps
            FOREACH_ENUM(PlayerNumber, p)
            steps_chosen_[p] = false;
            selection_state_ = SelectionState_SelectingSong;
          }
        }
      }
    }
  }

  if (input.type_ == IET_FIRST_PRESS && DetectCodes(input)) {
    return true;
  }

  return ScreenWithMenuElements::Input(input);
}

bool ScreenSelectMusic::DetectCodes(const InputEventPlus& input) {
  // We have a fallback close folder command that's a prefix of the prev/next
  // steps command, so make sure to handle close folder command first.
  if (CodeDetector::EnteredCloseFolder(input.game_i_.controller_)) {
    if (GAMESTATE->IsAnExtraStageAndSelectionLocked() ||
        music_wheel_.WheelIsLocked() || music_wheel_.IsRouletting()) {
      sound_locked_.Play(true);
    } else {
      music_wheel_.CloseOpenSectionOneLevel();
      AfterMusicChange();
    }
  } else if (
      CodeDetector::EnteredPrevSteps(input.game_i_.controller_) &&
      !CHANGE_STEPS_WITH_GAME_BUTTONS) {
    if (GAMESTATE->IsAnExtraStageAndSelectionLocked()) {
      sound_locked_.Play(true);
    } else {
      ChangeSteps(input.pn_, -1);
    }
  } else if (
      CodeDetector::EnteredNextSteps(input.game_i_.controller_) &&
      !CHANGE_STEPS_WITH_GAME_BUTTONS) {
    if (GAMESTATE->IsAnExtraStageAndSelectionLocked()) {
      sound_locked_.Play(true);
    } else {
      ChangeSteps(input.pn_, +1);
    }
  } else if (CodeDetector::EnteredModeMenu(input.game_i_.controller_)) {
    if (MODE_MENU_AVAILABLE) {
      music_wheel_.ChangeSort(SORT_MODE_MENU);
    } else {
      sound_locked_.Play(true);
    }
  } else if (CodeDetector::EnteredNextSort(input.game_i_.controller_)) {
    if (GAMESTATE->IsAnExtraStageAndSelectionLocked() ||
        music_wheel_.WheelIsLocked() || music_wheel_.IsRouletting()) {
      sound_locked_.Play(true);
    } else if (!GAMESTATE->IsCourseMode()) {
      // Only change sorts in non-course mode
      music_wheel_.NextSort();
    }
  } else if (
      !GAMESTATE->IsAnExtraStageAndSelectionLocked() &&
      !music_wheel_.WheelIsLocked() && !music_wheel_.IsRouletting() &&
      CodeDetector::DetectAndAdjustMusicOptions(input.game_i_.controller_)) {
    sound_options_change_.Play(true);

    Message msg("PlayerOptionsChanged");
    msg.SetParam("PlayerNumber", input.pn_);
    MESSAGEMAN->Broadcast(msg);

    MESSAGEMAN->Broadcast("SongOptionsChanged");
  } else if (
      CodeDetector::EnteredNextGroup(input.game_i_.controller_) &&
      !CHANGE_GROUPS_WITH_GAME_BUTTONS) {
    if (GAMESTATE->IsAnExtraStageAndSelectionLocked() ||
        music_wheel_.WheelIsLocked() || music_wheel_.IsRouletting()) {
      sound_locked_.Play(true);
    } else {
      std::string new_group = music_wheel_.JumpToNextGroup();
      music_wheel_.SelectSection(new_group);
      music_wheel_.SetOpenSection(new_group);
      MESSAGEMAN->Broadcast("NextGroup");
      AfterMusicChange();
    }
  } else if (
      CodeDetector::EnteredPrevGroup(input.game_i_.controller_) &&
      !CHANGE_GROUPS_WITH_GAME_BUTTONS) {
    if (GAMESTATE->IsAnExtraStageAndSelectionLocked() ||
        music_wheel_.WheelIsLocked() || music_wheel_.IsRouletting()) {
      sound_locked_.Play(true);
    } else {
      std::string new_group = music_wheel_.JumpToPrevGroup();
      music_wheel_.SelectSection(new_group);
      music_wheel_.SetOpenSection(new_group);
      MESSAGEMAN->Broadcast("PreviousGroup");
      AfterMusicChange();
    }
  } else {
    return false;
  }
  return true;
}

void ScreenSelectMusic::UpdateSelectButton(
    PlayerNumber pn, bool select_is_down) {
  if (!SELECT_MENU_AVAILABLE || !CanChangeSong()) {
    select_is_down = false;
  }

  if (select_is_down_[pn] != select_is_down) {
    select_is_down_[pn] = select_is_down;
    Message msg(select_is_down ? "SelectMenuOpened" : "SelectMenuClosed");
    msg.SetParam("Player", pn);
    MESSAGEMAN->Broadcast(msg);
  }
}

void ScreenSelectMusic::ChangeSteps(PlayerNumber pn, int dir) {
  LOG->Trace("ScreenSelectMusic::ChangeSteps( %d, %d )", pn, dir);

  ASSERT(GAMESTATE->IsHumanPlayer(pn));

  if (GAMESTATE->cur_song_) {
    selection_[pn] += dir;
    if (WRAP_CHANGE_STEPS) {
      wrap(selection_[pn], steps_.size());
    } else {
      if (rage_clamp(selection_[pn], 0, steps_.size() - 1)) {
        return;
      }
    }

    // the user explicity switched difficulties. Update the preferred Difficulty
    // and StepsType
    Steps* steps = steps_[selection_[pn]];
    GAMESTATE->ChangePreferredDifficultyAndStepsType(
        pn, steps->GetDifficulty(), steps->steps_type_);
  } else if (GAMESTATE->cur_course_) {
    selection_[pn] += dir;
    if (WRAP_CHANGE_STEPS) {
      wrap(selection_[pn], trails_.size());
    } else {
      if (rage_clamp(selection_[pn], 0, trails_.size() - 1)) {
        return;
      }
    }

    // the user explicity switched difficulties. Update the preferred Difficulty
    // and StepsType
    Trail* trail = trails_[selection_[pn]];
    GAMESTATE->ChangePreferredCourseDifficultyAndStepsType(
        pn, trail->course_difficulty_, trail->steps_type_);
  } else {
    // If we're showing multiple StepsTypes in the list, don't allow
    // changing the difficulty/StepsType when a non-Song, non-Course is
    // selected. Changing the preferred Difficulty and StepsType by
    // direction is complicated when multiple StepsTypes are being shown,
    // so we don't support it.
    if (CommonMetrics::AUTO_SET_STYLE) {
      return;
    }
    if (!GAMESTATE->ChangePreferredDifficulty(pn, dir)) {
      return;
    }
  }

  std::vector<PlayerNumber> vpns;
  FOREACH_HumanPlayer(p) {
    if (pn == p || GAMESTATE->DifficultiesLocked()) {
      selection_[p] = selection_[pn];
      vpns.push_back(p);
    }
  }
  AfterStepsOrTrailChange(vpns);

  float balance = GameSoundManager::GetPlayerBalance(pn);
  if (dir < 0) {
    sound_difficulty_easier_.SetProperty("Pan", balance);
    sound_difficulty_easier_.PlayCopy(true);
  } else {
    sound_difficulty_harder_.SetProperty("Pan", balance);
    sound_difficulty_harder_.PlayCopy(true);
  }
  GAMESTATE->ForceOtherPlayersToCompatibleSteps(pn);

  Message msg("ChangeSteps");
  msg.SetParam("Player", pn);
  msg.SetParam("Direction", dir);
  MESSAGEMAN->Broadcast(msg);
}

void ScreenSelectMusic::HandleMessage(const Message& msg) {
  if (running_ && msg == Message_PlayerJoined) {
    PlayerNumber pn;
    bool b = msg.GetParam("Player", pn);
    ASSERT(b);

    if (GAMESTATE->CanAcceptProfile(pn)) {
      MEMCARDMAN->UnlockCard(pn);
    }

    // A checking card will be loaded asynchronously when it becomes ready.
    // Continue loading local profiles immediately when there is no card.
    bool profile_loaded = false;
    if (MEMCARDMAN->GetCardState(pn) != MemoryCardState_Checking &&
        GAMESTATE->HaveProfileToLoad(pn)) {
      profile_loaded = LoadPlayerProfile(pn);
    }

    PlayerNumber master_pn = GAMESTATE->GetMasterPlayerNumber();
    // The current steps may no longer be playable. If one player has double
    // steps selected, they are no longer playable now that P2 has joined.

    // TODO: Invalidate the CurSteps only if they are no longer playable.
    // That way, after music change will clamp to the nearest in the
    // StepsDisplayList.
    GAMESTATE->cur_steps_[master_pn].SetWithoutBroadcast(nullptr);
    FOREACH_ENUM(PlayerNumber, p)
    GAMESTATE->cur_steps_[p].SetWithoutBroadcast(nullptr);

    /* If a course is selected, it may no longer be playable.
     * Let MusicWheel know about the late join. */
    music_wheel_.PlayerJoined();

    AfterMusicChange();

    int sel = 0;
    if (profile_loaded) {
      SCREENMAN
          ->ZeroNextUpdate();  // be kind, don't skip frames if you can avoid it
    }

    selection_[pn] = sel;
    if (GAMESTATE->IsCourseMode()) {
      Trail* trail = trails_.empty() ? nullptr : trails_[selection_[pn]];
      GAMESTATE->cur_trail_[pn].Set(trail);
    } else {
      Steps* steps = steps_.empty() ? nullptr : steps_[selection_[pn]];

      // handle changing rave difficulty on join
      if (GAMESTATE->play_mode_ == PLAY_MODE_RAVE) {
        steps = steps_[selection_[master_pn]];
      }

      GAMESTATE->cur_steps_[pn].Set(steps);
    }
  } else if (
      running_ && msg == Message_StorageDevicesChanged &&
      selection_state_ != SelectionState_Finalized && !IsTransitioning() &&
      SCREENMAN->GetTopScreen() == this) {
    LoadReadyMemoryCards();
  }

  ScreenWithMenuElements::HandleMessage(msg);
}

bool ScreenSelectMusic::LoadPlayerProfile(PlayerNumber pn) {
  if (!GAMESTATE->LoadProfileOverGuest(pn, true)) {
    return false;
  }

  Message msg(MessageIDToString(Message_PlayerProfileSet));
  msg.SetParam("Player", pn);
  MESSAGEMAN->Broadcast(msg);
  return true;
}

bool ScreenSelectMusic::LoadReadyMemoryCards() {
  bool profile_loaded = false;
  FOREACH_HumanPlayer(pn) {
    if (MEMCARDMAN->GetCardState(pn) == MemoryCardState_Ready) {
      profile_loaded |= LoadPlayerProfile(pn);
    }
  }

  if (profile_loaded) {
    music_wheel_.ReloadSongList();
    SCREENMAN->ZeroNextUpdate();
  }

  return profile_loaded;
}

bool ScreenSelectMusic::ShouldWaitForMemoryCard() const {
  FOREACH_HumanPlayer(pn) {
    if (GAMESTATE->CanAcceptProfile(pn) &&
        MEMCARDMAN->GetCardState(pn) == MemoryCardState_Checking &&
        MEMCARDMAN->GetCardStateAge(pn) < CARD_LATE_JOIN_BLOCK_SECONDS) {
      return true;
    }
  }
  return false;
}

void ScreenSelectMusic::HandleScreenMessage(const ScreenMessage sm) {
  if (sm == SM_AllowOptionsMenuRepeat) {
    allow_options_menu_repeat_ = true;
  } else if (sm == SM_MenuTimer) {
    if (music_wheel_.IsRouletting()) {
      MenuStart(InputEventPlus());
      menu_timer_->SetSeconds(ROULETTE_TIMER_SECONDS);
      menu_timer_->Start();
    } else if (
        DO_ROULETTE_ON_MENU_TIMER &&
        music_wheel_.GetSelectedSong() == nullptr &&
        music_wheel_.GetSelectedCourse() == nullptr) {
      music_wheel_.StartRoulette();
      menu_timer_->SetSeconds(ROULETTE_TIMER_SECONDS);
      menu_timer_->Start();
    } else {
      // Finish sort changing so that the wheel can respond immediately to
      // our request to choose random.
      music_wheel_.FinishChangingSorts();
      if (music_wheel_.GetSelectedSong() == nullptr &&
          music_wheel_.GetSelectedCourse() == nullptr) {
        music_wheel_.StartRandom();
      }

      MenuStart(InputEventPlus());
    }
    return;
  } else if (sm == SM_GoToPrevScreen) {
    /* We may have stray SM_SongChanged messages from the music wheel.
     * We can't handle them anymore, since the title menu (and attract
     * screens) reset the game state, so just discard them. */
    ClearMessageQueue();
  } else if (sm == SM_BeginFadingOut) {
    allow_options_menu_ = false;
    if (OPTIONS_MENU_AVAILABLE && !go_to_options_) {
      this->PlayCommand("HidePressStartForOptions");
    }

    this->PostScreenMessage(SM_GoToNextScreen, this->GetTweenTimeLeft());
  } else if (sm == SM_GoToNextScreen) {
    if (!go_to_options_) {
      SOUND->StopMusic();
    }
  } else if (sm == SM_SongChanged) {
    AfterMusicChange();
  } else if (sm == SM_SortOrderChanging)  // happens immediately
  {
    this->PlayCommand("SortChange");
  } else if (sm == SM_GainFocus) {
    CodeDetector::RefreshCacheItems(CODES);
  } else if (sm == SM_LoseFocus) {
    CodeDetector::RefreshCacheItems();  // reset for other screens
  } else if (sm == SM_ConfirmDeleteSong) {
    if (ScreenPrompt::last_answer_ == ANSWER_YES) {
      OnConfirmSongDeletion();
    } else {
      // need to resume the song preview that was automatically paused
      music_wheel_.ChangeMusic(0);
    }
  } else if (sm == SM_ConfirmExit) {
    if (ScreenPrompt::last_answer_ == ANSWER_YES) {
      ConfirmedCancel(SM_GoToPrevScreen);
    }
  }

  ScreenWithMenuElements::HandleScreenMessage(sm);
}

bool ScreenSelectMusic::MenuStart(const InputEventPlus& input) {
  if (input.type_ != IET_FIRST_PRESS) {
    return false;
  }

  /* If select is being pressed at the same time, this is probably an attempt
   * to change the sort, not to pick a song or difficulty. If it gets here,
   * the actual select press was probably hit during a tween and ignored.
   * Ignore it. */
  if (input.pn_ != PLAYER_INVALID &&
      INPUTMAPPER->IsBeingPressed(GAME_BUTTON_SELECT, input.pn_)) {
    return false;
  }

  // Honor locked input for start presses.
  if (lock_input_secs_ > 0) {
    return false;
  }

  switch (selection_state_) {
    DEFAULT_FAIL(selection_state_);
    case SelectionState_SelectingSong:
      // If false, we don't have a selection just yet.
      if (!music_wheel_.Select()) {
        return false;
      }

      // a song was selected
      if (music_wheel_.GetSelectedSong() != nullptr) {
        if (TWO_PART_CONFIRMS_ONLY &&
            SAMPLE_MUSIC_PREVIEW_MODE ==
                SampleMusicPreviewMode_StartToPreview) {
          // start playing the preview music.
          g_bSampleMusicWaiting = true;
          CheckBackgroundRequests(true);
        }

        const bool is_new =
            PROFILEMAN->IsSongNew(music_wheel_.GetSelectedSong());
        bool is_hard = false;
        FOREACH_HumanPlayer(p) {
          if (GAMESTATE->cur_steps_[p] &&
              GAMESTATE->cur_steps_[p]->GetMeter() >= HARD_COMMENT_METER) {
            is_hard = true;
          }
        }

        // See if this song is a repeat.
        // If we're in event mode, only check the last five songs.
        bool is_repeat = false;
        int i = 0;
        if (GAMESTATE->IsEventMode()) {
          i = std::max(0, int(STATSMAN->played_stage_stats_.size()) - 5);
        }
        for (; i < (int)STATSMAN->played_stage_stats_.size(); ++i) {
          if (STATSMAN->played_stage_stats_[i].played_songs_.back() ==
              music_wheel_.GetSelectedSong()) {
            is_repeat = true;
          }
        }

        // Don't complain about repeats if the user didn't get to pick.
        if (GAMESTATE->IsAnExtraStageAndSelectionLocked()) {
          is_repeat = false;
        }

        if (is_repeat) {
          SOUND->PlayOnceFromAnnouncer("select music comment repeat");
        } else if (is_new) {
          SOUND->PlayOnceFromAnnouncer("select music comment new");
        } else if (is_hard) {
          SOUND->PlayOnceFromAnnouncer("select music comment hard");
        } else {
          SOUND->PlayOnceFromAnnouncer("select music comment general");
        }

        /* If we're in event mode, we may have just played a course (putting
         * us in course mode). Make sure we're in a single song mode. */
        if (GAMESTATE->IsCourseMode()) {
          GAMESTATE->play_mode_.Set(PLAY_MODE_REGULAR);
        }
      } else if (music_wheel_.GetSelectedCourse() != nullptr) {
        SOUND->PlayOnceFromAnnouncer("select course comment general");

        Course* course = music_wheel_.GetSelectedCourse();
        ASSERT(course != nullptr);
        GAMESTATE->play_mode_.Set(course->GetPlayMode());

        // apply #LIVES
        if (course->lives_ != -1) {
          FOREACH_EnabledPlayer(pn) {
            PO_GROUP_ASSIGN(
                GAMESTATE->player_state_[pn]->player_options_, ModsLevel_Stage,
                life_type_, LifeType_Battery);
            PO_GROUP_ASSIGN(
                GAMESTATE->player_state_[pn]->player_options_, ModsLevel_Stage,
                battery_lives_, course->lives_);
          }
        }
        if (course->GetCourseType() == COURSE_TYPE_SURVIVAL) {
          FOREACH_EnabledPlayer(pn) {
            PO_GROUP_ASSIGN(
                GAMESTATE->player_state_[pn]->player_options_, ModsLevel_Stage,
                life_type_, LifeType_Time);
          }
        }
      } else {
        // We haven't made a selection yet.
        return false;
      }
      // I believe this is for those who like pump pro. -aj
      MESSAGEMAN->Broadcast("SongChosen");

      break;

    case SelectionState_SelectingSteps: {
      PlayerNumber pn = input.pn_;
      bool initiated_by_menu_timer = pn == PLAYER_INVALID;
      bool all_other_human_players_done = true;
      FOREACH_HumanPlayer(p) {
        if (p == pn) {
          continue;
        }
        all_other_human_players_done &= steps_chosen_[p];
      }

      bool all_players_done_selecting_steps =
          initiated_by_menu_timer || all_other_human_players_done;
      if (TWO_PART_CONFIRMS_ONLY) {
        all_players_done_selecting_steps = true;
      }

      /* TRICKY: if we have a Routine chart selected, we need to ensure
       * the following:
       * 1. Both players must select the same Routine steps.
       * 2. If the other player picks non-Routine steps, this player
       *    cannot pick Routine.
       * 3. If the other player picked Routine steps, and we pick
       *    non-Routine steps, the other player's steps must be unselected.
       * 4. If time runs out, and both players don't have the same Routine
       *    chart selected, we need to bump the player with a Routine
       *    chart selection to a playable chart.
       *    (Right now, we bump them to Beginner... Can we come up with
       *    something better?)
       */

      if (!GAMESTATE->IsCourseMode() && GAMESTATE->GetNumSidesJoined() == 2) {
        bool selected_routine_steps[NUM_PLAYERS], any_selected_routine = false;
        bool selected_same_steps =
            GAMESTATE->cur_steps_[PLAYER_1] == GAMESTATE->cur_steps_[PLAYER_2];

        FOREACH_HumanPlayer(p) {
          const Steps* steps = GAMESTATE->cur_steps_[p];
          const StepsTypeInfo& sti =
              GAMEMAN->GetStepsTypeInfo(steps->steps_type_);

          selected_routine_steps[p] =
              sti.steps_type_category_ == StepsTypeCategory_Routine;
          any_selected_routine |= selected_routine_steps[p];
        }

        if (any_selected_routine) {
          /* Timer ran out. If we haven't agreed on steps, move players with
           * Routine steps down to Beginner. I'll admit that's annoying,
           * but at least they won't lose more stages. */
          if (initiated_by_menu_timer && !selected_same_steps) {
            /* Since m_vpSteps is sorted by Difficulty, the first
             * entry should be the easiest. */
            ASSERT(steps_.size() != 0);
            Steps* steps = steps_[0];

            FOREACH_PlayerNumber(p) {
              if (selected_routine_steps[p]) {
                GAMESTATE->cur_steps_[p].Set(steps);
              }
            }

            break;
          }

          // If the steps don't match up, we need to check some more
          // conditions...
          if (!selected_same_steps) {
            const PlayerNumber other = OPPOSITE_PLAYER[pn];

            if (steps_chosen_[other]) {
              /* Unready the other player if they selected Routine
               * steps, but we didn't. */
              if (selected_routine_steps[other]) {
                steps_chosen_[other] = false;
                all_players_done_selecting_steps =
                    false;  // if the timer ran out, we handled it earlier

                // HACK: send an event to Input to tell it to unready.
                InputEventPlus event;
                event.menu_i_ = GAME_BUTTON_SELECT;
                event.pn_ = other;

                this->Input(event);
              } else if (selected_routine_steps[pn]) {
                /* They selected non-Routine steps, so we can't
                 * select Routine steps. */
                return false;
              }
            }
          }
        }
      }

      if (!all_players_done_selecting_steps) {
        steps_chosen_[pn] = true;
        sound_start_.Play(true);

        // impldiff: Pump it Up Pro uses "StepsSelected". -aj
        Message msg("StepsChosen");
        msg.SetParam("Player", pn);
        MESSAGEMAN->Broadcast(msg);
        return true;
      }
    } break;
  }

  const bool finalizing =
      GetNextSelectionState() == SelectionState_Finalized ||
      (TWO_PART_CONFIRMS_ONLY && menu_timer_->GetSeconds() < 1);
  if (finalizing && !GAMESTATE->IsCourseMode() &&
      GAMESTATE->prepare_song_for_gameplay() != 0) {
    SCREENMAN->PlayInvalidSound();
    return true;
  }

  FOREACH_ENUM(PlayerNumber, p) {
    if (!TWO_PART_SELECTION ||
        selection_state_ == SelectionState_SelectingSteps) {
      if (options_list_[p].IsOpened()) {
        options_list_[p].Close();
      }
    }
    UpdateSelectButton(p, false);
  }

  selection_state_ = GetNextSelectionState();
  Message msg("Start" + SelectionStateToString(selection_state_));
  MESSAGEMAN->Broadcast(msg);

  sound_start_.Play(true);

  // If the MenuTimer has forced us to move on && TWO_PART_CONFIRMS_ONLY,
  // set Selection State to finalized and move on.
  if (TWO_PART_CONFIRMS_ONLY) {
    if (menu_timer_->GetSeconds() < 1) {
      selection_state_ = SelectionState_Finalized;
    }
  }

  if (selection_state_ == SelectionState_Finalized) {
    menu_timer_->Stop();

    FOREACH_HumanPlayer(p) {
      if (!steps_chosen_[p]) {
        steps_chosen_[p] = true;
        // Don't play start sound. We play it again below on finalized
        // m_soundStart.Play(true);

        Message l_msg("StepsChosen");
        l_msg.SetParam("Player", p);
        MESSAGEMAN->Broadcast(l_msg);
      }
    }

    // Now that Steps have been chosen, set a Style that can play them.
    GAMESTATE->SetCompatibleStylesForPlayers();
    GAMESTATE->ForceSharedSidesMatch();

    /* If we're currently waiting on song assets, abort all except the music
     * and start the music, so if we make a choice quickly before background
     * requests come through, the music will still start. */
    g_bCDTitleWaiting = g_bBannerWaiting = false;
    background_loader_.Abort();
    CheckBackgroundRequests(true);

    if (OPTIONS_MENU_AVAILABLE) {
      // show "hold START for options"
      this->PlayCommand("ShowPressStartForOptions");

      allow_options_menu_ = true;

      /* Don't accept a held START for a little while, so it's not
       * hit accidentally.  Accept an initial START right away, though,
       * so we don't ignore deliberate fast presses (which would be
       * annoying). */
      if (PREFSMAN->allow_hold_for_options_.Get()) {
        this->PostScreenMessage(SM_AllowOptionsMenuRepeat, 0.5f);
      }

      StartTransitioningScreen(SM_None);
      float time =
          std::max(SHOW_OPTIONS_MESSAGE_SECONDS, this->GetTweenTimeLeft());
      this->PostScreenMessage(SM_BeginFadingOut, time);
    } else {
      StartTransitioningScreen(SM_BeginFadingOut);
    }
  } else  // !finalized.  Set the timer for selecting difficulty and mods.
  {
    float seconds = menu_timer_->GetSeconds();
    if (seconds < 10) {
      menu_timer_->SetSeconds(TWO_PART_TIMER_SECONDS);  // was 13 -aj
      menu_timer_->Start();
    }
  }
  return true;
}

bool ScreenSelectMusic::MenuBack(const InputEventPlus& /* input */) {
  // Handle unselect song (ffff)
  // todo: this isn't right at all. -aj
  /*
  if( m_SelectionState == SelectionState_SelectingSteps &&
          !m_bStepsChosen[input.pn] && input.MenuI == GAME_BUTTON_BACK &&
          input.type == IET_FIRST_PRESS )
  {
          // if a player has chosen their steps already, don't unchoose song.
          FOREACH_HumanPlayer( p )
                  if( m_bStepsChosen[p] ) return;

          // and if we get here...
          Message msg("SongUnchosen");
          msg.SetParam( "Player", input.pn );
          MESSAGEMAN->Broadcast( msg );
          m_SelectionState = SelectionState_SelectingSong;
          return true;
  }
  */

  Cancel(SM_GoToPrevScreen);
  return true;
}

void ScreenSelectMusic::Cancel(ScreenMessage send_when_done) {
  if (CONFIRM_EXIT) {
    music_wheel_.Move(0);
    ScreenPrompt::Prompt(
        SM_ConfirmExit, PROMPT_BEFORE_EXITING.GetValue().c_str(),
        PROMPT_YES_NO);
  } else {
    ConfirmedCancel(send_when_done);
  }
}

void ScreenSelectMusic::ConfirmedCancel(ScreenMessage send_when_done) {
  background_loader_.Abort();
  ScreenWithMenuElements::Cancel(send_when_done);
}

void ScreenSelectMusic::AfterStepsOrTrailChange(
    const std::vector<PlayerNumber>& vpns) {
  if (TWO_PART_CONFIRMS_ONLY &&
      selection_state_ == SelectionState_SelectingSteps) {
    // if TWO_PART_CONFIRMS_ONLY, changing difficulties unsets the song. -aj
    selection_state_ = SelectionState_SelectingSong;
    MESSAGEMAN->Broadcast("TwoPartConfirmCanceled");
  }

  for (const PlayerNumber& pn : vpns) {
    ASSERT(GAMESTATE->IsHumanPlayer(pn));

    if (GAMESTATE->cur_song_) {
      rage_clamp(selection_[pn], 0, steps_.size() - 1);

      Song* song = GAMESTATE->cur_song_;
      Steps* steps = steps_.empty() ? nullptr : steps_[selection_[pn]];

      GAMESTATE->cur_steps_[pn].Set(steps);
      GAMESTATE->cur_trail_[pn].Set(nullptr);

      int score = 0;
      if (steps) {
        const Profile* profile = PROFILEMAN->IsPersistentProfile(pn)
                                     ? PROFILEMAN->GetProfile(pn)
                                     : PROFILEMAN->GetMachineProfile();
        score = profile->GetStepsHighScoreList(song, steps)
                    .GetTopScore()
                    .GetScore();
      }

      text_high_score_[pn].SetText(ssprintf("%*i", NUM_SCORE_DIGITS, score));
    } else if (GAMESTATE->cur_course_) {
      rage_clamp(selection_[pn], 0, trails_.size() - 1);

      Course* course = GAMESTATE->cur_course_;
      Trail* trail = trails_.empty() ? nullptr : trails_[selection_[pn]];

      GAMESTATE->cur_steps_[pn].Set(nullptr);
      GAMESTATE->cur_trail_[pn].Set(trail);

      int score = 0;
      if (trail) {
        const Profile* profile = PROFILEMAN->IsPersistentProfile(pn)
                                     ? PROFILEMAN->GetProfile(pn)
                                     : PROFILEMAN->GetMachineProfile();
        score = profile->GetCourseHighScoreList(course, trail)
                    .GetTopScore()
                    .GetScore();
      }

      text_high_score_[pn].SetText(ssprintf("%*i", NUM_SCORE_DIGITS, score));
    } else {
      // The numbers shouldn't stay if the current selection is nullptr.
      text_high_score_[pn].SetText(NULL_SCORE_STRING);
    }
  }
}

void ScreenSelectMusic::SwitchPlayerStepDifficulty(
    PlayerNumber pn, Difficulty d) {
  // Find the closest match to the given difficulty and StepsType.
  int cur_difference = -1;
  int& selection = selection_[pn];
  int i = 0;
  for (Steps* s : steps_) {
    // If the current steps are listed, use them.
    if (GAMESTATE->cur_steps_[pn] == s) {
      selection = i;
      break;
    }

    if (d != Difficulty_Invalid) {
      int difficulty_difference = std::abs(s->GetDifficulty() - d);
      int steps_type_difference = 0;
      if (GAMESTATE->preferred_steps_type_ != StepsType_Invalid) {
        steps_type_difference =
            std::abs(s->steps_type_ - GAMESTATE->preferred_steps_type_);
      }
      int total_difference =
          steps_type_difference * NUM_Difficulty + difficulty_difference;

      if (cur_difference == -1 || total_difference < cur_difference) {
        selection = i;
        cur_difference = total_difference;
      }
    }
    i += 1;
  }

  rage_clamp(selection, 0, steps_.size() - 1);
}

void ScreenSelectMusic::SwitchPlayerCourseDifficulty(
    PlayerNumber pn, Difficulty d) {
  // Find the closest match to the given difficulty.
  int cur_difference = -1;
  int& selection = selection_[pn];
  int i = 0;
  for (Trail* t : trails_) {
    // If the current trail is listed, use it.
    if (GAMESTATE->cur_trail_[pn] == trails_[i]) {
      selection = i;
      break;
    }

    if (d != Difficulty_Invalid &&
        GAMESTATE->preferred_steps_type_ != StepsType_Invalid) {
      int difficulty_difference = std::abs(t->course_difficulty_ - d);
      int steps_type_difference =
          std::abs(t->steps_type_ - GAMESTATE->preferred_steps_type_);
      int total_difference =
          steps_type_difference * NUM_CourseDifficulty + difficulty_difference;

      if (cur_difference == -1 || total_difference < cur_difference) {
        selection = i;
        cur_difference = total_difference;
      }
    }
    i += 1;
  }

  rage_clamp(selection, 0, trails_.size() - 1);
}

void ScreenSelectMusic::SwitchToDifficulty(Difficulty d) {
  if (!GAMESTATE->cur_course_) {
    FOREACH_HumanPlayer(pn) { SwitchPlayerStepDifficulty(pn, d); }
  } else {
    FOREACH_HumanPlayer(pn) { SwitchPlayerCourseDifficulty(pn, d); }
  }

  if (GAMESTATE->DifficultiesLocked()) {
    FOREACH_HumanPlayer(p) selection_[p] =
        selection_[GAMESTATE->GetMasterPlayerNumber()];
  }
}

void ScreenSelectMusic::SwitchToPreferredDifficulty() {
  if (!GAMESTATE->cur_course_) {
    FOREACH_HumanPlayer(pn) {
      SwitchPlayerStepDifficulty(pn, GAMESTATE->preferred_difficulty_[pn]);
    }
  } else {
    FOREACH_HumanPlayer(pn) {
      SwitchPlayerCourseDifficulty(
          pn, GAMESTATE->preferred_course_difficulty_[pn]);
    }
  }

  if (GAMESTATE->DifficultiesLocked()) {
    FOREACH_HumanPlayer(p) selection_[p] =
        selection_[GAMESTATE->GetMasterPlayerNumber()];
  }
}

// NOTE: This could a be a bit more robust than just looking at the extension,
// but it's good enough for now.
static bool IsVideoFile(const std::string& path) {
  const std::string extension = GetExtension(path);
  return extension == "mp4" || extension == "avi" || extension == "mov" ||
         extension == "mkv" || extension == "mpg";
}

void ScreenSelectMusic::AfterMusicChange() {
  if (!music_wheel_.IsRouletting()) {
    menu_timer_->Stall();
  }

  Song* song = music_wheel_.GetSelectedSong();
  GAMESTATE->cur_song_.Set(song);
  if (song) {
    GAMESTATE->preferred_song_ = song;
  }

  Course* p_course = music_wheel_.GetSelectedCourse();
  GAMESTATE->cur_course_.Set(p_course);
  if (p_course) {
    GAMESTATE->preferred_course_ = p_course;
  }

  steps_.clear();
  trails_.clear();

  banner_.SetMovingFast(!!music_wheel_.IsMoving());

  if (SAMPLE_MUSIC_PREVIEW_MODE != SampleMusicPreviewMode_LastSong) {
    sample_music_to_play_ = "";
  }
  sample_music_timing_data_ = nullptr;
  g_sCDTitlePath = "";
  g_sBannerPath = "";
  g_bWantFallbackCdTitle = false;
  bool want_banner = true;

  static SortOrder last_sort_order = SortOrder_Invalid;
  if (GAMESTATE->sort_order_ != last_sort_order) {
    // Reload to let Lua metrics have a chance to change the help text.
    last_sort_order = GAMESTATE->sort_order_;
  }

  WheelItemDataType wtype = music_wheel_.GetSelectedType();
  SampleMusicPreviewMode pmode;
  switch (wtype) {
    case WheelItemDataType_Section:
    case WheelItemDataType_ParentSection:
    case WheelItemDataType_Sort:
    case WheelItemDataType_Roulette:
    case WheelItemDataType_Random:
    case WheelItemDataType_Custom:
      FOREACH_PlayerNumber(p) selection_[p] = -1;

      g_sCDTitlePath = "";  // none

      if (SAMPLE_MUSIC_PREVIEW_MODE == SampleMusicPreviewMode_LastSong) {
        // HACK: Make random music work in LastSong mode. -aj
        if (sample_music_to_play_ == random_music_path_) {
          sample_start_seconds_ = 0;
          sample_length_seconds_ = -1;
        }
      } else {
        sample_start_seconds_ = 0;
        sample_length_seconds_ = -1;
      }

      switch (wtype) {
        case WheelItemDataType_Section:
        case WheelItemDataType_ParentSection:
          // reduce scope
          {
            SortOrder cur_sort = GAMESTATE->sort_order_;
            if (wtype == WheelItemDataType_ParentSection &&
                cur_sort == SORT_SERIES) {
              g_sBannerPath = SONGMAN->GetSeriesBannerPath(
                  music_wheel_.GetSelectedSection());
            } else if (cur_sort == SORT_GROUP || cur_sort == SORT_SERIES) {
              g_sBannerPath = SONGMAN->GetSongGroupBannerPath(
                  music_wheel_.GetSelectedSection());
            } else {
              want_banner = false;  // we load it ourself
              banner_.LoadFromSortOrder(cur_sort);
            }

            if (SAMPLE_MUSIC_PREVIEW_MODE != SampleMusicPreviewMode_LastSong) {
              sample_music_to_play_ = section_music_path_;
            }
          }
          break;
        case WheelItemDataType_Sort:
          want_banner = false;  // we load it ourself
          banner_.LoadMode();
          if (SAMPLE_MUSIC_PREVIEW_MODE != SampleMusicPreviewMode_LastSong) {
            sample_music_to_play_ = sort_music_path_;
          }
          break;
        case WheelItemDataType_Roulette:
          want_banner = false;  // we load it ourself
          banner_.LoadRoulette();
          if (SAMPLE_MUSIC_PREVIEW_MODE != SampleMusicPreviewMode_LastSong) {
            sample_music_to_play_ = roulette_music_path_;
          }
          break;
        case WheelItemDataType_Random:
          want_banner = false;  // we load it ourself
          banner_.LoadRandom();
          // if( SAMPLE_MUSIC_PREVIEW_MODE != SampleMusicPreviewMode_LastSong )
          sample_music_to_play_ = random_music_path_;
          break;
        case WheelItemDataType_Custom: {
          want_banner = false;  // we load it ourself
          std::string banner_name =
              GetMusicWheel()
                  ->GetCurWheelItemData(GetMusicWheel()->GetCurrentIndex())
                  ->action_->name_.c_str();
          banner_.LoadCustom(banner_name);
          if (SAMPLE_MUSIC_PREVIEW_MODE != SampleMusicPreviewMode_LastSong) {
            sample_music_to_play_ = section_music_path_;
          }
        } break;
        default:
          FAIL_M(ssprintf("Invalid WheelItemDataType: %i", wtype));
      }
      // override this if the sample music mode wants to.
      /*
      if(SAMPLE_MUSIC_PREVIEW_MODE == SampleMusicPreviewMode_LastSong)
      {
              m_sSampleMusicToPlay = pSong->GetMusicPath();
              m_pSampleMusicTimingData = &pSong->m_SongTiming;
              m_fSampleStartSeconds = pSong->m_fMusicSampleStartSeconds;
              m_fSampleLengthSeconds = pSong->m_fMusicSampleLengthSeconds;
      }
      */
      break;
    case WheelItemDataType_Song:
    case WheelItemDataType_Portal:
      // check SampleMusicPreviewMode here.
      pmode = SAMPLE_MUSIC_PREVIEW_MODE;
      switch (pmode) {
        case SampleMusicPreviewMode_ScreenMusic:
          // play the screen music
          sample_music_to_play_ = loop_music_path_;
          sample_start_seconds_ = 0;
          sample_length_seconds_ = -1;
          break;
        case SampleMusicPreviewMode_StartToPreview:
          // we want to load the sample music, but we don't want to
          // actually play it. fall through. -aj
        case SampleMusicPreviewMode_Normal:
        case SampleMusicPreviewMode_LastSong:
          // play the sample music
          sample_music_to_play_ = song->GetPreviewMusicPath();
          if (!sample_music_to_play_.empty() &&
              ActorUtil::GetFileType(sample_music_to_play_) != FT_Sound) {
            LuaHelpers::ReportScriptErrorFmt(
                "Music file %s for song is not a sound file, ignoring.",
                sample_music_to_play_.c_str());
            sample_music_to_play_ = "";
          }
          sample_music_timing_data_ = &song->song_timing_;
          sample_start_seconds_ = song->GetPreviewStartSeconds();
          sample_length_seconds_ = song->music_sample_length_seconds_;
          break;
        default:
          FAIL_M(ssprintf("Invalid preview mode: %i", pmode));
      }

      SongUtil::GetPlayableSteps(song, steps_);
      if (steps_.empty()) {
        // LuaHelpers::ReportScriptError("GetPlayableSteps returned nothing.");
      }

      if (PREFSMAN->show_banners_) {
        g_sBannerPath = song->GetBannerPath();
      }

      g_sCDTitlePath = song->GetCDTitlePath();
      g_bWantFallbackCdTitle = true;

      if (GAMESTATE->sort_order_ == SORT_METER) {
        for (int i = steps_.size() - 1; i >= 0; i--) {
          if (steps_[i]->GetMeter() ==
              StringToInt(GAMESTATE->last_open_section_)) {
            SwitchToDifficulty(steps_[i]->GetDifficulty());
            break;
          }
        }
      } else {
        SwitchToPreferredDifficulty();
      }
      break;

    case WheelItemDataType_Course: {
      const Course* l_course = music_wheel_.GetSelectedCourse();
      const Style* style = nullptr;
      if (CommonMetrics::AUTO_SET_STYLE) {
        style = p_course->GetCourseStyle(
            GAMESTATE->cur_game_, GAMESTATE->GetNumPlayersEnabled());
        if (style == nullptr) {
          l_course->GetAllTrails(trails_);
          std::vector<Trail*>::iterator tra = trails_.begin();
          const Game* cur_game = GAMESTATE->GetCurrentGame();
          int num_players = GAMESTATE->GetNumPlayersEnabled();
          while (tra != trails_.end()) {
            if (GAMEMAN->GetFirstCompatibleStyle(
                    cur_game, num_players, (*tra)->steps_type_) == nullptr) {
              tra = trails_.erase(tra);
            } else {
              ++tra;
            }
          }
        } else {
          l_course->GetTrails(trails_, style->steps_type_);
        }
      } else {
        style = GAMESTATE->GetCurrentStyle(PLAYER_INVALID);
        l_course->GetTrails(trails_, style->steps_type_);
      }

      sample_music_to_play_ = course_music_path_;
      sample_start_seconds_ = 0;
      sample_length_seconds_ = -1;

      g_sBannerPath = l_course->GetBannerPath();
      if (g_sBannerPath.empty()) {
        banner_.LoadFallback();
      }

      SwitchToPreferredDifficulty();
      break;
    }
    default:
      FAIL_M(ssprintf("Invalid WheelItemDataType: %i", wtype));
  }

  spr_cd_title_front_.UnloadTexture();
  spr_cd_title_back_.UnloadTexture();

  // Cancel any previous, incomplete requests for song assets,
  // since we need new ones.
  background_loader_.Abort();

  g_bCDTitleWaiting = false;
  if (!g_sCDTitlePath.empty() || g_bWantFallbackCdTitle) {
    LOG->Trace("cache \"%s\"", g_sCDTitlePath.c_str());
    background_loader_.CacheFile(g_sCDTitlePath);  // empty OK
    g_bCDTitleWaiting = true;
  }

  g_bBannerWaiting = false;
  if (want_banner) {
    LOG->Trace("LoadFromCachedBanner(%s)", g_sBannerPath.c_str());
    // TODO: We should probably have some fallback banner for videos, but for
    // now we can just load the video file directly. This is to try an address
    // some issues with the video banners potentially crashing the game but
    // needs some more investigation.
    if (IsVideoFile(g_sBannerPath)) {
      // Directly load the video file.
      banner_.LoadFromCachedBanner(g_sBannerPath);
      g_bBannerWaiting = false;
    } else {
      if (banner_.LoadFromCachedBanner(g_sBannerPath)) {
        /* If the high-res banner is already loaded, just delay before
         * loading it, so the low-res one has time to fade in. */
        if (!TEXTUREMAN->IsTextureRegistered(
                Sprite::SongBannerTexture(g_sBannerPath))) {
          background_loader_.CacheFile(g_sBannerPath);
        }

        g_bBannerWaiting = true;
      }
    }
  }

  // Don't stop music if it's already playing the right file.
  g_bSampleMusicWaiting = false;
  if (!music_wheel_.IsRouletting() &&
      SOUND->GetMusicPath() != sample_music_to_play_) {
    SOUND->StopMusic();
    // some SampleMusicPreviewModes don't want the sample music immediately.
    if (SAMPLE_MUSIC_PREVIEW_MODE != SampleMusicPreviewMode_StartToPreview) {
      if (!sample_music_to_play_.empty()) {
        g_bSampleMusicWaiting = true;
      }
    }
  }

  g_StartedLoadingAt.Touch();

  std::vector<PlayerNumber> vpns;
  FOREACH_HumanPlayer(p) vpns.push_back(p);

  AfterStepsOrTrailChange(vpns);
}

void ScreenSelectMusic::OpenOptionsList(PlayerNumber pn) {
  if (pn != PLAYER_INVALID) {
    music_wheel_.Move(0);
    options_list_[pn].Open();
  }
}

void ScreenSelectMusic::OnConfirmSongDeletion() {
  Song* deleted_song = song_awaiting_deletion_confirmation_;
  if (!deleted_song) {
    LOG->Warn(
        "Attempted to delete a null song "
        "(ScreenSelectMusic::OnConfirmSongDeletion)");
    return;
  }
  // ensure Stepmania is configured to allow song deletion
  if (!PREFSMAN->allow_song_deletion_.Get()) {
    LOG->Warn(
        "Attemped to delete a song but AllowSongDeletion was set to false "
        "(ScreenSelectMusic::OnConfirmSongDeletion)");
    return;
  }

  std::string delete_dir = deleted_song->GetSongDir();
  // flush the deleted song from any caches
  SONGMAN->UnlistSong(deleted_song);
  // refresh the song list
  music_wheel_.ReloadSongList();
  LOG->Trace("Deleting song: '%s'\n", delete_dir.c_str());
  // delete the song directory from disk
  FILEMAN->DeleteRecursive(delete_dir);

  song_awaiting_deletion_confirmation_ = nullptr;
}

bool ScreenSelectMusic::can_open_options_list(PlayerNumber pn) {
  if (!USE_OPTIONS_LIST) {
    return false;
  }
  if (pn >= NUM_PLAYERS) {
    return false;
  }
  if (options_list_[pn].IsOpened()) {
    return false;
  }
  return true;
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the ScreenSelectMusic. */
class LunaScreenSelectMusic : public Luna<ScreenSelectMusic> {
 public:
  static int GetGoToOptions(T* p, lua_State* L) {
    lua_pushboolean(L, p->GetGoToOptions());
    return 1;
  }
  static int GetMusicWheel(T* p, lua_State* L) {
    p->GetMusicWheel()->PushSelf(L);
    return 1;
  }
  static int OpenOptionsList(T* p, lua_State* L) {
    PlayerNumber pn = Enum::Check<PlayerNumber>(L, 1);
    if (p->can_open_options_list(pn)) {
      p->OpenOptionsList(pn);
    }
    COMMON_RETURN_SELF;
  }
  static int CanOpenOptionsList(T* p, lua_State* L) {
    PlayerNumber pn = Enum::Check<PlayerNumber>(L, 1);
    lua_pushboolean(L, p->can_open_options_list(pn));
    return 1;
  }

  LunaScreenSelectMusic() {
    ADD_METHOD(GetGoToOptions);
    ADD_METHOD(GetMusicWheel);
    ADD_METHOD(OpenOptionsList);
    ADD_METHOD(CanOpenOptionsList);
  }
};

LUA_REGISTER_DERIVED_CLASS(ScreenSelectMusic, ScreenWithMenuElements)
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
