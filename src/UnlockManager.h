/* UnlockManager - Unlocks handling. */

#ifndef UNLOCK_MANAGER_H
#define UNLOCK_MANAGER_H

#include <set>
#include <string>
#include <vector>

#include "Command.h"
#include "CourseUtil.h"
#include "Difficulty.h"
#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "RageUtil.h"
#include "SongUtil.h"

class Song;
class Course;
class Steps;
class Profile;
struct lua_State;

/** @brief What is needed to unlock an item? */
enum UnlockRequirement {
  UnlockRequirement_ArcadePoints, /**< Get a certain number of arcade points. */
  UnlockRequirement_DancePoints,  /**< Get a certain number of dance points. */
  UnlockRequirement_SongPoints,   /**< Get a certain number of song points. */
  UnlockRequirement_ExtraCleared, /**< Pass the extra stage. */
  UnlockRequirement_ExtraFailed,  /**< Fail the extra stage. */
  UnlockRequirement_Toasties,     /**< Get a number of toasties. */
  UnlockRequirement_StagesCleared, /**< Clear a number of stages. */
  UnlockRequirement_NumUnlocked,   /**< Have a number of locked items already
                                      unlocked. */
  NUM_UnlockRequirement,
  UnlockRequirement_Invalid,
};
LuaDeclareType(UnlockRequirement);

enum UnlockRewardType {
  UnlockRewardType_Song,  /**< A song is unlocked. */
  UnlockRewardType_Steps, /**< A step pattern for all styles is unlocked. */
  UnlockRewardType_Steps_Type, /**< A step pattern for a specific style is
                                  unlocked. */
  UnlockRewardType_Course,     /**< A course is unlocked. */
  UnlockRewardType_Modifier,   /**< A modifier is unlocked. */
  NUM_UnlockRewardType,
  UnlockRewardType_Invalid
};
const std::string& UnlockRewardTypeToString(UnlockRewardType i);
const std::string& UnlockRewardTypeToLocalizedString(UnlockRewardType i);
LuaDeclareType(UnlockRewardType);

enum UnlockEntryStatus {
  UnlockEntryStatus_RequrementsNotMet,
  UnlockEntryStatus_RequirementsMet,
  UnlockEntryStatus_Unlocked,
};

class UnlockEntry {
 public:
  /**
   * @brief Set up the UnlockEntry with default values.
   *
   * m_sEntryID starts as an empty string. It will be filled automatically
   * if not specified. */
  UnlockEntry()
      : type_(UnlockRewardType_Invalid),
        cmd_(),
        song_(),
        dc_(Difficulty_Invalid),
        course_(),
        steps_type_(StepsType_Invalid),
        require_pass_hard_steps_(false),
        require_pass_challenge_steps_(false),
        roulette_(false),
        entry_id_(std::string("")) {
    ZERO(requirement_);
  }

  UnlockRewardType type_;
  Command cmd_;

  /* A cached pointer to the song or course this entry refers to.  Only one of
   * these will be non-nullptr. */
  SongID song_;
  Difficulty dc_;
  CourseID course_;
  StepsType steps_type_;

  float requirement_[NUM_UnlockRequirement];  // unlocked if any of of these
                                              // are met
  /** @brief Must the hard steps be passed to unlock a higher level? */
  bool require_pass_hard_steps_;
  /** @brief Must the challenge steps be passed to unlock a higher level? */
  bool require_pass_challenge_steps_;
  bool roulette_;
  std::string entry_id_;

  bool IsValid() const;
  bool IsLocked() const {
    return GetUnlockEntryStatus() != UnlockEntryStatus_Unlocked;
  }
  UnlockEntryStatus GetUnlockEntryStatus() const;
  std::string GetModifier() const { return cmd_.GetArg(1).s_; }
  std::string GetDescription() const;
  std::string GetBannerFile() const;
  std::string GetBackgroundFile() const;

  // Lua
  void PushSelf(lua_State* L);
};

// Option is locked due to an unsatisfied unlock entry.
#define LOCKED_LOCK 0x1

// Option is only available in random selections.
#define LOCKED_ROULETTE 0x2

// Option is locked due to a #SELECTABLE tag.
#define LOCKED_SELECTABLE 0x4

// Option is disabled by the operator. (For courses, this means that a song in
// the course is disabled.)
#define LOCKED_DISABLED 0x8

class UnlockManager {
  friend class UnlockEntry;

 public:
  UnlockManager();
  ~UnlockManager();
  void Reload();

  float PointsUntilNextUnlock(UnlockRequirement t) const;
  int SongIsLocked(const Song* song) const;
  bool StepsIsLocked(const Song* song, const Steps* steps) const;
  int CourseIsLocked(const Course* course) const;
  bool ModifierIsLocked(const std::string& one_mod) const;

  // Gets number of unlocks for title screen
  int GetNumUnlocks() const;
  int GetNumUnlocked() const;
  int GetUnlockEntryIndexToCelebrate() const;
  bool AnyUnlocksToCelebrate() const;

  void GetPoints(
      const Profile* profile, float scores[NUM_UnlockRequirement]) const;

  // Unlock an entry by code.
  void UnlockEntryID(std::string entry_id);
  void UnlockEntryIndex(int entry_index);

  // Lock an entry by code.
  void LockEntryID(std::string entry_id);
  void LockEntryIndex(int entry_index);

  /*
   * If a code is associated with at least one song or course, set the preferred
   * song and/or course in GAMESTATE to them.
   */
  void PreferUnlockEntryID(std::string entry_id);

  // Return the associated EntryID.
  std::string FindEntryID(const std::string& name) const;

  // All locked songs are stored here
  std::vector<UnlockEntry> unlock_entries_;

  void GetUnlocksByType(UnlockRewardType t, std::vector<UnlockEntry*>& entries);
  void GetSongsUnlockedByEntryID(
      std::vector<Song*>& songs_out, std::string entry_id);
  void GetStepsUnlockedByEntryID(
      std::vector<Song*>& songs_out, std::vector<Difficulty>& steps_out,
      std::string entry_id);

  const UnlockEntry* FindSong(const Song* song) const;
  const UnlockEntry* FindSteps(const Song* song, const Steps* steps) const;
  const UnlockEntry* FindCourse(const Course* course) const;
  const UnlockEntry* FindModifier(const std::string& one_mod) const;

  // Lua
  void PushSelf(lua_State* L);

 private:
  // read unlocks
  void Load();

  std::set<std::string>
      roulette_codes_;  // "codes" which are available in roulette and which
                        // unlock if rouletted
};

extern UnlockManager*
    UNLOCKMAN;  // global and accessible from anywhere in program

#endif

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
