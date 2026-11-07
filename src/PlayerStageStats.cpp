#include "PlayerStageStats.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <deque>
#include <map>
#include <numeric>
#include <string>

#include "CommonMetrics.h"
#include "Course.h"
#include "Difficulty.h"
#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "GameState.h"
#include "Grade.h"
#include "HighScore.h"
#include "LuaManager.h"
#include "MessageManager.h"
#include "PlayerNumber.h"
#include "PrefsManager.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "ScoreKeeperNormal.h"
#include "Steps.h"
#include "ThemeManager.h"
#include "ThemeMetric.h"
#include "global.h"

#define GRADE_PERCENT_TIER(i) \
  THEME->GetMetricF(          \
      "PlayerStageStats",     \
      ssprintf("GradePercent%s", GradeToString((Grade)i).c_str()))
// deprecated, but no solution to replace them exists yet:
#define GRADE_TIER02_IS_ALL_W2S \
  THEME->GetMetricB("PlayerStageStats", "GradeTier02IsAllW2s")
#define GRADE_TIER01_IS_ALL_W2S \
  THEME->GetMetricB("PlayerStageStats", "GradeTier01IsAllW2s")
#define GRADE_TIER02_IS_FULL_COMBO \
  THEME->GetMetricB("PlayerStageStats", "GradeTier02IsFullCombo")

static ThemeMetric<TapNoteScore> g_MinScoreToMaintainCombo(
    "Gameplay", "MinScoreToMaintainCombo");
static ThemeMetric<bool> g_MineHitIncrementsMissCombo(
    "Gameplay", "MineHitIncrementsMissCombo");

const float LESSON_PASS_THRESHOLD = 0.8f;

Grade GetGradeFromPercent(float percent);

void PlayerStageStats::InternalInit() {
  style_ = nullptr;
  player_number_ = PLAYER_1;

  player_can_achieve_full_combo_ = true;
  joined_ = false;
  possible_steps_.clear();
  steps_played_ = 0;
  alive_seconds_ = 0;
  failed_ = false;
  possible_dance_points_ = 0;
  cur_possible_dance_points_ = 0;
  actual_dance_points_ = 0;
  possible_grade_points_ = 0;
  cur_combo_ = 0;
  max_combo_ = 0;
  cur_miss_combo_ = 0;
  cur_score_multiplier_ = 1;
  score_ = 0;
  max_score_ = 0;
  cur_max_score_ = 0;
  songs_passed_ = 0;
  songs_played_ = 0;
  life_remaining_seconds_ = 0;
  num_controller_steps_ = 0;
  calories_burned_ = 0;

  ZERO(tap_note_scores_);
  ZERO(hold_note_scores_);
  radar_possible_.Zero();
  radar_actual_.Zero();

  first_second_ = FLT_MAX;
  last_second_ = 0;

  stage_award_ = StageAward_Invalid;
  peak_combo_award_ = PeakComboAward_Invalid;
  personal_high_score_index_ = -1;
  machine_high_score_index_ = -1;
  disqualified_ = false;
  rc_ = RankingCategory_Invalid;
  high_score_ = HighScore();
}

void PlayerStageStats::Init(PlayerNumber pn) { player_number_ = pn; }

void PlayerStageStats::AddStats(const PlayerStageStats& other) {
  style_ = other.style_;
  joined_ = other.joined_;
  for (Steps* s : other.possible_steps_) {
    possible_steps_.push_back(s);
  }
  steps_played_ += other.steps_played_;
  alive_seconds_ += other.alive_seconds_;
  failed_ |= other.failed_;
  possible_dance_points_ += other.possible_dance_points_;
  actual_dance_points_ += other.actual_dance_points_;
  cur_possible_dance_points_ += other.cur_possible_dance_points_;
  possible_grade_points_ += other.possible_grade_points_;

  for (int t = 0; t < NUM_TapNoteScore; t++) {
    tap_note_scores_[t] += other.tap_note_scores_[t];
  }
  for (int h = 0; h < NUM_HoldNoteScore; h++) {
    hold_note_scores_[h] += other.hold_note_scores_[h];
  }
  cur_combo_ += other.cur_combo_;
  max_combo_ += other.max_combo_;
  cur_miss_combo_ += other.cur_miss_combo_;
  score_ += other.score_;
  max_score_ += other.max_score_;
  cur_max_score_ += other.cur_max_score_;
  radar_possible_ += other.radar_possible_;
  radar_actual_ += other.radar_actual_;
  songs_passed_ += other.songs_passed_;
  songs_played_ += other.songs_played_;
  num_controller_steps_ += other.num_controller_steps_;
  calories_burned_ += other.calories_burned_;
  life_remaining_seconds_ = other.life_remaining_seconds_;  // don't accumulate
  disqualified_ |= other.disqualified_;

  // FirstSecond is always 0, and last second is the time of the last step,
  // so add 1 second between the stages so that the last element of this
  // stage's record isn't overwritten by the first element of the other
  // stage's record. -Kyz
  const float other_first_second = other.first_second_ + last_second_ + 1.0f;
  const float other_last_second = other.last_second_ + last_second_ + 1.0f;
  last_second_ = other_last_second;

  std::map<float, float>::const_iterator it;
  for (it = other.life_record_.begin(); it != other.life_record_.end(); ++it) {
    const float pos = it->first;
    const float life = it->second;
    life_record_[other_first_second + pos] = life;
  }

  for (unsigned i = 0; i < other.combo_list_.size(); ++i) {
    const Combo_t& combo = other.combo_list_[i];

    Combo_t newcombo(combo);
    newcombo.start_second_ += other_first_second;
    combo_list_.push_back(newcombo);
  }

  /* Merge identical combos. This normally only happens in course mode, when
   * a combo continues between songs. */
  for (unsigned i = 1; i < combo_list_.size(); ++i) {
    Combo_t& prevcombo = combo_list_[i - 1];
    Combo_t& combo = combo_list_[i];
    const float prev_combo_end =
        prevcombo.start_second_ + prevcombo.size_seconds_;
    const float this_combo_start = combo.start_second_;
    if (std::abs(prev_combo_end - this_combo_start) > 0.001) {
      continue;
    }

    // These are really the same combo.
    prevcombo.size_seconds_ += combo.size_seconds_;
    prevcombo.cnt_ += combo.cnt_;
    combo_list_.erase(combo_list_.begin() + i);
    --i;
  }
}

Grade GetGradeFromPercent(float percent) {
  Grade grade = Grade_Failed;

  FOREACH_ENUM(Grade, g) {
    if (percent >= GRADE_PERCENT_TIER(g)) {
      grade = g;
      break;
    }
  }
  return grade;
}

Grade PlayerStageStats::GetGrade() const {
  if (failed_) {
    return Grade_Failed;
  }

  /* XXX: This entire calculation should be in ScoreKeeper, but final evaluation
   * is tricky since at that point the ScoreKeepers no longer exist. */
  float actual = 0;

  bool is_beginner = false;
  if (steps_played_ > 0 && !GAMESTATE->IsCourseMode()) {
    is_beginner = possible_steps_[0]->GetDifficulty() == Difficulty_Beginner;
  }

  FOREACH_ENUM(TapNoteScore, tns) {
    int tap_score_value =
        ScoreKeeperNormal::TapNoteScoreToGradePoints(tns, is_beginner);
    actual += tap_note_scores_[tns] * tap_score_value;
    // LOG->Trace( "GetGrade actual: %i * %i", m_iTapNoteScores[tns],
    // iTapScoreValue );
  }

  FOREACH_ENUM(HoldNoteScore, hns) {
    int hold_score_value =
        ScoreKeeperNormal::HoldNoteScoreToGradePoints(hns, is_beginner);
    actual += hold_note_scores_[hns] * hold_score_value;
    // LOG->Trace( "GetGrade actual: %i * %i", m_iHoldNoteScores[hns],
    // iHoldScoreValue );
  }

  // LOG->Trace( "GetGrade: fActual: %f, fPossible: %d", fActual,
  // m_iPossibleGradePoints );

  float percent =
      (possible_grade_points_ == 0) ? 0 : actual / possible_grade_points_;

  Grade grade = GetGradeFromPercent(percent);

  // LOG->Trace( "GetGrade: Grade: %s, %i", GradeToString(grade).c_str(),
  // GRADE_TIER02_IS_ALL_W2S );

  // TODO: Change these conditions to use Lua instead. -aj
  if (GRADE_TIER02_IS_ALL_W2S) {
    if (FullComboOfScore(TNS_W1)) {
      return Grade_Tier01;
    }

    if (FullComboOfScore(TNS_W2)) {
      return Grade_Tier02;
    }

    grade = std::max(grade, Grade_Tier03);
  }

  if (GRADE_TIER01_IS_ALL_W2S) {
    if (FullComboOfScore(TNS_W2)) {
      return Grade_Tier01;
    }
    grade = std::max(grade, Grade_Tier02);
  }

  if (GRADE_TIER02_IS_FULL_COMBO) {
    if (FullComboOfScore(g_MinScoreToMaintainCombo)) {
      return Grade_Tier02;
    }
    grade = std::max(grade, Grade_Tier03);
  }

  return grade;
}

float PlayerStageStats::MakePercentScore(int actual, int possible) {
  if (possible == 0) {
    return 0;  // div/0
  }

  if (actual == possible) {
    return 1;  // correct for rounding error
  }

  // This can happen in battle, with transform attacks.
  // ASSERT_M( iActual <= iPossible, ssprintf("%i/%i", iActual, iPossible) );

  float percent = actual / (float)possible;

  // don't allow negative
  percent = std::max(0.0f, percent);

  int percent_total_digits =
      3 + CommonMetrics::PERCENT_SCORE_DECIMAL_PLACES;  // "100" + "." + "00"

  // TRICKY: printf will round, but we want to truncate. Otherwise, we may
  // display a percent score that's too high and doesn't match up with the
  // calculated grade.
  float trunc_interval = std::pow(0.1f, (float)percent_total_digits - 1);

  // TRICKY: ftruncf is rounding 1.0000000 to 0.99990004. Give a little boost
  // to fPercentDancePoints to correct for this.
  percent += 0.000001f;

  percent = ftruncf(percent, trunc_interval);
  return percent;
}

std::string PlayerStageStats::FormatPercentScore(float percent_dance_points) {
  int percent_total_digits =
      3 + CommonMetrics::PERCENT_SCORE_DECIMAL_PLACES;  // "100" + "." + "00"

  std::string s = ssprintf(
      "%*.*f%%", percent_total_digits,
      (int)CommonMetrics::PERCENT_SCORE_DECIMAL_PLACES,
      percent_dance_points * 100);
  return s;
}

float PlayerStageStats::GetPercentDancePoints() const {
  return MakePercentScore(actual_dance_points_, possible_dance_points_);
}

// TODO: Make this use lua. Let more judgments be possible. -Wolfman2000
int PlayerStageStats::GetLessonScoreActual() const {
  int score = 0;

  FOREACH_ENUM(TapNoteScore, tns) {
    switch (tns) {
      case TNS_AvoidMine:
      case TNS_W5:
      case TNS_W4:
      case TNS_W3:
      case TNS_W2:
      case TNS_W1:
        score += tap_note_scores_[tns];
      default:
        break;
    }
  }

  FOREACH_ENUM(HoldNoteScore, hns) {
    switch (hns) {
      case HNS_Held:
        score += hold_note_scores_[hns];
      default:
        break;
    }
  }

  return score;
}

int PlayerStageStats::GetLessonScoreNeeded() const {
  float score = std::accumulate(
      possible_steps_.begin(), possible_steps_.end(), 0.f,
      [](float total, const Steps* steps) {
        return total +
               steps->GetRadarValues(PLAYER_1)[RadarCategory_TapsAndHolds];
      });
  return std::lrint(score * LESSON_PASS_THRESHOLD);
}

void PlayerStageStats::ResetScoreForLesson() {
  cur_possible_dance_points_ = 0;
  actual_dance_points_ = 0;
  FOREACH_ENUM(TapNoteScore, tns)
  tap_note_scores_[tns] = 0;
  FOREACH_ENUM(HoldNoteScore, hns)
  hold_note_scores_[hns] = 0;
  cur_combo_ = 0;
  max_combo_ = 0;
  cur_miss_combo_ = 0;
  score_ = 0;
  cur_max_score_ = 0;
  max_score_ = 0;
}

void PlayerStageStats::SetLifeRecordAt(float life, float steps_second) {
  // Don't save life stats in endless courses, or could run OOM in a few hours.
  if (GAMESTATE->cur_course_ && GAMESTATE->cur_course_->IsEndless()) {
    return;
  }

  if (steps_second < 0) {
    return;
  }

  first_second_ = std::min(steps_second, first_second_);
  last_second_ = std::max(steps_second, last_second_);
  // LOG->Trace( "fLastSecond = %f", m_fLastSecond );

  // fStepsSecond will usually be greater than any value already in the map,
  // but if a tap and a hold both set the life on the same frame, it won't.
  // Check whether an entry already exists for the current time, and move it
  // back a tiny bit if it does and the new value is not the same as the old.
  // Otherwise, you get the rare bug where the life graph shows a gradual
  // decline when the lifebar was actually full up to a miss.  This occurs
  // because the first call has full life, and removes the previous full life
  // entry.  Then the second call of the frame occurs and sets the life for
  // the current time to a lower value.
  // -Kyz
  std::map<float, float>::iterator curr = life_record_.find(steps_second);
  if (curr != life_record_.end()) {
    if (curr->second != life) {
      // 2^-8
      life_record_[steps_second - 0.00390625] = curr->second;
    }
  }
  life_record_[steps_second] = life;

  Message msg(
      static_cast<MessageID>(
          Message_LifeMeterChangedP1 + Enum::to_integral(player_number_)));
  msg.SetParam("Life", life);
  msg.SetParam("StepsSecond", steps_second);
  MESSAGEMAN->Broadcast(msg);

  // Memory optimization:
  // If we have three consecutive records A, B, and C all with the same fLife,
  // we can eliminate record B without losing data. Only check the last three
  // records in the map since we're only inserting at the end, and we know all
  // earlier redundant records have already been removed.
  std::map<float, float>::iterator c = life_record_.end();
  --c;
  if (c == life_record_.begin()) {  // no earlier records left
    return;
  }

  std::map<float, float>::iterator b = c;
  --b;
  if (b == life_record_.begin()) {  // no earlier records left
    return;
  }

  std::map<float, float>::iterator a = b;
  --a;

  if (a->second == b->second && b->second == c->second) {
    life_record_.erase(b);
  }
}

float PlayerStageStats::GetLifeRecordLerpAt(float steps_second) const {
  if (life_record_.empty()) {
    return 0;
  }

  // Find the first element whose key is greater than k.
  std::map<float, float>::const_iterator later =
      life_record_.upper_bound(steps_second);

  // Find the last element whose key is less than or equal to k.
  std::map<float, float>::const_iterator earlier = later;
  if (earlier != life_record_.begin()) {
    --earlier;
  }

  if (later == life_record_.end()) {
    return earlier->second;
  }

  if (earlier->first == later->first) {  // two samples from the same time.
                                         // Don't divide by zero in SCALE
    return earlier->second;
  }

  // earlier <= pos <= later
  return SCALE(
      steps_second, earlier->first, later->first, earlier->second,
      later->second);
}

void PlayerStageStats::GetLifeRecord(
    float* life_out, int num_samples, float steps_end_second) const {
  for (int i = 0; i < num_samples; ++i) {
    float from = SCALE(i, 0, (float)num_samples, 0.0f, steps_end_second);
    life_out[i] = GetLifeRecordLerpAt(from);
  }
}

float PlayerStageStats::GetCurrentLife() const {
  if (life_record_.empty()) {
    return 0;
  }
  std::map<float, float>::const_iterator iter = life_record_.end();
  --iter;
  return iter->second;
}

/* If bRollover is true, we're being called before gameplay begins, so we can
 * record the amount of the first combo that comes from the previous song. */
void PlayerStageStats::UpdateComboList(float second, bool rollover) {
  // Don't save combo stats in endless courses, or could run OOM in a few hours.
  if (GAMESTATE->cur_course_ && GAMESTATE->cur_course_->IsEndless()) {
    return;
  }

  if (second < 0) {
    return;
  }

  if (!rollover) {
    first_second_ = std::min(second, first_second_);
    last_second_ = std::max(second, last_second_);
    // LOG->Trace( "fLastSecond = %f", fLastSecond );
  }

  int cnt = cur_combo_;
  if (!cnt) {
    return;  // no combo
  }

  if (combo_list_.size() == 0 || combo_list_.back().cnt_ >= cnt) {
    /* If the previous combo (if any) starts on -9999, then we rolled over
     * some combo, but missed the first step. Remove it. */
    if (combo_list_.size() && combo_list_.back().start_second_ == -9999) {
      combo_list_.erase(
          combo_list_.begin() + combo_list_.size() - 1, combo_list_.end());
    }

    // This is a new combo.
    Combo_t new_combo;
    /* "start" is the position that the combo started within this song.
     * If we're recording rollover, the combo hasn't started yet (within
     * this song), so put a placeholder in and set it on the next call.
     * (Otherwise, start will be less than fFirstPos.) */
    if (rollover) {
      new_combo.start_second_ = -9999;
    } else {
      new_combo.start_second_ = second;
    }
    combo_list_.push_back(new_combo);
  }

  Combo_t& combo = combo_list_.back();
  if (!rollover && combo.start_second_ == -9999) {
    combo.start_second_ = 0;
  }

  combo.size_seconds_ = second - combo.start_second_;
  combo.cnt_ = cnt;

  if (rollover) {
    combo.rollover_ = cnt;
  }
}

/* This returns the largest combo contained within the song, as if
 * m_bComboContinuesBetweenSongs is turned off. */
PlayerStageStats::Combo_t PlayerStageStats::GetMaxCombo() const {
  if (combo_list_.size() == 0) {
    return Combo_t();
  }

  int m = 0;
  for (unsigned i = 1; i < combo_list_.size(); ++i) {
    if (combo_list_[i].cnt_ > combo_list_[m].cnt_) {
      m = i;
    }
  }

  return combo_list_[m];
}

int PlayerStageStats::GetComboAtStartOfStage() const {
  if (combo_list_.empty()) {
    return 0;
  } else {
    return combo_list_[0].rollover_;
  }
}

bool PlayerStageStats::FullComboOfScore(
    TapNoteScore all_greater_or_equal) const {
  ASSERT(all_greater_or_equal >= TNS_W5);
  ASSERT(all_greater_or_equal <= TNS_W1);

  // if we've set MissCombo to anything besides 0, it's not a full combo
  if (!player_can_achieve_full_combo_) {
    return false;
  }

  // If missed any holds, then it's not a full combo
  if (hold_note_scores_[HNS_LetGo] > 0) {
    return false;
  }

  // if any checkpoints were missed, it's not a full combo	either
  if (tap_note_scores_[TNS_CheckpointMiss] > 0) {
    return false;
  }

  // If has any of the judgments below, then not a full combo
  for (int i = TNS_Miss; i < all_greater_or_equal; i++) {
    if (tap_note_scores_[i] > 0) {
      return false;
    }
  }

  // hit any mines when they increment the miss combo? It's not a full combo.
  if (g_MineHitIncrementsMissCombo && tap_note_scores_[TNS_HitMine] > 0) {
    return false;
  }

  // If has at least one of the judgments equal to or above, then is a full
  // combo.
  for (int i = all_greater_or_equal; i < NUM_TapNoteScore; i++) {
    if (tap_note_scores_[i] > 0) {
      return true;
    }
  }

  return false;
}

TapNoteScore PlayerStageStats::GetBestFullComboTapNoteScore() const {
  // Optimization opportunity: ...
  // (seriously? -aj)
  for (TapNoteScore i = TNS_W1; i >= TNS_W5; enum_add(i, -1)) {
    if (FullComboOfScore(i)) {
      return i;
    }
  }
  return TapNoteScore_Invalid;
}

bool PlayerStageStats::SingleDigitsOfScore(
    TapNoteScore all_greater_or_equal) const {
  return FullComboOfScore(all_greater_or_equal) &&
         tap_note_scores_[all_greater_or_equal] < 10;
}

bool PlayerStageStats::OneOfScore(TapNoteScore all_greater_or_equal) const {
  return FullComboOfScore(all_greater_or_equal) &&
         tap_note_scores_[all_greater_or_equal] == 1;
}

float PlayerStageStats::GetPercentageOfTaps(TapNoteScore tns) const {
  int total_taps = 0;
  for (int i = TNS_Miss; i < NUM_TapNoteScore; i++) {
    total_taps += tap_note_scores_[i];
  }
  return tap_note_scores_[tns] / (float)total_taps;
}

void PlayerStageStats::CalcAwards(
    PlayerNumber p, bool gave_up, bool used_autoplay) {
  // LOG->Trace( "hand out awards" );

  peak_combo_award_ = PeakComboAward_Invalid;

  if (gave_up || used_autoplay) {
    return;
  }

  std::deque<StageAward>& pdas = GAMESTATE->last_stage_awards_[p];

  // LOG->Trace( "per difficulty awards" );

  // per-difficulty awards
  // don't give per-difficutly awards if using easy mods
  if (!IsDisqualified()) {
    if (FullComboOfScore(TNS_W3)) {
      pdas.push_back(StageAward_FullComboW3);
    }
    if (SingleDigitsOfScore(TNS_W3)) {
      pdas.push_back(StageAward_SingleDigitW3);
    }
    if (FullComboOfScore(TNS_W2)) {
      pdas.push_back(StageAward_FullComboW2);
    }
    if (SingleDigitsOfScore(TNS_W2)) {
      pdas.push_back(StageAward_SingleDigitW2);
    }
    if (FullComboOfScore(TNS_W1)) {
      pdas.push_back(StageAward_FullComboW1);
    }

    if (OneOfScore(TNS_W3)) {
      pdas.push_back(StageAward_OneW3);
    }
    if (OneOfScore(TNS_W2)) {
      pdas.push_back(StageAward_OneW2);
    }

    float percent_w3s = GetPercentageOfTaps(TNS_W3);
    if (percent_w3s >= 0.8f) {
      pdas.push_back(StageAward_80PercentW3);
    }
    if (percent_w3s >= 0.9f) {
      pdas.push_back(StageAward_90PercentW3);
    }
    if (percent_w3s >= 1.f) {
      pdas.push_back(StageAward_100PercentW3);
    }
  }

  // Max one PDA per stage
  if (!pdas.empty()) {
    pdas.erase(pdas.begin(), pdas.end() - 1);
  }

  if (!pdas.empty()) {
    stage_award_ = pdas.back();
  } else {
    stage_award_ = StageAward_Invalid;
  }

  // LOG->Trace( "done with per difficulty awards" );

  // DO give peak combo awards if using easy mods
  int combo_at_start_of_stage = GetComboAtStartOfStage();
  int peak_combo = GetMaxCombo().cnt_;

  FOREACH_ENUM(PeakComboAward, pca) {
    int level = 1000 * (pca + 1);
    bool crossed_level = combo_at_start_of_stage < level && peak_combo >= level;
    // LOG->Trace( "pca = %d, iLevel = %d, bCrossedLevel = %d", pca, iLevel,
    // bCrossedLevel );
    if (crossed_level) {
      GAMESTATE->last_peak_combo_awards_[p].push_back(pca);
    }
  }

  if (!GAMESTATE->last_peak_combo_awards_[p].empty()) {
    peak_combo_award_ = GAMESTATE->last_peak_combo_awards_[p].back();
  } else {
    peak_combo_award_ = PeakComboAward_Invalid;
  }

  // LOG->Trace( "done with per combo awards" );
}

bool PlayerStageStats::IsDisqualified() const {
  if (!PREFSMAN->disqualification_) {
    return false;
  }
  return disqualified_;
}

LuaFunction(GetGradeFromPercent, GetGradeFromPercent(FArg(1))) LuaFunction(
    FormatPercentScore, PlayerStageStats::FormatPercentScore(FArg(1)))
// lua start
#include "LuaBinding.h"

    /** @brief Allow Lua to have access to the PlayerStageStats. */
    class LunaPlayerStageStats : public Luna<PlayerStageStats> {
 public:
  DEFINE_METHOD(GetCaloriesBurned, calories_burned_)
  DEFINE_METHOD(GetNumControllerSteps, num_controller_steps_)
  DEFINE_METHOD(GetLifeRemainingSeconds, life_remaining_seconds_)
  DEFINE_METHOD(GetSurvivalSeconds, GetSurvivalSeconds())
  DEFINE_METHOD(GetCurrentCombo, cur_combo_)
  DEFINE_METHOD(GetCurrentMissCombo, cur_miss_combo_)
  DEFINE_METHOD(GetCurrentScoreMultiplier, cur_score_multiplier_)
  DEFINE_METHOD(GetScore, score_)
  DEFINE_METHOD(GetCurMaxScore, cur_max_score_)
  DEFINE_METHOD(
      GetTapNoteScores, tap_note_scores_[Enum::Check<TapNoteScore>(L, 1)])
  DEFINE_METHOD(
      GetHoldNoteScores, hold_note_scores_[Enum::Check<HoldNoteScore>(L, 1)])
  DEFINE_METHOD(FullCombo, FullCombo())
  DEFINE_METHOD(
      FullComboOfScore, FullComboOfScore(Enum::Check<TapNoteScore>(L, 1)))
  DEFINE_METHOD(MaxCombo, GetMaxCombo().cnt_)
  DEFINE_METHOD(GetCurrentLife, GetCurrentLife())
  DEFINE_METHOD(GetGrade, GetGrade())
  DEFINE_METHOD(GetActualDancePoints, actual_dance_points_)
  DEFINE_METHOD(GetPossibleDancePoints, possible_dance_points_)
  DEFINE_METHOD(GetCurrentPossibleDancePoints, cur_possible_dance_points_)
  DEFINE_METHOD(GetPercentDancePoints, GetPercentDancePoints())
  DEFINE_METHOD(GetLessonScoreActual, GetLessonScoreActual())
  DEFINE_METHOD(GetLessonScoreNeeded, GetLessonScoreNeeded())
  DEFINE_METHOD(GetPersonalHighScoreIndex, personal_high_score_index_)
  DEFINE_METHOD(GetMachineHighScoreIndex, machine_high_score_index_)
  DEFINE_METHOD(GetStageAward, stage_award_)
  DEFINE_METHOD(GetPeakComboAward, peak_combo_award_)
  DEFINE_METHOD(IsDisqualified, IsDisqualified())
  DEFINE_METHOD(GetAliveSeconds, alive_seconds_)
  DEFINE_METHOD(
      GetPercentageOfTaps, GetPercentageOfTaps(Enum::Check<TapNoteScore>(L, 1)))
  DEFINE_METHOD(GetBestFullComboTapNoteScore, GetBestFullComboTapNoteScore())
  DEFINE_METHOD(GetFailed, failed_)
  DEFINE_METHOD(GetSongsPassed, songs_passed_)
  DEFINE_METHOD(GetSongsPlayed, songs_played_)

  static int GetHighScore(T* p, lua_State* L) {
    p->high_score_.PushSelf(L);
    return 1;
  }

  static int GetPlayedSteps(T* p, lua_State* L) {
    lua_newtable(L);
    for (int i = 0;
         i < (int)std::min(p->steps_played_, (int)p->possible_steps_.size());
         ++i) {
      p->possible_steps_[i]->PushSelf(L);
      lua_rawseti(L, -2, i + 1);
    }
    return 1;
  }
  static int GetPossibleSteps(T* p, lua_State* L) {
    lua_newtable(L);
    for (int i = 0; i < (int)p->possible_steps_.size(); ++i) {
      p->possible_steps_[i]->PushSelf(L);
      lua_rawseti(L, -2, i + 1);
    }
    return 1;
  }
  static int GetComboList(T* p, lua_State* L) {
    lua_createtable(L, p->combo_list_.size(), 0);
    for (size_t i = 0; i < p->combo_list_.size(); ++i) {
      lua_createtable(L, 0, 6);
      lua_pushstring(L, "StartSecond");
      lua_pushnumber(L, p->combo_list_[i].start_second_);
      lua_rawset(L, -3);
      lua_pushstring(L, "SizeSeconds");
      lua_pushnumber(L, p->combo_list_[i].size_seconds_);
      lua_rawset(L, -3);
      lua_pushstring(L, "Count");
      lua_pushnumber(L, p->combo_list_[i].cnt_);
      lua_rawset(L, -3);
      lua_pushstring(L, "Rollover");
      lua_pushnumber(L, p->combo_list_[i].rollover_);
      lua_rawset(L, -3);
      lua_pushstring(L, "StageCount");
      lua_pushnumber(L, p->combo_list_[i].GetStageCnt());
      lua_rawset(L, -3);
      lua_pushstring(L, "IsZero");
      lua_pushnumber(L, p->combo_list_[i].IsZero());
      lua_rawset(L, -3);
      lua_rawseti(L, -2, i + 1);
    }
    return 1;
  }
  static int GetLifeRecord(T* p, lua_State* L) {
    float last_second = FArg(1);
    int samples = 100;
    if (lua_gettop(L) >= 2 && !lua_isnil(L, 2)) {
      samples = IArg(2);
      if (samples <= 0) {
        LOG->Trace(
            "PlayerStageStats:GetLifeRecord requires an integer greater than "
            "0.  Defaulting to 100.");
        samples = 100;
      }
    }
    lua_createtable(L, samples, 0);
    for (int i = 0; i < samples; ++i) {
      // The scale from range is [0, samples-1] because that is i's range.
      float from = SCALE(i, 0, (float)samples - 1.0f, 0.0f, last_second);
      float curr = p->GetLifeRecordLerpAt(from);
      lua_pushnumber(L, curr);
      lua_rawseti(L, -2, i + 1);
    }
    return 1;
  }

  static int GetRadarPossible(T* p, lua_State* L) {
    p->radar_possible_.PushSelf(L);
    return 1;
  }
  static int GetRadarActual(T* p, lua_State* L) {
    p->radar_actual_.PushSelf(L);
    return 1;
  }
  static int SetScore(T* p, lua_State* L) {
    if (IArg(1) >= 0) {
      p->score_ = IArg(1);
      return 1;
    }
    COMMON_RETURN_SELF;
  }
  static int SetCurMaxScore(T* p, lua_State* L) {
    if (IArg(1) >= 0) {
      p->cur_max_score_ = IArg(1);
      return 1;
    }
    COMMON_RETURN_SELF;
  }
  static int SetDancePointLimits(T* p, lua_State* L) {
    int actual = IArg(1);
    int possible = IArg(2);
    if (actual >= 0 && possible > 0) {
      p->possible_dance_points_ = possible;
      if (actual <= possible) {
        p->actual_dance_points_ = actual;
      } else {
        p->actual_dance_points_ = possible;
      }
      return 1;
    }
    COMMON_RETURN_SELF;
  }

  static int FailPlayer(T* p, lua_State* L) {
    p->failed_ = true;
    COMMON_RETURN_SELF;
  }

  LunaPlayerStageStats() {
    ADD_METHOD(GetCaloriesBurned);
    ADD_METHOD(GetNumControllerSteps);
    ADD_METHOD(GetLifeRemainingSeconds);
    ADD_METHOD(GetSurvivalSeconds);
    ADD_METHOD(GetCurrentCombo);
    ADD_METHOD(GetCurrentMissCombo);
    ADD_METHOD(GetCurrentScoreMultiplier);
    ADD_METHOD(GetScore);
    ADD_METHOD(GetCurMaxScore);
    ADD_METHOD(GetTapNoteScores);
    ADD_METHOD(GetHoldNoteScores);
    ADD_METHOD(FullCombo);
    ADD_METHOD(FullComboOfScore);
    ADD_METHOD(MaxCombo);
    ADD_METHOD(GetCurrentLife);
    ADD_METHOD(GetGrade);
    ADD_METHOD(GetHighScore);
    ADD_METHOD(GetActualDancePoints);
    ADD_METHOD(GetPossibleDancePoints);
    ADD_METHOD(GetCurrentPossibleDancePoints);
    ADD_METHOD(GetPercentDancePoints);
    ADD_METHOD(GetLessonScoreActual);
    ADD_METHOD(GetLessonScoreNeeded);
    ADD_METHOD(GetPersonalHighScoreIndex);
    ADD_METHOD(GetMachineHighScoreIndex);
    ADD_METHOD(GetStageAward);
    ADD_METHOD(GetPeakComboAward);
    ADD_METHOD(IsDisqualified);
    ADD_METHOD(GetPlayedSteps);
    ADD_METHOD(GetPossibleSteps);
    ADD_METHOD(GetComboList);
    ADD_METHOD(GetLifeRecord);
    ADD_METHOD(GetAliveSeconds);
    ADD_METHOD(GetPercentageOfTaps);
    ADD_METHOD(GetRadarActual);
    ADD_METHOD(GetRadarPossible);
    ADD_METHOD(GetBestFullComboTapNoteScore);
    ADD_METHOD(GetFailed);
    ADD_METHOD(SetScore);
    ADD_METHOD(GetCurMaxScore);
    ADD_METHOD(SetCurMaxScore);
    ADD_METHOD(SetDancePointLimits);
    ADD_METHOD(FailPlayer);
    ADD_METHOD(GetSongsPassed);
    ADD_METHOD(GetSongsPlayed);
  }
};

LUA_REGISTER_CLASS(PlayerStageStats)
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
