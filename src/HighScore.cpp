#include "global.h"

#include "HighScore.h"

#include <algorithm>
#include <cstddef>
#include <vector>

#include "GameConstantsAndTypes.h"
#include "PlayerNumber.h"
#include "PrefsManager.h"
#include "RadarValues.h"
#include "ThemeManager.h"
#include "XmlFile.h"

ThemeMetric<RString> EMPTY_NAME("HighScore", "EmptyName");

struct HighScoreImpl {
  RString name;  // Name that shows in the machine's ranking screen.
  Grade grade;
  unsigned int score;
  float percent_dp;
  float survive_seconds;            // Seconds left in Survival mode.
  unsigned int max_combo;           // Maximum combo obtained [SM5 alpha 1a+].
  StageAward stage_award;           // Stage award [SM5 alpha 1a+].
  PeakComboAward peak_combo_award;  // Peak combo award [SM5 alpha 1a+].
  RString modifiers;
  DateTime date_time;    // Return value of time() when screenshot was taken.
  RString player_guid;   // Who made this high score?
  RString machine_guid;  // Where this high score was made.
  int product_id;
  int tap_note_scores[NUM_TapNoteScore];
  int hold_note_scores[NUM_HoldNoteScore];
  RadarValues radar_values;
  float life_remaining_seconds;
  bool disqualified;

  HighScoreImpl();
  XNode* CreateNode() const;
  void LoadFromNode(const XNode* node);

  bool operator==(const HighScoreImpl& other) const;
  bool operator!=(const HighScoreImpl& other) const {
    return !(*this == other);
  }
};

bool HighScoreImpl::operator==(const HighScoreImpl& other) const {
#define COMPARE(x) \
  if (x != other.x) return false;
  COMPARE(name);
  COMPARE(grade);
  COMPARE(score);
  COMPARE(percent_dp);
  COMPARE(survive_seconds);
  COMPARE(max_combo);
  COMPARE(stage_award);
  COMPARE(peak_combo_award);
  COMPARE(modifiers);
  COMPARE(date_time);
  COMPARE(player_guid);
  COMPARE(machine_guid);
  COMPARE(product_id);
  FOREACH_ENUM(TapNoteScore, tns)
  COMPARE(tap_note_scores[tns]);
  FOREACH_ENUM(HoldNoteScore, hns)
  COMPARE(hold_note_scores[hns]);
  COMPARE(radar_values);
  COMPARE(life_remaining_seconds);
  COMPARE(disqualified);
#undef COMPARE

  return true;
}

HighScoreImpl::HighScoreImpl() {
  name = "";
  grade = Grade_NoData;
  score = 0;
  percent_dp = 0;
  survive_seconds = 0;
  max_combo = 0;
  stage_award = StageAward_Invalid;
  peak_combo_award = PeakComboAward_Invalid;
  modifiers = "";
  date_time.Init();
  player_guid = "";
  machine_guid = "";
  product_id = 0;
  ZERO(tap_note_scores);
  ZERO(hold_note_scores);
  radar_values.MakeUnknown();
  life_remaining_seconds = 0;
}

XNode* HighScoreImpl::CreateNode() const {
  XNode* node = new XNode("HighScore");
  const bool bWriteSimpleValues = RadarValues::WRITE_SIMPLE_VALIES;
  const bool bWriteComplexValues = RadarValues::WRITE_COMPLEX_VALIES;

  // TRICKY:  Don't write "name to fill in" markers.
  node->AppendChild("Name", IsRankingToFillIn(name) ? RString("") : name);
  node->AppendChild("Grade", GradeToString(grade));
  node->AppendChild("Score", score);
  node->AppendChild("PercentDP", percent_dp);
  node->AppendChild("SurviveSeconds", survive_seconds);
  node->AppendChild("MaxCombo", max_combo);
  node->AppendChild("StageAward", StageAwardToString(stage_award));
  node->AppendChild("PeakComboAward", PeakComboAwardToString(peak_combo_award));
  node->AppendChild("Modifiers", modifiers);
  node->AppendChild("DateTime", date_time.GetString());
  node->AppendChild("PlayerGuid", player_guid);
  node->AppendChild("MachineGuid", machine_guid);
  node->AppendChild("ProductID", product_id);
  XNode* pTapNoteScores = node->AppendChild("TapNoteScores");
  FOREACH_ENUM(TapNoteScore, tns)
  // Don't save meaningless "none" count.
  if (tns != TNS_None) {
    pTapNoteScores->AppendChild(
        TapNoteScoreToString(tns), tap_note_scores[tns]);
  }
  XNode* pHoldNoteScores = node->AppendChild("HoldNoteScores");
  FOREACH_ENUM(HoldNoteScore, hns)
  // Don't save meaningless "none" count.
  if (hns != HNS_None) {
    pHoldNoteScores->AppendChild(
        HoldNoteScoreToString(hns), hold_note_scores[hns]);
  }
  node->AppendChild(
      radar_values.CreateNode(bWriteSimpleValues, bWriteComplexValues));
  node->AppendChild("LifeRemainingSeconds", life_remaining_seconds);
  node->AppendChild("Disqualified", disqualified);

  return node;
}

void HighScoreImpl::LoadFromNode(const XNode* node) {
  ASSERT(node->GetName() == "HighScore");

  RString s;

  node->GetChildValue("Name", name);
  node->GetChildValue("Grade", s);
  grade = StringToGrade(s);
  node->GetChildValue("Score", score);
  node->GetChildValue("PercentDP", percent_dp);
  node->GetChildValue("SurviveSeconds", survive_seconds);
  node->GetChildValue("MaxCombo", max_combo);
  node->GetChildValue("StageAward", s);
  stage_award = StringToStageAward(s);
  node->GetChildValue("PeakComboAward", s);
  peak_combo_award = StringToPeakComboAward(s);
  node->GetChildValue("Modifiers", modifiers);
  node->GetChildValue("DateTime", s);
  date_time.FromString(s);
  node->GetChildValue("PlayerGuid", player_guid);
  node->GetChildValue("MachineGuid", machine_guid);
  node->GetChildValue("ProductID", product_id);
  const XNode* tap_note_scores_node = node->GetChild("TapNoteScores");
  if (tap_note_scores_node) {
    FOREACH_ENUM(TapNoteScore, tns)
    tap_note_scores_node->GetChildValue(
        TapNoteScoreToString(tns), tap_note_scores[tns]);
  }
  const XNode* hold_note_scores_node = node->GetChild("HoldNoteScores");
  if (hold_note_scores_node) {
    FOREACH_ENUM(HoldNoteScore, hns)
    hold_note_scores_node->GetChildValue(
        HoldNoteScoreToString(hns), hold_note_scores[hns]);
  }
  const XNode* radar_values_node = node->GetChild("RadarValues");
  if (radar_values_node) {
    radar_values.LoadFromNode(radar_values_node);
  }
  node->GetChildValue("LifeRemainingSeconds", life_remaining_seconds);
  node->GetChildValue("Disqualified", disqualified);

  // Validate input.
  grade = clamp(grade, Grade_Tier01, Grade_Failed);
}

REGISTER_CLASS_TRAITS(HighScoreImpl, new HighScoreImpl(*pCopy))

HighScore::HighScore() { high_score_impl_ = new HighScoreImpl; }

void HighScore::Unset() { high_score_impl_ = new HighScoreImpl; }

bool HighScore::IsEmpty() const {
  if (high_score_impl_->tap_note_scores[TNS_W1] ||
      high_score_impl_->tap_note_scores[TNS_W2] ||
      high_score_impl_->tap_note_scores[TNS_W3] ||
      high_score_impl_->tap_note_scores[TNS_W4] ||
      high_score_impl_->tap_note_scores[TNS_W5]) {
    return false;
  }
  if (high_score_impl_->hold_note_scores[HNS_Held] > 0) {
    return false;
  }
  return true;
}

RString HighScore::GetName() const { return high_score_impl_->name; }
Grade HighScore::GetGrade() const { return high_score_impl_->grade; }
unsigned int HighScore::GetScore() const { return high_score_impl_->score; }
unsigned int HighScore::GetMaxCombo() const {
  return high_score_impl_->max_combo;
}
StageAward HighScore::GetStageAward() const {
  return high_score_impl_->stage_award;
}
PeakComboAward HighScore::GetPeakComboAward() const {
  return high_score_impl_->peak_combo_award;
}
float HighScore::GetPercentDP() const { return high_score_impl_->percent_dp; }
float HighScore::GetSurviveSeconds() const {
  return high_score_impl_->survive_seconds;
}
float HighScore::GetSurvivalSeconds() const {
  return GetSurviveSeconds() + GetLifeRemainingSeconds();
}
RString HighScore::GetModifiers() const { return high_score_impl_->modifiers; }
DateTime HighScore::GetDateTime() const { return high_score_impl_->date_time; }
RString HighScore::GetPlayerGuid() const {
  return high_score_impl_->player_guid;
}
RString HighScore::GetMachineGuid() const {
  return high_score_impl_->machine_guid;
}
int HighScore::GetProductID() const { return high_score_impl_->product_id; }
int HighScore::GetTapNoteScore(TapNoteScore tns) const {
  return high_score_impl_->tap_note_scores[tns];
}
int HighScore::GetHoldNoteScore(HoldNoteScore hns) const {
  return high_score_impl_->hold_note_scores[hns];
}
const RadarValues& HighScore::GetRadarValues() const {
  return high_score_impl_->radar_values;
}
float HighScore::GetLifeRemainingSeconds() const {
  return high_score_impl_->life_remaining_seconds;
}
bool HighScore::GetDisqualified() const {
  return high_score_impl_->disqualified;
}

void HighScore::SetName(const RString& name) { high_score_impl_->name = name; }
void HighScore::SetGrade(Grade grade) { high_score_impl_->grade = grade; }
void HighScore::SetScore(unsigned int score) {
  high_score_impl_->score = score;
}
void HighScore::SetMaxCombo(unsigned int max_combo) {
  high_score_impl_->max_combo = max_combo;
}
void HighScore::SetStageAward(StageAward stage_award) {
  high_score_impl_->stage_award = stage_award;
}
void HighScore::SetPeakComboAward(PeakComboAward peak_combo_award) {
  high_score_impl_->peak_combo_award = peak_combo_award;
}
void HighScore::SetPercentDP(float percent_dp) {
  high_score_impl_->percent_dp = percent_dp;
}
void HighScore::SetAliveSeconds(float alive_seconds) {
  high_score_impl_->survive_seconds = alive_seconds;
}
void HighScore::SetModifiers(RString modifiers) {
  high_score_impl_->modifiers = modifiers;
}
void HighScore::SetDateTime(DateTime date_time) {
  high_score_impl_->date_time = date_time;
}
void HighScore::SetPlayerGuid(RString player_guid) {
  high_score_impl_->player_guid = player_guid;
}
void HighScore::SetMachineGuid(RString machine_guid) {
  high_score_impl_->machine_guid = machine_guid;
}
void HighScore::SetProductID(int product_id) {
  high_score_impl_->product_id = product_id;
}
void HighScore::SetTapNoteScore(TapNoteScore tns, int i) {
  high_score_impl_->tap_note_scores[tns] = i;
}
void HighScore::SetHoldNoteScore(HoldNoteScore hns, int i) {
  high_score_impl_->hold_note_scores[hns] = i;
}
void HighScore::SetRadarValues(const RadarValues& radar_values) {
  high_score_impl_->radar_values = radar_values;
}
void HighScore::SetLifeRemainingSeconds(float life_remaining_seconds) {
  high_score_impl_->life_remaining_seconds = life_remaining_seconds;
}
void HighScore::SetDisqualified(bool b) { high_score_impl_->disqualified = b; }

// We normally don't give direct access to the members. We need this one for
// NameToFillIn; use a special accessor so it's easy to find where this is used.
RString* HighScore::GetNameMutable() { return &high_score_impl_->name; }

bool HighScore::operator<(const HighScore& other) const {
  // Make sure we treat AAAA as higher than AAA, even though the score is the
  // same.
  if (PREFSMAN->m_bPercentageScoring) {
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

bool HighScore::operator>(const HighScore& other) const {
  return other.operator<(*this);
}

bool HighScore::operator<=(const HighScore& other) const {
  return !operator>(other);
}

bool HighScore::operator>=(const HighScore& other) const {
  return !operator<(other);
}

bool HighScore::operator==(const HighScore& other) const {
  return *high_score_impl_ == *other.high_score_impl_;
}

bool HighScore::operator!=(const HighScore& other) const {
  return !operator==(other);
}

XNode* HighScore::CreateNode() const { return high_score_impl_->CreateNode(); }

void HighScore::LoadFromNode(const XNode* node) {
  high_score_impl_->LoadFromNode(node);
}

RString HighScore::GetDisplayName() const {
  if (GetName().empty()) {
    return EMPTY_NAME;
  } else {
    return GetName();
  }
}

// Begin HighScoreList.
void HighScoreList::Init() {
  num_times_played_ = 0;
  high_scores_.clear();
  high_grade_ = Grade_NoData;
}

void HighScoreList::AddHighScore(
    HighScore high_score, int& index_out, bool is_machine) {
  int i;
  for (i = 0; i < (int)high_scores_.size(); ++i) {
    if (high_score >= high_scores_[i]) {
      break;
    }
  }
  const int iMaxScores = is_machine
                             ? PREFSMAN->m_iMaxHighScoresPerListForMachine
                             : PREFSMAN->m_iMaxHighScoresPerListForPlayer;
  if (i < iMaxScores) {
    high_scores_.insert(high_scores_.begin() + i, high_score);
    index_out = i;

    // Delete extra machine high scores in RemoveAllButOneOfEachNameAndClampSize
    // and not here so that we don't end up with less than iMaxScores after
    // removing HighScores with duplicate names.
    if (!is_machine) {
      ClampSize(is_machine);
    }
  }
  high_grade_ = std::min(high_score.GetGrade(), high_grade_);
}

void HighScoreList::IncrementPlayCount(DateTime last_played) {
  last_played_ = last_played;
  num_times_played_++;
}

const HighScore& HighScoreList::GetTopScore() const {
  if (high_scores_.empty()) {
    static HighScore high_Score;
    high_Score = HighScore();
    return high_Score;
  } else {
    return high_scores_[0];
  }
}

XNode* HighScoreList::CreateNode() const {
  XNode* node = new XNode("HighScoreList");

  node->AppendChild("NumTimesPlayed", num_times_played_);
  node->AppendChild("LastPlayed", last_played_.GetString());
  if (high_grade_ != Grade_NoData) {
    node->AppendChild("HighGrade", GradeToString(high_grade_));
  }

  for (unsigned i = 0; i < high_scores_.size(); ++i) {
    const HighScore& high_score = high_scores_[i];
    node->AppendChild(high_score.CreateNode());
  }

  return node;
}

void HighScoreList::LoadFromNode(const XNode* high_score_list) {
  Init();

  ASSERT(high_score_list->GetName() == "HighScoreList");
  FOREACH_CONST_Child(high_score_list, p) {
    const RString& name = p->GetName();
    if (name == "NumTimesPlayed") {
      p->GetTextValue(num_times_played_);
    } else if (name == "LastPlayed") {
      RString last_played_str;
      p->GetTextValue(last_played_str);
      last_played_.FromString(last_played_str);
    } else if (name == "HighGrade") {
      RString grade_str;
      p->GetTextValue(grade_str);
      high_grade_ = StringToGrade(grade_str);
    } else if (name == "HighScore") {
      high_scores_.resize(high_scores_.size() + 1);
      high_scores_.back().LoadFromNode(p);

      // Ignore all high scores that are 0.
      if (high_scores_.back().GetScore() == 0) {
        high_scores_.pop_back();
      } else {
        high_grade_ = std::min(high_scores_.back().GetGrade(), high_grade_);
      }
    }
  }
}

void HighScoreList::RemoveAllButOneOfEachName() {
  for (auto i = high_scores_.begin(); i != high_scores_.end(); ++i) {
    for (auto j = i + 1; j != high_scores_.end(); j++) {
      if (i->GetName() == j->GetName()) {
        j--;
        high_scores_.erase(j + 1);
      }
    }
  }
}

void HighScoreList::ClampSize(bool is_machine) {
  const int max_scores = is_machine
                             ? PREFSMAN->m_iMaxHighScoresPerListForMachine
                             : PREFSMAN->m_iMaxHighScoresPerListForPlayer;
  if (high_scores_.size() > unsigned(max_scores)) {
    high_scores_.erase(high_scores_.begin() + max_scores, high_scores_.end());
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
  // NOTE(Kyz): Remove non-unique scores because they probably come from an
  // accidental repeated merge.
  auto unique_end = std::unique(high_scores_.begin(), high_scores_.end());
  high_scores_.erase(unique_end, high_scores_.end());
  // Reverse it because sort moved the lesser scores to the top.
  std::reverse(high_scores_.begin(), high_scores_.end());
  ClampSize(is_machine);
}

XNode* Screenshot::CreateNode() const {
  XNode* node = new XNode("Screenshot");

  // TRICKY:  Don't write "name to fill in" markers.
  node->AppendChild("FileName", file_name);
  node->AppendChild("MD5", md5);
  node->AppendChild(high_score.CreateNode());

  return node;
}

void Screenshot::LoadFromNode(const XNode* node) {
  ASSERT(node->GetName() == "Screenshot");

  node->GetChildValue("FileName", file_name);
  node->GetChildValue("MD5", md5);
  const XNode* high_score_node = node->GetChild("HighScore");
  if (high_score_node) {
    high_score.LoadFromNode(high_score_node);
  }
}

// lua start
#include "LuaBinding.h"

// Allow Lua to have access to the HighScore.
class LunaHighScore : public Luna<HighScore> {
 public:
  static int GetName(T* p, lua_State* L) {
    lua_pushstring(L, p->GetName());
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
    lua_pushstring(L, p->GetDateTime().GetString());
    return 1;
  }
  static int GetSurvivalSeconds(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetSurvivalSeconds());
    return 1;
  }
  static int IsFillInMarker(T* p, lua_State* L) {
    bool is_fill_in_marker = false;
    FOREACH_PlayerNumber(pn)
			is_fill_in_marker |= p->GetName() == RANKING_TO_FILL_IN_MARKER[pn];
    lua_pushboolean(L, is_fill_in_marker);
    return 1;
  }
  static int GetMaxCombo(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetMaxCombo());
    return 1;
  }
  static int GetModifiers(T* p, lua_State* L) {
    lua_pushstring(L, p->GetModifiers());
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
    RString name = SArg(1);
    for (std::size_t i = 0; i < p->high_scores_.size(); ++i) {
      if (name == p->high_scores_[i].GetName()) {
        p->high_scores_[i].PushSelf(L);
        return 1;
      }
    }
    lua_pushnil(L);
    return 1;
  }

  static int GetRankOfName(T* p, lua_State* L) {
    RString name = SArg(1);
    std::size_t rank = 0;
    for (std::size_t i = 0; i < p->high_scores_.size(); ++i) {
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
