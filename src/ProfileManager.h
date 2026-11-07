#ifndef ProfileManager_H
#define ProfileManager_H

#include <string>
#include <vector>

#include "Difficulty.h"
#include "GameConstantsAndTypes.h"
#include "PlayerNumber.h"
#include "Preference.h"
#include "Profile.h"

class Song;
class Steps;
class Style;
class Course;
class Trail;
struct HighScore;
struct lua_State;

/** @brief Interface to machine and memory card profiles. */
class ProfileManager {
 public:
  ProfileManager();
  ~ProfileManager();

  void Init();

  bool FixedProfiles() const;  // If true, profiles shouldn't be added/deleted

  // local profiles
  void UnloadAllLocalProfiles();
  void RefreshLocalProfilesFromDisk();
  void LoadLocalProfilesByPriority();
  void LoadLocalProfilesByRecent();
  void LoadLocalProfilesByName();
  void LoadLocalProfilesByCreationTime();

  const Profile* GetLocalProfile(const std::string& profile_id) const;
  Profile* GetLocalProfile(const std::string& profile_id) {
    return (Profile*)((const ProfileManager*)this)->GetLocalProfile(profile_id);
  }
  Profile* GetLocalProfileFromIndex(int index);
  std::string GetLocalProfileIDFromIndex(int index);

  bool CreateLocalProfile(std::string name, std::string& profile_id_out);
  void AddLocalProfileByID(
      Profile* profile,
      std::string profile_id);  // transfers ownership of pProfile
  bool RenameLocalProfile(std::string profile_id, std::string new_name);
  bool DeleteLocalProfile(std::string profile_id);
  void GetLocalProfileIDs(std::vector<std::string>& profile_ids_out) const;
  void GetLocalProfileDisplayNames(
      std::vector<std::string>& profile_display_names_out) const;
  int GetLocalProfileIndexFromID(std::string profile_id) const;
  int GetNumLocalProfiles() const;

  std::string GetStatsPrefix() { return stats_prefix_; }
  void SetStatsPrefix(const std::string& prefix);

  bool LoadFirstAvailableProfile(
      PlayerNumber pn, bool load_edits = true);  // memory card or local profile
  bool LoadLocalProfileFromMachine(PlayerNumber pn);
  bool LoadProfileFromMemoryCard(PlayerNumber pn, bool load_edits = true);
  bool FastLoadProfileNameFromMemoryCard(
      std::string root_dir, std::string& name) const;
  bool SaveProfile(PlayerNumber pn) const;
  bool SaveLocalProfile(std::string profile_id);
  void UnloadProfile(PlayerNumber pn);

  void MergeLocalProfiles(const std::string& from_id, const std::string& to_id);
  void MergeLocalProfileIntoMachine(
      const std::string& from_id, bool skip_totals);
  void ChangeProfileType(int index, ProfileType new_type);
  void MoveProfilePriority(int index, bool up);
  void MoveProfileTopBottom(int index, bool top);
  void MoveProfileSorted(int index, bool ascending);

  // General data
  void IncrementToastiesCount(PlayerNumber pn);
  void AddStepTotals(
      PlayerNumber pn, int num_taps_and_holds, int num_jumps, int num_holds,
      int num_rolls, int num_mines, int num_hands, int num_lifts,
      float calories_burned);

  // High scores
  void LoadMachineProfile();  // including edits
  void LoadMachineProfileEdits();
  void SaveMachineProfile() const;

  bool IsPersistentProfile(PlayerNumber pn) const {
    return !profile_dir_[pn].empty();
  }
  bool IsPersistentProfile(ProfileSlot slot) const;

  // return a profile even if !IsUsingProfile
  const Profile* GetProfile(PlayerNumber pn) const;
  Profile* GetProfile(PlayerNumber pn) {
    return (Profile*)((const ProfileManager*)this)->GetProfile(pn);
  }
  const Profile* GetProfile(ProfileSlot slot) const;
  Profile* GetProfile(ProfileSlot slot) {
    return (Profile*)((const ProfileManager*)this)->GetProfile(slot);
  }
  const Profile* GetProfileByGuid(const std::string& guid) const;
  Profile* GetProfileByGuid(const std::string& guid) {
    return (Profile*)((const ProfileManager*)this)->GetProfileByGuid(guid);
  }

  const std::string& GetProfileDir(ProfileSlot slot) const;
  Profile* GetMachineProfile() { return machine_profile_; }

  std::string GetPlayerName(PlayerNumber pn) const;
  bool ProfileWasLoadedFromMemoryCard(PlayerNumber pn) const;
  bool ProfileFromMemoryCardIsNew(PlayerNumber pn) const;
  bool LastLoadWasTamperedOrCorrupt(PlayerNumber pn) const;
  bool LastLoadWasFromLastGood(PlayerNumber pn) const;

  // Song stats
  int GetSongNumTimesPlayed(const Song* song, ProfileSlot card) const;
  bool IsSongNew(const Song* song) const {
    return GetSongNumTimesPlayed(song, ProfileSlot_Machine) == 0;
  }
  void AddStepsScore(
      const Song* song, const Steps* steps, PlayerNumber pn,
      const HighScore& hs, int& personal_index_out, int& machine_index_out);
  void IncrementStepsPlayCount(
      const Song* song, const Steps* steps, PlayerNumber pn);

  // Course stats
  void AddCourseScore(
      const Course* course, const Trail* trail, PlayerNumber pn,
      const HighScore& hs, int& personal_index_out, int& machine_index_out);
  void IncrementCoursePlayCount(
      const Course* course, const Trail* trail, PlayerNumber pn);

  // Category stats
  void AddCategoryScore(
      StepsType st, RankingCategory rc, PlayerNumber pn, const HighScore& hs,
      int& personal_index_out, int& machine_index_out);
  void IncrementCategoryPlayCount(
      StepsType st, RankingCategory rc, PlayerNumber pn);

  static void GetMemoryCardProfileDirectoriesToTry(
      std::vector<std::string>& dirs_to_try);

  // Lua
  void PushSelf(lua_State* L);

  static Preference<bool> profile_step_edits_;
  static Preference<bool> profile_course_edits_;
  static Preference1D<std::string> default_local_profile_id_;

 private:
  ProfileLoadResult LoadProfile(
      PlayerNumber pn, std::string profile_dir, bool is_mem_card);

  // Directory that contains the profile.  Either on local machine or
  // on a memory card.
  std::string profile_dir_[NUM_PLAYERS];

  std::string stats_prefix_;

  bool was_loaded_from_memory_card_[NUM_PLAYERS];
  bool last_load_was_tampered_or_corrupt_
      [NUM_PLAYERS];  // true if Stats.xml was present, but failed to load
                      // (probably because of a signature failure)
  bool last_load_was_from_last_good_
      [NUM_PLAYERS];  // true if the primary Stats file was corrupt or missing
                      // and LastGood loaded successfully
  mutable bool need_to_back_up_last_load_[NUM_PLAYERS];  // if true, back up
                                                         // profile on next save
  bool new_profile_[NUM_PLAYERS];
  // prevent a bad save from overwriting Stats.xml after a prefix reload failed
  bool stats_prefix_change_failed_[NUM_PLAYERS];

  Profile* memory_card_profile_[NUM_PLAYERS];  // holds Profile for the
                                               // currently inserted card
  Profile* machine_profile_;
};

extern ProfileManager*
    PROFILEMAN;  // global and accessible from anywhere in our program

#endif

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
