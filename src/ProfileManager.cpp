#include "ProfileManager.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "Character.h"
#include "CharacterManager.h"
#include "Course.h"
#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "GameState.h"
#include "Grade.h"
#include "HighScore.h"
#include "LuaManager.h"
#include "MemoryCardManager.h"
#include "PlayerNumber.h"
#include "Preference.h"
#include "PrefsManager.h"
#include "Profile.h"
#include "RageFileManager.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "Song.h"
#include "SongManager.h"
#include "StdString.h"
#include "Steps.h"
#include "StepsUtil.h"
#include "ThemeManager.h"
#include "ThemeMetric.h"
#include "XmlFile.h"
#include "global.h"

ProfileManager* PROFILEMAN =
    nullptr;  // global and accessible from anywhere in our program

#define ID_DIGITS 8
#define ID_DIGITS_STR "8"
#define MAX_ID 99999999

static void DefaultLocalProfileIDInit(
    size_t /*PlayerNumber*/ i, std::string& name_out,
    std::string& default_value_out) {
  name_out = ssprintf("DefaultLocalProfileIDP%d", int(i + 1));
  default_value_out = "";
}

Preference<bool> ProfileManager::profile_step_edits_("ProfileStepEdits", true);
Preference<bool> ProfileManager::profile_course_edits_(
    "ProfileCourseEdits", true);
Preference1D<std::string> ProfileManager::default_local_profile_id_(
    DefaultLocalProfileIDInit, NUM_PLAYERS);

const std::string USER_PROFILES_DIR = "/Save/LocalProfiles/";
const std::string MACHINE_PROFILE_DIR = "/Save/MachineProfile/";
const std::string LAST_GOOD_SUBDIR = "LastGood/";

// Directories to search for a profile if m_sMemoryCardProfileSubdir doesn't
// exist, separated by ";":
static Preference<std::string> g_sMemoryCardProfileImportSubdirs(
    "MemoryCardProfileImportSubdirs",
    "StepMania 5.1;StepMania 5;In The Groove 2");

static std::string LocalProfileIDToDir(const std::string& profile_id) {
  return USER_PROFILES_DIR + profile_id + "/";
}

// Sanitize a display name so it is safe to embed in a directory name.
constexpr size_t MAX_DISPLAY_NAME_IN_DIR = 32;
static std::string SanitizeForDirName(const std::string& name) {
  std::string result;

  result.reserve(name.size());
  for (unsigned char c : name) {
    // Allow only alphanumeric characters, underscore, and dash.
    if (std::isalnum(c) || c == '_' || c == '-') {
      result += static_cast<char>(c);
    } else {
      result += '_';
    }
  }
  // Cap length.
  if (result.size() > MAX_DISPLAY_NAME_IN_DIR) {
    result.resize(MAX_DISPLAY_NAME_IN_DIR);
  }
  // If the result is all underscores (e.g. the name was only spaces/periods),
  // treat it as empty so the caller falls back to the number-only format.
  if (result.find_first_not_of('_') == std::string::npos) {
    return "";
  }
  return result;
}

static std::string LocalProfileDirToID(const std::string& dir) {
  return Basename(dir);
}

struct DirAndProfile {
  std::string dir_;
  Profile profile_;
  void swap(DirAndProfile& other) {
    dir_.swap(other.dir_);
    profile_.swap(other.profile_);
  }
};
static std::vector<DirAndProfile> g_vLocalProfile;

static ThemeMetric<bool> FIXED_PROFILES("ProfileManager", "FixedProfiles");
static ThemeMetric<int> NUM_FIXED_PROFILES(
    "ProfileManager", "NumFixedProfiles");
#define FIXED_PROFILE_CHARACTER_ID(i) \
  THEME->GetMetric(                   \
      "ProfileManager", ssprintf("FixedProfileCharacterID%d", int(i + 1)))

ProfileManager::ProfileManager() : stats_prefix_("") {
  machine_profile_ = new Profile;
  FOREACH_PlayerNumber(pn) memory_card_profile_[pn] = new Profile;

  // Register with Lua.
  {
    Lua* L = LUA->Get();
    lua_pushstring(L, "PROFILEMAN");
    this->PushSelf(L);
    lua_settable(L, LUA_GLOBALSINDEX);
    LUA->Release(L);
  }
}

ProfileManager::~ProfileManager() {
  // Unregister with Lua.
  LUA->UnsetGlobal("PROFILEMAN");

  RageUtil::SafeDelete(machine_profile_);
  FOREACH_PlayerNumber(pn) RageUtil::SafeDelete(memory_card_profile_[pn]);
}

void ProfileManager::Init() {
  FOREACH_PlayerNumber(p) {
    was_loaded_from_memory_card_[p] = false;
    last_load_was_tampered_or_corrupt_[p] = false;
    last_load_was_from_last_good_[p] = false;
    need_to_back_up_last_load_[p] = false;
    new_profile_[p] = false;
    stats_prefix_change_failed_[p] = false;
  }

  LoadMachineProfile();

  RefreshLocalProfilesFromDisk();

  if (FIXED_PROFILES) {
    // resize to the fixed number
    if ((int)g_vLocalProfile.size() > NUM_FIXED_PROFILES) {
      g_vLocalProfile.erase(
          g_vLocalProfile.begin() + NUM_FIXED_PROFILES, g_vLocalProfile.end());
    }

    for (int i = g_vLocalProfile.size(); i < NUM_FIXED_PROFILES; i++) {
      std::string character_id = FIXED_PROFILE_CHARACTER_ID(i);
      Character* character = CHARMAN->GetCharacterFromID(character_id);
      ASSERT_M(character != nullptr, character_id);
      std::string profile_id;
      bool b = CreateLocalProfile(character->GetDisplayName(), profile_id);
      ASSERT(b);
      Profile* profile = GetLocalProfile(profile_id);
      ASSERT_M(profile != nullptr, profile_id);
      profile->character_id_ = character_id;
      SaveLocalProfile(profile_id);
    }

    ASSERT((int)g_vLocalProfile.size() == NUM_FIXED_PROFILES);
  }
}

bool ProfileManager::FixedProfiles() const { return FIXED_PROFILES; }

ProfileLoadResult ProfileManager::LoadProfile(
    PlayerNumber pn, std::string profile_dir, bool is_mem_card) {
  LOG->Trace(
      "LoadingProfile P%d, %s, %d", pn + 1, profile_dir.c_str(), is_mem_card);

  ASSERT(!profile_dir.empty());
  ASSERT(Right(profile_dir, 1) == "/");

  profile_dir_[pn] = profile_dir;
  was_loaded_from_memory_card_[pn] = is_mem_card;
  last_load_was_from_last_good_[pn] = false;
  need_to_back_up_last_load_[pn] = false;
  stats_prefix_change_failed_[pn] = false;

  // Try to load the original, non-backup data.
  ProfileLoadResult lr = GetProfile(pn)->LoadAllFromDir(
      profile_dir_[pn], PREFSMAN->sign_profile_data_);

  std::string backup_dir = profile_dir_[pn] + LAST_GOOD_SUBDIR;

  if (lr == ProfileLoadResult_Success) {
    /* Next time the profile is written, move this good profile into LastGood.
     */
    need_to_back_up_last_load_[pn] = true;
  }

  last_load_was_tampered_or_corrupt_[pn] =
      lr == ProfileLoadResult_FailedTampered;

  //
  // Try to load from the backup if the original data is corrupt or missing.
  //
  if (lr == ProfileLoadResult_FailedTampered ||
      lr == ProfileLoadResult_FailedNoProfile) {
    const ProfileLoadResult primary_lr = lr;

    if (primary_lr == ProfileLoadResult_FailedNoProfile) {
      // Validate LastGood separately so a missing or invalid backup does not
      // overwrite useful non-stats data loaded from the primary directory.
      Profile backup_profile;
      lr = backup_profile.LoadStatsFromDir(
          backup_dir, PREFSMAN->sign_profile_data_);
    }
    if (primary_lr == ProfileLoadResult_FailedTampered ||
        lr == ProfileLoadResult_Success) {
      lr = GetProfile(pn)->LoadAllFromDir(
          backup_dir, PREFSMAN->sign_profile_data_);
    }
    last_load_was_from_last_good_[pn] = lr == ProfileLoadResult_Success;

    // If the backup also fails, preserve the primary load result.
    // (a new profile should remain FailedNoProfile)
    if (!last_load_was_from_last_good_[pn]) {
      lr = primary_lr;
    }
  }

  if (lr == ProfileLoadResult_Success) {
    Profile* prof = GetProfile(pn);
    if (prof->display_name_.empty()) {
      prof->display_name_ = PlayerNumberToLocalizedString(pn);
    }
    prof->LoadCustomFunction(profile_dir, pn);

    // LoadSongsFromDir stops starting songs after this limit. Allow time to
    // finish the last one.
    const float last_song_seconds = 3;
    MEMCARDMAN->SetCardAccessTimeLimit(
        pn, PREFSMAN->custom_songs_load_timeout_ + last_song_seconds);
    prof->LoadSongsFromDir(profile_dir, ProfileSlot(pn));

    // Give the rest of the access, such as loading edits, a fresh limit.
    MEMCARDMAN->SetCardAccessTimeLimit(pn, 15);
  }

  LOG->Trace("Done loading profile - result %d", lr);

  return lr;
}

bool ProfileManager::LoadLocalProfileFromMachine(PlayerNumber pn) {
  std::string profile_id = default_local_profile_id_[pn];
  if (profile_id.empty()) {
    profile_dir_[pn] = "";
    return false;
  }

  // Make sure the profile is currently not in use by another player
  FOREACH_HumanPlayer(pl) {
    if (PROFILEMAN->IsPersistentProfile(pl) &&
        PROFILEMAN->GetProfile(pl) == PROFILEMAN->GetLocalProfile(profile_id)) {
      profile_dir_[pn] = "";
      return false;
    }
  }

  profile_dir_[pn] = LocalProfileIDToDir(profile_id);
  was_loaded_from_memory_card_[pn] = false;
  last_load_was_from_last_good_[pn] = false;
  stats_prefix_change_failed_[pn] = false;

  if (GetLocalProfile(profile_id) == nullptr) {
    profile_dir_[pn] = "";
    return false;
  }

  GetProfile(pn)->LoadCustomFunction(profile_dir_[pn], pn);

  return true;
}

void ProfileManager::GetMemoryCardProfileDirectoriesToTry(
    std::vector<std::string>& dirs_to_try) {
  /* Try to load the preferred profile. */
  dirs_to_try.push_back(PREFSMAN->memory_card_profile_subdir_.Get());

  /* If that failed, try loading from all fallback directories. */
  split(g_sMemoryCardProfileImportSubdirs, ";", dirs_to_try, true);
}

bool ProfileManager::LoadProfileFromMemoryCard(
    PlayerNumber pn, bool load_edits) {
  // The card must be ready and mounted to determine if a profile already
  // exists on the card.
  if (MEMCARDMAN->GetCardState(pn) != MemoryCardState_Ready ||
      !MEMCARDMAN->IsMounted(pn)) {
    return false;
  }

  UnloadProfile(pn);

  std::vector<std::string> dirs_to_try;
  GetMemoryCardProfileDirectoriesToTry(dirs_to_try);
  new_profile_[pn] = true;

  for (unsigned i = 0; i < dirs_to_try.size(); ++i) {
    const std::string& subdir = dirs_to_try[i];
    std::string dir = MEM_CARD_MOUNT_POINT[pn] + subdir + "/";

    /* If the load fails with ProfileLoadResult_FailedNoProfile, keep searching.
     * However, if it fails with failed_tampered, data existed but couldn't be
     * loaded; we don't want to mess with it, since it's confusing and may wipe
     * out recoverable backup data.  The only time we really want to import data
     * is on the very first use, when the new profile doesn't exist at all,
     * but we also want to import scores in the case where the player created
     * a directory for edits before playing, so keep searching if the directory
     * exists with exists with no scores. */
    ProfileLoadResult res = LoadProfile(pn, dir, true);
    if (res == ProfileLoadResult_Success) {
      new_profile_[pn] = false;
      break;
    }

    if (res == ProfileLoadResult_FailedTampered) {
      new_profile_[pn] = false;
      break;
    }
  }

  /* If we imported a profile fallback directory, change the memory card
   * directory back to the preferred directory: never write over imported
   * scores. */
  profile_dir_[pn] = MEM_CARD_MOUNT_POINT[pn] +
                     (std::string)PREFSMAN->memory_card_profile_subdir_ + "/";

  /* Load edits from all fallback directories, newest first. */
  if (load_edits) {
    for (unsigned i = 0; i < dirs_to_try.size(); ++i) {
      const std::string& subdir = dirs_to_try[i];
      std::string dir = MEM_CARD_MOUNT_POINT[pn] + subdir + "/";

      if (profile_step_edits_) {
        SONGMAN->LoadStepEditsFromProfileDir(dir, (ProfileSlot)pn);
      }
      if (profile_course_edits_) {
        SONGMAN->LoadCourseEditsFromProfileDir(dir, (ProfileSlot)pn);
      }
    }
  }

  return true;  // If a card is inserted, we want to use the memory card to save
                // - even if the Profile load failed.
}

bool ProfileManager::LoadFirstAvailableProfile(
    PlayerNumber pn, bool load_edits) {
  if (LoadProfileFromMemoryCard(pn, load_edits)) {
    return true;
  }

  if (LoadLocalProfileFromMachine(pn)) {
    return true;
  }

  return false;
}

bool ProfileManager::FastLoadProfileNameFromMemoryCard(
    std::string root_dir, std::string& name) const {
  std::vector<std::string> dirs_to_try;
  GetMemoryCardProfileDirectoriesToTry(dirs_to_try);

  for (unsigned i = 0; i < dirs_to_try.size(); ++i) {
    const std::string& subdir = dirs_to_try[i];
    std::string dir = root_dir + subdir + "/";

    Profile profile;
    ProfileLoadResult res = profile.LoadEditableDataFromDir(dir);
    if (res == ProfileLoadResult_Success) {
      name = profile.GetDisplayNameOrHighScoreName();
      return true;
    } else if (res != ProfileLoadResult_FailedNoProfile) {
      break;
    }
  }

  return false;
}

bool ProfileManager::SaveProfile(PlayerNumber pn) const {
  if (profile_dir_[pn].empty()) {
    return false;
  }
  if (stats_prefix_change_failed_[pn]) {
    return false;
  }

  /*
   * If the profile we're writing was loaded from the primary (non-backup)
   * data, then we've validated it and know it's good.  Before writing our
   * new data, move the old, good data to the backup.  (Only do this once;
   * if we save the profile more than once, we haven't re-validated the
   * newly written data.)
   */
  if (need_to_back_up_last_load_[pn]) {
    need_to_back_up_last_load_[pn] = false;
    std::string backup_dir = profile_dir_[pn] + LAST_GOOD_SUBDIR;
    Profile::MoveBackupToDir(profile_dir_[pn], backup_dir);
  }

  bool b = GetProfile(pn)->SaveAllToDir(
      profile_dir_[pn], PREFSMAN->sign_profile_data_);

  return b;
}

bool ProfileManager::SaveLocalProfile(std::string profile_id) {
  const Profile* profile = GetLocalProfile(profile_id);
  ASSERT(profile != nullptr);
  std::string dir = LocalProfileIDToDir(profile_id);
  bool b = profile->SaveAllToDir(dir, PREFSMAN->sign_profile_data_);
  return b;
}

void ProfileManager::UnloadProfile(PlayerNumber pn) {
  if (profile_dir_[pn].empty()) {
    // Don't bother unloading a profile that wasn't loaded in the first place.
    // Saves us an expensive and pointless trip through all the songs.
    return;
  }
  profile_dir_[pn] = "";
  was_loaded_from_memory_card_[pn] = false;
  last_load_was_tampered_or_corrupt_[pn] = false;
  last_load_was_from_last_good_[pn] = false;
  need_to_back_up_last_load_[pn] = false;
  stats_prefix_change_failed_[pn] = false;
  memory_card_profile_[pn]->InitAll();
  SONGMAN->FreeAllLoadedFromProfile((ProfileSlot)pn);
}

const Profile* ProfileManager::GetProfile(PlayerNumber pn) const {
  ASSERT(pn >= 0 && pn < NUM_PLAYERS);

  if (profile_dir_[pn].empty()) {
    // return an empty profile
    return memory_card_profile_[pn];
  } else if (ProfileWasLoadedFromMemoryCard(pn)) {
    return memory_card_profile_[pn];
  } else {
    std::string profile_id = LocalProfileDirToID(profile_dir_[pn]);
    return GetLocalProfile(profile_id);
  }
}

const Profile* ProfileManager::GetProfileByGuid(const std::string& guid) const {
  if (guid.empty()) {
    return nullptr;
  }

  if (machine_profile_ != nullptr && machine_profile_->guid_ == guid) {
    return machine_profile_;
  }

  FOREACH_PlayerNumber(pn) {
    const Profile* profile = GetProfile(pn);
    if (profile != nullptr && profile->guid_ == guid) {
      return profile;
    }
  }

  for (const DirAndProfile& dap : g_vLocalProfile) {
    if (dap.profile_.guid_ == guid) {
      return &dap.profile_;
    }
  }

  return nullptr;
}

std::string ProfileManager::GetPlayerName(PlayerNumber pn) const {
  const Profile* prof = GetProfile(pn);
  return prof ? prof->GetDisplayNameOrHighScoreName() : std::string();
}

void ProfileManager::UnloadAllLocalProfiles() { g_vLocalProfile.clear(); }

static void add_category_to_global_list(std::vector<DirAndProfile>& cat) {
  g_vLocalProfile.insert(g_vLocalProfile.end(), cat.begin(), cat.end());
}

template <typename CompareAscending>
static void LoadLocalProfiles(CompareAscending compare_ascending) {
  const bool ascending = PREFSMAN->profile_sort_order_ascending_;

  std::vector<std::string> profile_ids;
  GetDirListing(USER_PROFILES_DIR + "*", profile_ids, true, true);

  std::vector<DirAndProfile> categorized_profiles[NUM_ProfileType];
  for (const std::string& id : profile_ids) {
    DirAndProfile derp;
    derp.dir_ = id + "/";
    derp.profile_.LoadTypeFromDir(derp.dir_);
    derp.profile_.LoadEditableDataFromDir(derp.dir_);
    categorized_profiles[derp.profile_.type_].push_back(derp);
  }

  auto compare = [ascending, compare_ascending](
                     const DirAndProfile& a, const DirAndProfile& b) {
    return ascending ? compare_ascending(a, b) : compare_ascending(b, a);
  };
  for (std::vector<DirAndProfile>& bucket : categorized_profiles) {
    std::stable_sort(bucket.begin(), bucket.end(), compare);
  }

  add_category_to_global_list(categorized_profiles[ProfileType_Guest]);
  add_category_to_global_list(categorized_profiles[ProfileType_Normal]);
  add_category_to_global_list(categorized_profiles[ProfileType_Test]);

  for (DirAndProfile& curr : g_vLocalProfile) {
    curr.profile_.LoadAllFromDir(curr.dir_, PREFSMAN->sign_profile_data_);
  }
}

void ProfileManager::RefreshLocalProfilesFromDisk() {
  UnloadAllLocalProfiles();

  switch (PREFSMAN->profile_sort_order_) {
    case ProfileSortOrder_Alphabetical:
      LoadLocalProfilesByName();
      break;
    case ProfileSortOrder_Recent:
      LoadLocalProfilesByRecent();
      break;
    case ProfileSortOrder_CreationTime:
      LoadLocalProfilesByCreationTime();
      break;
    default:
      LoadLocalProfilesByPriority();
      break;
  }
}

void ProfileManager::LoadLocalProfilesByPriority() {
  LoadLocalProfiles([](const DirAndProfile& a, const DirAndProfile& b) {
    return a.profile_.list_priority_ < b.profile_.list_priority_;
  });
}

void ProfileManager::LoadLocalProfilesByName() {
  LoadLocalProfiles([](const DirAndProfile& a, const DirAndProfile& b) {
    return CompareNoCase(a.profile_.display_name_, b.profile_.display_name_) <
           0;
  });
}

// This function is used within RefreshLocalProfilesFromDisk() to sort the
// profiles by date.
void ProfileManager::LoadLocalProfilesByRecent() {
  LoadLocalProfiles([](const DirAndProfile& a, const DirAndProfile& b) {
    return a.profile_.last_played_date_ > b.profile_.last_played_date_;
  });
}

// This function is used within RefreshLocalProfilesFromDisk() to sort the
// profiles by creation time.
void ProfileManager::LoadLocalProfilesByCreationTime() {
  LoadLocalProfiles([](const DirAndProfile& a, const DirAndProfile& b) {
    return a.profile_.creation_time_ > b.profile_.creation_time_;
  });
}

const Profile* ProfileManager::GetLocalProfile(
    const std::string& profile_id) const {
  std::string dir = LocalProfileIDToDir(profile_id);
  for (const DirAndProfile& dap : g_vLocalProfile) {
    const std::string& other = dap.dir_;
    if (other == dir) {
      return &dap.profile_;
    }
  }

  return nullptr;
}

bool ProfileManager::CreateLocalProfile(
    std::string name, std::string& profile_id_out) {
  ASSERT(!name.empty());

  std::vector<std::string> profile_ids;
  GetLocalProfileIDs(profile_ids);

  std::string base = SanitizeForDirName(name);
  if (base.empty()) {
    base = "Profile";
  }

  std::string profile_id = base;
  bool base_exists = false;
  for (std::vector<std::string>::const_iterator id = profile_ids.begin();
       id != profile_ids.end(); ++id) {
    if (CompareNoCase(*id, base) == 0) {
      base_exists = true;
      break;
    }
  }

  // If a profile with the same name already exists..
  // attempt to append an incrementing number until we find a name that doesn't
  // exist.
  if (base_exists) {
    int16_t suffix = 1;
    while (true) {
      std::string candidate = ssprintf("%s_%d", base.c_str(), suffix);
      bool candidate_exists = false;
      for (std::vector<std::string>::const_iterator id = profile_ids.begin();
           id != profile_ids.end(); ++id) {
        if (CompareNoCase(*id, candidate) == 0) {
          candidate_exists = true;
          break;
        }
      }

      if (!candidate_exists) {
        profile_id = candidate;
        break;
      }

      // Bail before overflow
      if (suffix < 0 || suffix == std::numeric_limits<int16_t>::max()) {
        profile_id_out = "";
        return false;
      }
      ++suffix;
    }
  }

  // make sure this id doesn't already exist
  ASSERT_M(
      GetLocalProfile(profile_id) == nullptr,
      ssprintf(
          "creating profile with ID \"%s\" that already exists",
          profile_id.c_str()));

  // Create the new profile.
  Profile* profile = new Profile;
  profile->display_name_ = name;
  profile->character_id_ = CHARMAN->GetRandomCharacter()->character_id_;
  profile->creation_time_ = DateTime::GetNowDateTime();

  // Save it to disk.
  std::string profile_dir = LocalProfileIDToDir(profile_id);
  if (!profile->SaveAllToDir(profile_dir, PREFSMAN->sign_profile_data_)) {
    delete profile;
    profile_id_out = "";
    return false;
  }

  AddLocalProfileByID(profile, profile_id);

  profile_id_out = profile_id;
  return true;
}

static void InsertProfileIntoList(DirAndProfile& derp) {
  bool inserted = false;
  derp.profile_.list_priority_ = 0;
  int index = -1;
  for (auto curr = g_vLocalProfile.begin(); curr != g_vLocalProfile.end();
       ++curr) {
    ++index;
    if (curr->profile_.type_ > derp.profile_.type_) {
      derp.profile_.SaveTypeToDir(derp.dir_);
      g_vLocalProfile.insert(curr, derp);
      inserted = true;
      break;
    } else if (curr->profile_.type_ == derp.profile_.type_) {
      ++derp.profile_.list_priority_;
    }
  }

  if (!inserted) {
    derp.profile_.SaveTypeToDir(derp.dir_);
    g_vLocalProfile.push_back(derp);
  }

  // The above should run regardless of profile sort in case the user decides to
  // change back to priority sorting. If we're using Recent sorting, move the
  // new profile to the top, if we're using alphabetical find where it belongs.
  switch (PREFSMAN->profile_sort_order_) {
    case ProfileSortOrder_Recent:
      PROFILEMAN->MoveProfileTopBottom(
          index, PREFSMAN->profile_sort_order_ascending_);
      break;
    case ProfileSortOrder_Alphabetical:
      PROFILEMAN->MoveProfileSorted(
          index, PREFSMAN->profile_sort_order_ascending_);
      break;
    default:
      break;
  }
}

void ProfileManager::AddLocalProfileByID(
    Profile* profile, std::string profile_id) {
  // make sure this id doesn't already exist
  ASSERT_M(
      GetLocalProfile(profile_id) == nullptr,
      ssprintf(
          "creating \"%s\" \"%s\" that already exists",
          profile->display_name_.c_str(), profile_id.c_str()));

  DirAndProfile derp;
  derp.dir_ = LocalProfileIDToDir(profile_id);
  derp.profile_ = *profile;
  InsertProfileIntoList(derp);
}

bool ProfileManager::RenameLocalProfile(
    std::string profile_id, std::string new_name) {
  ASSERT(!profile_id.empty());

  Profile* profile = ProfileManager::GetLocalProfile(profile_id);
  ASSERT(profile != nullptr);
  profile->display_name_ = new_name;

  std::string profile_dir = LocalProfileIDToDir(profile_id);
  return profile->SaveAllToDir(profile_dir, PREFSMAN->sign_profile_data_);
}

bool ProfileManager::DeleteLocalProfile(std::string profile_id) {
  Profile* profile = ProfileManager::GetLocalProfile(profile_id);
  ASSERT(profile != nullptr);
  std::string profile_dir = LocalProfileIDToDir(profile_id);

  // flush directory cache in an attempt to get this working
  FILEMAN->FlushDirCache(profile_dir);

  for (std::vector<DirAndProfile>::iterator i = g_vLocalProfile.begin();
       i != g_vLocalProfile.end(); ++i) {
    if (i->dir_ == profile_dir) {
      if (DeleteRecursive(profile_dir)) {
        g_vLocalProfile.erase(i);

        // Delete all references to this profileID
        for (Preference<std::string>* j : default_local_profile_id_.v_) {
          if (j->Get() == profile_id) {
            j->Set("");
          }
        }
        return true;
      } else {
        LOG->Warn(
            "[ProfileManager::DeleteLocalProfile] DeleteRecursive(%s) failed",
            profile_id.c_str());
        return false;
      }
    }
  }

  LOG->Warn(
      "DeleteLocalProfile: ProfileID '%s' doesn't exist", profile_id.c_str());
  return false;
}

void ProfileManager::SaveMachineProfile() const {
  // If the machine name has changed, make sure we use the new name.
  // It's important that this name be applied before the Player profiles
  // are saved, so that the Player's profiles show the right machine name.
  const_cast<ProfileManager*>(this)->machine_profile_->display_name_ =
      PREFSMAN->machine_name_;

  machine_profile_->SaveAllToDir(
      MACHINE_PROFILE_DIR, false); /* don't sign machine profiles */
}

void ProfileManager::LoadMachineProfile() {
  ProfileLoadResult lr =
      machine_profile_->LoadAllFromDir(MACHINE_PROFILE_DIR, false);
  if (lr == ProfileLoadResult_FailedNoProfile) {
    machine_profile_->InitAll();
    machine_profile_->SaveAllToDir(
        MACHINE_PROFILE_DIR, false); /* don't sign machine profiles */
  }

  // If the machine name has changed, make sure we use the new name
  machine_profile_->display_name_ = PREFSMAN->machine_name_;

  LoadMachineProfileEdits();
}

void ProfileManager::LoadMachineProfileEdits() {
  SONGMAN->FreeAllLoadedFromProfile(ProfileSlot_Machine);
  SONGMAN->LoadStepEditsFromProfileDir(
      MACHINE_PROFILE_DIR, ProfileSlot_Machine);
  SONGMAN->LoadCourseEditsFromProfileDir(
      MACHINE_PROFILE_DIR, ProfileSlot_Machine);
}

bool ProfileManager::ProfileWasLoadedFromMemoryCard(PlayerNumber pn) const {
  return !profile_dir_[pn].empty() && was_loaded_from_memory_card_[pn];
}

bool ProfileManager::ProfileFromMemoryCardIsNew(PlayerNumber pn) const {
  return GetProfile(pn) && was_loaded_from_memory_card_[pn] && new_profile_[pn];
}

bool ProfileManager::LastLoadWasTamperedOrCorrupt(PlayerNumber pn) const {
  return !profile_dir_[pn].empty() && last_load_was_tampered_or_corrupt_[pn];
}

bool ProfileManager::LastLoadWasFromLastGood(PlayerNumber pn) const {
  return !profile_dir_[pn].empty() && last_load_was_from_last_good_[pn];
}

const std::string& ProfileManager::GetProfileDir(ProfileSlot slot) const {
  switch (slot) {
    case ProfileSlot_Player1:
    case ProfileSlot_Player2:
      return profile_dir_[slot];
    case ProfileSlot_Machine:
      return MACHINE_PROFILE_DIR;
    default:
      FAIL_M("Invalid profile slot chosen: unable to get the directory!");
  }
}

const Profile* ProfileManager::GetProfile(ProfileSlot slot) const {
  switch (slot) {
    case ProfileSlot_Player1:
    case ProfileSlot_Player2:
      return GetProfile((PlayerNumber)slot);
    case ProfileSlot_Machine:
      return machine_profile_;
    default:
      FAIL_M("Invalid profile slot chosen: unable to get the profile!");
  }
}

void ProfileManager::MergeLocalProfiles(
    const std::string& from_id, const std::string& to_id) {
  Profile* from = GetLocalProfile(from_id);
  Profile* to = GetLocalProfile(to_id);
  if (from == nullptr || to == nullptr) {
    return;
  }
  to->MergeScoresFromOtherProfile(
      from, false, LocalProfileIDToDir(from_id), LocalProfileIDToDir(to_id));
}

void ProfileManager::MergeLocalProfileIntoMachine(
    const std::string& from_id, bool skip_totals) {
  Profile* from = GetLocalProfile(from_id);
  if (from == nullptr) {
    return;
  }
  GetMachineProfile()->MergeScoresFromOtherProfile(
      from, skip_totals, LocalProfileIDToDir(from_id), MACHINE_PROFILE_DIR);
}

void ProfileManager::ChangeProfileType(int index, ProfileType new_type) {
  if (index < 0 || static_cast<size_t>(index) >= g_vLocalProfile.size()) {
    return;
  }
  if (new_type == g_vLocalProfile[index].profile_.type_) {
    return;
  }
  DirAndProfile derp = g_vLocalProfile[index];
  g_vLocalProfile.erase(g_vLocalProfile.begin() + index);
  derp.profile_.type_ = new_type;
  InsertProfileIntoList(derp);
}

void ProfileManager::MoveProfileTopBottom(int index, bool top) {
  if (index < 0 || static_cast<size_t>(index) >= g_vLocalProfile.size()) {
    return;
  }

  int swindex = 0;
  // There may be guest profiles at the top of the list, so we need to skip over
  // them if moving to the top. If we're moving the profile to the bottom we
  // should stop once we find the first test profile.
  for (size_t i = 0; i < g_vLocalProfile.size(); ++i) {
    ProfileType type = g_vLocalProfile[i].profile_.type_;
    if (!top) {
      if (type == ProfileType_Test) {
        break;
      }
    } else {
      if (type != ProfileType_Guest) {
        break;
      }
    }
    swindex++;
  }
  if ((top && index < swindex) || (!top && index > swindex)) {
    return;
  }

  // Save the profile to move
  DirAndProfile profile = g_vLocalProfile[index];
  // Remove the profile from its current position
  g_vLocalProfile.erase(g_vLocalProfile.begin() + index);
  // Insert the profile at the beginning of the list
  g_vLocalProfile.insert(g_vLocalProfile.begin() + swindex, profile);
}

void ProfileManager::MoveProfileSorted(int index, bool ascending) {
  if (index < 0 || static_cast<size_t>(index) >= g_vLocalProfile.size()) {
    return;
  }

  int swindex = 0;
  // There may be guest profiles at the top of the list, so we need to skip over
  // them.
  for (size_t i = 0; i < g_vLocalProfile.size(); ++i) {
    ProfileType type = g_vLocalProfile[i].profile_.type_;
    if (type != ProfileType_Guest) {
      break;
    }
    swindex++;
  }

  if (index < swindex) {
    return;
  }

  // Copy the profile to be moved
  DirAndProfile temp = g_vLocalProfile[index];

  // Remove the profile from the list
  g_vLocalProfile.erase(g_vLocalProfile.begin() + index);

  // Find the correct location to insert the profile
  auto it = std::lower_bound(
      g_vLocalProfile.begin() + swindex, g_vLocalProfile.end(), temp,
      [ascending](const DirAndProfile& a, const DirAndProfile& b) {
        if (ascending) {
          return a.profile_.display_name_ < b.profile_.display_name_;
        } else {
          return a.profile_.display_name_ > b.profile_.display_name_;
        }
      });
  g_vLocalProfile.insert(it, temp);
}

void ProfileManager::MoveProfilePriority(int index, bool up) {
  if (index < 0 || static_cast<size_t>(index) >= g_vLocalProfile.size()) {
    return;
  }
  // Changing the priority is complicated a bit because the profiles might
  // all have the same priority.  So this function has to assign priorities
  // to all the profiles of the same type.
  // bools are numbers, true evaluatues to 1.
  int swindex = index + ((up * -2) + 1);
  ProfileType type = g_vLocalProfile[index].profile_.type_;
  int priority = 0;
  for (size_t i = 0; i < g_vLocalProfile.size(); ++i) {
    DirAndProfile* curr = &g_vLocalProfile[i];
    if (curr->profile_.type_ == type) {
      if (curr->profile_.list_priority_ != priority) {
        curr->profile_.list_priority_ = priority;
        if (i != static_cast<size_t>(index) &&
            i != static_cast<size_t>(swindex)) {
          curr->profile_.SaveTypeToDir(curr->dir_);
        }
      }
      ++priority;
    } else if (curr->profile_.type_ > type) {
      break;
    }
  }
  // Only swap if both indices are valid and the types match.
  if (swindex >= 0 && static_cast<size_t>(swindex) < g_vLocalProfile.size() &&
      g_vLocalProfile[swindex].profile_.type_ ==
          g_vLocalProfile[index].profile_.type_) {
    g_vLocalProfile[index].swap(g_vLocalProfile[swindex]);
  }
}

//
// General
//
void ProfileManager::IncrementToastiesCount(PlayerNumber pn) {
  if (IsPersistentProfile(pn)) {
    ++GetProfile(pn)->num_toasties_;
  }
  ++GetMachineProfile()->num_toasties_;
}

void ProfileManager::AddStepTotals(
    PlayerNumber pn, int num_taps_and_holds, int num_jumps, int num_holds,
    int num_rolls, int num_mines, int num_hands, int num_lifts,
    float calories_burned) {
  if (IsPersistentProfile(pn)) {
    GetProfile(pn)->AddStepTotals(
        num_taps_and_holds, num_jumps, num_holds, num_rolls, num_mines,
        num_hands, num_lifts, calories_burned);
  }
  GetMachineProfile()->AddStepTotals(
      num_taps_and_holds, num_jumps, num_holds, num_rolls, num_mines, num_hands,
      num_lifts, calories_burned);
}

//
// Song stats
//
int ProfileManager::GetSongNumTimesPlayed(
    const Song* song, ProfileSlot slot) const {
  return GetProfile(slot)->GetSongNumTimesPlayed(song);
}

void ProfileManager::AddStepsScore(
    const Song* song, const Steps* steps, PlayerNumber pn,
    const HighScore& in_hs, int& personal_index_out, int& machine_index_out) {
  HighScore hs = in_hs;
  hs.SetPercentDP(
      std::max(0.0f, hs.GetPercentDP()));  // bump up negative scores

  personal_index_out = -1;
  machine_index_out = -1;

  // In event mode, set the score's name immediately to the Profile's last
  // used name.  If no profile last used name exists, use "EVNT".
  // Exception: routine/couples scores are entered by the master player on
  // behalf of the team after the song, so keep the per-player marker here
  // and let GameState::StoreRankingName replace it with the typed name.
  if (GAMESTATE->IsEventMode() && !hs.IsRoutine()) {
    Profile* profile = GetProfile(pn);
    if (profile && !profile->last_used_high_score_name_.empty()) {
      hs.SetName(profile->last_used_high_score_name_);
    } else {
      hs.SetName("EVNT");
    }
  } else {
    hs.SetName(RANKING_TO_FILL_IN_MARKER[pn]);
  }

  //
  // save high score
  //
  if (IsPersistentProfile(pn)) {
    GetProfile(pn)->AddStepsHighScore(song, steps, hs, personal_index_out);
  }

  // don't save machine scores for a failed song
  if (hs.GetPercentDP() >=
          PREFSMAN->min_percentage_for_machine_song_high_score_ &&
      hs.GetGrade() != Grade_Failed) {
    // don't leave machine high scores for edits loaded from the player's card
    if (!steps->IsAPlayerEdit()) {
      GetMachineProfile()->AddStepsHighScore(
          song, steps, hs, machine_index_out);
    }
  }

  /*
  // save recent score
  if( IsPersistentProfile(pn) )
          GetProfile(pn)->SaveStepsRecentScore( pSong, pSteps, hs );
  GetMachineProfile()->SaveStepsRecentScore( pSong, pSteps, hs );
  */
}

void ProfileManager::IncrementStepsPlayCount(
    const Song* song, const Steps* steps, PlayerNumber pn) {
  if (IsPersistentProfile(pn)) {
    GetProfile(pn)->IncrementStepsPlayCount(song, steps);
  }
  GetMachineProfile()->IncrementStepsPlayCount(song, steps);
}

// Course stats
void ProfileManager::AddCourseScore(
    const Course* course, const Trail* trail, PlayerNumber pn,
    const HighScore& in_hs, int& personal_index_out, int& machine_index_out) {
  HighScore hs = in_hs;
  hs.SetPercentDP(
      std::max(0.0f, hs.GetPercentDP()));  // bump up negative scores

  personal_index_out = -1;
  machine_index_out = -1;

  // In event mode, set the score's name immediately to the Profile's last
  // used name. If no profile last used name exists, use "EVNT".
  if (GAMESTATE->IsEventMode()) {
    Profile* profile = GetProfile(pn);
    if (profile && !profile->last_used_high_score_name_.empty()) {
      hs.SetName(profile->last_used_high_score_name_);
    } else {
      hs.SetName("EVNT");
    }
  } else {
    hs.SetName(RANKING_TO_FILL_IN_MARKER[pn]);
  }

  // save high score
  if (IsPersistentProfile(pn)) {
    GetProfile(pn)->AddCourseHighScore(course, trail, hs, personal_index_out);
  }
  if (hs.GetPercentDP() >=
      PREFSMAN->min_percentage_for_machine_course_high_score_) {
    GetMachineProfile()->AddCourseHighScore(
        course, trail, hs, machine_index_out);
  }

  /*
  // save recent score
  if( IsPersistentProfile(pn) )
          GetProfile(pn)->SaveCourseRecentScore( pCourse, pTrail, hs );
  GetMachineProfile()->SaveCourseRecentScore( pCourse, pTrail, hs );
  */
}

void ProfileManager::IncrementCoursePlayCount(
    const Course* course, const Trail* trail, PlayerNumber pn) {
  if (IsPersistentProfile(pn)) {
    GetProfile(pn)->IncrementCoursePlayCount(course, trail);
  }
  GetMachineProfile()->IncrementCoursePlayCount(course, trail);
}

// Category stats
void ProfileManager::AddCategoryScore(
    StepsType st, RankingCategory rc, PlayerNumber pn, const HighScore& in_hs,
    int& personal_index_out, int& machine_index_out) {
  HighScore hs = in_hs;
  hs.SetName(RANKING_TO_FILL_IN_MARKER[pn]);
  if (IsPersistentProfile(pn)) {
    GetProfile(pn)->AddCategoryHighScore(st, rc, hs, personal_index_out);
  }
  if (hs.GetPercentDP() >
      PREFSMAN->min_percentage_for_machine_song_high_score_) {
    GetMachineProfile()->AddCategoryHighScore(st, rc, hs, machine_index_out);
  }
}

void ProfileManager::IncrementCategoryPlayCount(
    StepsType st, RankingCategory rc, PlayerNumber pn) {
  if (IsPersistentProfile(pn)) {
    GetProfile(pn)->IncrementCategoryPlayCount(st, rc);
  }
  GetMachineProfile()->IncrementCategoryPlayCount(st, rc);
}

bool ProfileManager::IsPersistentProfile(ProfileSlot slot) const {
  switch (slot) {
    case ProfileSlot_Player1:
    case ProfileSlot_Player2:
      return GAMESTATE->IsHumanPlayer((PlayerNumber)slot) &&
             !profile_dir_[slot].empty();
    case ProfileSlot_Machine:
      return true;
    default:
      FAIL_M("Invalid profile slot chosen: unable to get profile info!");
  }
}

void ProfileManager::GetLocalProfileIDs(
    std::vector<std::string>& profile_ids_out) const {
  profile_ids_out.clear();
  for (const DirAndProfile& i : g_vLocalProfile) {
    std::string id = LocalProfileDirToID(i.dir_);
    profile_ids_out.push_back(id);
  }
}

void ProfileManager::GetLocalProfileDisplayNames(
    std::vector<std::string>& profile_display_names_out) const {
  profile_display_names_out.clear();
  for (const DirAndProfile& i : g_vLocalProfile) {
    profile_display_names_out.push_back(i.profile_.display_name_);
  }
}

int ProfileManager::GetLocalProfileIndexFromID(std::string profile_id) const {
  std::string dir = LocalProfileIDToDir(profile_id);
  int j = 0;
  for (const DirAndProfile& i : g_vLocalProfile) {
    if (i.dir_ == dir) {
      return j;
    }
    ++j;
  }
  return -1;
}

std::string ProfileManager::GetLocalProfileIDFromIndex(int index) {
  std::string id = LocalProfileDirToID(g_vLocalProfile[index].dir_);
  return id;
}

Profile* ProfileManager::GetLocalProfileFromIndex(int index) {
  return &g_vLocalProfile[index].profile_;
}

int ProfileManager::GetNumLocalProfiles() const {
  return g_vLocalProfile.size();
}

void ProfileManager::SetStatsPrefix(const std::string& prefix) {
  stats_prefix_ = prefix;
  for (size_t i = 0; i < g_vLocalProfile.size(); ++i) {
    g_vLocalProfile[i].profile_.HandleStatsPrefixChange(
        g_vLocalProfile[i].dir_, PREFSMAN->sign_profile_data_);
  }
  FOREACH_PlayerNumber(pn) {
    if (ProfileWasLoadedFromMemoryCard(pn)) {
      const bool was_mounted = MEMCARDMAN->IsMounted(pn);
      if (!was_mounted && !MEMCARDMAN->MountCard(pn)) {
        stats_prefix_change_failed_[pn] = true;
        continue;
      }

      stats_prefix_change_failed_[pn] =
          !GetProfile(pn)->HandleStatsPrefixChange(
              profile_dir_[pn], PREFSMAN->sign_profile_data_);

      if (!was_mounted) {
        MEMCARDMAN->UnmountCard(pn);
      }
    }
  }
  machine_profile_->HandleStatsPrefixChange(MACHINE_PROFILE_DIR, false);
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the ProfileManager. */
class LunaProfileManager : public Luna<ProfileManager> {
 public:
  static int GetStatsPrefix(T* p, lua_State* L) {
    lua_pushstring(L, p->GetStatsPrefix().c_str());
    return 1;
  }
  static int SetStatsPrefix(T* p, lua_State* L) {
    std::string prefix = SArg(1);
    p->SetStatsPrefix(prefix);
    COMMON_RETURN_SELF;
  }
  static int IsPersistentProfile(T* p, lua_State* L) {
    lua_pushboolean(L, p->IsPersistentProfile(Enum::Check<PlayerNumber>(L, 1)));
    return 1;
  }
  static int GetProfile(T* p, lua_State* L) {
    PlayerNumber pn = Enum::Check<PlayerNumber>(L, 1);
    Profile* p_p = p->GetProfile(pn);
    ASSERT(p_p != nullptr);
    p_p->PushSelf(L);
    return 1;
  }
  static int GetMachineProfile(T* p, lua_State* L) {
    p->GetMachineProfile()->PushSelf(L);
    return 1;
  }
  static int SaveMachineProfile(T* p, lua_State*) {
    p->SaveMachineProfile();
    return 0;
  }
  static int GetLocalProfile(T* p, lua_State* L) {
    Profile* profile = p->GetLocalProfile(SArg(1));
    if (profile) {
      profile->PushSelf(L);
    } else {
      lua_pushnil(L);
    }
    return 1;
  }
  static int GetLocalProfileFromIndex(T* p, lua_State* L) {
    int index = IArg(1);
    if (index >= p->GetNumLocalProfiles()) {
      luaL_error(L, "Profile index %d out of range.", index);
    }
    Profile* profile = p->GetLocalProfileFromIndex(index);
    if (profile == nullptr) {
      luaL_error(L, "No profile at index %d.", index);
    }
    profile->PushSelf(L);
    return 1;
  }
  static int GetLocalProfileIDFromIndex(T* p, lua_State* L) {
    int index = IArg(1);
    if (index >= p->GetNumLocalProfiles()) {
      luaL_error(L, "Profile index %d out of range.", index);
    }
    lua_pushstring(L, p->GetLocalProfileIDFromIndex(index).c_str());
    return 1;
  }
  static int GetLocalProfileIndexFromID(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetLocalProfileIndexFromID(SArg(1)));
    return 1;
  }
  static int GetNumLocalProfiles(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetNumLocalProfiles());
    return 1;
  }
  static int GetProfileDir(T* p, lua_State* L) {
    lua_pushstring(L, p->GetProfileDir(Enum::Check<ProfileSlot>(L, 1)).c_str());
    return 1;
  }
  static int IsSongNew(T* p, lua_State* L) {
    lua_pushboolean(L, p->IsSongNew(Luna<Song>::check(L, 1)));
    return 1;
  }
  static int ProfileWasLoadedFromMemoryCard(T* p, lua_State* L) {
    lua_pushboolean(
        L, p->ProfileWasLoadedFromMemoryCard(Enum::Check<PlayerNumber>(L, 1)));
    return 1;
  }
  static int LastLoadWasTamperedOrCorrupt(T* p, lua_State* L) {
    lua_pushboolean(
        L, p->LastLoadWasTamperedOrCorrupt(Enum::Check<PlayerNumber>(L, 1)));
    return 1;
  }
  static int GetPlayerName(T* p, lua_State* L) {
    PlayerNumber pn = Enum::Check<PlayerNumber>(L, 1);
    lua_pushstring(L, p->GetPlayerName(pn).c_str());
    return 1;
  }
  static int GetProfileFromGuid(T* p, lua_State* L) {
    Profile* profile = p->GetProfileByGuid(SArg(1));
    if (profile == nullptr) {
      lua_pushnil(L);
      return 1;
    }
    profile->PushSelf(L);
    return 1;
  }

  static int LocalProfileIDToDir(T*, lua_State* L) {
    std::string dir = USER_PROFILES_DIR + SArg(1) + "/";
    lua_pushstring(L, dir.c_str());
    return 1;
  }
  static int SaveProfile(T* p, lua_State* L) {
    lua_pushboolean(L, p->SaveProfile(Enum::Check<PlayerNumber>(L, 1)));
    return 1;
  }
  static int SaveLocalProfile(T* p, lua_State* L) {
    lua_pushboolean(L, p->SaveLocalProfile(SArg(1)));
    return 1;
  }
  static int ProfileFromMemoryCardIsNew(T* p, lua_State* L) {
    lua_pushboolean(
        L, p->ProfileFromMemoryCardIsNew(Enum::Check<PlayerNumber>(L, 1)));
    return 1;
  }
  static int GetSongNumTimesPlayed(T* p, lua_State* L) {
    lua_pushnumber(
        L, p->GetSongNumTimesPlayed(
               Luna<Song>::check(L, 1), Enum::Check<ProfileSlot>(L, 2)));
    return 1;
  }
  static int GetLocalProfileIDs(T* p, lua_State* L) {
    std::vector<std::string> profile_ids;
    p->GetLocalProfileIDs(profile_ids);
    LuaHelpers::CreateTableFromArray<std::string>(profile_ids, L);
    return 1;
  }
  static int GetLocalProfileDisplayNames(T* p, lua_State* L) {
    std::vector<std::string> profile_names;
    p->GetLocalProfileDisplayNames(profile_names);
    LuaHelpers::CreateTableFromArray<std::string>(profile_names, L);
    return 1;
  }

  LunaProfileManager() {
    ADD_METHOD(GetStatsPrefix);
    ADD_METHOD(SetStatsPrefix);
    ADD_METHOD(IsPersistentProfile);
    ADD_METHOD(GetProfile);
    ADD_METHOD(GetMachineProfile);
    ADD_METHOD(SaveMachineProfile);
    ADD_METHOD(GetLocalProfile);
    ADD_METHOD(GetLocalProfileFromIndex);
    ADD_METHOD(GetLocalProfileIDFromIndex);
    ADD_METHOD(GetLocalProfileIndexFromID);
    ADD_METHOD(GetNumLocalProfiles);
    ADD_METHOD(GetProfileDir);
    ADD_METHOD(IsSongNew);
    ADD_METHOD(ProfileWasLoadedFromMemoryCard);
    ADD_METHOD(LastLoadWasTamperedOrCorrupt);
    ADD_METHOD(GetPlayerName);
    ADD_METHOD(GetProfileFromGuid);
    //
    ADD_METHOD(SaveProfile);
    ADD_METHOD(SaveLocalProfile);
    ADD_METHOD(ProfileFromMemoryCardIsNew);
    ADD_METHOD(GetSongNumTimesPlayed);
    ADD_METHOD(GetLocalProfileIDs);
    ADD_METHOD(GetLocalProfileDisplayNames);
    ADD_METHOD(LocalProfileIDToDir);
  }
};

LUA_REGISTER_CLASS(ProfileManager)
// lua end

/*
 * (c) 2003-2004 Chris Danford
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
