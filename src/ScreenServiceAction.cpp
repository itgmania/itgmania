#include "ScreenServiceAction.h"

#include <algorithm>
#include <string>
#include <vector>

#include "Bookkeeper.h"
#include "GameConstantsAndTypes.h"
#include "GameState.h"
#include "LocalizedString.h"
#include "MemoryCardManager.h"
#include "NotesLoaderSSC.h"
#include "PlayerNumber.h"
#include "PrefsManager.h"
#include "Profile.h"
#include "ProfileManager.h"
#include "RageFileManager.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "Screen.h"
#include "ScreenPrompt.h"
#include "StepMania.h"
#include "ThemeManager.h"
#include "global.h"

static LocalizedString BOOKKEEPING_DATA_CLEARED(
    "ScreenServiceAction", "Bookkeeping data cleared.");
static std::string ClearBookkeepingData() {
  BOOKKEEPER->ClearAll();
  BOOKKEEPER->WriteToDisk();
  return BOOKKEEPING_DATA_CLEARED.GetValue();
}

static LocalizedString MACHINE_STATS_CLEARED(
    "ScreenServiceAction", "Machine stats cleared.");
std::string ClearMachineStats() {
  Profile* profile = PROFILEMAN->GetMachineProfile();
  profile->ClearStats();
  PROFILEMAN->SaveMachineProfile();
  return MACHINE_STATS_CLEARED.GetValue();
}

static LocalizedString MACHINE_EDITS_CLEARED(
    "ScreenServiceAction", "%d edits cleared, %d errors.");
static std::string ClearMachineEdits() {
  std::vector<std::string> edit_files;
  GetDirListing(
      PROFILEMAN->GetProfileDir(ProfileSlot_Machine) + EDIT_STEPS_SUBDIR +
          "*.edit",
      edit_files, false, true);
  GetDirListing(
      PROFILEMAN->GetProfileDir(ProfileSlot_Machine) + EDIT_COURSES_SUBDIR +
          "*.crs",
      edit_files, false, true);

  int edit_count = edit_files.size();
  int removed_count = std::count_if(
      edit_files.begin(), edit_files.end(),
      [](const std::string& i) { return FILEMAN->Remove(i); });

  // reload the machine profile
  PROFILEMAN->SaveMachineProfile();
  PROFILEMAN->LoadMachineProfile();

  int error_count = edit_count - removed_count;
  return ssprintf(
      MACHINE_EDITS_CLEARED.GetValue().c_str(), edit_count, error_count);
}

static PlayerNumber GetFirstReadyMemoryCard() {
  FOREACH_PlayerNumber(pn) {
    if (MEMCARDMAN->GetCardState(pn) != MemoryCardState_Ready) {
      continue;  // skip
    }

    if (!MEMCARDMAN->IsMounted(pn)) {
      MEMCARDMAN->MountCard(pn);
    }
    return pn;
  }

  return PLAYER_INVALID;
}

static LocalizedString MEMORY_CARD_EDITS_NOT_CLEARED(
    "ScreenServiceAction", "Edits not cleared - No memory cards ready.");
static LocalizedString EDITS_CLEARED(
    "ScreenServiceAction", "%d edits cleared, %d errors.");
static std::string ClearMemoryCardEdits() {
  PlayerNumber pn = GetFirstReadyMemoryCard();
  if (pn == PLAYER_INVALID) {
    return MEMORY_CARD_EDITS_NOT_CLEARED.GetValue();
  }

  if (!MEMCARDMAN->IsMounted(pn)) {
    MEMCARDMAN->MountCard(pn);
  }

  std::string dir = MEM_CARD_MOUNT_POINT[pn] +
                    (std::string)PREFSMAN->memory_card_profile_subdir_ + "/";
  std::vector<std::string> edit_files;
  GetDirListing(dir + EDIT_STEPS_SUBDIR + "*.edit", edit_files, false, true);
  GetDirListing(dir + EDIT_COURSES_SUBDIR + "*.crs", edit_files, false, true);
  int edit_count = edit_files.size();
  int removed_count = std::count_if(
      edit_files.begin(), edit_files.end(),
      [](const std::string& i) { return FILEMAN->Remove(i); });

  MEMCARDMAN->UnmountCard(pn);

  return ssprintf(
      EDITS_CLEARED.GetValue().c_str(), edit_count, edit_count - removed_count);
}

static LocalizedString STATS_NOT_SAVED(
    "ScreenServiceAction", "Stats not saved - No memory cards ready.");
static LocalizedString MACHINE_STATS_SAVED(
    "ScreenServiceAction", "Machine stats saved to P%d card.");
static LocalizedString ERROR_SAVING_MACHINE_STATS(
    "ScreenServiceAction", "Error saving machine stats to P%d card.");
static std::string TransferStatsMachineToMemoryCard() {
  PlayerNumber pn = GetFirstReadyMemoryCard();
  if (pn == PLAYER_INVALID) {
    return STATS_NOT_SAVED.GetValue();
  }

  if (!MEMCARDMAN->IsMounted(pn)) {
    MEMCARDMAN->MountCard(pn);
  }

  std::string dir = MEM_CARD_MOUNT_POINT[pn];
  dir += "MachineProfile/";

  bool saved = PROFILEMAN->GetMachineProfile()->SaveAllToDir(
      dir, PREFSMAN->sign_profile_data_);

  MEMCARDMAN->UnmountCard(pn);

  if (saved) {
    return ssprintf(MACHINE_STATS_SAVED.GetValue().c_str(), pn + 1);
  } else {
    return ssprintf(ERROR_SAVING_MACHINE_STATS.GetValue().c_str(), pn + 1);
  }
}

static LocalizedString STATS_NOT_LOADED(
    "ScreenServiceAction", "Stats not loaded - No memory cards ready.");
static LocalizedString MACHINE_STATS_LOADED(
    "ScreenServiceAction", "Machine stats loaded from P%d card.");
static LocalizedString THERE_IS_NO_PROFILE(
    "ScreenServiceAction", "There is no machine profile on P%d card.");
static LocalizedString PROFILE_CORRUPT(
    "ScreenServiceAction",
    "The profile on P%d card contains corrupt or tampered data.");
static std::string TransferStatsMemoryCardToMachine() {
  PlayerNumber pn = GetFirstReadyMemoryCard();
  if (pn == PLAYER_INVALID) {
    return STATS_NOT_LOADED.GetValue();
  }

  if (!MEMCARDMAN->IsMounted(pn)) {
    MEMCARDMAN->MountCard(pn);
  }

  std::string dir = MEM_CARD_MOUNT_POINT[pn];
  dir += "MachineProfile/";

  Profile backup = *PROFILEMAN->GetMachineProfile();

  ProfileLoadResult lr = PROFILEMAN->GetMachineProfile()->LoadAllFromDir(
      dir, PREFSMAN->sign_profile_data_);
  std::string s;
  switch (lr) {
    case ProfileLoadResult_Success:
      s = ssprintf(MACHINE_STATS_LOADED.GetValue().c_str(), pn + 1);
      break;
    case ProfileLoadResult_FailedNoProfile:
      *PROFILEMAN->GetMachineProfile() = backup;
      s = ssprintf(THERE_IS_NO_PROFILE.GetValue().c_str(), pn + 1);
      break;
    case ProfileLoadResult_FailedTampered:
      *PROFILEMAN->GetMachineProfile() = backup;
      s = ssprintf(PROFILE_CORRUPT.GetValue().c_str(), pn + 1);
      break;
    default:
      FAIL_M(ssprintf("Invalid profile load result: %i", lr));
  }

  MEMCARDMAN->UnmountCard(pn);

  return s;
}

static void CopyEdits(
    const std::string& from_profile_dir, const std::string& to_profile_dir,
    int& num_succeeded, int& num_overwritten, int& num_ignored,
    int& num_errored) {
  num_succeeded = 0;
  num_overwritten = 0;
  num_ignored = 0;
  num_errored = 0;

  {
    std::string from_dir = from_profile_dir + EDIT_STEPS_SUBDIR;
    std::string to_dir = to_profile_dir + EDIT_STEPS_SUBDIR;

    std::vector<std::string> files;
    GetDirListing(from_dir + "*.edit", files, false, false);
    for (const std::string& i : files) {
      if (DoesFileExist(to_dir + i)) {
        num_overwritten++;
      }
      bool success = FileCopy(from_dir + i, to_dir + i);
      if (success) {
        num_succeeded++;
      } else {
        num_errored++;
      }

      // Test whether the song we need for this edit is present and ignore this
      // edit if not present.
      SSCLoader loader_ssc;
      if (!loader_ssc.LoadEditFromFile(
              from_dir + i, ProfileSlot_Machine, false)) {
        num_ignored++;
        continue;
      }
    }
  }

  // TODO: Seprarate copying stats for steps and courses

  {
    std::string from_dir = from_profile_dir + EDIT_COURSES_SUBDIR;
    std::string to_dir = to_profile_dir + EDIT_COURSES_SUBDIR;

    std::vector<std::string> files;
    GetDirListing(from_dir + "*.crs", files, false, false);
    for (const std::string& i : files) {
      if (DoesFileExist(to_dir + i)) {
        num_overwritten++;
      }
      bool success = FileCopy(from_dir + i, to_dir + i);
      if (success) {
        num_succeeded++;
      } else {
        num_errored++;
      }
    }
  }
}

static LocalizedString EDITS_NOT_COPIED(
    "ScreenServiceAction", "Edits not copied - No memory cards ready.");
static LocalizedString COPIED_TO_CARD(
    "ScreenServiceAction", "Copied to P%d card:");
static LocalizedString COPIED("ScreenServiceAction", "%d copied");
static LocalizedString OVERWRITTEN("ScreenServiceAction", "%d overwritten");
static LocalizedString ADDED("ScreenServiceAction", "%d added");
static LocalizedString IGNORED("ScreenServiceAction", "%d ignored");
static LocalizedString FAILED("ScreenServiceAction", "%d failed");
static LocalizedString DELETED("ScreenServiceAction", "%d deleted");

static std::string CopyEdits(
    const std::string& from_profile_dir, const std::string& to_profile_dir,
    const std::string& display_dir) {
  int num_succeeded = 0;
  int num_overwritten = 0;
  int num_ignored = 0;
  int num_errored = 0;

  CopyEdits(
      from_profile_dir, to_profile_dir, num_succeeded, num_overwritten,
      num_ignored, num_errored);

  std::vector<std::string> vs;
  vs.push_back(display_dir);
  vs.push_back(
      ssprintf(COPIED.GetValue().c_str(), num_succeeded) + ", " +
      ssprintf(OVERWRITTEN.GetValue().c_str(), num_overwritten));
  if (num_ignored) {
    vs.push_back(ssprintf(IGNORED.GetValue().c_str(), num_ignored));
  }
  if (num_errored) {
    vs.push_back(ssprintf(FAILED.GetValue().c_str(), num_errored));
  }
  return join("\n", vs);
}

static void SyncFiles(
    const std::string& from_dir, const std::string& to_dir,
    const std::string& mask, int& num_added, int& num_deleted,
    int& num_overwritten, int& num_failed) {
  std::vector<std::string> files_source;
  GetDirListing(from_dir + mask, files_source, false, false);

  std::vector<std::string> files_dest;
  GetDirListing(to_dir + mask, files_dest, false, false);

  std::vector<std::string> to_delete;
  GetAsNotInBs(files_dest, files_source, to_delete);

  for (unsigned i = 0; i < to_delete.size(); ++i) {
    std::string file = to_dir + to_delete[i];
    LOG->Trace("Delete \"%s\"", file.c_str());

    if (FILEMAN->Remove(file)) {
      ++num_deleted;
    } else {
      ++num_failed;
    }
  }

  for (unsigned i = 0; i < files_source.size(); ++i) {
    std::string file_from = from_dir + files_source[i];
    std::string file_to = to_dir + files_source[i];
    LOG->Trace("Copy \"%s\"", file_from.c_str());
    bool overwrite = DoesFileExist(file_to);
    bool success = FileCopy(file_from, file_to);
    if (success) {
      if (overwrite) {
        ++num_overwritten;
      } else {
        ++num_added;
      }
    } else {
      ++num_failed;
    }
  }
  FILEMAN->FlushDirCache(to_dir);
}

static void SyncEdits(
    const std::string& from_dir, const std::string& to_dir, int& num_added,
    int& num_deleted, int& num_overwritten, int& num_failed) {
  num_added = 0;
  num_deleted = 0;
  num_overwritten = 0;
  num_failed = 0;

  SyncFiles(
      from_dir + EDIT_STEPS_SUBDIR, to_dir + EDIT_STEPS_SUBDIR, "*.edit",
      num_added, num_deleted, num_overwritten, num_failed);
  SyncFiles(
      from_dir + EDIT_COURSES_SUBDIR, to_dir + EDIT_COURSES_SUBDIR, "*.crs",
      num_added, num_deleted, num_overwritten, num_failed);
}

static std::string CopyEditsMachineToMemoryCard() {
  PlayerNumber pn = GetFirstReadyMemoryCard();
  if (pn == PLAYER_INVALID) {
    return EDITS_NOT_COPIED.GetValue();
  }

  if (!MEMCARDMAN->IsMounted(pn)) {
    MEMCARDMAN->MountCard(pn);
  }

  std::string from_dir = PROFILEMAN->GetProfileDir(ProfileSlot_Machine);
  std::string to_dir = MEM_CARD_MOUNT_POINT[pn] +
                       (std::string)PREFSMAN->memory_card_profile_subdir_ + "/";

  std::vector<std::string> vs;
  vs.push_back(ssprintf(COPIED_TO_CARD.GetValue().c_str(), pn + 1));
  std::string s =
      CopyEdits(from_dir, to_dir, PREFSMAN->memory_card_profile_subdir_);
  vs.push_back(s);

  MEMCARDMAN->UnmountCard(pn);

  return join("\n\n", vs);
}

static std::string SyncEditsMachineToMemoryCard() {
  PlayerNumber pn = GetFirstReadyMemoryCard();
  if (pn == PLAYER_INVALID) {
    return EDITS_NOT_COPIED.GetValue();
  }

  if (!MEMCARDMAN->IsMounted(pn)) {
    MEMCARDMAN->MountCard(pn);
  }

  int num_added = 0;
  int num_deleted = 0;
  int num_overwritten = 0;
  int num_failed = 0;

  std::string from_dir = PROFILEMAN->GetProfileDir(ProfileSlot_Machine);
  std::string to_dir = MEM_CARD_MOUNT_POINT[pn] +
                       (std::string)PREFSMAN->memory_card_profile_subdir_ + "/";
  SyncEdits(
      from_dir, to_dir, num_added, num_deleted, num_overwritten, num_failed);

  MEMCARDMAN->UnmountCard(pn);

  std::string ret = ssprintf(COPIED_TO_CARD.GetValue().c_str(), pn + 1) + " ";
  ret += ssprintf(ADDED.GetValue().c_str(), num_added) + ", " +
         ssprintf(OVERWRITTEN.GetValue().c_str(), num_overwritten);
  if (num_deleted) {
    ret += std::string(" ") + ssprintf(DELETED.GetValue().c_str(), num_deleted);
  }
  if (num_failed) {
    ret += std::string("; ") + ssprintf(FAILED.GetValue().c_str(), num_failed);
  }
  return ret;
}

static LocalizedString COPIED_FROM_CARD(
    "ScreenServiceAction", "Copied from P%d card:");
static std::string CopyEditsMemoryCardToMachine() {
  PlayerNumber pn = GetFirstReadyMemoryCard();
  if (pn == PLAYER_INVALID) {
    return EDITS_NOT_COPIED.GetValue();
  }

  if (!MEMCARDMAN->IsMounted(pn)) {
    MEMCARDMAN->MountCard(pn);
  }

  std::vector<std::string> sub_dirs;
  ProfileManager::GetMemoryCardProfileDirectoriesToTry(sub_dirs);

  std::vector<std::string> vs;
  vs.push_back(ssprintf(COPIED_FROM_CARD.GetValue().c_str(), pn + 1));

  for (const std::string& sub_dir : sub_dirs) {
    std::string from_dir = MEM_CARD_MOUNT_POINT[pn] + sub_dir + "/";
    std::string to_dir = PROFILEMAN->GetProfileDir(ProfileSlot_Machine);

    std::string s = CopyEdits(from_dir, to_dir, sub_dir);
    vs.push_back(s);
  }

  MEMCARDMAN->UnmountCard(pn);

  // reload the machine profile
  PROFILEMAN->SaveMachineProfile();
  PROFILEMAN->LoadMachineProfile();

  return join("\n\n", vs);
}

static LocalizedString PREFERENCES_RESET(
    "ScreenServiceAction", "Preferences reset.");
static std::string ResetPreferences() {
  StepMania::ResetPreferences();
  return PREFERENCES_RESET.GetValue();
}

REGISTER_SCREEN_CLASS(ScreenServiceAction);
void ScreenServiceAction::BeginScreen() {
  std::string s_actions = THEME->GetMetric(name_, "Actions");
  std::vector<std::string> vs_actions;
  split(s_actions, ",", vs_actions);

  std::vector<std::string> results;
  for (const std::string& s : vs_actions) {
    std::string (*pfn)() = nullptr;

    if (s == "ClearBookkeepingData") {
      pfn = ClearBookkeepingData;
    } else if (s == "ClearMachineStats") {
      pfn = ClearMachineStats;
    } else if (s == "ClearMachineEdits") {
      pfn = ClearMachineEdits;
    } else if (s == "ClearMemoryCardEdits") {
      pfn = ClearMemoryCardEdits;
    } else if (s == "TransferStatsMachineToMemoryCard") {
      pfn = TransferStatsMachineToMemoryCard;
    } else if (s == "TransferStatsMemoryCardToMachine") {
      pfn = TransferStatsMemoryCardToMachine;
    } else if (s == "CopyEditsMachineToMemoryCard") {
      pfn = CopyEditsMachineToMemoryCard;
    } else if (s == "CopyEditsMemoryCardToMachine") {
      pfn = CopyEditsMemoryCardToMachine;
    } else if (s == "SyncEditsMachineToMemoryCard") {
      pfn = SyncEditsMachineToMemoryCard;
    } else if (s == "ResetPreferences") {
      pfn = ResetPreferences;
    }

    ASSERT_M(pfn != nullptr, s);

    std::string result = pfn();
    results.push_back(result);
  }

  ScreenPrompt::SetPromptSettings(join("\n", results), PROMPT_OK);

  ScreenPrompt::BeginScreen();
}

/*
 * (c) 2001-2005 Chris Danford
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
