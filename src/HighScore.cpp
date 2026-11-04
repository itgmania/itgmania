#include "HighScore.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "Grade.h"
#include "LuaManager.h"
#include "PlayerNumber.h"
#include "PrefsManager.h"
#include "RadarValues.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "RageUtil_AutoPtr.h"
#include "ThemeMetric.h"
#include "XmlFile.h"
#include "global.h"

ThemeMetric<std::string> EMPTY_NAME("HighScore", "EmptyName");

struct HighScoreImpl {
  std::string name_;  // name that shows in the machine's ranking screen
  Grade grade_;
  unsigned int score_;
  float percent_dp_;
  float survive_seconds_;
  unsigned int max_combo_;           // maximum combo obtained [SM5 alpha 1a+]
  StageAward stage_award_;           // stage award [SM5 alpha 1a+]
  PeakComboAward peak_combo_award_;  // peak combo award [SM5 alpha 1a+]
  std::string modifiers_;
  DateTime date_time_;       // return value of time() when screenshot was taken
  std::string player_guid_;  // who made this high score
  std::string machine_guid_;  // where this high score was made
  int product_id_;
  int tap_note_scores_[NUM_TapNoteScore];
  int hold_note_scores_[NUM_HoldNoteScore];
  RadarValues radar_values_;
  float life_remaining_seconds_;
  bool disqualified_;
  bool is_routine_;

  // Additional player-specific attributes for HighScore
  std::vector<std::string> player_names_;
  std::vector<Grade> player_grades_;
  std::vector<unsigned int> player_scores_;
  std::vector<float> player_percent_d_ps_;
  std::vector<unsigned int> player_max_combos_;
  std::vector<std::string> player_guids_;
  std::vector<std::array<int, NUM_TapNoteScore>> player_tap_note_scores_;
  std::vector<std::array<int, NUM_HoldNoteScore>> player_hold_note_scores_;

  HighScoreImpl();
  XNode* CreateNode() const;
  void LoadFromNode(const XNode* pNode);

  bool operator==(const HighScoreImpl& other) const;
  bool operator!=(const HighScoreImpl& other) const {
    return !(*this == other);
  }
};

bool HighScoreImpl::operator==(const HighScoreImpl& other) const {
  if (name_ != other.name_) {
    return false;
  }
  if (grade_ != other.grade_) {
    return false;
  }
  if (score_ != other.score_) {
    return false;
  }
  if (max_combo_ != other.max_combo_) {
    return false;
  }
  if (stage_award_ != other.stage_award_) {
    return false;
  }
  if (peak_combo_award_ != other.peak_combo_award_) {
    return false;
  }
  if (percent_dp_ != other.percent_dp_) {
    return false;
  }
  if (survive_seconds_ != other.survive_seconds_) {
    return false;
  }
  if (modifiers_ != other.modifiers_) {
    return false;
  }
  if (date_time_ != other.date_time_) {
    return false;
  }
  if (player_guid_ != other.player_guid_) {
    return false;
  }
  if (machine_guid_ != other.machine_guid_) {
    return false;
  }
  if (product_id_ != other.product_id_) {
    return false;
  }
  FOREACH_ENUM(TapNoteScore, tns) {
    if (tap_note_scores_[tns] != other.tap_note_scores_[tns]) {
      return false;
    }
  }
  FOREACH_ENUM(HoldNoteScore, hns) {
    if (hold_note_scores_[hns] != other.hold_note_scores_[hns]) {
      return false;
    }
  }
  if (radar_values_ != other.radar_values_) {
    return false;
  }
  if (life_remaining_seconds_ != other.life_remaining_seconds_) {
    return false;
  }
  if (disqualified_ != other.disqualified_) {
    return false;
  }
  return true;
}

HighScoreImpl::HighScoreImpl() {
  name_ = "";
  grade_ = Grade_NoData;
  score_ = 0;
  percent_dp_ = 0;
  survive_seconds_ = 0;
  max_combo_ = 0;
  stage_award_ = StageAward_Invalid;
  peak_combo_award_ = PeakComboAward_Invalid;
  modifiers_ = "";
  date_time_.Init();
  player_guid_ = "";
  machine_guid_ = "";
  product_id_ = 0;
  ZERO(tap_note_scores_);
  ZERO(hold_note_scores_);
  radar_values_.MakeUnknown();
  life_remaining_seconds_ = 0;
  is_routine_ = false;

  player_names_ = std::vector<std::string>(NUM_PLAYERS, "");
  player_grades_ = std::vector<Grade>(NUM_PLAYERS, Grade_NoData);
  player_scores_ = std::vector<unsigned int>(NUM_PLAYERS, 0);
  player_percent_d_ps_ = std::vector<float>(NUM_PLAYERS, 0);
  player_max_combos_ = std::vector<unsigned int>(NUM_PLAYERS, 0);
  player_guids_ = std::vector<std::string>(NUM_PLAYERS, "");

  player_tap_note_scores_.resize(
      NUM_PLAYERS, std::array<int, NUM_TapNoteScore>{});
  player_hold_note_scores_.resize(
      NUM_PLAYERS, std::array<int, NUM_HoldNoteScore>{});

  // Initialize each array to zeros
  for (auto& scoresArray : player_tap_note_scores_) {
    std::fill(scoresArray.begin(), scoresArray.end(), 0);
  }
  for (auto& scoresArray : player_hold_note_scores_) {
    std::fill(scoresArray.begin(), scoresArray.end(), 0);
  }
}

XNode* HighScoreImpl::CreateNode() const {
  XNode* pNode = new XNode("HighScore");
  const bool bWriteSimpleValues = RadarValues::WRITE_SIMPLE_VALIES;
  const bool bWriteComplexValues = RadarValues::WRITE_COMPLEX_VALIES;

  // TRICKY:  Don't write "name to fill in" markers.
  pNode->AppendChild(
      "Name", IsRankingToFillIn(name_) ? std::string("") : name_);
  pNode->AppendChild("Grade", GradeToString(grade_));
  pNode->AppendChild("Score", score_);
  pNode->AppendChild("PercentDP", percent_dp_);
  pNode->AppendChild("SurviveSeconds", survive_seconds_);
  pNode->AppendChild("MaxCombo", max_combo_);
  pNode->AppendChild("StageAward", StageAwardToString(stage_award_));
  pNode->AppendChild(
      "PeakComboAward", PeakComboAwardToString(peak_combo_award_));
  pNode->AppendChild("Modifiers", modifiers_);
  pNode->AppendChild("DateTime", date_time_.GetString());
  pNode->AppendChild("PlayerGuid", player_guid_);
  pNode->AppendChild("MachineGuid", machine_guid_);
  pNode->AppendChild("ProductID", product_id_);
  XNode* pTapNoteScores = pNode->AppendChild("TapNoteScores");
  FOREACH_ENUM(TapNoteScore, tns)
  if (tns != TNS_None) {  // HACK: don't save meaningless "none" count
    pTapNoteScores->AppendChild(
        TapNoteScoreToString(tns), tap_note_scores_[tns]);
  }
  XNode* pHoldNoteScores = pNode->AppendChild("HoldNoteScores");
  FOREACH_ENUM(HoldNoteScore, hns)
  if (hns != HNS_None) {  // HACK: don't save meaningless "none" count
    pHoldNoteScores->AppendChild(
        HoldNoteScoreToString(hns), hold_note_scores_[hns]);
  }
  pNode->AppendChild(
      radar_values_.CreateNode(bWriteSimpleValues, bWriteComplexValues));
  pNode->AppendChild("LifeRemainingSeconds", life_remaining_seconds_);
  pNode->AppendChild("Disqualified", disqualified_);
  if (is_routine_) {
    XNode* pRoutineNode = pNode->AppendChild("RoutineData");
    // Loop through each player and add their specific data to "RoutineData"
    for (size_t i = 0; i < player_names_.size(); ++i) {
      XNode* pPlayerNode =
          pRoutineNode->AppendChild(PlayerNumberToString((PlayerNumber)i));
      pPlayerNode->AppendChild("Grade", GradeToString(player_grades_[i]));
      pPlayerNode->AppendChild("Score", player_scores_[i]);
      pPlayerNode->AppendChild("PercentDP", player_percent_d_ps_[i]);
      pPlayerNode->AppendChild("MaxCombo", player_max_combos_[i]);
      pPlayerNode->AppendChild("PlayerGuid", player_guids_[i]);

      XNode* pRoutineTapNoteScores = pPlayerNode->AppendChild("TapNoteScores");
      FOREACH_ENUM(TapNoteScore, tns)
      if (tns != TNS_None) {  // HACK: don't save meaningless "none" count
        pRoutineTapNoteScores->AppendChild(
            TapNoteScoreToString(tns), player_tap_note_scores_[i][tns]);
      }
      XNode* pRoutineHoldNoteScores =
          pPlayerNode->AppendChild("HoldNoteScores");
      FOREACH_ENUM(HoldNoteScore, hns)
      if (hns != HNS_None) {  // HACK: don't save meaningless "none" count
        pRoutineHoldNoteScores->AppendChild(
            HoldNoteScoreToString(hns), player_hold_note_scores_[i][hns]);
      }
    }
  }
  return pNode;
}

void HighScoreImpl::LoadFromNode(const XNode* pNode) {
  ASSERT(pNode->GetName() == "HighScore");

  std::string s;

  pNode->GetChildValue("Name", name_);
  pNode->GetChildValue("Grade", s);
  grade_ = StringToGrade(s);
  pNode->GetChildValue("Score", score_);
  pNode->GetChildValue("PercentDP", percent_dp_);
  pNode->GetChildValue("SurviveSeconds", survive_seconds_);
  pNode->GetChildValue("MaxCombo", max_combo_);
  pNode->GetChildValue("StageAward", s);
  stage_award_ = StringToStageAward(s);
  pNode->GetChildValue("PeakComboAward", s);
  peak_combo_award_ = StringToPeakComboAward(s);
  pNode->GetChildValue("Modifiers", modifiers_);
  pNode->GetChildValue("DateTime", s);
  date_time_.FromString(s);
  pNode->GetChildValue("PlayerGuid", player_guid_);
  pNode->GetChildValue("MachineGuid", machine_guid_);
  pNode->GetChildValue("ProductID", product_id_);
  const XNode* pTapNoteScores = pNode->GetChild("TapNoteScores");
  if (pTapNoteScores) {
    FOREACH_ENUM(TapNoteScore, tns)
    pTapNoteScores->GetChildValue(
        TapNoteScoreToString(tns), tap_note_scores_[tns]);
  }
  const XNode* pHoldNoteScores = pNode->GetChild("HoldNoteScores");
  if (pHoldNoteScores) {
    FOREACH_ENUM(HoldNoteScore, hns)
    pHoldNoteScores->GetChildValue(
        HoldNoteScoreToString(hns), hold_note_scores_[hns]);
  }
  const XNode* pRadarValues = pNode->GetChild("RadarValues");
  if (pRadarValues) {
    radar_values_.LoadFromNode(pRadarValues);
  }
  pNode->GetChildValue("LifeRemainingSeconds", life_remaining_seconds_);
  pNode->GetChildValue("Disqualified", disqualified_);

  // Validate input.
  grade_ = std::clamp(grade_, Grade_Tier01, Grade_Failed);
  const XNode* pRoutineNode = pNode->GetChild("RoutineData");

  if (pRoutineNode) {
    // Load player-specific data
    is_routine_ = true;
    FOREACH_PlayerNumber(pn) {
      const XNode* pPlayerNode =
          pRoutineNode->GetChild(PlayerNumberToString(pn));
      if (pPlayerNode) {
        std::string gradeStr;
        pPlayerNode->GetChildValue("Name", player_names_[pn]);
        pPlayerNode->GetChildValue("Grade", gradeStr);
        player_grades_[pn] = StringToGrade(gradeStr);
        pPlayerNode->GetChildValue("Score", player_scores_[pn]);
        LOG->Trace("Loaded player score %d", player_scores_[pn]);
        pPlayerNode->GetChildValue("PercentDP", player_percent_d_ps_[pn]);
        LOG->Trace("Loaded player percentDP %f", player_percent_d_ps_[pn]);
        pPlayerNode->GetChildValue("MaxCombo", player_max_combos_[pn]);
        LOG->Trace("Loaded player maxCombo %d", player_max_combos_[pn]);
        pPlayerNode->GetChildValue("PlayerGuid", player_guids_[pn]);

        const XNode* pPTapNoteScores = pPlayerNode->GetChild("TapNoteScores");
        if (pPTapNoteScores) {
          FOREACH_ENUM(TapNoteScore, tns) {
            pPTapNoteScores->GetChildValue(
                TapNoteScoreToString(tns), player_tap_note_scores_[pn][tns]);
          }
        }

        const XNode* pPHoldNoteScores = pPlayerNode->GetChild("HoldNoteScores");
        if (pPHoldNoteScores) {
          FOREACH_ENUM(HoldNoteScore, hns) {
            pPHoldNoteScores->GetChildValue(
                HoldNoteScoreToString(hns), player_hold_note_scores_[pn][hns]);
          }
        }
      }
    }
  } else {
    is_routine_ = false;
  }
}

REGISTER_CLASS_TRAITS(HighScoreImpl, new HighScoreImpl(*pCopy))

HighScore::HighScore() { impl_ = new HighScoreImpl; }

bool HighScore::IsEmpty() const {
  if (impl_->tap_note_scores_[TNS_W1] || impl_->tap_note_scores_[TNS_W2] ||
      impl_->tap_note_scores_[TNS_W3] || impl_->tap_note_scores_[TNS_W4] ||
      impl_->tap_note_scores_[TNS_W5]) {
    return false;
  }
  if (impl_->hold_note_scores_[HNS_Held] > 0) {
    return false;
  }
  return true;
}

std::string HighScore::GetName() const { return impl_->name_; }
Grade HighScore::GetGrade() const { return impl_->grade_; }
unsigned int HighScore::GetScore() const { return impl_->score_; }
unsigned int HighScore::GetMaxCombo() const { return impl_->max_combo_; }
StageAward HighScore::GetStageAward() const { return impl_->stage_award_; }
PeakComboAward HighScore::GetPeakComboAward() const {
  return impl_->peak_combo_award_;
}
float HighScore::GetPercentDP() const { return impl_->percent_dp_; }
float HighScore::GetSurviveSeconds() const { return impl_->survive_seconds_; }
float HighScore::GetSurvivalSeconds() const {
  return GetSurviveSeconds() + GetLifeRemainingSeconds();
}
std::string HighScore::GetModifiers() const { return impl_->modifiers_; }
DateTime HighScore::GetDateTime() const { return impl_->date_time_; }
int HighScore::GetTapNoteScore(TapNoteScore tns) const {
  return impl_->tap_note_scores_[tns];
}
int HighScore::GetHoldNoteScore(HoldNoteScore hns) const {
  return impl_->hold_note_scores_[hns];
}
const RadarValues& HighScore::GetRadarValues() const {
  return impl_->radar_values_;
}
float HighScore::GetLifeRemainingSeconds() const {
  return impl_->life_remaining_seconds_;
}
bool HighScore::GetDisqualified() const { return impl_->disqualified_; }

void HighScore::SetName(const std::string& sName) { impl_->name_ = sName; }
void HighScore::SetGrade(Grade g) { impl_->grade_ = g; }
void HighScore::SetScore(unsigned int iScore) { impl_->score_ = iScore; }
void HighScore::SetMaxCombo(unsigned int i) { impl_->max_combo_ = i; }
void HighScore::SetStageAward(StageAward a) { impl_->stage_award_ = a; }
void HighScore::SetPeakComboAward(PeakComboAward a) {
  impl_->peak_combo_award_ = a;
}
void HighScore::SetPercentDP(float f) { impl_->percent_dp_ = f; }
void HighScore::SetAliveSeconds(float f) { impl_->survive_seconds_ = f; }
void HighScore::SetModifiers(std::string s) { impl_->modifiers_ = s; }
void HighScore::SetDateTime(DateTime d) { impl_->date_time_ = d; }
void HighScore::SetPlayerGuid(std::string s) { impl_->player_guid_ = s; }
void HighScore::SetMachineGuid(std::string s) { impl_->machine_guid_ = s; }
void HighScore::SetProductID(int i) { impl_->product_id_ = i; }
void HighScore::SetTapNoteScore(TapNoteScore tns, int i) {
  impl_->tap_note_scores_[tns] = i;
}
void HighScore::SetHoldNoteScore(HoldNoteScore hns, int i) {
  impl_->hold_note_scores_[hns] = i;
}
void HighScore::SetRadarValues(const RadarValues& rv) {
  impl_->radar_values_ = rv;
}
void HighScore::SetLifeRemainingSeconds(float f) {
  impl_->life_remaining_seconds_ = f;
}
void HighScore::SetDisqualified(bool b) { impl_->disqualified_ = b; }

void HighScore::SetRoutine(bool b) { impl_->is_routine_ = b; }

bool HighScore::IsRoutine() const { return impl_->is_routine_; }

// Getters
std::string HighScore::GetPlayerGuid(const PlayerNumber& playerNum) const {
  return impl_->player_guids_.at(playerNum);
}

void HighScore::SetPlayerGrade(const PlayerNumber& playerNum, Grade grade) {
  impl_->player_grades_[playerNum] = grade;
}

void HighScore::SetPlayerScore(
    const PlayerNumber& playerNum, unsigned int score) {
  impl_->player_scores_[playerNum] = score;
}

void HighScore::SetPlayerPercentDP(
    const PlayerNumber& playerNum, float percentDP) {
  impl_->player_percent_d_ps_[playerNum] = percentDP;
}

void HighScore::SetPlayerMaxCombo(
    const PlayerNumber& playerNum, unsigned int maxCombo) {
  impl_->player_max_combos_[playerNum] = maxCombo;
}

void HighScore::SetPlayerGuid(
    const PlayerNumber& playerNum, const std::string& guid) {
  impl_->player_guids_[playerNum] = guid;
}

// Assuming TapNoteScores and HoldNoteScores are stored in a similar format to
// HighScoreImpl
void HighScore::SetPlayerTapNoteScore(
    const PlayerNumber& playerNum, TapNoteScore tns, int score) {
  impl_->player_tap_note_scores_[playerNum][tns] = score;
}

void HighScore::SetPlayerHoldNoteScore(
    const PlayerNumber& playerNum, HoldNoteScore hns, int score) {
  impl_->player_hold_note_scores_[playerNum][hns] = score;
}

/* We normally don't give direct access to the members.  We need this one
 * for NameToFillIn; use a special accessor so it's easy to find where this
 * is used. */
std::string* HighScore::GetNameMutable() { return &impl_->name_; }

bool HighScore::operator<(const HighScore& other) const {
  /* Make sure we treat AAAA as higher than AAA, even though the score
   * is the same. */
  if (PREFSMAN->percentage_scoring_) {
    if (GetPercentDP() != other.GetPercentDP()) {
      return GetPercentDP() < other.GetPercentDP();
    }
  } else {
    if (GetScore() != other.GetScore()) {
      return GetScore() < other.GetScore();
    }
  }

  return GetGrade() < other.GetGrade();
}

bool HighScore::operator>=(const HighScore& other) const {
  return !operator<(other);
}

bool HighScore::operator==(const HighScore& other) const {
  return *impl_ == *other.impl_;
}

XNode* HighScore::CreateNode() const { return impl_->CreateNode(); }

void HighScore::LoadFromNode(const XNode* pNode) { impl_->LoadFromNode(pNode); }

std::string HighScore::GetDisplayName() const {
  if (GetName().empty()) {
    return EMPTY_NAME;
  } else {
    return GetName();
  }
}

/* begin HighScoreList */
void HighScoreList::Init() {
  num_times_played_ = 0;
  high_scores_.clear();
  high_grade_ = Grade_NoData;
}

void HighScoreList::AddHighScore(
    HighScore hs, int& iIndexOut, bool bIsMachine) {
  int i;
  for (i = 0; i < (int)high_scores_.size(); i++) {
    if (hs >= high_scores_[i]) {
      break;
    }
  }
  const int iMaxScores = bIsMachine
                             ? PREFSMAN->max_high_scores_per_list_for_machine_
                             : PREFSMAN->max_high_scores_per_list_for_player_;
  if (i < iMaxScores) {
    high_scores_.insert(high_scores_.begin() + i, hs);
    iIndexOut = i;

    // Delete extra machine high scores in RemoveAllButOneOfEachNameAndClampSize
    // and not here so that we don't end up with less than iMaxScores after
    // removing HighScores with duplicate names.
    //
    if (!bIsMachine) {
      ClampSize(bIsMachine);
    }
  }
  high_grade_ = std::min(hs.GetGrade(), high_grade_);
}

void HighScoreList::IncrementPlayCount(DateTime _dtLastPlayed) {
  last_played_ = _dtLastPlayed;
  num_times_played_++;
}

const HighScore& HighScoreList::GetTopScore() const {
  if (high_scores_.empty()) {
    static HighScore hs;
    hs = HighScore();
    return hs;
  } else {
    return high_scores_[0];
  }
}

XNode* HighScoreList::CreateNode() const {
  XNode* pNode = new XNode("HighScoreList");

  pNode->AppendChild("NumTimesPlayed", num_times_played_);
  pNode->AppendChild("LastPlayed", last_played_.GetString());
  if (high_grade_ != Grade_NoData) {
    pNode->AppendChild("HighGrade", GradeToString(high_grade_));
  }

  for (unsigned i = 0; i < high_scores_.size(); i++) {
    const HighScore& hs = high_scores_[i];
    pNode->AppendChild(hs.CreateNode());
  }

  return pNode;
}

void HighScoreList::LoadFromNode(const XNode* pHighScoreList) {
  Init();

  ASSERT(pHighScoreList->GetName() == "HighScoreList");
  FOREACH_CONST_Child(pHighScoreList, p) {
    const std::string& name = p->GetName();
    if (name == "NumTimesPlayed") {
      p->GetTextValue(num_times_played_);
    } else if (name == "LastPlayed") {
      std::string s;
      p->GetTextValue(s);
      last_played_.FromString(s);
    } else if (name == "HighGrade") {
      std::string s;
      p->GetTextValue(s);
      high_grade_ = StringToGrade(s);
    } else if (name == "HighScore") {
      high_scores_.resize(high_scores_.size() + 1);
      high_scores_.back().LoadFromNode(p);

      high_grade_ = std::min(high_scores_.back().GetGrade(), high_grade_);
    }
  }
}

void HighScoreList::RemoveAllButOneOfEachName() {
  for (std::vector<HighScore>::iterator i = high_scores_.begin();
       i != high_scores_.end(); ++i) {
    for (std::vector<HighScore>::iterator j = i + 1; j != high_scores_.end();
         j++) {
      if (i->GetName() == j->GetName()) {
        j--;
        high_scores_.erase(j + 1);
      }
    }
  }
}

void HighScoreList::ClampSize(bool bIsMachine) {
  const int iMaxScores = bIsMachine
                             ? PREFSMAN->max_high_scores_per_list_for_machine_
                             : PREFSMAN->max_high_scores_per_list_for_player_;
  if (high_scores_.size() > unsigned(iMaxScores)) {
    high_scores_.erase(high_scores_.begin() + iMaxScores, high_scores_.end());
  }
}

void HighScoreList::MergeFromOtherHSL(HighScoreList& other, bool is_machine) {
  num_times_played_ += other.num_times_played_;
  if (other.last_played_ > last_played_) {
    last_played_ = other.last_played_;
  }
  if (other.high_grade_ > high_grade_) {
    high_grade_ = other.high_grade_;
  }
  high_scores_.insert(
      high_scores_.end(), other.high_scores_.begin(), other.high_scores_.end());
  std::sort(high_scores_.begin(), high_scores_.end());
  // Remove non-unique scores because they probably come from an accidental
  // repeated merge. -Kyz
  std::vector<HighScore>::iterator unique_end =
      std::unique(high_scores_.begin(), high_scores_.end());
  high_scores_.erase(unique_end, high_scores_.end());
  // Reverse it because sort moved the lesser scores to the top.
  std::reverse(high_scores_.begin(), high_scores_.end());

  if (!PREFSMAN->allow_multiple_high_score_with_same_name_) {
    // erase all but the highest score for each name
    RemoveAllButOneOfEachName();
  }
  ClampSize(is_machine);
}

XNode* Screenshot::CreateNode() const {
  XNode* pNode = new XNode("Screenshot");

  // TRICKY:  Don't write "name to fill in" markers.
  pNode->AppendChild("FileName", file_name_);
  pNode->AppendChild("MD5", md5_);
  pNode->AppendChild(high_score_.CreateNode());

  return pNode;
}

void Screenshot::LoadFromNode(const XNode* pNode) {
  ASSERT(pNode->GetName() == "Screenshot");

  pNode->GetChildValue("FileName", file_name_);
  pNode->GetChildValue("MD5", md5_);
  const XNode* pHighScore = pNode->GetChild("HighScore");
  if (pHighScore) {
    high_score_.LoadFromNode(pHighScore);
  }
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the HighScore. */
class LunaHighScore : public Luna<HighScore> {
 public:
  static int GetName(T* p, lua_State* L) {
    lua_pushstring(L, p->GetName().c_str());
    return 1;
  }
  static int GetScore(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetScore());
    return 1;
  }
  static int GetPercentDP(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetPercentDP());
    return 1;
  }
  static int GetDate(T* p, lua_State* L) {
    lua_pushstring(L, p->GetDateTime().GetString().c_str());
    return 1;
  }
  static int GetSurvivalSeconds(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetSurvivalSeconds());
    return 1;
  }
  static int IsFillInMarker(T* p, lua_State* L) {
    bool bIsFillInMarker = false;
    FOREACH_PlayerNumber(pn) bIsFillInMarker |=
        p->GetName() == RANKING_TO_FILL_IN_MARKER[pn];
    lua_pushboolean(L, bIsFillInMarker);
    return 1;
  }
  static int GetMaxCombo(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetMaxCombo());
    return 1;
  }
  static int GetModifiers(T* p, lua_State* L) {
    lua_pushstring(L, p->GetModifiers().c_str());
    return 1;
  }
  static int GetTapNoteScore(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetTapNoteScore(Enum::Check<TapNoteScore>(L, 1)));
    return 1;
  }
  static int GetHoldNoteScore(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetHoldNoteScore(Enum::Check<HoldNoteScore>(L, 1)));
    return 1;
  }
  static int GetRadarValues(T* p, lua_State* L) {
    RadarValues& rv = const_cast<RadarValues&>(p->GetRadarValues());
    rv.PushSelf(L);
    return 1;
  }
  static int GetPlayerGuid(T* p, lua_State* L) {
    PlayerNumber pn = Enum::Check<PlayerNumber>(L, 1);
    lua_pushstring(L, p->GetPlayerGuid(pn).c_str());
    return 1;
  }
  DEFINE_METHOD(GetGrade, GetGrade())
  DEFINE_METHOD(GetStageAward, GetStageAward())
  DEFINE_METHOD(GetPeakComboAward, GetPeakComboAward())

  LunaHighScore() {
    ADD_METHOD(GetName);
    ADD_METHOD(GetScore);
    ADD_METHOD(GetPercentDP);
    ADD_METHOD(GetDate);
    ADD_METHOD(GetSurvivalSeconds);
    ADD_METHOD(IsFillInMarker);
    ADD_METHOD(GetModifiers);
    ADD_METHOD(GetTapNoteScore);
    ADD_METHOD(GetHoldNoteScore);
    ADD_METHOD(GetRadarValues);
    ADD_METHOD(GetPlayerGuid);
    ADD_METHOD(GetGrade);
    ADD_METHOD(GetMaxCombo);
    ADD_METHOD(GetStageAward);
    ADD_METHOD(GetPeakComboAward);
  }
};

LUA_REGISTER_CLASS(HighScore)

/** @brief Allow Lua to have access to the HighScoreList. */
class LunaHighScoreList : public Luna<HighScoreList> {
 public:
  static int GetHighScores(T* p, lua_State* L) {
    lua_newtable(L);
    for (int i = 0; i < (int)p->high_scores_.size(); ++i) {
      p->high_scores_[i].PushSelf(L);
      lua_rawseti(L, -2, i + 1);
    }

    return 1;
  }

  static int GetHighestScoreOfName(T* p, lua_State* L) {
    std::string name = SArg(1);
    for (size_t i = 0; i < p->high_scores_.size(); ++i) {
      if (name == p->high_scores_[i].GetName()) {
        p->high_scores_[i].PushSelf(L);
        return 1;
      }
    }
    lua_pushnil(L);
    return 1;
  }

  static int GetRankOfName(T* p, lua_State* L) {
    std::string name = SArg(1);
    size_t rank = 0;
    for (size_t i = 0; i < p->high_scores_.size(); ++i) {
      if (name == p->high_scores_[i].GetName()) {
        // Indices from Lua are one-indexed.  +1 to adjust.
        rank = i + 1;
        break;
      }
    }
    // The themer is expected to check for validity before using.
    lua_pushnumber(L, rank);
    return 1;
  }

  LunaHighScoreList() {
    ADD_METHOD(GetHighScores);
    ADD_METHOD(GetHighestScoreOfName);
    ADD_METHOD(GetRankOfName);
  }
};

LUA_REGISTER_CLASS(HighScoreList)
// lua end

/*
 * (c) 2004 Chris Danford
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
