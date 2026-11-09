#include "ScreenEvaluation.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <vector>

#include "ActorUtil.h"
#include "AnnouncerManager.h"
#include "CodeDetector.h"
#include "Course.h"
#include "CryptManager.h"
#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "GameInput.h"
#include "GameManager.h"
#include "GameSoundManager.h"
#include "GameState.h"
#include "Grade.h"
#include "HighScore.h"
#include "InputEventPlus.h"
#include "InputFilter.h"
#include "LuaManager.h"
#include "MemoryCardManager.h"
#include "ModsGroup.h"
#include "PlayerNumber.h"
#include "PlayerState.h"
#include "PrefsManager.h"
#include "Profile.h"
#include "ProfileManager.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "RageUtil/RandomNumbers.h"
#include "ScoreKeeperNormal.h"
#include "Screen.h"
#include "ScreenManager.h"
#include "ScreenMessage.h"
#include "ScreenWithMenuElements.h"
#include "Song.h"
#include "SongManager.h"
#include "StatsManager.h"
#include "StepMania.h"
#include "Steps.h"
#include "ThemeManager.h"
#include "ThemeMetric.h"
#include "global.h"

// metrics that are common to all ScreenEvaluation classes
#define BANNER_WIDTH THEME->GetMetricF(name_, "BannerWidth")
#define BANNER_HEIGHT THEME->GetMetricF(name_, "BannerHeight")
static const char* JudgmentLineNames[] = {"W1", "W2",   "W3",   "W4",
                                          "W5", "Miss", "Held", "MaxCombo"};
XToString(JudgmentLine);
LuaXType(JudgmentLine);
XToLocalizedString(JudgmentLine);
LuaFunction(
    JudgmentLineToLocalizedString,
    JudgmentLineToLocalizedString(Enum::Check<JudgmentLine>(L, 1)));

static const char* DetailLineNames[NUM_DetailLine] = {
    "NumSteps", "Jumps", "Holds", "Mines", "Hands", "Rolls", "Lifts", "Fakes"};
XToString(DetailLine);
#define DETAILLINE_FORMAT THEME->GetMetric(name_, "DetailLineFormat")

#define CHEER_DELAY_SECONDS THEME->GetMetricF(name_, "CheerDelaySeconds")
#define BAR_ACTUAL_MAX_COMMAND THEME->GetMetricA(name_, "BarActualMaxCommand")

// metrics that are specific to classes derived from ScreenEvaluation
#define SHOW_BANNER_AREA THEME->GetMetricB(name_, "ShowBannerArea")
#define SHOW_GRADE_AREA THEME->GetMetricB(name_, "ShowGradeArea")
#define SHOW_POINTS_AREA THEME->GetMetricB(name_, "ShowPointsArea")
#define SHOW_BONUS_AREA THEME->GetMetricB(name_, "ShowBonusArea")
#define SHOW_SURVIVED_AREA THEME->GetMetricB(name_, "ShowSurvivedArea")
#define SHOW_WIN_AREA THEME->GetMetricB(name_, "ShowWinArea")
#define SHOW_SHARED_JUDGMENT_LINE_LABELS \
  THEME->GetMetricB(name_, "ShowSharedJudgmentLineLabels")
#define SHOW_JUDGMENT_LINE(l) \
  THEME->GetMetricB(name_, "ShowJudgmentLine" + JudgmentLineToString(l))

#define SHOW_DETAIL_AREA THEME->GetMetricB(name_, "ShowDetailArea")
#define SHOW_SCORE_AREA THEME->GetMetricB(name_, "ShowScoreArea")
#define SHOW_TIME_AREA THEME->GetMetricB(name_, "ShowTimeArea")
#define SHOW_RECORDS_AREA THEME->GetMetricB(name_, "ShowRecordsArea")
#define PLAYER_OPTIONS_HIDE_FAIL_TYPE \
  THEME->GetMetricB(name_, "PlayerOptionsHideFailType")
#define PLAYER_OPTIONS_SEPARATOR \
  THEME->GetMetric(name_, "PlayerOptionsSeparator")
#define CHECKPOINTS_WITH_JUDGMENTS \
  THEME->GetMetricB(name_, "CheckpointsWithJudgments")

static ThemeMetric<TapNoteScore> g_MinScoreToMaintainCombo(
    "Gameplay", "MinScoreToMaintainCombo");
static const int kNumShownRadarCategories = 5;

AutoScreenMessage(SM_PlayCheer);

REGISTER_SCREEN_CLASS(ScreenEvaluation);

ScreenEvaluation::ScreenEvaluation() {
  GAMESTATE->adjust_tokens_by_song_cost_for_final_stage_check_ = false;
}

ScreenEvaluation::~ScreenEvaluation() {
  GAMESTATE->adjust_tokens_by_song_cost_for_final_stage_check_ = true;
}

void ScreenEvaluation::Init() {
  LOG->Trace("ScreenEvaluation::Init()");

  // debugging
  // Only fill StageStats with fake info if we're the InitialScreen
  // (i.e. StageStats not already filled)
  if (PREFSMAN->test_initial_screen_.Get() == name_) {
    PROFILEMAN->LoadFirstAvailableProfile(PLAYER_1);
    PROFILEMAN->LoadFirstAvailableProfile(PLAYER_2);

    STATSMAN->played_stage_stats_.clear();
    STATSMAN->played_stage_stats_.push_back(StageStats());
    StageStats& ss = STATSMAN->played_stage_stats_.back();

    GAMESTATE->play_mode_.Set(PLAY_MODE_REGULAR);
    GAMESTATE->SetCurrentStyle(
        GAMEMAN->GameAndStringToStyle(GAMEMAN->GetDefaultGame(), "versus"),
        kPlayerInvalid);
    ss.play_mode_ = GAMESTATE->play_mode_;
    ss.stage_ = Stage_1st;
    enum_add(ss.stage_, rand() % 3);
    ss.earned_extra_stage_ = (EarnedExtraStage)(rand() % NUM_EarnedExtraStage);
    GAMESTATE->SetMasterPlayerNumber(PLAYER_1);
    GAMESTATE->cur_song_.Set(SONGMAN->GetRandomSong());
    ss.played_songs_.push_back(GAMESTATE->cur_song_);
    ss.possible_songs_.push_back(GAMESTATE->cur_song_);
    GAMESTATE->cur_course_.Set(SONGMAN->GetRandomCourse());
    GAMESTATE->current_stage_index_ = 0;
    FOREACH_ENUM(PlayerNumber, p)
    GAMESTATE->player_stage_tokens_[p] = 1;

    FOREACH_PlayerNumber(p) {
      ss.player_[p].style_ = GAMESTATE->GetCurrentStyle(p);
      if (RandomInt(2)) {
        PO_GROUP_ASSIGN_N(
            GAMESTATE->player_state_[p]->player_options_, ModsLevel_Stage,
            transforms_, PlayerOptions::TRANSFORM_ECHO,
            true);  // show "disqualified"
      }
      SO_GROUP_ASSIGN(
          GAMESTATE->song_options_, ModsLevel_Stage, music_rate_, 1.1f);

      GAMESTATE->JoinPlayer(p);
      GAMESTATE->cur_steps_[p].Set(GAMESTATE->cur_song_->GetAllSteps()[0]);
      if (GAMESTATE->cur_course_) {
        std::vector<Trail*> trails;
        GAMESTATE->cur_course_->GetAllTrails(trails);
        if (trails.size()) {
          GAMESTATE->cur_trail_[p].Set(trails[0]);
        }
      }
      ss.player_[p].possible_steps_.push_back(GAMESTATE->cur_steps_[PLAYER_1]);
      ss.player_[p].steps_played_ = 1;

      PO_GROUP_ASSIGN(
          GAMESTATE->player_state_[p]->player_options_, ModsLevel_Stage,
          scroll_speed_, 2.0f);
      PO_GROUP_CALL(
          GAMESTATE->player_state_[p]->player_options_, ModsLevel_Stage,
          ChooseRandomModifiers);
    }

    for (float f = 0; f < 100.0f; f += 1.0f) {
      float p1 = std::fmod(f / 100 * 4 + .3f, 1);
      ss.player_[PLAYER_1].SetLifeRecordAt(p1, f);
      ss.player_[PLAYER_2].SetLifeRecordAt(1 - p1, f);
    }

    FOREACH_PlayerNumber(p) {
      float seconds = GAMESTATE->cur_song_->GetStepsSeconds();
      ss.player_[p].actual_dance_points_ = RandomInt(3);
      ss.player_[p].possible_dance_points_ = 2;
      if (RandomInt(2)) {
        ss.player_[p].cur_combo_ = RandomInt(15000);
      } else {
        ss.player_[p].cur_combo_ = 0;
      }
      ss.player_[p].UpdateComboList(0, true);

      ss.player_[p].cur_combo_ += 50;
      ss.player_[p].UpdateComboList(0.10f * seconds, false);

      ss.player_[p].cur_combo_ = 0;
      ss.player_[p].UpdateComboList(0.15f * seconds, false);
      ss.player_[p].cur_combo_ = 1;
      ss.player_[p].UpdateComboList(0.25f * seconds, false);
      ss.player_[p].cur_combo_ = 50;
      ss.player_[p].UpdateComboList(0.35f * seconds, false);
      ss.player_[p].cur_combo_ = 0;
      ss.player_[p].UpdateComboList(0.45f * seconds, false);
      ss.player_[p].cur_combo_ = 1;
      ss.player_[p].UpdateComboList(0.50f * seconds, false);
      ss.player_[p].cur_combo_ = 100;
      ss.player_[p].UpdateComboList(1.00f * seconds, false);
      if (RandomInt(5) == 0) {
        ss.player_[p].failed_ = true;
      }
      ss.player_[p].tap_note_scores_[TNS_W1] = RandomInt(3);
      ss.player_[p].tap_note_scores_[TNS_W2] = RandomInt(3);
      ss.player_[p].tap_note_scores_[TNS_W3] = RandomInt(3);
      ss.player_[p].possible_grade_points_ =
          4 * ScoreKeeperNormal::TapNoteScoreToGradePoints(TNS_W1, false);
      ss.player_[p].life_remaining_seconds_ = randomf(90, 580);
      ss.player_[p].score_ = rand() % (900 * 1000 * 1000);
      ss.player_[p].personal_high_score_index_ = (rand() % 3) - 1;
      ss.player_[p].machine_high_score_index_ = (rand() % 3) - 1;
      ss.player_[p].peak_combo_award_ =
          (PeakComboAward)(rand() % NUM_PeakComboAward);
      ss.player_[p].stage_award_ = (StageAward)(rand() % NUM_StageAward);

      FOREACH_ENUM(RadarCategory, rc) {
        switch (rc) {
          case RadarCategory_Stream:
          case RadarCategory_Voltage:
          case RadarCategory_Air:
          case RadarCategory_Freeze:
          case RadarCategory_Chaos:
            ss.player_[p].radar_possible_[rc] = randomf(0, 1);
            ss.player_[p].radar_actual_[rc] =
                randomf(0, ss.player_[p].radar_possible_[rc]);
            break;
          case RadarCategory_TapsAndHolds:
          case RadarCategory_Jumps:
          case RadarCategory_Holds:
          case RadarCategory_Mines:
          case RadarCategory_Hands:
          case RadarCategory_Rolls:
          case RadarCategory_Lifts:
          case RadarCategory_Fakes:
            ss.player_[p].radar_possible_[rc] = float(1 + (rand() % 200));
            ss.player_[p].radar_actual_[rc] =
                float(rand() % (int)(ss.player_[p].radar_possible_[rc]));
            break;
          default:
            break;
        }

        ;  // filled in by ScreenGameplay on start of notes
      }
    }
  }

  if (STATSMAN->played_stage_stats_.empty()) {
    LuaHelpers::ReportScriptError(
        "PlayerStageStats is empty!  Do not use SM_GoToNextScreen on "
        "ScreenGameplay, use SM_DoNextScreen instead so that ScreenGameplay "
        "can clean up properly.");
    STATSMAN->played_stage_stats_.push_back(STATSMAN->cur_stage_stats_);
  }
  stage_stats_ = &STATSMAN->played_stage_stats_.back();

  ZERO(saved_screenshot_);

  // Figure out which statistics and songs we're going to display
  SUMMARY.Load(name_, "Summary");
  if (SUMMARY) {
    STATSMAN->GetFinalEvalStageStats(final_eval_stage_stats_);
    stage_stats_ = &final_eval_stage_stats_;
  }

  // update persistent statistics
  if (SUMMARY) {
    stage_stats_->FinalizeScores(true);
  }

  // Run this here, so STATSMAN->m_CurStageStats is available to overlays.
  ScreenWithMenuElements::Init();

  // Calculate grades
  Grade grade[kNumPlayers];

  FOREACH_PlayerNumber(p) {
    if (GAMESTATE->IsPlayerEnabled(p)) {
      grade[p] = stage_stats_->player_[p].GetGrade();
    } else {
      grade[p] = Grade_Failed;
    }
  }

  // load sounds
  sound_start_.Load(THEME->GetPathS(name_, "start"));

  // init banner area
  if (SHOW_BANNER_AREA) {
    if (SUMMARY) {
      for (size_t i = 0;
           i < stage_stats_->played_songs_.size() && i < kMaxSongsToShow; i++) {
        Song* song = stage_stats_->played_songs_[i];

        small_banner_[i].LoadFromSong(song);
        small_banner_[i].ScaleToClipped(BANNER_WIDTH, BANNER_HEIGHT);
        small_banner_[i].SetName(ssprintf("SmallBanner%u", (unsigned)i + 1));
        ActorUtil::LoadAllCommands(small_banner_[i], name_);
        SET_XY(small_banner_[i]);
        this->AddChild(&small_banner_[i]);

        spr_small_banner_frame_[i].Load(THEME->GetPathG(name_, "BannerFrame"));
        spr_small_banner_frame_[i]->SetName(
            ssprintf("SmallBanner%u", (unsigned)i + 1));
        ActorUtil::LoadAllCommands(*spr_small_banner_frame_[i], name_);
        SET_XY(spr_small_banner_frame_[i]);
        this->AddChild(spr_small_banner_frame_[i]);
      }
    } else {
      if (GAMESTATE->IsCourseMode()) {
        large_banner_.LoadFromCourse(GAMESTATE->cur_course_);
      } else {
        large_banner_.LoadFromSong(GAMESTATE->cur_song_);
      }
      large_banner_.ScaleToClipped(BANNER_WIDTH, BANNER_HEIGHT);
      large_banner_.SetName("LargeBanner");
      ActorUtil::LoadAllCommands(large_banner_, name_);
      SET_XY(large_banner_);
      this->AddChild(&large_banner_);

      spr_large_banner_frame_.Load(THEME->GetPathG(name_, "BannerFrame"));
      spr_large_banner_frame_->SetName("LargeBannerFrame");
      ActorUtil::LoadAllCommands(*spr_large_banner_frame_, name_);
      SET_XY(spr_large_banner_frame_);
      this->AddChild(spr_large_banner_frame_);
    }
  }

  {
    if (!SUMMARY) {
      FOREACH_EnabledPlayer(p) {
        text_player_options_[p].LoadFromFont(
            THEME->GetPathF(name_, "PlayerOptions"));
        text_player_options_[p].SetName(ssprintf("PlayerOptionsP%d", p + 1));
        ActorUtil::LoadAllCommands(text_player_options_[p], name_);
        SET_XY(text_player_options_[p]);
        std::vector<std::string> v;
        PlayerOptions po =
            GAMESTATE->player_state_[p]->player_options_.GetPreferred();
        if (PLAYER_OPTIONS_HIDE_FAIL_TYPE) {
          po.fail_type_ = (FailType)0;  // blank out the fail type so that it
                                        // won't show in the mods list
        }
        po.GetLocalizedMods(v);
        std::string s_po = join(PLAYER_OPTIONS_SEPARATOR, v);
        text_player_options_[p].SetText(s_po);
        this->AddChild(&text_player_options_[p]);
      }

      {
        text_song_options_.LoadFromFont(THEME->GetPathF(name_, "SongOptions"));
        text_song_options_.SetName("SongOptions");
        ActorUtil::LoadAllCommands(text_song_options_, name_);
        SET_XY(text_song_options_);
        text_song_options_.SetText(
            GAMESTATE->song_options_.GetStage().GetLocalizedString());
        this->AddChild(&text_song_options_);
      }
    }

    // Dairy Queen'd (disqualified)
    FOREACH_EnabledPlayer(p) {
      spr_disqualified_[p].Load(THEME->GetPathG(name_, "Disqualified"));
      spr_disqualified_[p]->SetName(ssprintf("DisqualifiedP%d", p + 1));
      LOAD_ALL_COMMANDS_AND_SET_XY(spr_disqualified_[p]);
      spr_disqualified_[p]->SetVisible(stage_stats_->player_[p].disqualified_);
      this->AddChild(spr_disqualified_[p]);
    }
  }

  // init grade area
  if (SHOW_GRADE_AREA) {
    FOREACH_EnabledPlayer(p) {
      spr_grade_frame_[p].Load(
          THEME->GetPathG(name_, ssprintf("GradeFrame p%d", p + 1)));
      spr_grade_frame_[p]->SetName(ssprintf("GradeFrameP%d", p + 1));
      ActorUtil::LoadAllCommands(*spr_grade_frame_[p], name_);
      SET_XY(spr_grade_frame_[p]);
      this->AddChild(spr_grade_frame_[p]);

      // TODO: Re-add scrolling grade functionality
      grades_[p].Load("GradeDisplayEval");
      grades_[p].SetGrade(grade[p]);
      grades_[p].SetName(ssprintf("GradeP%d", p + 1));
      ActorUtil::LoadAllCommands(grades_[p], name_);
      SET_XY(grades_[p]);
      this->AddChild(&grades_[p]);
    }
  }

  // init points area
  if (SHOW_POINTS_AREA) {
    FOREACH_EnabledPlayer(p) {
      spr_percent_frame_[p].Load(
          THEME->GetPathG(name_, ssprintf("PercentFrame p%d", p + 1)));
      spr_percent_frame_[p]->SetName(ssprintf("PercentFrameP%d", p + 1));
      ActorUtil::LoadAllCommands(*spr_percent_frame_[p], name_);
      SET_XY(spr_percent_frame_[p]);
      this->AddChild(spr_percent_frame_[p]);

      /* Use "ScreenEvaluation Percent" in the [metrics], but position and
       * tween it with "PercentP1X", etc. */
      percent_[p].SetName(ssprintf("PercentP%d", p + 1));
      percent_[p].Load(
          GAMESTATE->player_state_[p], &stage_stats_->player_[p],
          "ScreenEvaluation Percent", true);
      ActorUtil::LoadAllCommands(percent_[p], name_);
      SET_XY(percent_[p]);
      this->AddChild(&percent_[p]);
    }
  }

  // init bonus area
  if (SHOW_BONUS_AREA) {
    // In course mode, we need to make sure the bar doesn't overflow. -aj
    float divider = 1.0f;
    if (GAMESTATE->IsCourseMode()) {
      divider = divider / GAMESTATE->cur_course_->entries_.size();
    }

    FOREACH_EnabledPlayer(p) {
      spr_bonus_frame_[p].Load(
          THEME->GetPathG(name_, ssprintf("BonusFrame p%d", p + 1)));
      spr_bonus_frame_[p]->SetName(ssprintf("BonusFrameP%d", p + 1));
      ActorUtil::LoadAllCommands(*spr_bonus_frame_[p], name_);
      SET_XY(spr_bonus_frame_[p]);
      this->AddChild(spr_bonus_frame_[p]);

      // todo: convert this to use category names instead of numbers? -aj
      for (int r = 0; r < kNumShownRadarCategories; r++)  // foreach line
      {
        float possible = stage_stats_->player_[p].radar_possible_[r];
        float actual = stage_stats_->player_[p].radar_actual_[r];
        if (possible > 1.0) {
          actual /= possible;
          possible /= possible;
        }
        spr_possible_bar_[p][r].Load(
            THEME->GetPathG(name_, ssprintf("BarPossible p%d", p + 1)));
        spr_possible_bar_[p][r].SetWidth(
            spr_possible_bar_[p][r].GetUnzoomedWidth() * possible * divider);
        spr_possible_bar_[p][r].SetName(
            ssprintf("BarPossible%dP%d", r + 1, p + 1));
        ActorUtil::LoadAllCommands(spr_possible_bar_[p][r], name_);
        SET_XY(spr_possible_bar_[p][r]);
        this->AddChild(&spr_possible_bar_[p][r]);

        spr_actual_bar_[p][r].Load(
            THEME->GetPathG(name_, ssprintf("BarActual p%d", p + 1)));
        // should be out of the possible bar, not actual (whatever value that is
        // at)
        spr_actual_bar_[p][r].SetWidth(
            spr_possible_bar_[p][r].GetUnzoomedWidth() * actual * divider);

        float value = (float)100 * spr_actual_bar_[p][r].GetUnzoomedWidth() /
                      spr_possible_bar_[p][r].GetUnzoomedWidth();
        LOG->Trace("Radar bar %d of 5 - %f percent", r, value);

        spr_actual_bar_[p][r].SetName(ssprintf("BarActual%dP%d", r + 1, p + 1));
        ActorUtil::LoadAllCommands(spr_actual_bar_[p][r], name_);
        SET_XY(spr_actual_bar_[p][r]);

        // .99999 is fairly close to 1.00, so we use that.
        // todo: allow extra commands for AAA/AAAA? -aj
        if (actual > 0.99999f) {
          spr_actual_bar_[p][r].RunCommands(BAR_ACTUAL_MAX_COMMAND);
        }
        this->AddChild(&spr_actual_bar_[p][r]);
      }
    }
  }

  // init survived area
  if (SHOW_SURVIVED_AREA) {
    FOREACH_EnabledPlayer(p) {
      spr_survived_frame_[p].Load(
          THEME->GetPathG(name_, ssprintf("SurvivedFrame p%d", p + 1)));
      spr_survived_frame_[p]->SetName(ssprintf("SurvivedFrameP%d", p + 1));
      ActorUtil::LoadAllCommands(*spr_survived_frame_[p], name_);
      SET_XY(spr_survived_frame_[p]);
      this->AddChild(spr_survived_frame_[p]);

      text_survived_number_[p].LoadFromFont(
          THEME->GetPathF(name_, "SurvivedNumber"));
      // curewater: edited the "# stages cleared" text so it deducts one if you
      // failed. Should be accurate, but I'm not sure if its "standard" that
      // (bool)true = 1.  (assumption)
      text_survived_number_[p].SetText(
          ssprintf("%02d", stage_stats_->player_[p].songs_passed_));
      text_survived_number_[p].SetName(ssprintf("SurvivedNumberP%d", p + 1));
      ActorUtil::LoadAllCommands(text_survived_number_[p], name_);
      SET_XY(text_survived_number_[p]);
      this->AddChild(&text_survived_number_[p]);
    }
  }

  // init win area
  if (SHOW_WIN_AREA) {
    FOREACH_EnabledPlayer(p) {
      spr_win_frame_[p].Load(
          THEME->GetPathG(name_, ssprintf("win frame p%d", p + 1)));
      spr_win_frame_[p]->SetName(ssprintf("WinFrameP%d", p + 1));
      ActorUtil::LoadAllCommands(*spr_win_frame_[p], name_);
      SET_XY(spr_win_frame_[p]);
      this->AddChild(spr_win_frame_[p]);

      spr_win_[p].Load(THEME->GetPathG(name_, ssprintf("win p%d 1x3", p + 1)));
      spr_win_[p].StopAnimating();
      int frame = GAMESTATE->GetStageResult(p);
      spr_win_[p].SetState(frame);
      spr_win_[p].SetName(ssprintf("WinP%d", p + 1));
      ActorUtil::LoadAllCommands(spr_win_[p], name_);
      SET_XY(spr_win_[p]);
      this->AddChild(&spr_win_[p]);
    }
  }

  // init judgment area
  ROLLING_NUMBERS_CLASS.Load(name_, "RollingNumbersClass");
  ROLLING_NUMBERS_MAX_COMBO_CLASS.Load(name_, "RollingNumbersMaxComboClass");
  FOREACH_ENUM(JudgmentLine, l) {
    if (l == JudgmentLine_W1 && !GAMESTATE->ShowW1()) {
      continue;  // skip
    }

    if (SHOW_JUDGMENT_LINE(l)) {
      if (SHOW_SHARED_JUDGMENT_LINE_LABELS) {
        LuaThreadVariable var2("JudgmentLine", LuaReference::Create(l));
        spr_shared_judgment_line_labels_[l].Load(
            THEME->GetPathG(name_, "JudgmentLabel " + JudgmentLineToString(l)));
        spr_shared_judgment_line_labels_[l]->SetName(
            JudgmentLineToString(l) + "Label");
        ActorUtil::LoadAllCommands(spr_shared_judgment_line_labels_[l], name_);
        SET_XY(spr_shared_judgment_line_labels_[l]);
        this->AddChild(spr_shared_judgment_line_labels_[l]);
      }

      FOREACH_EnabledPlayer(p) {
        text_judgment_line_number_[l][p].LoadFromFont(
            THEME->GetPathF(name_, "JudgmentLineNumber"));
        text_judgment_line_number_[l][p].SetName(
            JudgmentLineToString(l) + ssprintf("NumberP%d", p + 1));
        if (JudgmentLineToString(l) == "MaxCombo") {
          text_judgment_line_number_[l][p].Load(
              ROLLING_NUMBERS_MAX_COMBO_CLASS);
        } else {
          text_judgment_line_number_[l][p].Load(ROLLING_NUMBERS_CLASS);
        }
        ActorUtil::LoadAllCommands(text_judgment_line_number_[l][p], name_);
        SET_XY(text_judgment_line_number_[l][p]);
        this->AddChild(&text_judgment_line_number_[l][p]);

        int i_value;
        switch (l) {
          /* xxx: This doesn't seem to handle checkpoints correctly.
           * Something about checkpoints needing to be tied into
           * the correct judgments instead of just W1/W2. Misses are ok. */
          case JudgmentLine_W1:
            i_value = stage_stats_->player_[p].tap_note_scores_[TNS_W1];
            if (CHECKPOINTS_WITH_JUDGMENTS && GAMESTATE->ShowW1()) {
              i_value +=
                  stage_stats_->player_[p].tap_note_scores_[TNS_CheckpointHit];
            }
            break;
          case JudgmentLine_W2:
            i_value = stage_stats_->player_[p].tap_note_scores_[TNS_W2];
            if (CHECKPOINTS_WITH_JUDGMENTS && !GAMESTATE->ShowW1()) {
              i_value +=
                  stage_stats_->player_[p].tap_note_scores_[TNS_CheckpointHit];
            }
            break;
          case JudgmentLine_W3:
            i_value = stage_stats_->player_[p].tap_note_scores_[TNS_W3];
            break;
          case JudgmentLine_W4:
            i_value = stage_stats_->player_[p].tap_note_scores_[TNS_W4];
            break;
          case JudgmentLine_W5:
            i_value = stage_stats_->player_[p].tap_note_scores_[TNS_W5];
            break;
          case JudgmentLine_Miss:
            i_value = stage_stats_->player_[p].tap_note_scores_[TNS_Miss];
            if (CHECKPOINTS_WITH_JUDGMENTS) {
              i_value +=
                  stage_stats_->player_[p].tap_note_scores_[TNS_CheckpointMiss];
            }
            break;
          case JudgmentLine_Held:
            i_value = stage_stats_->player_[p].hold_note_scores_[HNS_Held];
            break;
          case JudgmentLine_MaxCombo:
            i_value = stage_stats_->player_[p].GetMaxCombo().cnt_;
            break;
            DEFAULT_FAIL(l);
        }

        text_judgment_line_number_[l][p].SetTargetNumber(float(i_value));
      }
    }
  }

  // init detail area
  if (SHOW_DETAIL_AREA) {
    FOREACH_EnabledPlayer(p) {
      spr_detail_frame_[p].Load(
          THEME->GetPathG(name_, ssprintf("DetailFrame p%d", p + 1)));
      spr_detail_frame_[p]->SetName(ssprintf("DetailFrameP%d", p + 1));
      ActorUtil::LoadAllCommands(*spr_detail_frame_[p], name_);
      SET_XY(spr_detail_frame_[p]);
      this->AddChild(spr_detail_frame_[p]);
    }

    FOREACH_ENUM(DetailLine, l) {
      FOREACH_EnabledPlayer(p) {
        text_detail_text_[l][p].LoadFromFont(
            THEME->GetPathF(name_, "DetailLineNumber"));
        text_detail_text_[l][p].SetName(
            DetailLineToString(l) + ssprintf("NumberP%d", p + 1));
        ActorUtil::LoadAllCommands(text_detail_text_[l][p], name_);
        SET_XY(text_detail_text_[l][p]);
        this->AddChild(&text_detail_text_[l][p]);

        static const int kIndices[NUM_DetailLine] = {
            RadarCategory_TapsAndHolds, RadarCategory_Jumps,
            RadarCategory_Holds,        RadarCategory_Mines,
            RadarCategory_Hands,        RadarCategory_Rolls,
            RadarCategory_Lifts,        RadarCategory_Fakes};
        const int ind = kIndices[l];
        const int i_actual =
            std::lrint(stage_stats_->player_[p].radar_actual_[ind]);
        const int i_possible =
            std::lrint(stage_stats_->player_[p].radar_possible_[ind]);

        // todo: check if format string is valid
        // (two integer values in DETAILLINE_FORMAT) -aj
        text_detail_text_[l][p].SetText(
            ssprintf(DETAILLINE_FORMAT.c_str(), i_actual, i_possible));
      }
    }
  }

  // init score area
  if (SHOW_SCORE_AREA) {
    spr_score_label_.Load(THEME->GetPathG(name_, "ScoreLabel"));
    spr_score_label_->SetName("ScoreLabel");
    ActorUtil::LoadAllCommands(*spr_score_label_, name_);
    SET_XY(spr_score_label_);
    this->AddChild(spr_score_label_);

    FOREACH_EnabledPlayer(p) {
      text_score_[p].LoadFromFont(THEME->GetPathF(name_, "ScoreNumber"));
      text_score_[p].SetName(ssprintf("ScoreNumberP%d", p + 1));
      text_score_[p].Load("RollingNumbersEvaluation");
      ActorUtil::LoadAllCommands(text_score_[p], name_);
      SET_XY(text_score_[p]);
      text_score_[p].SetTargetNumber(float(stage_stats_->player_[p].score_));
      this->AddChild(&text_score_[p]);
    }
  }

  // init time area
  if (SHOW_TIME_AREA) {
    spr_time_label_.Load(THEME->GetPathG(name_, "TimeLabel"));
    spr_time_label_->SetName("TimeLabel");
    ActorUtil::LoadAllCommands(*spr_time_label_, name_);
    SET_XY(spr_time_label_);
    this->AddChild(spr_time_label_);

    FOREACH_EnabledPlayer(p) {
      text_time_[p].LoadFromFont(THEME->GetPathF(name_, "time"));
      text_time_[p].SetShadowLength(0);
      text_time_[p].SetName(ssprintf("TimeNumberP%d", p + 1));
      ActorUtil::LoadAllCommands(text_time_[p], name_);
      SET_XY(text_time_[p]);
      text_time_[p].SetText(
          SecondsToMMSSMsMs(stage_stats_->player_[p].alive_seconds_));
      this->AddChild(&text_time_[p]);
    }
  }

  // init records area
  bool one_has_new_top_record = false;
  bool one_has_full_w1_combo = false;
  bool one_has_full_w2_combo = false;
  bool one_has_full_w3_combo = false;
  bool one_has_full_w4_combo = false;

  FOREACH_PlayerNumber(p) {
    if (GAMESTATE->IsPlayerEnabled(p)) {
      if ((stage_stats_->player_[p].machine_high_score_index_ == 0 ||
           stage_stats_->player_[p].personal_high_score_index_ == 0)) {
        one_has_new_top_record = true;
      }

      if (stage_stats_->player_[p].FullComboOfScore(TNS_W4)) {
        one_has_full_w4_combo = true;
      }

      if (stage_stats_->player_[p].FullComboOfScore(TNS_W3)) {
        one_has_full_w3_combo = true;
      }

      if (stage_stats_->player_[p].FullComboOfScore(TNS_W2)) {
        one_has_full_w2_combo = true;
      }

      if (stage_stats_->player_[p].FullComboOfScore(TNS_W1)) {
        one_has_full_w1_combo = true;
      }
    }
  }

  Grade best_grade = Grade_NoData;
  FOREACH_PlayerNumber(p) best_grade = std::min(best_grade, grade[p]);

  if (stage_stats_->earned_extra_stage_ != EarnedExtraStage_No) {
    SOUND->PlayOnce(THEME->GetPathS(
        name_,
        "try " + EarnedExtraStageToString(stage_stats_->earned_extra_stage_)));
  } else if (
      one_has_new_top_record &&
      ANNOUNCER->HasSoundsFor("evaluation new record")) {
    SOUND->PlayOnceFromDir(ANNOUNCER->GetPathTo("evaluation new record"));
  } else if (one_has_full_w4_combo && g_MinScoreToMaintainCombo == TNS_W4) {
    SOUND->PlayOnceFromDir(ANNOUNCER->GetPathTo("evaluation full combo W4"));
  } else if ((one_has_full_w1_combo || one_has_full_w2_combo ||
              one_has_full_w3_combo)) {
    std::string combo_type =
        one_has_full_w1_combo ? "W1" : (one_has_full_w2_combo ? "W2" : "W3");
    SOUND->PlayOnceFromDir(
        ANNOUNCER->GetPathTo("evaluation full combo " + combo_type));
  } else {
    if (SUMMARY || GAMESTATE->IsCourseMode()) {
      SOUND->PlayOnceFromDir(ANNOUNCER->GetPathTo(
          "evaluation final " + GradeToOldString(best_grade)));
    } else {
      switch (GAMESTATE->play_mode_) {
        case PLAY_MODE_BATTLE: {
          bool won = GAMESTATE->GetStageResult(
                         GAMESTATE->GetMasterPlayerNumber()) == RESULT_WIN;
          std::string result = won ? "win" : "lose";
          SOUND->PlayOnceFromDir(ANNOUNCER->GetPathTo("evaluation " + result));
        } break;
        default:
          SOUND->PlayOnceFromDir(ANNOUNCER->GetPathTo(
              "evaluation " + GradeToOldString(best_grade)));
          break;
      }
    }
  }

  switch (GradeToOldGrade(best_grade)) {
    case Grade_Tier01:
    case Grade_Tier02:
    case Grade_Tier03:
      this->PostScreenMessage(SM_PlayCheer, CHEER_DELAY_SECONDS);
    default:
      break;
  }
}

bool ScreenEvaluation::Input(const InputEventPlus& input) {
  if (IsTransitioning()) {
    return false;
  }

  if (input.menu_i_ == GAME_BUTTON_RESTART && input.type_ == IET_FIRST_PRESS &&
      GAMESTATE->IsEventMode() && !GAMESTATE->IsCourseMode()) {
    return MenuRestart(input);
  }

  if (input.game_i_.IsValid()) {
    if (CodeDetector::EnteredCode(
            input.game_i_.controller_, CODE_SAVE_SCREENSHOT1) ||
        CodeDetector::EnteredCode(
            input.game_i_.controller_, CODE_SAVE_SCREENSHOT2)) {
      PlayerNumber pn = input.pn_;
      if (!saved_screenshot_[pn] &&  // only allow one screenshot
          PROFILEMAN->IsPersistentProfile(pn)) {
        if (PROFILEMAN->ProfileWasLoadedFromMemoryCard(pn)) {
          MEMCARDMAN->MountCard(pn);
        }

        Profile* profile = PROFILEMAN->GetProfile(pn);
        std::string dir =
            PROFILEMAN->GetProfileDir((ProfileSlot)pn) + "Screenshots/";
        std::string file_name =
            StepMania::SaveScreenshot(dir, true, true, "", "");

        if (!file_name.empty()) {
          std::string path = dir + file_name;

          const HighScore& hs = stage_stats_->player_[pn].high_score_;
          Screenshot screenshot;
          screenshot.file_name_ = file_name;
          screenshot.md5_ = BinaryToHex(CRYPTMAN->GetMD5ForFile(path));
          screenshot.high_score_ = hs;
          profile->AddScreenshot(screenshot);
        }

        if (PROFILEMAN->ProfileWasLoadedFromMemoryCard(pn)) {
          MEMCARDMAN->UnmountCard(pn);
        }

        saved_screenshot_[pn] = true;
        return true;  // handled
      }
    }
  }

  return ScreenWithMenuElements::Input(input);
}

void ScreenEvaluation::HandleScreenMessage(const ScreenMessage sm) {
  if (sm == SM_PlayCheer) {
    SOUND->PlayOnceFromDir(ANNOUNCER->GetPathTo("evaluation cheer"));
  }

  ScreenWithMenuElements::HandleScreenMessage(sm);
}

bool ScreenEvaluation::MenuBack(const InputEventPlus& input) {
  return MenuStart(input);
}

bool ScreenEvaluation::MenuStart(const InputEventPlus& input) {
  if (IsTransitioning()) {
    return false;
  }

  sound_start_.Play(true);

  HandleMenuStart();
  return true;
}

bool ScreenEvaluation::MenuRestart(const InputEventPlus& input) {
  if (IsTransitioning()) {
    return false;
  }

  SCREENMAN->GetTopScreen()->SetNextScreenName("ScreenGameplay");
  StartTransitioningScreen(SM_GoToNextScreen);
  return true;
}

void ScreenEvaluation::HandleMenuStart() {
  StartTransitioningScreen(SM_GoToNextScreen);
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the ScreenEvaluation. */
class LunaScreenEvaluation : public Luna<ScreenEvaluation> {
 public:
  static int GetStageStats(T* p, lua_State* L) {
    LuaHelpers::Push(L, p->GetStageStats());
    return 1;
  }
  LunaScreenEvaluation() { ADD_METHOD(GetStageStats); }
};

LUA_REGISTER_DERIVED_CLASS(ScreenEvaluation, ScreenWithMenuElements)

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
