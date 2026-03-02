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
  std::string sName;  // name that shows in the machine's ranking screen
  Grade grade;
  unsigned int iScore;
  float fPercentDP;
  float fSurviveSeconds;
  unsigned int iMaxCombo;         // maximum combo obtained [SM5 alpha 1a+]
  StageAward stageAward;          // stage award [SM5 alpha 1a+]
  PeakComboAward peakComboAward;  // peak combo award [SM5 alpha 1a+]
  std::string sModifiers;
  DateTime dateTime;         // return value of time() when screenshot was taken
  std::string sPlayerGuid;   // who made this high score
  std::string sMachineGuid;  // where this high score was made
  int iProductID;
  int iTapNoteScores[NUM_TapNoteScore];
  int iHoldNoteScores[NUM_HoldNoteScore];
  RadarValues radarValues;
  float fLifeRemainingSeconds;
  bool bDisqualified;
  bool bIsRoutine;

  // Additional player-specific attributes for HighScore
  std::vector<std::string> playerNames;
  std::vector<Grade> playerGrades;
  std::vector<unsigned int> playerScores;
  std::vector<float> playerPercentDPs;
  std::vector<unsigned int> playerMaxCombos;
  std::vector<std::string> playerGuids;
  std::vector<std::array<int, NUM_TapNoteScore>> playerTapNoteScores;
  std::vector<std::array<int, NUM_HoldNoteScore>> playerHoldNoteScores;

  HighScoreImpl();
  XNode* CreateNode() const;
  void LoadFromNode(const XNode* pNode);

  bool operator==(const HighScoreImpl& other) const;
  bool operator!=(const HighScoreImpl& other) const {
    return !(*this == other);
  }
};

bool HighScoreImpl::operator==(const HighScoreImpl& other) const {
  if (sName != other.sName) {
    return false;
  }
  if (grade != other.grade) {
    return false;
  }
  if (iScore != other.iScore) {
    return false;
  }
  if (iMaxCombo != other.iMaxCombo) {
    return false;
  }
  if (stageAward != other.stageAward) {
    return false;
  }
  if (peakComboAward != other.peakComboAward) {
    return false;
  }
  if (fPercentDP != other.fPercentDP) {
    return false;
  }
  if (fSurviveSeconds != other.fSurviveSeconds) {
    return false;
  }
  if (sModifiers != other.sModifiers) {
    return false;
  }
  if (dateTime != other.dateTime) {
    return false;
  }
  if (sPlayerGuid != other.sPlayerGuid) {
    return false;
  }
  if (sMachineGuid != other.sMachineGuid) {
    return false;
  }
  if (iProductID != other.iProductID) {
    return false;
  }
  FOREACH_ENUM(TapNoteScore, tns) {
    if (iTapNoteScores[tns] != other.iTapNoteScores[tns]) {
      return false;
    }
  }
  FOREACH_ENUM(HoldNoteScore, hns) {
    if (iHoldNoteScores[hns] != other.iHoldNoteScores[hns]) {
      return false;
    }
  }
  if (radarValues != other.radarValues) {
    return false;
  }
  if (fLifeRemainingSeconds != other.fLifeRemainingSeconds) {
    return false;
  }
  if (bDisqualified != other.bDisqualified) {
    return false;
  }
  return true;
}

HighScoreImpl::HighScoreImpl() {
  sName = "";
  grade = Grade_NoData;
  iScore = 0;
  fPercentDP = 0;
  fSurviveSeconds = 0;
  iMaxCombo = 0;
  stageAward = StageAward_Invalid;
  peakComboAward = PeakComboAward_Invalid;
  sModifiers = "";
  dateTime.Init();
  sPlayerGuid = "";
  sMachineGuid = "";
  iProductID = 0;
  ZERO(iTapNoteScores);
  ZERO(iHoldNoteScores);
  radarValues.MakeUnknown();
  fLifeRemainingSeconds = 0;
  bIsRoutine = false;

  playerNames = std::vector<std::string>(NUM_PLAYERS, "");
  playerGrades = std::vector<Grade>(NUM_PLAYERS, Grade_NoData);
  playerScores = std::vector<unsigned int>(NUM_PLAYERS, 0);
  playerPercentDPs = std::vector<float>(NUM_PLAYERS, 0);
  playerMaxCombos = std::vector<unsigned int>(NUM_PLAYERS, 0);
  playerGuids = std::vector<std::string>(NUM_PLAYERS, "");

  playerTapNoteScores.resize(NUM_PLAYERS, std::array<int, NUM_TapNoteScore>{});
  playerHoldNoteScores.resize(
      NUM_PLAYERS, std::array<int, NUM_HoldNoteScore>{});

  // Initialize each array to zeros
  for (auto& scoresArray : playerTapNoteScores) {
    std::fill(scoresArray.begin(), scoresArray.end(), 0);
  }
  for (auto& scoresArray : playerHoldNoteScores) {
    std::fill(scoresArray.begin(), scoresArray.end(), 0);
  }
}

XNode* HighScoreImpl::CreateNode() const {
  XNode* pNode = new XNode("HighScore");
  const bool bWriteSimpleValues = RadarValues::WRITE_SIMPLE_VALIES;
  const bool bWriteComplexValues = RadarValues::WRITE_COMPLEX_VALIES;

  // TRICKY:  Don't write "name to fill in" markers.
  pNode->AppendChild(
      "Name", IsRankingToFillIn(sName) ? std::string("") : sName);
  pNode->AppendChild("Grade", GradeToString(grade));
  pNode->AppendChild("Score", iScore);
  pNode->AppendChild("PercentDP", fPercentDP);
  pNode->AppendChild("SurviveSeconds", fSurviveSeconds);
  pNode->AppendChild("MaxCombo", iMaxCombo);
  pNode->AppendChild("StageAward", StageAwardToString(stageAward));
  pNode->AppendChild("PeakComboAward", PeakComboAwardToString(peakComboAward));
  pNode->AppendChild("Modifiers", sModifiers);
  pNode->AppendChild("DateTime", dateTime.GetString());
  pNode->AppendChild("PlayerGuid", sPlayerGuid);
  pNode->AppendChild("MachineGuid", sMachineGuid);
  pNode->AppendChild("ProductID", iProductID);
  XNode* pTapNoteScores = pNode->AppendChild("TapNoteScores");
  FOREACH_ENUM(TapNoteScore, tns)
  if (tns != TNS_None) {  // HACK: don't save meaningless "none" count
    pTapNoteScores->AppendChild(TapNoteScoreToString(tns), iTapNoteScores[tns]);
  }
  XNode* pHoldNoteScores = pNode->AppendChild("HoldNoteScores");
  FOREACH_ENUM(HoldNoteScore, hns)
  if (hns != HNS_None) {  // HACK: don't save meaningless "none" count
    pHoldNoteScores->AppendChild(
        HoldNoteScoreToString(hns), iHoldNoteScores[hns]);
  }
  pNode->AppendChild(
      radarValues.CreateNode(bWriteSimpleValues, bWriteComplexValues));
  pNode->AppendChild("LifeRemainingSeconds", fLifeRemainingSeconds);
  pNode->AppendChild("Disqualified", bDisqualified);
  if (bIsRoutine) {
    XNode* pRoutineNode = pNode->AppendChild("RoutineData");
    // Loop through each player and add their specific data to "RoutineData"
    for (size_t i = 0; i < playerNames.size(); ++i) {
      XNode* pPlayerNode =
          pRoutineNode->AppendChild(PlayerNumberToString((PlayerNumber)i));
      pPlayerNode->AppendChild("Name", playerNames[i]);
      pPlayerNode->AppendChild("Grade", playerGrades[i]);
      pPlayerNode->AppendChild("Score", playerScores[i]);
      pPlayerNode->AppendChild("PercentDP", playerPercentDPs[i]);
      pPlayerNode->AppendChild("MaxCombo", playerMaxCombos[i]);
      pPlayerNode->AppendChild("PlayerGuid", playerGuids[i]);

      XNode* pRoutineTapNoteScores = pPlayerNode->AppendChild("TapNoteScores");
      FOREACH_ENUM(TapNoteScore, tns)
      if (tns != TNS_None) {  // HACK: don't save meaningless "none" count
        pRoutineTapNoteScores->AppendChild(
            TapNoteScoreToString(tns), playerTapNoteScores[i][tns]);
      }
      XNode* pRoutineHoldNoteScores =
          pPlayerNode->AppendChild("HoldNoteScores");
      FOREACH_ENUM(HoldNoteScore, hns)
      if (hns != HNS_None) {  // HACK: don't save meaningless "none" count
        pRoutineHoldNoteScores->AppendChild(
            HoldNoteScoreToString(hns), playerHoldNoteScores[i][hns]);
      }
    }
  }
  return pNode;
}

void HighScoreImpl::LoadFromNode(const XNode* pNode) {
  ASSERT(pNode->GetName() == "HighScore");

  std::string s;

  pNode->GetChildValue("Name", sName);
  pNode->GetChildValue("Grade", s);
  grade = StringToGrade(s);
  pNode->GetChildValue("Score", iScore);
  pNode->GetChildValue("PercentDP", fPercentDP);
  pNode->GetChildValue("SurviveSeconds", fSurviveSeconds);
  pNode->GetChildValue("MaxCombo", iMaxCombo);
  pNode->GetChildValue("StageAward", s);
  stageAward = StringToStageAward(s);
  pNode->GetChildValue("PeakComboAward", s);
  peakComboAward = StringToPeakComboAward(s);
  pNode->GetChildValue("Modifiers", sModifiers);
  pNode->GetChildValue("DateTime", s);
  dateTime.FromString(s);
  pNode->GetChildValue("PlayerGuid", sPlayerGuid);
  pNode->GetChildValue("MachineGuid", sMachineGuid);
  pNode->GetChildValue("ProductID", iProductID);
  const XNode* pTapNoteScores = pNode->GetChild("TapNoteScores");
  if (pTapNoteScores) {
    FOREACH_ENUM(TapNoteScore, tns)
    pTapNoteScores->GetChildValue(
        TapNoteScoreToString(tns), iTapNoteScores[tns]);
  }
  const XNode* pHoldNoteScores = pNode->GetChild("HoldNoteScores");
  if (pHoldNoteScores) {
    FOREACH_ENUM(HoldNoteScore, hns)
    pHoldNoteScores->GetChildValue(
        HoldNoteScoreToString(hns), iHoldNoteScores[hns]);
  }
  const XNode* pRadarValues = pNode->GetChild("RadarValues");
  if (pRadarValues) {
    radarValues.LoadFromNode(pRadarValues);
  }
  pNode->GetChildValue("LifeRemainingSeconds", fLifeRemainingSeconds);
  pNode->GetChildValue("Disqualified", bDisqualified);

  // Validate input.
  grade = std::clamp(grade, Grade_Tier01, Grade_Failed);
  const XNode* pRoutineNode = pNode->GetChild("RoutineData");

  if (pRoutineNode) {
    // Load player-specific data
    bIsRoutine = true;
    FOREACH_PlayerNumber(pn) {
      const XNode* pPlayerNode =
          pRoutineNode->GetChild(PlayerNumberToString(pn));
      if (pPlayerNode) {
        std::string gradeStr;
        pPlayerNode->GetChildValue("Name", playerNames[pn]);
        pPlayerNode->GetChildValue("Grade", gradeStr);
        playerGrades[pn] = StringToGrade(gradeStr);
        pPlayerNode->GetChildValue("Score", playerScores[pn]);
        LOG->Trace("Loaded player score %d", playerScores[pn]);
        pPlayerNode->GetChildValue("PercentDP", playerPercentDPs[pn]);
        LOG->Trace("Loaded player percentDP %f", playerPercentDPs[pn]);
        pPlayerNode->GetChildValue("MaxCombo", playerMaxCombos[pn]);
        LOG->Trace("Loaded player maxCombo %d", playerMaxCombos[pn]);
        pPlayerNode->GetChildValue("PlayerGuid", playerGuids[pn]);

        const XNode* pPTapNoteScores = pPlayerNode->GetChild("TapNoteScores");
        if (pPTapNoteScores) {
          FOREACH_ENUM(TapNoteScore, tns) {
            pPTapNoteScores->GetChildValue(
                TapNoteScoreToString(tns), playerTapNoteScores[pn][tns]);
          }
        }

        const XNode* pPHoldNoteScores = pPlayerNode->GetChild("HoldNoteScores");
        if (pPHoldNoteScores) {
          FOREACH_ENUM(HoldNoteScore, hns) {
            pPHoldNoteScores->GetChildValue(
                HoldNoteScoreToString(hns), playerHoldNoteScores[pn][hns]);
          }
        }

        LOG->Trace("Loaded player-specific data for player %d", pn);
      }
    }
  } else {
    bIsRoutine = false;
  }
}

REGISTER_CLASS_TRAITS(HighScoreImpl, new HighScoreImpl(*pCopy))

HighScore::HighScore() { m_Impl = new HighScoreImpl; }

void HighScore::Unset() { m_Impl = new HighScoreImpl; }

bool HighScore::IsEmpty() const {
  if (m_Impl->iTapNoteScores[TNS_W1] || m_Impl->iTapNoteScores[TNS_W2] ||
      m_Impl->iTapNoteScores[TNS_W3] || m_Impl->iTapNoteScores[TNS_W4] ||
      m_Impl->iTapNoteScores[TNS_W5]) {
    return false;
  }
  if (m_Impl->iHoldNoteScores[HNS_Held] > 0) {
    return false;
  }
  return true;
}

std::string HighScore::GetName() const { return m_Impl->sName; }
Grade HighScore::GetGrade() const { return m_Impl->grade; }
unsigned int HighScore::GetScore() const { return m_Impl->iScore; }
unsigned int HighScore::GetMaxCombo() const { return m_Impl->iMaxCombo; }
StageAward HighScore::GetStageAward() const { return m_Impl->stageAward; }
PeakComboAward HighScore::GetPeakComboAward() const {
  return m_Impl->peakComboAward;
}
float HighScore::GetPercentDP() const { return m_Impl->fPercentDP; }
float HighScore::GetSurviveSeconds() const { return m_Impl->fSurviveSeconds; }
float HighScore::GetSurvivalSeconds() const {
  return GetSurviveSeconds() + GetLifeRemainingSeconds();
}
std::string HighScore::GetModifiers() const { return m_Impl->sModifiers; }
DateTime HighScore::GetDateTime() const { return m_Impl->dateTime; }
std::string HighScore::GetPlayerGuid() const { return m_Impl->sPlayerGuid; }
std::string HighScore::GetMachineGuid() const { return m_Impl->sMachineGuid; }
int HighScore::GetProductID() const { return m_Impl->iProductID; }
int HighScore::GetTapNoteScore(TapNoteScore tns) const {
  return m_Impl->iTapNoteScores[tns];
}
int HighScore::GetHoldNoteScore(HoldNoteScore hns) const {
  return m_Impl->iHoldNoteScores[hns];
}
const RadarValues& HighScore::GetRadarValues() const {
  return m_Impl->radarValues;
}
float HighScore::GetLifeRemainingSeconds() const {
  return m_Impl->fLifeRemainingSeconds;
}
bool HighScore::GetDisqualified() const { return m_Impl->bDisqualified; }

void HighScore::SetName(const std::string& sName) { m_Impl->sName = sName; }
void HighScore::SetGrade(Grade g) { m_Impl->grade = g; }
void HighScore::SetScore(unsigned int iScore) { m_Impl->iScore = iScore; }
void HighScore::SetMaxCombo(unsigned int i) { m_Impl->iMaxCombo = i; }
void HighScore::SetStageAward(StageAward a) { m_Impl->stageAward = a; }
void HighScore::SetPeakComboAward(PeakComboAward a) {
  m_Impl->peakComboAward = a;
}
void HighScore::SetPercentDP(float f) { m_Impl->fPercentDP = f; }
void HighScore::SetAliveSeconds(float f) { m_Impl->fSurviveSeconds = f; }
void HighScore::SetModifiers(std::string s) { m_Impl->sModifiers = s; }
void HighScore::SetDateTime(DateTime d) { m_Impl->dateTime = d; }
void HighScore::SetPlayerGuid(std::string s) { m_Impl->sPlayerGuid = s; }
void HighScore::SetMachineGuid(std::string s) { m_Impl->sMachineGuid = s; }
void HighScore::SetProductID(int i) { m_Impl->iProductID = i; }
void HighScore::SetTapNoteScore(TapNoteScore tns, int i) {
  m_Impl->iTapNoteScores[tns] = i;
}
void HighScore::SetHoldNoteScore(HoldNoteScore hns, int i) {
  m_Impl->iHoldNoteScores[hns] = i;
}
void HighScore::SetRadarValues(const RadarValues& rv) {
  m_Impl->radarValues = rv;
}
void HighScore::SetLifeRemainingSeconds(float f) {
  m_Impl->fLifeRemainingSeconds = f;
}
void HighScore::SetDisqualified(bool b) { m_Impl->bDisqualified = b; }

void HighScore::SetRoutine(bool b) { m_Impl->bIsRoutine = b; }

// Getters
std::string HighScore::GetPlayerName(const PlayerNumber& playerNum) const {
  return m_Impl->playerNames.at(playerNum);
}

std::string HighScore::GetPlayerGrade(const PlayerNumber& playerNum) const {
  return GradeToString(m_Impl->playerGrades.at(playerNum));
}

unsigned int HighScore::GetPlayerScore(const PlayerNumber& playerNum) const {
  return m_Impl->playerScores.at(playerNum);
}

float HighScore::GetPlayerPercentDP(const PlayerNumber& playerNum) const {
  return m_Impl->playerPercentDPs.at(playerNum);
}

unsigned int HighScore::GetPlayerMaxCombo(const PlayerNumber& playerNum) const {
  return m_Impl->playerMaxCombos.at(playerNum);
}

std::string HighScore::GetPlayerGuid(const PlayerNumber& playerNum) const {
  return m_Impl->playerGuids.at(playerNum);
}

// Assuming TapNoteScores and HoldNoteScores are stored in a similar format to
// HighScoreImpl
int HighScore::GetPlayerTapNoteScore(
    const PlayerNumber& playerNum, TapNoteScore tns) const {
  return m_Impl->playerTapNoteScores.at(playerNum)[tns];
}

int HighScore::GetPlayerHoldNoteScore(
    const PlayerNumber& playerNum, HoldNoteScore hns) const {
  return m_Impl->playerHoldNoteScores.at(playerNum)[hns];
}

// Setters
void HighScore::SetPlayerName(
    const PlayerNumber& playerNum, const std::string& name) {
  m_Impl->playerNames[playerNum] = name;
}

void HighScore::SetPlayerGrade(const PlayerNumber& playerNum, Grade grade) {
  m_Impl->playerGrades[playerNum] = grade;
}

void HighScore::SetPlayerScore(
    const PlayerNumber& playerNum, unsigned int score) {
  m_Impl->playerScores[playerNum] = score;
}

void HighScore::SetPlayerPercentDP(
    const PlayerNumber& playerNum, float percentDP) {
  m_Impl->playerPercentDPs[playerNum] = percentDP;
}

void HighScore::SetPlayerMaxCombo(
    const PlayerNumber& playerNum, unsigned int maxCombo) {
  m_Impl->playerMaxCombos[playerNum] = maxCombo;
}

void HighScore::SetPlayerGuid(
    const PlayerNumber& playerNum, const std::string& guid) {
  m_Impl->playerGuids[playerNum] = guid;
}

// Assuming TapNoteScores and HoldNoteScores are stored in a similar format to
// HighScoreImpl
void HighScore::SetPlayerTapNoteScore(
    const PlayerNumber& playerNum, TapNoteScore tns, int score) {
  m_Impl->playerTapNoteScores[playerNum][tns] = score;
}

void HighScore::SetPlayerHoldNoteScore(
    const PlayerNumber& playerNum, HoldNoteScore hns, int score) {
  m_Impl->playerHoldNoteScores[playerNum][hns] = score;
}

/* We normally don't give direct access to the members.  We need this one
 * for NameToFillIn; use a special accessor so it's easy to find where this
 * is used. */
std::string* HighScore::GetNameMutable() { return &m_Impl->sName; }

bool HighScore::operator<(const HighScore& other) const {
  /* Make sure we treat AAAA as higher than AAA, even though the score
   * is the same. */
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
  return *m_Impl == *other.m_Impl;
}

bool HighScore::operator!=(const HighScore& other) const {
  return !operator==(other);
}

XNode* HighScore::CreateNode() const { return m_Impl->CreateNode(); }

void HighScore::LoadFromNode(const XNode* pNode) {
  m_Impl->LoadFromNode(pNode);
}

std::string HighScore::GetDisplayName() const {
  if (GetName().empty()) {
    return EMPTY_NAME;
  } else {
    return GetName();
  }
}

/* begin HighScoreList */
void HighScoreList::Init() {
  iNumTimesPlayed = 0;
  vHighScores.clear();
  HighGrade = Grade_NoData;
}

void HighScoreList::AddHighScore(
    HighScore hs, int& iIndexOut, bool bIsMachine) {
  int i;
  for (i = 0; i < (int)vHighScores.size(); i++) {
    if (hs >= vHighScores[i]) {
      break;
    }
  }
  const int iMaxScores = bIsMachine
                             ? PREFSMAN->m_iMaxHighScoresPerListForMachine
                             : PREFSMAN->m_iMaxHighScoresPerListForPlayer;
  if (i < iMaxScores) {
    vHighScores.insert(vHighScores.begin() + i, hs);
    iIndexOut = i;

    // Delete extra machine high scores in RemoveAllButOneOfEachNameAndClampSize
    // and not here so that we don't end up with less than iMaxScores after
    // removing HighScores with duplicate names.
    //
    if (!bIsMachine) {
      ClampSize(bIsMachine);
    }
  }
  HighGrade = std::min(hs.GetGrade(), HighGrade);
}

void HighScoreList::IncrementPlayCount(DateTime _dtLastPlayed) {
  dtLastPlayed = _dtLastPlayed;
  iNumTimesPlayed++;
}

const HighScore& HighScoreList::GetTopScore() const {
  if (vHighScores.empty()) {
    static HighScore hs;
    hs = HighScore();
    return hs;
  } else {
    return vHighScores[0];
  }
}

XNode* HighScoreList::CreateNode() const {
  XNode* pNode = new XNode("HighScoreList");

  pNode->AppendChild("NumTimesPlayed", iNumTimesPlayed);
  pNode->AppendChild("LastPlayed", dtLastPlayed.GetString());
  if (HighGrade != Grade_NoData) {
    pNode->AppendChild("HighGrade", GradeToString(HighGrade));
  }

  for (unsigned i = 0; i < vHighScores.size(); i++) {
    const HighScore& hs = vHighScores[i];
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
      p->GetTextValue(iNumTimesPlayed);
    } else if (name == "LastPlayed") {
      std::string s;
      p->GetTextValue(s);
      dtLastPlayed.FromString(s);
    } else if (name == "HighGrade") {
      std::string s;
      p->GetTextValue(s);
      HighGrade = StringToGrade(s);
    } else if (name == "HighScore") {
      vHighScores.resize(vHighScores.size() + 1);
      vHighScores.back().LoadFromNode(p);

      HighGrade = std::min(vHighScores.back().GetGrade(), HighGrade);
    }
  }
}

void HighScoreList::RemoveAllButOneOfEachName() {
  for (std::vector<HighScore>::iterator i = vHighScores.begin();
       i != vHighScores.end(); ++i) {
    for (std::vector<HighScore>::iterator j = i + 1; j != vHighScores.end();
         j++) {
      if (i->GetName() == j->GetName()) {
        j--;
        vHighScores.erase(j + 1);
      }
    }
  }
}

void HighScoreList::ClampSize(bool bIsMachine) {
  const int iMaxScores = bIsMachine
                             ? PREFSMAN->m_iMaxHighScoresPerListForMachine
                             : PREFSMAN->m_iMaxHighScoresPerListForPlayer;
  if (vHighScores.size() > unsigned(iMaxScores)) {
    vHighScores.erase(vHighScores.begin() + iMaxScores, vHighScores.end());
  }
}

void HighScoreList::MergeFromOtherHSL(HighScoreList& other, bool is_machine) {
  iNumTimesPlayed += other.iNumTimesPlayed;
  if (other.dtLastPlayed > dtLastPlayed) {
    dtLastPlayed = other.dtLastPlayed;
  }
  if (other.HighGrade > HighGrade) {
    HighGrade = other.HighGrade;
  }
  vHighScores.insert(
      vHighScores.end(), other.vHighScores.begin(), other.vHighScores.end());
  std::sort(vHighScores.begin(), vHighScores.end());
  // Remove non-unique scores because they probably come from an accidental
  // repeated merge. -Kyz
  std::vector<HighScore>::iterator unique_end =
      std::unique(vHighScores.begin(), vHighScores.end());
  vHighScores.erase(unique_end, vHighScores.end());
  // Reverse it because sort moved the lesser scores to the top.
  std::reverse(vHighScores.begin(), vHighScores.end());

  if (!PREFSMAN->m_bAllowMultipleHighScoreWithSameName) {
    // erase all but the highest score for each name
    RemoveAllButOneOfEachName();
  }
  ClampSize(is_machine);
}

XNode* Screenshot::CreateNode() const {
  XNode* pNode = new XNode("Screenshot");

  // TRICKY:  Don't write "name to fill in" markers.
  pNode->AppendChild("FileName", sFileName);
  pNode->AppendChild("MD5", sMD5);
  pNode->AppendChild(highScore.CreateNode());

  return pNode;
}

void Screenshot::LoadFromNode(const XNode* pNode) {
  ASSERT(pNode->GetName() == "Screenshot");

  pNode->GetChildValue("FileName", sFileName);
  pNode->GetChildValue("MD5", sMD5);
  const XNode* pHighScore = pNode->GetChild("HighScore");
  if (pHighScore) {
    highScore.LoadFromNode(pHighScore);
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
    for (int i = 0; i < (int)p->vHighScores.size(); ++i) {
      p->vHighScores[i].PushSelf(L);
      lua_rawseti(L, -2, i + 1);
    }

    return 1;
  }

  static int GetHighestScoreOfName(T* p, lua_State* L) {
    std::string name = SArg(1);
    for (size_t i = 0; i < p->vHighScores.size(); ++i) {
      if (name == p->vHighScores[i].GetName()) {
        p->vHighScores[i].PushSelf(L);
        return 1;
      }
    }
    lua_pushnil(L);
    return 1;
  }

  static int GetRankOfName(T* p, lua_State* L) {
    std::string name = SArg(1);
    size_t rank = 0;
    for (size_t i = 0; i < p->vHighScores.size(); ++i) {
      if (name == p->vHighScores[i].GetName()) {
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
