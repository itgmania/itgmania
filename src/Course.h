#ifndef COURSE_H
#define COURSE_H

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "Attack.h"
#include "Difficulty.h"
#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "RadarValues.h"
#include "RageTypes.h"
#include "RageUtil/RandomNumbers.h"
#include "SongUtil.h"
#include "StepsUtil.h"
#include "Trail.h"

struct lua_State;
class Style;
struct Game;

inline PlayMode CourseTypeToPlayMode(CourseType ct) {
  switch (ct) {
    case COURSE_TYPE_NONSTOP:
      return PLAY_MODE_NONSTOP;
    case COURSE_TYPE_ONI:
      return PLAY_MODE_ONI;
    case COURSE_TYPE_ENDLESS:
      return PLAY_MODE_ENDLESS;
    case COURSE_TYPE_SURVIVAL:
      return PLAY_MODE_BATTLE;
    default:
      break;
  }
  return PlayMode_Invalid;
}

enum SongSort {
  SongSort_Randomize,
  SongSort_MostPlays,
  SongSort_FewestPlays,
  SongSort_TopGrades,
  SongSort_LowestGrades,
  NUM_SongSort,
  SongSort_Invalid,
};
const std::string& SongSortToString(SongSort ss);
const std::string& SongSortToLocalizedString(SongSort ss);

SongSort StringToSongSort(const std::string& ss);
SongSort OldStyleStringToSongSort(const std::string& ss);

class CourseEntry {
 public:
  bool secret_;           // show "??????" instead of an exact song
  bool use_song_select_;  // if true, this entry was created from a #SONGSELECT
                          // entry, instead of a #SONG entry

  // filter criteria, applied from top to bottom
  SongID song_id_;  // don't filter if unset
  SongCriteria song_criteria_;
  StepsCriteria steps_criteria_;
  bool no_difficult_;  // if true, CourseDifficulty doesn't affect this entry

  SongSort song_sort_;  // sort by this after filtering
  int choose_index_;    //

  std::string modifiers_;  // set player and song options using these
  AttackArray attacks_;    // timed sModifiers
  float gain_seconds_;     // time gained back at the beginning of the song.
                           // LifeMeterTime only.
  int gain_lives_;  // lives gained back at the beginning of the next song

  CourseEntry()
      : secret_(false),
        use_song_select_(false),
        song_id_(),
        song_criteria_(),
        steps_criteria_(),
        no_difficult_(false),
        song_sort_(SongSort_Randomize),
        choose_index_(0),
        modifiers_(std::string("")),
        attacks_(),
        gain_seconds_(0),
        gain_lives_(-1) {}

  bool IsFixedSong() const { return song_id_.IsValid(); }

  std::string GetTextDescription() const;
  int GetNumModChanges() const;

  // Lua
  void PushSelf(lua_State* L);
};

/** @brief A queue of songs and notes. */
class Course {
 public:
  Course();

  std::string GetBannerPath() const;
  std::string GetBackgroundPath() const;
  bool HasBanner() const;
  bool HasBackground() const;

  /* If PREFSMAN->m_bShowNative is off, these are the same as GetTranslit*
   * below. Otherwise, they return the main titles. */
  std::string GetDisplayMainTitle() const;
  std::string GetDisplaySubTitle() const;

  // Returns the transliterated titles, if any; otherwise returns the main
  // titles.
  std::string GetTranslitMainTitle() const {
    return main_title_translit_.size() ? main_title_translit_ : main_title_;
  }
  std::string GetTranslitSubTitle() const {
    return sub_title_translit_.size() ? sub_title_translit_ : sub_title_;
  }

  // "title subtitle"
  std::string GetDisplayFullTitle() const;
  std::string GetTranslitFullTitle() const;

  // Dereferences course_entries and returns only the playable Songs and Steps
  Trail* GetTrail(StepsType st, CourseDifficulty cd = Difficulty_Medium) const;
  Trail* GetTrailForceRegenCache(
      StepsType st, CourseDifficulty cd = Difficulty_Medium) const;
  void GetTrails(std::vector<Trail*>& add_to, StepsType st) const;
  void GetAllTrails(std::vector<Trail*>& add_to) const;
  int GetMeter(StepsType st, CourseDifficulty cd = Difficulty_Medium) const;
  bool HasMods() const;
  bool HasTimedMods() const;
  bool AllSongsAreFixed() const;
  const Style* GetCourseStyle(const Game* game, int num_players) const;

  int GetEstimatedNumStages() const { return entries_.size(); }
  bool IsPlayableIn(StepsType st) const;
  bool CourseHasBestOrWorst() const;
  RageColor GetColor() const;
  bool GetTotalSeconds(StepsType st, float& seconds_out) const;

  bool IsNonstop() const { return GetPlayMode() == PLAY_MODE_NONSTOP; }
  bool IsOni() const { return GetPlayMode() == PLAY_MODE_ONI; }
  bool IsEndless() const { return GetPlayMode() == PLAY_MODE_ENDLESS; }
  CourseType GetCourseType() const;
  PlayMode GetPlayMode() const;

  bool ShowInDemonstrationAndRanking() const;

  void RevertFromDisk();
  void Init();

  bool IsRanking() const;

  void UpdateCourseStats(StepsType st);

  // Call to regenerate Trails with random entries
  void RegenerateNonFixedTrails() const;

  void InvalidateTrailCache();

  // Call when a Song or its Steps are deleted/changed.
  void Invalidate(const Song* stale_song);

  std::string GetCacheFilePath() const;

  const CourseEntry* FindFixedSong(const Song* song) const;

  ProfileSlot GetLoadedFromProfileSlot() const { return loaded_from_profile_; }

  bool Matches(std::string group, std::string course) const;

  // Lua
  void PushSelf(lua_State* L);

  void CalculateRadarValues();

  bool GetTrailUnsorted(StepsType st, CourseDifficulty cd, Trail& trail) const;
  void GetTrailUnsortedEndless(
      const std::vector<CourseEntry>& entries, Trail& trail, StepsType& st,
      CourseDifficulty& cd, RandomGen& rnd,
      bool& course_difficulty_is_significant) const;
  bool GetTrailSorted(StepsType st, CourseDifficulty cd, Trail& trail) const;

  bool IsAnEdit() const { return loaded_from_profile_ != ProfileSlot_Invalid; }

  bool is_autogen_;  // was this created by AutoGen?
  std::string path_;

  std::string main_title_, main_title_translit_;
  std::string sub_title_, sub_title_translit_;
  std::string scripter_;
  std::string description_;

  std::string banner_path_;
  std::string background_path_;
  std::string group_name_;

  bool repeat_;         // repeat after last song?  "Endless"
  float goal_seconds_;  // if not 0, stop play after this number of seconds
  bool shuffle_;
  int lives_;                         // -1 means use bar life meter
  int custom_meter_[NUM_Difficulty];  // -1 = no meter specified
  bool sort_by_meter_;

  bool incomplete_;

  std::vector<CourseEntry> entries_;

  // sorting values
  int sort_order_total_difficulty_;
  int sort_order_ranking_;

  ProfileSlot loaded_from_profile_;  // ProfileSlot_Invalid if wasn't loaded
                                     // from a profile

  typedef std::pair<StepsType, Difficulty> CacheEntry;
  struct CacheData {
    Trail trail_;
    bool null_;

    CacheData() : trail_(), null_(false) {}
  };
  typedef std::map<CacheEntry, CacheData> TrailCache_t;
  mutable TrailCache_t trail_cache_;
  mutable int trail_cache_seed_;

  typedef std::map<CacheEntry, RadarValues> RadarCache_t;
  RadarCache_t radar_cache_;

  // Preferred styles:
  std::set<std::string> set_styles_;
};

#endif

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
