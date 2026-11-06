#include "UnlockManager.h"

#include <algorithm>
#include <cfloat>
#include <set>
#include <string>
#include <vector>

#include "Command.h"
#include "CommonMetrics.h"
#include "Course.h"
#include "Difficulty.h"
#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"  // StepsTypeToString
#include "GameManager.h"
#include "GameState.h"
#include "Grade.h"
#include "LuaManager.h"
#include "PrefsManager.h"
#include "Profile.h"
#include "ProfileManager.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "Song.h"
#include "SongManager.h"
#include "SongUtil.h"
#include "StdString.h"
#include "Steps.h"
#include "Style.h"
#include "ThemeManager.h"
#include "ThemeMetric.h"
#include "global.h"

UnlockManager* UNLOCKMAN =
    nullptr;  // global and accessible from anywhere in our program

#define UNLOCK_NAMES THEME->GetMetric("UnlockManager", "UnlockNames")
#define UNLOCK(x) \
  THEME->GetMetricR("UnlockManager", ssprintf("Unlock%sCommand", x.c_str()));

static ThemeMetric<bool> AUTO_LOCK_CHALLENGE_STEPS(
    "UnlockManager", "AutoLockChallengeSteps");
static ThemeMetric<bool> AUTO_LOCK_EDIT_STEPS(
    "UnlockManager", "AutoLockEditSteps");

static const char* UnlockRequirementNames[] = {
    "ArcadePoints", "DancePoints", "SongPoints",    "ExtraCleared",
    "ExtraFailed",  "Toasties",    "StagesCleared", "NumberUnlocked"};
XToString(UnlockRequirement);
StringToX(UnlockRequirement);
LuaXType(UnlockRequirement);

static const char* UnlockRewardTypeNames[] = {
    "Song", "Steps", "StepsType", "Course", "Modifier",
};
XToString(UnlockRewardType);
XToLocalizedString(UnlockRewardType);
LuaXType(UnlockRewardType);
LuaFunction(
    UnlockRewardTypeToLocalizedString,
    UnlockRewardTypeToLocalizedString(Enum::Check<UnlockRewardType>(L, 1)));

UnlockManager::UnlockManager() {
  // Register with Lua.
  {
    Lua* L = LUA->Get();
    lua_pushstring(L, "UNLOCKMAN");
    this->PushSelf(L);
    lua_settable(L, LUA_GLOBALSINDEX);
    LUA->Release(L);
  }

  UNLOCKMAN = this;

  Load();
}

UnlockManager::~UnlockManager() {
  // Unregister with Lua.
  LUA->UnsetGlobal("UNLOCKMAN");
}

std::string UnlockManager::FindEntryID(const std::string& sName) const {
  const UnlockEntry* pEntry = nullptr;

  const Song* pSong = SONGMAN->FindSong(sName);
  if (pSong != nullptr) {
    pEntry = FindSong(pSong);
  }

  const Course* pCourse = SONGMAN->FindCourse(sName);
  if (pCourse != nullptr) {
    pEntry = FindCourse(pCourse);
  }

  if (pEntry == nullptr) {
    pEntry = FindModifier(sName);
  }

  if (pEntry == nullptr) {
    LuaHelpers::ReportScriptErrorFmt(
        "Couldn't find locked entry \"%s\"", sName.c_str());
    return "";
  }

  return pEntry->entry_id_;
}

int UnlockManager::CourseIsLocked(const Course* pCourse) const {
  int iRet = 0;

  if (PREFSMAN->use_unlock_system_) {
    const UnlockEntry* p = FindCourse(pCourse);
    if (p == nullptr) {
      return false;
    }
    if (p->IsLocked()) {
      iRet |= LOCKED_LOCK;
    }
  }

  /* If a course uses a song that is disabled, disable the course too. */
  for (const CourseEntry& ce : pCourse->entries_) {
    const Song* pSong = ce.song_id_.ToSong();
    if (pSong == nullptr) {
      continue;
    }
    int iSongLock = SongIsLocked(pSong);
    if (iSongLock & LOCKED_DISABLED) {
      iRet |= LOCKED_DISABLED;
    }
  }

  return iRet;
}

int UnlockManager::SongIsLocked(const Song* pSong) const {
  int iRet = 0;
  if (PREFSMAN->use_unlock_system_) {
    const UnlockEntry* p = FindSong(pSong);
    if (p != nullptr && p->IsLocked()) {
      iRet |= LOCKED_LOCK;
      if (!p->entry_id_.empty() &&
          roulette_codes_.find(p->entry_id_) != roulette_codes_.end()) {
        iRet |= LOCKED_ROULETTE;
      }
    }
  }
  if (PREFSMAN->hidden_songs_ &&
      pSong->selection_display_ == Song::SHOW_NEVER) {
    iRet |= LOCKED_SELECTABLE;
  }

  if (!pSong->enabled_) {
    iRet |= LOCKED_DISABLED;
  }

  return iRet;
}

bool UnlockManager::StepsIsLocked(
    const Song* pSong, const Steps* pSteps) const {
  if (!PREFSMAN->use_unlock_system_) {
    return false;
  }

  const UnlockEntry* p = FindSteps(pSong, pSteps);
  if (p == nullptr) {
    return false;
  }

  return p->IsLocked();
}

bool UnlockManager::ModifierIsLocked(const std::string& sOneMod) const {
  if (!PREFSMAN->use_unlock_system_) {
    return false;
  }

  const UnlockEntry* p = FindModifier(sOneMod);
  if (p == nullptr) {
    return false;
  }

  return p->IsLocked();
}

const UnlockEntry* UnlockManager::FindSong(const Song* pSong) const {
  for (const UnlockEntry& e : unlock_entries_) {
    if (e.song_.ToSong() == pSong && e.dc_ == Difficulty_Invalid) {
      return &e;
    }
  }
  return nullptr;
}

const UnlockEntry* UnlockManager::FindSteps(
    const Song* pSong, const Steps* pSteps) const {
  ASSERT(pSong && pSteps);
  for (const UnlockEntry& e : unlock_entries_) {
    if (e.song_.ToSong() == pSong && e.dc_ == pSteps->GetDifficulty()) {
      return &e;
    }
  }
  return nullptr;
}

const UnlockEntry* UnlockManager::FindCourse(const Course* pCourse) const {
  for (const UnlockEntry& e : unlock_entries_) {
    if (e.course_.ToCourse() == pCourse) {
      return &e;
    }
  }
  return nullptr;
}

const UnlockEntry* UnlockManager::FindModifier(
    const std::string& sOneMod) const {
  for (const UnlockEntry& e : unlock_entries_) {
    if (CompareNoCase(e.GetModifier(), sOneMod) == 0) {
      return &e;
    }
  }
  return nullptr;
}

static float GetArcadePoints(const Profile* pProfile) {
  float fAP = 0;

  FOREACH_ENUM(Grade, g) {
    switch (g) {
      case Grade_Tier01:
      case Grade_Tier02:
        fAP += 9 * pProfile->num_stages_passed_by_grade_[g];
        break;
      default:
        fAP += 1 * pProfile->num_stages_passed_by_grade_[g];
        break;

      case Grade_Failed:
      case Grade_NoData:;  // no points
        break;
    }
  }

  FOREACH_ENUM(PlayMode, pm) {
    switch (pm) {
      case PLAY_MODE_NONSTOP:
      case PLAY_MODE_ONI:
      case PLAY_MODE_ENDLESS: {
        fAP += pProfile->num_songs_played_by_play_mode_[pm];
      }
      default:
        break;
    }
  }

  return fAP;
}

// TODO: Make this more flexible for games with many grade tiers. Lua-ize it?
// -Wolfman2000
static float GetSongPoints(const Profile* pProfile) {
  float fSP = 0;

  FOREACH_ENUM(Grade, g) {
    switch (g) {
      case Grade_Tier01: /*AAAA*/
        fSP += 20 * pProfile->num_stages_passed_by_grade_[g];
        break;
      case Grade_Tier02: /*AAA*/
        fSP += 10 * pProfile->num_stages_passed_by_grade_[g];
        break;
      case Grade_Tier03: /*AA*/
        fSP += 5 * pProfile->num_stages_passed_by_grade_[g];
        break;
      case Grade_Tier04: /*A*/
        fSP += 4 * pProfile->num_stages_passed_by_grade_[g];
        break;
      case Grade_Tier05: /*B*/
        fSP += 3 * pProfile->num_stages_passed_by_grade_[g];
        break;
      case Grade_Tier06: /*C*/
        fSP += 2 * pProfile->num_stages_passed_by_grade_[g];
        break;
      case Grade_Tier07: /*D*/
        fSP += 1 * pProfile->num_stages_passed_by_grade_[g];
        break;
      case Grade_Failed:
      case Grade_NoData:
      default:;  // no points
        break;
    }
  }

  FOREACH_ENUM(PlayMode, pm) {
    switch (pm) {
      case PLAY_MODE_NONSTOP:
      case PLAY_MODE_ONI:
      case PLAY_MODE_ENDLESS: {
        fSP += pProfile->num_songs_played_by_play_mode_[pm];
      }
      default:
        break;
    }
  }

  return fSP;
}

void UnlockManager::GetPoints(
    const Profile* pProfile, float fScores[NUM_UnlockRequirement]) const {
  fScores[UnlockRequirement_ArcadePoints] = GetArcadePoints(pProfile);
  fScores[UnlockRequirement_SongPoints] = GetSongPoints(pProfile);
  fScores[UnlockRequirement_DancePoints] = (float)pProfile->total_dance_points_;
  fScores[UnlockRequirement_StagesCleared] =
      (float)pProfile->GetTotalNumSongsPassed();
  // fScores[UnlockRequirement_NumUnlocked] = (float) GetNumUnlocked();
}

/* Return true if all songs and/or courses referenced by an unlock are
 * available. */
bool UnlockEntry::IsValid() const {
  switch (type_) {
    case UnlockRewardType_Song:
      return song_.IsValid();

    case UnlockRewardType_Steps:
      return song_.IsValid() && dc_ != Difficulty_Invalid;

    case UnlockRewardType_Steps_Type: {
      return song_.IsValid() && dc_ != Difficulty_Invalid &&
             steps_type_ != StepsType_Invalid;
    }

    case UnlockRewardType_Course:
      return course_.IsValid();

    case UnlockRewardType_Modifier:
      return true;

    default:
      WARN(ssprintf("%i", type_));
      return false;
  }
}

UnlockEntryStatus UnlockEntry::GetUnlockEntryStatus() const {
  std::set<std::string>& ids =
      PROFILEMAN->GetMachineProfile()->unlocked_entry_ids_;
  if (!entry_id_.empty() && ids.find(entry_id_) != ids.end()) {
    return UnlockEntryStatus_Unlocked;
  }

  float fScores[NUM_UnlockRequirement];
  UNLOCKMAN->GetPoints(PROFILEMAN->GetMachineProfile(), fScores);

  for (int i = 0; i < NUM_UnlockRequirement; ++i) {
    if (i == UnlockRequirement_NumUnlocked) {
      continue;
    }

    if (requirement_[i] && fScores[i] >= requirement_[i]) {
      return UnlockEntryStatus_RequirementsMet;
    }
  }

  if (require_pass_hard_steps_ && song_.IsValid()) {
    Song* pSong = song_.ToSong();
    std::vector<Steps*> vp;
    SongUtil::GetSteps(pSong, vp, StepsType_Invalid, Difficulty_Hard);
    for (const Steps* s : vp) {
      if (PROFILEMAN->GetMachineProfile()->HasPassedSteps(pSong, s)) {
        return UnlockEntryStatus_Unlocked;
      }
    }
  }

  if (require_pass_challenge_steps_ && song_.IsValid()) {
    Song* pSong = song_.ToSong();
    std::vector<Steps*> vp;
    SongUtil::GetSteps(pSong, vp, StepsType_Invalid, Difficulty_Challenge);
    for (const Steps* s : vp) {
      if (PROFILEMAN->GetMachineProfile()->HasPassedSteps(pSong, s)) {
        return UnlockEntryStatus_Unlocked;
      }
    }
  }

  return UnlockEntryStatus_RequrementsNotMet;
}

std::string UnlockEntry::GetDescription() const {
  Song* pSong = song_.ToSong();
  switch (type_) {
    default:
      FAIL_M(ssprintf("Invalid UnlockRewardType: %i", type_));
    case UnlockRewardType_Song:
      return pSong ? pSong->GetDisplayFullTitle() : std::string("");
    case UnlockRewardType_Steps: {
      StepsType st =
          GAMEMAN->GetHowToPlayStyleForGame(GAMESTATE->cur_game_)
              ->steps_type_;  // TODO: Is this the best thing we can do here?
      return (pSong ? pSong->GetDisplayFullTitle() : std::string("")) + ", " +
             CustomDifficultyToLocalizedString(
                 GetCustomDifficulty(st, dc_, CourseType_Invalid));
    }
    case UnlockRewardType_Steps_Type: {
      std::string ret =
          (pSong ? pSong->GetDisplayFullTitle() : std::string(""));
      ret += "," + CustomDifficultyToLocalizedString(GetCustomDifficulty(
                       steps_type_, dc_, CourseType_Invalid));
      return ret + "," +
             StringConversion::ToString(steps_type_);  // yeah, bit strange.
    }
    case UnlockRewardType_Course:
      return course_.IsValid() ? course_.ToCourse()->GetDisplayFullTitle()
                               : std::string("");
    case UnlockRewardType_Modifier:
      return CommonMetrics::LocalizeOptionItem(GetModifier(), false);
  }
}

std::string UnlockEntry::GetBannerFile() const {
  Song* pSong = song_.ToSong();
  switch (type_) {
    default:
      FAIL_M(ssprintf("Invalid UnlockRewardType: %i", type_));
    case UnlockRewardType_Song:
    case UnlockRewardType_Steps:
    case UnlockRewardType_Steps_Type:
      return pSong ? pSong->GetBannerPath() : std::string("");
    case UnlockRewardType_Course:
      return course_.ToCourse() ? course_.ToCourse()->GetBannerPath()
                                : std::string("");
    case UnlockRewardType_Modifier:
      return "";
  }
}

std::string UnlockEntry::GetBackgroundFile() const {
  Song* pSong = song_.ToSong();
  switch (type_) {
    default:
      FAIL_M(ssprintf("Invalid UnlockRewardType: %i", type_));
    case UnlockRewardType_Song:
    case UnlockRewardType_Steps:
    case UnlockRewardType_Steps_Type:
      return pSong ? pSong->GetBackgroundPath() : std::string("");
    case UnlockRewardType_Course:
      return "";
    case UnlockRewardType_Modifier:
      return "";
  }
}

/////////////////////////////////////////////////////////

void UnlockManager::Load() {
  LOG->Trace("UnlockManager::Load()");

  std::vector<std::string> asUnlockNames;
  split(UNLOCK_NAMES, ",", asUnlockNames);

  Lua* L = LUA->Get();
  for (unsigned i = 0; i < asUnlockNames.size(); ++i) {
    const std::string& sUnlockName = asUnlockNames[i];

    LuaReference cmds = UNLOCK(sUnlockName);

    UnlockEntry current;
    current.entry_id_ = sUnlockName;  // default

    // function
    cmds.PushSelf(L);
    ASSERT(!lua_isnil(L, -1));

    // 1st parameter
    current.PushSelf(L);

    // call function with 1 argument and 0 results
    std::string error = "Lua error in command: ";
    LuaHelpers::RunScriptOnStack(L, error, 1, 0, true);

    if (current.roulette_) {
      roulette_codes_.insert(current.entry_id_);
    }

    unlock_entries_.push_back(current);
  }
  LUA->Release(L);

  if (AUTO_LOCK_CHALLENGE_STEPS) {
    for (const Song* s : SONGMAN->GetAllSongs()) {
      // If no hard steps to play to unlock, skip
      if (SongUtil::GetOneSteps(s, StepsType_Invalid, Difficulty_Hard) ==
          nullptr) {
        continue;
      }

      // If no challenge steps to unlock, skip
      if (SongUtil::GetOneSteps(s, StepsType_Invalid, Difficulty_Challenge) ==
          nullptr) {
        continue;
      }

      UnlockEntry ue;
      ue.entry_id_ = "_challenge_" + s->GetSongDir();
      ue.type_ = UnlockRewardType_Steps;
      ue.cmd_.Load(
          s->group_name_ + "/" + s->GetTranslitFullTitle() + ",expert");
      ue.require_pass_hard_steps_ = true;

      unlock_entries_.push_back(ue);
    }
  }

  if (AUTO_LOCK_EDIT_STEPS) {
    for (const Song* s : SONGMAN->GetAllSongs()) {
      // no challenge steps to play: skip.
      if (SongUtil::GetOneSteps(s, StepsType_Invalid, Difficulty_Challenge) ==
          nullptr) {
        continue;
      }

      // no edit steps to unlock: skip.
      if (SongUtil::GetOneSteps(s, StepsType_Invalid, Difficulty_Edit) ==
          nullptr) {
        continue;
      }

      UnlockEntry ue;
      ue.entry_id_ = "_edit_" + s->GetSongDir();
      ue.type_ = UnlockRewardType_Steps;
      ue.cmd_.Load(s->group_name_ + "/" + s->GetTranslitFullTitle() + ",edit");
      ue.require_pass_challenge_steps_ = true;

      unlock_entries_.push_back(ue);
    }
  }

  // Make sure that we don't have duplicate unlock IDs. This can cause problems
  // with UnlockCelebrate and with codes.
  unsigned size = unlock_entries_.size();
  if (size > 1) {
    for (unsigned i = 0; i < size - 1; ++i) {
      const UnlockEntry& ue1 = unlock_entries_[i];
      for (unsigned j = i + 1; j < size; ++j) {
        const UnlockEntry& ue2 = unlock_entries_[j];
        // at this point, these two are definitely different. Assert.
        ASSERT_M(
            ue1.entry_id_ != ue2.entry_id_,
            ssprintf("duplicate unlock entry id %s", ue1.entry_id_.c_str()));
      }
    }
  }

  for (UnlockEntry& e : unlock_entries_) {
    switch (e.type_) {
      case UnlockRewardType_Song:
        e.song_.FromSong(SONGMAN->FindSong(e.cmd_.GetArg(0).s_));
        if (!e.song_.IsValid()) {
          LuaHelpers::ReportScriptErrorFmt(
              "Unlock: Cannot find song matching \"%s\"",
              e.cmd_.GetArg(0).s_.c_str());
        }
        break;
      case UnlockRewardType_Steps:
        e.song_.FromSong(SONGMAN->FindSong(e.cmd_.GetArg(0).s_));
        if (!e.song_.IsValid()) {
          LuaHelpers::ReportScriptErrorFmt(
              "Unlock: Cannot find song matching \"%s\"",
              e.cmd_.GetArg(0).s_.c_str());
          break;
        }

        e.dc_ = StringToDifficulty(e.cmd_.GetArg(1).s_);
        if (e.dc_ == Difficulty_Invalid) {
          LuaHelpers::ReportScriptErrorFmt(
              "Unlock: Invalid difficulty \"%s\"", e.cmd_.GetArg(1).s_.c_str());
          break;
        }

        break;
      case UnlockRewardType_Steps_Type: {
        e.song_.FromSong(SONGMAN->FindSong(e.cmd_.GetArg(0).s_));
        if (!e.song_.IsValid()) {
          LuaHelpers::ReportScriptErrorFmt(
              "Unlock: Cannot find song matching \"%s\"",
              e.cmd_.GetArg(0).s_.c_str());
          break;
        }

        e.dc_ = StringToDifficulty(e.cmd_.GetArg(1).s_);
        if (e.dc_ == Difficulty_Invalid) {
          LuaHelpers::ReportScriptErrorFmt(
              "Unlock: Invalid difficulty \"%s\"", e.cmd_.GetArg(1).s_.c_str());
          break;
        }

        e.steps_type_ = GAMEMAN->StringToStepsType(e.cmd_.GetArg(2).s_);
        if (e.steps_type_ == StepsType_Invalid) {
          LuaHelpers::ReportScriptErrorFmt(
              "Unlock: Invalid steps type \"%s\"", e.cmd_.GetArg(2).s_.c_str());
          break;
        }
        break;
      }
      case UnlockRewardType_Course:
        e.course_.FromCourse(SONGMAN->FindCourse(e.cmd_.GetArg(0).s_));
        if (!e.course_.IsValid()) {
          LuaHelpers::ReportScriptErrorFmt(
              "Unlock: Cannot find course matching \"%s\"",
              e.cmd_.GetArg(0).s_.c_str());
        }
        break;
      case UnlockRewardType_Modifier:
        // nothing to cache
        break;
      default:
        FAIL_M(ssprintf("Invalid UnlockRewardType: %i", e.type_));
    }
  }

  // Log unlocks
  for (UnlockEntry& e : unlock_entries_) {
    std::string str =
        ssprintf("Unlock: %s; ", join("\n", e.cmd_.args_).c_str());
    FOREACH_ENUM(UnlockRequirement, j)
    if (e.requirement_[j]) {
      str += ssprintf(
          "%s = %f; ", UnlockRequirementToString(j).c_str(), e.requirement_[j]);
    }
    if (e.require_pass_hard_steps_) {
      str += "RequirePassHardSteps; ";
    }
    if (e.require_pass_challenge_steps_) {
      str += "RequirePassChallengeSteps; ";
    }

    str += ssprintf("entryID = %s ", e.entry_id_.c_str());
    str += e.IsLocked() ? "locked" : "unlocked";
    if (e.song_.IsValid()) {
      str += (" (found song)");
    }
    if (e.course_.IsValid()) {
      str += (" (found course)");
    }
    LOG->Trace("%s", str.c_str());
  }

  return;
}

void UnlockManager::Reload() {
  // clear old data, if any
  unlock_entries_.clear();
  roulette_codes_.clear();

  Load();
}

float UnlockManager::PointsUntilNextUnlock(UnlockRequirement t) const {
  float fScores[NUM_UnlockRequirement];
  ZERO(fScores);
  GetPoints(PROFILEMAN->GetMachineProfile(), fScores);

  float fSmallestPoints = FLT_MAX;  // or an arbitrarily large value
  for (unsigned a = 0; a < unlock_entries_.size(); a++) {
    if (unlock_entries_[a].requirement_[t] > fScores[t]) {
      fSmallestPoints =
          std::min(fSmallestPoints, unlock_entries_[a].requirement_[t]);
    }
  }

  if (fSmallestPoints == FLT_MAX) {
    return 0;  // no match found
  }
  return fSmallestPoints - fScores[t];
}

void UnlockManager::UnlockEntryID(std::string sEntryID) {
  PROFILEMAN->GetMachineProfile()->unlocked_entry_ids_.insert(sEntryID);
  SONGMAN->InvalidateCachedTrails();
}

void UnlockManager::UnlockEntryIndex(int iEntryIndex) {
  std::string sEntryID = unlock_entries_[iEntryIndex].entry_id_;
  UnlockEntryID(sEntryID);
}

void UnlockManager::LockEntryID(std::string entryID) {
  PROFILEMAN->GetMachineProfile()->unlocked_entry_ids_.erase(entryID);
  SONGMAN->InvalidateCachedTrails();
}

void UnlockManager::LockEntryIndex(int entryIndex) {
  std::string entryID = unlock_entries_[entryIndex].entry_id_;
  LockEntryID(entryID);
}

void UnlockManager::PreferUnlockEntryID(std::string sUnlockEntryID) {
  for (unsigned i = 0; i < unlock_entries_.size(); ++i) {
    UnlockEntry& pEntry = unlock_entries_[i];
    if (pEntry.entry_id_ != sUnlockEntryID) {
      continue;
    }

    if (pEntry.song_.ToSong() != nullptr) {
      GAMESTATE->preferred_song_ = pEntry.song_.ToSong();
    }
    if (pEntry.course_.ToCourse()) {
      GAMESTATE->preferred_course_ = pEntry.course_.ToCourse();
    }
  }
}

int UnlockManager::GetNumUnlocks() const { return unlock_entries_.size(); }

int UnlockManager::GetNumUnlocked() const {
  return std::count_if(
      unlock_entries_.begin(), unlock_entries_.end(),
      [](const UnlockEntry& ue) { return !ue.IsLocked(); });
}

int UnlockManager::GetUnlockEntryIndexToCelebrate() const {
  int i = 0;
  for (const UnlockEntry& ue : unlock_entries_) {
    if (ue.GetUnlockEntryStatus() == UnlockEntryStatus_RequirementsMet) {
      return i;
    }
    ++i;
  }
  return -1;
}

bool UnlockManager::AnyUnlocksToCelebrate() const {
  return GetUnlockEntryIndexToCelebrate() != -1;
}

void UnlockManager::GetUnlocksByType(
    UnlockRewardType t, std::vector<UnlockEntry*>& apEntries) {
  for (UnlockEntry& entry : unlock_entries_) {
    if (entry.IsValid() && entry.type_ == t) {
      apEntries.push_back(&entry);
    }
  }
}

void UnlockManager::GetSongsUnlockedByEntryID(
    std::vector<Song*>& apSongsOut, std::string sUnlockEntryID) {
  std::vector<UnlockEntry*> apEntries;
  GetUnlocksByType(UnlockRewardType_Song, apEntries);

  for (const UnlockEntry* ue : apEntries) {
    if (ue->entry_id_ == sUnlockEntryID) {
      apSongsOut.push_back(ue->song_.ToSong());
    }
  }
}

void UnlockManager::GetStepsUnlockedByEntryID(
    std::vector<Song*>& apSongsOut, std::vector<Difficulty>& apDifficultyOut,
    std::string sUnlockEntryID) {
  std::vector<UnlockEntry*> apEntries;
  GetUnlocksByType(UnlockRewardType_Steps, apEntries);

  for (const UnlockEntry* entry : apEntries) {
    if (entry->entry_id_ == sUnlockEntryID) {
      apSongsOut.push_back(entry->song_.ToSong());
      apDifficultyOut.push_back(entry->dc_);
    }
  }
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the UnlockEntry. */
class LunaUnlockEntry : public Luna<UnlockEntry> {
 public:
  static int IsLocked(T* p, lua_State* L) {
    lua_pushboolean(L, p->IsLocked());
    return 1;
  }
  static int GetDescription(T* p, lua_State* L) {
    lua_pushstring(L, p->GetDescription().c_str());
    return 1;
  }
  static int GetUnlockRewardType(T* p, lua_State* L) {
    lua_pushnumber(L, p->type_);
    return 1;
  }
  static int GetRequirement(T* p, lua_State* L) {
    UnlockRequirement i = Enum::Check<UnlockRequirement>(L, 1);
    lua_pushnumber(L, p->requirement_[i]);
    return 1;
  }
  static int GetRequirePassHardSteps(T* p, lua_State* L) {
    lua_pushboolean(L, p->require_pass_hard_steps_);
    return 1;
  }
  static int GetRequirePassChallengeSteps(T* p, lua_State* L) {
    lua_pushboolean(L, p->require_pass_challenge_steps_);
    return 1;
  }
  static int GetSong(T* p, lua_State* L) {
    Song* pSong = p->song_.ToSong();
    if (pSong) {
      pSong->PushSelf(L);
      return 1;
    }
    return 0;
  }
  // Get all of the steps locked based on difficulty (similar to In The Groove
  // 2).
  static int GetStepOfAllTypes(T* p, lua_State* L) {
    Song* pSong = p->song_.ToSong();
    if (pSong) {
      const std::vector<Steps*>& allSteps = pSong->GetAllSteps();
      std::vector<Steps*> toRet;
      for (Steps* step : allSteps) {
        if (step->GetDifficulty() == p->dc_) {
          toRet.push_back(step);
        }
      }
      LuaHelpers::CreateTableFromArray<Steps*>(toRet, L);
      return 1;
    }
    return 0;
  }

  // TODO: Add a function to just get all steps.
  static int GetStepByStepsType(T* p, lua_State* L) {
    Song* pSong = p->song_.ToSong();
    if (pSong) {
      const std::vector<Steps*>& allStepsType =
          pSong->GetStepsByStepsType(p->steps_type_);
      for (Steps* step : allStepsType) {
        if (step->GetDifficulty() == p->dc_) {
          step->PushSelf(L);
          return 1;
        }
      }
    }
    return 0;
  }

  static int GetCourse(T* p, lua_State* L) {
    Course* pCourse = p->course_.ToCourse();
    if (pCourse) {
      pCourse->PushSelf(L);
      return 1;
    }
    return 0;
  }
  static int GetCode(T* p, lua_State* L) {
    lua_pushstring(L, p->entry_id_.c_str());
    return 1;
  }

  // internal
  static int GetArgs(T* p, lua_State* L) {
    Command cmd;
    for (int i = 1; i <= lua_gettop(L); ++i) {
      cmd.args_.push_back(SArg(i));
    }
    p->cmd_ = cmd;
    return 0;
  }

  static int song(T* p, lua_State* L) {
    GetArgs(p, L);
    p->type_ = UnlockRewardType_Song;
    return 0;
  }
  static int steps(T* p, lua_State* L) {
    GetArgs(p, L);
    p->type_ = UnlockRewardType_Steps;
    return 0;
  }
  static int steps_type(T* p, lua_State* L) {
    GetArgs(p, L);
    p->type_ = UnlockRewardType_Steps_Type;
    return 0;
  }
  static int course(T* p, lua_State* L) {
    GetArgs(p, L);
    p->type_ = UnlockRewardType_Course;
    return 0;
  }
  static int mod(T* p, lua_State* L) {
    GetArgs(p, L);
    p->type_ = UnlockRewardType_Modifier;
    return 0;
  }
  static int code(T* p, lua_State* L) {
    p->entry_id_ = SArg(1);
    return 0;
  }
  static int roulette(T* p, lua_State* L) {
    p->roulette_ = true;
    return 0;
  }
  static int requirepasshardsteps(T* p, lua_State* L) {
    p->require_pass_hard_steps_ = true;
    return 0;
  }
  static int requirepasschallengesteps(T* p, lua_State* L) {
    p->require_pass_challenge_steps_ = true;
    return 0;
  }
  static int require(T* p, lua_State* L) {
    const UnlockRequirement ut = Enum::Check<UnlockRequirement>(L, 1);
    if (ut != UnlockRequirement_Invalid) {
      p->requirement_[ut] = FArg(2);
    }
    return 0;
  }

  LunaUnlockEntry() {
    ADD_METHOD(IsLocked);
    ADD_METHOD(GetCode);
    ADD_METHOD(GetDescription);
    ADD_METHOD(GetUnlockRewardType);
    ADD_METHOD(GetRequirement);
    ADD_METHOD(GetRequirePassHardSteps);
    ADD_METHOD(GetRequirePassChallengeSteps);
    ADD_METHOD(GetSong);
    ADD_METHOD(GetCourse);
    ADD_METHOD(GetStepOfAllTypes);
    ADD_METHOD(GetStepByStepsType);
    ADD_METHOD(song);
    ADD_METHOD(steps);
    ADD_METHOD(steps_type);
    ADD_METHOD(course);
    ADD_METHOD(mod);
    ADD_METHOD(code);
    ADD_METHOD(roulette);
    ADD_METHOD(requirepasshardsteps);
    ADD_METHOD(requirepasschallengesteps);
    ADD_METHOD(require);
  }
};

LUA_REGISTER_CLASS(UnlockEntry)

/** @brief Allow Lua to have access to the UnlockManager. */
class LunaUnlockManager : public Luna<UnlockManager> {
 public:
  static int GetPointsUntilNextUnlock(T* p, lua_State* L) {
    const UnlockRequirement ut = Enum::Check<UnlockRequirement>(L, 1);
    lua_pushnumber(L, p->PointsUntilNextUnlock(ut));
    return 1;
  }
  static int FindEntryID(T* p, lua_State* L) {
    std::string sName = SArg(1);
    std::string s = p->FindEntryID(sName);
    if (s.empty()) {
      lua_pushnil(L);
    } else {
      lua_pushstring(L, s.c_str());
    }
    return 1;
  }
  static int UnlockEntryID(T* p, lua_State* L) {
    std::string sUnlockEntryID = SArg(1);
    p->UnlockEntryID(sUnlockEntryID);
    COMMON_RETURN_SELF;
  }
  static int UnlockEntryIndex(T* p, lua_State* L) {
    int iUnlockEntryID = IArg(1);
    p->UnlockEntryIndex(iUnlockEntryID);
    COMMON_RETURN_SELF;
  }
  static int LockEntryID(T* p, lua_State* L) {
    std::string entryID = SArg(1);
    p->LockEntryID(entryID);
    COMMON_RETURN_SELF;
  }
  static int LockEntryIndex(T* p, lua_State* L) {
    int entryIndex = IArg(1);
    p->LockEntryIndex(entryIndex);
    COMMON_RETURN_SELF;
  }
  static int PreferUnlockEntryID(T* p, lua_State* L) {
    std::string sUnlockEntryID = SArg(1);
    p->PreferUnlockEntryID(sUnlockEntryID);
    COMMON_RETURN_SELF;
  }
  static int GetNumUnlocks(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetNumUnlocks());
    return 1;
  }
  static int GetNumUnlocked(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetNumUnlocked());
    return 1;
  }
  static int GetUnlockEntryIndexToCelebrate(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetUnlockEntryIndexToCelebrate());
    return 1;
  }
  static int AnyUnlocksToCelebrate(T* p, lua_State* L) {
    lua_pushboolean(L, p->AnyUnlocksToCelebrate());
    return 1;
  }
  static int GetUnlockEntry(T* p, lua_State* L) {
    unsigned iIndex = IArg(1);
    if (iIndex >= p->unlock_entries_.size()) {
      return 0;
    }
    p->unlock_entries_[iIndex].PushSelf(L);
    return 1;
  }
  static int GetSongsUnlockedByEntryID(T* p, lua_State* L) {
    std::vector<Song*> apSongs;
    UNLOCKMAN->GetSongsUnlockedByEntryID(apSongs, SArg(1));
    LuaHelpers::CreateTableFromArray(apSongs, L);
    return 1;
  }

  static int GetStepsUnlockedByEntryID(T* p, lua_State* L) {
    // Return the Song each Steps are associated with, too.
    std::vector<Song*> apSongs;
    std::vector<Difficulty> apDifficulty;
    UNLOCKMAN->GetStepsUnlockedByEntryID(apSongs, apDifficulty, SArg(1));
    LuaHelpers::CreateTableFromArray(apSongs, L);
    LuaHelpers::CreateTableFromArray(apDifficulty, L);
    return 2;
  }

  static int GetPoints(T* p, lua_State* L) {
    float fScores[NUM_UnlockRequirement];
    UNLOCKMAN->GetPoints(PROFILEMAN->GetMachineProfile(), fScores);
    lua_pushnumber(L, fScores[Enum::Check<UnlockRequirement>(L, 1)]);
    return 1;
  }

  static int GetPointsForProfile(T* p, lua_State* L) {
    float fScores[NUM_UnlockRequirement];
    UNLOCKMAN->GetPoints(Luna<Profile>::check(L, 1), fScores);
    lua_pushnumber(L, fScores[Enum::Check<UnlockRequirement>(L, 2)]);
    return 1;
  }

  static int IsSongLocked(T* p, lua_State* L) {
    Song* pSong = Luna<Song>::check(L, 1);
    lua_pushnumber(L, UNLOCKMAN->SongIsLocked(pSong));
    return 1;
  }

  LunaUnlockManager() {
    ADD_METHOD(AnyUnlocksToCelebrate);
    ADD_METHOD(FindEntryID);
    ADD_METHOD(GetNumUnlocks);
    ADD_METHOD(GetNumUnlocked);
    ADD_METHOD(GetPoints);
    ADD_METHOD(GetPointsForProfile);
    ADD_METHOD(GetPointsUntilNextUnlock);
    ADD_METHOD(GetSongsUnlockedByEntryID);
    ADD_METHOD(GetStepsUnlockedByEntryID);
    ADD_METHOD(GetUnlockEntry);
    ADD_METHOD(GetUnlockEntryIndexToCelebrate);
    ADD_METHOD(PreferUnlockEntryID);
    ADD_METHOD(UnlockEntryID);
    ADD_METHOD(UnlockEntryIndex);
    ADD_METHOD(LockEntryID);
    ADD_METHOD(LockEntryIndex);
    ADD_METHOD(IsSongLocked);
    // ADD_METHOD( GetUnlocksByType );
  }
};

LUA_REGISTER_CLASS(UnlockManager)
// lua end

/*
 * (c) 2001-2004 Kevin Slaughter, Andrew Wong, Glenn Maynard
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
