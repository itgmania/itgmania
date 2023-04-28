#ifndef COURSE_H
#define COURSE_H

#include <map>
#include <set>
#include <vector>

#include "Attack.h"
#include "EnumHelper.h"
#include "Game.h"
#include "GameConstantsAndTypes.h"
#include "RageTypes.h"
#include "SongUtil.h"
#include "StepsUtil.h"
#include "Style.h"
#include "Trail.h"

struct lua_State;

const int MAX_EDIT_COURSE_TITLE_LENGTH = 16;

inline PlayMode CourseTypeToPlayMode(CourseType course_type) {
  switch (course_type) {
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
};
// Loop through the various Song Sorts.
#define FOREACH_SongSort(i) FOREACH_ENUM(SongSort, i)
const RString& SongSortToString(SongSort song_sort);
const RString& SongSortToLocalizedString(SongSort song_sort);

class CourseEntry {
 public:
  // Show "??????" instead of an exact song
  bool is_secret_;

  // Filter criteria, applied from top to bottom
  SongID song_id_;  // don't filter if unset
  SongCriteria song_criteria_;
  StepsCriteria steps_criteria_;
  // If true, CourseDifficulty doesn't affect this entry
  bool no_difficult_;

  // Sort by this after filtering
  SongSort song_sort_;
  int choose_index_;

  // Set player and song options using these.
  RString modifiers_;
  // Timed modifiers_
  AttackArray attacks_;
  // Time gained back at the beginning of the song. LifeMeterTime only.
  float gain_seconds_;
  // Lives gained back at the beginning of the next song.
  int gain_lives_;

  CourseEntry()
      : is_secret_(false),
        song_id_(),
        song_criteria_(),
        steps_criteria_(),
        no_difficult_(false),
        song_sort_(SongSort_Randomize),
        choose_index_(0),
        modifiers_(RString("")),
        attacks_(),
        gain_seconds_(0),
        gain_lives_(-1) {}

  bool IsFixedSong() const { return song_id_.IsValid(); }

  RString GetTextDescription() const;
  int GetNumModChanges() const;

  // Lua
  void PushSelf(lua_State* L);
};

// A queue of songs and notes.
class Course {
 public:
  Course();

  RString GetBannerPath() const;
  RString GetBackgroundPath() const;
  bool HasBanner() const;
  bool HasBackground() const;

  // If PREFSMAN->m_bShowNative is off, these are the same as GetTranslit*
  // below. Otherwise, they return the main titles.
  RString GetDisplayMainTitle() const;
  RString GetDisplaySubTitle() const;

  // Returns the transliterated titles, if any. Otherwise returns the main
  // titles.
  RString GetTranslitMainTitle() const {
    return m_sMainTitleTranslit.size() ? m_sMainTitleTranslit : m_sMainTitle;
  }
  RString GetTranslitSubTitle() const {
    return m_sSubTitleTranslit.size() ? m_sSubTitleTranslit : m_sSubTitle;
  }

  // "title subtitle"
  RString GetDisplayFullTitle() const;
  RString GetTranslitFullTitle() const;

  // Dereferences course_entries and returns only the playable Songs and Steps
  Trail* GetTrail(
      StepsType steps_type,
      CourseDifficulty course_difficulty = Difficulty_Medium) const;
  Trail* GetTrailForceRegenCache(
      StepsType steps_type,
      CourseDifficulty course_difficulty = Difficulty_Medium) const;
  void GetTrails(std::vector<Trail*>& add_to, StepsType steps_type) const;
  void GetAllTrails(std::vector<Trail*>& add_to) const;
  int GetMeter(
      StepsType steps_type,
      CourseDifficulty course_difficulty = Difficulty_Medium) const;
  bool HasMods() const;
  bool HasTimedMods() const;
  bool AllSongsAreFixed() const;
  const Style* GetCourseStyle(const Game* game, int num_players) const;

  int GetEstimatedNumStages() const { return m_vEntries.size(); }
  bool IsPlayableIn(StepsType steps_type) const;
  bool CourseHasBestOrWorst() const;
  RageColor GetColor() const;
  bool GetTotalSeconds(StepsType steps_type, float& seconds_out) const;

  bool IsNonstop() const { return GetPlayMode() == PLAY_MODE_NONSTOP; }
  bool IsOni() const { return GetPlayMode() == PLAY_MODE_ONI; }
  bool IsEndless() const { return GetPlayMode() == PLAY_MODE_ENDLESS; }
  CourseType GetCourseType() const;
  void SetCourseType(CourseType course_type);
  PlayMode GetPlayMode() const;

  bool ShowInDemonstrationAndRanking() const;

  void RevertFromDisk();
  void Init();

  bool IsRanking() const;

  void UpdateCourseStats(StepsType steps_type);

  // Call to regenerate Trails with random entries
  void RegenerateNonFixedTrails() const;

  void InvalidateTrailCache();

  // Call when a Song or its Steps are deleted/changed.
  void Invalidate(const Song* stale_song);

  void GetAllCachedTrails(std::vector<Trail*>& out);
  RString GetCacheFilePath() const;

  const CourseEntry* FindFixedSong(const Song* song) const;

  ProfileSlot GetLoadedFromProfileSlot() const { return m_LoadedFromProfile; }
  void SetLoadedFromProfile(ProfileSlot slot) { m_LoadedFromProfile = slot; }

  bool Matches(RString group, RString course) const;

  // Lua
  void PushSelf(lua_State* L);

  void CalculateRadarValues();

  bool GetTrailUnsorted(
      StepsType steps_type, CourseDifficulty course_difficulty,
      Trail& trail) const;
  void GetTrailUnsortedEndless(
      const std::vector<CourseEntry>& entries, Trail& trail,
      StepsType& steps_type, CourseDifficulty& course_difficulty,
      RandomGen& rnd, bool& course_difficulty_is_siginificant) const;
  bool GetTrailSorted(
      StepsType steps_type, CourseDifficulty course_difficulty,
      Trail& trail) const;

  bool IsAnEdit() const { return m_LoadedFromProfile != ProfileSlot_Invalid; }

  bool m_bIsAutogen;  // was this created by AutoGen?
  RString m_sPath;

  RString m_sMainTitle, m_sMainTitleTranslit;
  RString m_sSubTitle, m_sSubTitleTranslit;
  RString m_sScripter;
  RString m_sDescription;

  RString m_sBannerPath;
  RString m_sBackgroundPath;
  RString m_sCDTitlePath;
  RString m_sGroupName;

  bool m_bRepeat;        // repeat after last song?  "Endless"
  float m_fGoalSeconds;  // if not 0, stop play after this number of seconds
  bool m_bShuffle;
  int m_iLives;                        // -1 means use bar life meter
  int m_iCustomMeter[NUM_Difficulty];  // -1 = no meter specified
  bool m_bSortByMeter;

  bool m_bIncomplete;

  std::vector<CourseEntry> m_vEntries;

  // sorting values
  int m_SortOrder_TotalDifficulty;
  int m_SortOrder_Ranking;

  // ProfileSlot_Invalid if wasn't loaded from a profile.
  ProfileSlot m_LoadedFromProfile;

  typedef std::pair<StepsType, Difficulty> CacheEntry;
  struct CacheData {
    Trail trail;
    bool null;

    CacheData() : trail(), null(false) {}
  };
  typedef std::map<CacheEntry, CacheData> TrailCache_t;
  mutable TrailCache_t m_TrailCache;
  mutable int m_iTrailCacheSeed;

  typedef std::map<CacheEntry, RadarValues> RadarCache_t;
  RadarCache_t m_RadarCache;

  // Preferred styles:
  std::set<RString> m_setStyles;
};

#endif  // COURSE_H

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
