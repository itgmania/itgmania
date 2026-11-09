#include "Player.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstddef>
#include <set>
#include <string>
#include <vector>

#include "ActorFrame.h"
#include "ActorUtil.h"
#include "AdjustSync.h"
#include "ArrowEffects.h"
#include "Attack.h"
#include "AttackDisplay.h"
#include "CombinedLifeMeter.h"
#include "Game.h"
#include "GameCommand.h"
#include "GameConstantsAndTypes.h"
#include "GameInput.h"
#include "GameManager.h"
#include "GameSoundManager.h"
#include "GameState.h"
#include "HoldJudgment.h"
#include "InputMapper.h"
#include "LifeMeter.h"
#include "LuaManager.h"
#include "MessageManager.h"
#include "NoteDataUtil.h"
#include "NoteDataWithScoring.h"
#include "NoteField.h"
#include "NoteTypes.h"
#include "PlayerAI.h"
#include "PlayerNumber.h"
#include "PlayerOptions.h"
#include "PlayerState.h"
#include "Preference.h"
#include "PrefsManager.h"
#include "Profile.h"
#include "ProfileManager.h"
#include "RageDisplay.h"
#include "RageLog.h"
#include "RageSound.h"
#include "RageSoundManager.h"
#include "RageUtil.h"
#include "RageUtil/RandomNumbers.h"
#include "ScoreDisplay.h"
#include "ScoreKeeperNormal.h"
#include "ScreenDimensions.h"
#include "ScreenManager.h"
#include "ScreenMessage.h"
#include "Song.h"
#include "SongManager.h"
#include "SongPosition.h"
#include "StageStats.h"
#include "StatsManager.h"
#include "Steps.h"
#include "Style.h"
#include "ThemeManager.h"
#include "ThemeMetric.h"
#include "TimingData.h"
#include "TimingSegments.h"
#include "Trail.h"
#include "global.h"

std::string ATTACK_DISPLAY_X_NAME(size_t p, size_t both_sides);
void TimingWindowSecondsInit(
    size_t /*TimingWindow*/ i, std::string& name_out, float& default_value_out);

/**
 * @brief Helper class to ensure that each row is only judged once without
 * taking too much memory.
 */
class JudgedRows {
  std::vector<bool> rows_;
  int start_;
  int offset_;

  void Resize(size_t min) {
    size_t new_size = std::max(2 * rows_.size(), min);
    std::vector<bool> new_rows(rows_.begin() + offset_, rows_.end());
    new_rows.reserve(new_size);
    new_rows.insert(new_rows.end(), rows_.begin(), rows_.begin() + offset_);
    new_rows.resize(new_size, false);
    rows_.swap(new_rows);
    offset_ = 0;
  }

 public:
  JudgedRows() : start_(0), offset_(0) { Resize(32); }
  // Returns true if the row has already been judged.
  bool JudgeRow(int row) {
    if (row < start_) {
      return true;
    }
    if (row >= start_ + int(rows_.size())) {
      Resize(row + 1 - start_);
    }
    const int index = (row - start_ + offset_) % rows_.size();
    const bool ret = rows_[index];
    rows_[index] = true;
    while (rows_[offset_]) {
      rows_[offset_] = false;
      ++start_;
      if (++offset_ >= int(rows_.size())) {
        offset_ -= rows_.size();
      }
    }
    return ret;
  }
  void Reset(int start) {
    start_ = start;
    offset_ = 0;
    rows_.assign(rows_.size(), false);
  }
};

std::string ATTACK_DISPLAY_X_NAME(size_t p, size_t both_sides) {
  return "AttackDisplayXOffset" + (both_sides
                                       ? std::string("BothSides")
                                       : ssprintf("OneSideP%d", int(p + 1)));
}

/**
 * @brief Distance to search for a note in Step(), in seconds.
 *
 * TODO: This should be calculated based on the max size of the current judgment
 * windows. */
static const float kStepSearchDistance = 1.0f;

void TimingWindowSecondsInit(
    size_t /*TimingWindow*/ i, std::string& name_out,
    float& default_value_out) {
  name_out = "TimingWindowSeconds" +
             TimingWindowToString(static_cast<TimingWindow>(i));
  switch (i) {
    case TW_W1:
      default_value_out = 0.0225f;
      break;
    case TW_W2:
      default_value_out = 0.045f;
      break;
    case TW_W3:
      default_value_out = 0.090f;
      break;
    case TW_W4:
      default_value_out = 0.135f;
      break;
    case TW_W5:
      default_value_out = 0.180f;
      break;
    case TW_Mine:  // same as great
      default_value_out = 0.090f;
      break;
    case TW_Hold:  // allow enough time to take foot off and put back on
      default_value_out = 0.250f;
      break;
    case TW_Roll:
      default_value_out = 0.500f;
      break;
    case TW_Attack:
      default_value_out = 0.135f;
      break;
    case TW_Checkpoint:  // similar to TW_Hold, but a little more
                         // strict/accurate to Pump play.
      default_value_out = 0.1664f;
      break;
    default:
      FAIL_M(ssprintf("Invalid timing window: %i", static_cast<int>(i)));
  }
}

void ValidateMinTNSToScoreNotes(TapNoteScore& tns) {
  switch (tns) {
    case TNS_W1:
    case TNS_W2:
    case TNS_W3:
    case TNS_W4:
    case TNS_W5:
    case TNS_None:
      break;
    default:
      tns = TNS_None;
  }
}

static Preference<float> timing_window_scale("TimingWindowScale", 1.0f);
static Preference<float> timing_window_add("TimingWindowAdd", 0);
static Preference1D<float> timing_window_seconds(
    TimingWindowSecondsInit, NUM_TimingWindow);
static Preference<float> timing_window_jump("TimingWindowJump", 0.25);
static Preference<float> max_input_latency_seconds(
    "MaxInputLatencySeconds", 0.0);
static Preference<bool> enable_attack_sound_playback(
    "EnableAttackSounds", true);
static Preference<bool> enable_mine_sound_playback("EnableMineHitSound", true);
static Preference<TapNoteScore> min_tns_to_score_notes(
    "MinTNSToScoreNotes", TNS_None,
    ValidateMinTNSToScoreNotes);  // Default to great and above.

ThemeMetric<bool> TICK_HOLDS("Player", "TickHolds");

/** @brief How much life is in a hold note when you start on it? */
ThemeMetric<float> INITIAL_HOLD_LIFE("Player", "InitialHoldLife");
/**
 * @brief How much hold life is possible to have when holding a hold note?
 *
 * This was an sm-ssc addition. */
ThemeMetric<float> MAX_HOLD_LIFE("Player", "MaxHoldLife");
ThemeMetric<bool> PENALIZE_TAP_SCORE_NONE("Player", "PenalizeTapScoreNone");
ThemeMetric<bool> JUDGE_HOLD_NOTES_ON_SAME_ROW_TOGETHER(
    "Player", "JudgeHoldNotesOnSameRowTogether");
ThemeMetric<bool> CHECKPOINTS_FLASH_ON_HOLD(
    "Player", "CheckpointsFlashOnHold");  // sm-ssc addition
ThemeMetric<bool> IMMEDIATE_HOLD_LET_GO("Player", "ImmediateHoldLetGo");
ThemeMetric<bool> COMBO_BREAK_ON_IMMEDIATE_HOLD_LET_GO(
    "Player", "ComboBreakOnImmediateHoldLetGo");
/**
 * @brief Must a Player step on a hold head for a hold to activate?
 *
 * If set to true, the Player must step on a hold head in order for the hold to
 * activate. If set to false, merely holding your foot down as the hold head
 * approaches will suffice. */
ThemeMetric<bool> REQUIRE_STEP_ON_HOLD_HEADS(
    "Player", "RequireStepOnHoldHeads");
/**
 * @brief Must a Player step on a mine for it to activate?
 *
 * If set to true, the Player must step on a mine for it to blow up.
 * If set to false, merely holding your foot down as the mine approaches will
 * suffice. */
ThemeMetric<bool> REQUIRE_STEP_ON_MINES("Player", "RequireStepOnMines");
// ThemeMetric<bool> HOLD_TRIGGERS_TAP_NOTES	( "Player",
// "HoldTriggersTapNotes" ); // parastar stuff; leave in though
/**
 * @brief Does repeatedly stepping on a roll to keep it alive increment the
 * combo?
 *
 * If set to true, repeatedly stepping on a roll will increment the combo.
 * If set to false, only the roll head causes the combo to be incremented.
 *
 * For those wishing to make a theme very accurate to In The Groove 2, set this
 * to false. */
ThemeMetric<bool> ROLL_BODY_INCREMENTS_COMBO(
    "Player", "RollBodyIncrementsCombo");
/**
 * @brief Does not stepping on a mine increase the combo?
 *
 * If set to true, every mine missed will increment the combo.
 * If set to false, missing a mine will not affect the combo. */
ThemeMetric<bool> AVOID_MINE_INCREMENTS_COMBO(
    "Gameplay", "AvoidMineIncrementsCombo");
/**
 * @brief Does stepping on a mine increment the miss combo?
 *
 * If set to true, every mine stepped on will break the combo and increment the
 * miss combo. If set to false, stepping on a mine will not affect the combo. */
ThemeMetric<bool> MINE_HIT_INCREMENTS_MISS_COMBO(
    "Gameplay", "MineHitIncrementsMissCombo");
/**
 * @brief Are checkpoints and taps considered separate judgments?
 *
 * If set to true, they are considered separate.
 * If set to false, they are considered the same. */
ThemeMetric<bool> CHECKPOINTS_TAPS_SEPARATE_JUDGMENT(
    "Player", "CheckpointsTapsSeparateJudgment");
/**
 * @brief Do we score missed holds and rolls with HoldNoteScores?
 *
 * If set to true, missed holds and rolls are given LetGo judgments.
 * If set to false, missed holds and rolls are given no judgment on the hold
 * side of things. */
ThemeMetric<bool> SCORE_MISSED_HOLDS_AND_ROLLS(
    "Player", "ScoreMissedHoldsAndRolls");
/** @brief How much of the song/course must have gone by before a Player's combo
 * is colored? */
ThemeMetric<float> PERCENT_UNTIL_COLOR_COMBO(
    "Player", "PercentUntilColorCombo");
/** @brief How much combo must be earned before the announcer says "Combo
 * Stopped"? */
ThemeMetric<int> COMBO_STOPPED_AT("Player", "ComboStoppedAt");
ThemeMetric<float> ATTACK_RUN_TIME_RANDOM("Player", "AttackRunTimeRandom");
ThemeMetric<float> ATTACK_RUN_TIME_MINE("Player", "AttackRunTimeMine");

/**
 * @brief What is our highest cap for mMods?
 *
 * If set to 0 or less, assume the song takes over. */
ThemeMetric<float> M_MOD_HIGH_CAP("Player", "MModHighCap");

/** @brief Will battle modes have their steps mirrored or kept the same? */
ThemeMetric<bool> BATTLE_RAVE_MIRROR("Player", "BattleRaveMirror");

float Player::GetWindowSeconds(TimingWindow tw) {
  float secs = timing_window_seconds[tw];
  secs *= timing_window_scale;
  secs += timing_window_add;
  return secs;
}

Player::Player(NoteData& nd, bool visible_parts) : note_data_(nd) {
  drawing_notefield_board_ = false;
  loaded_ = false;
  inside_lua_set_life_ = false;
  oitg_zoom_mode_ = false;

  player_state_ = nullptr;
  player_stage_stats_ = nullptr;
  note_field_height_ = 0;

  life_meter_ = nullptr;
  combined_life_meter_ = nullptr;
  score_display_ = nullptr;
  secondary_score_display_ = nullptr;
  primary_score_keeper_ = nullptr;
  secondary_score_keeper_ = nullptr;
  iter_needs_tap_judging_ = nullptr;
  iter_needs_hold_judging_ = nullptr;
  iter_uncrossed_rows_ = nullptr;
  iter_unjudged_rows_ = nullptr;
  iter_unjudged_mine_rows_ = nullptr;

  paused_ = false;
  delay_ = false;

  attack_display_ = nullptr;
  if (visible_parts) {
    attack_display_ = new AttackDisplay;
    this->AddChild(attack_display_);
  }

  PlayerAI::InitFromDisk();

  note_field_ = nullptr;
  if (visible_parts) {
    note_field_ = new NoteField;
    note_field_->SetName("NoteField");
  }
  judged_rows_ = new JudgedRows;

  send_judgment_and_combo_messages_ = true;
}

Player::~Player() {
  RageUtil::SafeDelete(attack_display_);
  RageUtil::SafeDelete(note_field_);
  for (unsigned i = 0; i < hold_judgment_.size(); ++i) {
    RageUtil::SafeDelete(hold_judgment_[i]);
  }
  RageUtil::SafeDelete(judged_rows_);
  RageUtil::SafeDelete(iter_needs_tap_judging_);
  RageUtil::SafeDelete(iter_needs_hold_judging_);
  RageUtil::SafeDelete(iter_uncrossed_rows_);
  RageUtil::SafeDelete(iter_unjudged_rows_);
  RageUtil::SafeDelete(iter_unjudged_mine_rows_);
}

/* Init() does the expensive stuff: load sounds and noteskins.  Load() just
 * loads a NoteData. */
void Player::Init(
    const std::string& type, PlayerState* player_state,
    PlayerStageStats* player_stage_stats, LifeMeter* lm,
    CombinedLifeMeter* combined_lm, ScoreDisplay* score_display,
    ScoreDisplay* secondary_score_display, ScoreKeeper* primary_score_keeper,
    ScoreKeeper* secondary_score_keeper) {
  GRAY_ARROWS_Y_STANDARD.Load(type, "ReceptorArrowsYStandard");
  GRAY_ARROWS_Y_REVERSE.Load(type, "ReceptorArrowsYReverse");
  ATTACK_DISPLAY_X.Load(type, ATTACK_DISPLAY_X_NAME, kNumPlayers, 2);
  HOLD_JUDGMENT_Y_STANDARD.Load(type, "HoldJudgmentYStandard");
  HOLD_JUDGMENT_Y_REVERSE.Load(type, "HoldJudgmentYReverse");
  BRIGHT_GHOST_COMBO_THRESHOLD.Load(type, "BrightGhostComboThreshold");
  TAP_JUDGMENTS_UNDER_FIELD.Load(type, "TapJudgmentsUnderField");
  HOLD_JUDGMENTS_UNDER_FIELD.Load(type, "HoldJudgmentsUnderField");
  COMBO_UNDER_FIELD.Load(type, "ComboUnderField");
  DRAW_DISTANCE_AFTER_TARGET_PIXELS.Load(
      type, "DrawDistanceAfterTargetsPixels");
  DRAW_DISTANCE_BEFORE_TARGET_PIXELS.Load(
      type, "DrawDistanceBeforeTargetsPixels");

  {
    // Init judgment positions
    bool player_using_both_sides =
        GAMESTATE->GetCurrentStyle(player_state->player_number_)
            ->GetUsesCenteredArrows();
    Actor temp_judgment;
    temp_judgment.SetName("Judgment");
    ActorUtil::LoadCommand(temp_judgment, type, "Transform");

    Actor temp_combo;
    temp_combo.SetName("Combo");
    ActorUtil::LoadCommand(temp_combo, type, "Transform");

    int enabled_player_index = -1;
    int num_enabled_players = 0;
    FOREACH_EnabledPlayer(p) {
      if (p == player_state->player_number_) {
        enabled_player_index = num_enabled_players;
      }
      num_enabled_players++;
    }

    if (num_enabled_players ==
        0)  // hack for ScreenHowToPlay where no players are joined
    {
      enabled_player_index = 0;
      num_enabled_players = 1;
    }

    for (int i = 0; i < NUM_REVERSE; i++) {
      for (int j = 0; j < NUM_CENTERED; j++) {
        Message msg("Transform");
        msg.SetParam("Player", player_state->player_number_);
        msg.SetParam("iEnabledPlayerIndex", enabled_player_index);
        msg.SetParam("iNumEnabledPlayers", num_enabled_players);
        msg.SetParam("bPlayerUsingBothSides", player_using_both_sides);
        msg.SetParam("bReverse", !!i);
        msg.SetParam("bCentered", !!j);

        temp_judgment.HandleMessage(msg);
        judgment_[i][j] = temp_judgment.DestTweenState();

        temp_combo.HandleMessage(msg);
        combo_[i][j] = temp_combo.DestTweenState();
      }
    }
  }

  this->SortByDrawOrder();

  player_state_ = player_state;
  player_stage_stats_ = player_stage_stats;
  life_meter_ = lm;
  combined_life_meter_ = combined_lm;
  score_display_ = score_display;
  secondary_score_display_ = secondary_score_display;
  primary_score_keeper_ = primary_score_keeper;
  secondary_score_keeper_ = secondary_score_keeper;

  last_seen_combo_ = 0;
  seen_combo_yet_ = false;

  // set initial life
  if (life_meter_ && player_stage_stats_) {
    float life = life_meter_->GetLife();
    player_stage_stats_->SetLifeRecordAt(
        life, STATSMAN->cur_stage_stats_.steps_seconds_);
  }

  // TODO: Remove use of PlayerNumber.
  PlayerNumber pn = player_state_->player_number_;

  RageSoundLoadParams sound_params;
  sound_params.support_pan_ = true;
  sound_mine_.Load(THEME->GetPathS(type, "mine"), true, &sound_params);

  /* Attacks can be launched in course modes and in battle modes.  They both
   * come here to play, but allow loading a different sound for different modes.
   */
  switch (GAMESTATE->play_mode_) {
    case PLAY_MODE_RAVE:
    case PLAY_MODE_BATTLE:
      sound_attack_launch_.Load(
          THEME->GetPathS(type, "battle attack launch"), true, &sound_params);
      sound_attack_ending_.Load(
          THEME->GetPathS(type, "battle attack ending"), true, &sound_params);
      break;
    default:
      sound_attack_launch_.Load(
          THEME->GetPathS(type, "course attack launch"), true, &sound_params);
      sound_attack_ending_.Load(
          THEME->GetPathS(type, "course attack ending"), true, &sound_params);
      break;
  }

  // calculate M-mod speed here, so we can adjust properly on a per-song basis.
  // XXX: can we find a better location for this?
  // Always calculate the reading bpm, to allow switching to an mmod mid-song.
  {
    DisplayBpms bpms;

    if (GAMESTATE->IsCourseMode()) {
      ASSERT(GAMESTATE->cur_trail_[pn] != nullptr);
      GAMESTATE->cur_trail_[pn]->GetDisplayBpms(bpms);
    } else {
      ASSERT(GAMESTATE->cur_steps_[pn] != nullptr);
      GAMESTATE->cur_steps_[pn]->GetDisplayBpms(bpms);
    }

    float max_bpm = 0;

    /* TODO: Find a way to not go above a certain BPM range
     * for getting the max BPM. Otherwise, you get songs
     * like Tsuhsuixamush, M550, 0.18x speed. Even slow
     * speed readers would not generally find this fun.
     * -Wolfman2000
     */

    // all BPMs are listed and available, so try them first.
    // get the maximum listed value for the song or course.
    // if the BPMs are < 0, reset and get the actual values.
    if (!bpms.IsSecret()) {
      max_bpm =
          (M_MOD_HIGH_CAP > 0 ? bpms.GetMaxWithin(M_MOD_HIGH_CAP)
                              : bpms.GetMax());
      max_bpm = std::max(0.0f, max_bpm);
    }

    // we can't rely on the displayed BPMs, so manually calculate.
    if (max_bpm == 0) {
      float throw_away = 0;

      if (GAMESTATE->IsCourseMode()) {
        for (const TrailEntry& e : GAMESTATE->cur_trail_[pn]->entries_) {
          float max_for_entry;
          if (M_MOD_HIGH_CAP > 0) {
            e.song_->song_timing_.GetActualBPM(
                throw_away, max_for_entry, M_MOD_HIGH_CAP);
          } else {
            e.song_->song_timing_.GetActualBPM(throw_away, max_for_entry);
          }
          max_bpm = std::max(max_for_entry, max_bpm);
        }
      } else {
        if (M_MOD_HIGH_CAP > 0) {
          GAMESTATE->cur_song_->song_timing_.GetActualBPM(
              throw_away, max_bpm, M_MOD_HIGH_CAP);
        } else {
          GAMESTATE->cur_song_->song_timing_.GetActualBPM(throw_away, max_bpm);
        }
      }
    }

    ASSERT(max_bpm > 0);
    player_state_->read_bpm_ = max_bpm;
  }

  float balance = GameSoundManager::GetPlayerBalance(pn);
  sound_mine_.SetProperty("Pan", balance);
  sound_attack_launch_.SetProperty("Pan", balance);
  sound_attack_ending_.SetProperty("Pan", balance);

  if (HasVisibleParts()) {
    LuaThreadVariable var(
        "Player", LuaReference::Create(player_state_->player_number_));

    spr_combo_.Load(THEME->GetPathG(type, "combo"));
    spr_combo_->SetName("Combo");
    actor_with_combo_position_ = &*spr_combo_;
    this->AddChild(spr_combo_);

    // todo: allow for judgments to be loaded per-column a la pop'n?
    // see how HoldJudgments are handled below for an example, though
    // it would need more work. -aj
    spr_judgment_.Load(THEME->GetPathG(type, "judgment"));
    spr_judgment_->SetName("Judgment");
    actor_with_judgment_position_ = &*spr_judgment_;
    this->AddChild(spr_judgment_);
  } else {
    actor_with_combo_position_ = nullptr;
    actor_with_judgment_position_ = nullptr;
  }

  // Load HoldJudgments
  hold_judgment_.resize(
      GAMESTATE->GetCurrentStyle(GetPlayerState()->player_number_)
          ->cols_per_player_);
  for (int i = 0;
       i < GAMESTATE->GetCurrentStyle(GetPlayerState()->player_number_)
               ->cols_per_player_;
       ++i) {
    hold_judgment_[i] = nullptr;
  }

  if (HasVisibleParts()) {
    for (int i = 0;
         i < GAMESTATE->GetCurrentStyle(GetPlayerState()->player_number_)
                 ->cols_per_player_;
         ++i) {
      HoldJudgment* judgment = new HoldJudgment;
      // xxx: assumes sprite; todo: don't force 1x2 -aj
      judgment->Load(THEME->GetPathG("HoldJudgment", "label 1x2"));
      hold_judgment_[i] = judgment;
      this->AddChild(hold_judgment_[i]);
    }
  }

  note_field_height_ = GRAY_ARROWS_Y_REVERSE - GRAY_ARROWS_Y_STANDARD;
  if (note_field_) {
    note_field_->Init(player_state_, note_field_height_);
    ActorUtil::LoadAllCommands(*note_field_, type);
    this->AddChild(note_field_);
  }

  active_random_attack_start_ = -1.0f;
}
/**
 * @brief Determine if a TapNote needs a tap note style judgment.
 * @param tn the TapNote in question.
 * @return true if it does, false otherwise. */
static bool NeedsTapJudging(const TapNote& tn) {
  switch (tn.type_) {
    DEFAULT_FAIL(tn.type_);
    case TapNoteType_Tap:
    case TapNoteType_HoldHead:
    case TapNoteType_Mine:
    case TapNoteType_Lift:
      return tn.result_.tns_ == TNS_None;
    case TapNoteType_HoldTail:
    case TapNoteType_Attack:
    case TapNoteType_AutoKeysound:
    case TapNoteType_Fake:
    case TapNoteType_Empty:
      return false;
  }
}

/**
 * @brief Determine if a TapNote needs a hold note style judgment.
 * @param tn the TapNote in question.
 * @return true if it does, false otherwise. */
static bool NeedsHoldJudging(const TapNote& tn) {
  switch (tn.type_) {
    DEFAULT_FAIL(tn.type_);
    case TapNoteType_HoldHead:
      return tn.hold_result_.hns_ == HNS_None;
    case TapNoteType_Tap:
    case TapNoteType_HoldTail:
    case TapNoteType_Mine:
    case TapNoteType_Lift:
    case TapNoteType_Attack:
    case TapNoteType_AutoKeysound:
    case TapNoteType_Fake:
    case TapNoteType_Empty:
      return false;
  }
}

static void GenerateCacheDataStructure(
    PlayerState* player_state, const NoteData& notes) {
  player_state->cache_displayed_beat_.clear();

  const std::vector<TimingSegment*> scrolls =
      player_state->GetDisplayedTiming().GetTimingSegments(SEGMENT_SCROLL);

  float displayed_beat = 0.0f;
  float last_real_beat = 0.0f;
  float last_ratio = 1.0f;
  for (unsigned i = 0; i < scrolls.size(); i++) {
    ScrollSegment* seg = ToScroll(scrolls[i]);
    displayed_beat += (seg->GetBeat() - last_real_beat) * last_ratio;
    last_real_beat = seg->GetBeat();
    last_ratio = seg->GetRatio();
    CacheDisplayedBeat c = {seg->GetBeat(), displayed_beat, seg->GetRatio()};
    player_state->cache_displayed_beat_.push_back(c);
  }

  player_state->cache_note_stat_.clear();

  NoteData::all_tracks_const_iterator it =
      notes.GetTapNoteRangeAllTracks(0, kMaxNoteRow, true);
  int count = 0, last_count = 0;
  for (; !it.IsAtEnd(); ++it) {
    for (int t = 0; t < notes.GetNumTracks(); t++) {
      if (notes.GetTapNote(t, it.Row()) != tap_empty) {
        count++;
      }
    }
    CacheNoteStat c = {NoteRowToBeat(it.Row()), last_count, count};
    last_count = count;
    player_state->cache_note_stat_.push_back(c);
  }
}

void Player::Load() {
  loaded_ = true;

  // Figured this is probably a little expensive so let's cache it
  tick_holds_ = TICK_HOLDS;

  // The editor can start playing in the middle of the song.
  const int note_row =
      BeatToNoteRowNotRounded(player_state_->position_.song_beat_);
  first_uncrossed_row_ = note_row - 1;
  judged_rows_->Reset(note_row);

  // TODO: Remove use of PlayerNumber.
  PlayerNumber pn = player_state_->player_number_;

  bool oni_dead =
      player_state_->player_options_.GetStage().life_type_ ==
          LifeType_Battery &&
      (player_stage_stats_ == nullptr || player_stage_stats_->failed_);

  /* The editor reuses Players ... so we really need to make sure everything
   * is reset and not tweening.  Perhaps ActorFrame should recurse to subactors;
   * then we could just this->StopTweening()? -glenn */
  // hurr why don't you just set m_bPropagateCommands on it then -aj
  if (spr_judgment_) {
    spr_judgment_->PlayCommand("Reset");
  }
  if (player_stage_stats_) {
    SetCombo(
        player_stage_stats_->cur_combo_,
        player_stage_stats_
            ->cur_miss_combo_);  // combo can persist between songs and games
  }
  if (attack_display_) {
    attack_display_->Init(player_state_);
  }

  /* Don't re-init this; that'll reload graphics.  Add a separate Reset() call
   * if some ScoreDisplays need it. */
  //	if( m_pScore )
  //		m_pScore->Init( pn );

  timing_ = GAMESTATE->cur_steps_[pn]->GetTimingData();

  if (note_data_.IsComposite()) {
    std::vector<NoteData> parts;

    NoteDataUtil::SplitCompositeNoteData(note_data_, parts);
    note_data_ = parts[pn];
  }

  /* Apply transforms. */
  NoteDataUtil::TransformNoteData(
      note_data_, *timing_, player_state_->player_options_.GetStage(),
      GAMESTATE->GetCurrentStyle(GetPlayerState()->player_number_)
          ->steps_type_);

  const Song* song = GAMESTATE->cur_song_;

  // Generate some cache data structure.
  GenerateCacheDataStructure(player_state_, note_data_);

  switch (GAMESTATE->play_mode_) {
    case PLAY_MODE_RAVE:
    case PLAY_MODE_BATTLE: {
      // ugly, ugly, ugly.  Works only w/ dance.
      // Why does this work only with dance? - Steve
      // it has to do with there only being four cases. This is a lame
      // workaround, but since only DDR has ever really implemented those
      // modes, it's stayed like this. -aj
      StepsType st =
          GAMESTATE->GetCurrentStyle(GetPlayerState()->player_number_)
              ->steps_type_;
      NoteDataUtil::TransformNoteData(
          note_data_, *timing_, player_state_->player_options_.GetStage(), st);

      if (BATTLE_RAVE_MIRROR) {
        // shuffle either p1 or p2
        static int count = 0;
        switch (count) {
          case 0:
          case 3:
            NoteDataUtil::Turn(note_data_, st, NoteDataUtil::left);
            break;
          case 1:
          case 2:
            NoteDataUtil::Turn(note_data_, st, NoteDataUtil::right);
            break;
          default:
            FAIL_M(ssprintf("Count %i not in range 0-3", count));
        }
        count++;
        count %= 4;
      }
      break;
    }
    default:
      break;
  }

  int draw_distance_after_targets_pixels =
      GAMESTATE->IsEditing() ? -100 : DRAW_DISTANCE_AFTER_TARGET_PIXELS;
  int draw_distance_before_targets_pixels =
      GAMESTATE->IsEditing() ? 400 : DRAW_DISTANCE_BEFORE_TARGET_PIXELS;

  float note_field_middle =
      (GRAY_ARROWS_Y_STANDARD + GRAY_ARROWS_Y_REVERSE) / 2;

  if (note_field_ && !oni_dead) {
    note_field_->SetY(note_field_middle);
    note_field_->Load(
        &note_data_, draw_distance_after_targets_pixels,
        draw_distance_before_targets_pixels);
  }

  bool player_using_both_sides =
      GAMESTATE->GetCurrentStyle(GetPlayerState()->player_number_)
          ->GetUsesCenteredArrows();
  if (attack_display_) {
    attack_display_->SetX(
        ATTACK_DISPLAY_X.GetValue(pn, player_using_both_sides) - 40);
  }

  // Load keysounds.  If sounds are already loaded (as in the editor), don't
  // reload them.
  // XXX: the editor will load several duplicate copies (in each NoteField), and
  // each player will load duplicate sounds.  Does this belong somewhere else
  // (perhaps in a separate object, used alongside ScreenGameplay::m_pSoundMusic
  // and ScreenEdit::m_pSoundMusic?) We don't have to load separate copies to
  // set player fade: always make a copy, and set the fade on the copy.
  std::string song_dir = song->GetSongDir();
  keysounds_.resize(song->keysound_file_.size());

  // parameters are invalid somehow... -aj
  RageSoundLoadParams sound_params;
  sound_params.support_pan_ = true;

  float balance = GameSoundManager::GetPlayerBalance(pn);
  for (unsigned i = 0; i < keysounds_.size(); i++) {
    std::string keysound_file_path = song_dir + song->keysound_file_[i];
    RageSound& sound = keysounds_[i];
    if (sound.GetLoadedFilePath() != keysound_file_path) {
      sound.Load(keysound_file_path, true, &sound_params);
    }
    sound.SetProperty("Pan", balance);
    sound.SetStopModeFromString("stop");
  }

  if (player_stage_stats_) {
    SendComboMessages(
        player_stage_stats_->cur_combo_, player_stage_stats_->cur_miss_combo_);
  }

  RageUtil::SafeDelete(iter_needs_tap_judging_);
  iter_needs_tap_judging_ = new NoteData::all_tracks_iterator(
      note_data_.GetTapNoteRangeAllTracks(note_row, kMaxNoteRow));

  RageUtil::SafeDelete(iter_needs_hold_judging_);
  iter_needs_hold_judging_ = new NoteData::all_tracks_iterator(
      note_data_.GetTapNoteRangeAllTracks(note_row, kMaxNoteRow));

  RageUtil::SafeDelete(iter_uncrossed_rows_);
  iter_uncrossed_rows_ = new NoteData::all_tracks_iterator(
      note_data_.GetTapNoteRangeAllTracks(note_row, kMaxNoteRow));

  RageUtil::SafeDelete(iter_unjudged_rows_);
  iter_unjudged_rows_ = new NoteData::all_tracks_iterator(
      note_data_.GetTapNoteRangeAllTracks(note_row, kMaxNoteRow));

  RageUtil::SafeDelete(iter_unjudged_mine_rows_);
  iter_unjudged_mine_rows_ = new NoteData::all_tracks_iterator(
      note_data_.GetTapNoteRangeAllTracks(note_row, kMaxNoteRow));
}

void Player::SendComboMessages(
    unsigned int old_combo, unsigned int old_miss_combo) {
  const unsigned int cur_combo =
      player_stage_stats_ ? player_stage_stats_->cur_combo_ : 0;
  if (old_combo > (unsigned int)COMBO_STOPPED_AT &&
      cur_combo < (unsigned int)COMBO_STOPPED_AT) {
    SCREENMAN->PostMessageToTopScreen(SM_ComboStopped, 0);
  }

  if (send_judgment_and_combo_messages_) {
    Message msg("ComboChanged");
    msg.SetParam("Player", player_state_->player_number_);
    msg.SetParam("OldCombo", old_combo);
    msg.SetParam("OldMissCombo", old_miss_combo);
    if (player_state_) {
      msg.SetParam("PlayerState", LuaReference::CreateFromPush(*player_state_));
    }
    if (player_stage_stats_) {
      msg.SetParam(
          "PlayerStageStats",
          LuaReference::CreateFromPush(*player_stage_stats_));
    }
    MESSAGEMAN->Broadcast(msg);
  }
}

void Player::Update(float delta_time) {
  const RageTimer now;
  // Don't update if we haven't been loaded yet.
  if (!loaded_) {
    return;
  }

  // LOG->Trace( "Player::Update(%f)", fDeltaTime );

  if (GAMESTATE->cur_song_ == nullptr || IsOniDead()) {
    return;
  }

  ActorFrame::Update(delta_time);

  const float song_beat = player_state_->position_.song_beat_;
  const int song_row = BeatToNoteRow(song_beat);

  ArrowEffects::SetCurrentOptions(&player_state_->player_options_.GetCurrent());

  // Optimization: Don't spend time processing the things below that won't show
  // if the Player doesn't show anything on the screen.
  if (HasVisibleParts()) {
    // Random Attack Mod
    if (player_state_->player_options_.GetCurrent().rand_attack_) {
      float current_game_time = STATSMAN->cur_stage_stats_.gameplay_seconds_;

      const float attack_run_time = ATTACK_RUN_TIME_RANDOM;

      // Don't start until 1 seconds into game, minimum
      if (current_game_time > 1.0f) {
        /* Update the attack if there are no others currently running.
         * Note that we have a new one activate a little early; This is
         * to have a bit of overlap rather than an abrupt change. */
        if ((current_game_time - active_random_attack_start_) >
            (attack_run_time - 0.5f)) {
          active_random_attack_start_ = current_game_time;

          Attack random_attack;
          random_attack.modifiers_ = ApplyRandomAttack();
          random_attack.secs_remaining_ = attack_run_time;
          player_state_->LaunchAttack(random_attack);
        }
      }
    }

    if (enable_attack_sound_playback) {
      if (player_state_->attack_began_this_update_) {
        sound_attack_launch_.Play(false);
      }
      if (player_state_->attack_ended_this_update_) {
        sound_attack_ending_.Play(false);
      }
    }

    float mini_percent = player_state_->player_options_.GetCurrent()
                             .effects_[PlayerOptions::EFFECT_MINI];
    float tiny_percent = player_state_->player_options_.GetCurrent()
                             .effects_[PlayerOptions::EFFECT_TINY];
    float judgment_zoom =
        std::min(std::pow(0.5f, mini_percent + tiny_percent), 1.0f);

    // Update Y positions
    {
      for (int c = 0;
           c < GAMESTATE->GetCurrentStyle(GetPlayerState()->player_number_)
                   ->cols_per_player_;
           c++) {
        float percent_reverse = player_state_->player_options_.GetCurrent()
                                    .GetReversePercentForColumn(c);
        float hold_judge_y_pos = SCALE(
            percent_reverse, 0.f, 1.f, HOLD_JUDGMENT_Y_STANDARD,
            HOLD_JUDGMENT_Y_REVERSE);
        // float fGrayYPos = SCALE( fPercentReverse, 0.f, 1.f,
        // GRAY_ARROWS_Y_STANDARD, GRAY_ARROWS_Y_REVERSE );

        float x = ArrowEffects::GetXPos(player_state_, c, 0);
        const float z = ArrowEffects::GetZPos(player_state_, c, 0);
        x *= (1 - mini_percent * 0.5f);

        hold_judgment_[c]->SetX(x);
        hold_judgment_[c]->SetY(hold_judge_y_pos);
        hold_judgment_[c]->SetZ(z);
        hold_judgment_[c]->SetZoom(judgment_zoom);
      }
    }

    // NoteField accounts for reverse on its own now.
    // if( m_pNoteField )
    //	m_pNoteField->SetY( fGrayYPos );

    const bool reverse =
        player_state_->player_options_.GetCurrent().GetReversePercentForColumn(
            0) == 1;
    float percent_centered = player_state_->player_options_.GetCurrent()
                                 .scrolls_[PlayerOptions::SCROLL_CENTERED];

    if (actor_with_judgment_position_ != nullptr) {
      const Actor::TweenState& ts1 = judgment_[reverse ? 1 : 0][0];
      const Actor::TweenState& ts2 = judgment_[reverse ? 1 : 0][1];
      Actor::TweenState::MakeWeightedAverage(
          actor_with_judgment_position_->DestTweenState(), ts1, ts2,
          percent_centered);
    }

    if (actor_with_combo_position_ != nullptr) {
      const Actor::TweenState& ts1 = combo_[reverse ? 1 : 0][0];
      const Actor::TweenState& ts2 = combo_[reverse ? 1 : 0][1];
      Actor::TweenState::MakeWeightedAverage(
          actor_with_combo_position_->DestTweenState(), ts1, ts2,
          percent_centered);
    }

    float field_zoom = 1 - mini_percent * 0.5f;
    if (note_field_) {
      if (oitg_zoom_mode_) {
        note_field_->SetZoomX(field_zoom);
        note_field_->SetZoomY(field_zoom);
      } else {
        note_field_->SetZoom(field_zoom);
      }
    }
    if (actor_with_judgment_position_ != nullptr) {
      actor_with_judgment_position_->SetZoom(
          actor_with_judgment_position_->GetZoom() * judgment_zoom);
    }
    if (actor_with_combo_position_ != nullptr) {
      actor_with_combo_position_->SetZoom(
          actor_with_combo_position_->GetZoom() * judgment_zoom);
    }
  }

  // If we're paused, don't update tap or hold note logic, so hold notes can be
  // released during pause.
  if (paused_) {
    return;
  }

  // update pressed flag
  const int num_cols =
      GAMESTATE->GetCurrentStyle(GetPlayerState()->player_number_)
          ->cols_per_player_;
  ASSERT_M(
      num_cols <= kMaxColsPerPlayer,
      ssprintf("%i > %i", num_cols, kMaxColsPerPlayer));
  for (int col = 0; col < num_cols; ++col) {
    ASSERT(player_state_ != nullptr);

    // TODO: Remove use of PlayerNumber.
    std::vector<GameInput> game_i;
    GAMESTATE->GetCurrentStyle(GetPlayerState()->player_number_)
        ->StyleInputToGameInput(col, player_state_->player_number_, game_i);

    bool is_holding_button = INPUTMAPPER->IsBeingPressed(game_i);

    // TODO: Make this work for non-human-controlled players
    if (is_holding_button && !GAMESTATE->demonstration_or_jukebox_ &&
        player_state_->player_controller_ == PC_HUMAN) {
      if (note_field_) {
        note_field_->SetPressed(col);
      }
    }
  }

  // handle Autoplay for rolls
  if (player_state_->player_controller_ != PC_HUMAN) {
    for (int i_track = 0; i_track < note_data_.GetNumTracks(); ++i_track) {
      // TODO: Make the CPU miss sometimes.
      int head_row;
      if (!note_data_.IsHoldNoteAtRow(i_track, song_row, &head_row)) {
        head_row = song_row;
      }

      const TapNote& tn = note_data_.GetTapNote(i_track, head_row);
      if (tn.type_ != TapNoteType_HoldHead ||
          tn.sub_type_ != TapNoteSubType_Roll) {
        continue;
      }
      if (tn.hold_result_.hns_ != HNS_None) {
        continue;
      }
      if (tn.hold_result_.life_ >= 0.5f) {
        continue;
      }

      Step(i_track, head_row, now, false, false);
      if (player_state_->player_controller_ == PC_AUTOPLAY) {
        STATSMAN->cur_stage_stats_.used_autoplay_ = true;
        if (player_stage_stats_) {
          player_stage_stats_->disqualified_ = true;
        }
      }
    }
  }

  // Track held misses
  //
  // A miss is "held" if the player was physically holding the button at the
  // note's exact timing point.
  {
    const float music_position = player_state_->position_.music_seconds_;
    const float rate = GAMESTATE->song_options_.GetCurrent().music_rate_;

    // We have to add 1 here, because GetBeatFromElapsedTime() can round down.
    const int last_check_row =
        BeatToNoteRow(timing_->GetBeatFromElapsedTime(music_position) + 1);

    for (auto iter = *iter_needs_tap_judging_;
         !iter.IsAtEnd() && iter.Row() <= last_check_row; ++iter) {
      TapNote& tn = *iter;
      const int row = iter.Row();
      const int track = iter.Track();

      const float note_position =
          timing_->GetElapsedTimeFromBeat(NoteRowToBeat(row));

      // Check if we're looking at notes in the future we don't care about.
      if (note_position > music_position) {
        break;
      }

      // Already determined this is held, don't un-mark it.
      if (tn.result_.held_) {
        continue;
      }

      // Skip over warp and fake segments
      if (!timing_->IsJudgableAtRow(row)) {
        continue;
      }

      // Held misses only apply to tap notes
      if (tn.type_ != TapNoteType_Tap && tn.type_ != TapNoteType_HoldHead) {
        continue;
      }

      // Skip notes already judged (e.g. hit notes in a jump).
      if (!NeedsTapJudging(tn)) {
        continue;
      }

      PlayerNumber pn = player_state_->player_number_;
      std::vector<GameInput> inputs;
      GAMESTATE->GetCurrentStyle(pn)->StyleInputToGameInput(track, pn, inputs);

      // Button must have been held continuously since at least the note time
      // (max time of all inputs mapped to this column).
      const float secs_since_note = (music_position - note_position) / rate;
      float max_secs_held = 0.0f;
      for (const GameInput& gi : inputs) {
        max_secs_held = std::max(max_secs_held, INPUTMAPPER->GetSecsHeld(gi));
      }
      tn.result_.held_ = max_secs_held >= secs_since_note;
    }
  }

  // update HoldNotes logic
  {
    // Fast forward to the first that needs hold judging.
    {
      NoteData::all_tracks_iterator& iter = *iter_needs_hold_judging_;
      while (!iter.IsAtEnd() && iter.Row() <= song_row &&
             !NeedsHoldJudging(*iter)) {
        ++iter;
      }
    }

    std::vector<TrackRowTapNote> hold_notes_to_grade_together;
    int row_of_last_hold_note = -1;
    NoteData::all_tracks_iterator iter = *iter_needs_hold_judging_;  // copy
    for (; !iter.IsAtEnd() && iter.Row() <= song_row; ++iter) {
      TapNote& tn = *iter;
      if (tn.type_ != TapNoteType_HoldHead) {
        continue;
      }

      int i_track = iter.Track();
      int i_row = iter.Row();
      TrackRowTapNote trtn = {i_track, i_row, &tn};

      /* All holds must be of the same subType because fLife is handled
       * in different ways depending on the SubType. Handle Rolls one at
       * a time and don't mix with holds. */
      switch (tn.sub_type_) {
        DEFAULT_FAIL(tn.sub_type_);
        case TapNoteSubType_Hold:
          break;
        case TapNoteSubType_Roll: {
          std::vector<TrackRowTapNote> v;
          v.push_back(trtn);
          UpdateHoldNotes(song_row, delta_time, v);
        }
          continue;  // don't process this below
      }
      /*
      case TapNoteSubType_Mine:
              break;
      */

      if (i_row != row_of_last_hold_note ||
          !JUDGE_HOLD_NOTES_ON_SAME_ROW_TOGETHER) {
        if (!hold_notes_to_grade_together.empty()) {
          // LOG->Trace( ssprintf("UpdateHoldNotes; %i != %i || !judge holds on
          // same row together",iRow,iRowOfLastHoldNote) );
          UpdateHoldNotes(song_row, delta_time, hold_notes_to_grade_together);
          hold_notes_to_grade_together.clear();
        }
      }
      row_of_last_hold_note = i_row;
      hold_notes_to_grade_together.push_back(trtn);
    }

    if (!hold_notes_to_grade_together.empty()) {
      // LOG->Trace("UpdateHoldNotes since !vHoldNotesToGradeTogether.empty()");
      UpdateHoldNotes(song_row, delta_time, hold_notes_to_grade_together);
      hold_notes_to_grade_together.clear();
    }
  }

  {
    // Why was this originally "BeatToNoteRowNotRounded"? It should be rounded.
    // -Chris
    /* We want to send the crossed row message exactly when we cross the
     * row--not .5 before the row. Use a very slow song (around 2 BPM) as a test
     * case: without rounding, autoplay steps early. -glenn */
    const float position_seconds =
        player_state_->position_.music_seconds_ - PREFSMAN->pad_stick_seconds_;
    const float displayed_song_beat =
        player_state_->GetDisplayedTiming().GetBeatFromElapsedTime(
            position_seconds);
    const int row_now = BeatToNoteRowNotRounded(displayed_song_beat);

    if (row_now >= 0) {
      if (GAMESTATE->IsPlayerEnabled(player_state_)) {
        if (player_state_->position_.delay_) {
          if (!delay_) {
            delay_ = true;
          }
        } else {
          if (delay_) {
            if (player_state_->player_controller_ != PC_HUMAN) {
              CrossedRows(row_now - 1, now);
            }
            delay_ = false;
          }
          CrossedRows(row_now, now);
        }
      }
    }
  }

  // Check for completely judged rows.
  UpdateJudgedRows();

  // Check for TapNote misses
  if (!GAMESTATE->in_step_editor_) {
    UpdateTapNotesMissedOlderThan(GetMaxStepDistanceSeconds());
  }
  // process transforms that are waiting to be applied
  ApplyWaitingTransforms();
}

// Update a group of holds with shared scoring/life. All of these holds will
// have the same start row.
void Player::UpdateHoldNotes(
    int song_row, float delta_time, std::vector<TrackRowTapNote>& v_tn) {
  ASSERT(!v_tn.empty());

  // LOG->Trace("--------------------------------");
  /*
  LOG->Trace("[Player::UpdateHoldNotes] begins");
  LOG->Trace( ssprintf("song row %i, deltaTime = %f",iSongRow,fDeltaTime) );
  */

  int start_row = v_tn[0].row_;
  int max_end_row = INT_MIN;
  int first_track_with_max_end_row = -1;

  TapNoteSubType sub_type = TapNoteSubType_Invalid;
  for (const TrackRowTapNote& trtn : v_tn) {
    int track = trtn.track_;
    ASSERT(start_row == trtn.row_);
    TapNote& tn = *trtn.tn_;
    int end_row = start_row + tn.duration_;
    if (sub_type == TapNoteSubType_Invalid) {
      sub_type = tn.sub_type_;
    }

    /* All holds must be of the same subType because fLife is handled
     * in different ways depending on the SubType. */
    ASSERT(tn.sub_type_ == sub_type);

    // Routine charts assign each note to a specific player; skip notes
    // that don't belong to this Player
    if (tn.pn_ != kPlayerInvalid && tn.pn_ != player_state_->player_number_) {
      continue;
    }

    if (end_row > max_end_row) {
      max_end_row = end_row;
      first_track_with_max_end_row = track;
    }
  }

  ASSERT(first_track_with_max_end_row != -1);
  // LOG->Trace( ssprintf("start row: %i; max/end row: =
  // %i",iStartRow,iMaxEndRow) ); LOG->Trace( ssprintf("first track with max end
  // row = %i",iFirstTrackWithMaxEndRow) ); LOG->Trace( ssprintf("max end row -
  // start row (in beats) =
  // %f",NoteRowToBeat(iMaxEndRow)-NoteRowToBeat(iStartRow)) );

  for (const TrackRowTapNote& trtn : v_tn) {
    TapNote& tn = *trtn.tn_;
    // Routine charts assign each note to a specific player; skip notes
    // that don't belong to this Player
    if (tn.pn_ != kPlayerInvalid && tn.pn_ != player_state_->player_number_) {
      continue;
    }
    // set hold flags so NoteField can do intelligent drawing
    tn.hold_result_.held_ = false;
    tn.hold_result_.active_ = false;

    int row = trtn.row_;
    // LOG->Trace( ssprintf("this row: %i",iRow) );

    // If the song beat is in the range of this hold:
    if (row <= song_row && row <= max_end_row) {
      // LOG->Trace( ssprintf("overlap time before:
      // %f",tn.HoldResult.fOverlappedTime) );
      tn.hold_result_.overlapped_time_ += delta_time;
      // LOG->Trace( ssprintf("overlap time after:
      // %f",tn.HoldResult.fOverlappedTime) );
    } else {
      // LOG->Trace( "overlap time = 0" );
      tn.hold_result_.overlapped_time_ = 0;
    }
  }

  HoldNoteScore hns = v_tn[0].tn_->hold_result_.hns_;
  float life = v_tn[0].tn_->hold_result_.life_;

  if (hns != HNS_None)  // if this HoldNote already has a result
  {
    // LOG->Trace("hold note has a result, skipping.");
    return;  // we don't need to update the logic for this group
  }

  // LOG->Trace("hold note doesn't already have result, let's check.");

  // LOG->Trace( ssprintf("[C++] hold note score:
  // %s",HoldNoteScoreToString(hns).c_str()) );
  // LOG->Trace(ssprintf("[Player::UpdateHoldNotes] fLife = %f",fLife));

  bool stepped_on_head = true;
  bool head_judged = true;
  for (const TrackRowTapNote& trtn : v_tn) {
    TapNote& tn = *trtn.tn_;
    TapNoteScore tns = tn.result_.tns_;
    // Routine charts assign each note to a specific player; skip notes
    // that don't belong to this Player
    if (tn.pn_ != kPlayerInvalid && tn.pn_ != player_state_->player_number_) {
      continue;
    }
    // LOG->Trace( ssprintf("[C++] tap note score:
    // %s",StringConversion::ToString(tns).c_str()) );

    // TODO: When using JUDGE_HOLD_NOTES_ON_SAME_ROW_TOGETHER, require that the
    // whole row of taps was hit before activating this group of holds.
    /* Something about the logic in this section is causing 192nd steps to
     * fail for some odd reason. -aj */
    // NOTE(teejusb): We want early hits to count as a hit on the hold head
    // otherwise it visually looks weird if you never have a second hit for the
    // arrow.
    stepped_on_head &=
        ((tns != TNS_Miss && tns != TNS_None) ||
         tn.result_.early_tns_ !=
             TNS_None);  // did they step on the start of this hold?
    head_judged &=
        (tns != TNS_None ||
         tn.result_.early_tns_ !=
             TNS_None);  // has this hold really even started yet?

    /*
    if(bSteppedOnHead)
            LOG->Trace("[Player::UpdateHoldNotes] player stepped on head");
    else
            LOG->Trace("[Player::UpdateHoldNotes] player didn't step on the
    head");
    */
  }

  bool initiated_note;
  if (REQUIRE_STEP_ON_HOLD_HEADS) {
    // XXX HACK: Miniholds (a 64th or 192nd length hold) will not always
    // register as Held, even if you hit the note. This is considered a
    // major roadblock to adoption, so until a proper fix is found,
    // DON'T REMOVE THIS HACK! -aj
    /*if( iMaxEndRow-iStartRow <= 4 )
            bInitiatedNote = true;
    else*/
    initiated_note = stepped_on_head;
  } else {
    initiated_note = true;
    head_judged = true;
  }

  bool is_holding_button = true;
  for (const TrackRowTapNote& trtn : v_tn) {
    /*if this hold is already done, pretend it's always being pressed.
    fixes/masks the phantom hold issue. -FSX*/
    // That interacts badly with !IMMEDIATE_HOLD_LET_GO,
    // causing ALL holds to be judged HNS_Held whether they were or not.
    if (!IMMEDIATE_HOLD_LET_GO ||
        (start_row + trtn.tn_->duration_) > song_row) {
      int track = trtn.track_;

      // TODO: Remove use of PlayerNumber.
      PlayerNumber pn = player_state_->player_number_;

      if (player_state_->player_controller_ != PC_HUMAN) {
        // TODO: Make the CPU miss sometimes.
        if (player_state_->player_controller_ == PC_AUTOPLAY) {
          STATSMAN->cur_stage_stats_.used_autoplay_ = true;
          if (player_stage_stats_ != nullptr) {
            player_stage_stats_->disqualified_ = true;
          }
        }
      } else {
        std::vector<GameInput> game_i;
        GAMESTATE->GetCurrentStyle(GetPlayerState()->player_number_)
            ->StyleInputToGameInput(track, pn, game_i);

        is_holding_button &= INPUTMAPPER->IsBeingPressed(game_i);
      }
    }
  }

  if (initiated_note && life != 0 && head_judged) {
    // LOG->Trace("[Player::UpdateHoldNotes] initiated note, fLife != 0");
    /* This hold note is not judged and we stepped on its head.
     * Update iLastHeldRow. Do this even if we're a little beyond the end
     * of the hold note, to make sure iLastHeldRow is clamped to iEndRow
     * if the hold note is held all the way. */
    for (const TrackRowTapNote& trtn : v_tn) {
      TapNote& tn = *trtn.tn_;
      int end_row = start_row + tn.duration_;
      // Routine charts assign each note to a specific player; skip notes
      // that don't belong to this Player
      if (tn.pn_ != kPlayerInvalid && tn.pn_ != player_state_->player_number_) {
        continue;
      }
      // LOG->Trace(ssprintf("trying for min between iSongRow (%i) and iEndRow
      // (%i) (duration %i)",iSongRow,iEndRow,tn.iDuration));
      tn.hold_result_.last_held_row_ = std::min(song_row, end_row);
    }
  }

  // If the song beat is in the range of this hold:
  if (start_row <= song_row && start_row <= max_end_row && head_judged) {
    switch (sub_type) {
      case TapNoteSubType_Hold:
        for (const TrackRowTapNote& trtn : v_tn) {
          TapNote& tn = *trtn.tn_;
          if (tn.pn_ != kPlayerInvalid &&
              tn.pn_ != player_state_->player_number_) {
            continue;
          }
          // set hold flag so NoteField can do intelligent drawing
          tn.hold_result_.held_ = is_holding_button && initiated_note;
          tn.hold_result_.active_ = initiated_note;
        }

        if (initiated_note && is_holding_button) {
          // LOG->Trace("bInitiatedNote && bIsHoldingButton; Increasing hold
          // life to MAX_HOLD_LIFE");
          //  Increase life
          life = MAX_HOLD_LIFE;  // was 1 -aj
        } else {
          /*
          LOG->Trace("Checklist:");
          if(bInitiatedNote)
                  LOG->Trace("[X] Initiated Note");
          else
                  LOG->Trace("[ ] Initiated Note");

          if(bIsHoldingButton)
                  LOG->Trace("[X] Holding Button");
          else
                  LOG->Trace("[ ] Holding Button");
          */

          TimingWindow window = tick_holds_ ? TW_Checkpoint : TW_Hold;
          // LOG->Trace("fLife before minus: %f",fLife);
          life -= delta_time / GetWindowSeconds(window);
          // LOG->Trace("fLife before clamp: %f",fLife);
          life = std::max(0.0f, life);
          // LOG->Trace("fLife after: %f",fLife);
        }
        break;
      case TapNoteSubType_Roll:
        for (const TrackRowTapNote& trtn : v_tn) {
          TapNote& tn = *trtn.tn_;
          if (tn.pn_ != kPlayerInvalid &&
              tn.pn_ != player_state_->player_number_) {
            continue;
          }
          tn.hold_result_.held_ = true;
          tn.hold_result_.active_ = initiated_note;
        }

        // give positive life in Step(), not here.

        // Decrease life
        life -= delta_time / GetWindowSeconds(TW_Roll);
        life = std::max(life, 0.0f);  // clamp
        break;
      /*
      case TapNoteSubType_Mine:
              break;
      */
      default:
        FAIL_M(ssprintf("Invalid tap note subtype: %i", sub_type));
    }
  }

  // TODO: Cap the active time passed to the score keeper to the actual start
  // time and end time of the hold.
  if (v_tn[0].tn_->hold_result_.active_) {
    float seconds_active_since_last_update =
        delta_time * GAMESTATE->song_options_.GetCurrent().music_rate_;
    if (primary_score_keeper_) {
      primary_score_keeper_->HandleHoldActiveSeconds(
          seconds_active_since_last_update);
    }
    if (secondary_score_keeper_) {
      secondary_score_keeper_->HandleHoldActiveSeconds(
          seconds_active_since_last_update);
    }
  }

  // check for LetGo. If the head was missed completely, don't count an LetGo.
  /* Why? If you never step on the head, then it will be left as HNS_None,
   * which doesn't seem correct. */
  if (IMMEDIATE_HOLD_LET_GO) {
    if (initiated_note && life == 0 &&
        head_judged)  // the player has not pressed the button for a long time!
    {
      // LOG->Trace("LetGo from life == 0 (did initiate hold)");
      hns = HNS_LetGo;
    }
  }

  // score hold notes that have passed
  if (song_row >= max_end_row && head_judged) {
    bool let_go_of_hold_note = false;

    /* Score rolls that end with fLife == 0 as LetGo, even if
     * m_bTickHolds is on. Rolls don't have iCheckpointsMissed set, so,
     * unless we check Life == 0, rolls would always be scored as Held. */
    bool allow_hold_checkpoints;
    switch (sub_type) {
      DEFAULT_FAIL(sub_type);
      case TapNoteSubType_Hold:
        allow_hold_checkpoints = true;
        break;
      case TapNoteSubType_Roll:
        allow_hold_checkpoints = false;
        break;
        /*
        case TapNoteSubType_Mine:
                bAllowHoldCheckpoints = true;
                break;
        */
    }

    if (tick_holds_ && allow_hold_checkpoints) {
      // LOG->Trace("(hold checkpoints are allowed and enabled.)");
      int checkpoints_hit = 0;
      int checkpoints_missed = 0;
      for (const TrackRowTapNote& v : v_tn) {
        if (v.tn_->pn_ != kPlayerInvalid &&
            v.tn_->pn_ != player_state_->player_number_) {
          continue;
        }
        checkpoints_hit += v.tn_->hold_result_.checkpoints_hit_;
        checkpoints_missed += v.tn_->hold_result_.checkpoints_missed_;
      }
      let_go_of_hold_note = checkpoints_missed > 0 || checkpoints_hit == 0;

      // TRICKY: If the hold is so short that it has no checkpoints,
      // then mark it as Held if the head was stepped on.
      if (checkpoints_hit == 0 && checkpoints_missed == 0) {
        let_go_of_hold_note = !stepped_on_head;
      }

      /*
      if(bLetGoOfHoldNote)
              LOG->Trace("let go of hold note, life is 0");
      else
              LOG->Trace("did not let go of hold note :D");
      */
    } else {
      // LOG->Trace("(hold checkpoints disabled.)");
      let_go_of_hold_note = life == 0;
      /*
      if(bLetGoOfHoldNote)
              LOG->Trace("let go of hold note, life is 0");
      else
              LOG->Trace("did not let go of hold note :D");
      */
    }

    if (initiated_note) {
      if (!let_go_of_hold_note) {
        // LOG->Trace("initiated note and didn't let go");
        life = 1;  // xxx: should be MAX_HOLD_LIFE instead? -aj
        hns = HNS_Held;
        bool bright = player_stage_stats_ &&
                      player_stage_stats_->cur_combo_ >
                          (unsigned int)BRIGHT_GHOST_COMBO_THRESHOLD;
        if (note_field_) {
          for (const TrackRowTapNote& trtn : v_tn) {
            if (trtn.tn_->pn_ != kPlayerInvalid &&
                trtn.tn_->pn_ != player_state_->player_number_) {
              continue;
            }
            int track = trtn.track_;
            note_field_->DidHoldNote(
                track, HNS_Held, bright);  // bright ghost flash
          }
        }
      }

      else {
        // LOG->Trace("initiated note and let go :(");
      }
    } else if (SCORE_MISSED_HOLDS_AND_ROLLS) {
      hns = HNS_LetGo;
    } else {
      hns = HNS_Missed;
    }
  }

  float life_fraction = life / MAX_HOLD_LIFE;

  for (const TrackRowTapNote& trtn : v_tn) {
    TapNote& tn = *trtn.tn_;
    // Routine charts assign each note to a specific player; skip notes
    // that don't belong to this Player
    if (tn.pn_ != kPlayerInvalid && tn.pn_ != player_state_->player_number_) {
      continue;
    }
    tn.hold_result_.life_ = life;
    tn.hold_result_.hns_ = hns;
    // Stop the playing keysound for the hold note.
    // I think this causes crashes too. -aj
    // This can still crash. I think it expects a full game and quit before the
    // preference works: otherwise, it causes problems on holds. At least, that
    // hapened on my Mac. -wolfman2000

    Preference<float>* volume =
        Preference<float>::GetPreferenceByName("SoundVolume");
    if (volume != nullptr) {
      float vol = volume->Get();

      if (tn.keysound_index_ >= 0 &&
          tn.keysound_index_ < (int)keysounds_.size()) {
        float factor =
            (tn.sub_type_ == TapNoteSubType_Roll
                 ? 2.0f * life_fraction
                 : 10.0f * life_fraction - 8.5f);
        keysounds_[tn.keysound_index_].SetProperty(
            "Volume", std::max(0.0f, std::min(1.0f, factor)) * vol);
      }
    }
  }

  if ((hns == HNS_LetGo) && COMBO_BREAK_ON_IMMEDIATE_HOLD_LET_GO) {
    IncrementMissCombo();
  }

  if (hns != HNS_None) {
    // LOG->Trace("tap note scoring time.");
    TapNote& tn = *v_tn[0].tn_;
    // Routine charts assign each note to a specific player; skip notes
    // that don't belong to this Player
    if (tn.pn_ != kPlayerInvalid && tn.pn_ != player_state_->player_number_) {
      return;
    }
    SetHoldJudgment(tn, first_track_with_max_end_row);
    HandleHoldScore(tn);
    // LOG->Trace("hold result =
    // %s",StringConversion::ToString(tn.HoldResult.hns).c_str());
  }
  // LOG->Trace("[Player::UpdateHoldNotes] ends");
}

void Player::ApplyWaitingTransforms() {
  for (unsigned j = 0; j < player_state_->mods_to_apply_.size(); j++) {
    const Attack& mod = player_state_->mods_to_apply_[j];
    PlayerOptions po;
    // if re-adding noteskin changes, blank out po.m_sNoteSkin. -aj
    po.FromString(mod.modifiers_);

    float start_beat, end_beat;
    mod.GetRealtimeAttackBeats(
        GAMESTATE->cur_song_, player_state_, start_beat, end_beat);
    end_beat = std::min(end_beat, note_data_.GetLastBeat());

    LOG->Trace(
        "Applying transform '%s' from %f to %f to '%s'", mod.modifiers_.c_str(),
        start_beat, end_beat,
        GAMESTATE->cur_song_->GetTranslitMainTitle().c_str());

    // if re-adding noteskin changes, this is one place to edit -aj

    NoteDataUtil::TransformNoteData(
        note_data_, *timing_, po,
        GAMESTATE->GetCurrentStyle(GetPlayerState()->player_number_)
            ->steps_type_,
        BeatToNoteRow(start_beat), BeatToNoteRow(end_beat));
  }
  player_state_->mods_to_apply_.clear();
}

void Player::DrawPrimitives() {
  // TODO: Remove use of PlayerNumber.
  PlayerNumber pn = player_state_->player_number_;

  // May have both players in doubles (for battle play); only draw primary
  // player.
  if (GAMESTATE->GetCurrentStyle(GetPlayerState()->player_number_)
              ->style_type_ == StyleType_OnePlayerTwoSides &&
      pn != GAMESTATE->GetMasterPlayerNumber()) {
    return;
  }

  bool draw_notefield = note_field_ && !IsOniDead();

  const PlayerOptions& curr_options =
      player_state_->player_options_.GetCurrent();
  float tilt = curr_options.perspective_tilt_;
  float skew = curr_options.skew_;
  float mini = curr_options.effects_[PlayerOptions::EFFECT_MINI];
  float center_y =
      GetY() + (GRAY_ARROWS_Y_STANDARD + GRAY_ARROWS_Y_REVERSE) / 2;
  bool reverse = curr_options.GetReversePercentForColumn(0) > .5;

  if (drawing_notefield_board_) {
    // Ask the Notefield to draw its board primitive before everything else
    // so that things drawn under the field aren't behind the opaque board.
    // -Kyz
    if (draw_notefield) {
      PlayerNoteFieldPositioner poser(
          this, GetX(), tilt, skew, mini, center_y, reverse);
      note_field_->DrawBoardPrimitive();
    }
    return;
  }

  // Draw these below everything else.
  if (COMBO_UNDER_FIELD && curr_options.blind_ == 0) {
    if (spr_combo_) {
      spr_combo_->Draw();
    }
  }

  if (attack_display_) {
    attack_display_->Draw();
  }

  if (TAP_JUDGMENTS_UNDER_FIELD) {
    DrawTapJudgments();
  }

  if (HOLD_JUDGMENTS_UNDER_FIELD) {
    DrawHoldJudgments();
  }

  if (draw_notefield) {
    PlayerNoteFieldPositioner poser(
        this, GetX(), tilt, skew, mini, center_y, reverse);
    note_field_->Draw();
  }

  // m_pNoteField->m_sprBoard->GetVisible()
  if (!COMBO_UNDER_FIELD && curr_options.blind_ == 0) {
    if (spr_combo_) {
      spr_combo_->Draw();
    }
  }

  if (!(bool)TAP_JUDGMENTS_UNDER_FIELD) {
    DrawTapJudgments();
  }

  if (!(bool)HOLD_JUDGMENTS_UNDER_FIELD) {
    DrawHoldJudgments();
  }
}

void Player::PushPlayerMatrix(float x, float skew, float center_y) {
  DISPLAY->CameraPushMatrix();
  DISPLAY->PushMatrix();
  DISPLAY->LoadMenuPerspective(
      45, SCREEN_WIDTH, SCREEN_HEIGHT,
      SCALE(skew, 0.1f, 1.0f, x, SCREEN_CENTER_X), center_y);
}

void Player::PopPlayerMatrix() {
  DISPLAY->CameraPopMatrix();
  DISPLAY->PopMatrix();
}

void Player::DrawNoteFieldBoard() {
  drawing_notefield_board_ = true;
  Draw();
  drawing_notefield_board_ = false;
}

Player::PlayerNoteFieldPositioner::PlayerNoteFieldPositioner(
    Player* p, float x, float tilt, float skew, float mini, float center_y,
    bool reverse)
    : player_(p) {
  player_->PushPlayerMatrix(x, skew, center_y);
  float reverse_mult = (reverse ? -1 : 1);
  original_y_ = player_->note_field_->GetY();
  float tilt_degrees = SCALE(tilt, -1.f, +1.f, +30, -30) * reverse_mult;
  float zoom = SCALE(mini, 0.f, 1.f, 1.f, .5f);
  // Something strange going on here.  Notice that the range for tilt's
  // effect on y_offset goes to -45 when positive, but -20 when negative.
  // I don't know why it's done this why, simply preserving old behavior.
  // -Kyz
  if (tilt > 0) {
    zoom *= SCALE(tilt, 0.f, 1.f, 1.f, 0.9f);
    y_offset_ = SCALE(tilt, 0.f, 1.f, 0.f, -45.f) * reverse_mult;
  } else {
    zoom *= SCALE(tilt, 0.f, -1.f, 1.f, 0.9f);
    y_offset_ = SCALE(tilt, 0.f, -1.f, 0.f, -20.f) * reverse_mult;
  }
  player_->note_field_->SetY(original_y_ + y_offset_);
  if (player_->oitg_zoom_mode_) {
    player_->note_field_->SetZoomX(zoom);
    player_->note_field_->SetZoomY(zoom);
  } else {
    player_->note_field_->SetZoom(zoom);
  }
  player_->note_field_->SetRotationX(tilt_degrees);
}

Player::PlayerNoteFieldPositioner::~PlayerNoteFieldPositioner() {
  player_->note_field_->SetY(original_y_);
  player_->PopPlayerMatrix();
}

void Player::DrawTapJudgments() {
  if (player_state_->player_options_.GetCurrent().blind_ > 0) {
    return;
  }

  if (spr_judgment_) {
    spr_judgment_->Draw();
  }
}

void Player::DrawHoldJudgments() {
  if (player_state_->player_options_.GetCurrent().blind_ > 0) {
    return;
  }

  for (int c = 0; c < note_data_.GetNumTracks(); c++) {
    if (hold_judgment_[c]) {
      hold_judgment_[c]->Draw();
    }
  }
}

void Player::ChangeLife(TapNoteScore tns) {
  PlayerNumber pn = player_state_->player_number_;
  if (life_meter_) {
    life_meter_->ChangeLife(tns);
  }
  if (combined_life_meter_) {
    combined_life_meter_->ChangeLife(pn, tns);
  }

  ChangeLifeRecord();

  switch (tns) {
    case TNS_None:
    case TNS_Miss:
    case TNS_CheckpointMiss:
    case TNS_HitMine:
      ++player_state_->taps_missed_since_last_haste_update_;
      break;
    default:
      ++player_state_->taps_hit_since_last_haste_update_;
      break;
  }
}

void Player::ChangeLife(HoldNoteScore hns, TapNoteScore tns) {
  PlayerNumber pn = player_state_->player_number_;
  if (life_meter_) {
    life_meter_->ChangeLife(hns, tns);
  }
  if (combined_life_meter_) {
    combined_life_meter_->ChangeLife(pn, hns, tns);
  }

  ChangeLifeRecord();
}

void Player::ChangeLife(float delta) {
  // If ChangeLifeRecord is not called before the change, then the life graph
  // will show a gradual change from the time of the previous step (or
  // change) to the time of this change, instead of the sharp change that
  // actually occurred. -Kyz
  ChangeLifeRecord();
  PlayerNumber pn = player_state_->player_number_;
  if (life_meter_) {
    life_meter_->ChangeLife(delta);
  }
  if (combined_life_meter_) {
    combined_life_meter_->ChangeLife(pn, delta);
  }
  ChangeLifeRecord();
}

void Player::SetLife(float value) {
  // If ChangeLifeRecord is not called before the change, then the life graph
  // will show a gradual change from the time of the previous step (or
  // change) to the time of this change, instead of the sharp change that
  // actually occurred. -Kyz
  ChangeLifeRecord();
  PlayerNumber pn = player_state_->player_number_;
  if (life_meter_) {
    life_meter_->SetLife(value);
  }
  if (combined_life_meter_) {
    combined_life_meter_->SetLife(pn, value);
  }
  ChangeLifeRecord();
}

void Player::ChangeLifeRecord() {
  PlayerNumber pn = player_state_->player_number_;
  float life = -1;
  if (life_meter_) {
    life = life_meter_->GetLife();
  } else if (combined_life_meter_) {
    life = GAMESTATE->tug_life_percent_p1_;
    if (pn == PLAYER_2) {
      life = 1.0f - life;
    }
  }
  if (life != -1) {
    if (player_stage_stats_) {
      player_stage_stats_->SetLifeRecordAt(
          life, STATSMAN->cur_stage_stats_.steps_seconds_);
    }
  }
}

int Player::GetClosestNoteDirectional(
    int col, int start_row, int end_row, bool allow_graded,
    bool forward) const {
  NoteData::const_iterator begin, end;
  note_data_.GetTapNoteRange(col, start_row, end_row, begin, end);

  if (!forward) {
    swap(begin, end);
  }

  while (begin != end) {
    if (!forward) {
      --begin;
    }

    // Is this the row we want?
    do {
      const TapNote& tn = begin->second;
      if (!timing_->IsJudgableAtRow(begin->first)) {
        break;
      }
      // unsure if autoKeysounds should be excluded. -Wolfman2000
      if (tn.type_ == TapNoteType_Empty ||
          tn.type_ == TapNoteType_AutoKeysound) {
        break;
      }
      if (!allow_graded && tn.result_.tns_ != TNS_None) {
        break;
      }

      return begin->first;
    } while (0);

    if (forward) {
      ++begin;
    }
  }

  return -1;
}

// Find the closest note to fBeat.
int Player::GetClosestNote(
    int col, int note_row, int max_rows_ahead, int max_rows_behind,
    bool allow_graded) const {
  // Start at iIndexStartLookingAt and search outward.
  int next_index = GetClosestNoteDirectional(
      col, note_row, note_row + max_rows_ahead, allow_graded, true);
  int prev_index = GetClosestNoteDirectional(
      col, note_row - max_rows_behind, note_row, allow_graded, false);

  if (next_index == -1 && prev_index == -1) {
    return -1;
  }
  if (next_index == -1) {
    return prev_index;
  }
  if (prev_index == -1) {
    return next_index;
  }

  /* Figure out which row is closer. */
  if (std::abs(note_row - next_index) > std::abs(note_row - prev_index)) {
    return prev_index;
  } else {
    return next_index;
  }
}

bool Player::IsOniDead() const {
  // If we're playing on oni and we've died, do nothing.
  return player_state_->player_options_.GetStage().life_type_ ==
             LifeType_Battery &&
         player_stage_stats_ && player_stage_stats_->failed_;
}

void Player::DoTapScoreNone() {
  Message msg("ScoreNone");
  MESSAGEMAN->Broadcast(msg);

  const unsigned int old_combo =
      player_stage_stats_ ? player_stage_stats_->cur_combo_ : 0;
  const unsigned int old_miss_combo =
      player_stage_stats_ ? player_stage_stats_->cur_miss_combo_ : 0;

  /* The only real way to tell if a mine has been scored is if it has
   * disappeared but this only works for hit mines so update the scores for
   * avoided mines here. */
  if (primary_score_keeper_) {
    primary_score_keeper_->HandleTapScoreNone();
  }
  if (secondary_score_keeper_) {
    secondary_score_keeper_->HandleTapScoreNone();
  }

  SendComboMessages(old_combo, old_miss_combo);

  if (life_meter_) {
    life_meter_->HandleTapScoreNone();
  }
  // TODO: Remove use of PlayerNumber
  PlayerNumber pn = kPlayerInvalid;
  if (combined_life_meter_) {
    combined_life_meter_->HandleTapScoreNone(pn);
  }

  if (PENALIZE_TAP_SCORE_NONE) {
    SetJudgment(
        BeatToNoteRow(player_state_->position_.song_beat_), -1, tap_empty,
        TNS_Miss, 0);
    // the ScoreKeeper will subtract points later.
  }
}

void Player::PlayKeysound(const TapNote& tn, TapNoteScore score) {
  // tap note must have keysound
  if (tn.keysound_index_ >= 0 && tn.keysound_index_ < (int)keysounds_.size()) {
    // handle a case for hold notes
    if (tn.type_ == TapNoteType_HoldHead) {
      // if the hold is not already held
      if (tn.hold_result_.hns_ == HNS_None) {
        // if the hold is already activated
        TapNoteScore tns = tn.result_.tns_;
        if (tns != TNS_None && tns != TNS_Miss && score == TNS_None) {
          // the sound must also be already playing
          if (keysounds_[tn.keysound_index_].IsPlaying()) {
            // if all of these conditions are met, don't play the sound.
            return;
          }
        }
      }
    }
    keysounds_[tn.keysound_index_].Play(false);
    Preference<float>* volume =
        Preference<float>::GetPreferenceByName("SoundVolume");
    float vol = volume->Get();
    keysounds_[tn.keysound_index_].SetProperty("Volume", vol);
  }
}

void Player::Step(
    int col, int row, const RageTimer& tm, bool held, bool release) {
  if (IsOniDead()) {
    return;
  }

  // Do everything that depends on a RageTimer here;
  // set your breakpoints somewhere after this block.
  const float last_beat_update =
      player_state_->position_.last_beat_update_.Ago();
  const float position_seconds =
      player_state_->position_.music_seconds_ - tm.Ago();
  const float time_since_step = tm.Ago();

  float song_beat = player_state_->position_.song_beat_;

  if (GAMESTATE->cur_song_) {
    song_beat = GAMESTATE->cur_song_->song_timing_.GetBeatFromElapsedTime(
        position_seconds);

    if (GAMESTATE->cur_steps_[player_state_->player_number_]) {
      song_beat = timing_->GetBeatFromElapsedTime(position_seconds);
    }
  }

  const int song_row = row == -1 ? BeatToNoteRow(song_beat) : row;
  // If we're playinng on TwoPlayerSharedSides, we need to check player number
  // to determine which side of the screen we're on.
  if (GAMESTATE->GetCurrentStyle(player_state_->player_number_)->style_type_ ==
      StyleType_TwoPlayersSharedSides) {
    const int step_search_rows =
        std::max(
            BeatToNoteRow(timing_->GetBeatFromElapsedTime(
                player_state_->position_.music_seconds_ +
                kStepSearchDistance)) -
                song_row,
            song_row - BeatToNoteRow(timing_->GetBeatFromElapsedTime(
                           player_state_->position_.music_seconds_ -
                           kStepSearchDistance))) +
        kRowsPerBeat;
    int row_of_overlapping_note_or_row = row;
    if (row == -1) {
      row_of_overlapping_note_or_row = GetClosestNote(
          col, song_row, step_search_rows, step_search_rows, false);
    }
    if (row_of_overlapping_note_or_row != -1) {
      NoteData::iterator iter =
          note_data_.FindTapNote(col, row_of_overlapping_note_or_row);
      if (iter != note_data_.end(col)) {
        const TapNote& tn = iter->second;
        if (tn.pn_ != kPlayerInvalid &&
            tn.pn_ != player_state_->player_number_) {
          return;
        }
      }
    }
  }

  if (col != -1 && !release) {
    // Update roll life
    // Let's not check the whole array every time.
    // Instead, only check 1 beat back.  Even 1 is overkill.
    // Just update the life here and let Update judge the roll.
    const int start_checking_at = std::max(0, song_row - BeatToNoteRow(1));
    NoteData::TrackMap::iterator begin, end;
    note_data_.GetTapNoteRangeInclusive(
        col, start_checking_at, song_row + 1, begin, end);
    for (; begin != end; ++begin) {
      TapNote& tn = begin->second;
      if (tn.type_ != TapNoteType_HoldHead) {
        continue;
      }

      switch (tn.sub_type_) {
        DEFAULT_FAIL(tn.sub_type_);
        case TapNoteSubType_Hold:
          continue;
        case TapNoteSubType_Roll:
          break;
      }

      const int i_row = begin->first;

      HoldNoteScore hns = tn.hold_result_.hns_;
      if (hns != HNS_None) {  // if this HoldNote already has a result
        continue;             // we don't need to update the logic for this one
      }

      // if they got a bad score or haven't stepped on the corresponding tap yet
      const TapNoteScore tns = tn.result_.tns_;
      bool initiated_note = true;
      if (REQUIRE_STEP_ON_HOLD_HEADS) {
        initiated_note =
            ((tns != TNS_None && tns != TNS_Miss) ||
             tn.result_.early_tns_ != TNS_None);  // did they step on the start?
      }
      const int end_row = i_row + tn.duration_;

      if (initiated_note && tn.hold_result_.life_ != 0) {
        /* This hold note is not judged and we stepped on its head.  Update
         * iLastHeldRow. Do this even if we're a little beyond the end of the
         * hold note, to make sure iLastHeldRow is clamped to iEndRow if the
         * hold note is held all the way. */
        // LOG->Trace("setting iLastHeldRow to min of iSongRow (%i) and iEndRow
        // (%i)",iSongRow,iEndRow);
        tn.hold_result_.last_held_row_ = std::min(song_row, end_row);
      }

      // If the song beat is in the range of this hold:
      if (i_row <= song_row && i_row <= end_row) {
        if (initiated_note) {
          // Increase life
          tn.hold_result_.life_ = 1;

          if (ROLL_BODY_INCREMENTS_COMBO &&
              player_state_->player_controller_ != PC_AUTOPLAY) {
            IncrementCombo();

            bool bright = player_stage_stats_ &&
                          player_stage_stats_->cur_combo_ >
                              (unsigned int)BRIGHT_GHOST_COMBO_THRESHOLD;
            if (note_field_) {
              note_field_->DidHoldNote(col, HNS_Held, bright);
            }
          }
        }
        break;
      }
    }
  }

  // Count calories for this step, unless we're being called because a button
  // is held over a mine or being released.
  // TODO: Move calorie counting into a ScoreKeeper?
  if (player_stage_stats_ && player_state_ && !held && !release) {
    // TODO: remove use of PlayerNumber
    PlayerNumber pn = player_state_->player_number_;
    Profile* profile = PROFILEMAN->GetProfile(pn);

    int num_tracks_held = 0;
    for (int t = 0; t < note_data_.GetNumTracks(); t++) {
      std::vector<GameInput> game_i;
      GAMESTATE->GetCurrentStyle(GetPlayerState()->player_number_)
          ->StyleInputToGameInput(t, pn, game_i);
      float secs_held = 0.0f;
      for (size_t i = 0; i < game_i.size(); ++i) {
        secs_held = std::max(secs_held, INPUTMAPPER->GetSecsHeld(game_i[i]));
      }
      if (secs_held > 0 && secs_held < timing_window_jump) {
        num_tracks_held++;
      }
    }

    float cals = 0;
    switch (num_tracks_held) {
      case 0:
        // autoplay is on, or this is a computer player
        num_tracks_held = 1;
        [[fallthrough]];
      default: {
        float cals_for100_lbs = SCALE(num_tracks_held, 1, 2, 0.023f, 0.077f);
        float cals_for200_lbs = SCALE(num_tracks_held, 1, 2, 0.041f, 0.133f);
        cals = SCALE(
            profile->GetCalculatedWeightPounds(), 100.f, 200.f, cals_for100_lbs,
            cals_for200_lbs);
      } break;
    }

    player_stage_stats_->calories_burned_ += cals;
    player_stage_stats_->num_controller_steps_++;
  }

  // Check for step on a TapNote
  /* XXX: This seems wrong. If a player steps twice quickly and two notes are
   * close together in the same column then it is possible for the two notes
   * to be graded out of order.
   * Two possible fixes:
   * 1. Adjust the fSongBeat (or the resulting note row) backward by
   * iStepSearchRows and search forward two iStepSearchRows lengths,
   * disallowing graded. This doesn't seem right because if a second note has
   * passed, an earlier one should not be graded.
   * 2. Clamp the distance searched backward to the previous row graded.
   * Either option would fundamentally change the grading of two quick notes
   * "jack hammers." Hmm.
   */
  const int step_search_rows =
      std::max(
          BeatToNoteRow(timing_->GetBeatFromElapsedTime(
              player_state_->position_.music_seconds_ + kStepSearchDistance)) -
              song_row,
          song_row - BeatToNoteRow(timing_->GetBeatFromElapsedTime(
                         player_state_->position_.music_seconds_ -
                         kStepSearchDistance))) +
      kRowsPerBeat;
  int row_of_overlapping_note_or_row = row;
  if (row == -1) {
    row_of_overlapping_note_or_row = GetClosestNote(
        col, song_row, step_search_rows, step_search_rows, false);
  }

  // calculate TapNoteScore
  TapNoteScore score = TNS_None;

  if (row_of_overlapping_note_or_row != -1) {
    // compute the score for this hit
    float note_offset = 0.0f;
    // we need this later if we are autosyncing
    const float step_beat = NoteRowToBeat(row_of_overlapping_note_or_row);
    const float step_seconds = timing_->GetElapsedTimeFromBeat(step_beat);

    if (row == -1) {
      // We actually stepped on the note this long ago:
      // fTimeSinceStep

      /* GAMESTATE->m_fMusicSeconds is the music time as of
       * GAMESTATE->m_LastBeatUpdate. Figure out what the music time is as of
       * now. */
      const float current_music_seconds =
          player_state_->position_.music_seconds_ +
          (last_beat_update *
           GAMESTATE->song_options_.GetCurrent().music_rate_);

      // ... which means it happened at this point in the music:
      const float music_seconds =
          current_music_seconds -
          time_since_step * GAMESTATE->song_options_.GetCurrent().music_rate_;

      // The offset from the actual step in seconds:
      note_offset = (step_seconds - music_seconds) /
                    GAMESTATE->song_options_.GetCurrent()
                        .music_rate_;  // account for music rate
                                       /*
                                       LOG->Trace("step was %.3f ago, music is off by %f: %f vs %f, step was %f
                                       off",                                   fTimeSinceStep,
                                       GAMESTATE->m_LastBeatUpdate.Ago()/GAMESTATE->m_SongOptions.m_fMusicRate,
                                               fStepSeconds, fMusicSeconds, fNoteOffset );
                                       */
    }

    const float seconds_from_exact = std::abs(note_offset);

    TapNote* p_tn = nullptr;
    NoteData::iterator iter =
        note_data_.FindTapNote(col, row_of_overlapping_note_or_row);
    DEBUG_ASSERT(iter != note_data_.end(col));
    p_tn = &iter->second;

    switch (player_state_->player_controller_) {
      case PC_HUMAN:
        switch (p_tn->type_) {
          case TapNoteType_Mine:
            // Stepped too close to mine?
            if (!release &&
                (!REQUIRE_STEP_ON_MINES || REQUIRE_STEP_ON_MINES == !held) &&
                seconds_from_exact <= GetWindowSeconds(TW_Mine)) {
              score = TNS_HitMine;
            }
            break;
          case TapNoteType_Attack:
            if (!release && seconds_from_exact <= GetWindowSeconds(TW_Attack) &&
                !p_tn->result_.hidden_) {
              score = AllowW1() ? TNS_W1 : TNS_W2;  // sentinel
            }
            break;
          case TapNoteType_HoldHead:
            // oh wow, this was causing the trigger before the hold heads
            // bug. (It was fNoteOffset > 0.f before) -DaisuMaster
            if (!REQUIRE_STEP_ON_HOLD_HEADS &&
                (note_offset <= GetWindowSeconds(TW_W5) &&
                 GetWindowSeconds(TW_W5) != 0)) {
              // Set it to the first non-disabled window.
              const auto& disabled_windows =
                  player_state_->player_options_.GetCurrent().disabled_windows_;
              if (!disabled_windows[TW_W1]) {
                score = TNS_W1;
              } else if (!disabled_windows[TW_W2]) {
                score = TNS_W2;
              } else if (!disabled_windows[TW_W3]) {
                score = TNS_W3;
              } else if (!disabled_windows[TW_W4]) {
                score = TNS_W4;
              } else if (!disabled_windows[TW_W5]) {
                score = TNS_W5;
              }

              break;
            }
            [[fallthrough]];
          default:
            if ((p_tn->type_ == TapNoteType_Lift) == release) {
              const auto& disabled_windows =
                  player_state_->player_options_.GetCurrent().disabled_windows_;
              if (seconds_from_exact <= GetWindowSeconds(TW_W1) &&
                  !disabled_windows[TW_W1]) {
                score = TNS_W1;
              } else if (
                  seconds_from_exact <= GetWindowSeconds(TW_W2) &&
                  !disabled_windows[TW_W2]) {
                score = TNS_W2;
              } else if (
                  seconds_from_exact <= GetWindowSeconds(TW_W3) &&
                  !disabled_windows[TW_W3]) {
                score = TNS_W3;
              } else if (
                  seconds_from_exact <= GetWindowSeconds(TW_W4) &&
                  !disabled_windows[TW_W4]) {
                score = TNS_W4;
              } else if (
                  seconds_from_exact <= GetWindowSeconds(TW_W5) &&
                  !disabled_windows[TW_W5]) {
                score = TNS_W5;
              }
            }
            break;
        }
        break;

      case PC_CPU:
      case PC_AUTOPLAY: {
        score = PlayerAI::GetTapNoteScore(player_state_);

        /* XXX: This doesn't make sense.
         * Step should only be called in autoplay for hit notes. */
#if 0
			// GetTapNoteScore always returns TNS_W1 in autoplay.
			// If the step is far away, don't judge it.
			if( player_state_->player_controller_ == PC_AUTOPLAY &&
				fSecondsFromExact > GetWindowSeconds(TW_W5) )
			{
				score = TNS_None;
				break;
			}
#endif

        // TRICKY:  We're asking the AI to judge mines. Consider TNS_W4 and
        // below as "mine was hit" and everything else as "mine was avoided"
        if (p_tn->type_ == TapNoteType_Mine) {
          // The CPU hits a lot of mines. Only consider hitting the
          // first mine for a row. We know we're the first mine if
          // there are are no mines to the left of us.
          for (int t = 0; t < col; t++) {
            if (note_data_.GetTapNote(t, row_of_overlapping_note_or_row)
                    .type_ ==
                TapNoteType_Mine) {  // there's a mine to the left of us
              return;                // avoid
            }
          }

          // The CPU hits a lot of mines. Make it less likely to hit
          // mines that don't have a tap note on the same row.
          bool taps_on_row = note_data_.IsThereATapOrHoldHeadAtRow(
              row_of_overlapping_note_or_row);
          TapNoteScore get_to_avoid = taps_on_row ? TNS_W3 : TNS_W4;

          if (score >= get_to_avoid) {
            return;  // avoided
          } else {
            score = TNS_HitMine;
          }
        }

        if (p_tn->type_ == TapNoteType_Attack && score > TNS_W4) {
          score = TNS_W2;  // sentinel
        }

        /* AI will generate misses here. Don't handle a miss like a regular
         * note because we want the judgment animation to appear delayed.
         * Instead, return early if AI generated a miss, and let
         * UpdateTapNotesMissedOlderThan() detect and handle the misses. */
        if (score == TNS_Miss) {
          return;
        }

        // Put some small, random amount in fNoteOffset so that demonstration
        // show a mix of late and early. - Chris (StepMania r15628)
        // fNoteOffset = randomf( -0.1f, 0.1f );
        // Since themes may use the offset in a visual graph, the above
        // behavior is not the best thing to do. Instead, random numbers
        // should be generated based on the TapNoteScore, so that they can
        // logically match up with the current timing windows. -aj
        {
          float window_w1 = GetWindowSeconds(TW_W1);
          float window_w2 = GetWindowSeconds(TW_W2);
          float window_w3 = GetWindowSeconds(TW_W3);
          float window_w4 = GetWindowSeconds(TW_W4);
          float window_w5 = GetWindowSeconds(TW_W5);

          // W1 is the top judgment, there is no overlap.
          if (score == TNS_W1) {
            note_offset = randomf(-window_w1, window_w1);
          } else {
            // figure out overlap.
            float lower_bound = 0.0f;     // negative upper limit
            float upper_bound = 0.0f;     // positive lower limit
            float compare_window = 0.0f;  // filled in here:
            if (score == TNS_W2) {
              lower_bound = -window_w1;
              upper_bound = window_w1;
              compare_window = window_w2;
            } else if (score == TNS_W3) {
              lower_bound = -window_w2;
              upper_bound = window_w2;
              compare_window = window_w3;
            } else if (score == TNS_W4) {
              lower_bound = -window_w3;
              upper_bound = window_w3;
              compare_window = window_w4;
            } else if (score == TNS_W5) {
              lower_bound = -window_w4;
              upper_bound = window_w4;
              compare_window = window_w5;
            }
            float f1 = randomf(-compare_window, lower_bound);
            float f2 = randomf(upper_bound, compare_window);

            if (randomf() * 100 >= 50) {
              note_offset = f1;
            } else {
              note_offset = f2;
            }
          }
        }

        break;

        /*
        case PC_REPLAY:
                // based on where we are, see what grade to get.
                score = PlayerAI::GetTapNoteScore( m_pPlayerState );
                // row is the current row, col is current column (track)
                fNoteOffset = TapNoteOffset attribute
                break;
        */
      }
      default:
        FAIL_M(ssprintf(
            "Invalid player controller type: %i",
            player_state_->player_controller_));
    }

    // handle attack notes
    if (p_tn->type_ == TapNoteType_Attack && score == TNS_W2) {
      score = TNS_None;  // don't score this as anything

      sound_attack_launch_.Play(false);

      // put attack in effect
      Attack attack(
          ATTACK_LEVEL_1,
          -1,  // now
          p_tn->attack_duration_seconds_, p_tn->attack_modifiers_, true, false);

      // TODO: Remove use of PlayerNumber
      PlayerNumber to_attack = kOppositePlayer[player_state_->player_number_];
      PlayerState* player_state_to_attack = GAMESTATE->player_state_[to_attack];
      player_state_to_attack->LaunchAttack(attack);

      // remove all TapAttacks on this row
      for (int t = 0; t < note_data_.GetNumTracks(); t++) {
        const TapNote& tn =
            note_data_.GetTapNote(t, row_of_overlapping_note_or_row);
        if (tn.type_ == TapNoteType_Attack) {
          HideNote(t, row_of_overlapping_note_or_row);
        }
      }
    }

    if (player_state_->player_controller_ == PC_HUMAN && score >= TNS_W3) {
      AdjustSync::HandleAutosync(note_offset, step_seconds);
    }

    // Do game-specific and mode-specific score mapping.
    score = GAMESTATE->GetCurrentGame()->MapTapNoteScore(score);
    if (score == TNS_W1 && !GAMESTATE->ShowW1()) {
      score = TNS_W2;
    }

    if (score != TNS_None) {
      TapNoteScore min_tns_to_score = min_tns_to_score_notes;
      bool bad_tns = false;
      if (min_tns_to_score != TNS_None) {
        switch (score) {
          case TNS_W1:
          case TNS_W2:
          case TNS_W3:
          case TNS_W4:
          case TNS_W5:
            if (score < min_tns_to_score) {
              bad_tns = true;
            }
            break;
          default:
            break;
        }
      }

      // Seems like we want to take the negation of "fNoteOffset" for
      // comparison. That's what we do with the fTapNoteOffset below, where
      // negative implies an early hit. Not sure why this is the case, but I
      // think this implies there's a bug above on line 2288.
      //
      // NOTE(teejusb): We have a few options for how to handle jumps with
      // re-hits. 1) Ignore trying re-hit jumps (which is what we do now). 2)
      // Let early hits for jumps count as the final judgment, but we have to
      //    remember to not actually trigger any life bar hits if
      //    CountNotesSeparately is false, until the final track has been
      //    finalized. Otherwise chord cohesion will be broken and players will
      //    incur unnecessary life bar hits. This option is a bit complicated so
      //    for now we'll stick with 1.
      int num_tracks_with_notes = note_data_.GetNumTracksWithTapOrHoldHead(
          row_of_overlapping_note_or_row);
      if (bad_tns && -note_offset < 0.0f && num_tracks_with_notes == 1) {
        // If a player gets an early way off as well as an early decent, we only
        // want to change the life once.
        if (p_tn->result_.early_tns_ == TNS_None) {
          p_tn->result_.early_tns_ = score;
          p_tn->result_.early_tap_note_offset_ = -note_offset;
          ChangeLife(score);
          // We don't want to trigger a JudgmentMessage since we use that to
          // indicate "finalized" judgments. We handle that elsewhere so instead
          // we introduce a new EarlyHitMessage to control the instant feedback
          // we want to relay to to the player.
          Message msg("EarlyHit");
          msg.SetParam("Player", player_state_->player_number_);
          msg.SetParam("TapNoteScore", score);
          msg.SetParam("Column", col);
          msg.SetParam("TapNoteOffset", -note_offset);
          MESSAGEMAN->Broadcast(msg);
        }
      } else {
        // If earlyTns is not set (good hit):
        //   - Default behavior
        // Otherwise:
        //   - this should only trigger a judgment and a score change if
        //   	 it's not a miss and the TNS is minTnsToScore or better.
        if (p_tn->result_.early_tns_ == TNS_None ||
            (score != TNS_Miss && !bad_tns)) {
          p_tn->result_.tns_ = score;
          p_tn->result_.tap_note_offset_ = -note_offset;
          p_tn->result_.held_ = false;
        }
      }
    }

    if (GAMESTATE->GetCurrentGame()->count_notes_separately_) {
      if (p_tn->type_ != TapNoteType_Mine) {
        const bool blind =
            (player_state_->player_options_.GetCurrent().blind_ != 0);
        // XXX: This is the wrong combo for shared players.
        // STATSMAN->m_CurStageStats.m_Player[pn] might work, but could be
        // wrong.
        const bool bright = (player_stage_stats_ &&
                             player_stage_stats_->cur_combo_ >
                                 (unsigned int)BRIGHT_GHOST_COMBO_THRESHOLD) ||
                            blind;
        if (note_field_) {
          note_field_->DidTapNote(col, blind ? TNS_W1 : score, bright);
        }
        if (score >= player_state_->player_options_.GetCurrent()
                         .min_tns_to_hide_notes_ ||
            blind) {
          HideNote(col, row_of_overlapping_note_or_row);
        }
      }
    } else if (NoteDataWithScoring::IsRowCompletelyJudged(
                   note_data_, row_of_overlapping_note_or_row)) {
      FlashGhostRow(row_of_overlapping_note_or_row);
    }
  }

  if (score == TNS_None) {
    DoTapScoreNone();
  }

  if (!release) {
    /* Search for keyed sounds separately.  Play the nearest note. */
    /* XXX: This isn't quite right. As per the above XXX for
     * iRowOfOverlappingNote, if iRowOfOverlappingNote is set to a previous
     * note, the keysound could have changed and this would cause the wrong one
     * to play, in essence playing two sounds in the opposite order. Maybe this
     * should always perform the search. Still, even that doesn't seem quite
     * right since it would then play the same (new) keysound twice which would
     * sound wrong even though the notes were judged as being correct, above.
     * Fixing the above problem would fix this one as well. */
    int head_row;
    if (row_of_overlapping_note_or_row != -1 && score != TNS_None) {
      // just pressing a note, use that row.
      // in other words, iRowOfOverlappingNoteOrRow = iRowOfOverlappingNoteOrRow
    } else if (note_data_.IsHoldNoteAtRow(col, song_row, &head_row)) {
      // stepping on a hold, use it!
      row_of_overlapping_note_or_row = head_row;
    } else {
      // or else find the closest note.
      row_of_overlapping_note_or_row =
          GetClosestNote(col, song_row, kMaxNoteRow, kMaxNoteRow, true);
    }
    if (row_of_overlapping_note_or_row != -1) {
      const TapNote& tn =
          note_data_.GetTapNote(col, row_of_overlapping_note_or_row);
      PlayKeysound(tn, score);
    }
  }
  // XXX:
  if (!release) {
    if (note_field_) {
      note_field_->Step(col, score);
    }
    Message msg("Step");
    msg.SetParam("PlayerNumber", player_state_->player_number_);
    msg.SetParam("Column", col);
    MESSAGEMAN->Broadcast(msg);
    // Backwards compatibility
    Message msg2(ssprintf("StepP%d", player_state_->player_number_ + 1));
    MESSAGEMAN->Broadcast(msg2);
  }
}

void Player::UpdateTapNotesMissedOlderThan(float miss_if_older_than_seconds) {
  // LOG->Trace( "Steps::UpdateTapNotesMissedOlderThan(%f)",
  // fMissIfOlderThanThisBeat );
  int miss_if_older_than_this_row;
  const float earliest_time =
      player_state_->position_.music_seconds_ - miss_if_older_than_seconds;
  {
    TimingData::GetBeatArgs beat_info;
    beat_info.elapsed_time_ = earliest_time;
    timing_->GetBeatAndBPSFromElapsedTime(beat_info);

    miss_if_older_than_this_row = BeatToNoteRow(beat_info.beat_);
    if (beat_info.freeze_out_ || beat_info.delay_out_) {
      /* If there is a freeze on iMissIfOlderThanThisIndex, include this index
       * too. Otherwise we won't show misses for tap notes on freezes until the
       * freeze finishes. */
      if (!beat_info.delay_out_) {
        miss_if_older_than_this_row++;
      }
    }
  }

  NoteData::all_tracks_iterator& iter = *iter_needs_tap_judging_;

  for (; !iter.IsAtEnd() && iter.Row() < miss_if_older_than_this_row; ++iter) {
    TapNote& tn = *iter;

    if (!NeedsTapJudging(tn)) {
      continue;
    }

    // Ignore all notes in WarpSegments or FakeSegments.
    if (!timing_->IsJudgableAtRow(iter.Row())) {
      continue;
    }

    if (tn.type_ == TapNoteType_Mine) {
      tn.result_.tns_ = TNS_AvoidMine;
      /* The only real way to tell if a mine has been scored is if it has
       * disappeared but this only works for hit mines so update the scores for
       * avoided mines here. */
      if (primary_score_keeper_) {
        primary_score_keeper_->HandleTapScore(tn);
      }
      if (secondary_score_keeper_) {
        secondary_score_keeper_->HandleTapScore(tn);
      }
    } else {
      if (tn.result_.early_tns_ != TNS_None) {
        tn.result_.tns_ = tn.result_.early_tns_;
        tn.result_.tap_note_offset_ = tn.result_.early_tap_note_offset_;
      } else {
        tn.result_.tns_ = TNS_Miss;
      }
    }
  }
}

void Player::UpdateJudgedRows() {
  // Look ahead far enough to catch any rows judged early.
  const int end_row = BeatToNoteRow(timing_->GetBeatFromElapsedTime(
      player_state_->position_.music_seconds_ + GetMaxStepDistanceSeconds()));
  bool all_judged = true;
  const bool separately = GAMESTATE->GetCurrentGame()->count_notes_separately_;

  {
    NoteData::all_tracks_iterator iter = *iter_unjudged_rows_;
    int last_seen_row = -1;
    for (; !iter.IsAtEnd() && iter.Row() <= end_row; ++iter) {
      int row = iter.Row();

      // Do not judge arrows in WarpSegments or FakeSegments
      if (!timing_->IsJudgableAtRow(row)) {
        continue;
      }

      if (last_seen_row != row) {
        last_seen_row = row;

        // crossed a nonempty row
        if (!NoteDataWithScoring::IsRowCompletelyJudged(note_data_, row)) {
          all_judged = false;
          continue;
        }
        if (all_judged) {
          *iter_unjudged_rows_ = iter;
        }
        if (judged_rows_->JudgeRow(row)) {
          continue;
        }
        const TapNoteResult& last_tnr =
            NoteDataWithScoring::LastTapNoteWithResult(note_data_, row).result_;

        if (last_tnr.tns_ < TNS_Miss) {
          continue;
        }
        if (separately) {
          for (int track = 0; track < note_data_.GetNumTracks(); ++track) {
            const TapNote& tn = note_data_.GetTapNote(track, row);
            if (tn.type_ == TapNoteType_Empty || tn.type_ == TapNoteType_Mine ||
                tn.type_ == TapNoteType_AutoKeysound) {
              continue;
            }
            SetJudgment(row, track, tn);
          }
        } else {
          SetJudgment(
              row, note_data_.GetFirstTrackWithTapOrHoldHead(row),
              NoteDataWithScoring::LastTapNoteWithResult(note_data_, row));
        }
        HandleTapRowScore(row);
      }
    }
  }

  // handle mines.
  {
    all_judged = true;
    std::set<RageSound*> set_sounds;
    NoteData::all_tracks_iterator iter = *iter_unjudged_mine_rows_;  // copy
    int last_seen_row = -1;
    for (; !iter.IsAtEnd() && iter.Row() <= end_row; ++iter) {
      int row = iter.Row();

      // Do not worry about mines in WarpSegments or FakeSegments
      if (!timing_->IsJudgableAtRow(row)) {
        continue;
      }

      TapNote& tn = *iter;

      if (row != last_seen_row) {
        last_seen_row = row;
        if (all_judged) {
          *iter_unjudged_mine_rows_ = iter;
        }
      }

      bool mine_not_hidden =
          tn.type_ == TapNoteType_Mine && !tn.result_.hidden_;
      if (!mine_not_hidden) {
        continue;
      }

      switch (tn.result_.tns_) {
        DEFAULT_FAIL(tn.result_.tns_);
        case TNS_None:
          all_judged = false;
          continue;
        case TNS_AvoidMine:
          SetMineJudgment(tn.result_.tns_, iter.Track());
          tn.result_.hidden_ = true;
          continue;
        case TNS_HitMine:
          SetMineJudgment(tn.result_.tns_, iter.Track());
          break;
      }
      if (note_field_) {
        note_field_->DidTapNote(iter.Track(), tn.result_.tns_, false);
      }

      if (tn.keysound_index_ >= 0 &&
          tn.keysound_index_ < (int)keysounds_.size()) {
        set_sounds.insert(&keysounds_[tn.keysound_index_]);
      } else if (enable_mine_sound_playback) {
        set_sounds.insert(&sound_mine_);
      }

      /* Attack Mines:
       * Only difference is these launch an attack rather than affecting
       * the lifebar. All the other mine impacts (score, dance points,
       * etc.) are still applied. */
      if (player_state_->player_options_.GetCurrent()
              .transforms_[PlayerOptions::TRANSFORM_ATTACKMINES]) {
        const float attack_run_time = ATTACK_RUN_TIME_MINE;

        Attack mine_attack;
        mine_attack.modifiers_ = ApplyRandomAttack();
        mine_attack.start_second_ = ATTACK_STARTS_NOW;
        mine_attack.secs_remaining_ = attack_run_time;

        player_state_->LaunchAttack(mine_attack);
      } else {
        ChangeLife(tn.result_.tns_);
      }

      if (score_display_) {
        score_display_->OnJudgment(tn.result_.tns_);
      }
      if (secondary_score_display_) {
        secondary_score_display_->OnJudgment(tn.result_.tns_);
      }

      // Make sure hit mines affect the dance points.
      if (primary_score_keeper_) {
        primary_score_keeper_->HandleTapScore(tn);
      }
      if (secondary_score_keeper_) {
        secondary_score_keeper_->HandleTapScore(tn);
      }
      tn.result_.hidden_ = true;
    }
    // If we hit the end of the loop, m_pIterUnjudgedMineRows needs to be
    // updated. -Kyz
    if ((iter.IsAtEnd() || last_seen_row == end_row) && all_judged) {
      *iter_unjudged_mine_rows_ = iter;
    }

    for (RageSound* sound : set_sounds) {
      // Only play one copy of each mine sound at a time per player.
      sound->Stop();
      sound->Play(false);
    }
  }
}

void Player::FlashGhostRow(int row) {
  TapNoteScore last_tns =
      NoteDataWithScoring::LastTapNoteWithResult(note_data_, row).result_.tns_;
  const bool blind = (player_state_->player_options_.GetCurrent().blind_ != 0);
  const bool bright =
      (player_stage_stats_ && player_stage_stats_->cur_combo_ >
                                  (unsigned int)BRIGHT_GHOST_COMBO_THRESHOLD) ||
      blind;

  for (int track = 0; track < note_data_.GetNumTracks(); ++track) {
    const TapNote& tn = note_data_.GetTapNote(track, row);

    if (tn.type_ == TapNoteType_Empty || tn.type_ == TapNoteType_Mine ||
        tn.type_ == TapNoteType_Fake || tn.result_.hidden_) {
      continue;
    }
    if (note_field_) {
      note_field_->DidTapNote(track, last_tns, bright);
    }
    if (last_tns >= player_state_->player_options_.GetCurrent()
                        .min_tns_to_hide_notes_ ||
        blind) {
      HideNote(track, row);
    }
  }
}

void Player::CrossedRows(int last_row_crossed, const RageTimer& now) {
  // LOG->Trace( "Player::CrossedRows   %d    %d", iFirstRowCrossed,
  // iLastRowCrossed );

  NoteData::all_tracks_iterator& iter = *iter_uncrossed_rows_;
  int last_seen_row = -1;
  for (; !iter.IsAtEnd() && iter.Row() <= last_row_crossed; ++iter) {
    // Apply InitialHoldLife.
    TapNote& tn = *iter;
    int row = iter.Row();
    int track = iter.Track();
    switch (tn.type_) {
      case TapNoteType_HoldHead: {
        tn.hold_result_.life_ = INITIAL_HOLD_LIFE;
        if (!REQUIRE_STEP_ON_HOLD_HEADS) {
          PlayerNumber pn = player_state_->player_number_;
          std::vector<GameInput> game_i;
          GAMESTATE->GetCurrentStyle(GetPlayerState()->player_number_)
              ->StyleInputToGameInput(track, pn, game_i);
          if (PREFSMAN->pad_stick_seconds_ > 0.f) {
            for (size_t i = 0; i < game_i.size(); ++i) {
              float secs_held = INPUTMAPPER->GetSecsHeld(game_i[i]);
              if (secs_held >= PREFSMAN->pad_stick_seconds_) {
                Step(
                    track, -1, now - PREFSMAN->pad_stick_seconds_, true, false);
              }
            }
          } else {
            if (INPUTMAPPER->IsBeingPressed(game_i)) {
              Step(track, -1, now, true, false);
            }
          }
        }
        break;
      }
      case TapNoteType_Mine: {
        // Hold the panel while crossing a mine will cause the mine to explode
        // TODO: Remove use of PlayerNumber.
        PlayerNumber pn = player_state_->player_number_;
        std::vector<GameInput> game_i;
        GAMESTATE->GetCurrentStyle(GetPlayerState()->player_number_)
            ->StyleInputToGameInput(track, pn, game_i);
        if (PREFSMAN->pad_stick_seconds_ > 0.0f) {
          for (size_t i = 0; i < game_i.size(); ++i) {
            float secs_held = INPUTMAPPER->GetSecsHeld(game_i[i]);
            if (secs_held >= PREFSMAN->pad_stick_seconds_) {
              Step(track, -1, now - PREFSMAN->pad_stick_seconds_, true, false);
            }
          }
        } else if (INPUTMAPPER->IsBeingPressed(game_i)) {
          Step(track, row, now, true, false);
        }
        break;
      }
      default:
        break;
    }

    // check to see if there's a note at the crossed row
    if (player_state_->player_controller_ != PC_HUMAN) {
      if (tn.type_ != TapNoteType_Empty && tn.type_ != TapNoteType_Fake &&
          tn.type_ != TapNoteType_AutoKeysound && tn.result_.tns_ == TNS_None &&
          this->timing_->IsJudgableAtRow(row)) {
        Step(track, row, now, false, false);
        if (player_state_->player_controller_ == PC_AUTOPLAY) {
          if (player_stage_stats_) {
            player_stage_stats_->disqualified_ = true;
          }
        }
      }
    }

    // TODO: Can we remove the iLastSeenRow logic and the
    // autokeysound for loop, since the iterator in this loop will
    // already be iterating over all of the tracks?
    if (row != last_seen_row) {
      // crossed a new not-empty row
      last_seen_row = row;

      // handle autokeysounds here (if not in the editor).
      if (!GAMESTATE->in_step_editor_) {
        for (int t = 0; t < note_data_.GetNumTracks(); ++t) {
          const TapNote& tap = note_data_.GetTapNote(t, row);
          if (tap.type_ == TapNoteType_AutoKeysound) {
            PlayKeysound(tap, TNS_None);
          }
        }
      }
    }
  }

  /* Update hold checkpoints
   *
   * TODO: Move this to a separate function. */
  if (tick_holds_ && player_state_->player_controller_ != PC_AUTOPLAY) {
    // Few rows typically cross per update. Easier to check all crossed rows
    // than to calculate from timing segments.
    for (int r = first_uncrossed_row_; r <= last_row_crossed; ++r) {
      int tick_current = timing_->GetTickcountAtRow(r);

      // There is a tick count at this row
      if (tick_current > 0 && r % (kRowsPerBeat / tick_current) == 0) {
        std::vector<int> cols_with_hold;
        int num_holds_held_this_row = 0;
        int num_holds_missed_this_row = 0;

        // start at r-1 so that we consider holds whose end rows are equal to
        // the checkpoint row
        NoteData::all_tracks_iterator n_iter =
            note_data_.GetTapNoteRangeAllTracks(r - 1, r, true);
        for (; !n_iter.IsAtEnd(); ++n_iter) {
          TapNote& tn = *n_iter;
          if (tn.type_ != TapNoteType_HoldHead) {
            continue;
          }

          int track = n_iter.Track();
          cols_with_hold.push_back(track);

          if (tn.hold_result_.life_ > 0) {
            ++num_holds_held_this_row;
            ++tn.hold_result_.checkpoints_hit_;
          } else {
            ++num_holds_missed_this_row;
            ++tn.hold_result_.checkpoints_missed_;
          }
        }
        GAMESTATE->SetProcessedTimingData(this->timing_);

        // TODO: Find a better way of handling hold checkpoints with other taps.
        if (!cols_with_hold.empty() &&
            (CHECKPOINTS_TAPS_SEPARATE_JUDGMENT ||
             note_data_.GetNumTapNotesInRow(r) == 0)) {
          HandleHoldCheckpoint(
              r, num_holds_held_this_row, num_holds_missed_this_row,
              cols_with_hold);
        }
      }
    }
  }

  first_uncrossed_row_ = last_row_crossed + 1;
}

void Player::HandleTapRowScore(unsigned row) {
  bool no_cheating = true;
#ifdef DEBUG
  no_cheating = false;
#endif

  // Do not score rows in WarpSegments or FakeSegments
  if (!timing_->IsJudgableAtRow(row)) {
    return;
  }

  if (GAMESTATE->demonstration_or_jukebox_) {
    no_cheating = false;
  }
  // don't accumulate points if AutoPlay is on.
  if (no_cheating && player_state_->player_controller_ == PC_AUTOPLAY) {
    return;
  }

  TapNote last_tap =
      NoteDataWithScoring::LastTapNoteWithResult(note_data_, row);
  TapNoteScore score_of_last_tap = last_tap.result_.tns_;
  const unsigned int iOldCombo =
      player_stage_stats_ ? player_stage_stats_->cur_combo_ : 0;
  const unsigned int old_miss_combo =
      player_stage_stats_ ? player_stage_stats_->cur_miss_combo_ : 0;

  for (int track = 0; track < note_data_.GetNumTracks(); ++track) {
    const TapNote& tn = note_data_.GetTapNote(track, row);
    // Mines cannot be handled here.
    if (tn.type_ == TapNoteType_Empty || tn.type_ == TapNoteType_Fake ||
        tn.type_ == TapNoteType_Mine || tn.type_ == TapNoteType_AutoKeysound) {
      continue;
    }
    if (primary_score_keeper_) {
      primary_score_keeper_->HandleTapScore(tn);
    }
    if (secondary_score_keeper_) {
      secondary_score_keeper_->HandleTapScore(tn);
    }
  }

  if (primary_score_keeper_ != nullptr) {
    primary_score_keeper_->HandleTapRowScore(note_data_, row);
  }
  if (secondary_score_keeper_ != nullptr) {
    secondary_score_keeper_->HandleTapRowScore(note_data_, row);
  }

  const unsigned int iCurCombo =
      player_stage_stats_ ? player_stage_stats_->cur_combo_ : 0;
  const unsigned int cur_miss_combo =
      player_stage_stats_ ? player_stage_stats_->cur_miss_combo_ : 0;

  SendComboMessages(iOldCombo, old_miss_combo);

  if (player_stage_stats_) {
    SetCombo(iCurCombo, cur_miss_combo);
  }

#define CROSSED(x) (iOldCombo < x && iCurCombo >= x)
  if (CROSSED(100)) {
    SCREENMAN->PostMessageToTopScreen(SM_100Combo, 0);
  } else if (CROSSED(200)) {
    SCREENMAN->PostMessageToTopScreen(SM_200Combo, 0);
  } else if (CROSSED(300)) {
    SCREENMAN->PostMessageToTopScreen(SM_300Combo, 0);
  } else if (CROSSED(400)) {
    SCREENMAN->PostMessageToTopScreen(SM_400Combo, 0);
  } else if (CROSSED(500)) {
    SCREENMAN->PostMessageToTopScreen(SM_500Combo, 0);
  } else if (CROSSED(600)) {
    SCREENMAN->PostMessageToTopScreen(SM_600Combo, 0);
  } else if (CROSSED(700)) {
    SCREENMAN->PostMessageToTopScreen(SM_700Combo, 0);
  } else if (CROSSED(800)) {
    SCREENMAN->PostMessageToTopScreen(SM_800Combo, 0);
  } else if (CROSSED(900)) {
    SCREENMAN->PostMessageToTopScreen(SM_900Combo, 0);
  } else if (CROSSED(1000)) {
    SCREENMAN->PostMessageToTopScreen(SM_1000Combo, 0);
  } else if ((iOldCombo / 100) < (iCurCombo / 100) && iCurCombo > 1000) {
    SCREENMAN->PostMessageToTopScreen(SM_ComboContinuing, 0);
  }
#undef CROSSED

  // new max combo
  if (player_stage_stats_) {
    player_stage_stats_->max_combo_ =
        std::max(player_stage_stats_->max_combo_, iCurCombo);
  }

  /* Use the real current beat, not the beat we've been passed. That's because
   * we want to record the current life/combo to the current time; eg. if it's
   * a MISS, the beat we're registering is in the past, but the life is changing
   * now. We need to include time from previous songs in a course, so we
   * can't use GAMESTATE->m_fMusicSeconds. Use fStepsSeconds instead. */
  if (player_stage_stats_) {
    player_stage_stats_->UpdateComboList(
        STATSMAN->cur_stage_stats_.steps_seconds_, false);
  }

  if (score_display_) {
    if (player_stage_stats_) {
      score_display_->SetScore(player_stage_stats_->score_);
    }
    score_display_->OnJudgment(score_of_last_tap);
  }
  if (secondary_score_display_) {
    if (player_stage_stats_) {
      secondary_score_display_->SetScore(player_stage_stats_->score_);
    }
    secondary_score_display_->OnJudgment(score_of_last_tap);
  }

  // if earlyTns is set, the life has already been affected for this note.
  // Don't want to change the life again for a re-hit.
  if (last_tap.result_.early_tns_ == TNS_None) {
    ChangeLife(score_of_last_tap);
  }
}

void Player::HandleHoldCheckpoint(
    int row, int num_holds_held_this_row, int num_holds_missed_this_row,
    const std::vector<int>& cols_with_hold) {
  bool no_cheating = true;
#ifdef DEBUG
  no_cheating = false;
#endif

  // WarpSegments and FakeSegments aren't judged in any way.
  if (!timing_->IsJudgableAtRow(row)) {
    return;
  }

  // don't accumulate combo if AutoPlay is on.
  if (no_cheating && player_state_->player_controller_ == PC_AUTOPLAY) {
    return;
  }

  const unsigned int old_combo =
      player_stage_stats_ ? player_stage_stats_->cur_combo_ : 0;
  const unsigned int old_miss_combo =
      player_stage_stats_ ? player_stage_stats_->cur_miss_combo_ : 0;

  if (primary_score_keeper_) {
    primary_score_keeper_->HandleHoldCheckpointScore(
        note_data_, row, num_holds_held_this_row, num_holds_missed_this_row);
  }
  if (secondary_score_keeper_) {
    secondary_score_keeper_->HandleHoldCheckpointScore(
        note_data_, row, num_holds_held_this_row, num_holds_missed_this_row);
  }

  if (num_holds_missed_this_row == 0) {
    // added for http://ssc.ajworld.net/sm-ssc/bugtracker/view.php?id=16 -aj
    if (CHECKPOINTS_FLASH_ON_HOLD && note_field_ != nullptr) {
      for (const int& i : cols_with_hold) {
        bool bright = player_stage_stats_ &&
                      player_stage_stats_->cur_combo_ >
                          (unsigned int)BRIGHT_GHOST_COMBO_THRESHOLD;
        if (note_field_) {
          note_field_->DidHoldNote(i, HNS_Held, bright);
        }
      }
    }
  }

  SendComboMessages(old_combo, old_miss_combo);

  if (player_stage_stats_) {
    SetCombo(
        player_stage_stats_->cur_combo_, player_stage_stats_->cur_miss_combo_);
    player_stage_stats_->UpdateComboList(
        STATSMAN->cur_stage_stats_.steps_seconds_, false);
  }

  ChangeLife(
      num_holds_missed_this_row == 0 ? TNS_CheckpointHit : TNS_CheckpointMiss);

  SetJudgment(
      row, cols_with_hold[0], tap_empty,
      num_holds_missed_this_row == 0 ? TNS_CheckpointHit : TNS_CheckpointMiss,
      0);
}

void Player::HandleHoldScore(const TapNote& tn) {
  HoldNoteScore hold_score = tn.hold_result_.hns_;
  TapNoteScore tap_score = tn.result_.tns_;
  bool no_cheating = true;
#ifdef DEBUG
  no_cheating = false;
#endif

  if (GAMESTATE->demonstration_or_jukebox_) {
    no_cheating = false;
  }
  // don't accumulate points if AutoPlay is on.
  if (no_cheating && player_state_->player_controller_ == PC_AUTOPLAY) {
    return;
  }

  if (primary_score_keeper_) {
    primary_score_keeper_->HandleHoldScore(tn);
  }
  if (secondary_score_keeper_) {
    secondary_score_keeper_->HandleHoldScore(tn);
  }

  if (score_display_) {
    if (player_stage_stats_) {
      score_display_->SetScore(player_stage_stats_->score_);
    }
    score_display_->OnJudgment(hold_score, tap_score);
  }
  if (secondary_score_display_) {
    if (player_stage_stats_) {
      secondary_score_display_->SetScore(player_stage_stats_->score_);
    }
    secondary_score_display_->OnJudgment(hold_score, tap_score);
  }

  ChangeLife(hold_score, tap_score);
}

float Player::GetMaxStepDistanceSeconds() {
  float max = 0;
  max = std::max(max, GetWindowSeconds(TW_W5));
  max = std::max(max, GetWindowSeconds(TW_W4));
  max = std::max(max, GetWindowSeconds(TW_W3));
  max = std::max(max, GetWindowSeconds(TW_W2));
  max = std::max(max, GetWindowSeconds(TW_W1));
  max = std::max(max, GetWindowSeconds(TW_Mine));
  max = std::max(max, GetWindowSeconds(TW_Hold));
  max = std::max(max, GetWindowSeconds(TW_Roll));
  max = std::max(max, GetWindowSeconds(TW_Attack));
  max = std::max(max, GetWindowSeconds(TW_Checkpoint));
  float f = GAMESTATE->song_options_.GetCurrent().music_rate_ * max;
  return f + max_input_latency_seconds;
}

void Player::FadeToFail() {
  if (note_field_) {
    note_field_->FadeToFail();
  }

  // clear miss combo
  SetCombo(0, 0);
}

void Player::CacheAllUsedNoteSkins() {
  if (note_field_) {
    note_field_->CacheAllUsedNoteSkins();
  }
}

void Player::SetMineJudgment(TapNoteScore tns, int track) {
  if (send_judgment_and_combo_messages_) {
    Message msg("Judgment");
    msg.SetParam("Player", player_state_->player_number_);
    msg.SetParam("TapNoteScore", tns);
    msg.SetParam("FirstTrack", track);
    MESSAGEMAN->Broadcast(msg);
    if (player_stage_stats_ &&
        ((tns == TNS_AvoidMine && AVOID_MINE_INCREMENTS_COMBO) ||
         (tns == TNS_HitMine && MINE_HIT_INCREMENTS_MISS_COMBO))) {
      SetCombo(
          player_stage_stats_->cur_combo_,
          player_stage_stats_->cur_miss_combo_);
    }
  }
}

void Player::SetJudgment(
    int row, int track, const TapNote& tn, TapNoteScore tns,
    float tap_note_offset) {
  if (send_judgment_and_combo_messages_) {
    Message msg("Judgment");
    msg.SetParam("Player", player_state_->player_number_);
    msg.SetParam("FirstTrack", track);
    msg.SetParam("TapNoteScore", tns);
    msg.SetParam("Early", tap_note_offset < 0.0f);
    msg.SetParam("TapNoteOffset", tn.result_.tap_note_offset_);
    msg.SetParam("EarlyTapNoteScore", tn.result_.early_tns_);
    msg.SetParam("EarlyTapNoteOffset", tn.result_.early_tap_note_offset_);

    if (tns == TNS_Miss) {
      msg.SetParam("HeldMiss", tn.result_.held_);
    }

    Lua* L = LUA->Get();
    lua_createtable(L, 0, note_data_.GetNumTracks());  // TapNotes this row
    lua_createtable(
        L, 0,
        note_data_.GetNumTracks());  // HoldHeads of tracks held at this row.

    for (int track_tmp = 0; track_tmp < note_data_.GetNumTracks();
         ++track_tmp) {
      NoteData::iterator tmp = note_data_.FindTapNote(track_tmp, row);
      if (tmp != note_data_.end(track_tmp)) {
        tmp->second.PushSelf(L);
        lua_rawseti(L, -3, track_tmp + 1);
      } else {
        int head_row;
        if (note_data_.IsHoldNoteAtRow(track_tmp, row, &head_row)) {
          NoteData::iterator hold = note_data_.FindTapNote(track_tmp, head_row);
          hold->second.PushSelf(L);
          lua_rawseti(L, -2, track_tmp + 1);
        }
      }
    }
    msg.SetParamFromStack(L, "Holds");
    msg.SetParamFromStack(L, "Notes");

    LUA->Release(L);
    MESSAGEMAN->Broadcast(msg);
  }
}

void Player::SetHoldJudgment(TapNote& tn, int track) {
  ASSERT(track < (int)hold_judgment_.size());
  if (hold_judgment_[track]) {
    hold_judgment_[track]->SetHoldJudgment(tn.hold_result_.hns_);
  }

  if (send_judgment_and_combo_messages_) {
    Message msg("Judgment");
    msg.SetParam("Player", player_state_->player_number_);
    msg.SetParam("FirstTrack", track);
    msg.SetParam("NumTracks", (int)hold_judgment_.size());
    msg.SetParam("TapNoteScore", tn.result_.tns_);
    msg.SetParam("HoldNoteScore", tn.hold_result_.hns_);
    msg.SetParam("EarlyTapNoteScore", tn.result_.early_tns_);

    Lua* L = LUA->Get();
    tn.PushSelf(L);
    msg.SetParamFromStack(L, "TapNote");
    LUA->Release(L);

    MESSAGEMAN->Broadcast(msg);
  }
}

void Player::SetCombo(unsigned int iCombo, unsigned int misses) {
  if (!seen_combo_yet_)  // first update, don't set bIsMilestone=true
  {
    seen_combo_yet_ = true;
    last_seen_combo_ = iCombo;
  }

  bool b25_milestone = false;
  bool b50_milestone = false;
  bool b100_milestone = false;
  bool b250_milestone = false;
  bool b1000_milestone = false;

#define MILESTONE_CHECK(amount) \
  ((iCombo / amount) > (last_seen_combo_ / amount))
  if (last_seen_combo_ < 600) {
    b25_milestone = MILESTONE_CHECK(25);
    b50_milestone = MILESTONE_CHECK(50);
    b100_milestone = MILESTONE_CHECK(100);
    b250_milestone = MILESTONE_CHECK(250);
    b1000_milestone = MILESTONE_CHECK(1000);
  } else {
    b1000_milestone = MILESTONE_CHECK(1000);
  }
#undef MILESTONE_CHECK

  last_seen_combo_ = iCombo;

  if (b25_milestone) {
    this->PlayCommand("TwentyFiveMilestone");
  }
  if (b50_milestone) {
    this->PlayCommand("FiftyMilestone");
  }
  if (b100_milestone) {
    this->PlayCommand("HundredMilestone");
  }
  if (b250_milestone) {
    this->PlayCommand("TwoHundredFiftyMilestone");
  }
  if (b1000_milestone) {
    this->PlayCommand("ThousandMilestone");
  }

  /* Colored combo logic differs between Songs and Courses.
   *	Songs:
   *	The theme decides how far into the song the combo color should appear.
   *	(PERCENT_UNTIL_COLOR_COMBO)
   *
   *	Courses:
   *	PERCENT_UNTIL_COLOR_COMBO refers to how long through the course the
   *	combo color should appear (scaling to the number of songs). This may
   *	not be desired behavior, however. -aj
   *
   *	TODO: Add a metric that determines Course combo colors logic?
   *	Or possibly move the logic to a Lua function? -aj */
  bool past_beginning = false;
  if (GAMESTATE->IsCourseMode()) {
    int song_index_start_coloring =
        GAMESTATE->cur_course_->GetEstimatedNumStages();
    song_index_start_coloring = static_cast<int>(
        std::floor(song_index_start_coloring * PERCENT_UNTIL_COLOR_COMBO));
    past_beginning =
        GAMESTATE->GetCourseSongIndex() >= song_index_start_coloring;
  } else {
    past_beginning =
        player_state_->position_.music_seconds_ >
        GAMESTATE->cur_song_->music_length_seconds_ * PERCENT_UNTIL_COLOR_COMBO;
  }

  if (send_judgment_and_combo_messages_) {
    Message msg("Combo");
    if (iCombo) {
      msg.SetParam("Combo", iCombo);
    }
    if (misses) {
      msg.SetParam("Misses", misses);
    }
    if (past_beginning && player_stage_stats_->FullComboOfScore(TNS_W1)) {
      msg.SetParam("FullComboW1", true);
    }
    if (past_beginning && player_stage_stats_->FullComboOfScore(TNS_W2)) {
      msg.SetParam("FullComboW2", true);
    }
    if (past_beginning && player_stage_stats_->FullComboOfScore(TNS_W3)) {
      msg.SetParam("FullComboW3", true);
    }
    if (past_beginning && player_stage_stats_->FullComboOfScore(TNS_W4)) {
      msg.SetParam("FullComboW4", true);
    }
    this->HandleMessage(msg);
  }
}

void Player::IncrementComboOrMissCombo(bool combo_or_miss_combo) {
  const unsigned int old_combo =
      player_stage_stats_ ? player_stage_stats_->cur_combo_ : 0;
  const unsigned int old_miss_combo =
      player_stage_stats_ ? player_stage_stats_->cur_miss_combo_ : 0;

  if (player_stage_stats_) {
    if (combo_or_miss_combo) {
      player_stage_stats_->cur_combo_++;
      player_stage_stats_->cur_miss_combo_ = 0;
    } else {
      player_stage_stats_->cur_combo_ = 0;
      player_stage_stats_->cur_miss_combo_++;
    }
    SetCombo(
        player_stage_stats_->cur_combo_, player_stage_stats_->cur_miss_combo_);
  }

  SendComboMessages(old_combo, old_miss_combo);
}

std::string Player::ApplyRandomAttack() {
  if (GAMESTATE->random_attacks_.size() < 1) {
    return "";
  }

  int attack_to_use = RandomInt(GAMESTATE->random_attacks_.size());

  return GAMESTATE->random_attacks_[attack_to_use];
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the Player. */
class LunaPlayer : public Luna<Player> {
 public:
  static int SetLife(T* p, lua_State* L) {
    if (p->inside_lua_set_life_) {
      luaL_error(
          L,
          "Do not call SetLife from inside LifeChangedMessageCommand because "
          "SetLife causes a LifeChangedMessageCommand.");
    }
    p->inside_lua_set_life_ = true;
    p->SetLife(FArg(1));
    p->inside_lua_set_life_ = false;
    COMMON_RETURN_SELF;
  }
  static int ChangeLife(T* p, lua_State* L) {
    if (p->inside_lua_set_life_) {
      luaL_error(
          L,
          "Do not call ChangeLife from inside LifeChangedMessageCommand "
          "because ChangeLife causes a LifeChangedMessageCommand.");
    }
    p->inside_lua_set_life_ = true;
    p->ChangeLife(FArg(1));
    p->inside_lua_set_life_ = false;
    COMMON_RETURN_SELF;
  }
  static int SetActorWithJudgmentPosition(T* p, lua_State* L) {
    Actor* actor = Luna<Actor>::check(L, 1);
    p->SetActorWithJudgmentPosition(actor);
    COMMON_RETURN_SELF;
  }
  static int SetActorWithComboPosition(T* p, lua_State* L) {
    Actor* actor = Luna<Actor>::check(L, 1);
    p->SetActorWithComboPosition(actor);
    COMMON_RETURN_SELF;
  }
  static int GetPlayerTimingData(T* p, lua_State* L) {
    p->GetPlayerTimingData().PushSelf(L);
    return 1;
  }
  GET_SET_BOOL_METHOD(oitg_zoom_mode, oitg_zoom_mode_);

  LunaPlayer() {
    ADD_METHOD(SetLife);
    ADD_METHOD(ChangeLife);
    ADD_METHOD(SetActorWithJudgmentPosition);
    ADD_METHOD(SetActorWithComboPosition);
    ADD_METHOD(GetPlayerTimingData);
    ADD_GET_SET_METHODS(oitg_zoom_mode);
  }
};

LUA_REGISTER_DERIVED_CLASS(Player, ActorFrame)
// lua end

/*
 * (c) 2001-2006 Chris Danford, Steve Checkoway
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
