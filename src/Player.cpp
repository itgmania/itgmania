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
    size_t /*TimingWindow*/ i, std::string& sNameOut, float& defaultValueOut);

/**
 * @brief Helper class to ensure that each row is only judged once without
 * taking too much memory.
 */
class JudgedRows {
  std::vector<bool> rows_;
  int start_;
  int offset_;

  void Resize(size_t iMin) {
    size_t iNewSize = std::max(2 * rows_.size(), iMin);
    std::vector<bool> vNewRows(rows_.begin() + offset_, rows_.end());
    vNewRows.reserve(iNewSize);
    vNewRows.insert(vNewRows.end(), rows_.begin(), rows_.begin() + offset_);
    vNewRows.resize(iNewSize, false);
    rows_.swap(vNewRows);
    offset_ = 0;
  }

 public:
  JudgedRows() : start_(0), offset_(0) { Resize(32); }
  // Returns true if the row has already been judged.
  bool JudgeRow(int iRow) {
    if (iRow < start_) {
      return true;
    }
    if (iRow >= start_ + int(rows_.size())) {
      Resize(iRow + 1 - start_);
    }
    const int iIndex = (iRow - start_ + offset_) % rows_.size();
    const bool ret = rows_[iIndex];
    rows_[iIndex] = true;
    while (rows_[offset_]) {
      rows_[offset_] = false;
      ++start_;
      if (++offset_ >= int(rows_.size())) {
        offset_ -= rows_.size();
      }
    }
    return ret;
  }
  void Reset(int iStart) {
    start_ = iStart;
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
static const float StepSearchDistance = 1.0f;

void TimingWindowSecondsInit(
    size_t /*TimingWindow*/ i, std::string& sNameOut, float& defaultValueOut) {
  sNameOut = "TimingWindowSeconds" +
             TimingWindowToString(static_cast<TimingWindow>(i));
  switch (i) {
    case TW_W1:
      defaultValueOut = 0.0225f;
      break;
    case TW_W2:
      defaultValueOut = 0.045f;
      break;
    case TW_W3:
      defaultValueOut = 0.090f;
      break;
    case TW_W4:
      defaultValueOut = 0.135f;
      break;
    case TW_W5:
      defaultValueOut = 0.180f;
      break;
    case TW_Mine:  // same as great
      defaultValueOut = 0.090f;
      break;
    case TW_Hold:  // allow enough time to take foot off and put back on
      defaultValueOut = 0.250f;
      break;
    case TW_Roll:
      defaultValueOut = 0.500f;
      break;
    case TW_Attack:
      defaultValueOut = 0.135f;
      break;
    case TW_Checkpoint:  // similar to TW_Hold, but a little more
                         // strict/accurate to Pump play.
      defaultValueOut = 0.1664f;
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

static Preference<float> m_fTimingWindowScale("TimingWindowScale", 1.0f);
static Preference<float> m_fTimingWindowAdd("TimingWindowAdd", 0);
static Preference1D<float> m_fTimingWindowSeconds(
    TimingWindowSecondsInit, NUM_TimingWindow);
static Preference<float> m_fTimingWindowJump("TimingWindowJump", 0.25);
static Preference<float> m_fMaxInputLatencySeconds(
    "MaxInputLatencySeconds", 0.0);
static Preference<bool> g_bEnableAttackSoundPlayback(
    "EnableAttackSounds", true);
static Preference<bool> g_bEnableMineSoundPlayback("EnableMineHitSound", true);
static Preference<TapNoteScore> g_MinTNSToScoreNotes(
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
  float fSecs = m_fTimingWindowSeconds[tw];
  fSecs *= m_fTimingWindowScale;
  fSecs += m_fTimingWindowAdd;
  return fSecs;
}

Player::Player(NoteData& nd, bool bVisibleParts) : note_data_(nd) {
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
  if (bVisibleParts) {
    attack_display_ = new AttackDisplay;
    this->AddChild(attack_display_);
  }

  PlayerAI::InitFromDisk();

  note_field_ = nullptr;
  if (bVisibleParts) {
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
    const std::string& sType, PlayerState* pPlayerState,
    PlayerStageStats* pPlayerStageStats, LifeMeter* pLM,
    CombinedLifeMeter* pCombinedLM, ScoreDisplay* pScoreDisplay,
    ScoreDisplay* pSecondaryScoreDisplay, ScoreKeeper* pPrimaryScoreKeeper,
    ScoreKeeper* pSecondaryScoreKeeper) {
  GRAY_ARROWS_Y_STANDARD.Load(sType, "ReceptorArrowsYStandard");
  GRAY_ARROWS_Y_REVERSE.Load(sType, "ReceptorArrowsYReverse");
  ATTACK_DISPLAY_X.Load(sType, ATTACK_DISPLAY_X_NAME, NUM_PLAYERS, 2);
  HOLD_JUDGMENT_Y_STANDARD.Load(sType, "HoldJudgmentYStandard");
  HOLD_JUDGMENT_Y_REVERSE.Load(sType, "HoldJudgmentYReverse");
  BRIGHT_GHOST_COMBO_THRESHOLD.Load(sType, "BrightGhostComboThreshold");
  TAP_JUDGMENTS_UNDER_FIELD.Load(sType, "TapJudgmentsUnderField");
  HOLD_JUDGMENTS_UNDER_FIELD.Load(sType, "HoldJudgmentsUnderField");
  COMBO_UNDER_FIELD.Load(sType, "ComboUnderField");
  DRAW_DISTANCE_AFTER_TARGET_PIXELS.Load(
      sType, "DrawDistanceAfterTargetsPixels");
  DRAW_DISTANCE_BEFORE_TARGET_PIXELS.Load(
      sType, "DrawDistanceBeforeTargetsPixels");

  {
    // Init judgment positions
    bool bPlayerUsingBothSides =
        GAMESTATE->GetCurrentStyle(pPlayerState->player_number_)
            ->GetUsesCenteredArrows();
    Actor TempJudgment;
    TempJudgment.SetName("Judgment");
    ActorUtil::LoadCommand(TempJudgment, sType, "Transform");

    Actor TempCombo;
    TempCombo.SetName("Combo");
    ActorUtil::LoadCommand(TempCombo, sType, "Transform");

    int iEnabledPlayerIndex = -1;
    int iNumEnabledPlayers = 0;
    FOREACH_EnabledPlayer(p) {
      if (p == pPlayerState->player_number_) {
        iEnabledPlayerIndex = iNumEnabledPlayers;
      }
      iNumEnabledPlayers++;
    }

    if (iNumEnabledPlayers ==
        0)  // hack for ScreenHowToPlay where no players are joined
    {
      iEnabledPlayerIndex = 0;
      iNumEnabledPlayers = 1;
    }

    for (int i = 0; i < NUM_REVERSE; i++) {
      for (int j = 0; j < NUM_CENTERED; j++) {
        Message msg("Transform");
        msg.SetParam("Player", pPlayerState->player_number_);
        msg.SetParam("iEnabledPlayerIndex", iEnabledPlayerIndex);
        msg.SetParam("iNumEnabledPlayers", iNumEnabledPlayers);
        msg.SetParam("bPlayerUsingBothSides", bPlayerUsingBothSides);
        msg.SetParam("bReverse", !!i);
        msg.SetParam("bCentered", !!j);

        TempJudgment.HandleMessage(msg);
        judgment_[i][j] = TempJudgment.DestTweenState();

        TempCombo.HandleMessage(msg);
        combo_[i][j] = TempCombo.DestTweenState();
      }
    }
  }

  this->SortByDrawOrder();

  player_state_ = pPlayerState;
  player_stage_stats_ = pPlayerStageStats;
  life_meter_ = pLM;
  combined_life_meter_ = pCombinedLM;
  score_display_ = pScoreDisplay;
  secondary_score_display_ = pSecondaryScoreDisplay;
  primary_score_keeper_ = pPrimaryScoreKeeper;
  secondary_score_keeper_ = pSecondaryScoreKeeper;

  last_seen_combo_ = 0;
  seen_combo_yet_ = false;

  // set initial life
  if (life_meter_ && player_stage_stats_) {
    float fLife = life_meter_->GetLife();
    player_stage_stats_->SetLifeRecordAt(
        fLife, STATSMAN->cur_stage_stats_.steps_seconds_);
  }

  // TODO: Remove use of PlayerNumber.
  PlayerNumber pn = player_state_->player_number_;

  RageSoundLoadParams SoundParams;
  SoundParams.support_pan_ = true;
  sound_mine_.Load(THEME->GetPathS(sType, "mine"), true, &SoundParams);

  /* Attacks can be launched in course modes and in battle modes.  They both
   * come here to play, but allow loading a different sound for different modes.
   */
  switch (GAMESTATE->play_mode_) {
    case PLAY_MODE_RAVE:
    case PLAY_MODE_BATTLE:
      sound_attack_launch_.Load(
          THEME->GetPathS(sType, "battle attack launch"), true, &SoundParams);
      sound_attack_ending_.Load(
          THEME->GetPathS(sType, "battle attack ending"), true, &SoundParams);
      break;
    default:
      sound_attack_launch_.Load(
          THEME->GetPathS(sType, "course attack launch"), true, &SoundParams);
      sound_attack_ending_.Load(
          THEME->GetPathS(sType, "course attack ending"), true, &SoundParams);
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

    float fMaxBPM = 0;

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
      fMaxBPM =
          (M_MOD_HIGH_CAP > 0 ? bpms.GetMaxWithin(M_MOD_HIGH_CAP)
                              : bpms.GetMax());
      fMaxBPM = std::max(0.0f, fMaxBPM);
    }

    // we can't rely on the displayed BPMs, so manually calculate.
    if (fMaxBPM == 0) {
      float fThrowAway = 0;

      if (GAMESTATE->IsCourseMode()) {
        for (const TrailEntry& e : GAMESTATE->cur_trail_[pn]->entries_) {
          float fMaxForEntry;
          if (M_MOD_HIGH_CAP > 0) {
            e.song_->song_timing_.GetActualBPM(
                fThrowAway, fMaxForEntry, M_MOD_HIGH_CAP);
          } else {
            e.song_->song_timing_.GetActualBPM(fThrowAway, fMaxForEntry);
          }
          fMaxBPM = std::max(fMaxForEntry, fMaxBPM);
        }
      } else {
        if (M_MOD_HIGH_CAP > 0) {
          GAMESTATE->cur_song_->song_timing_.GetActualBPM(
              fThrowAway, fMaxBPM, M_MOD_HIGH_CAP);
        } else {
          GAMESTATE->cur_song_->song_timing_.GetActualBPM(fThrowAway, fMaxBPM);
        }
      }
    }

    ASSERT(fMaxBPM > 0);
    player_state_->read_bpm_ = fMaxBPM;
  }

  float fBalance = GameSoundManager::GetPlayerBalance(pn);
  sound_mine_.SetProperty("Pan", fBalance);
  sound_attack_launch_.SetProperty("Pan", fBalance);
  sound_attack_ending_.SetProperty("Pan", fBalance);

  if (HasVisibleParts()) {
    LuaThreadVariable var(
        "Player", LuaReference::Create(player_state_->player_number_));

    spr_combo_.Load(THEME->GetPathG(sType, "combo"));
    spr_combo_->SetName("Combo");
    actor_with_combo_position_ = &*spr_combo_;
    this->AddChild(spr_combo_);

    // todo: allow for judgments to be loaded per-column a la pop'n?
    // see how HoldJudgments are handled below for an example, though
    // it would need more work. -aj
    spr_judgment_.Load(THEME->GetPathG(sType, "judgment"));
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
      HoldJudgment* pJudgment = new HoldJudgment;
      // xxx: assumes sprite; todo: don't force 1x2 -aj
      pJudgment->Load(THEME->GetPathG("HoldJudgment", "label 1x2"));
      hold_judgment_[i] = pJudgment;
      this->AddChild(hold_judgment_[i]);
    }
  }

  note_field_height_ = GRAY_ARROWS_Y_REVERSE - GRAY_ARROWS_Y_STANDARD;
  if (note_field_) {
    note_field_->Init(player_state_, note_field_height_);
    ActorUtil::LoadAllCommands(*note_field_, sType);
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
    PlayerState* pPlayerState, const NoteData& notes) {
  pPlayerState->cache_displayed_beat_.clear();

  const std::vector<TimingSegment*> vScrolls =
      pPlayerState->GetDisplayedTiming().GetTimingSegments(SEGMENT_SCROLL);

  float displayedBeat = 0.0f;
  float lastRealBeat = 0.0f;
  float lastRatio = 1.0f;
  for (unsigned i = 0; i < vScrolls.size(); i++) {
    ScrollSegment* seg = ToScroll(vScrolls[i]);
    displayedBeat += (seg->GetBeat() - lastRealBeat) * lastRatio;
    lastRealBeat = seg->GetBeat();
    lastRatio = seg->GetRatio();
    CacheDisplayedBeat c = {seg->GetBeat(), displayedBeat, seg->GetRatio()};
    pPlayerState->cache_displayed_beat_.push_back(c);
  }

  pPlayerState->cache_note_stat_.clear();

  NoteData::all_tracks_const_iterator it =
      notes.GetTapNoteRangeAllTracks(0, MAX_NOTE_ROW, true);
  int count = 0, lastCount = 0;
  for (; !it.IsAtEnd(); ++it) {
    for (int t = 0; t < notes.GetNumTracks(); t++) {
      if (notes.GetTapNote(t, it.Row()) != TAP_EMPTY) {
        count++;
      }
    }
    CacheNoteStat c = {NoteRowToBeat(it.Row()), lastCount, count};
    lastCount = count;
    pPlayerState->cache_note_stat_.push_back(c);
  }
}

void Player::Load() {
  loaded_ = true;

  // Figured this is probably a little expensive so let's cache it
  tick_holds_ = TICK_HOLDS;

  // The editor can start playing in the middle of the song.
  const int iNoteRow =
      BeatToNoteRowNotRounded(player_state_->position_.song_beat_);
  first_uncrossed_row_ = iNoteRow - 1;
  judged_rows_->Reset(iNoteRow);

  // TODO: Remove use of PlayerNumber.
  PlayerNumber pn = player_state_->player_number_;

  bool bOniDead =
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
    std::vector<NoteData> vParts;

    NoteDataUtil::SplitCompositeNoteData(note_data_, vParts);
    note_data_ = vParts[pn];
  }

  /* Apply transforms. */
  NoteDataUtil::TransformNoteData(
      note_data_, *timing_, player_state_->player_options_.GetStage(),
      GAMESTATE->GetCurrentStyle(GetPlayerState()->player_number_)
          ->steps_type_);

  const Song* pSong = GAMESTATE->cur_song_;

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

  int iDrawDistanceAfterTargetsPixels =
      GAMESTATE->IsEditing() ? -100 : DRAW_DISTANCE_AFTER_TARGET_PIXELS;
  int iDrawDistanceBeforeTargetsPixels =
      GAMESTATE->IsEditing() ? 400 : DRAW_DISTANCE_BEFORE_TARGET_PIXELS;

  float fNoteFieldMiddle = (GRAY_ARROWS_Y_STANDARD + GRAY_ARROWS_Y_REVERSE) / 2;

  if (note_field_ && !bOniDead) {
    note_field_->SetY(fNoteFieldMiddle);
    note_field_->Load(
        &note_data_, iDrawDistanceAfterTargetsPixels,
        iDrawDistanceBeforeTargetsPixels);
  }

  bool bPlayerUsingBothSides =
      GAMESTATE->GetCurrentStyle(GetPlayerState()->player_number_)
          ->GetUsesCenteredArrows();
  if (attack_display_) {
    attack_display_->SetX(
        ATTACK_DISPLAY_X.GetValue(pn, bPlayerUsingBothSides) - 40);
  }

  // Load keysounds.  If sounds are already loaded (as in the editor), don't
  // reload them.
  // XXX: the editor will load several duplicate copies (in each NoteField), and
  // each player will load duplicate sounds.  Does this belong somewhere else
  // (perhaps in a separate object, used alongside ScreenGameplay::m_pSoundMusic
  // and ScreenEdit::m_pSoundMusic?) We don't have to load separate copies to
  // set player fade: always make a copy, and set the fade on the copy.
  std::string sSongDir = pSong->GetSongDir();
  keysounds_.resize(pSong->keysound_file_.size());

  // parameters are invalid somehow... -aj
  RageSoundLoadParams SoundParams;
  SoundParams.support_pan_ = true;

  float fBalance = GameSoundManager::GetPlayerBalance(pn);
  for (unsigned i = 0; i < keysounds_.size(); i++) {
    std::string sKeysoundFilePath = sSongDir + pSong->keysound_file_[i];
    RageSound& sound = keysounds_[i];
    if (sound.GetLoadedFilePath() != sKeysoundFilePath) {
      sound.Load(sKeysoundFilePath, true, &SoundParams);
    }
    sound.SetProperty("Pan", fBalance);
    sound.SetStopModeFromString("stop");
  }

  if (player_stage_stats_) {
    SendComboMessages(
        player_stage_stats_->cur_combo_, player_stage_stats_->cur_miss_combo_);
  }

  RageUtil::SafeDelete(iter_needs_tap_judging_);
  iter_needs_tap_judging_ = new NoteData::all_tracks_iterator(
      note_data_.GetTapNoteRangeAllTracks(iNoteRow, MAX_NOTE_ROW));

  RageUtil::SafeDelete(iter_needs_hold_judging_);
  iter_needs_hold_judging_ = new NoteData::all_tracks_iterator(
      note_data_.GetTapNoteRangeAllTracks(iNoteRow, MAX_NOTE_ROW));

  RageUtil::SafeDelete(iter_uncrossed_rows_);
  iter_uncrossed_rows_ = new NoteData::all_tracks_iterator(
      note_data_.GetTapNoteRangeAllTracks(iNoteRow, MAX_NOTE_ROW));

  RageUtil::SafeDelete(iter_unjudged_rows_);
  iter_unjudged_rows_ = new NoteData::all_tracks_iterator(
      note_data_.GetTapNoteRangeAllTracks(iNoteRow, MAX_NOTE_ROW));

  RageUtil::SafeDelete(iter_unjudged_mine_rows_);
  iter_unjudged_mine_rows_ = new NoteData::all_tracks_iterator(
      note_data_.GetTapNoteRangeAllTracks(iNoteRow, MAX_NOTE_ROW));
}

void Player::SendComboMessages(
    unsigned int iOldCombo, unsigned int iOldMissCombo) {
  const unsigned int iCurCombo =
      player_stage_stats_ ? player_stage_stats_->cur_combo_ : 0;
  if (iOldCombo > (unsigned int)COMBO_STOPPED_AT &&
      iCurCombo < (unsigned int)COMBO_STOPPED_AT) {
    SCREENMAN->PostMessageToTopScreen(SM_ComboStopped, 0);
  }

  if (send_judgment_and_combo_messages_) {
    Message msg("ComboChanged");
    msg.SetParam("Player", player_state_->player_number_);
    msg.SetParam("OldCombo", iOldCombo);
    msg.SetParam("OldMissCombo", iOldMissCombo);
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

void Player::Update(float fDeltaTime) {
  const RageTimer now;
  // Don't update if we haven't been loaded yet.
  if (!loaded_) {
    return;
  }

  // LOG->Trace( "Player::Update(%f)", fDeltaTime );

  if (GAMESTATE->cur_song_ == nullptr || IsOniDead()) {
    return;
  }

  ActorFrame::Update(fDeltaTime);

  const float fSongBeat = player_state_->position_.song_beat_;
  const int iSongRow = BeatToNoteRow(fSongBeat);

  ArrowEffects::SetCurrentOptions(&player_state_->player_options_.GetCurrent());

  // Optimization: Don't spend time processing the things below that won't show
  // if the Player doesn't show anything on the screen.
  if (HasVisibleParts()) {
    // Random Attack Mod
    if (player_state_->player_options_.GetCurrent().rand_attack_) {
      float fCurrentGameTime = STATSMAN->cur_stage_stats_.gameplay_seconds_;

      const float fAttackRunTime = ATTACK_RUN_TIME_RANDOM;

      // Don't start until 1 seconds into game, minimum
      if (fCurrentGameTime > 1.0f) {
        /* Update the attack if there are no others currently running.
         * Note that we have a new one activate a little early; This is
         * to have a bit of overlap rather than an abrupt change. */
        if ((fCurrentGameTime - active_random_attack_start_) >
            (fAttackRunTime - 0.5f)) {
          active_random_attack_start_ = fCurrentGameTime;

          Attack attRandomAttack;
          attRandomAttack.modifiers_ = ApplyRandomAttack();
          attRandomAttack.secs_remaining_ = fAttackRunTime;
          player_state_->LaunchAttack(attRandomAttack);
        }
      }
    }

    if (g_bEnableAttackSoundPlayback) {
      if (player_state_->attack_began_this_update_) {
        sound_attack_launch_.Play(false);
      }
      if (player_state_->attack_ended_this_update_) {
        sound_attack_ending_.Play(false);
      }
    }

    float fMiniPercent = player_state_->player_options_.GetCurrent()
                             .effects_[PlayerOptions::EFFECT_MINI];
    float fTinyPercent = player_state_->player_options_.GetCurrent()
                             .effects_[PlayerOptions::EFFECT_TINY];
    float fJudgmentZoom =
        std::min(std::pow(0.5f, fMiniPercent + fTinyPercent), 1.0f);

    // Update Y positions
    {
      for (int c = 0;
           c < GAMESTATE->GetCurrentStyle(GetPlayerState()->player_number_)
                   ->cols_per_player_;
           c++) {
        float fPercentReverse = player_state_->player_options_.GetCurrent()
                                    .GetReversePercentForColumn(c);
        float fHoldJudgeYPos = SCALE(
            fPercentReverse, 0.f, 1.f, HOLD_JUDGMENT_Y_STANDARD,
            HOLD_JUDGMENT_Y_REVERSE);
        // float fGrayYPos = SCALE( fPercentReverse, 0.f, 1.f,
        // GRAY_ARROWS_Y_STANDARD, GRAY_ARROWS_Y_REVERSE );

        float fX = ArrowEffects::GetXPos(player_state_, c, 0);
        const float fZ = ArrowEffects::GetZPos(player_state_, c, 0);
        fX *= (1 - fMiniPercent * 0.5f);

        hold_judgment_[c]->SetX(fX);
        hold_judgment_[c]->SetY(fHoldJudgeYPos);
        hold_judgment_[c]->SetZ(fZ);
        hold_judgment_[c]->SetZoom(fJudgmentZoom);
      }
    }

    // NoteField accounts for reverse on its own now.
    // if( m_pNoteField )
    //	m_pNoteField->SetY( fGrayYPos );

    const bool bReverse =
        player_state_->player_options_.GetCurrent().GetReversePercentForColumn(
            0) == 1;
    float fPercentCentered = player_state_->player_options_.GetCurrent()
                                 .scrolls_[PlayerOptions::SCROLL_CENTERED];

    if (actor_with_judgment_position_ != nullptr) {
      const Actor::TweenState& ts1 = judgment_[bReverse ? 1 : 0][0];
      const Actor::TweenState& ts2 = judgment_[bReverse ? 1 : 0][1];
      Actor::TweenState::MakeWeightedAverage(
          actor_with_judgment_position_->DestTweenState(), ts1, ts2,
          fPercentCentered);
    }

    if (actor_with_combo_position_ != nullptr) {
      const Actor::TweenState& ts1 = combo_[bReverse ? 1 : 0][0];
      const Actor::TweenState& ts2 = combo_[bReverse ? 1 : 0][1];
      Actor::TweenState::MakeWeightedAverage(
          actor_with_combo_position_->DestTweenState(), ts1, ts2,
          fPercentCentered);
    }

    float field_zoom = 1 - fMiniPercent * 0.5f;
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
          actor_with_judgment_position_->GetZoom() * fJudgmentZoom);
    }
    if (actor_with_combo_position_ != nullptr) {
      actor_with_combo_position_->SetZoom(
          actor_with_combo_position_->GetZoom() * fJudgmentZoom);
    }
  }

  // If we're paused, don't update tap or hold note logic, so hold notes can be
  // released during pause.
  if (paused_) {
    return;
  }

  // update pressed flag
  const int iNumCols =
      GAMESTATE->GetCurrentStyle(GetPlayerState()->player_number_)
          ->cols_per_player_;
  ASSERT_M(
      iNumCols <= MAX_COLS_PER_PLAYER,
      ssprintf("%i > %i", iNumCols, MAX_COLS_PER_PLAYER));
  for (int col = 0; col < iNumCols; ++col) {
    ASSERT(player_state_ != nullptr);

    // TODO: Remove use of PlayerNumber.
    std::vector<GameInput> GameI;
    GAMESTATE->GetCurrentStyle(GetPlayerState()->player_number_)
        ->StyleInputToGameInput(col, player_state_->player_number_, GameI);

    bool bIsHoldingButton = INPUTMAPPER->IsBeingPressed(GameI);

    // TODO: Make this work for non-human-controlled players
    if (bIsHoldingButton && !GAMESTATE->demonstration_or_jukebox_ &&
        player_state_->player_controller_ == PC_HUMAN) {
      if (note_field_) {
        note_field_->SetPressed(col);
      }
    }
  }

  // handle Autoplay for rolls
  if (player_state_->player_controller_ != PC_HUMAN) {
    for (int iTrack = 0; iTrack < note_data_.GetNumTracks(); ++iTrack) {
      // TODO: Make the CPU miss sometimes.
      int iHeadRow;
      if (!note_data_.IsHoldNoteAtRow(iTrack, iSongRow, &iHeadRow)) {
        iHeadRow = iSongRow;
      }

      const TapNote& tn = note_data_.GetTapNote(iTrack, iHeadRow);
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

      Step(iTrack, iHeadRow, now, false, false);
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
    const float musicPosition = player_state_->position_.music_seconds_;
    const float rate = GAMESTATE->song_options_.GetCurrent().music_rate_;

    // We have to add 1 here, because GetBeatFromElapsedTime() can round down.
    const int lastCheckRow =
        BeatToNoteRow(timing_->GetBeatFromElapsedTime(musicPosition) + 1);

    for (auto iter = *iter_needs_tap_judging_;
         !iter.IsAtEnd() && iter.Row() <= lastCheckRow; ++iter) {
      TapNote& tn = *iter;
      const int row = iter.Row();
      const int track = iter.Track();

      const float notePosition =
          timing_->GetElapsedTimeFromBeat(NoteRowToBeat(row));

      // Check if we're looking at notes in the future we don't care about.
      if (notePosition > musicPosition) {
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
      const float secsSinceNote = (musicPosition - notePosition) / rate;
      float maxSecsHeld = 0.0f;
      for (const GameInput& gi : inputs) {
        maxSecsHeld = std::max(maxSecsHeld, INPUTMAPPER->GetSecsHeld(gi));
      }
      tn.result_.held_ = maxSecsHeld >= secsSinceNote;
    }
  }

  // update HoldNotes logic
  {
    // Fast forward to the first that needs hold judging.
    {
      NoteData::all_tracks_iterator& iter = *iter_needs_hold_judging_;
      while (!iter.IsAtEnd() && iter.Row() <= iSongRow &&
             !NeedsHoldJudging(*iter)) {
        ++iter;
      }
    }

    std::vector<TrackRowTapNote> vHoldNotesToGradeTogether;
    int iRowOfLastHoldNote = -1;
    NoteData::all_tracks_iterator iter = *iter_needs_hold_judging_;  // copy
    for (; !iter.IsAtEnd() && iter.Row() <= iSongRow; ++iter) {
      TapNote& tn = *iter;
      if (tn.type_ != TapNoteType_HoldHead) {
        continue;
      }

      int iTrack = iter.Track();
      int iRow = iter.Row();
      TrackRowTapNote trtn = {iTrack, iRow, &tn};

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
          UpdateHoldNotes(iSongRow, fDeltaTime, v);
        }
          continue;  // don't process this below
      }
      /*
      case TapNoteSubType_Mine:
              break;
      */

      if (iRow != iRowOfLastHoldNote ||
          !JUDGE_HOLD_NOTES_ON_SAME_ROW_TOGETHER) {
        if (!vHoldNotesToGradeTogether.empty()) {
          // LOG->Trace( ssprintf("UpdateHoldNotes; %i != %i || !judge holds on
          // same row together",iRow,iRowOfLastHoldNote) );
          UpdateHoldNotes(iSongRow, fDeltaTime, vHoldNotesToGradeTogether);
          vHoldNotesToGradeTogether.clear();
        }
      }
      iRowOfLastHoldNote = iRow;
      vHoldNotesToGradeTogether.push_back(trtn);
    }

    if (!vHoldNotesToGradeTogether.empty()) {
      // LOG->Trace("UpdateHoldNotes since !vHoldNotesToGradeTogether.empty()");
      UpdateHoldNotes(iSongRow, fDeltaTime, vHoldNotesToGradeTogether);
      vHoldNotesToGradeTogether.clear();
    }
  }

  {
    // Why was this originally "BeatToNoteRowNotRounded"? It should be rounded.
    // -Chris
    /* We want to send the crossed row message exactly when we cross the
     * row--not .5 before the row. Use a very slow song (around 2 BPM) as a test
     * case: without rounding, autoplay steps early. -glenn */
    const float fPositionSeconds =
        player_state_->position_.music_seconds_ - PREFSMAN->pad_stick_seconds_;
    const float fDisplayedSongBeat =
        player_state_->GetDisplayedTiming().GetBeatFromElapsedTime(
            fPositionSeconds);
    const int iRowNow = BeatToNoteRowNotRounded(fDisplayedSongBeat);

    if (iRowNow >= 0) {
      if (GAMESTATE->IsPlayerEnabled(player_state_)) {
        if (player_state_->position_.delay_) {
          if (!delay_) {
            delay_ = true;
          }
        } else {
          if (delay_) {
            if (player_state_->player_controller_ != PC_HUMAN) {
              CrossedRows(iRowNow - 1, now);
            }
            delay_ = false;
          }
          CrossedRows(iRowNow, now);
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
    int iSongRow, float fDeltaTime, std::vector<TrackRowTapNote>& vTN) {
  ASSERT(!vTN.empty());

  // LOG->Trace("--------------------------------");
  /*
  LOG->Trace("[Player::UpdateHoldNotes] begins");
  LOG->Trace( ssprintf("song row %i, deltaTime = %f",iSongRow,fDeltaTime) );
  */

  int iStartRow = vTN[0].row_;
  int iMaxEndRow = INT_MIN;
  int iFirstTrackWithMaxEndRow = -1;

  TapNoteSubType subType = TapNoteSubType_Invalid;
  for (const TrackRowTapNote& trtn : vTN) {
    int iTrack = trtn.track_;
    ASSERT(iStartRow == trtn.row_);
    TapNote& tn = *trtn.tn_;
    int iEndRow = iStartRow + tn.duration_;
    if (subType == TapNoteSubType_Invalid) {
      subType = tn.sub_type_;
    }

    /* All holds must be of the same subType because fLife is handled
     * in different ways depending on the SubType. */
    ASSERT(tn.sub_type_ == subType);

    // Routine charts assign each note to a specific player; skip notes
    // that don't belong to this Player
    if (tn.pn_ != PLAYER_INVALID && tn.pn_ != player_state_->player_number_) {
      continue;
    }

    if (iEndRow > iMaxEndRow) {
      iMaxEndRow = iEndRow;
      iFirstTrackWithMaxEndRow = iTrack;
    }
  }

  ASSERT(iFirstTrackWithMaxEndRow != -1);
  // LOG->Trace( ssprintf("start row: %i; max/end row: =
  // %i",iStartRow,iMaxEndRow) ); LOG->Trace( ssprintf("first track with max end
  // row = %i",iFirstTrackWithMaxEndRow) ); LOG->Trace( ssprintf("max end row -
  // start row (in beats) =
  // %f",NoteRowToBeat(iMaxEndRow)-NoteRowToBeat(iStartRow)) );

  for (const TrackRowTapNote& trtn : vTN) {
    TapNote& tn = *trtn.tn_;
    // Routine charts assign each note to a specific player; skip notes
    // that don't belong to this Player
    if (tn.pn_ != PLAYER_INVALID && tn.pn_ != player_state_->player_number_) {
      continue;
    }
    // set hold flags so NoteField can do intelligent drawing
    tn.hold_result_.held_ = false;
    tn.hold_result_.active_ = false;

    int iRow = trtn.row_;
    // LOG->Trace( ssprintf("this row: %i",iRow) );

    // If the song beat is in the range of this hold:
    if (iRow <= iSongRow && iRow <= iMaxEndRow) {
      // LOG->Trace( ssprintf("overlap time before:
      // %f",tn.HoldResult.fOverlappedTime) );
      tn.hold_result_.overlapped_time_ += fDeltaTime;
      // LOG->Trace( ssprintf("overlap time after:
      // %f",tn.HoldResult.fOverlappedTime) );
    } else {
      // LOG->Trace( "overlap time = 0" );
      tn.hold_result_.overlapped_time_ = 0;
    }
  }

  HoldNoteScore hns = vTN[0].tn_->hold_result_.hns_;
  float fLife = vTN[0].tn_->hold_result_.life_;

  if (hns != HNS_None)  // if this HoldNote already has a result
  {
    // LOG->Trace("hold note has a result, skipping.");
    return;  // we don't need to update the logic for this group
  }

  // LOG->Trace("hold note doesn't already have result, let's check.");

  // LOG->Trace( ssprintf("[C++] hold note score:
  // %s",HoldNoteScoreToString(hns).c_str()) );
  // LOG->Trace(ssprintf("[Player::UpdateHoldNotes] fLife = %f",fLife));

  bool bSteppedOnHead = true;
  bool bHeadJudged = true;
  for (const TrackRowTapNote& trtn : vTN) {
    TapNote& tn = *trtn.tn_;
    TapNoteScore tns = tn.result_.tns_;
    // Routine charts assign each note to a specific player; skip notes
    // that don't belong to this Player
    if (tn.pn_ != PLAYER_INVALID && tn.pn_ != player_state_->player_number_) {
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
    bSteppedOnHead &=
        ((tns != TNS_Miss && tns != TNS_None) ||
         tn.result_.early_tns_ !=
             TNS_None);  // did they step on the start of this hold?
    bHeadJudged &=
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

  bool bInitiatedNote;
  if (REQUIRE_STEP_ON_HOLD_HEADS) {
    // XXX HACK: Miniholds (a 64th or 192nd length hold) will not always
    // register as Held, even if you hit the note. This is considered a
    // major roadblock to adoption, so until a proper fix is found,
    // DON'T REMOVE THIS HACK! -aj
    /*if( iMaxEndRow-iStartRow <= 4 )
            bInitiatedNote = true;
    else*/
    bInitiatedNote = bSteppedOnHead;
  } else {
    bInitiatedNote = true;
    bHeadJudged = true;
  }

  bool bIsHoldingButton = true;
  for (const TrackRowTapNote& trtn : vTN) {
    /*if this hold is already done, pretend it's always being pressed.
    fixes/masks the phantom hold issue. -FSX*/
    // That interacts badly with !IMMEDIATE_HOLD_LET_GO,
    // causing ALL holds to be judged HNS_Held whether they were or not.
    if (!IMMEDIATE_HOLD_LET_GO ||
        (iStartRow + trtn.tn_->duration_) > iSongRow) {
      int iTrack = trtn.track_;

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
        std::vector<GameInput> GameI;
        GAMESTATE->GetCurrentStyle(GetPlayerState()->player_number_)
            ->StyleInputToGameInput(iTrack, pn, GameI);

        bIsHoldingButton &= INPUTMAPPER->IsBeingPressed(GameI);
      }
    }
  }

  if (bInitiatedNote && fLife != 0 && bHeadJudged) {
    // LOG->Trace("[Player::UpdateHoldNotes] initiated note, fLife != 0");
    /* This hold note is not judged and we stepped on its head.
     * Update iLastHeldRow. Do this even if we're a little beyond the end
     * of the hold note, to make sure iLastHeldRow is clamped to iEndRow
     * if the hold note is held all the way. */
    for (const TrackRowTapNote& trtn : vTN) {
      TapNote& tn = *trtn.tn_;
      int iEndRow = iStartRow + tn.duration_;
      // Routine charts assign each note to a specific player; skip notes
      // that don't belong to this Player
      if (tn.pn_ != PLAYER_INVALID && tn.pn_ != player_state_->player_number_) {
        continue;
      }
      // LOG->Trace(ssprintf("trying for min between iSongRow (%i) and iEndRow
      // (%i) (duration %i)",iSongRow,iEndRow,tn.iDuration));
      tn.hold_result_.last_held_row_ = std::min(iSongRow, iEndRow);
    }
  }

  // If the song beat is in the range of this hold:
  if (iStartRow <= iSongRow && iStartRow <= iMaxEndRow && bHeadJudged) {
    switch (subType) {
      case TapNoteSubType_Hold:
        for (const TrackRowTapNote& trtn : vTN) {
          TapNote& tn = *trtn.tn_;
          if (tn.pn_ != PLAYER_INVALID &&
              tn.pn_ != player_state_->player_number_) {
            continue;
          }
          // set hold flag so NoteField can do intelligent drawing
          tn.hold_result_.held_ = bIsHoldingButton && bInitiatedNote;
          tn.hold_result_.active_ = bInitiatedNote;
        }

        if (bInitiatedNote && bIsHoldingButton) {
          // LOG->Trace("bInitiatedNote && bIsHoldingButton; Increasing hold
          // life to MAX_HOLD_LIFE");
          //  Increase life
          fLife = MAX_HOLD_LIFE;  // was 1 -aj
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
          fLife -= fDeltaTime / GetWindowSeconds(window);
          // LOG->Trace("fLife before clamp: %f",fLife);
          fLife = std::max(0.0f, fLife);
          // LOG->Trace("fLife after: %f",fLife);
        }
        break;
      case TapNoteSubType_Roll:
        for (const TrackRowTapNote& trtn : vTN) {
          TapNote& tn = *trtn.tn_;
          if (tn.pn_ != PLAYER_INVALID &&
              tn.pn_ != player_state_->player_number_) {
            continue;
          }
          tn.hold_result_.held_ = true;
          tn.hold_result_.active_ = bInitiatedNote;
        }

        // give positive life in Step(), not here.

        // Decrease life
        fLife -= fDeltaTime / GetWindowSeconds(TW_Roll);
        fLife = std::max(fLife, 0.0f);  // clamp
        break;
      /*
      case TapNoteSubType_Mine:
              break;
      */
      default:
        FAIL_M(ssprintf("Invalid tap note subtype: %i", subType));
    }
  }

  // TODO: Cap the active time passed to the score keeper to the actual start
  // time and end time of the hold.
  if (vTN[0].tn_->hold_result_.active_) {
    float fSecondsActiveSinceLastUpdate =
        fDeltaTime * GAMESTATE->song_options_.GetCurrent().music_rate_;
    if (primary_score_keeper_) {
      primary_score_keeper_->HandleHoldActiveSeconds(
          fSecondsActiveSinceLastUpdate);
    }
    if (secondary_score_keeper_) {
      secondary_score_keeper_->HandleHoldActiveSeconds(
          fSecondsActiveSinceLastUpdate);
    }
  }

  // check for LetGo. If the head was missed completely, don't count an LetGo.
  /* Why? If you never step on the head, then it will be left as HNS_None,
   * which doesn't seem correct. */
  if (IMMEDIATE_HOLD_LET_GO) {
    if (bInitiatedNote && fLife == 0 &&
        bHeadJudged)  // the player has not pressed the button for a long time!
    {
      // LOG->Trace("LetGo from life == 0 (did initiate hold)");
      hns = HNS_LetGo;
    }
  }

  // score hold notes that have passed
  if (iSongRow >= iMaxEndRow && bHeadJudged) {
    bool bLetGoOfHoldNote = false;

    /* Score rolls that end with fLife == 0 as LetGo, even if
     * m_bTickHolds is on. Rolls don't have iCheckpointsMissed set, so,
     * unless we check Life == 0, rolls would always be scored as Held. */
    bool bAllowHoldCheckpoints;
    switch (subType) {
      DEFAULT_FAIL(subType);
      case TapNoteSubType_Hold:
        bAllowHoldCheckpoints = true;
        break;
      case TapNoteSubType_Roll:
        bAllowHoldCheckpoints = false;
        break;
        /*
        case TapNoteSubType_Mine:
                bAllowHoldCheckpoints = true;
                break;
        */
    }

    if (tick_holds_ && bAllowHoldCheckpoints) {
      // LOG->Trace("(hold checkpoints are allowed and enabled.)");
      int iCheckpointsHit = 0;
      int iCheckpointsMissed = 0;
      for (const TrackRowTapNote& v : vTN) {
        if (v.tn_->pn_ != PLAYER_INVALID &&
            v.tn_->pn_ != player_state_->player_number_) {
          continue;
        }
        iCheckpointsHit += v.tn_->hold_result_.checkpoints_hit_;
        iCheckpointsMissed += v.tn_->hold_result_.checkpoints_missed_;
      }
      bLetGoOfHoldNote = iCheckpointsMissed > 0 || iCheckpointsHit == 0;

      // TRICKY: If the hold is so short that it has no checkpoints,
      // then mark it as Held if the head was stepped on.
      if (iCheckpointsHit == 0 && iCheckpointsMissed == 0) {
        bLetGoOfHoldNote = !bSteppedOnHead;
      }

      /*
      if(bLetGoOfHoldNote)
              LOG->Trace("let go of hold note, life is 0");
      else
              LOG->Trace("did not let go of hold note :D");
      */
    } else {
      // LOG->Trace("(hold checkpoints disabled.)");
      bLetGoOfHoldNote = fLife == 0;
      /*
      if(bLetGoOfHoldNote)
              LOG->Trace("let go of hold note, life is 0");
      else
              LOG->Trace("did not let go of hold note :D");
      */
    }

    if (bInitiatedNote) {
      if (!bLetGoOfHoldNote) {
        // LOG->Trace("initiated note and didn't let go");
        fLife = 1;  // xxx: should be MAX_HOLD_LIFE instead? -aj
        hns = HNS_Held;
        bool bBright = player_stage_stats_ &&
                       player_stage_stats_->cur_combo_ >
                           (unsigned int)BRIGHT_GHOST_COMBO_THRESHOLD;
        if (note_field_) {
          for (const TrackRowTapNote& trtn : vTN) {
            if (trtn.tn_->pn_ != PLAYER_INVALID &&
                trtn.tn_->pn_ != player_state_->player_number_) {
              continue;
            }
            int iTrack = trtn.track_;
            note_field_->DidHoldNote(
                iTrack, HNS_Held, bBright);  // bright ghost flash
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

  float fLifeFraction = fLife / MAX_HOLD_LIFE;

  for (const TrackRowTapNote& trtn : vTN) {
    TapNote& tn = *trtn.tn_;
    // Routine charts assign each note to a specific player; skip notes
    // that don't belong to this Player
    if (tn.pn_ != PLAYER_INVALID && tn.pn_ != player_state_->player_number_) {
      continue;
    }
    tn.hold_result_.life_ = fLife;
    tn.hold_result_.hns_ = hns;
    // Stop the playing keysound for the hold note.
    // I think this causes crashes too. -aj
    // This can still crash. I think it expects a full game and quit before the
    // preference works: otherwise, it causes problems on holds. At least, that
    // hapened on my Mac. -wolfman2000

    Preference<float>* pVolume =
        Preference<float>::GetPreferenceByName("SoundVolume");
    if (pVolume != nullptr) {
      float fVol = pVolume->Get();

      if (tn.keysound_index_ >= 0 &&
          tn.keysound_index_ < (int)keysounds_.size()) {
        float factor =
            (tn.sub_type_ == TapNoteSubType_Roll
                 ? 2.0f * fLifeFraction
                 : 10.0f * fLifeFraction - 8.5f);
        keysounds_[tn.keysound_index_].SetProperty(
            "Volume", std::max(0.0f, std::min(1.0f, factor)) * fVol);
      }
    }
  }

  if ((hns == HNS_LetGo) && COMBO_BREAK_ON_IMMEDIATE_HOLD_LET_GO) {
    IncrementMissCombo();
  }

  if (hns != HNS_None) {
    // LOG->Trace("tap note scoring time.");
    TapNote& tn = *vTN[0].tn_;
    // Routine charts assign each note to a specific player; skip notes
    // that don't belong to this Player
    if (tn.pn_ != PLAYER_INVALID && tn.pn_ != player_state_->player_number_) {
      return;
    }
    SetHoldJudgment(tn, iFirstTrackWithMaxEndRow);
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

    float fStartBeat, fEndBeat;
    mod.GetRealtimeAttackBeats(
        GAMESTATE->cur_song_, player_state_, fStartBeat, fEndBeat);
    fEndBeat = std::min(fEndBeat, note_data_.GetLastBeat());

    LOG->Trace(
        "Applying transform '%s' from %f to %f to '%s'", mod.modifiers_.c_str(),
        fStartBeat, fEndBeat,
        GAMESTATE->cur_song_->GetTranslitMainTitle().c_str());

    // if re-adding noteskin changes, this is one place to edit -aj

    NoteDataUtil::TransformNoteData(
        note_data_, *timing_, po,
        GAMESTATE->GetCurrentStyle(GetPlayerState()->player_number_)
            ->steps_type_,
        BeatToNoteRow(fStartBeat), BeatToNoteRow(fEndBeat));
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
  float fLife = -1;
  if (life_meter_) {
    fLife = life_meter_->GetLife();
  } else if (combined_life_meter_) {
    fLife = GAMESTATE->tug_life_percent_p1_;
    if (pn == PLAYER_2) {
      fLife = 1.0f - fLife;
    }
  }
  if (fLife != -1) {
    if (player_stage_stats_) {
      player_stage_stats_->SetLifeRecordAt(
          fLife, STATSMAN->cur_stage_stats_.steps_seconds_);
    }
  }
}

int Player::GetClosestNoteDirectional(
    int col, int iStartRow, int iEndRow, bool bAllowGraded,
    bool bForward) const {
  NoteData::const_iterator begin, end;
  note_data_.GetTapNoteRange(col, iStartRow, iEndRow, begin, end);

  if (!bForward) {
    swap(begin, end);
  }

  while (begin != end) {
    if (!bForward) {
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
      if (!bAllowGraded && tn.result_.tns_ != TNS_None) {
        break;
      }

      return begin->first;
    } while (0);

    if (bForward) {
      ++begin;
    }
  }

  return -1;
}

// Find the closest note to fBeat.
int Player::GetClosestNote(
    int col, int iNoteRow, int iMaxRowsAhead, int iMaxRowsBehind,
    bool bAllowGraded) const {
  // Start at iIndexStartLookingAt and search outward.
  int iNextIndex = GetClosestNoteDirectional(
      col, iNoteRow, iNoteRow + iMaxRowsAhead, bAllowGraded, true);
  int iPrevIndex = GetClosestNoteDirectional(
      col, iNoteRow - iMaxRowsBehind, iNoteRow, bAllowGraded, false);

  if (iNextIndex == -1 && iPrevIndex == -1) {
    return -1;
  }
  if (iNextIndex == -1) {
    return iPrevIndex;
  }
  if (iPrevIndex == -1) {
    return iNextIndex;
  }

  /* Figure out which row is closer. */
  if (std::abs(iNoteRow - iNextIndex) > std::abs(iNoteRow - iPrevIndex)) {
    return iPrevIndex;
  } else {
    return iNextIndex;
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

  const unsigned int iOldCombo =
      player_stage_stats_ ? player_stage_stats_->cur_combo_ : 0;
  const unsigned int iOldMissCombo =
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

  SendComboMessages(iOldCombo, iOldMissCombo);

  if (life_meter_) {
    life_meter_->HandleTapScoreNone();
  }
  // TODO: Remove use of PlayerNumber
  PlayerNumber pn = PLAYER_INVALID;
  if (combined_life_meter_) {
    combined_life_meter_->HandleTapScoreNone(pn);
  }

  if (PENALIZE_TAP_SCORE_NONE) {
    SetJudgment(
        BeatToNoteRow(player_state_->position_.song_beat_), -1, TAP_EMPTY,
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
    Preference<float>* pVolume =
        Preference<float>::GetPreferenceByName("SoundVolume");
    float fVol = pVolume->Get();
    keysounds_[tn.keysound_index_].SetProperty("Volume", fVol);
  }
}

void Player::Step(
    int col, int row, const RageTimer& tm, bool bHeld, bool bRelease) {
  if (IsOniDead()) {
    return;
  }

  // Do everything that depends on a RageTimer here;
  // set your breakpoints somewhere after this block.
  const float fLastBeatUpdate =
      player_state_->position_.last_beat_update_.Ago();
  const float fPositionSeconds =
      player_state_->position_.music_seconds_ - tm.Ago();
  const float fTimeSinceStep = tm.Ago();

  float fSongBeat = player_state_->position_.song_beat_;

  if (GAMESTATE->cur_song_) {
    fSongBeat = GAMESTATE->cur_song_->song_timing_.GetBeatFromElapsedTime(
        fPositionSeconds);

    if (GAMESTATE->cur_steps_[player_state_->player_number_]) {
      fSongBeat = timing_->GetBeatFromElapsedTime(fPositionSeconds);
    }
  }

  const int iSongRow = row == -1 ? BeatToNoteRow(fSongBeat) : row;
  // If we're playinng on TwoPlayerSharedSides, we need to check player number
  // to determine which side of the screen we're on.
  if (GAMESTATE->GetCurrentStyle(player_state_->player_number_)->style_type_ ==
      StyleType_TwoPlayersSharedSides) {
    const int iStepSearchRows =
        std::max(
            BeatToNoteRow(timing_->GetBeatFromElapsedTime(
                player_state_->position_.music_seconds_ + StepSearchDistance)) -
                iSongRow,
            iSongRow - BeatToNoteRow(timing_->GetBeatFromElapsedTime(
                           player_state_->position_.music_seconds_ -
                           StepSearchDistance))) +
        ROWS_PER_BEAT;
    int iRowOfOverlappingNoteOrRow = row;
    if (row == -1) {
      iRowOfOverlappingNoteOrRow = GetClosestNote(
          col, iSongRow, iStepSearchRows, iStepSearchRows, false);
    }
    if (iRowOfOverlappingNoteOrRow != -1) {
      NoteData::iterator iter =
          note_data_.FindTapNote(col, iRowOfOverlappingNoteOrRow);
      if (iter != note_data_.end(col)) {
        const TapNote& tn = iter->second;
        if (tn.pn_ != PLAYER_INVALID &&
            tn.pn_ != player_state_->player_number_) {
          return;
        }
      }
    }
  }

  if (col != -1 && !bRelease) {
    // Update roll life
    // Let's not check the whole array every time.
    // Instead, only check 1 beat back.  Even 1 is overkill.
    // Just update the life here and let Update judge the roll.
    const int iStartCheckingAt = std::max(0, iSongRow - BeatToNoteRow(1));
    NoteData::TrackMap::iterator begin, end;
    note_data_.GetTapNoteRangeInclusive(
        col, iStartCheckingAt, iSongRow + 1, begin, end);
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

      const int iRow = begin->first;

      HoldNoteScore hns = tn.hold_result_.hns_;
      if (hns != HNS_None) {  // if this HoldNote already has a result
        continue;             // we don't need to update the logic for this one
      }

      // if they got a bad score or haven't stepped on the corresponding tap yet
      const TapNoteScore tns = tn.result_.tns_;
      bool bInitiatedNote = true;
      if (REQUIRE_STEP_ON_HOLD_HEADS) {
        bInitiatedNote =
            ((tns != TNS_None && tns != TNS_Miss) ||
             tn.result_.early_tns_ != TNS_None);  // did they step on the start?
      }
      const int iEndRow = iRow + tn.duration_;

      if (bInitiatedNote && tn.hold_result_.life_ != 0) {
        /* This hold note is not judged and we stepped on its head.  Update
         * iLastHeldRow. Do this even if we're a little beyond the end of the
         * hold note, to make sure iLastHeldRow is clamped to iEndRow if the
         * hold note is held all the way. */
        // LOG->Trace("setting iLastHeldRow to min of iSongRow (%i) and iEndRow
        // (%i)",iSongRow,iEndRow);
        tn.hold_result_.last_held_row_ = std::min(iSongRow, iEndRow);
      }

      // If the song beat is in the range of this hold:
      if (iRow <= iSongRow && iRow <= iEndRow) {
        if (bInitiatedNote) {
          // Increase life
          tn.hold_result_.life_ = 1;

          if (ROLL_BODY_INCREMENTS_COMBO &&
              player_state_->player_controller_ != PC_AUTOPLAY) {
            IncrementCombo();

            bool bBright = player_stage_stats_ &&
                           player_stage_stats_->cur_combo_ >
                               (unsigned int)BRIGHT_GHOST_COMBO_THRESHOLD;
            if (note_field_) {
              note_field_->DidHoldNote(col, HNS_Held, bBright);
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
  if (player_stage_stats_ && player_state_ && !bHeld && !bRelease) {
    // TODO: remove use of PlayerNumber
    PlayerNumber pn = player_state_->player_number_;
    Profile* pProfile = PROFILEMAN->GetProfile(pn);

    int iNumTracksHeld = 0;
    for (int t = 0; t < note_data_.GetNumTracks(); t++) {
      std::vector<GameInput> GameI;
      GAMESTATE->GetCurrentStyle(GetPlayerState()->player_number_)
          ->StyleInputToGameInput(t, pn, GameI);
      float secs_held = 0.0f;
      for (size_t i = 0; i < GameI.size(); ++i) {
        secs_held = std::max(secs_held, INPUTMAPPER->GetSecsHeld(GameI[i]));
      }
      if (secs_held > 0 && secs_held < m_fTimingWindowJump) {
        iNumTracksHeld++;
      }
    }

    float fCals = 0;
    switch (iNumTracksHeld) {
      case 0:
        // autoplay is on, or this is a computer player
        iNumTracksHeld = 1;
        [[fallthrough]];
      default: {
        float fCalsFor100Lbs = SCALE(iNumTracksHeld, 1, 2, 0.023f, 0.077f);
        float fCalsFor200Lbs = SCALE(iNumTracksHeld, 1, 2, 0.041f, 0.133f);
        fCals = SCALE(
            pProfile->GetCalculatedWeightPounds(), 100.f, 200.f, fCalsFor100Lbs,
            fCalsFor200Lbs);
      } break;
    }

    player_stage_stats_->calories_burned_ += fCals;
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
  const int iStepSearchRows =
      std::max(
          BeatToNoteRow(timing_->GetBeatFromElapsedTime(
              player_state_->position_.music_seconds_ + StepSearchDistance)) -
              iSongRow,
          iSongRow - BeatToNoteRow(timing_->GetBeatFromElapsedTime(
                         player_state_->position_.music_seconds_ -
                         StepSearchDistance))) +
      ROWS_PER_BEAT;
  int iRowOfOverlappingNoteOrRow = row;
  if (row == -1) {
    iRowOfOverlappingNoteOrRow =
        GetClosestNote(col, iSongRow, iStepSearchRows, iStepSearchRows, false);
  }

  // calculate TapNoteScore
  TapNoteScore score = TNS_None;

  if (iRowOfOverlappingNoteOrRow != -1) {
    // compute the score for this hit
    float fNoteOffset = 0.0f;
    // we need this later if we are autosyncing
    const float fStepBeat = NoteRowToBeat(iRowOfOverlappingNoteOrRow);
    const float fStepSeconds = timing_->GetElapsedTimeFromBeat(fStepBeat);

    if (row == -1) {
      // We actually stepped on the note this long ago:
      // fTimeSinceStep

      /* GAMESTATE->m_fMusicSeconds is the music time as of
       * GAMESTATE->m_LastBeatUpdate. Figure out what the music time is as of
       * now. */
      const float fCurrentMusicSeconds =
          player_state_->position_.music_seconds_ +
          (fLastBeatUpdate * GAMESTATE->song_options_.GetCurrent().music_rate_);

      // ... which means it happened at this point in the music:
      const float fMusicSeconds =
          fCurrentMusicSeconds -
          fTimeSinceStep * GAMESTATE->song_options_.GetCurrent().music_rate_;

      // The offset from the actual step in seconds:
      fNoteOffset = (fStepSeconds - fMusicSeconds) /
                    GAMESTATE->song_options_.GetCurrent()
                        .music_rate_;  // account for music rate
                                       /*
                                       LOG->Trace("step was %.3f ago, music is off by %f: %f vs %f, step was %f
                                       off",                                   fTimeSinceStep,
                                       GAMESTATE->m_LastBeatUpdate.Ago()/GAMESTATE->m_SongOptions.m_fMusicRate,
                                               fStepSeconds, fMusicSeconds, fNoteOffset );
                                       */
    }

    const float fSecondsFromExact = std::abs(fNoteOffset);

    TapNote* pTN = nullptr;
    NoteData::iterator iter =
        note_data_.FindTapNote(col, iRowOfOverlappingNoteOrRow);
    DEBUG_ASSERT(iter != note_data_.end(col));
    pTN = &iter->second;

    switch (player_state_->player_controller_) {
      case PC_HUMAN:
        switch (pTN->type_) {
          case TapNoteType_Mine:
            // Stepped too close to mine?
            if (!bRelease &&
                (!REQUIRE_STEP_ON_MINES || REQUIRE_STEP_ON_MINES == !bHeld) &&
                fSecondsFromExact <= GetWindowSeconds(TW_Mine)) {
              score = TNS_HitMine;
            }
            break;
          case TapNoteType_Attack:
            if (!bRelease && fSecondsFromExact <= GetWindowSeconds(TW_Attack) &&
                !pTN->result_.hidden_) {
              score = AllowW1() ? TNS_W1 : TNS_W2;  // sentinel
            }
            break;
          case TapNoteType_HoldHead:
            // oh wow, this was causing the trigger before the hold heads
            // bug. (It was fNoteOffset > 0.f before) -DaisuMaster
            if (!REQUIRE_STEP_ON_HOLD_HEADS &&
                (fNoteOffset <= GetWindowSeconds(TW_W5) &&
                 GetWindowSeconds(TW_W5) != 0)) {
              // Set it to the first non-disabled window.
              const auto& disabledWindows =
                  player_state_->player_options_.GetCurrent().disabled_windows_;
              if (!disabledWindows[TW_W1]) {
                score = TNS_W1;
              } else if (!disabledWindows[TW_W2]) {
                score = TNS_W2;
              } else if (!disabledWindows[TW_W3]) {
                score = TNS_W3;
              } else if (!disabledWindows[TW_W4]) {
                score = TNS_W4;
              } else if (!disabledWindows[TW_W5]) {
                score = TNS_W5;
              }

              break;
            }
            [[fallthrough]];
          default:
            if ((pTN->type_ == TapNoteType_Lift) == bRelease) {
              const auto& disabledWindows =
                  player_state_->player_options_.GetCurrent().disabled_windows_;
              if (fSecondsFromExact <= GetWindowSeconds(TW_W1) &&
                  !disabledWindows[TW_W1]) {
                score = TNS_W1;
              } else if (
                  fSecondsFromExact <= GetWindowSeconds(TW_W2) &&
                  !disabledWindows[TW_W2]) {
                score = TNS_W2;
              } else if (
                  fSecondsFromExact <= GetWindowSeconds(TW_W3) &&
                  !disabledWindows[TW_W3]) {
                score = TNS_W3;
              } else if (
                  fSecondsFromExact <= GetWindowSeconds(TW_W4) &&
                  !disabledWindows[TW_W4]) {
                score = TNS_W4;
              } else if (
                  fSecondsFromExact <= GetWindowSeconds(TW_W5) &&
                  !disabledWindows[TW_W5]) {
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
        if (pTN->type_ == TapNoteType_Mine) {
          // The CPU hits a lot of mines. Only consider hitting the
          // first mine for a row. We know we're the first mine if
          // there are are no mines to the left of us.
          for (int t = 0; t < col; t++) {
            if (note_data_.GetTapNote(t, iRowOfOverlappingNoteOrRow).type_ ==
                TapNoteType_Mine) {  // there's a mine to the left of us
              return;                // avoid
            }
          }

          // The CPU hits a lot of mines. Make it less likely to hit
          // mines that don't have a tap note on the same row.
          bool bTapsOnRow =
              note_data_.IsThereATapOrHoldHeadAtRow(iRowOfOverlappingNoteOrRow);
          TapNoteScore get_to_avoid = bTapsOnRow ? TNS_W3 : TNS_W4;

          if (score >= get_to_avoid) {
            return;  // avoided
          } else {
            score = TNS_HitMine;
          }
        }

        if (pTN->type_ == TapNoteType_Attack && score > TNS_W4) {
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
          float fWindowW1 = GetWindowSeconds(TW_W1);
          float fWindowW2 = GetWindowSeconds(TW_W2);
          float fWindowW3 = GetWindowSeconds(TW_W3);
          float fWindowW4 = GetWindowSeconds(TW_W4);
          float fWindowW5 = GetWindowSeconds(TW_W5);

          // W1 is the top judgment, there is no overlap.
          if (score == TNS_W1) {
            fNoteOffset = randomf(-fWindowW1, fWindowW1);
          } else {
            // figure out overlap.
            float fLowerBound = 0.0f;     // negative upper limit
            float fUpperBound = 0.0f;     // positive lower limit
            float fCompareWindow = 0.0f;  // filled in here:
            if (score == TNS_W2) {
              fLowerBound = -fWindowW1;
              fUpperBound = fWindowW1;
              fCompareWindow = fWindowW2;
            } else if (score == TNS_W3) {
              fLowerBound = -fWindowW2;
              fUpperBound = fWindowW2;
              fCompareWindow = fWindowW3;
            } else if (score == TNS_W4) {
              fLowerBound = -fWindowW3;
              fUpperBound = fWindowW3;
              fCompareWindow = fWindowW4;
            } else if (score == TNS_W5) {
              fLowerBound = -fWindowW4;
              fUpperBound = fWindowW4;
              fCompareWindow = fWindowW5;
            }
            float f1 = randomf(-fCompareWindow, fLowerBound);
            float f2 = randomf(fUpperBound, fCompareWindow);

            if (randomf() * 100 >= 50) {
              fNoteOffset = f1;
            } else {
              fNoteOffset = f2;
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
    if (pTN->type_ == TapNoteType_Attack && score == TNS_W2) {
      score = TNS_None;  // don't score this as anything

      sound_attack_launch_.Play(false);

      // put attack in effect
      Attack attack(
          ATTACK_LEVEL_1,
          -1,  // now
          pTN->attack_duration_seconds_, pTN->attack_modifiers_, true, false);

      // TODO: Remove use of PlayerNumber
      PlayerNumber pnToAttack = OPPOSITE_PLAYER[player_state_->player_number_];
      PlayerState* pPlayerStateToAttack = GAMESTATE->player_state_[pnToAttack];
      pPlayerStateToAttack->LaunchAttack(attack);

      // remove all TapAttacks on this row
      for (int t = 0; t < note_data_.GetNumTracks(); t++) {
        const TapNote& tn =
            note_data_.GetTapNote(t, iRowOfOverlappingNoteOrRow);
        if (tn.type_ == TapNoteType_Attack) {
          HideNote(t, iRowOfOverlappingNoteOrRow);
        }
      }
    }

    if (player_state_->player_controller_ == PC_HUMAN && score >= TNS_W3) {
      AdjustSync::HandleAutosync(fNoteOffset, fStepSeconds);
    }

    // Do game-specific and mode-specific score mapping.
    score = GAMESTATE->GetCurrentGame()->MapTapNoteScore(score);
    if (score == TNS_W1 && !GAMESTATE->ShowW1()) {
      score = TNS_W2;
    }

    if (score != TNS_None) {
      TapNoteScore minTnsToScore = g_MinTNSToScoreNotes;
      bool badTns = false;
      if (minTnsToScore != TNS_None) {
        switch (score) {
          case TNS_W1:
          case TNS_W2:
          case TNS_W3:
          case TNS_W4:
          case TNS_W5:
            if (score < minTnsToScore) {
              badTns = true;
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
      int iNumTracksWithNotes =
          note_data_.GetNumTracksWithTapOrHoldHead(iRowOfOverlappingNoteOrRow);
      if (badTns && -fNoteOffset < 0.0f && iNumTracksWithNotes == 1) {
        // If a player gets an early way off as well as an early decent, we only
        // want to change the life once.
        if (pTN->result_.early_tns_ == TNS_None) {
          pTN->result_.early_tns_ = score;
          pTN->result_.early_tap_note_offset_ = -fNoteOffset;
          ChangeLife(score);
          // We don't want to trigger a JudgmentMessage since we use that to
          // indicate "finalized" judgments. We handle that elsewhere so instead
          // we introduce a new EarlyHitMessage to control the instant feedback
          // we want to relay to to the player.
          Message msg("EarlyHit");
          msg.SetParam("Player", player_state_->player_number_);
          msg.SetParam("TapNoteScore", score);
          msg.SetParam("Column", col);
          msg.SetParam("TapNoteOffset", -fNoteOffset);
          MESSAGEMAN->Broadcast(msg);
        }
      } else {
        // If earlyTns is not set (good hit):
        //   - Default behavior
        // Otherwise:
        //   - this should only trigger a judgment and a score change if
        //   	 it's not a miss and the TNS is minTnsToScore or better.
        if (pTN->result_.early_tns_ == TNS_None ||
            (score != TNS_Miss && !badTns)) {
          pTN->result_.tns_ = score;
          pTN->result_.tap_note_offset_ = -fNoteOffset;
          pTN->result_.held_ = false;
        }
      }
    }

    if (GAMESTATE->GetCurrentGame()->count_notes_separately_) {
      if (pTN->type_ != TapNoteType_Mine) {
        const bool bBlind =
            (player_state_->player_options_.GetCurrent().blind_ != 0);
        // XXX: This is the wrong combo for shared players.
        // STATSMAN->m_CurStageStats.m_Player[pn] might work, but could be
        // wrong.
        const bool bBright = (player_stage_stats_ &&
                              player_stage_stats_->cur_combo_ >
                                  (unsigned int)BRIGHT_GHOST_COMBO_THRESHOLD) ||
                             bBlind;
        if (note_field_) {
          note_field_->DidTapNote(col, bBlind ? TNS_W1 : score, bBright);
        }
        if (score >= player_state_->player_options_.GetCurrent()
                         .min_tns_to_hide_notes_ ||
            bBlind) {
          HideNote(col, iRowOfOverlappingNoteOrRow);
        }
      }
    } else if (NoteDataWithScoring::IsRowCompletelyJudged(
                   note_data_, iRowOfOverlappingNoteOrRow)) {
      FlashGhostRow(iRowOfOverlappingNoteOrRow);
    }
  }

  if (score == TNS_None) {
    DoTapScoreNone();
  }

  if (!bRelease) {
    /* Search for keyed sounds separately.  Play the nearest note. */
    /* XXX: This isn't quite right. As per the above XXX for
     * iRowOfOverlappingNote, if iRowOfOverlappingNote is set to a previous
     * note, the keysound could have changed and this would cause the wrong one
     * to play, in essence playing two sounds in the opposite order. Maybe this
     * should always perform the search. Still, even that doesn't seem quite
     * right since it would then play the same (new) keysound twice which would
     * sound wrong even though the notes were judged as being correct, above.
     * Fixing the above problem would fix this one as well. */
    int iHeadRow;
    if (iRowOfOverlappingNoteOrRow != -1 && score != TNS_None) {
      // just pressing a note, use that row.
      // in other words, iRowOfOverlappingNoteOrRow = iRowOfOverlappingNoteOrRow
    } else if (note_data_.IsHoldNoteAtRow(col, iSongRow, &iHeadRow)) {
      // stepping on a hold, use it!
      iRowOfOverlappingNoteOrRow = iHeadRow;
    } else {
      // or else find the closest note.
      iRowOfOverlappingNoteOrRow =
          GetClosestNote(col, iSongRow, MAX_NOTE_ROW, MAX_NOTE_ROW, true);
    }
    if (iRowOfOverlappingNoteOrRow != -1) {
      const TapNote& tn =
          note_data_.GetTapNote(col, iRowOfOverlappingNoteOrRow);
      PlayKeysound(tn, score);
    }
  }
  // XXX:
  if (!bRelease) {
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

void Player::UpdateTapNotesMissedOlderThan(float fMissIfOlderThanSeconds) {
  // LOG->Trace( "Steps::UpdateTapNotesMissedOlderThan(%f)",
  // fMissIfOlderThanThisBeat );
  int iMissIfOlderThanThisRow;
  const float fEarliestTime =
      player_state_->position_.music_seconds_ - fMissIfOlderThanSeconds;
  {
    TimingData::GetBeatArgs beat_info;
    beat_info.elapsed_time_ = fEarliestTime;
    timing_->GetBeatAndBPSFromElapsedTime(beat_info);

    iMissIfOlderThanThisRow = BeatToNoteRow(beat_info.beat_);
    if (beat_info.freeze_out_ || beat_info.delay_out_) {
      /* If there is a freeze on iMissIfOlderThanThisIndex, include this index
       * too. Otherwise we won't show misses for tap notes on freezes until the
       * freeze finishes. */
      if (!beat_info.delay_out_) {
        iMissIfOlderThanThisRow++;
      }
    }
  }

  NoteData::all_tracks_iterator& iter = *iter_needs_tap_judging_;

  for (; !iter.IsAtEnd() && iter.Row() < iMissIfOlderThanThisRow; ++iter) {
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
  const int iEndRow = BeatToNoteRow(timing_->GetBeatFromElapsedTime(
      player_state_->position_.music_seconds_ + GetMaxStepDistanceSeconds()));
  bool bAllJudged = true;
  const bool bSeparately = GAMESTATE->GetCurrentGame()->count_notes_separately_;

  {
    NoteData::all_tracks_iterator iter = *iter_unjudged_rows_;
    int iLastSeenRow = -1;
    for (; !iter.IsAtEnd() && iter.Row() <= iEndRow; ++iter) {
      int iRow = iter.Row();

      // Do not judge arrows in WarpSegments or FakeSegments
      if (!timing_->IsJudgableAtRow(iRow)) {
        continue;
      }

      if (iLastSeenRow != iRow) {
        iLastSeenRow = iRow;

        // crossed a nonempty row
        if (!NoteDataWithScoring::IsRowCompletelyJudged(note_data_, iRow)) {
          bAllJudged = false;
          continue;
        }
        if (bAllJudged) {
          *iter_unjudged_rows_ = iter;
        }
        if (judged_rows_->JudgeRow(iRow)) {
          continue;
        }
        const TapNoteResult& lastTNR =
            NoteDataWithScoring::LastTapNoteWithResult(note_data_, iRow)
                .result_;

        if (lastTNR.tns_ < TNS_Miss) {
          continue;
        }
        if (bSeparately) {
          for (int iTrack = 0; iTrack < note_data_.GetNumTracks(); ++iTrack) {
            const TapNote& tn = note_data_.GetTapNote(iTrack, iRow);
            if (tn.type_ == TapNoteType_Empty || tn.type_ == TapNoteType_Mine ||
                tn.type_ == TapNoteType_AutoKeysound) {
              continue;
            }
            SetJudgment(iRow, iTrack, tn);
          }
        } else {
          SetJudgment(
              iRow, note_data_.GetFirstTrackWithTapOrHoldHead(iRow),
              NoteDataWithScoring::LastTapNoteWithResult(note_data_, iRow));
        }
        HandleTapRowScore(iRow);
      }
    }
  }

  // handle mines.
  {
    bAllJudged = true;
    std::set<RageSound*> setSounds;
    NoteData::all_tracks_iterator iter = *iter_unjudged_mine_rows_;  // copy
    int iLastSeenRow = -1;
    for (; !iter.IsAtEnd() && iter.Row() <= iEndRow; ++iter) {
      int iRow = iter.Row();

      // Do not worry about mines in WarpSegments or FakeSegments
      if (!timing_->IsJudgableAtRow(iRow)) {
        continue;
      }

      TapNote& tn = *iter;

      if (iRow != iLastSeenRow) {
        iLastSeenRow = iRow;
        if (bAllJudged) {
          *iter_unjudged_mine_rows_ = iter;
        }
      }

      bool bMineNotHidden = tn.type_ == TapNoteType_Mine && !tn.result_.hidden_;
      if (!bMineNotHidden) {
        continue;
      }

      switch (tn.result_.tns_) {
        DEFAULT_FAIL(tn.result_.tns_);
        case TNS_None:
          bAllJudged = false;
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
        setSounds.insert(&keysounds_[tn.keysound_index_]);
      } else if (g_bEnableMineSoundPlayback) {
        setSounds.insert(&sound_mine_);
      }

      /* Attack Mines:
       * Only difference is these launch an attack rather than affecting
       * the lifebar. All the other mine impacts (score, dance points,
       * etc.) are still applied. */
      if (player_state_->player_options_.GetCurrent()
              .transforms_[PlayerOptions::TRANSFORM_ATTACKMINES]) {
        const float fAttackRunTime = ATTACK_RUN_TIME_MINE;

        Attack attMineAttack;
        attMineAttack.modifiers_ = ApplyRandomAttack();
        attMineAttack.start_second_ = ATTACK_STARTS_NOW;
        attMineAttack.secs_remaining_ = fAttackRunTime;

        player_state_->LaunchAttack(attMineAttack);
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
    if ((iter.IsAtEnd() || iLastSeenRow == iEndRow) && bAllJudged) {
      *iter_unjudged_mine_rows_ = iter;
    }

    for (RageSound* sound : setSounds) {
      // Only play one copy of each mine sound at a time per player.
      sound->Stop();
      sound->Play(false);
    }
  }
}

void Player::FlashGhostRow(int iRow) {
  TapNoteScore lastTNS =
      NoteDataWithScoring::LastTapNoteWithResult(note_data_, iRow).result_.tns_;
  const bool bBlind = (player_state_->player_options_.GetCurrent().blind_ != 0);
  const bool bBright =
      (player_stage_stats_ && player_stage_stats_->cur_combo_ >
                                  (unsigned int)BRIGHT_GHOST_COMBO_THRESHOLD) ||
      bBlind;

  for (int iTrack = 0; iTrack < note_data_.GetNumTracks(); ++iTrack) {
    const TapNote& tn = note_data_.GetTapNote(iTrack, iRow);

    if (tn.type_ == TapNoteType_Empty || tn.type_ == TapNoteType_Mine ||
        tn.type_ == TapNoteType_Fake || tn.result_.hidden_) {
      continue;
    }
    if (note_field_) {
      note_field_->DidTapNote(iTrack, lastTNS, bBright);
    }
    if (lastTNS >= player_state_->player_options_.GetCurrent()
                       .min_tns_to_hide_notes_ ||
        bBlind) {
      HideNote(iTrack, iRow);
    }
  }
}

void Player::CrossedRows(int iLastRowCrossed, const RageTimer& now) {
  // LOG->Trace( "Player::CrossedRows   %d    %d", iFirstRowCrossed,
  // iLastRowCrossed );

  NoteData::all_tracks_iterator& iter = *iter_uncrossed_rows_;
  int iLastSeenRow = -1;
  for (; !iter.IsAtEnd() && iter.Row() <= iLastRowCrossed; ++iter) {
    // Apply InitialHoldLife.
    TapNote& tn = *iter;
    int iRow = iter.Row();
    int iTrack = iter.Track();
    switch (tn.type_) {
      case TapNoteType_HoldHead: {
        tn.hold_result_.life_ = INITIAL_HOLD_LIFE;
        if (!REQUIRE_STEP_ON_HOLD_HEADS) {
          PlayerNumber pn = player_state_->player_number_;
          std::vector<GameInput> GameI;
          GAMESTATE->GetCurrentStyle(GetPlayerState()->player_number_)
              ->StyleInputToGameInput(iTrack, pn, GameI);
          if (PREFSMAN->pad_stick_seconds_ > 0.f) {
            for (size_t i = 0; i < GameI.size(); ++i) {
              float fSecsHeld = INPUTMAPPER->GetSecsHeld(GameI[i]);
              if (fSecsHeld >= PREFSMAN->pad_stick_seconds_) {
                Step(
                    iTrack, -1, now - PREFSMAN->pad_stick_seconds_, true,
                    false);
              }
            }
          } else {
            if (INPUTMAPPER->IsBeingPressed(GameI)) {
              Step(iTrack, -1, now, true, false);
            }
          }
        }
        break;
      }
      case TapNoteType_Mine: {
        // Hold the panel while crossing a mine will cause the mine to explode
        // TODO: Remove use of PlayerNumber.
        PlayerNumber pn = player_state_->player_number_;
        std::vector<GameInput> GameI;
        GAMESTATE->GetCurrentStyle(GetPlayerState()->player_number_)
            ->StyleInputToGameInput(iTrack, pn, GameI);
        if (PREFSMAN->pad_stick_seconds_ > 0.0f) {
          for (size_t i = 0; i < GameI.size(); ++i) {
            float fSecsHeld = INPUTMAPPER->GetSecsHeld(GameI[i]);
            if (fSecsHeld >= PREFSMAN->pad_stick_seconds_) {
              Step(iTrack, -1, now - PREFSMAN->pad_stick_seconds_, true, false);
            }
          }
        } else if (INPUTMAPPER->IsBeingPressed(GameI)) {
          Step(iTrack, iRow, now, true, false);
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
          this->timing_->IsJudgableAtRow(iRow)) {
        Step(iTrack, iRow, now, false, false);
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
    if (iRow != iLastSeenRow) {
      // crossed a new not-empty row
      iLastSeenRow = iRow;

      // handle autokeysounds here (if not in the editor).
      if (!GAMESTATE->in_step_editor_) {
        for (int t = 0; t < note_data_.GetNumTracks(); ++t) {
          const TapNote& tap = note_data_.GetTapNote(t, iRow);
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
    for (int r = first_uncrossed_row_; r <= iLastRowCrossed; ++r) {
      int tickCurrent = timing_->GetTickcountAtRow(r);

      // There is a tick count at this row
      if (tickCurrent > 0 && r % (ROWS_PER_BEAT / tickCurrent) == 0) {
        std::vector<int> viColsWithHold;
        int iNumHoldsHeldThisRow = 0;
        int iNumHoldsMissedThisRow = 0;

        // start at r-1 so that we consider holds whose end rows are equal to
        // the checkpoint row
        NoteData::all_tracks_iterator nIter =
            note_data_.GetTapNoteRangeAllTracks(r - 1, r, true);
        for (; !nIter.IsAtEnd(); ++nIter) {
          TapNote& tn = *nIter;
          if (tn.type_ != TapNoteType_HoldHead) {
            continue;
          }

          int iTrack = nIter.Track();
          viColsWithHold.push_back(iTrack);

          if (tn.hold_result_.life_ > 0) {
            ++iNumHoldsHeldThisRow;
            ++tn.hold_result_.checkpoints_hit_;
          } else {
            ++iNumHoldsMissedThisRow;
            ++tn.hold_result_.checkpoints_missed_;
          }
        }
        GAMESTATE->SetProcessedTimingData(this->timing_);

        // TODO: Find a better way of handling hold checkpoints with other taps.
        if (!viColsWithHold.empty() &&
            (CHECKPOINTS_TAPS_SEPARATE_JUDGMENT ||
             note_data_.GetNumTapNotesInRow(r) == 0)) {
          HandleHoldCheckpoint(
              r, iNumHoldsHeldThisRow, iNumHoldsMissedThisRow, viColsWithHold);
        }
      }
    }
  }

  first_uncrossed_row_ = iLastRowCrossed + 1;
}

void Player::HandleTapRowScore(unsigned row) {
  bool bNoCheating = true;
#ifdef DEBUG
  bNoCheating = false;
#endif

  // Do not score rows in WarpSegments or FakeSegments
  if (!timing_->IsJudgableAtRow(row)) {
    return;
  }

  if (GAMESTATE->demonstration_or_jukebox_) {
    bNoCheating = false;
  }
  // don't accumulate points if AutoPlay is on.
  if (bNoCheating && player_state_->player_controller_ == PC_AUTOPLAY) {
    return;
  }

  TapNote lastTap = NoteDataWithScoring::LastTapNoteWithResult(note_data_, row);
  TapNoteScore scoreOfLastTap = lastTap.result_.tns_;
  const unsigned int iOldCombo =
      player_stage_stats_ ? player_stage_stats_->cur_combo_ : 0;
  const unsigned int iOldMissCombo =
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
  const unsigned int iCurMissCombo =
      player_stage_stats_ ? player_stage_stats_->cur_miss_combo_ : 0;

  SendComboMessages(iOldCombo, iOldMissCombo);

  if (player_stage_stats_) {
    SetCombo(iCurCombo, iCurMissCombo);
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
    score_display_->OnJudgment(scoreOfLastTap);
  }
  if (secondary_score_display_) {
    if (player_stage_stats_) {
      secondary_score_display_->SetScore(player_stage_stats_->score_);
    }
    secondary_score_display_->OnJudgment(scoreOfLastTap);
  }

  // if earlyTns is set, the life has already been affected for this note.
  // Don't want to change the life again for a re-hit.
  if (lastTap.result_.early_tns_ == TNS_None) {
    ChangeLife(scoreOfLastTap);
  }
}

void Player::HandleHoldCheckpoint(
    int iRow, int iNumHoldsHeldThisRow, int iNumHoldsMissedThisRow,
    const std::vector<int>& viColsWithHold) {
  bool bNoCheating = true;
#ifdef DEBUG
  bNoCheating = false;
#endif

  // WarpSegments and FakeSegments aren't judged in any way.
  if (!timing_->IsJudgableAtRow(iRow)) {
    return;
  }

  // don't accumulate combo if AutoPlay is on.
  if (bNoCheating && player_state_->player_controller_ == PC_AUTOPLAY) {
    return;
  }

  const unsigned int iOldCombo =
      player_stage_stats_ ? player_stage_stats_->cur_combo_ : 0;
  const unsigned int iOldMissCombo =
      player_stage_stats_ ? player_stage_stats_->cur_miss_combo_ : 0;

  if (primary_score_keeper_) {
    primary_score_keeper_->HandleHoldCheckpointScore(
        note_data_, iRow, iNumHoldsHeldThisRow, iNumHoldsMissedThisRow);
  }
  if (secondary_score_keeper_) {
    secondary_score_keeper_->HandleHoldCheckpointScore(
        note_data_, iRow, iNumHoldsHeldThisRow, iNumHoldsMissedThisRow);
  }

  if (iNumHoldsMissedThisRow == 0) {
    // added for http://ssc.ajworld.net/sm-ssc/bugtracker/view.php?id=16 -aj
    if (CHECKPOINTS_FLASH_ON_HOLD && note_field_ != nullptr) {
      for (const int& i : viColsWithHold) {
        bool bBright = player_stage_stats_ &&
                       player_stage_stats_->cur_combo_ >
                           (unsigned int)BRIGHT_GHOST_COMBO_THRESHOLD;
        if (note_field_) {
          note_field_->DidHoldNote(i, HNS_Held, bBright);
        }
      }
    }
  }

  SendComboMessages(iOldCombo, iOldMissCombo);

  if (player_stage_stats_) {
    SetCombo(
        player_stage_stats_->cur_combo_, player_stage_stats_->cur_miss_combo_);
    player_stage_stats_->UpdateComboList(
        STATSMAN->cur_stage_stats_.steps_seconds_, false);
  }

  ChangeLife(
      iNumHoldsMissedThisRow == 0 ? TNS_CheckpointHit : TNS_CheckpointMiss);

  SetJudgment(
      iRow, viColsWithHold[0], TAP_EMPTY,
      iNumHoldsMissedThisRow == 0 ? TNS_CheckpointHit : TNS_CheckpointMiss, 0);
}

void Player::HandleHoldScore(const TapNote& tn) {
  HoldNoteScore holdScore = tn.hold_result_.hns_;
  TapNoteScore tapScore = tn.result_.tns_;
  bool bNoCheating = true;
#ifdef DEBUG
  bNoCheating = false;
#endif

  if (GAMESTATE->demonstration_or_jukebox_) {
    bNoCheating = false;
  }
  // don't accumulate points if AutoPlay is on.
  if (bNoCheating && player_state_->player_controller_ == PC_AUTOPLAY) {
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
    score_display_->OnJudgment(holdScore, tapScore);
  }
  if (secondary_score_display_) {
    if (player_stage_stats_) {
      secondary_score_display_->SetScore(player_stage_stats_->score_);
    }
    secondary_score_display_->OnJudgment(holdScore, tapScore);
  }

  ChangeLife(holdScore, tapScore);
}

float Player::GetMaxStepDistanceSeconds() {
  float fMax = 0;
  fMax = std::max(fMax, GetWindowSeconds(TW_W5));
  fMax = std::max(fMax, GetWindowSeconds(TW_W4));
  fMax = std::max(fMax, GetWindowSeconds(TW_W3));
  fMax = std::max(fMax, GetWindowSeconds(TW_W2));
  fMax = std::max(fMax, GetWindowSeconds(TW_W1));
  fMax = std::max(fMax, GetWindowSeconds(TW_Mine));
  fMax = std::max(fMax, GetWindowSeconds(TW_Hold));
  fMax = std::max(fMax, GetWindowSeconds(TW_Roll));
  fMax = std::max(fMax, GetWindowSeconds(TW_Attack));
  fMax = std::max(fMax, GetWindowSeconds(TW_Checkpoint));
  float f = GAMESTATE->song_options_.GetCurrent().music_rate_ * fMax;
  return f + m_fMaxInputLatencySeconds;
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

void Player::SetMineJudgment(TapNoteScore tns, int iTrack) {
  if (send_judgment_and_combo_messages_) {
    Message msg("Judgment");
    msg.SetParam("Player", player_state_->player_number_);
    msg.SetParam("TapNoteScore", tns);
    msg.SetParam("FirstTrack", iTrack);
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
    int iRow, int iTrack, const TapNote& tn, TapNoteScore tns,
    float fTapNoteOffset) {
  if (send_judgment_and_combo_messages_) {
    Message msg("Judgment");
    msg.SetParam("Player", player_state_->player_number_);
    msg.SetParam("FirstTrack", iTrack);
    msg.SetParam("TapNoteScore", tns);
    msg.SetParam("Early", fTapNoteOffset < 0.0f);
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

    for (int iTrackTmp = 0; iTrackTmp < note_data_.GetNumTracks();
         ++iTrackTmp) {
      NoteData::iterator tnTmp = note_data_.FindTapNote(iTrackTmp, iRow);
      if (tnTmp != note_data_.end(iTrackTmp)) {
        tnTmp->second.PushSelf(L);
        lua_rawseti(L, -3, iTrackTmp + 1);
      } else {
        int iHeadRow;
        if (note_data_.IsHoldNoteAtRow(iTrackTmp, iRow, &iHeadRow)) {
          NoteData::iterator hold = note_data_.FindTapNote(iTrackTmp, iHeadRow);
          hold->second.PushSelf(L);
          lua_rawseti(L, -2, iTrackTmp + 1);
        }
      }
    }
    msg.SetParamFromStack(L, "Holds");
    msg.SetParamFromStack(L, "Notes");

    LUA->Release(L);
    MESSAGEMAN->Broadcast(msg);
  }
}

void Player::SetHoldJudgment(TapNote& tn, int iTrack) {
  ASSERT(iTrack < (int)hold_judgment_.size());
  if (hold_judgment_[iTrack]) {
    hold_judgment_[iTrack]->SetHoldJudgment(tn.hold_result_.hns_);
  }

  if (send_judgment_and_combo_messages_) {
    Message msg("Judgment");
    msg.SetParam("Player", player_state_->player_number_);
    msg.SetParam("FirstTrack", iTrack);
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

void Player::SetCombo(unsigned int iCombo, unsigned int iMisses) {
  if (!seen_combo_yet_)  // first update, don't set bIsMilestone=true
  {
    seen_combo_yet_ = true;
    last_seen_combo_ = iCombo;
  }

  bool b25Milestone = false;
  bool b50Milestone = false;
  bool b100Milestone = false;
  bool b250Milestone = false;
  bool b1000Milestone = false;

#define MILESTONE_CHECK(amount) \
  ((iCombo / amount) > (last_seen_combo_ / amount))
  if (last_seen_combo_ < 600) {
    b25Milestone = MILESTONE_CHECK(25);
    b50Milestone = MILESTONE_CHECK(50);
    b100Milestone = MILESTONE_CHECK(100);
    b250Milestone = MILESTONE_CHECK(250);
    b1000Milestone = MILESTONE_CHECK(1000);
  } else {
    b1000Milestone = MILESTONE_CHECK(1000);
  }
#undef MILESTONE_CHECK

  last_seen_combo_ = iCombo;

  if (b25Milestone) {
    this->PlayCommand("TwentyFiveMilestone");
  }
  if (b50Milestone) {
    this->PlayCommand("FiftyMilestone");
  }
  if (b100Milestone) {
    this->PlayCommand("HundredMilestone");
  }
  if (b250Milestone) {
    this->PlayCommand("TwoHundredFiftyMilestone");
  }
  if (b1000Milestone) {
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
  bool bPastBeginning = false;
  if (GAMESTATE->IsCourseMode()) {
    int iSongIndexStartColoring =
        GAMESTATE->cur_course_->GetEstimatedNumStages();
    iSongIndexStartColoring = static_cast<int>(
        std::floor(iSongIndexStartColoring * PERCENT_UNTIL_COLOR_COMBO));
    bPastBeginning = GAMESTATE->GetCourseSongIndex() >= iSongIndexStartColoring;
  } else {
    bPastBeginning =
        player_state_->position_.music_seconds_ >
        GAMESTATE->cur_song_->music_length_seconds_ * PERCENT_UNTIL_COLOR_COMBO;
  }

  if (send_judgment_and_combo_messages_) {
    Message msg("Combo");
    if (iCombo) {
      msg.SetParam("Combo", iCombo);
    }
    if (iMisses) {
      msg.SetParam("Misses", iMisses);
    }
    if (bPastBeginning && player_stage_stats_->FullComboOfScore(TNS_W1)) {
      msg.SetParam("FullComboW1", true);
    }
    if (bPastBeginning && player_stage_stats_->FullComboOfScore(TNS_W2)) {
      msg.SetParam("FullComboW2", true);
    }
    if (bPastBeginning && player_stage_stats_->FullComboOfScore(TNS_W3)) {
      msg.SetParam("FullComboW3", true);
    }
    if (bPastBeginning && player_stage_stats_->FullComboOfScore(TNS_W4)) {
      msg.SetParam("FullComboW4", true);
    }
    this->HandleMessage(msg);
  }
}

void Player::IncrementComboOrMissCombo(bool bComboOrMissCombo) {
  const unsigned int iOldCombo =
      player_stage_stats_ ? player_stage_stats_->cur_combo_ : 0;
  const unsigned int iOldMissCombo =
      player_stage_stats_ ? player_stage_stats_->cur_miss_combo_ : 0;

  if (player_stage_stats_) {
    if (bComboOrMissCombo) {
      player_stage_stats_->cur_combo_++;
      player_stage_stats_->cur_miss_combo_ = 0;
    } else {
      player_stage_stats_->cur_combo_ = 0;
      player_stage_stats_->cur_miss_combo_++;
    }
    SetCombo(
        player_stage_stats_->cur_combo_, player_stage_stats_->cur_miss_combo_);
  }

  SendComboMessages(iOldCombo, iOldMissCombo);
}

std::string Player::ApplyRandomAttack() {
  if (GAMESTATE->random_attacks_.size() < 1) {
    return "";
  }

  int iAttackToUse = RandomInt(GAMESTATE->random_attacks_.size());

  return GAMESTATE->random_attacks_[iAttackToUse];
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
    Actor* pActor = Luna<Actor>::check(L, 1);
    p->SetActorWithJudgmentPosition(pActor);
    COMMON_RETURN_SELF;
  }
  static int SetActorWithComboPosition(T* p, lua_State* L) {
    Actor* pActor = Luna<Actor>::check(L, 1);
    p->SetActorWithComboPosition(pActor);
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
