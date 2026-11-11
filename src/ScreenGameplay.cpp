#include "ScreenGameplay.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "ActiveAttackList.h"
#include "Actor.h"
#include "ActorUtil.h"
#include "AdjustSync.h"
#include "ArrowEffects.h"
#include "Attack.h"
#include "Background.h"
#include "BitmapText.h"
#include "CombinedLifeMeterShared.h"
#include "CombinedLifeMeterTug.h"
#include "CommonMetrics.h"
#include "Course.h"
#include "DancingCharacters.h"
#include "Difficulty.h"
#include "EnumHelper.h"
#include "Foreground.h"
#include "Game.h"
#include "GameConstantsAndTypes.h"
#include "GameInput.h"
#include "GameManager.h"
#include "GamePreferences.h"
#include "GameSoundManager.h"
#include "GameState.h"
#include "GameplayHelpers.h"
#include "InputFilter.h"
#include "InputMapper.h"
#include "LifeMeter.h"
#include "LifeMeterBar.h"
#include "LightsManager.h"
#include "LuaManager.h"
#include "LyricsLoader.h"
#include "MemoryCardManager.h"
#include "MessageManager.h"
#include "ModsGroup.h"
#include "NoteData.h"
#include "NoteDataUtil.h"
#include "NoteDataWithScoring.h"
#include "NoteTypes.h"
#include "Player.h"
#include "PlayerAI.h"  // for NUM_SKILL_LEVELS
#include "PlayerNumber.h"
#include "PlayerOptions.h"
#include "PlayerState.h"
#include "Preference.h"
#include "PrefsManager.h"
#include "Profile.h"  // for replay data stuff
#include "ProfileManager.h"
#include "RageDisplay.h"
#include "RageInputDevice.h"
#include "RageLog.h"
#include "RageSoundManager.h"
#include "RageSoundReader.h"
#include "RageThreads.h"
#include "RageTimer.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "RageUtil/Regex.h"
#include "ScoreDisplayLifeTime.h"
#include "ScoreDisplayNormal.h"
#include "ScoreDisplayOni.h"
#include "ScoreDisplayPercentage.h"
#include "ScoreDisplayRave.h"
#include "ScoreKeeperNormal.h"
#include "ScoreKeeperRave.h"
#include "ScreenDimensions.h"
#include "ScreenManager.h"
#include "ScreenMessage.h"
#include "ScreenSaveSync.h"
#include "ScreenWithMenuElements.h"
#include "Song.h"
#include "SongManager.h"
#include "SongOptions.h"
#include "SongUtil.h"
#include "StatsManager.h"
#include "StdString.h"
#include "Steps.h"
#include "StepsDisplay.h"
#include "StepsUtil.h"
#include "Style.h"
#include "ThemeManager.h"
#include "ThemeMetric.h"
#include "Trail.h"
#include "UnlockManager.h"
#include "XmlFile.h"
#include "XmlFileUtil.h"
#include "global.h"

// Defines
#define SHOW_LIFE_METER_FOR_DISABLED_PLAYERS \
  THEME->GetMetricB(name_, "ShowLifeMeterForDisabledPlayers")
#define SHOW_SCORE_IN_RAVE THEME->GetMetricB(name_, "ShowScoreInRave")
#define SONG_POSITION_METER_WIDTH \
  THEME->GetMetricF(name_, "SongPositionMeterWidth")
#define STOP_COURSE_EARLY \
  THEME->GetMetricB(      \
      name_, "StopCourseEarly")  // evaluate this every time it's used

static ThemeMetric<float> INITIAL_BACKGROUND_BRIGHTNESS(
    "ScreenGameplay", "InitialBackgroundBrightness");
static ThemeMetric<float> SECONDS_BETWEEN_COMMENTS(
    "ScreenGameplay", "SecondsBetweenComments");
static ThemeMetric<std::string> SCORE_KEEPER_CLASS(
    "ScreenGameplay", "ScoreKeeperClass");
static ThemeMetric<bool> FORCE_IMMEDIATE_FAIL_FOR_BATTERY(
    "ScreenGameplay", "ForceImmediateFailForBattery");

AutoScreenMessage(SM_PlayGo);

// received while STATE_DANCING
AutoScreenMessage(SM_LoadNextSong);
AutoScreenMessage(SM_StartLoadingNextSong);

// received while STATE_OUTRO
AutoScreenMessage(SM_DoPrevScreen);
AutoScreenMessage(SM_DoNextScreen);

static Preference<bool> center1_player("Center1Player", false);
static Preference<bool> show_lyrics("ShowLyrics", true);
static Preference<bool> easter_eggs("EasterEggs", true);

PlayerInfo::PlayerInfo()
    : pn_(kPlayerInvalid),
      is_dummy_(false),
      dummy_index_(0),
      player_enabled_(false),
      player_state_dummy_(),
      player_stage_stats_dummy_(),
      sound_effect_control_(),
      steps_queue_(),
      modifiers_queue_(),
      life_meter_(nullptr),
      text_course_song_number_(nullptr),
      text_steps_description_(nullptr),
      primary_score_display_(nullptr),
      secondary_score_display_(nullptr),
      primary_score_keeper_(nullptr),
      secondary_score_keeper_(nullptr),
      text_player_options_(nullptr),
      active_attack_list_(nullptr),
      note_data_(),
      player_(nullptr),
      steps_display_(nullptr),
      spr_oni_game_over_() {}

void PlayerInfo::Load(PlayerNumber pn, bool show_note_field) {
  pn_ = pn;
  player_enabled_ = IsEnabled();
  is_dummy_ = false;
  life_meter_ = nullptr;
  text_course_song_number_ = nullptr;
  text_steps_description_ = nullptr;

  PlayMode mode = GAMESTATE->play_mode_;
  switch (mode) {
    case PLAY_MODE_REGULAR:
    case PLAY_MODE_NONSTOP:
    case PLAY_MODE_BATTLE:
    case PLAY_MODE_RAVE:
      if (PREFSMAN->percentage_scoring_) {
        primary_score_display_ = new ScoreDisplayPercentage;
      } else {
        primary_score_display_ = new ScoreDisplayNormal;
      }
      break;
    case PLAY_MODE_ONI:
    case PLAY_MODE_ENDLESS:
      if (GAMESTATE->player_state_[pn]->player_options_.GetStage().life_type_ ==
          LifeType_Time) {
        primary_score_display_ = new ScoreDisplayLifeTime;
      } else {
        primary_score_display_ = new ScoreDisplayOni;
      }
      break;
    default:
      FAIL_M(ssprintf("Invalid PlayMode: %i", mode));
  }

  PlayerState* const player_state = GetPlayerState();
  PlayerStageStats* const player_stage_stats = GetPlayerStageStats();

  if (primary_score_display_) {
    primary_score_display_->Init(player_state, player_stage_stats);
  }

  switch (GAMESTATE->play_mode_) {
    case PLAY_MODE_RAVE:
      secondary_score_display_ = new ScoreDisplayRave;
      secondary_score_display_->SetName("ScoreDisplayRave");
    default:
      break;
  }

  if (secondary_score_display_) {
    secondary_score_display_->Init(player_state, player_stage_stats);
  }

  std::string primary_score_keeper_class = SCORE_KEEPER_CLASS;
  const Style* style = GAMESTATE->GetCurrentStyle(pn);
  if (style != nullptr &&
      style->style_type_ == StyleType_TwoPlayersSharedSides) {
    // Shared-sides keeps per-player scoring on normal scorekeepers.
    primary_score_keeper_class = "ScoreKeeperNormal";
  }

  primary_score_keeper_ = ScoreKeeper::MakeScoreKeeper(
      primary_score_keeper_class, player_state, player_stage_stats);

  switch (GAMESTATE->play_mode_) {
    case PLAY_MODE_RAVE:
      secondary_score_keeper_ =
          new ScoreKeeperRave(player_state, player_stage_stats);
    default:
      break;
  }

  text_player_options_ = nullptr;
  active_attack_list_ = nullptr;
  player_ = new Player(note_data_, show_note_field);
  steps_display_ = nullptr;
}

PlayerInfo::~PlayerInfo() {
  RageUtil::SafeDelete(life_meter_);
  RageUtil::SafeDelete(text_course_song_number_);
  RageUtil::SafeDelete(text_steps_description_);
  RageUtil::SafeDelete(primary_score_display_);
  RageUtil::SafeDelete(secondary_score_display_);
  RageUtil::SafeDelete(primary_score_keeper_);
  RageUtil::SafeDelete(secondary_score_keeper_);
  RageUtil::SafeDelete(text_player_options_);
  RageUtil::SafeDelete(active_attack_list_);
  RageUtil::SafeDelete(player_);
  RageUtil::SafeDelete(steps_display_);
}

void PlayerInfo::ShowOniGameOver() { spr_oni_game_over_->PlayCommand("Die"); }

PlayerState* PlayerInfo::GetPlayerState() {
  if (is_dummy_) {
    return &player_state_dummy_;
  }
  return GAMESTATE->player_state_[GetPlayerStateAndStageStatsIndex()];
}

PlayerStageStats* PlayerInfo::GetPlayerStageStats() {
  if (is_dummy_) {
    return &player_stage_stats_dummy_;
  }
  return &STATSMAN->cur_stage_stats_
              .player_[GetPlayerStateAndStageStatsIndex()];
}

bool PlayerInfo::IsEnabled() {
  if (pn_ != kPlayerInvalid) {
    return GAMESTATE->IsPlayerEnabled(pn_);
  } else if (is_dummy_) {
    return true;
  }
  FAIL_M("Invalid non-dummy player.");
}

std::vector<PlayerInfo>::iterator GetNextEnabledPlayerInfo(
    std::vector<PlayerInfo>::iterator iter, std::vector<PlayerInfo>& v) {
  for (; iter != v.end(); ++iter) {
    if (!iter->player_enabled_) {
      continue;
    }
    return iter;
  }
  return iter;
}

std::vector<PlayerInfo>::iterator GetNextEnabledPlayerInfoNotDummy(
    std::vector<PlayerInfo>::iterator iter, std::vector<PlayerInfo>& v) {
  for (; iter != v.end(); iter++) {
    if (iter->is_dummy_) {
      continue;
    }
    if (!iter->player_enabled_) {
      continue;
    }
    return iter;
  }
  return iter;
}

std::vector<PlayerInfo>::iterator GetNextEnabledPlayerNumberInfo(
    std::vector<PlayerInfo>::iterator iter, std::vector<PlayerInfo>& v) {
  for (; iter != v.end(); ++iter) {
    if (iter->is_dummy_) {
      continue;
    }
    if (!iter->player_enabled_) {
      continue;
    }
    return iter;
  }
  return iter;
}

std::vector<PlayerInfo>::iterator GetNextPlayerNumberInfo(
    std::vector<PlayerInfo>::iterator iter, std::vector<PlayerInfo>& v) {
  for (; iter != v.end(); ++iter) {
    if (iter->is_dummy_) {
      continue;
    }
    if (iter->pn_ == kPlayerInvalid) {
      continue;
    }
    return iter;
  }
  return iter;
}

std::vector<PlayerInfo>::iterator GetNextVisiblePlayerInfo(
    std::vector<PlayerInfo>::iterator iter, std::vector<PlayerInfo>& v) {
  for (; iter != v.end(); ++iter) {
    if (!iter->player_->HasVisibleParts()) {
      continue;
    }
    return iter;
  }
  return iter;
}

////////////////////////////////////////////////////////////////////////////////

ScreenGameplay::ScreenGameplay() {
  song_background_ = nullptr;
  song_foreground_ = nullptr;
  routine_shared_score_keeper_ = nullptr;
  delaying_ready_announce_ = false;
  GAMESTATE->adjust_tokens_by_song_cost_for_final_stage_check_ = false;
}

void ScreenGameplay::Init() {
  SubscribeToMessage("Judgment");

  PLAYER_TYPE.Load(name_, "PlayerType");
  PLAYER_INIT_COMMAND.Load(name_, "PlayerInitCommand");
  GIVE_UP_START_TEXT.Load(name_, "GiveUpStartText");
  GIVE_UP_BACK_TEXT.Load(name_, "GiveUpBackText");
  GIVE_UP_ABORTED_TEXT.Load(name_, "GiveUpAbortedText");
  SKIP_SONG_TEXT.Load(name_, "SkipSongText");
  GIVE_UP_SECONDS.Load(name_, "GiveUpSeconds");
  MUSIC_FADE_OUT_SECONDS.Load(name_, "MusicFadeOutSeconds");
  OUT_TRANSITION_LENGTH.Load(name_, "OutTransitionLength");
  COURSE_TRANSITION_LENGTH.Load(name_, "CourseTransitionLength");
  BEGIN_FAILED_DELAY.Load(name_, "BeginFailedDelay");
  MIN_SECONDS_TO_STEP.Load(name_, "MinSecondsToStep");
  MIN_SECONDS_TO_MUSIC.Load(name_, "MinSecondsToMusic");
  MIN_SECONDS_TO_STEP_NEXT_SONG.Load(name_, "MinSecondsToStepNextSong");
  START_GIVES_UP.Load(name_, "StartGivesUp");
  BACK_GIVES_UP.Load(name_, "BackGivesUp");
  SELECT_SKIPS_SONG.Load(name_, "SelectSkipsSong");
  GIVING_UP_GOES_TO_PREV_SCREEN.Load(name_, "GivingUpGoesToPrevScreen");
  FAIL_ON_MISS_COMBO.Load(name_, "FailOnMissCombo");
  ALLOW_CENTER_1_PLAYER.Load(name_, "AllowCenter1Player");
  // configurable:
  UNPAUSE_WITH_START.Load(name_, "UnpauseWithStart");
  SURVIVAL_MOD_OVERRIDE.Load(name_, "SurvivalModOverride");

  // Default values.  The theme can set its own through the Lua interface.
  haste_turning_points_.clear();
  haste_turning_points_.push_back(-1);
  haste_turning_points_.push_back(0);
  haste_turning_points_.push_back(0.3f);
  haste_turning_points_.push_back(1);
  haste_add_amounts_.clear();
  haste_add_amounts_.push_back(-0.5f);
  haste_add_amounts_.push_back(0);
  haste_add_amounts_.push_back(0.2f);
  haste_add_amounts_.push_back(0.5f);
  haste_time_between_updates_ = 4;
  haste_life_switch_point_ = 0.5f;
  curr_haste_rate_ = 1;  // Should this be in BeginSong?  Not sure whether it
                         // should carry over between songs.

  if (UseSongBackgroundAndForeground()) {
    song_background_ = new Background;
    song_foreground_ = new Foreground;
  }

  ScreenWithMenuElements::Init();

  this->FillPlayerInfo(player_info_);

  const Style* master_style =
      GAMESTATE->GetCurrentStyle(GAMESTATE->GetMasterPlayerNumber());
  const bool shared_sides_style =
      master_style != nullptr &&
      master_style->style_type_ == StyleType_TwoPlayersSharedSides;
  if (shared_sides_style) {
    const PlayerNumber master = GAMESTATE->GetMasterPlayerNumber();
    PlayerInfo& master_info = player_info_[master];
    routine_shared_score_keeper_ = ScoreKeeper::MakeScoreKeeper(
        "ScoreKeeperShared", master_info.GetPlayerState(),
        &STATSMAN->cur_stage_stats_.routine_player_);

    FOREACH_EnabledPlayerNumberInfo(player_info_, pi) {
      if (pi->secondary_score_keeper_ != nullptr) {
        RageUtil::SafeDelete(pi->secondary_score_keeper_);
      }
      pi->secondary_score_keeper_ = routine_shared_score_keeper_;
    }
  }

  {
    ASSERT_M(
        !player_info_.empty(), "player_info_ must be filled by FillPlayerInfo");

    int num_enabled_players = 0;
    FOREACH_EnabledPlayerInfo(player_info_, pi)++ num_enabled_players;

    /* If this is 0, we have no active players and havn't been initialized
     * correctly. */
    ASSERT(num_enabled_players > 0);
  }

  /* Pause MEMCARDMAN.  If a memory card is removed, we don't want to interrupt
   * the player by making a noise until the game finishes. */
  if (!GAMESTATE->demonstration_or_jukebox_) {
    MEMCARDMAN->PauseMountingThread();
  }

  sound_music_ = nullptr;
  set_paused_internal(false);

  combined_life_meter_ = nullptr;

  if (GAMESTATE->cur_song_ == nullptr && GAMESTATE->cur_course_ == nullptr) {
    return;  // ScreenDemonstration will move us to the next screen.  We just
             // need to survive for one update without crashing.
  }

  /* Save settings to the profile now.  Don't do this on extra stages, since the
   * user doesn't have full control; saving would force profiles to
   * Difficulty_Hard and save over their default modifiers every time someone
   * got an extra stage. Do this before course modifiers are set up. */
  if (!GAMESTATE->IsAnExtraStage()) {
    FOREACH_HumanPlayer(pn) GAMESTATE->SaveCurrentSettingsToProfile(pn);
  }

  /* Called once per stage (single song or single course). */
  GAMESTATE->BeginStage();

  int player = 1;
  FOREACH_EnabledPlayerInfo(player_info_, pi) {
    unsigned int count = pi->steps_queue_.size();

    for (unsigned int i = 0; i < count; i++) {
      Steps* cur_steps = pi->steps_queue_[i];
      if (cur_steps->IsNoteDataEmpty()) {
        if (cur_steps->GetNoteDataFromSimfile()) {
          LOG->Trace("Notes should be loaded for player %d", player);
        } else {
          LOG->Trace("Error loading notes for player %d", player);
        }
      }
    }
    player++;
  }

  if (!GAMESTATE->IsCourseMode() && !GAMESTATE->demonstration_or_jukebox_) {
    // fill in difficulty of CPU players with that of the first human player
    // this should not need to worry about step content.
    FOREACH_PotentialCpuPlayer(p) {
      PlayerNumber human_pn = GAMESTATE->GetFirstHumanPlayer();
      GAMESTATE->cur_steps_[p].Set(GAMESTATE->cur_steps_[human_pn]);
      if (GAMESTATE->GetCurrentGame()->players_have_separate_styles_) {
        GAMESTATE->SetCurrentStyle(GAMESTATE->GetCurrentStyle(human_pn), p);
      }
    }

    FOREACH_EnabledPlayer(p) ASSERT(GAMESTATE->cur_steps_[p].Get() != nullptr);
  }

  /* Increment the course play count. */
  if (GAMESTATE->IsCourseMode() && !GAMESTATE->demonstration_or_jukebox_) {
    FOREACH_EnabledPlayer(p) PROFILEMAN->IncrementCoursePlayCount(
        GAMESTATE->cur_course_, GAMESTATE->cur_trail_[p], p);
  }

  STATSMAN->cur_stage_stats_.stage_ = GAMESTATE->GetCurrentStage();
  STATSMAN->cur_stage_stats_.stage_index_ = GAMESTATE->current_stage_index_;
  STATSMAN->cur_stage_stats_.play_mode_ = GAMESTATE->play_mode_;
  FOREACH_PlayerNumber(pn) {
    STATSMAN->cur_stage_stats_.player_[pn].style_ =
        GAMESTATE->GetCurrentStyle(pn);
  }
  if (shared_sides_style) {
    STATSMAN->cur_stage_stats_.routine_player_.style_ = master_style;
  }

  /* Record combo rollover. */
  FOREACH_EnabledPlayerInfoNotDummy(player_info_, pi) pi->GetPlayerStageStats()
      ->UpdateComboList(0, true);

  dancing_state_ = STATE_INTRO;

  // Set this in LoadNextSong()
  // m_fTimeLeftBeforeDancingComment = SECONDS_BETWEEN_COMMENTS;

  zero_delta_on_next_update_ = false;

  if (song_background_) {
    song_background_->SetName("SongBackground");
    song_background_->SetDrawOrder(kDrawOrderBeforeEverything);
    ActorUtil::LoadAllCommands(*song_background_, name_);
    this->AddChild(song_background_);
  }

  if (song_foreground_) {
    song_foreground_->SetName("SongForeground");
    song_foreground_->SetDrawOrder(
        kDrawOrderOverlay + 1);  // on top of the overlay, but under transitions
    ActorUtil::LoadAllCommands(*song_foreground_, name_);
    this->AddChild(song_foreground_);
  }

  if (PREFSMAN->show_beginner_helper_) {
    beginner_helper_.SetDrawOrder(kDrawOrderBeforeEverything);
    beginner_helper_.SetXY(SCREEN_CENTER_X, SCREEN_CENTER_Y);
    this->AddChild(&beginner_helper_);
  }

  if (!GAMESTATE
           ->demonstration_or_jukebox_)  // only load if we're going to use it
  {
    toasty_.Load(THEME->GetPathB(name_, "toasty"));
    this->AddChild(&toasty_);
  }

  std::vector<NotefieldMargins> margins = GetNotefieldMargins();

  float left_edge[kNumPlayers] = {0.0f, SCREEN_WIDTH / 2.0f};
  FOREACH_EnabledPlayerInfo(player_info_, pi) {
    std::string name = ssprintf("Player%s", pi->GetName().c_str());
    pi->player_->SetName(name);

    const Style* style = GAMESTATE->GetCurrentStyle(pi->pn_);
    float style_width = style->GetWidth(pi->pn_);
    float edge = left_edge[pi->pn_];
    float screen_space;
    float field_space;
    float left_marge;
    float right_marge;
#define CENTER_PLAYER_BLOCK                                \
  {                                                        \
    edge = 0.0f;                                           \
    screen_space = SCREEN_WIDTH;                           \
    left_marge = margins[PLAYER_1].left_;                  \
    right_marge = margins[PLAYER_2].right_;                \
    field_space = screen_space - left_marge - right_marge; \
  }
    // If pi->m_pn is set, then the player will be visible.  If not, then it's
    // not visible and don't bother setting its position.
    screen_space = SCREEN_WIDTH / 2.0f;
    left_marge = margins[pi->pn_].left_;
    right_marge = margins[pi->pn_].right_;
    field_space = screen_space - left_marge - right_marge;
    if (Center1Player() ||
        style->style_type_ == StyleType_TwoPlayersSharedSides ||
        (style_width > field_space && GAMESTATE->GetNumPlayersEnabled() == 1 &&
         (bool)ALLOW_CENTER_1_PLAYER)) {
      CENTER_PLAYER_BLOCK
    }
#undef CENTER_PLAYER_BLOCK
    float player_x = edge + left_marge + (field_space / 2.0f);
    float field_zoom = field_space / style_width;
    /*
    LuaHelpers::ReportScriptErrorFmt("Positioning player %d at %.0f:  "
            "screen_space %.0f, left_edge %.0f, field_space %.0f, left_marge
    %.0f," " right_marge %.0f, style_width %.0f, field_zoom %.2f.", pi->m_pn+1,
    player_x, screen_space, left_edge[pi->m_pn], field_space, left_marge,
    right_marge, style_width, field_zoom);
    */
    pi->GetPlayerState()->notefield_zoom_ = std::min(1.0f, field_zoom);

    pi->player_->SetX(player_x);
    pi->player_->RunCommands(PLAYER_INIT_COMMAND);
    // ActorUtil::LoadAllCommands(pi->m_pPlayer, m_sName);
    this->AddChild(pi->player_);
    pi->player_->PlayCommand("On");
  }

  FOREACH_EnabledPlayerInfoNotDummy(player_info_, pi) {
    if (pi->player_->HasVisibleParts()) {
      pi->spr_oni_game_over_.Load(THEME->GetPathG(name_, "oni gameover"));
      pi->spr_oni_game_over_->SetName(
          ssprintf("OniGameOver%s", pi->GetName().c_str()));
      LOAD_ALL_COMMANDS_AND_SET_XY(pi->spr_oni_game_over_);
      this->AddChild(pi->spr_oni_game_over_);
    }
  }

  next_song_.Load(THEME->GetPathB(name_, "next course song"));
  next_song_.SetDrawOrder(kDrawOrderTransitions - 1);
  this->AddChild(&next_song_);

  // Add combined life meter
  switch (GAMESTATE->play_mode_) {
    case PLAY_MODE_REGULAR:
    case PLAY_MODE_ONI:
    case PLAY_MODE_NONSTOP:
    case PLAY_MODE_ENDLESS:
      if (shared_sides_style) {
        const PlayerNumber master = GAMESTATE->GetMasterPlayerNumber();
        PlayerInfo& master_info = player_info_[master];
        combined_life_meter_ = new CombinedLifeMeterShared(
            master_info.GetPlayerState(), master_info.GetPlayerStageStats(),
            master_info.GetPlayerState()
                ->player_options_.GetStage()
                .life_type_);
        combined_life_meter_->SetName("CombinedLife");
        LOAD_ALL_COMMANDS_AND_SET_XY(*combined_life_meter_);
        this->AddChild(combined_life_meter_);
      }
      break;
    case PLAY_MODE_BATTLE:
    case PLAY_MODE_RAVE:
      combined_life_meter_ = new CombinedLifeMeterTug;
      combined_life_meter_->SetName("CombinedLife");
      LOAD_ALL_COMMANDS_AND_SET_XY(*combined_life_meter_);
      this->AddChild(combined_life_meter_);
      break;
    default:
      break;
  }

  // Add individual life meter
  switch (GAMESTATE->play_mode_) {
    case PLAY_MODE_REGULAR:
    case PLAY_MODE_ONI:
    case PLAY_MODE_NONSTOP:
    case PLAY_MODE_ENDLESS:
      if (shared_sides_style) {
        FOREACH_PlayerNumberInfo(player_info_, pi) {
          pi->life_meter_ = nullptr;
        }
      } else {
        FOREACH_PlayerNumberInfo(player_info_, pi) {
          if (!GAMESTATE->IsPlayerEnabled(pi->pn_) &&
              !SHOW_LIFE_METER_FOR_DISABLED_PLAYERS) {
            continue;  // skip
          }

          pi->life_meter_ = LifeMeter::MakeLifeMeter(
              pi->GetPlayerState()->player_options_.GetStage().life_type_);
          pi->life_meter_->Load(
              pi->GetPlayerState(), pi->GetPlayerStageStats());
          pi->life_meter_->SetName(ssprintf("Life%s", pi->GetName().c_str()));
          LOAD_ALL_COMMANDS_AND_SET_XY(pi->life_meter_);
          this->AddChild(pi->life_meter_);

          // HACK: When SHOW_LIFE_METER_FOR_DISABLED_PLAYERS is enabled,
          // we don't want to have any life in the disabled player's life
          // meter. I think this only happens with LifeMeterBars, but I'm
          // not 100% sure of that. -freem
          if (!GAMESTATE->IsPlayerEnabled(pi->pn_) &&
              SHOW_LIFE_METER_FOR_DISABLED_PLAYERS) {
            if (pi->GetPlayerState()->player_options_.GetStage().life_type_ ==
                LifeType_Bar) {
              static_cast<LifeMeterBar*>(pi->life_meter_)->ChangeLife(-1.0f);
            }
          }
        }
      }
      break;
    case PLAY_MODE_BATTLE:
    case PLAY_MODE_RAVE:
    default:
      break;
  }

  FOREACH_EnabledPlayerInfo(player_info_, pi) {
    // primary score display
    if (pi->primary_score_display_) {
      pi->primary_score_display_->SetName(
          ssprintf("Score%s", pi->GetName().c_str()));
      LOAD_ALL_COMMANDS_AND_SET_XY(pi->primary_score_display_);
      if (GAMESTATE->play_mode_ != PLAY_MODE_RAVE ||
          SHOW_SCORE_IN_RAVE) { /* XXX: ugly */
        this->AddChild(pi->primary_score_display_);
      }
    }

    // secondary score display
    if (pi->secondary_score_display_) {
      pi->secondary_score_display_->SetName(
          ssprintf("SecondaryScore%s", pi->GetName().c_str()));
      LOAD_ALL_COMMANDS_AND_SET_XY(pi->secondary_score_display_);
      this->AddChild(pi->secondary_score_display_);
    }
  }

  // Add stage / SongNumber
  FOREACH_EnabledPlayerNumberInfo(player_info_, pi) {
    if (GAMESTATE->IsCourseMode()) {
      ASSERT(pi->text_course_song_number_ == nullptr);
      SONG_NUMBER_FORMAT.Load(name_, "SongNumberFormat");
      pi->text_course_song_number_ = new BitmapText;
      pi->text_course_song_number_->LoadFromFont(
          THEME->GetPathF(name_, "SongNum"));
      pi->text_course_song_number_->SetName(
          ssprintf("SongNumber%s", pi->GetName().c_str()));
      LOAD_ALL_COMMANDS_AND_SET_XY(pi->text_course_song_number_);
      pi->text_course_song_number_->SetText("");
      // pi->m_ptextCourseSongNumber->SetDiffuse( RageColor(0,0.5f,1,1) );
      // // light blue
      this->AddChild(pi->text_course_song_number_);
    }

    ASSERT(pi->text_steps_description_ == nullptr);
    pi->text_steps_description_ = new BitmapText;
    pi->text_steps_description_->LoadFromFont(
        THEME->GetPathF(name_, "StepsDescription"));
    pi->text_steps_description_->SetName(
        ssprintf("StepsDescription%s", pi->GetName().c_str()));
    LOAD_ALL_COMMANDS_AND_SET_XY(pi->text_steps_description_);
    this->AddChild(pi->text_steps_description_);

    // Player/Song options
    ASSERT(pi->text_player_options_ == nullptr);
    pi->text_player_options_ = new BitmapText;
    pi->text_player_options_->LoadFromFont(
        THEME->GetPathF(name_, "player options"));
    pi->text_player_options_->SetName(
        ssprintf("PlayerOptions%s", pi->GetName().c_str()));
    LOAD_ALL_COMMANDS_AND_SET_XY(pi->text_player_options_);
    this->AddChild(pi->text_player_options_);

    // Difficulty icon and meter
    ASSERT(pi->steps_display_ == nullptr);
    pi->steps_display_ = new StepsDisplay;
    pi->steps_display_->Load("StepsDisplayGameplay", pi->GetPlayerState());
    pi->steps_display_->SetName(
        ssprintf("StepsDisplay%s", pi->GetName().c_str()));
    PlayerNumber pn = pi->GetStepsAndTrailIndex();
    if (pn != PlayerNumber_Invalid) {
      pi->steps_display_->PlayCommand("Set" + pi->GetName());
    }
    LOAD_ALL_COMMANDS_AND_SET_XY(pi->steps_display_);
    this->AddChild(pi->steps_display_);
  }

  text_song_options_.LoadFromFont(THEME->GetPathF(name_, "song options"));
  text_song_options_.SetShadowLength(0);
  text_song_options_.SetName("SongOptions");
  LOAD_ALL_COMMANDS_AND_SET_XY(text_song_options_);
  text_song_options_.SetText(
      GAMESTATE->song_options_.GetStage().GetLocalizedString());
  this->AddChild(&text_song_options_);

  FOREACH_VisiblePlayerInfo(player_info_, pi) {
    ASSERT(pi->active_attack_list_ == nullptr);
    pi->active_attack_list_ = new ActiveAttackList;
    pi->active_attack_list_->LoadFromFont(
        THEME->GetPathF(name_, "ActiveAttackList"));
    pi->active_attack_list_->Init(pi->GetPlayerState());
    pi->active_attack_list_->SetName(
        ssprintf("ActiveAttackList%s", pi->GetName().c_str()));
    LOAD_ALL_COMMANDS_AND_SET_XY(pi->active_attack_list_);
    this->AddChild(pi->active_attack_list_);
  }

  if (show_lyrics) {
    lyric_display_.SetName("LyricDisplay");
    LOAD_ALL_COMMANDS(lyric_display_);
    this->AddChild(&lyric_display_);
  }

  if (!GAMESTATE
           ->demonstration_or_jukebox_)  // only load if we're going to use it
  {
    ready_.Load(THEME->GetPathB(name_, "ready"));
    this->AddChild(&ready_);

    go_.Load(THEME->GetPathB(name_, "go"));
    this->AddChild(&go_);

    failed_.Load(THEME->GetPathB(name_, "failed"));
    failed_.SetDrawOrder(
        kDrawOrderTransitions - 1);  // on top of everything else
    this->AddChild(&failed_);

    text_debug_.LoadFromFont(THEME->GetPathF(name_, "debug"));
    text_debug_.SetName("Debug");
    LOAD_ALL_COMMANDS_AND_SET_XY(text_debug_);
    text_debug_.SetDrawOrder(
        kDrawOrderTransitions -
        1);  // just under transitions, over the foreground
    this->AddChild(&text_debug_);

    gameplay_assist_.Init();

    if (GAMESTATE->IsAnExtraStage())  // only load if we're going to use it
    {
      text_survive_time_.LoadFromFont(THEME->GetPathF(name_, "survive time"));
      text_survive_time_.SetShadowLength(0);
      text_survive_time_.SetName("SurviveTime");
      LOAD_ALL_COMMANDS_AND_SET_XY(text_survive_time_);
      text_survive_time_.SetDrawOrder(kDrawOrderTransitions - 1);
      text_survive_time_.SetDiffuse(RageColor(1, 1, 1, 0));
      this->AddChild(&text_survive_time_);
    }
  }

  if (song_background_) {
    song_background_->Init();
  }

  FOREACH_EnabledPlayerInfo(player_info_, pi) {
    std::string type = PLAYER_TYPE;
    if (pi->is_dummy_) {
      type += "Dummy";
    }
    pi->player_->Init(
        type, pi->GetPlayerState(), pi->GetPlayerStageStats(), pi->life_meter_,
        combined_life_meter_, pi->primary_score_display_,
        pi->secondary_score_display_, pi->primary_score_keeper_,
        pi->secondary_score_keeper_);
  }

  // fill in m_apSongsQueue, m_vpStepsQueue, m_asModifiersQueue
  InitSongQueues();

  // Fill StageStats
  STATSMAN->cur_stage_stats_.possible_songs_ = songs_queue_;
  for (PlayerInfo& pi : player_info_) {
    if (pi.GetPlayerStageStats()) {
      pi.GetPlayerStageStats()->possible_steps_ = pi.steps_queue_;
    }
  }
  if (shared_sides_style) {
    const PlayerNumber master = GAMESTATE->GetMasterPlayerNumber();
    STATSMAN->cur_stage_stats_.routine_player_.possible_steps_ =
        player_info_[master].steps_queue_;
    STATSMAN->cur_stage_stats_.routine_player_.joined_ = true;
  }

  bool loaded_routine_shared_keeper = false;
  FOREACH_EnabledPlayerInfo(player_info_, pi) {
    ASSERT(!pi->steps_queue_.empty());
    if (pi->GetPlayerStageStats()) {
      pi->GetPlayerStageStats()->joined_ = true;
    }
    if (pi->primary_score_keeper_) {
      pi->primary_score_keeper_->Load(
          songs_queue_, pi->steps_queue_, pi->modifiers_queue_);
    }
    if (pi->secondary_score_keeper_) {
      if (pi->secondary_score_keeper_ == routine_shared_score_keeper_) {
        if (loaded_routine_shared_keeper) {
          continue;
        }
        loaded_routine_shared_keeper = true;
      }
      pi->secondary_score_keeper_->Load(
          songs_queue_, pi->steps_queue_, pi->modifiers_queue_);
    }
  }

  GAMESTATE->gameplay_lead_in_.Set(true);

  /* LoadNextSong first, since that positions some elements which need to be
   * positioned before we TweenOnScreen. */
  LoadNextSong();

  give_up_timer_.SetZero();
  skip_song_timer_.SetZero();
  gave_up_ = false;
  skipped_song_ = false;
}

bool ScreenGameplay::Center1Player() const {
  /* Perhaps this should be handled better by defining a new
   * StyleType for ONE_PLAYER_ONE_CREDIT_AND_ONE_COMPUTER,
   * but for now just ignore Center1Player when it's Battle or Rave
   * Mode. This doesn't begin to address two-player solo (6 arrows)
   *
   * If this check is modified, also modify the center check in
   * ScreenEdit::Init().
   */
  return center1_player && (bool)ALLOW_CENTER_1_PLAYER &&
         GAMESTATE->play_mode_ != PLAY_MODE_BATTLE &&
         GAMESTATE->play_mode_ != PLAY_MODE_RAVE &&
         GAMESTATE->GetCurrentStyle(kPlayerInvalid)->style_type_ ==
             StyleType_OnePlayerOneSide;
}

bool ScreenGameplay::MenuRestart(const InputEventPlus& input) {
  if (IsTransitioning()) {
    return false;
  }

  SCREENMAN->GetTopScreen()->SetPrevScreenName("ScreenGameplay");
  BeginBackingOutFromGameplay();
  return true;
}

// fill in m_apSongsQueue, m_vpStepsQueue, m_asModifiersQueue
void ScreenGameplay::InitSongQueues() {
  if (GAMESTATE->IsCourseMode()) {
    Course* course = GAMESTATE->cur_course_;
    ASSERT(course != nullptr);

    songs_queue_.clear();
    PlayerNumber master = GAMESTATE->GetMasterPlayerNumber();
    Trail* p_trail = GAMESTATE->cur_trail_[master];
    ASSERT(p_trail != nullptr);
    for (const TrailEntry& e : p_trail->entries_) {
      ASSERT(e.song_ != nullptr);
      songs_queue_.push_back(e.song_);
    }

    FOREACH_EnabledPlayerInfo(player_info_, pi) {
      Trail* l_trail = GAMESTATE->cur_trail_[pi->GetStepsAndTrailIndex()];
      ASSERT(l_trail != nullptr);

      pi->steps_queue_.clear();
      pi->modifiers_queue_.clear();
      for (const TrailEntry& e : l_trail->entries_) {
        ASSERT(e.steps_ != nullptr);
        pi->steps_queue_.push_back(e.steps_);
        AttackArray a;
        e.GetAttackArray(a);
        pi->modifiers_queue_.push_back(a);
      }

      // In a survival course, override stored mods
      if (course->GetCourseType() == COURSE_TYPE_SURVIVAL &&
          SURVIVAL_MOD_OVERRIDE) {
        pi->GetPlayerState()->player_options_.FromString(
            ModsLevel_Stage,
            "clearall," + CommonMetrics::DEFAULT_NOTESKIN_NAME.GetValue() +
                "," + CommonMetrics::DEFAULT_MODIFIERS.GetValue());
        pi->GetPlayerState()->RebuildPlayerOptionsFromActiveAttacks();
      }
    }
  } else {
    songs_queue_.push_back(GAMESTATE->cur_song_);
    FOREACH_EnabledPlayerInfo(player_info_, pi) {
      Steps* steps = GAMESTATE->cur_steps_[pi->GetStepsAndTrailIndex()];
      pi->steps_queue_.push_back(steps);
      const PlayerOptions& p =
          pi->GetPlayerState()->player_options_.GetCurrent();

      AttackArray attacks = steps->GetAttacks();

      if (p.no_attack_ == 0 && p.rand_attack_ == 0 && !attacks.empty()) {
        pi->modifiers_queue_.push_back(attacks);
      } else {
        AttackArray aa;
        pi->modifiers_queue_.push_back(aa);
      }
    }
  }
}

ScreenGameplay::~ScreenGameplay() {
  GAMESTATE->adjust_tokens_by_song_cost_for_final_stage_check_ = true;
  if (this->IsFirstUpdate()) {
    /* We never received any updates. That means we were deleted without being
     * used, and never actually played. (This can happen when backing out of
     * ScreenStage.) Cancel the stage. */
    GAMESTATE->CancelStage();
  }

  LOG->Trace("ScreenGameplay::~ScreenGameplay()");

  const Style* master_style =
      GAMESTATE->GetCurrentStyle(GAMESTATE->GetMasterPlayerNumber());
  const bool shared_sides_style =
      master_style != nullptr &&
      master_style->style_type_ == StyleType_TwoPlayersSharedSides;
  if (shared_sides_style) {
    const PlayerNumber master = GAMESTATE->GetMasterPlayerNumber();
    FOREACH_PlayerNumberInfo(player_info_, pi) {
      if (pi->pn_ != master) {
        pi->life_meter_ = nullptr;
        pi->secondary_score_keeper_ = nullptr;
      }
    }
  }

  RageUtil::SafeDelete(song_background_);
  RageUtil::SafeDelete(song_foreground_);

  if (!GAMESTATE->demonstration_or_jukebox_) {
    MEMCARDMAN->UnPauseMountingThread();
  }

  RageUtil::SafeDelete(combined_life_meter_);
  routine_shared_score_keeper_ = nullptr;
  if (sound_music_) {
    sound_music_->StopPlaying();
  }

  gameplay_assist_.StopPlaying();
}

bool ScreenGameplay::IsLastSong() {
  if (GAMESTATE->cur_course_) {
    if (GAMESTATE->cur_course_->goal_seconds_ > 0 &&
        GAMESTATE->workout_goal_complete_) {
      return true;
    }
  }
  if (GAMESTATE->cur_course_ && GAMESTATE->cur_course_->repeat_) {
    return false;
  }
  return GAMESTATE->GetCourseSongIndex() >=
         (int)songs_queue_.size() - 1;  // GetCourseSongIndex() is 0-based
}

void ScreenGameplay::SetupSong(int song_index) {
  FOREACH_EnabledPlayerInfo(player_info_, pi) {
    /* This is the first beat that can be changed without it being visible.
     * Until we draw for the first time, any beat can be changed. */
    pi->GetPlayerState()->last_drawn_beat_ = -100;

    Steps* steps = pi->steps_queue_[song_index];
    GAMESTATE->cur_steps_[pi->GetStepsAndTrailIndex()].Set(steps);

    /* Load new NoteData into Player. Do this before
     * RebuildPlayerOptionsFromActiveAttacks or else transform mods will get
     * propagated to GAMESTATE->m_pPlayerOptions too early and be double-applied
     * to the NoteData:
     * once in Player::Load, then again in Player::ApplyActiveAttacks.
     * This is very bad for transforms like AddMines.
     */
    NoteData original_note_data;
    steps->GetNoteData(original_note_data);

    const Style* style = GAMESTATE->GetCurrentStyle(pi->pn_);
    NoteData transformed;
    style->GetTransformedNoteDataForStyle(
        pi->GetStepsAndTrailIndex(), original_note_data, transformed);

    // HACK: Apply NoteSkins from global course options. Do this before
    // Player::Load, since it needs to know which note skin to load.
    pi->GetPlayerState()->mods_to_apply_.clear();
    for (unsigned i = 0; i < pi->modifiers_queue_[song_index].size(); ++i) {
      Attack a = pi->modifiers_queue_[song_index][i];
      if (a.start_second_ != 0) {
        continue;
      }
      a.start_second_ = ATTACK_STARTS_NOW;  // now

      PlayerOptions po;
      po.FromString(a.modifiers_);
      if (po.note_skin_.empty()) {
        continue;
      }
      a.modifiers_ = po.note_skin_;

      pi->GetPlayerState()->LaunchAttack(a);
    }

    /* Update attack bOn flags, and rebuild Current-level options
     * from Song-level options. The current NoteSkin could have changed
     * because of an attack ending. */
    pi->GetPlayerState()->Update(0);

    // load player
    {
      pi->note_data_ = transformed;
      NoteDataUtil::RemoveAllTapsOfType(
          pi->note_data_, TapNoteType_AutoKeysound);
      pi->player_->Load();
    }

    // load auto keysounds
    {
      NoteData nd = transformed;
      NoteDataUtil::RemoveAllTapsExceptForType(nd, TapNoteType_AutoKeysound);
      auto_keysounds_.Load(pi->GetStepsAndTrailIndex(), nd);
    }

    {
      std::string type;
      switch (GAMESTATE->song_options_.GetCurrent().sound_effect_type_) {
        case SoundEffectType_Off:
          type = "SoundEffectControl_Off";
          break;
        case SoundEffectType_Speed:
          type = "SoundEffectControl_Speed";
          break;
        case SoundEffectType_Pitch:
          type = "SoundEffectControl_Pitch";
          break;
        default:
          break;
      }

      pi->sound_effect_control_.Load(
          type, pi->GetPlayerState(), &pi->note_data_);
    }

    // Put course options into effect.  Do this after Player::Load so
    // that mods aren't double-applied.
    pi->GetPlayerState()->mods_to_apply_.clear();
    for (unsigned i = 0; i < pi->modifiers_queue_[song_index].size(); ++i) {
      Attack a = pi->modifiers_queue_[song_index][i];
      if (a.start_second_ == 0) {
        a.start_second_ = ATTACK_STARTS_NOW;  // now
      }

      pi->GetPlayerState()->LaunchAttack(a);
      GAMESTATE->song_options_.FromString(ModsLevel_Song, a.modifiers_);
    }

    // Update attack bOn flags.
    pi->GetPlayerState()->Update(0);

    // Hack: Course modifiers that are set to start immediately shouldn't tween
    // on.
    pi->GetPlayerState()->player_options_.SetCurrentToLevel(ModsLevel_Stage);
  }
}

void ScreenGameplay::ReloadCurrentSong() {
  FOREACH_EnabledPlayerInfoNotDummy(player_info_, pi) pi->GetPlayerStageStats()
      ->songs_played_--;

  LoadNextSong();
}

void ScreenGameplay::LoadNextSong() {
  GAMESTATE->ResetMusicStatistics();

  FOREACH_EnabledPlayerInfo(player_info_, pi) {
    pi->GetPlayerStageStats()->songs_played_++;
    if (pi->text_course_song_number_) {
      pi->text_course_song_number_->SetText(ssprintf(
          SONG_NUMBER_FORMAT.GetValue().c_str(),
          pi->GetPlayerStageStats()->songs_passed_ + 1));
    }
  }

  int play_song_index = GAMESTATE->GetCourseSongIndex();
  play_song_index %= songs_queue_.size();
  GAMESTATE->cur_song_.Set(songs_queue_[play_song_index]);
  STATSMAN->cur_stage_stats_.played_songs_.push_back(GAMESTATE->cur_song_);

  // No need to do this here.  We do it in SongFinished().
  // GAMESTATE->RemoveAllActiveAttacks();

  // Force immediate fail behavior changed to theme metric by Kyz.
  if (FORCE_IMMEDIATE_FAIL_FOR_BATTERY) {
    FOREACH_EnabledPlayerInfo(player_info_, pi) {
      if (pi->GetPlayerState()->player_options_.GetStage().life_type_ ==
          LifeType_Battery) {
        PO_GROUP_ASSIGN(
            pi->GetPlayerState()->player_options_, ModsLevel_Song, fail_type_,
            FailType_Immediate);
      }
    }
  }

  text_song_options_.SetText(GAMESTATE->song_options_.GetCurrent().GetString());

  SetupSong(play_song_index);

  Song* song = GAMESTATE->cur_song_;
  bool sent_routine_shared_on_next_song = false;
  FOREACH_EnabledPlayerInfo(player_info_, pi) {
    Steps* steps = GAMESTATE->cur_steps_[pi->GetStepsAndTrailIndex()];
    ++pi->GetPlayerStageStats()->steps_played_;

    ASSERT(GAMESTATE->cur_steps_[pi->GetStepsAndTrailIndex()] != nullptr);
    if (pi->text_steps_description_) {
      pi->text_steps_description_->SetText(steps->GetDescription());
    }

    /* Increment the play count even if the player fails.  (It's still popular,
     * even if the people playing it aren't good at it.) */
    if (!GAMESTATE->demonstration_or_jukebox_) {
      if (pi->pn_ != kPlayerInvalid) {
        PROFILEMAN->IncrementStepsPlayCount(song, steps, pi->pn_);
      }
    }

    if (pi->text_player_options_) {
      pi->text_player_options_->SetText(
          pi->GetPlayerState()->player_options_.GetCurrent().GetString());
    }
    if (pi->active_attack_list_) {
      pi->active_attack_list_->Refresh();
    }

    // reset oni game over graphic
    SET_XY_AND_ON_COMMAND(pi->spr_oni_game_over_);

    if (pi->GetPlayerState()->player_options_.GetStage().life_type_ ==
            LifeType_Battery &&
        pi->GetPlayerStageStats()->failed_) {  // already failed
      pi->ShowOniGameOver();
    }

    if (pi->GetPlayerState()->player_options_.GetStage().life_type_ ==
            LifeType_Bar &&
        pi->life_meter_) {
      pi->life_meter_->UpdateNonstopLifebar();
    }

    if (pi->steps_display_) {
      pi->steps_display_->SetFromSteps(steps);
    }

    /* The actual note data for scoring is the base class of Player.  This
     * includes transforms, like Wide.  Otherwise, the scoring will operate on
     * the wrong data. */
    if (pi->primary_score_keeper_) {
      pi->primary_score_keeper_->OnNextSong(
          GAMESTATE->GetCourseSongIndex(), steps, &pi->player_->GetNoteData());
    }
    if (pi->secondary_score_keeper_) {
      const bool is_routine_shared_keeper =
          pi->secondary_score_keeper_ == routine_shared_score_keeper_;
      const bool should_call_secondary =
          !is_routine_shared_keeper || !sent_routine_shared_on_next_song;
      if (should_call_secondary) {
        if (is_routine_shared_keeper) {
          sent_routine_shared_on_next_song = true;
        }
        pi->secondary_score_keeper_->OnNextSong(
            GAMESTATE->GetCourseSongIndex(), steps,
            &pi->player_->GetNoteData());
      }
    }

    // Don't mess with the PlayerController of the Dummy player
    if (!pi->is_dummy_) {
      if (GAMESTATE->demonstration_or_jukebox_) {
        pi->GetPlayerState()->player_controller_ = PC_CPU;
        pi->GetPlayerState()->cpu_skill_ = 5;
      } else if (GAMESTATE->IsCpuPlayer(pi->GetStepsAndTrailIndex())) {
        pi->GetPlayerState()->player_controller_ = PC_CPU;
        int meter = steps->GetMeter();
        int new_skill =
            SCALE(meter, kMinMeter, kMaxMeter, 0, kNumSkillLevels - 1);
        /* Watch out: songs aren't actually bound by MAX_METER. */
        new_skill = std::clamp(new_skill, 0, kNumSkillLevels - 1);
        pi->GetPlayerState()->cpu_skill_ = new_skill;
      } else {
        if (pi->GetPlayerState()
                ->player_options_.GetCurrent()
                .player_auto_play_ != 0) {
          pi->GetPlayerState()->player_controller_ = PC_AUTOPLAY;
        } else {
          pi->GetPlayerState()->player_controller_ = GamePreferences::auto_play;
        }
      }
    }
  }

  bool all_reverse = true;
  bool at_least_one_reverse = false;
  FOREACH_EnabledPlayerInfo(player_info_, pi) {
    if (pi->GetPlayerState()
            ->player_options_.GetCurrent()
            .scrolls_[PlayerOptions::SCROLL_REVERSE] == 1) {
      at_least_one_reverse = true;
    } else {
      all_reverse = false;
    }
  }

  FOREACH_EnabledPlayerInfo(player_info_, pi) {
    bool reverse = pi->GetPlayerState()
                       ->player_options_.GetCurrent()
                       .scrolls_[PlayerOptions::SCROLL_REVERSE] == 1;

    if (pi->steps_display_) {
      pi->steps_display_->PlayCommand(reverse ? "SetReverse" : "SetNoReverse");
    }
  }

  lyric_display_.PlayCommand(
      all_reverse            ? "SetReverse"
      : at_least_one_reverse ? "SetOneReverse"
                             : "SetNoReverse");

  // Load lyrics
  // XXX: don't load this here (who and why? -aj)
  LyricsLoader ll;
  if (GAMESTATE->cur_song_->HasLyrics()) {
    ll.LoadFromLRCFile(
        GAMESTATE->cur_song_->GetLyricsPath(), *GAMESTATE->cur_song_);
  }

  // Set up song-specific graphics.

  // Check to see if any players are in beginner mode.
  // Note: steps can be different if turn modifiers are used.
  if (PREFSMAN->show_beginner_helper_) {
    FOREACH_EnabledPlayerNumberInfo(player_info_, pi) {
      PlayerNumber pn = pi->GetStepsAndTrailIndex();
      if (GAMESTATE->IsHumanPlayer(pn) &&
          GAMESTATE->cur_steps_[pn]->GetDifficulty() == Difficulty_Beginner) {
        beginner_helper_.AddPlayer(pn, pi->player_->GetNoteData());
      }
    }
  }

  if (song_background_) {
    song_background_->Unload();
  }

  if (song_foreground_) {
    song_foreground_->Unload();
  }

  if (!PREFSMAN->show_beginner_helper_ || !beginner_helper_.Init(2)) {
    beginner_helper_.SetVisible(false);

    // BeginnerHelper disabled, or failed to load.
    if (song_background_) {
      song_background_->LoadFromSong(GAMESTATE->cur_song_);
    }

    if (!GAMESTATE->demonstration_or_jukebox_) {
      /* This will fade from a preset brightness to the actual brightness
       * (based on prefs and "cover"). The preset brightness may be 0 (to
       * fade from black), or it might be 1, if the stage screen has the
       * song BG and we're coming from it (like Pump). This used to be done
       * in SM_PlayReady, but that means it's impossible to snap to the
       * new brightness immediately. */
      if (song_background_) {
        song_background_->SetBrightness(INITIAL_BACKGROUND_BRIGHTNESS);
        song_background_->FadeToActualBrightness();
      }
    }
  } else {
    beginner_helper_.SetVisible(true);
  }

  FOREACH_EnabledPlayerInfo(player_info_, pi) {
    if (!pi->GetPlayerStageStats()->failed_) {
      // give a little life back between stages
      if (pi->life_meter_) {
        pi->life_meter_->OnLoadSong();
      }
      if (pi->primary_score_display_) {
        pi->primary_score_display_->OnLoadSong();
      }
      if (pi->secondary_score_display_) {
        pi->secondary_score_display_->OnLoadSong();
      }
    }
  }
  if (combined_life_meter_) {
    combined_life_meter_->OnLoadSong();
  }

  if (song_foreground_) {
    song_foreground_->LoadFromSong(GAMESTATE->cur_song_);
  }

  time_since_last_dancing_comment_ = 0;

  /* m_soundMusic and m_pSongBackground take a very long time to load,
   * so cap fDelta at 0 so m_NextSong will show up on screen.
   * -Chris */
  zero_delta_on_next_update_ = true;
  SCREENMAN->ZeroNextUpdate();

  // Load cabinet lights data
  LoadLights();

  /* Load the music last, since it may start streaming and we don't want the
   * music to compete with other loading. */
  auto_keysounds_.FinishLoading();
  sound_music_ = auto_keysounds_.GetSound();

  /* Give SoundEffectControls the new RageSoundReaders. */
  FOREACH_EnabledPlayerInfo(player_info_, pi) {
    RageSoundReader* sound_reader =
        pi->pn_ == GAMESTATE->GetMasterPlayerNumber()
            ? auto_keysounds_.GetSharedSound()
            : nullptr;
    pi->sound_effect_control_.SetSoundReader(sound_reader);
  }

  MESSAGEMAN->Broadcast("DoneLoadingNextSong");
}

void ScreenGameplay::LoadLights() {
  if (!LIGHTSMAN->IsEnabled()) {
    return;
  }

  // First, check if the song has explicit lights
  cabinet_lights_note_data_.Init();
  ASSERT(GAMESTATE->cur_song_ != nullptr);

  const Steps* steps = SongUtil::GetClosestNotes(
      GAMESTATE->cur_song_, StepsType_lights_cabinet, Difficulty_Medium);
  if (steps != nullptr) {
    steps->GetNoteData(cabinet_lights_note_data_);
    return;
  }

  // No explicit lights.  Create autogen lights.
  std::string difficulty = PREFSMAN->lights_steps_difficulty_;
  std::vector<std::string> difficulties;
  split(difficulty, ",", difficulties);

  // Always use the steps from the primary steps type so that lights are
  // consistent over single and double styles.
  StepsType st =
      GAMEMAN->GetHowToPlayStyleForGame(GAMESTATE->cur_game_)->steps_type_;

  Difficulty d1 = Difficulty_Invalid;
  if (difficulties.size() > 0) {
    if (CompareNoCase(difficulties[0], "selected") == 0) {
      // Base lights off current difficulty of active player
      // Can be either P1 or P2 if they're individual or P1 if both are active
      FOREACH_EnabledPlayerNumberInfo(player_info_, pi) {
        PlayerNumber pn = pi->GetStepsAndTrailIndex();

        if (GAMESTATE->IsPlayerEnabled(pn)) {
          d1 = GAMESTATE->cur_steps_[pn]->GetDifficulty();
          break;
        }
      }
    } else {
      d1 = StringToDifficulty(difficulties[0]);
    }
  }

  steps = SongUtil::GetClosestNotes(GAMESTATE->cur_song_, st, d1);

  // If we can't find anything at all, stop.
  if (steps == nullptr) {
    return;
  }

  NoteData tap_note_data1;
  steps->GetNoteData(tap_note_data1);

  // taken from oitg, restores arrow -> marquee/bass light mapping.
  // if the user has a pref for more than one difficulty to make the lighting
  // chart...
  if (difficulties.size() > 1) {
    Difficulty d2 = StringToDifficulty(difficulties[1]);

    Steps* steps2;

    steps2 = SongUtil::GetClosestNotes(GAMESTATE->cur_song_, st, d2);

    // if the difficulities are actually different
    // then we can use them to generate a lighting chart.
    // as the user defined.
    if (steps != steps2) {
      NoteData tap_note_data2;
      steps2->GetNoteData(tap_note_data2);

      NoteDataUtil::LoadTransformedLightsFromTwo(
          tap_note_data1, tap_note_data2, cabinet_lights_note_data_);
      return;
    }

    // fall through
  }

  NoteDataUtil::LoadTransformedLights(
      tap_note_data1, cabinet_lights_note_data_,
      GAMEMAN->GetStepsTypeInfo(StepsType_lights_cabinet).num_tracks_);
}

void ScreenGameplay::StartPlayingSong(
    float min_time_to_notes, float min_time_to_music) {
  ASSERT(min_time_to_notes >= 0);
  ASSERT(min_time_to_music >= 0);

  sound_music_->SetProperty("AccurateSync", true);

  RageSoundParams p;
  p.speed_ = GAMESTATE->song_options_.GetCurrent().music_rate_;
  p.stop_mode_ = RageSoundParams::M_CONTINUE;

  {
    const float first_second = GAMESTATE->cur_song_->GetFirstSecond();
    float start_delay = min_time_to_notes - first_second;
    start_delay = std::max(start_delay, min_time_to_music);
    p.start_second_ = -start_delay;
  }

  ASSERT(!sound_music_->IsPlaying());
  {
    float seconds_to_start_fading_out_music, seconds_to_start_transitioning_out;
    GetMusicEndTiming(
        seconds_to_start_fading_out_music, seconds_to_start_transitioning_out);

    if (seconds_to_start_fading_out_music <
        GAMESTATE->cur_song_->music_length_seconds_) {
      p.fade_out_seconds_ = MUSIC_FADE_OUT_SECONDS;
      p.length_seconds_ = seconds_to_start_fading_out_music +
                          MUSIC_FADE_OUT_SECONDS - p.start_second_;
    }
  }
  sound_music_->Play(false, &p);
  if (paused_) {
    sound_music_->Pause(true);
  }

  /* Make sure GAMESTATE->m_fMusicSeconds is set up. */
  GAMESTATE->position_.music_seconds_ = -5000;
  UpdateSongPosition(0);

  ASSERT(
      GAMESTATE->position_.music_seconds_ >
      -4000); /* make sure the "fake timer" code doesn't trigger */
}

void ScreenGameplay::set_paused_internal(bool p) {
  paused_ = p;
  GAMESTATE->SetPaused(p);
}

void ScreenGameplay::PauseGame(bool pause, GameController gc) {
  if (paused_ == pause) {
    LOG->Trace(
        "ScreenGameplay::PauseGame(%i) received, but already in that state; "
        "ignored",
        pause);
    return;
  }

  // Don't pause if we're already tweening out.
  if (pause && dancing_state_ == STATE_OUTRO) {
    return;
  }

  ResetGiveUpTimers(false);

  set_paused_internal(pause);
  pause_controller_ = gc;

  sound_music_->Pause(pause);
  if (pause) {
    this->PlayCommand("Pause");
  } else {
    this->PlayCommand("Unpause");
  }

  FOREACH_EnabledPlayerInfo(player_info_, pi) {
    pi->player_->SetPaused(paused_);
  }
}

// play assist ticks
void ScreenGameplay::PlayTicks() {
  /* TODO: Allow all players to have ticks. Not as simple as it looks.
   * If a loop takes place, it could make one player's ticks come later
   * than intended. Any help here would be appreciated. -Wolfman2000 */
  Player& player = *player_info_[GAMESTATE->GetMasterPlayerNumber()].player_;
  const NoteData& nd = player.GetNoteData();
  gameplay_assist_.PlayTicks(nd, player.GetPlayerState());
}

/* Play announcer "type" if it's been at least fSeconds since the last
 * announcer. */
void ScreenGameplay::PlayAnnouncer(
    const std::string& type, float seconds, float* delta_seconds) {
  if (GAMESTATE->opponent_health_percent_ == 0) {
    return;  // Shut the announcer up
  }

  /* Don't play in demonstration. */
  if (GAMESTATE->demonstration_or_jukebox_) {
    return;
  }

  /* Don't play before the first beat, or after we're finished. */
  if (dancing_state_ != STATE_DANCING) {
    return;
  }
  if (GAMESTATE->cur_song_ ==
          nullptr ||  // this will be true on ScreenDemonstration sometimes
      GAMESTATE->position_.song_beat_ < GAMESTATE->cur_song_->GetFirstBeat()) {
    return;
  }

  if (*delta_seconds < seconds) {
    return;
  }
  *delta_seconds = 0;

  SOUND->PlayOnceFromAnnouncer(type);
}

void ScreenGameplay::UpdateSongPosition(float delta_time) {
  if (!sound_music_->IsPlaying()) {
    return;
  }

  RageTimer tm;
  const float seconds = sound_music_->GetPositionSeconds(&tm);
  GAMESTATE->UpdateSongPosition(
      seconds, GAMESTATE->cur_song_->song_timing_, tm);
}

void ScreenGameplay::BeginScreen() {
  if (GAMESTATE->cur_song_ == nullptr) {
    return;
  }

  ScreenWithMenuElements::BeginScreen();

  SOUND->PlayOnceFromAnnouncer("gameplay intro");  // crowd cheer

  StartPlayingSong(MIN_SECONDS_TO_STEP, MIN_SECONDS_TO_MUSIC);
}

bool ScreenGameplay::OneFailed() {
  const Style* style =
      GAMESTATE->GetCurrentStyle(GAMESTATE->GetMasterPlayerNumber());
  if (style != nullptr &&
      style->style_type_ == StyleType_TwoPlayersSharedSides) {
    return combined_life_meter_ != nullptr && combined_life_meter_->IsFailing();
  }

  FOREACH_EnabledPlayerInfo(player_info_, pi) {
    if (pi->life_meter_ && pi->life_meter_->IsFailing()) {
      return true;
    }
  }
  return false;
}

void ScreenGameplay::GetMusicEndTiming(
    float& seconds_to_start_fading_out_music,
    float& seconds_to_start_transitioning_out) {
  float last_step_seconds = GAMESTATE->cur_song_->GetLastSecond();
  last_step_seconds += Player::GetMaxStepDistanceSeconds();

  float transition_length;
  if (!GAMESTATE->IsCourseMode() || IsLastSong()) {
    transition_length = OUT_TRANSITION_LENGTH;
  } else {
    transition_length = COURSE_TRANSITION_LENGTH;
  }

  seconds_to_start_transitioning_out = last_step_seconds;

  // Align the end of the music fade to the end of the transition.
  float seconds_to_finish_fading_out_music =
      seconds_to_start_transitioning_out + transition_length;
  if (seconds_to_finish_fading_out_music <
      GAMESTATE->cur_song_->music_length_seconds_) {
    seconds_to_start_fading_out_music =
        seconds_to_finish_fading_out_music - MUSIC_FADE_OUT_SECONDS;
  } else {
    seconds_to_start_fading_out_music =
        GAMESTATE->cur_song_->music_length_seconds_;  // don't fade
  }

  /* Make sure we keep going long enough to register a miss for the last note,
   * and never start fading before the last note. */
  seconds_to_start_fading_out_music =
      std::max(seconds_to_start_fading_out_music, last_step_seconds);
  seconds_to_start_transitioning_out =
      std::max(seconds_to_start_transitioning_out, last_step_seconds);

  /* Make sure the fade finishes before the transition finishes. */
  seconds_to_start_transitioning_out = std::max(
      seconds_to_start_transitioning_out, seconds_to_start_fading_out_music +
                                              MUSIC_FADE_OUT_SECONDS -
                                              transition_length);
}

void ScreenGameplay::Update(float delta_time) {
  if (GAMESTATE->cur_song_ == nullptr) {
    /* ScreenDemonstration will move us to the next screen.  We just need to
     * survive for one update without crashing.  We need to call Screen::Update
     * to make sure we receive the next-screen message. */
    Screen::Update(delta_time);
    return;
  }

  UpdateSongPosition(delta_time);

  if (zero_delta_on_next_update_) {
    Screen::Update(0);
    zero_delta_on_next_update_ = false;
  } else {
    const float rate = PREFSMAN->rate_mods_affect_tweens_
                           ? GAMESTATE->song_options_.GetCurrent().music_rate_
                           : 1.0f;
    Screen::Update(delta_time * rate);
  }

  /* This happens if ScreenDemonstration::HandleScreenMessage sets a new screen
   * when PREFSMAN->m_bDelayedScreenLoad. */
  if (GAMESTATE->cur_song_ == nullptr) {
    return;
  }
  /* This can happen if ScreenDemonstration::HandleScreenMessage sets a new
   * screen when !PREFSMAN->m_bDelayedScreenLoad.  (The new screen was loaded
   * when we called Screen::Update, and the ctor might set a new
   * GAMESTATE->m_pCurSong, so the above check can fail.) */
  if (SCREENMAN->GetTopScreen() != this) {
    return;
  }

  /* Update actors when paused, but never move on to another state. */
  if (paused_) {
    return;
  }

  // LOG->Trace( "m_fOffsetInBeats = %f, m_fBeatsPerSecond = %f,
  // m_Music.GetPositionSeconds = %f", m_fOffsetInBeats, m_fBeatsPerSecond,
  // m_Music.GetPositionSeconds() );

  const Style* style =
      GAMESTATE->GetCurrentStyle(GAMESTATE->GetMasterPlayerNumber());
  const bool shared_sides =
      style != nullptr && style->style_type_ == StyleType_TwoPlayersSharedSides;

  // update GameState HealthState
  FOREACH_EnabledPlayerInfo(player_info_, pi) {
    bool failing = pi->life_meter_ && pi->life_meter_->IsFailing();
    bool hot = pi->life_meter_ && pi->life_meter_->IsHot();
    bool danger = pi->life_meter_ && pi->life_meter_->IsInDanger();
    if (shared_sides && combined_life_meter_ != nullptr) {
      failing = combined_life_meter_->IsFailing();
      hot = combined_life_meter_->IsHot();
      danger = combined_life_meter_->IsInDanger();
    }

    HealthState& hs = pi->GetPlayerState()->health_state_;
    HealthState old_health_state = hs;
    if (GAMESTATE->GetPlayerFailType(pi->GetPlayerState()) != FailType_Off &&
        failing) {
      hs = HealthState_Dead;
    } else if (hot) {
      hs = HealthState_Hot;
    } else if (
        GAMESTATE->GetPlayerFailType(pi->GetPlayerState()) != FailType_Off &&
        danger) {
      hs = HealthState_Danger;
    } else {
      hs = HealthState_Alive;
    }

    if (hs != old_health_state) {
      Message msg("HealthStateChanged");
      msg.SetParam("PlayerNumber", pi->pn_);
      msg.SetParam("HealthState", hs);
      msg.SetParam("OldHealthState", old_health_state);
      MESSAGEMAN->Broadcast(msg);
    }

    pi->sound_effect_control_.Update(delta_time);
  }

  {
    float speed = GAMESTATE->song_options_.GetCurrent().music_rate_;
    if (GAMESTATE->song_options_.GetCurrent().haste_ != 0.0f) {
      speed *= GetHasteRate();
    }

    RageSoundParams p = sound_music_->GetParams();
    if (std::abs(p.speed_ - speed) > 0.01f && speed >= 0.0f) {
      p.speed_ = speed;
      sound_music_->SetParams(p);
    }
  }

  switch (dancing_state_) {
    case STATE_DANCING: {
      // If we're in TwoPlayerSharedSides, if one player fails, both fail.
      if (shared_sides) {
        if (OneFailed()) {
          FOREACH_EnabledPlayerInfo(player_info_, pi) {
            PlayerNumber pn = pi->GetStepsAndTrailIndex();

            FailType ft = GAMESTATE->GetPlayerFailType(pi->GetPlayerState());
            LifeType lt =
                pi->GetPlayerState()->player_options_.GetStage().life_type_;
            if (ft == FailType_Off || ft == FailType_EndOfSong) {
              continue;
            }
            if (pi->GetPlayerStageStats()->failed_) {
              continue; /* failed and is already dead */
            }
            pi->GetPlayerStageStats()->failed_ = true;  // fail
          }
        }
      }

      /* Set STATSMAN->m_CurStageStats.bFailed for failed players.  In,
       * FAIL_IMMEDIATE, send SM_BeginFailed if all players failed, and kill
       * dead Oni players. */
      FOREACH_EnabledPlayerInfo(player_info_, pi) {
        PlayerNumber pn = pi->GetStepsAndTrailIndex();

        FailType ft = GAMESTATE->GetPlayerFailType(pi->GetPlayerState());
        LifeType lt =
            pi->GetPlayerState()->player_options_.GetStage().life_type_;
        if (ft == FailType_Off || ft == FailType_EndOfSong) {
          continue;
        }

        if (shared_sides) {
          if (combined_life_meter_ != nullptr &&
              !combined_life_meter_->IsFailing()) {
            continue; /* isn't failing */
          }
        } else {
          // check for individual fail
          if (pi->life_meter_ == nullptr || !pi->life_meter_->IsFailing()) {
            continue; /* isn't failing */
          }
        }

        if (pi->GetPlayerStageStats()->failed_) {
          continue; /* failed and is already dead */
        }

        LOG->Trace("Player %d failed", (int)pn);
        pi->GetPlayerStageStats()->failed_ = true;  // fail
        pi->life_meter_->SetLife(0);
        {
          Message msg("PlayerFailed");
          msg.SetParam("PlayerNumber", pi->pn_);
          MESSAGEMAN->Broadcast(msg);
        }

        // Check for and do Oni die.
        bool allow_oni_die = false;
        switch (lt) {
          case LifeType_Battery:
            allow_oni_die = true;
          default:
            break;
        }
        if (allow_oni_die && ft == FailType_Immediate) {
          if (!STATSMAN->cur_stage_stats_
                   .AllFailed())  // if not the last one to fail
          {
            // kill them!
            FailFadeRemovePlayer(&*pi);
          }
        }
      }

      bool all_failed = true;
      FOREACH_EnabledPlayerInfo(player_info_, pi) {
        FailType ft = GAMESTATE->GetPlayerFailType(pi->GetPlayerState());
        switch (ft) {
          case FailType_Immediate:
            if (shared_sides) {
              if (combined_life_meter_ != nullptr &&
                  !combined_life_meter_->IsFailing()) {
                all_failed = false;
              }
            } else {
              if (pi->life_meter_ == nullptr ||
                  (pi->life_meter_ && !pi->life_meter_->IsFailing())) {
                all_failed = false;
              }
            }
            break;
          case FailType_ImmediateContinue:
          case FailType_EndOfSong:
            all_failed = false;  // wait until the end of the song to fail.
            break;
          case FailType_Off:
            all_failed = false;  // never fail.
            break;
          default:
            FAIL_M("Invalid fail type! Aborting...");
        }
      }

      if (all_failed) {
        sound_music_->StopPlaying();
        SCREENMAN->PostMessageToTopScreen(SM_NotesEnded, 0);
        lyric_display_.Stop();
      }

      // Update living players' alive time
      // HACK: Don't scale alive time when using tab/tilde.  Instead of
      // accumulating time from a timer, this time should instead be tied to the
      // music position.
      float unscaled_delta_time = timer_gameplay_seconds_.GetDeltaTime();

      FOREACH_EnabledPlayerInfo(
          player_info_, pi) if (!pi->GetPlayerStageStats()->failed_) pi
          ->GetPlayerStageStats()
          ->alive_seconds_ += unscaled_delta_time *
                              GAMESTATE->song_options_.GetCurrent().music_rate_;

      // update fGameplaySeconds
      STATSMAN->cur_stage_stats_.gameplay_seconds_ += unscaled_delta_time;
      float cur_beat = GAMESTATE->position_.song_beat_;
      Song& s = *GAMESTATE->cur_song_;

      if (cur_beat >= s.GetFirstBeat() && cur_beat < s.GetLastBeat()) {
        STATSMAN->cur_stage_stats_.steps_seconds_ += unscaled_delta_time;

        UpdateHasteRate();

        if (GAMESTATE->song_options_.GetCurrent().haste_ != 0.0f) {
          float haste_rate = GetHasteRate();
          // For negative haste, accumulate seconds while the song is slowed
          // down.
          if (GAMESTATE->song_options_.GetCurrent().haste_ < 0) {
            GAMESTATE->accumulated_haste_seconds_ -=
                (unscaled_delta_time * haste_rate) - unscaled_delta_time;
          }
          // For positive haste, accumulate seconds while the song is sped up.
          else {
            GAMESTATE->accumulated_haste_seconds_ +=
                (unscaled_delta_time * haste_rate) - unscaled_delta_time;
          }
        }
      }

      // Check for end of song
      {
        float seconds_to_start_fading_out_music,
            seconds_to_start_transitioning_out;
        GetMusicEndTiming(
            seconds_to_start_fading_out_music,
            seconds_to_start_transitioning_out);

        bool all_really_failed = STATSMAN->cur_stage_stats_.AllFailed();
        if (all_really_failed) {
          seconds_to_start_transitioning_out += BEGIN_FAILED_DELAY;
        }

        if (GAMESTATE->position_.music_seconds_ >=
                seconds_to_start_transitioning_out &&
            !next_song_.IsTransitioning()) {
          this->PostScreenMessage(SM_NotesEnded, 0);
        }
      }

      // Check for enemy death in enemy battle
      static float last_seen_enemy_health = 1;
      if (last_seen_enemy_health != GAMESTATE->opponent_health_percent_) {
        last_seen_enemy_health = GAMESTATE->opponent_health_percent_;

        if (GAMESTATE->opponent_health_percent_ == 0) {
          // HACK:  Load incorrect directory on purpose for now.
          PlayAnnouncer("gameplay battle damage level3", 0);

          GAMESTATE->RemoveAllActiveAttacks();

          FOREACH_EnabledPlayerNumberInfo(player_info_, pi) {
            if (!GAMESTATE->IsCpuPlayer(pi->pn_)) {
              continue;
            }

            FailFadeRemovePlayer(&*pi);
          }
        }
      }

      // update give up
      bool give_up_timer_fired = false;
      give_up_timer_fired =
          !give_up_timer_.IsZero() && give_up_timer_.Ago() > GIVE_UP_SECONDS;
      gave_up_ = give_up_timer_fired;
      skipped_song_ = !skip_song_timer_.IsZero() &&
                      skip_song_timer_.Ago() > GIVE_UP_SECONDS;

      bool all_human_have_big_miss_combo = true;
      FOREACH_EnabledPlayerNumberInfo(player_info_, pi) {
        if (pi->GetPlayerState()->player_options_.GetCurrent().fail_type_ ==
                FailType_Off ||
            pi->GetPlayerState()->health_state_ < HealthState_Dead) {
          all_human_have_big_miss_combo = false;
          break;
        }
      }
      if (all_human_have_big_miss_combo)  // possible to get in here.
      {
        all_human_have_big_miss_combo =
            FAIL_ON_MISS_COMBO.GetValue() != -1 &&
            STATSMAN->cur_stage_stats_.GetMinimumMissCombo() >=
                (unsigned int)FAIL_ON_MISS_COMBO;
      }
      if (give_up_timer_fired || all_human_have_big_miss_combo ||
          skipped_song_) {
        STATSMAN->cur_stage_stats_.gave_up_ = true;
        FOREACH_EnabledPlayerNumberInfo(player_info_, pi) {
          pi->GetPlayerStageStats()->failed_ |= all_human_have_big_miss_combo;
          pi->GetPlayerStageStats()->disqualified_ |=
              give_up_timer_fired;  // Don't disqualify if failing for miss
                                    // combo. The player should still be
                                    // eligable for a high score on courses.
        }
        ResetGiveUpTimers(false);
        if (GIVING_UP_GOES_TO_PREV_SCREEN && !skipped_song_) {
          BeginBackingOutFromGameplay();
        } else {
          sound_music_->StopPlaying();
          this->PostScreenMessage(SM_NotesEnded, 0);
        }
        return;
      }

      // Check to see if it's time to play a ScreenGameplay comment
      time_since_last_dancing_comment_ += delta_time;

      PlayMode mode = GAMESTATE->play_mode_;
      switch (mode) {
        case PLAY_MODE_REGULAR:
        case PLAY_MODE_BATTLE:
        case PLAY_MODE_RAVE:
          if (GAMESTATE->OneIsHot()) {
            PlayAnnouncer("gameplay comment hot", SECONDS_BETWEEN_COMMENTS);
          } else if (GAMESTATE->AllAreInDangerOrWorse()) {
            PlayAnnouncer("gameplay comment danger", SECONDS_BETWEEN_COMMENTS);
          } else {
            PlayAnnouncer("gameplay comment good", SECONDS_BETWEEN_COMMENTS);
          }
          break;
        case PLAY_MODE_NONSTOP:
        case PLAY_MODE_ONI:
        case PLAY_MODE_ENDLESS:
          PlayAnnouncer("gameplay comment oni", SECONDS_BETWEEN_COMMENTS);
          break;
        default:
          FAIL_M(ssprintf("Invalid PlayMode: %i", mode));
      }
    }
    default:
      break;
  }

  PlayTicks();
  UpdateLights();
  SendCrossedMessages();

  // ArrowEffects::Update call moved because having it happen once per
  // NoteField (which means twice in two player) seemed wasteful. -Kyz
  ArrowEffects::Update();
}

void ScreenGameplay::DrawPrimitives() {
  // ScreenGameplay::DrawPrimitives exists so that the notefield board can be
  // above the song background and underneath everything else.  This way, a
  // theme can put a screen filter in the notefield board and not have it
  // obscure custom elements on the screen.  Putting the screen filter in the
  // notefield board simplifies placement because it ensures that the filter
  // is in the same place as the notefield, instead of forcing the filter to
  // check conditions and metrics that affect the position of the notefield.
  // This also solves the problem of the ComboUnderField metric putting the
  // combo underneath the opaque notefield board.
  // -Kyz
  if (song_background_) {
    song_background_->disable_draw_ = false;
    song_background_->Draw();
    song_background_->disable_draw_ = true;
  }
  FOREACH_EnabledPlayerNumberInfo(player_info_, pi) {
    pi->player_->DrawNoteFieldBoard();
  }
  // Clear the z buffer so 3D notes aren't hidden by anything in the underlay
  // using masking. -Kyz
  DISPLAY->ClearZBuffer();
  ScreenWithMenuElements::DrawPrimitives();
}

void ScreenGameplay::FailFadeRemovePlayer(PlayerInfo* pi) {
  SOUND->PlayOnceFromDir(THEME->GetPathS(name_, "oni die"));
  pi->ShowOniGameOver();
  int tracks = pi->note_data_.GetNumTracks();
  pi->note_data_.Init();                // remove all notes and scoring
  pi->note_data_.SetNumTracks(tracks);  // reset the number of tracks.
  pi->player_->FadeToFail();            // tell the NoteField to fade to white
}

float ScreenGameplay::GetHasteRate() { return curr_haste_rate_; }

void ScreenGameplay::UpdateHasteRate() {
  if (GAMESTATE->position_.music_seconds_ <
          GAMESTATE->last_haste_update_music_seconds_ ||  // new song
      GAMESTATE->position_.music_seconds_ >
          GAMESTATE->last_haste_update_music_seconds_ +
              haste_time_between_updates_) {
    bool any_player_hit_all_notes = false;
    FOREACH_EnabledPlayerInfo(player_info_, pi) {
      if (!GAMESTATE->IsHumanPlayer(pi->pn_)) {
        continue;
      }

      PlayerState* ps = pi->GetPlayerState();
      if (ps->taps_hit_since_last_haste_update_ > 0 &&
          ps->taps_missed_since_last_haste_update_ == 0) {
        any_player_hit_all_notes = true;
      }

      ps->taps_hit_since_last_haste_update_ = 0;
      ps->taps_missed_since_last_haste_update_ = 0;
    }

    if (any_player_hit_all_notes) {
      GAMESTATE->haste_rate_ += 0.1f;
    }
    rage_clamp(GAMESTATE->haste_rate_, -1.0f, +1.0f);

    GAMESTATE->last_haste_update_music_seconds_ =
        GAMESTATE->position_.music_seconds_;
  }

  /* If the life meter is less than half full, push the haste rate down to let
   * the player use his accumulated haste time. */
  float max_life = 0;
  FOREACH_EnabledPlayerInfo(player_info_, pi) {
    if (!GAMESTATE->IsHumanPlayer(pi->pn_)) {
      continue;
    }
    // In Battle/Rave mode, the players don't have life meters.
    if (pi->life_meter_) {
      max_life = std::max(max_life, pi->life_meter_->GetLife());
    } else {
      max_life = 1;
    }
  }
  if (max_life <= haste_life_switch_point_) {
    GAMESTATE->haste_rate_ =
        SCALE(max_life, 0.0f, haste_life_switch_point_, -1.0f, 0.0f);
  }
  rage_clamp(GAMESTATE->haste_rate_, -1.0f, +1.0f);

  float speed = 1.0f;
  // If there are no turning points or no add amounts, the bad themer probably
  // thinks that's a way to disable haste. Since we're outside a lua function,
  // crashing (asserting) won't point back to the source of the problem.
  if (haste_turning_points_.size() < 2 || haste_add_amounts_.size() < 2 ||
      haste_turning_points_.size() != haste_add_amounts_.size()) {
    curr_haste_rate_ = speed;
    return;
  }
  float options_haste = GAMESTATE->song_options_.GetCurrent().haste_;
  float scale_from_low = -1;
  float scale_from_high = 1;
  float scale_to_low = 0;
  float scale_to_high = 0;
  for (size_t turning_point = 0; turning_point < haste_turning_points_.size();
       ++turning_point) {
    float curr_turning_point = haste_turning_points_[turning_point];
    scale_from_high = curr_turning_point;
    scale_to_high = haste_add_amounts_[turning_point];
    if (GAMESTATE->haste_rate_ < curr_turning_point) {
      break;
    }
    scale_from_low = curr_turning_point;
    scale_to_low = haste_add_amounts_[turning_point];
  }
  // If negative haste is being used, the game instead slows down when the
  // player does well.
  float speed_add = SCALE(
                        GAMESTATE->haste_rate_, scale_from_low, scale_from_high,
                        scale_to_low, scale_to_high) *
                    options_haste;
  if (scale_from_low == scale_from_high) {
    speed_add = scale_to_high * options_haste;
  }
  rage_clamp(speed_add, -1.0f, 1.0f);

  // Only adjust speed_add by AccumulatedHasteSeconds when the player is losing
  // seconds.  Otherwise, gaining the first second is interfered with.
  bool losing_seconds = false;
  if (options_haste > 0) {
    losing_seconds = speed_add < 0;
  } else {
    losing_seconds = speed_add > 0;
  }
  if (losing_seconds && GAMESTATE->accumulated_haste_seconds_ <= 1) {
    /* Only allow slowing down the song while the players have accumulated
     * haste. This prevents dragging on the song by keeping the life meter
     * nearly empty. */
    /* In positive haste mode, the player accumulates seconds while the song
     * is sped up, and loses them while the song is slowed down.  "<= 1"
     * means that the player is only eligible to slow the song down when
     * they are down to their last accumulated second. -Kyz */
    // 1 second left is full speed_add, 0 seconds left is no speed_add.
    float clamp_secs = std::max(0.0f, GAMESTATE->accumulated_haste_seconds_);
    speed_add = speed_add * clamp_secs;
  }
  speed += speed_add;
  curr_haste_rate_ = speed;
}

void ScreenGameplay::UpdateLights() {
  if (!LIGHTSMAN->IsEnabled()) {
    return;
  }
  if (cabinet_lights_note_data_.GetNumTracks() ==
      0) {  // light data wasn't loaded
    return;
  }

  bool blink_cabinet_light[NUM_CabinetLight];
  bool blink_game_button[NUM_GameController][NUM_GameButton];
  ZERO(blink_cabinet_light);
  ZERO(blink_game_button);
  {
    const float song_beat = GAMESTATE->position_.light_song_beat_;
    const int song_row = BeatToNoteRowNotRounded(song_beat);

    static int row_last_crossed = 0;

    FOREACH_CabinetLight(cl) {
      // for each index we crossed since the last update:
      FOREACH_NONEMPTY_ROW_IN_TRACK_RANGE(
          cabinet_lights_note_data_, cl, r, row_last_crossed + 1,
          song_row + 1) {
        if (cabinet_lights_note_data_.GetTapNote(cl, r).type_ !=
            TapNoteType_Empty) {
          blink_cabinet_light[cl] = true;
        }
      }

      if (cabinet_lights_note_data_.IsHoldNoteAtRow(cl, song_row)) {
        blink_cabinet_light[cl] = true;
      }
    }

    FOREACH_EnabledPlayerNumberInfo(player_info_, pi) {
      const Style* style = GAMESTATE->GetCurrentStyle(pi->pn_);
      const NoteData& nd = pi->player_->GetNoteData();
      for (int t = 0; t < nd.GetNumTracks(); t++) {
        bool blink = false;

        // for each index we crossed since the last update:
        FOREACH_NONEMPTY_ROW_IN_TRACK_RANGE(
            nd, t, r, row_last_crossed + 1, song_row + 1) {
          const TapNote& tn = nd.GetTapNote(t, r);
          if (tn.type_ != TapNoteType_Mine) {
            blink = true;
          }
        }

        // check if a hold should be active
        if (nd.IsHoldNoteAtRow(t, song_row)) {
          blink = true;
        }

        if (blink) {
          std::vector<GameInput> gi;
          style->StyleInputToGameInput(t, pi->pn_, gi);
          for (size_t i = 0; i < gi.size(); ++i) {
            blink_game_button[gi[i].controller_][gi[i].button_] = true;
          }
        }
      }
    }

    row_last_crossed = song_row;
  }

  // Before the first beat of the song, all cabinet lights solid on (except for
  // menu buttons).
  Song& s = *GAMESTATE->cur_song_;
  bool override_cabinet_blink =
      (GAMESTATE->position_.song_beat_ < s.GetFirstBeat());
  FOREACH_CabinetLight(cl) blink_cabinet_light[cl] |= override_cabinet_blink;

  // Send blink data.
  FOREACH_CabinetLight(cl) {
    if (blink_cabinet_light[cl]) {
      LIGHTSMAN->BlinkCabinetLight(cl);
    }
  }

  FOREACH_ENUM(GameController, gc) {
    FOREACH_ENUM(GameButton, gb) {
      if (blink_game_button[gc][gb]) {
        LIGHTSMAN->BlinkGameButton(GameInput(gc, gb));
      }
    }
  }
}

void ScreenGameplay::SendCrossedMessages() {
  {
    static int row_last_crossed = 0;

    float position_seconds = GAMESTATE->position_.music_seconds_;
    float song_beat = GAMESTATE->cur_song_->song_timing_.GetBeatFromElapsedTime(
        position_seconds);

    int row_now = BeatToNoteRowNotRounded(song_beat);
    row_now = std::max(0, row_now);

    for (int r = row_last_crossed + 1; r <= row_now; r++) {
      if (GetNoteType(r) == NOTE_TYPE_4TH) {
        MESSAGEMAN->Broadcast(Message_BeatCrossed);
      }
    }

    row_last_crossed = row_now;
  }

  {
    const int kNumMessagesToSend = 4;
    const float kMessageSpacingSeconds = 0.4f;

    PlayerNumber pn = kPlayerInvalid;
    FOREACH_EnabledPlayerNumberInfo(player_info_, pi) {
      if (GAMESTATE->cur_steps_[pi->pn_]->GetDifficulty() ==
          Difficulty_Beginner) {
        pn = pi->pn_;
        break;
      }
    }
    if (pn == kPlayerInvalid) {
      return;
    }

    const NoteData& nd = player_info_[pn].player_->GetNoteData();

    static int row_last_crossed_all[kNumMessagesToSend] = {0, 0, 0, 0};
    for (int i = 0; i < kNumMessagesToSend; i++) {
      float note_will_cross_in_seconds = kMessageSpacingSeconds * i;

      float position_seconds =
          GAMESTATE->position_.music_seconds_ + note_will_cross_in_seconds;
      float song_beat =
          GAMESTATE->cur_song_->song_timing_.GetBeatFromElapsedTime(
              position_seconds);

      int row_now = BeatToNoteRowNotRounded(song_beat);
      row_now = std::max(0, row_now);
      int& row_last_crossed = row_last_crossed_all[i];

      FOREACH_NONEMPTY_ROW_ALL_TRACKS_RANGE(
          nd, r, row_last_crossed + 1, row_now + 1) {
        int num_tracks_with_tap_or_hold_head = 0;
        for (int t = 0; t < nd.GetNumTracks(); t++) {
          if (nd.GetTapNote(t, r).type_ == TapNoteType_Empty) {
            continue;
          }

          num_tracks_with_tap_or_hold_head++;

          // send crossed message
          if (GAMESTATE->GetCurrentGame()->players_have_separate_styles_) {
            FOREACH_EnabledPlayerNumberInfo(player_info_, pi) {
              const Style* style = GAMESTATE->GetCurrentStyle(pi->pn_);
              std::string button = style->ColToButtonName(t);
              Message msg(i == 0 ? "NoteCrossed" : "NoteWillCross");
              msg.SetParam("ButtonName", button);
              msg.SetParam("NumMessagesFromCrossed", i);
              msg.SetParam("PlayerNumber", pi->pn_);
              MESSAGEMAN->Broadcast(msg);
            }
          } else {
            const Style* style = GAMESTATE->GetCurrentStyle(kPlayerInvalid);
            std::string button = style->ColToButtonName(t);
            Message msg(i == 0 ? "NoteCrossed" : "NoteWillCross");
            msg.SetParam("ButtonName", button);
            msg.SetParam("NumMessagesFromCrossed", i);
            MESSAGEMAN->Broadcast(msg);
          }
        }

        if (num_tracks_with_tap_or_hold_head > 0) {
          MESSAGEMAN->Broadcast((MessageID)(Message_NoteCrossed + i));
        }
        if (i == 0 && num_tracks_with_tap_or_hold_head >= 2) {
          std::string message_name = "NoteCrossedJump";
          MESSAGEMAN->Broadcast(message_name);
        }
      }

      row_last_crossed = row_now;
    }
  }
}

void ScreenGameplay::BeginBackingOutFromGameplay() {
  dancing_state_ = STATE_OUTRO;
  ResetGiveUpTimers(false);

  sound_music_->StopPlaying();
  gameplay_assist_.StopPlaying();  // Stop any queued assist ticks.

  this->ClearMessageQueue();

  // If this is the final stage, don't allow extra stage
  if (GAMESTATE->GetSmallestNumStagesLeftForAnyHumanPlayer() == 0) {
    GAMESTATE->backed_out_of_final_stage_ = true;
  }
  // Disallow backing out of extra stage
  if (GAMESTATE->IsAnExtraStage()) {
    SCREENMAN->PostMessageToTopScreen(SM_BeginFailed, 0);
  } else {
    cancel_.StartTransitioning(SM_DoPrevScreen);
  }
}

void ScreenGameplay::AbortGiveUpText(bool show_abort_text) {
  text_debug_.StopTweening();
  if (show_abort_text) {
    text_debug_.SetText(GIVE_UP_ABORTED_TEXT);
  }
  // otherwise tween out the text that's there

  text_debug_.BeginTweening(1 / 2.f);
  text_debug_.SetDiffuse(RageColor(1, 1, 1, 0));
}

void ScreenGameplay::AbortSkipSong(bool show_text) {
  if (skip_song_timer_.IsZero()) {
    return;
  }
  AbortGiveUpText(show_text);
  skip_song_timer_.SetZero();
}

void ScreenGameplay::AbortGiveUp(bool show_text) {
  if (give_up_timer_.IsZero()) {
    return;
  }
  AbortGiveUpText(show_text);
  give_up_timer_.SetZero();
}

void ScreenGameplay::ResetGiveUpTimers(bool show_text) {
  AbortSkipSong(show_text);
  AbortGiveUp(show_text);
}

bool ScreenGameplay::Input(const InputEventPlus& input) {
  // LOG->Trace( "ScreenGameplay::Input()" );

  Message msg("");
  if (codes_.InputMessage(input, msg)) {
    this->HandleMessage(msg);
  }

  if (paused_) {
    /* If we're paused, only accept GAME_BUTTON_START to unpause. */
    if (GAMESTATE->IsHumanPlayer(input.pn_) &&
        input.menu_i_ == GAME_BUTTON_START && input.type_ == IET_FIRST_PRESS) {
      if (pause_controller_ == GameController_Invalid ||
          pause_controller_ == input.game_i_.controller_) {
        // IMO, it's better to have this configurable. -DaisuMaster
        if (UNPAUSE_WITH_START) {
          this->PauseGame(false);
          return true;
        }
      }
    }
    return false;
  }

  if (input.menu_i_ == GAME_BUTTON_RESTART && input.type_ == IET_FIRST_PRESS &&
      GAMESTATE->IsEventMode() && !GAMESTATE->IsCourseMode()) {
    return MenuRestart(input);
  }

  if (dancing_state_ != STATE_OUTRO && GAMESTATE->IsHumanPlayer(input.pn_) &&
      !cancel_.IsTransitioning()) {
    /* Allow bailing out by holding any START button.
     * This gives a way to "give up" when a back button isn't available.
     * If this is also a style button, don't do this; pump center is start.
     */
    bool holding_give_up = false;
    if (GAMESTATE->GetCurrentStyle(input.pn_)->GameInputToColumn(
            input.game_i_) == kColumnInvalid) {
      holding_give_up |= (START_GIVES_UP && input.menu_i_ == GAME_BUTTON_START);
      holding_give_up |= (BACK_GIVES_UP && input.menu_i_ == GAME_BUTTON_BACK);
    }
    // Allow holding SELECT to skip the current song in course mode. -Kyz
    if (GAMESTATE->IsCourseMode() && SELECT_SKIPS_SONG &&
        input.menu_i_ == GAME_BUTTON_SELECT) {
      if (input.type_ == IET_RELEASE) {
        AbortSkipSong(true);
      } else if (input.type_ == IET_FIRST_PRESS && skip_song_timer_.IsZero()) {
        text_debug_.SetText(SKIP_SONG_TEXT);
        text_debug_.PlayCommand("StartOn");
        skip_song_timer_.Touch();
      }
      return true;
    }

    if (holding_give_up) {
      // No PREFSMAN->m_bDelayedEscape; always delayed.
      if (input.type_ == IET_RELEASE) {
        AbortGiveUp(true);
      } else if (input.type_ == IET_FIRST_PRESS && give_up_timer_.IsZero()) {
        text_debug_.SetText(GIVE_UP_START_TEXT);
        text_debug_.PlayCommand("StartOn");
        give_up_timer_.Touch();  // start the timer
      }

      return true;
    }

    /* Only handle GAME_BUTTON_BACK as a regular BACK button if BACK_GIVES_UP is
     * disabled. */
    bool holding_back = false;
    if (GAMESTATE->GetCurrentStyle(input.pn_)->GameInputToColumn(
            input.game_i_) == kColumnInvalid) {
      holding_back |= input.menu_i_ == GAME_BUTTON_BACK && !BACK_GIVES_UP;
    }

    if (holding_back) {
      if (((!PREFSMAN->delayed_back_ && input.type_ == IET_FIRST_PRESS) ||
           (input.device_i_.device_ == DEVICE_KEYBOARD &&
            input.type_ == IET_REPEAT) ||
           (input.device_i_.device_ != DEVICE_KEYBOARD &&
            INPUTFILTER->GetSecsHeld(input.device_i_) >= 1.0f))) {
        LOG->Trace("Player %i went back", input.pn_ + 1);
        BeginBackingOutFromGameplay();
      } else if (PREFSMAN->delayed_back_ && input.type_ == IET_FIRST_PRESS) {
        text_debug_.SetText(GIVE_UP_BACK_TEXT);
        text_debug_.PlayCommand("BackOn");
      } else if (PREFSMAN->delayed_back_ && input.type_ == IET_RELEASE) {
        text_debug_.PlayCommand("TweenOff");
      }

      return true;
    }
  }

  bool release = input.type_ == IET_RELEASE;
  if (!input.game_i_.IsValid()) {
    return false;
  }

  int col =
      GAMESTATE->GetCurrentStyle(input.pn_)->GameInputToColumn(input.game_i_);

  // Don't pass on any inputs to Player that aren't a press or a release.
  switch (input.type_) {
    case IET_FIRST_PRESS:
    case IET_RELEASE:
      break;
    default:
      return false;
  }

  // If we are in routine mode, two players share sides so a step could
  // correspond to either P1 or P2, we don't really know Let's just send it to
  // both players and let them decide what to do with it.
  if (GAMESTATE->GetCurrentStyle(GAMESTATE->GetMasterPlayerNumber())
          ->style_type_ == StyleType_TwoPlayersSharedSides) {
    if (GAMESTATE->IsHumanPlayer(input.pn_)) {
      if (GamePreferences::auto_play == PC_HUMAN &&
          GAMESTATE->player_state_[input.pn_]
                  ->player_options_.GetCurrent()
                  .player_auto_play_ == 0) {
        ASSERT(input.game_i_.IsValid());

        GameButtonType gbt =
            GAMESTATE->cur_game_->GetPerButtonInfo(input.game_i_.button_)->gbt_;
        switch (gbt) {
          case GameButtonType_Menu:
            return false;
          case GameButtonType_Step:
            if (col != -1) {
              player_info_[PLAYER_1].player_->Step(
                  col, -1, input.device_i_.ts_, false, release);
              player_info_[PLAYER_2].player_->Step(
                  col, -1, input.device_i_.ts_, false, release);
            }
            return true;
        }
      }
    }
  } else {
    // handle a step or battle item activate
    if (GAMESTATE->IsHumanPlayer(input.pn_)) {
      ResetGiveUpTimers(true);

      if (GamePreferences::auto_play == PC_HUMAN &&
          GAMESTATE->player_state_[input.pn_]
                  ->player_options_.GetCurrent()
                  .player_auto_play_ == 0) {
        PlayerInfo& pi = GetPlayerInfoForInput(input);

        ASSERT(input.game_i_.IsValid());

        GameButtonType gbt =
            GAMESTATE->cur_game_->GetPerButtonInfo(input.game_i_.button_)->gbt_;
        switch (gbt) {
          case GameButtonType_Menu:
            return false;
          case GameButtonType_Step:
            if (col != -1) {
              pi.player_->Step(col, -1, input.device_i_.ts_, false, release);
            }
            return true;
        }
      }
    }
  }
  return false;
}

/* Saving StageStats that are affected by the note pattern is a little tricky:
 *
 * Stats are cumulative for course play.
 *
 * For regular songs, it doesn't matter how we do it; the pattern doesn't change
 * during play.
 *
 * The pattern changes during play in battle and course mode. We want to include
 * these changes, so run stats for a song after the song finishes.
 *
 * If we fail, be sure to include the current song in stats,
 * with the current modifier set. So:
 * 1. At the end of a song in any mode, pass or fail, add stats for that song
 *    (from m_pPlayer).
 * 2. At the end of gameplay in course mode, add stats for any songs that
 * weren't played, applying the modifiers the song would have been played with.
 *    This doesn't include songs that were played but failed; that was done in
 * #1.
 */
void ScreenGameplay::SaveStats() {
  float music_len = GAMESTATE->cur_song_->music_length_seconds_;

  FOREACH_EnabledPlayerInfo(player_info_, pi) {
    /* Note that adding stats is only meaningful for the counters (eg.
     * RadarCategory_Jumps), not for the percentages (RadarCategory_Air). */
    RadarValues rv;
    PlayerStageStats& pss = *pi->GetPlayerStageStats();
    const NoteData& nd = pi->player_->GetNoteData();
    PlayerNumber pn = pi->pn_;

    GAMESTATE->SetProcessedTimingData(
        GAMESTATE->cur_steps_[pn]->GetTimingData());
    NoteDataUtil::CalculateRadarValues(nd, music_len, rv);
    pss.radar_possible_ += rv;
    NoteDataWithScoring::GetActualRadarValues(nd, pss, music_len, rv);
    pss.radar_actual_ += rv;
    GAMESTATE->SetProcessedTimingData(nullptr);
  }
}

void ScreenGameplay::SongFinished() {
  AdjustSync::HandleSongEnd();
  SaveStats();  // Let subclasses save the stats.
  /* Extremely important: if we don't remove attacks before moving on to the
   * next screen, they'll still be turned on eventually. */
  GAMESTATE->RemoveAllActiveAttacks();
  FOREACH_VisiblePlayerInfo(player_info_, pi)
      pi->active_attack_list_->Refresh();
}

void ScreenGameplay::StageFinished(bool backed_out) {
  if (GAMESTATE->IsCourseMode() && GAMESTATE->play_mode_ != PLAY_MODE_ENDLESS) {
    LOG->Trace(
        "Stage finished at index %i/%i", GAMESTATE->GetCourseSongIndex(),
        (int)songs_queue_.size());
    // +1 to skip the current song; that song has already passed.
    for (unsigned i = GAMESTATE->GetCourseSongIndex() + 1;
         i < songs_queue_.size(); ++i) {
      LOG->Trace("Running stats for %i", i);
      SetupSong(i);
      FOREACH_EnabledPlayerInfo(player_info_, pi)
          pi->player_->ApplyWaitingTransforms();
      SongFinished();
    }
  }

  if (backed_out) {
    GAMESTATE->CancelStage();
    return;
  }

  // If all players failed, kill.
  if (STATSMAN->cur_stage_stats_.AllFailed()) {
    FOREACH_HumanPlayer(p) GAMESTATE->player_stage_tokens_[p] = 0;
  }

  FOREACH_HumanPlayer(pn) STATSMAN->cur_stage_stats_.player_[pn].CalcAwards(
      pn, STATSMAN->cur_stage_stats_.gave_up_,
      STATSMAN->cur_stage_stats_.used_autoplay_);
  STATSMAN->cur_stage_stats_.FinalizeScores(false);

  GAMESTATE->CommitStageStats();

  // save current stage stats
  STATSMAN->played_stage_stats_.push_back(STATSMAN->cur_stage_stats_);

  STATSMAN->CalcAccumPlayedStageStats();
  GAMESTATE->FinishStage();
}

void ScreenGameplay::HandleScreenMessage(const ScreenMessage sm) {
  CHECKPOINT_M(ssprintf(
      "HandleScreenMessage(%s)",
      ScreenMessageHelpers::ScreenMessageToString(sm).c_str()));
  if (sm == SM_DoneFadingIn) {
    // If the ready animation is zero length, then playing the sound will
    // make it overlap with the go sound.
    // If the Ready animation is zero length, and the Go animation is not,
    // only play the Go sound.
    // If they're both zero length, only play the Ready sound.
    // Otherwise, play both sounds.
    // -Kyz
    ready_.StartTransitioning(SM_PlayGo);
    if (ready_.GetTweenTimeLeft() <= .0f) {
      delaying_ready_announce_ = true;
    } else {
      delaying_ready_announce_ = false;
      SOUND->PlayOnceFromAnnouncer("gameplay ready");
    }
  } else if (sm == SM_PlayGo) {
    go_.StartTransitioning(SM_None);
    bool should_play_go = true;
    if (delaying_ready_announce_) {
      if (go_.GetTweenTimeLeft() <= .0f) {
        SOUND->PlayOnceFromAnnouncer("gameplay ready");
        should_play_go = false;
      } else {
        should_play_go = true;
      }
    }
    if (should_play_go) {
      if (GAMESTATE->IsAnExtraStage()) {
        SOUND->PlayOnceFromAnnouncer("gameplay here we go extra");
      } else if (GAMESTATE->GetSmallestNumStagesLeftForAnyHumanPlayer() == 0) {
        SOUND->PlayOnceFromAnnouncer("gameplay here we go final");
      } else {
        SOUND->PlayOnceFromAnnouncer("gameplay here we go normal");
      }
    }

    GAMESTATE->dance_start_time_.Touch();

    GAMESTATE->gameplay_lead_in_.Set(false);
    dancing_state_ =
        STATE_DANCING;  // STATE CHANGE!  Now the user is allowed to press Back
  } else if (sm == SM_NotesEnded)  // received while STATE_DANCING
  {
    ResetGiveUpTimers(
        false);  // don't allow giveup while the next song is loading

    /* Do this in LoadNextSong, so we don't tween off old attacks until
     * m_NextSong finishes. */
    // GAMESTATE->RemoveAllActiveAttacks();

    FOREACH_EnabledPlayerInfo(player_info_, pi) {
      // Mark failure.
      if (GAMESTATE->GetPlayerFailType(pi->GetPlayerState()) != FailType_Off &&
          (pi->life_meter_ && pi->life_meter_->IsFailing())) {
        pi->GetPlayerStageStats()->failed_ = true;
      }

      if (!pi->GetPlayerStageStats()->failed_) {
        pi->GetPlayerStageStats()->songs_passed_++;
      }

      // set a life record at the point of failure
      if (pi->GetPlayerStageStats()->failed_) {
        pi->GetPlayerStageStats()->SetLifeRecordAt(
            0, STATSMAN->cur_stage_stats_.gameplay_seconds_);
      }
    }

    /* If all players have *really* failed (bFailed, not the life meter or
     * bFailedEarlier): */
    const bool all_really_failed = STATSMAN->cur_stage_stats_.AllFailed();
    const bool stop_course_early = STOP_COURSE_EARLY;
    const bool is_last_song = IsLastSong();

    LOG->Trace(
        "bAllReallyFailed = %d, bStopCourseEarly = %d, "
        "bIsLastSong = %d, gave_up_ = %d, skipped_song_ = %d",
        all_really_failed, stop_course_early, is_last_song, gave_up_,
        skipped_song_);

    if (!is_last_song && skipped_song_) {
      // Load the next song in the course.
      HandleScreenMessage(SM_StartLoadingNextSong);
      return;
    }
    if (stop_course_early || all_really_failed || is_last_song || gave_up_) {
      // Time to leave from ScreenGameplay
      HandleScreenMessage(SM_LeaveGameplay);
    } else {
      // Load the next song in the course.
      HandleScreenMessage(SM_StartLoadingNextSong);
      return;
    }
  } else if (sm == SM_LeaveGameplay) {
    GAMESTATE->dance_duration_ = GAMESTATE->dance_start_time_.Ago();
    // End round.
    if (dancing_state_ == STATE_OUTRO) {  // ScreenGameplay already ended
      return;                             // ignore
    }
    dancing_state_ = STATE_OUTRO;
    ResetGiveUpTimers(false);

    GAMESTATE->RemoveAllActiveAttacks();
    FOREACH_EnabledPlayerInfo(player_info_, pi) {
      if (pi->active_attack_list_) {
        pi->active_attack_list_->Refresh();
      }
    }

    LIGHTSMAN->SetLightsMode(LIGHTSMODE_ALL_CLEARED);

    bool all_really_failed = STATSMAN->cur_stage_stats_.AllFailed();

    if (all_really_failed) {
      this->PostScreenMessage(SM_BeginFailed, 0);
      return;
    }

    // todo: add GameplayCleared, StartTransitioningCleared commands -aj
    TweenOffScreen();

    out_.StartTransitioning(SM_DoNextScreen);

    // do they deserve an extra stage?
    if (GAMESTATE->HasEarnedExtraStage()) {
      SOUND->PlayOnceFromAnnouncer("gameplay extra");
    } else {
      SOUND->PlayOnceFromAnnouncer("gameplay cleared");
    }
  } else if (sm == SM_StartLoadingNextSong) {
    // Next song.
    // give a little life back between stages
    FOREACH_EnabledPlayerInfo(player_info_, pi) {
      if (pi->life_meter_) {
        pi->life_meter_->OnSongEnded();
      }
    }

    // If this is a repeating course, and we're at the end of it, repick and
    // add new songs to the players' step and song queues.
    if (GAMESTATE->IsCourseMode() && GAMESTATE->cur_course_ &&
        GAMESTATE->cur_course_->repeat_ &&
        GAMESTATE->GetCourseSongIndex() >= (int)songs_queue_.size() - 1) {
      Course* course = GAMESTATE->cur_course_;
      ASSERT(course != nullptr);
      // Need to store these so they can be used to refetch the players'
      // trails after they're invalidated.
      std::vector<StepsType> trail_sts;
      std::vector<CourseDifficulty> trail_cds;
      FOREACH_EnabledPlayerInfo(player_info_, pi) {
        Trail* trail = GAMESTATE->cur_trail_[pi->GetStepsAndTrailIndex()];
        ASSERT(trail != nullptr);
        trail_sts.push_back(trail->steps_type_);
        trail_cds.push_back(trail->course_difficulty_);
      }
      // Set a new stage seed so the order will be different.
      GAMESTATE->SetNewStageSeed();
      course->InvalidateTrailCache();
      course->RegenerateNonFixedTrails();
      size_t info_id = 0;  // Can't use the player number in the playerinfo
      // because it won't match up in 2-player.
      FOREACH_EnabledPlayerInfo(player_info_, pi) {
        Trail* trail = course->GetTrail(trail_sts[info_id], trail_cds[info_id]);
        ASSERT(trail != nullptr);
        GAMESTATE->cur_trail_[pi->pn_].Set(trail);
        ++info_id;
      }
      PlayerNumber master_pn = GAMESTATE->GetMasterPlayerNumber();
      Trail* master_trail = GAMESTATE->cur_trail_[master_pn];
      ASSERT(master_trail != nullptr);
      for (const TrailEntry& entry : master_trail->entries_) {
        ASSERT(entry.song_ != nullptr);
        songs_queue_.push_back(entry.song_);
        STATSMAN->cur_stage_stats_.possible_songs_.push_back(entry.song_);
      }
      FOREACH_EnabledPlayerInfo(player_info_, pi) {
        Trail* trail = GAMESTATE->cur_trail_[pi->GetStepsAndTrailIndex()];
        ASSERT(trail != nullptr);
        for (const TrailEntry& entry : master_trail->entries_) {
          ASSERT(entry.steps_ != nullptr);
          pi->steps_queue_.push_back(entry.steps_);
          AttackArray a;
          entry.GetAttackArray(a);
          pi->modifiers_queue_.push_back(a);
          pi->GetPlayerStageStats()->possible_steps_.push_back(entry.steps_);
        }
        // In a survival course, override stored mods
        if (course->GetCourseType() == COURSE_TYPE_SURVIVAL &&
            SURVIVAL_MOD_OVERRIDE) {
          pi->GetPlayerState()->player_options_.FromString(
              ModsLevel_Stage,
              "clearall," + CommonMetrics::DEFAULT_NOTESKIN_NAME.GetValue() +
                  "," + CommonMetrics::DEFAULT_MODIFIERS.GetValue());
          pi->GetPlayerState()->RebuildPlayerOptionsFromActiveAttacks();
        }
      }
    }

    GAMESTATE->loading_next_song_ = true;
    MESSAGEMAN->Broadcast("BeforeLoadingNextCourseSong");
    next_song_.Reset();
    next_song_.PlayCommand("Start");
    next_song_.StartTransitioning(SM_LoadNextSong);
    MESSAGEMAN->Broadcast("ChangeCourseSongIn");
  } else if (sm == SM_LoadNextSong) {
    sound_music_->Stop();
    SongFinished();

    MESSAGEMAN->Broadcast("ChangeCourseSongOut");

    GAMESTATE->loading_next_song_ = false;
    LoadNextSong();

    next_song_.Reset();
    next_song_.PlayCommand("Finish");
    next_song_.StartTransitioning(SM_None);

    StartPlayingSong(MIN_SECONDS_TO_STEP_NEXT_SONG, 0);
  } else if (sm == SM_PlayToasty) {
    if (easter_eggs) {
      if (PREFSMAN->allow_multiple_toasties_ || toasty_.IsWaiting()) {
        toasty_.Reset();
        toasty_.StartTransitioning();
      }
    }
  } else if (
      ScreenMessageHelpers::ScreenMessageToString(sm).find("0Combo") !=
      std::string::npos) {
    int combo;
    std::string cropped =
        ScreenMessageHelpers::ScreenMessageToString(sm).substr(3);
    sscanf(cropped.c_str(), "%d%*s", &combo);
    PlayAnnouncer(ssprintf("gameplay %d combo", combo), 2);
  } else if (sm == SM_ComboStopped) {
    PlayAnnouncer("gameplay combo stopped", 2);
  } else if (sm == SM_ComboContinuing) {
    PlayAnnouncer("gameplay combo overflow", 2);
  } else if (sm == SM_DoPrevScreen) {
    SongFinished();
    this->StageFinished(true);

    next_screen_ = GetPrevScreen();

    if (AdjustSync::IsSyncDataChanged()) {
      ScreenSaveSync::PromptSaveSync(SM_GoToPrevScreen);
    } else {
      HandleScreenMessage(SM_GoToPrevScreen);
    }
  } else if (sm == SM_DoNextScreen) {
    SongFinished();
    this->StageFinished(false);
    // only save replays if the player chose to
    if (GAMESTATE->song_options_.GetCurrent().save_replay_) {
      SaveReplay();
    }

    if (AdjustSync::IsSyncDataChanged()) {
      ScreenSaveSync::PromptSaveSync(SM_GoToNextScreen);
    } else {
      HandleScreenMessage(SM_GoToNextScreen);
    }
  } else if (sm == SM_GainFocus) {
    // We do this ourself.
    SOUND->HandleSongTimer(false);
  } else if (sm == SM_LoseFocus) {
    // We might have turned the song timer off. Be sure to turn it back on.
    SOUND->HandleSongTimer(true);
  } else if (sm == SM_BeginFailed) {
    dancing_state_ = STATE_OUTRO;
    ResetGiveUpTimers(false);
    gameplay_assist_.StopPlaying();  // Stop any queued assist ticks.
    TweenOffScreen();
    failed_.StartTransitioning(SM_DoNextScreen);

    // show the survive time if extra stage
    if (GAMESTATE->IsAnExtraStage()) {
      float max_alive_seconds = 0;
      FOREACH_EnabledPlayer(p) max_alive_seconds = std::max(
          max_alive_seconds,
          STATSMAN->cur_stage_stats_.player_[p].alive_seconds_);
      text_survive_time_.SetText(
          "TIME: " + SecondsToMMSSMsMs(max_alive_seconds));
      ON_COMMAND(text_survive_time_);
    }

    if (GAMESTATE->IsCourseMode()) {
      if (GAMESTATE->GetCourseSongIndex() >= int(songs_queue_.size() / 2)) {
        SOUND->PlayOnceFromAnnouncer("gameplay oni failed halfway");
      } else {
        SOUND->PlayOnceFromAnnouncer("gameplay oni failed");
      }
    } else {
      SOUND->PlayOnceFromAnnouncer("gameplay failed");
    }
  } else if (sm == SM_Pause) {
    // Ignore SM_Pause when in demonstration.
    if (GAMESTATE->demonstration_or_jukebox_) {
      return;
    }

    if (!paused_) {
      PauseGame(true);
    }
  }

  ScreenWithMenuElements::HandleScreenMessage(sm);
}

void ScreenGameplay::HandleMessage(const Message& msg) {
  if (msg == "Judgment") {
    PlayerNumber pn;
    msg.GetParam("Player", pn);

    FOREACH_EnabledPlayerNumberInfo(player_info_, pi) {
      if (pi->pn_ != pn) {
        continue;
      }
      if (!pi->GetPlayerState()->player_options_.GetCurrent().mute_on_error_) {
        continue;
      }

      RageSoundReader* sound_reader = auto_keysounds_.GetSharedSound();

      HoldNoteScore hns;
      msg.GetParam("HoldNoteScore", hns);
      TapNoteScore tns;
      msg.GetParam("TapNoteScore", tns);

      bool on = false;
      if (hns != HoldNoteScore_Invalid) {
        on = hns != HNS_LetGo;
      } else {
        on = tns != TNS_Miss;
      }

      if (sound_reader) {
        sound_reader->SetProperty("Volume", on ? 1.0f : 0.0f);
      }
    }
  }

  ScreenWithMenuElements::HandleMessage(msg);
}

void ScreenGameplay::Cancel(ScreenMessage send_when_done) {
  sound_music_->Stop();

  ScreenWithMenuElements::Cancel(send_when_done);
}

Song* ScreenGameplay::GetNextCourseSong() const {
  ASSERT(GAMESTATE->IsCourseMode());

  int play_song_index = GAMESTATE->GetCourseSongIndex() + 1;
  play_song_index %= songs_queue_.size();

  return songs_queue_[play_song_index];
}

PlayerInfo* ScreenGameplay::GetPlayerInfo(PlayerNumber pn) {
  FOREACH_EnabledPlayerNumberInfo(player_info_, pi) {
    if (pi->pn_ == pn) {
      return &*pi;
    }
  }
  return nullptr;
}

PlayerInfo* ScreenGameplay::GetDummyPlayerInfo(int dummy_index) {
  FOREACH_EnabledPlayerInfo(player_info_, pi) {
    if (pi->is_dummy_ && pi->dummy_index_ == dummy_index) {
      return &*pi;
    }
  }
  return nullptr;
}

void ScreenGameplay::SaveReplay() {
  /* Replay data TODO:
   * Add more player information (?)
   * Add AutoGen flag if steps were autogen?
   * Add proper steps hash?
   * Add modifiers used
   * Add date played, machine played on, etc.
   * Hash of some stuff to validate data (see Profile)
   */
  FOREACH_HumanPlayer(pn) {
    FOREACH_EnabledPlayerInfo(player_info_, pi) {
      Profile* temp_profile = PROFILEMAN->GetProfile(pn);

      XNode* p = new XNode("ReplayData");
      // append version number (in case the format changes)
      p->AppendAttr("Version", 0);

      // song information node
      SongID song_id;
      song_id.FromSong(GAMESTATE->cur_song_);
      XNode* song_info_node = song_id.CreateNode();
      song_info_node->AppendChild(
          "Title", GAMESTATE->cur_song_->GetDisplayFullTitle());
      song_info_node->AppendChild(
          "Artist", GAMESTATE->cur_song_->GetDisplayArtist());
      p->AppendChild(song_info_node);

      // steps information
      StepsID steps_id;
      steps_id.FromSteps(GAMESTATE->cur_steps_[pn]);
      XNode* steps_info_node = steps_id.CreateNode();
      // hashing = argh
      // pStepsInfoNode->AppendChild("StepsHash",
      // stepsID.ToSteps(GAMESTATE->m_pCurSong,false)->GetHash());
      p->AppendChild(steps_info_node);

      // player information node (rival data sup)
      XNode* player_info_node = new XNode("Player");
      player_info_node->AppendChild("DisplayName", temp_profile->display_name_);
      player_info_node->AppendChild("Guid", temp_profile->guid_);
      p->AppendChild(player_info_node);

      // the timings.
      p->AppendChild(pi->player_->GetNoteData().CreateNode());

      // Find a file name for the replay
      std::vector<std::string> files;
      GetDirListing("Save/Replays/replay*", files, false, false);
      sort(files.begin(), files.end());

      // Files should be of the form "replay#####.xml".
      int index = 0;

      for (int i = files.size() - 1; i >= 0; --i) {
        static Regex re("^replay([0-9]{5})\\....$");
        std::vector<std::string> matches;
        if (!re.Compare(files[i], matches)) {
          continue;
        }

        ASSERT(matches.size() == 1);
        index = StringToInt(matches[0]) + 1;
        break;
      }

      std::string file_name = ssprintf("replay%05d.xml", index);

      XmlFileUtil::SaveToFile(p, "Save/Replays/" + file_name);
      RageUtil::SafeDelete(p);
      return;
    }
  }
}

/*
bool ScreenGameplay::LoadReplay()
{
        // Load replay which was selected via options
}
*/

// lua start
#include "LuaBinding.h"
#include "OptionsBinding.h"

/** @brief Allow Lua to have access to the ScreenGameplay. */
class LunaScreenGameplay : public Luna<ScreenGameplay> {
 public:
  static int GetNextCourseSong(T* p, lua_State* L) {
    p->GetNextCourseSong()->PushSelf(L);
    return 1;
  }
  static int Center1Player(T* p, lua_State* L) {
    lua_pushboolean(L, p->Center1Player());
    return 1;
  }
  static int GetLifeMeter(T* p, lua_State* L) {
    PlayerNumber pn = Enum::Check<PlayerNumber>(L, 1);

    PlayerInfo* pi = p->GetPlayerInfo(pn);
    if (pi == nullptr) {
      return 0;
    }
    LifeMeter* lm = pi->life_meter_;
    if (lm == nullptr && p->GetCombinedLifeMeter() != nullptr) {
      lm = p->GetCombinedLifeMeter()->GetInnerLifeMeter();
    }
    if (lm == nullptr) {
      return 0;
    }

    lm->PushSelf(L);
    return 1;
  }
  static int GetPlayerInfo(T* p, lua_State* L) {
    PlayerNumber pn = Enum::Check<PlayerNumber>(L, 1);

    PlayerInfo* pi = p->GetPlayerInfo(pn);
    if (pi == nullptr) {
      return 0;
    }

    pi->PushSelf(L);
    return 1;
  }
  static int GetDummyPlayerInfo(T* p, lua_State* L) {
    int dummy_index = IArg(1);
    PlayerInfo* pi = p->GetDummyPlayerInfo(dummy_index);
    if (pi == nullptr) {
      return 0;
    }
    pi->PushSelf(L);
    return 1;
  }
  static int PauseGame(T* p, lua_State* L) {
    p->Pause(BArg(1));
    return 0;
  }
  static int IsPaused(T* p, lua_State* L) {
    lua_pushboolean(L, p->IsPaused());
    return 1;
  }
  static int GetHasteRate(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetHasteRate());
    return 1;
  }
  static bool TurningPointsValid(lua_State* L, int index) {
    size_t size = lua_objlen(L, index);
    if (size < 2) {
      luaL_error(L, "Invalid number of entries %zu", size);
    }
    float prev_turning = -1;
    for (size_t n = 1; n < size; ++n) {
      lua_pushnumber(L, n);
      lua_gettable(L, index);
      float v = FArg(-1);
      if (v < prev_turning || v > 1) {
        luaL_error(L, "Invalid value %f", v);
      }
      lua_pop(L, 1);
    }
    return true;
  }
  static bool AddAmountsValid(lua_State* L, int index) {
    return TurningPointsValid(L, index);
  }
  FLOAT_TABLE_INTERFACE(
      HasteTurningPoints, haste_turning_points_, TurningPointsValid);
  FLOAT_TABLE_INTERFACE(HasteAddAmounts, haste_add_amounts_, AddAmountsValid);
  FLOAT_NO_SPEED_INTERFACE(
      HasteTimeBetweenUpdates, haste_time_between_updates_, (v > 0));
  FLOAT_NO_SPEED_INTERFACE(
      HasteLifeSwitchPoint, haste_life_switch_point_, (v >= 0 && v <= 1));
  static int begin_backing_out(T* p, lua_State* L) {
    p->BeginBackingOutFromGameplay();
    COMMON_RETURN_SELF;
  }
  static int GetTrueBPS(T* p, lua_State* L) {
    PlayerNumber pn = Enum::Check<PlayerNumber>(L, 1);
    float haste = p->GetHasteRate();
    float rate = GAMESTATE->song_options_.GetCurrent().music_rate_;
    float bps = GAMESTATE->player_state_[pn]->position_.cur_bps_;
    float true_bps = haste * rate * bps;
    lua_pushnumber(L, true_bps);
    return 1;
  }

  LunaScreenGameplay() {
    ADD_METHOD(GetNextCourseSong);
    ADD_METHOD(Center1Player);
    ADD_METHOD(GetLifeMeter);
    ADD_METHOD(GetPlayerInfo);
    ADD_METHOD(GetDummyPlayerInfo);
    // sm-ssc additions:
    ADD_METHOD(PauseGame);
    ADD_METHOD(IsPaused);
    ADD_METHOD(GetHasteRate);
    ADD_METHOD(HasteTurningPoints);
    ADD_METHOD(HasteAddAmounts);
    ADD_METHOD(HasteTimeBetweenUpdates);
    ADD_METHOD(HasteLifeSwitchPoint);
    ADD_METHOD(begin_backing_out);
    ADD_METHOD(GetTrueBPS);
  }
};

LUA_REGISTER_DERIVED_CLASS(ScreenGameplay, ScreenWithMenuElements)

/** @brief Allow Lua to have access to the PlayerInfo. */
class LunaPlayerInfo : public Luna<PlayerInfo> {
 public:
  static int GetLifeMeter(T* p, lua_State* L) {
    if (p->life_meter_) {
      p->life_meter_->PushSelf(L);
      return 1;
    }
    return 0;
  }

  static int GetStepsQueueWrapped(T* p, lua_State* L) {
    int index = IArg(1);
    index %= p->steps_queue_.size();
    Steps* steps = p->steps_queue_[index];
    steps->PushSelf(L);
    return 1;
  }

  LunaPlayerInfo() {
    ADD_METHOD(GetLifeMeter);
    ADD_METHOD(GetStepsQueueWrapped);
  }
};

LUA_REGISTER_CLASS(PlayerInfo)
// lua end

/*
 * (c) 2001-2004 Chris Danford, Glenn Maynard
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
