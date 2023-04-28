#include "global.h"

#include "Course.h"

#include <climits>
#include <cstddef>
#include <vector>

#include "CourseLoaderCRS.h"
#include "Game.h"
#include "GameManager.h"
#include "GameState.h"
#include "LocalizedString.h"
#include "LuaManager.h"
#include "Preference.h"
#include "PrefsManager.h"
#include "ProfileManager.h"
#include "RageLog.h"
#include "Song.h"
#include "SongCacheIndex.h"
#include "Steps.h"
#include "Style.h"
#include "ThemeManager.h"
#include "UnlockManager.h"

static Preference<int> MAX_SONGS_IN_EDIT_COURSE("MaxSongsInEditCourse", -1);

static const char* SongSortNames[] = {
    "Randomize", "MostPlays", "FewestPlays", "TopGrades", "LowestGrades",
};
XToString(SongSort);
XToLocalizedString(SongSort);

// Maximum lower value of ranges when difficult:
const int MAX_BOTTOM_RANGE = 10;

#define SORT_PREFERRED_COLOR THEME->GetMetricC("Course", "SortPreferredColor")
#define SORT_LEVEL1_COLOR THEME->GetMetricC("Course", "SortLevel1Color")
#define SORT_LEVEL2_COLOR THEME->GetMetricC("Course", "SortLevel2Color")
#define SORT_LEVEL3_COLOR THEME->GetMetricC("Course", "SortLevel3Color")
#define SORT_LEVEL4_COLOR THEME->GetMetricC("Course", "SortLevel4Color")
#define SORT_LEVEL5_COLOR THEME->GetMetricC("Course", "SortLevel5Color")

RString CourseEntry::GetTextDescription() const {
  std::vector<RString> entry_description;
  Song* song = song_id_.ToSong();
  if (song) {
    entry_description.push_back(song->GetTranslitFullTitle());
  } else {
    entry_description.push_back("Random");
  }

  if (!song_criteria_.m_sGroupName.empty()) {
    entry_description.push_back(song_criteria_.m_sGroupName);
  }

  if (song_criteria_.m_bUseSongGenreAllowedList) {
    entry_description.push_back(
        join(",", song_criteria_.m_vsSongGenreAllowedList));
  }

  if (steps_criteria_.m_difficulty != Difficulty_Invalid &&
      steps_criteria_.m_difficulty != Difficulty_Medium) {
    entry_description.push_back(
        CourseDifficultyToLocalizedString(steps_criteria_.m_difficulty));
  }

  if (steps_criteria_.m_iLowMeter != -1) {
    entry_description.push_back(
        ssprintf("Low meter: %d", steps_criteria_.m_iLowMeter));
  }

  if (steps_criteria_.m_iHighMeter != -1) {
    entry_description.push_back(
        ssprintf("High meter: %d", steps_criteria_.m_iHighMeter));
  }

  if (song_sort_ != SongSort_Randomize) {
    entry_description.push_back(
        "Sort: %d" + SongSortToLocalizedString(song_sort_));
  }

  if (song_sort_ != SongSort_Randomize && choose_index_ != 0) {
    entry_description.push_back(
        "Choose " + FormatNumberAndSuffix(choose_index_) + " match");
  }

  int num_mod_changes = GetNumModChanges();
  if (num_mod_changes != 0) {
    entry_description.push_back(ssprintf("%d mod changes", num_mod_changes));
  }

  if (gain_seconds_ != 0) {
    entry_description.push_back(ssprintf("Low meter: %.0f", gain_seconds_));
  }

  RString s = join(",", entry_description);
  return s;
}

int CourseEntry::GetNumModChanges() const {
  int num_mod_changes = 0;
  if (!modifiers_.empty()) {
    num_mod_changes++;
  }
  num_mod_changes += attacks_.size();
  return num_mod_changes;
}

Course::Course()
    : m_bIsAutogen(false),
      m_sPath(""),
      m_sMainTitle(""),
      m_sMainTitleTranslit(""),
      m_sSubTitle(""),
      m_sSubTitleTranslit(""),
      m_sScripter(""),
      m_sDescription(""),
      m_sBannerPath(""),
      m_sBackgroundPath(""),
      m_sCDTitlePath(""),
      m_sGroupName(""),
      m_bRepeat(false),
      m_fGoalSeconds(0),
      m_bShuffle(false),
      m_iLives(-1),
      m_bSortByMeter(false),
      m_bIncomplete(false),
      m_vEntries(),
      m_SortOrder_TotalDifficulty(0),
      m_SortOrder_Ranking(0),
      m_LoadedFromProfile(ProfileSlot_Invalid),
      m_TrailCache(),
      m_iTrailCacheSeed(0),
      m_RadarCache(),
      m_setStyles() {
  FOREACH_ENUM(Difficulty, dc)
  m_iCustomMeter[dc] = -1;
}

CourseType Course::GetCourseType() const {
  if (m_bRepeat) {
    return COURSE_TYPE_ENDLESS;
  }

  if (m_iLives > 0) {
    return COURSE_TYPE_ONI;
  }

  if (!m_vEntries.empty() && m_vEntries[0].gain_seconds_ > 0) {
    return COURSE_TYPE_SURVIVAL;
  }
  return COURSE_TYPE_NONSTOP;
}

void Course::SetCourseType(CourseType course_type) {
  if (GetCourseType() == course_type) {
    return;
  }

  m_bRepeat = false;
  m_iLives = -1;
  if (!m_vEntries.empty()) {
    m_vEntries[0].gain_seconds_ = 0;
  }

  switch (course_type) {
    default:
      FAIL_M(ssprintf("Invalid course type: %i", course_type));
    case COURSE_TYPE_NONSTOP:
      break;
    case COURSE_TYPE_ONI:
      m_iLives = 4;
      break;
    case COURSE_TYPE_ENDLESS:
      m_bRepeat = true;
      break;
    case COURSE_TYPE_SURVIVAL:
      if (!m_vEntries.empty()) {
        m_vEntries[0].gain_seconds_ = 120;
      }
      break;
  }
}

PlayMode Course::GetPlayMode() const {
  CourseType course_type = GetCourseType();
  switch (course_type) {
    case COURSE_TYPE_ENDLESS:
      return PLAY_MODE_ENDLESS;
    case COURSE_TYPE_ONI:
      return PLAY_MODE_ONI;
    case COURSE_TYPE_SURVIVAL:
      return PLAY_MODE_ONI;
    case COURSE_TYPE_NONSTOP:
      return PLAY_MODE_NONSTOP;
    default:
      FAIL_M(ssprintf("Invalid course type: %i", course_type));
  }
}

void Course::RevertFromDisk() {
  // trying to catch invalid an Course
  ASSERT(!m_sPath.empty());

  CourseLoaderCRS::LoadFromCRSFile(m_sPath, *this);
}

RString Course::GetCacheFilePath() const {
  return SongCacheIndex::GetCacheFilePath("Courses", m_sPath);
}

void Course::Init() {
  m_bIsAutogen = false;
  m_sPath = "";

  m_sMainTitle = "";
  m_sMainTitleTranslit = "";
  m_sSubTitle = "";
  m_sSubTitleTranslit = "";
  m_sScripter = "";
  m_sDescription = "";

  m_sBannerPath = "";
  m_sBackgroundPath = "";
  m_sCDTitlePath = "";
  m_sGroupName = "";

  m_bRepeat = false;
  m_fGoalSeconds = 0;
  m_bShuffle = false;

  m_iLives = -1;
  FOREACH_ENUM(Difficulty, dc)
  m_iCustomMeter[dc] = -1;
  m_bSortByMeter = false;

  m_vEntries.clear();

  m_SortOrder_TotalDifficulty = 0;
  m_SortOrder_Ranking = 0;

  m_LoadedFromProfile = ProfileSlot_Invalid;
  m_bIncomplete = false;
  m_TrailCache.clear();
  m_iTrailCacheSeed = 0;
  m_RadarCache.clear();
}

bool Course::IsPlayableIn(StepsType steps_type) const {
  Trail trail;
  FOREACH_ShownCourseDifficulty(course_difficulty) {
    if (GetTrailUnsorted(steps_type, course_difficulty, trail)) {
      return true;
    }
  }

  // No valid trail for this StepsType.
  return false;
}

struct SortTrailEntry {
  TrailEntry entry;
  int sort_meter;

  SortTrailEntry() : entry(), sort_meter(0) {}

  bool operator<(const SortTrailEntry& rhs) const {
    return sort_meter < rhs.sort_meter;
  }
};

RString Course::GetDisplayMainTitle() const {
  if (!PREFSMAN->m_bShowNativeLanguage) {
    return GetTranslitMainTitle();
  }
  return m_sMainTitle;
}

RString Course::GetDisplaySubTitle() const {
  if (!PREFSMAN->m_bShowNativeLanguage) {
    return GetTranslitSubTitle();
  }
  return m_sSubTitle;
}

RString Course::GetDisplayFullTitle() const {
  RString sTitle = GetDisplayMainTitle();
  RString sSubTitle = GetDisplaySubTitle();

  if (!sSubTitle.empty()) {
    sTitle += " " + sSubTitle;
  }
  return sTitle;
}

RString Course::GetTranslitFullTitle() const {
  RString sTitle = GetTranslitMainTitle();
  RString sSubTitle = GetTranslitSubTitle();

  if (!sSubTitle.empty()) {
    sTitle += " " + sSubTitle;
  }
  return sTitle;
}

// This is called by many simple functions, like Course::GetTotalSeconds, and
// may be called on all songs to sort. It can take time to execute, so we cache
// the results. Returned pointers remain valid for the lifetime of the Course.
// If the course difficulty doesn't exist, nullptr is returned.
Trail* Course::GetTrail(
    StepsType steps_type, CourseDifficulty course_difficulty) const {
  ASSERT(course_difficulty != Difficulty_Invalid);

  // Check to see if the Trail cache is out of date
  if (m_iTrailCacheSeed != GAMESTATE->m_iStageSeed) {
    RegenerateNonFixedTrails();
    m_iTrailCacheSeed = GAMESTATE->m_iStageSeed;
  }

  // Look in the Trail cache
  {
    auto it = m_TrailCache.find(CacheEntry(steps_type, course_difficulty));
    if (it != m_TrailCache.end()) {
      CacheData& cache = it->second;
      if (cache.null) {
        return nullptr;
      }
      return &cache.trail;
    }
  }

  return GetTrailForceRegenCache(steps_type, course_difficulty);
}

Trail* Course::GetTrailForceRegenCache(
    StepsType steps_type, CourseDifficulty course_difficulty) const {
  // Construct a new Trail, add it to the cache, then return it.
  CacheData& cache = m_TrailCache[CacheEntry(steps_type, course_difficulty)];
  Trail& trail = cache.trail;
  trail.Init();
  if (!GetTrailSorted(steps_type, course_difficulty, trail)) {
    // This course difficulty doesn't exist.
    cache.null = true;
    return nullptr;
  }

  // If we have cached RadarValues for this trail, insert them.
  {
    auto it = m_RadarCache.find(CacheEntry(steps_type, course_difficulty));
    if (it != m_RadarCache.end()) {
      const RadarValues& radar_values = it->second;
      trail.SetRadarValues(radar_values);
    }
  }

  cache.null = false;
  return &cache.trail;
}

bool Course::GetTrailSorted(
    StepsType steps_type, CourseDifficulty course_difficulty,
    Trail& trail) const {
  if (!GetTrailUnsorted(steps_type, course_difficulty, trail)) {
    return false;
  }

  if (m_bSortByMeter) {
    // Sort according to Difficulty_Medium, since the order of songs
    // must not change across difficulties.
    Trail sort_trail;
    if (course_difficulty == Difficulty_Medium) {
      sort_trail = trail;
    } else {
      bool ok = GetTrailUnsorted(steps_type, Difficulty_Medium, sort_trail);

      // If we have any other difficulty, we must have Difficulty_Medium.
      ASSERT(ok);
    }
    ASSERT_M(
        trail.m_vEntries.size() == sort_trail.m_vEntries.size(),
        ssprintf(
            "%i %i", int(trail.m_vEntries.size()),
            int(sort_trail.m_vEntries.size())));

    std::vector<SortTrailEntry> entries;
    for (unsigned i = 0; i < trail.m_vEntries.size(); ++i) {
      SortTrailEntry sort_trail_entry;
      sort_trail_entry.entry = trail.m_vEntries[i];
      sort_trail_entry.sort_meter = sort_trail.m_vEntries[i].pSteps->GetMeter();
      entries.push_back(sort_trail_entry);
    }

    stable_sort(entries.begin(), entries.end());
    for (unsigned i = 0; i < trail.m_vEntries.size(); ++i) {
      trail.m_vEntries[i] = entries[i].entry;
    }
  }

  if (trail.m_vEntries.empty()) {
    return false;
  }
  return true;
}

// TODO: Move Course initialization after PROFILEMAN is created
static void CourseSortSongs(
    SongSort sort, std::vector<Song*>& vpPossibleSongs, RandomGen& rnd) {
  switch (sort) {
    DEFAULT_FAIL(sort);
    case SongSort_Randomize:
      std::shuffle(vpPossibleSongs.begin(), vpPossibleSongs.end(), rnd);
      break;
    case SongSort_MostPlays:
      if (PROFILEMAN) {
        SongUtil::SortSongPointerArrayByNumPlays(
            vpPossibleSongs, PROFILEMAN->GetMachineProfile(),
            true);  // descending
      }
      break;
    case SongSort_FewestPlays:
      if (PROFILEMAN) {
        SongUtil::SortSongPointerArrayByNumPlays(
            vpPossibleSongs, PROFILEMAN->GetMachineProfile(),
            false);  // ascending
      }
      break;
    case SongSort_TopGrades:
      if (PROFILEMAN) {
        SongUtil::SortSongPointerArrayByGrades(
            vpPossibleSongs, true);  // descending
      }
      break;
    case SongSort_LowestGrades:
      if (PROFILEMAN) {
        SongUtil::SortSongPointerArrayByGrades(
            vpPossibleSongs, false);  // ascending
      }
      break;
  }
}

namespace {

class SongIsEqual {
 public:
  const Song* song_;
  SongIsEqual(const Song* song) : song_(song) {}
  bool operator()(const SongAndSteps& song_and_steps) const {
    return song_and_steps.pSong == song_;
  }
};

}  // namespace

bool Course::GetTrailUnsorted(
    StepsType steps_type, CourseDifficulty course_difficulty,
    Trail& trail) const {
  trail.Init();

  // Construct a new Trail, add it to the cache, then return it.
  // Different seed for each course, but the same for the whole round:
  RandomGen rnd(GAMESTATE->m_iStageSeed + GetHashForString(m_sMainTitle));

  std::vector<CourseEntry> tmp_entries;
  if (m_bShuffle) {
    // Always randomize the same way per round. Otherwise, the displayed course
    // will change every time it's viewed, and the displayed order will have no
    // bearing on what you'll actually play.
    tmp_entries = m_vEntries;
    std::shuffle(tmp_entries.begin(), tmp_entries.end(), rnd);
  }

  const std::vector<CourseEntry>& entries =
      m_bShuffle ? tmp_entries : m_vEntries;

  // This can take some time, so don't fill it out unless we need it.
  std::vector<Song*> songs_by_most_played;
  std::vector<Song*> all_songs_shuffled;

  trail.m_StepsType = steps_type;
  trail.m_CourseType = GetCourseType();
  trail.m_CourseDifficulty = course_difficulty;
  trail.m_vEntries.reserve(entries.size());

  // Set to true if CourseDifficulty is able to change something.
  bool course_difficulty_is_significant =
      (course_difficulty == Difficulty_Medium);

  // Resolve each entry to a Song and Steps.
  if (trail.m_CourseType == COURSE_TYPE_ENDLESS) {
    GetTrailUnsortedEndless(
        entries, trail, steps_type, course_difficulty, rnd,
        course_difficulty_is_significant);
  } else {
    std::vector<SongAndSteps> song_and_steps;
    for (auto e = entries.begin(); e != entries.end(); ++e) {
      SongAndSteps resolved;
      SongCriteria song_criteria = e->song_criteria_;

      Song* song = e->song_id_.ToSong();
      if (song) {
        song_criteria.m_bUseSongAllowedList = true;
        song_criteria.m_vpSongAllowedList.push_back(song);
      }
      song_criteria.m_Tutorial = SongCriteria::Tutorial_No;
      song_criteria.m_Locked = SongCriteria::Locked_Unlocked;
      if (!song_criteria.m_bUseSongAllowedList) {
        song_criteria.m_iMaxStagesForSong = 1;
      }

      StepsCriteria steps_criteria = e->steps_criteria_;
      steps_criteria.m_st = steps_type;
      steps_criteria.m_Locked = StepsCriteria::Locked_Unlocked;

      const bool same_song_criteria =
          e != entries.begin() && (e - 1)->song_criteria_ == song_criteria;
      const bool same_steps_criteria =
          e != entries.begin() && (e - 1)->steps_criteria_ == steps_criteria;

      if (song) {
        song_and_steps.clear();
        StepsUtil::GetAllMatching(song, steps_criteria, song_and_steps);
      } else if (
          song_and_steps.empty() ||
          !(same_song_criteria && same_steps_criteria)) {
        song_and_steps.clear();
        StepsUtil::GetAllMatching(
            song_criteria, steps_criteria, song_and_steps);
      }

      // It looks bad to have the same song 2x in a row in a randomly generated
      // course. Don't allow the same song to be played 2x in a row, unless
      // there's only one song in vpPossibleSongs.
      if (trail.m_vEntries.size() > 0 && song_and_steps.size() > 1) {
        const TrailEntry& teLast = trail.m_vEntries.back();
        RemoveIf(song_and_steps, SongIsEqual(teLast.pSong));
      }

      // If there are no songs to choose from, abort this CourseEntry.
      if (song_and_steps.empty()) {
        continue;
      }

      std::vector<Song*> songs;
      typedef std::vector<Steps*> StepsVector;
      std::map<Song*, StepsVector> song_to_steps_map;
      for (auto it = song_and_steps.begin(); it != song_and_steps.end(); ++it) {
        StepsVector& steps = song_to_steps_map[it->pSong];

        steps.push_back(it->pSteps);
        if (steps.size() == 1) {
          songs.push_back(it->pSong);
        }
      }

      CourseSortSongs(e->song_sort_, songs, rnd);

      ASSERT(e->choose_index_ >= 0);
      if (e->choose_index_ < int(song_and_steps.size())) {
        resolved.pSong = songs[e->choose_index_];
        const std::vector<Steps*>& mapped_songs =
            song_to_steps_map[resolved.pSong];
        resolved.pSteps = mapped_songs[RandomInt(mapped_songs.size())];
      } else {
        continue;
      }

      // If we're not COURSE_DIFFICULTY_REGULAR, then we should be choosing
      // steps that are either easier or harder than the base difficulty. If no
      // such steps exist, then just use the one we already have.
      Difficulty diff = resolved.pSteps->GetDifficulty();
      int low_meter = e->steps_criteria_.m_iLowMeter;
      int high_meter = e->steps_criteria_.m_iHighMeter;
      if (course_difficulty != Difficulty_Medium && !e->no_difficult_) {
        Difficulty new_diff =
            (Difficulty)(diff + course_difficulty - Difficulty_Medium);
        new_diff =
            clamp(new_diff, (Difficulty)0, (Difficulty)(Difficulty_Edit - 1));

        bool changed_difficulty = false;
        if (new_diff != diff) {
          Steps* new_steps = SongUtil::GetStepsByDifficulty(
              resolved.pSong, steps_type, new_diff);
          if (new_steps) {
            diff = new_diff;
            resolved.pSteps = new_steps;
            changed_difficulty = true;
            course_difficulty_is_significant = true;
          }
        }

        // Hack: We used to adjust low_meter/high_meter above while searching
        // for songs.  However, that results in a different song being chosen
        // for difficult courses, which is bad when LockCourseDifficulties is
        // disabled; each player can end up with a different song.  Instead,
        // choose based on the original range, bump the steps based on course
        // difficulty, and then retroactively tweak the low_meter/high_meter so
        // course displays line up.
        if (e->steps_criteria_.m_difficulty == Difficulty_Invalid &&
            changed_difficulty) {
          // Minimum and maximum to add to make the meter range contain the
          // actual meter:
          int min_dist = resolved.pSteps->GetMeter() - high_meter;
          int max_dist = resolved.pSteps->GetMeter() - low_meter;

          // Clamp the possible adjustments to try to avoid going under 1 or
          // over MAX_BOTTOM_RANGE.
          min_dist = std::min(std::max(min_dist, -low_meter + 1), max_dist);
          max_dist = std::max(
              std::min(max_dist, MAX_BOTTOM_RANGE - high_meter), min_dist);

          int iAdd;
          if (max_dist == min_dist) {
            iAdd = max_dist;
          } else {
            std::uniform_int_distribution<> dist(min_dist, max_dist);
            iAdd = dist(rnd);
          }
          low_meter += iAdd;
          high_meter += iAdd;
        }
      }

      TrailEntry trail_entry;
      trail_entry.pSong = resolved.pSong;
      trail_entry.pSteps = resolved.pSteps;
      trail_entry.Modifiers = e->modifiers_;
      trail_entry.Attacks = e->attacks_;
      trail_entry.bSecret = e->is_secret_;
      trail_entry.iLowMeter = low_meter;
      trail_entry.iHighMeter = high_meter;

      // If we chose based on meter (not difficulty), then store
      // Difficulty_Invalid, so other classes can tell that we used meter.
      if (e->steps_criteria_.m_difficulty == Difficulty_Invalid) {
        trail_entry.dc = Difficulty_Invalid;
      } else {
        // Otherwise, store the actual difficulty we got
        // (post-course-difficulty). This may or may not be the same as
        // e.difficulty.
        trail_entry.dc = diff;
      }
      trail.m_vEntries.push_back(trail_entry);

      if (IsAnEdit() && MAX_SONGS_IN_EDIT_COURSE > 0 &&
          int(trail.m_vEntries.size()) >= MAX_SONGS_IN_EDIT_COURSE) {
        break;
      }
    }
  }

  // Hack: If any entry was non-FIXED, or m_bShuffle is set, then radar values
  // for this trail will be meaningless as they'll change every time. Pre-cache
  // empty data. XXX: How can we do this cleanly, without propagating lots of
  // otherwise unnecessary data (course entry types, m_bShuffle) to Trail, or
  // storing a Course pointer in Trail (yuck)?
  if (!AllSongsAreFixed() || m_bShuffle) {
    trail.m_bRadarValuesCached = true;
    trail.m_CachedRadarValues = RadarValues();
  }

  // If we have a manually-entered meter for this difficulty, use it.
  if (m_iCustomMeter[course_difficulty] != -1) {
    trail.m_iSpecifiedMeter = m_iCustomMeter[course_difficulty];
  }

  // If the course difficulty never actually changed anything, then this
  // difficulty is equivalent to Difficulty_Medium; it doesn't exist.
  return course_difficulty_is_significant && trail.m_vEntries.size() > 0;
}

void Course::GetTrailUnsortedEndless(
    const std::vector<CourseEntry>& entries, Trail& trail,
    StepsType& steps_type, CourseDifficulty& course_difficulty, RandomGen& rnd,
    bool& course_difficulty_is_significant) const {
  typedef std::vector<Steps*> StepsVector;

  std::set<Song*> already_selected;
  Song* last_song_selected = nullptr;
  std::vector<SongAndSteps> song_and_steps;
  for (auto e = entries.begin(); e != entries.end(); ++e) {
    SongAndSteps resolved;
    SongCriteria song_criteria = e->song_criteria_;

    Song* song = e->song_id_.ToSong();
    if (song) {
      song_criteria.m_bUseSongAllowedList = true;
      song_criteria.m_vpSongAllowedList.push_back(song);
    }
    song_criteria.m_Tutorial = SongCriteria::Tutorial_No;
    song_criteria.m_Locked = SongCriteria::Locked_Unlocked;
    if (!song_criteria.m_bUseSongAllowedList) {
      song_criteria.m_iMaxStagesForSong = 1;
    }

    StepsCriteria steps_criteria = e->steps_criteria_;
    steps_criteria.m_st = steps_type;
    steps_criteria.m_Locked = StepsCriteria::Locked_Unlocked;

    const bool same_song_criteria =
        e != entries.begin() && (e - 1)->song_criteria_ == song_criteria;
    const bool same_steps_criteria =
        e != entries.begin() && (e - 1)->steps_criteria_ == steps_criteria;

    // If we're doing the same wildcard search as last entry,
    // we can just reuse the song_and_steps vector.
    if (song) {
      song_and_steps.clear();
      StepsUtil::GetAllMatchingEndless(song, steps_criteria, song_and_steps);
    } else if (
        song_and_steps.empty() ||
        !(same_song_criteria && same_steps_criteria)) {
      song_and_steps.clear();
      StepsUtil::GetAllMatching(song_criteria, steps_criteria, song_and_steps);
    }

    // if there are no songs to choose from, abort this CourseEntry
    if (song_and_steps.empty()) {
      continue;
    }

    // if we're doing a RANDOM wildcard search, try to avoid repetition
    if (song_and_steps.size() > 1 &&
        e->song_sort_ == SongSort::SongSort_Randomize) {
      // Make a backup of the steplist so we can revert if we overfilter
      std::vector<SongAndSteps> revert_list = song_and_steps;
      // Filter candidate list via blacklist
      RemoveIf(song_and_steps, [&](const SongAndSteps& song_and_steps) {
        return std::find(
                   already_selected.begin(), already_selected.end(),
                   song_and_steps.pSong) != already_selected.end();
      });
      // If every candidate is in the blacklist, pick random song that wasn't
      // played last (Repeat songs may still occur if song after this is fixed;
      // this algorithm doesn't look ahead)
      if (song_and_steps.empty()) {
        song_and_steps = revert_list;
        RemoveIf(song_and_steps, SongIsEqual(last_song_selected));

        // If the song that was played last was the only candidate, give up pick
        // randomly
        if (song_and_steps.empty()) {
          song_and_steps = revert_list;
        }
      }
    }

    std::vector<Song*> songs;
    std::map<Song*, StepsVector> song_steps_map;
    // Build list of songs for sorting, and mapping of songs to steps
    // simultaneously
    for (auto& ss : song_and_steps) {
      StepsVector& steps_for_song = song_steps_map[ss.pSong];
      // If we haven't noted this song yet, add it to the song list
      if (steps_for_song.size() == 0) {
        songs.push_back(ss.pSong);
      }

      steps_for_song.push_back(ss.pSteps);
    }

    ASSERT(e->choose_index_ >= 0);
    // If we're trying to pick BEST100 when only 99 songs exist,
    // we have a problem, so bail out
    if (static_cast<std::size_t>(e->choose_index_) >= songs.size()) {
      continue;
    }

    // Otherwise, pick random steps corresponding to the selected song
    CourseSortSongs(e->song_sort_, songs, rnd);
    resolved.pSong = songs[e->choose_index_];
    const std::vector<Steps*>& songSteps = song_steps_map[resolved.pSong];
    resolved.pSteps = songSteps[RandomInt(songSteps.size())];

    last_song_selected = resolved.pSong;
    already_selected.emplace(resolved.pSong);

    // If we're not COURSE_DIFFICULTY_REGULAR, then we should be choosing steps
    // that are either easier or harder than the base difficulty. If no such
    // steps exist, then just use the one we already have.
    Difficulty diff = resolved.pSteps->GetDifficulty();
    int low_meter = e->steps_criteria_.m_iLowMeter;
    int high_meter = e->steps_criteria_.m_iHighMeter;
    if (course_difficulty != Difficulty_Medium && !e->no_difficult_) {
      Difficulty new_diff =
          (Difficulty)(diff + course_difficulty - Difficulty_Medium);
      if (diff != Difficulty_Medium) {
        new_diff = course_difficulty;
      }
      new_diff =
          clamp(new_diff, (Difficulty)0, (Difficulty)(Difficulty_Edit - 1));

      bool changed_difficulty = false;
      if (new_diff != diff) {
        Steps* new_steps = SongUtil::GetStepsByDifficulty(
            resolved.pSong, steps_type, new_diff);
        if (new_steps) {
          diff = new_diff;
          resolved.pSteps = new_steps;
          changed_difficulty = true;
          course_difficulty_is_significant = true;
        }
      }

      // Hack: We used to adjust low_meter/high_meter above while searching for
      // songs.  However, that results in a different song being chosen for
      // difficult courses, which is bad when LockCourseDifficulties is
      // disabled; each player can end up with a different song. Instead,
      // choose based on the original range, bump the steps based on course
      // difficulty, and then retroactively tweak the low_meter/high_meter so
      // course displays line up.
      if (e->steps_criteria_.m_difficulty == Difficulty_Invalid &&
          changed_difficulty) {
        // Minimum and maximum to add to make the meter range contain the actual
        // meter:
        int min_dist = resolved.pSteps->GetMeter() - high_meter;
        int max_dist = resolved.pSteps->GetMeter() - low_meter;

        /* Clamp the possible adjustments to try to avoid going under 1 or over
         * MAX_BOTTOM_RANGE. */
        min_dist = std::min(std::max(min_dist, -low_meter + 1), max_dist);
        max_dist = std::max(
            std::min(max_dist, MAX_BOTTOM_RANGE - high_meter), min_dist);

        int iAdd;
        if (max_dist == min_dist) {
          iAdd = max_dist;
        } else {
          std::uniform_int_distribution<> dist(min_dist, max_dist);
          iAdd = dist(rnd);
        }
        low_meter += iAdd;
        high_meter += iAdd;
      }
    }

    TrailEntry trail_entry;
    trail_entry.pSong = resolved.pSong;
    trail_entry.pSteps = resolved.pSteps;
    trail_entry.Modifiers = e->modifiers_;
    trail_entry.Attacks = e->attacks_;
    trail_entry.bSecret = e->is_secret_;
    trail_entry.iLowMeter = low_meter;
    trail_entry.iHighMeter = high_meter;

    // If we chose based on meter (not difficulty), then store
    // Difficulty_Invalid, so other classes can tell that we used meter.
    if (e->steps_criteria_.m_difficulty == Difficulty_Invalid) {
      trail_entry.dc = Difficulty_Invalid;
    } else {
      // Otherwise, store the actual difficulty we got (post-course-difficulty).
      // This may or may not be the same as e.difficulty.
      trail_entry.dc = diff;
    }

    trail.m_vEntries.push_back(trail_entry);

    if (IsAnEdit() && MAX_SONGS_IN_EDIT_COURSE > 0 &&
        int(trail.m_vEntries.size()) >= MAX_SONGS_IN_EDIT_COURSE) {
      break;
    }
  }
}

void Course::GetTrails(
    std::vector<Trail*>& add_to, StepsType steps_type) const {
  FOREACH_ShownCourseDifficulty(course_difficulty) {
    Trail* trail = GetTrail(steps_type, course_difficulty);
    if (trail == nullptr) {
      continue;
    }
    add_to.push_back(trail);
  }
}

void Course::GetAllTrails(std::vector<Trail*>& add_to) const {
  std::vector<StepsType> steps_type_to_show;
  GAMEMAN->GetStepsTypesForGame(GAMESTATE->m_pCurGame, steps_type_to_show);
  for (const StepsType& steps_type : steps_type_to_show) {
    GetTrails(add_to, steps_type);
  }
}

int Course::GetMeter(
    StepsType steps_type, CourseDifficulty course_difficulty) const {
  if (m_iCustomMeter[course_difficulty] != -1) {
    return m_iCustomMeter[course_difficulty];
  }
  const Trail* trail = GetTrail(steps_type);
  if (trail != nullptr) {
    return trail->GetMeter();
  }
  return 0;
}

bool Course::HasMods() const {
  return std::any_of(
      m_vEntries.begin(), m_vEntries.end(),
      [](const CourseEntry& e) { return !e.attacks_.empty(); });
}

bool Course::HasTimedMods() const {
  // NOTE(aj): What makes this different from the SM4 implementation is that
  // HasTimedMods now searches for bGlobal in the attacks; if one of
  // them is false, it has timed mods. Also returning false will probably
  // take longer than expected.
  for (const CourseEntry& course_entry : m_vEntries) {
    if (course_entry.attacks_.empty()) {
      continue;
    }
    if (std::any_of(
            course_entry.attacks_.begin(), course_entry.attacks_.end(),
            [](const Attack& a) { return !a.bGlobal; })) {
      return true;
    }
  }
  return false;
}

bool Course::AllSongsAreFixed() const {
  return std::all_of(
      m_vEntries.begin(), m_vEntries.end(),
      [](const CourseEntry& course_entry) {
        return course_entry.IsFixedSong();
      });
}

const Style* Course::GetCourseStyle(const Game* game, int num_players) const {
  std::vector<const Style*> styles;
  GAMEMAN->GetCompatibleStyles(game, num_players, styles);

  for (const Style* style : styles) {
    for (const RString& style_str : m_setStyles) {
      if (!style_str.CompareNoCase(style->m_szName)) {
        return style;
      }
    }
  }
  return nullptr;
}

void Course::InvalidateTrailCache() { m_TrailCache.clear(); }

void Course::Invalidate(const Song* stale_song) {
  for (const CourseEntry& course_entry : m_vEntries) {
    Song* song = course_entry.song_id_.ToSong();
    // A fixed entry that references the stale Song.
    if (song == stale_song) {
      RevertFromDisk();
      return;
    }
  }

  // Invalidate any Trails that contain this song.
  // If we find a Trail that contains this song, then it's part of a
  // non-fixed entry. So, regenerating the Trail will force different
  // songs to be chosen.
  FOREACH_ENUM(StepsType, steps_type) {
    FOREACH_ShownCourseDifficulty(course_difficulty) {
      auto it = m_TrailCache.find(CacheEntry(steps_type, course_difficulty));
      if (it == m_TrailCache.end()) {
        continue;
      }
      CacheData& cache = it->second;
      if (!cache.null) {
        if (GetTrail(steps_type, course_difficulty)->ContainsSong(stale_song)) {
          m_TrailCache.erase(it);
        }
      }
    }
  }
}

void Course::RegenerateNonFixedTrails() const {
  // Only need to regen Trails if the Course has a random entry.
  // We can create these Trails on demand because we don't
  // calculate RadarValues for Trails with one or more non-fixed
  // entry.
  if (AllSongsAreFixed()) {
    return;
  }

  for (const auto& e : m_TrailCache) {
    const CacheEntry& cache_entry = e.first;
    GetTrailForceRegenCache(cache_entry.first, cache_entry.second);
  }
}

RageColor Course::GetColor() const {
  // FIXME: Calculate the meter.
  int meter = 5;

  switch (PREFSMAN->m_CourseSortOrder) {
    case COURSE_SORT_PREFERRED:
      // This will also be used for autogen'd courses in some cases.
      return SORT_PREFERRED_COLOR;

    case COURSE_SORT_SONGS:
      if (m_vEntries.size() >= 7) {
        return SORT_LEVEL2_COLOR;
      } else if (m_vEntries.size() >= 4) {
        return SORT_LEVEL4_COLOR;
      } else {
        return SORT_LEVEL5_COLOR;
      }

    case COURSE_SORT_METER:
      if (!AllSongsAreFixed()) {
        return SORT_LEVEL1_COLOR;
      } else if (meter > 9) {
        return SORT_LEVEL2_COLOR;
      } else if (meter >= 7) {
        return SORT_LEVEL3_COLOR;
      } else if (meter >= 5) {
        return SORT_LEVEL4_COLOR;
      } else {
        return SORT_LEVEL5_COLOR;
      }

    case COURSE_SORT_METER_SUM:
      if (!AllSongsAreFixed()) {
        return SORT_LEVEL1_COLOR;
      }
      if (m_SortOrder_TotalDifficulty >= 40) {
        return SORT_LEVEL2_COLOR;
      }
      if (m_SortOrder_TotalDifficulty >= 30) {
        return SORT_LEVEL3_COLOR;
      }
      if (m_SortOrder_TotalDifficulty >= 20) {
        return SORT_LEVEL4_COLOR;
      } else {
        return SORT_LEVEL5_COLOR;
      }

    case COURSE_SORT_RANK:
      if (m_SortOrder_Ranking == 3) {
        return SORT_LEVEL1_COLOR;
      } else if (m_SortOrder_Ranking == 2) {
        return SORT_LEVEL3_COLOR;
      } else if (m_SortOrder_Ranking == 1) {
        return SORT_LEVEL5_COLOR;
      } else {
        return SORT_LEVEL4_COLOR;
      }
    default:
      FAIL_M(ssprintf(
          "Invalid course sort %d.", int(PREFSMAN->m_CourseSortOrder)));
  }
}

bool Course::GetTotalSeconds(StepsType steps_type, float& seconds_out) const {
  if (!AllSongsAreFixed()) {
    return false;
  }

  Trail* trail = GetTrail(steps_type, Difficulty_Medium);
  if (!trail) {
    for (int course_difficulty = 0; course_difficulty < NUM_CourseDifficulty;
         ++course_difficulty) {
      trail = GetTrail(steps_type, (CourseDifficulty)course_difficulty);
      if (trail) {
        break;
      }
    }
    if (!trail) {
      return false;
    }
  }

  seconds_out = trail->GetLengthSeconds();
  return true;
}

bool Course::CourseHasBestOrWorst() const {
  for (const CourseEntry& course_entry : m_vEntries) {
    if (course_entry.song_sort_ == SongSort_MostPlays &&
        course_entry.choose_index_ != -1) {
      return true;
    }
    if (course_entry.song_sort_ == SongSort_FewestPlays &&
        course_entry.choose_index_ != -1) {
      return true;
    }
  }

  return false;
}

RString Course::GetBannerPath() const {
  if (m_sBannerPath.empty()) {
    return RString();
  }
  if (m_sBannerPath[0] == '/') {
    return m_sBannerPath;
  }
  return Dirname(m_sPath) + m_sBannerPath;
}

RString Course::GetBackgroundPath() const {
  if (m_sBackgroundPath.empty()) {
    return RString();
  }
  if (m_sBackgroundPath[0] == '/') {
    return m_sBackgroundPath;
  }
  return Dirname(m_sPath) + m_sBackgroundPath;
}

bool Course::HasBanner() const {
  return GetBannerPath() != "" && IsAFile(GetBannerPath());
}

bool Course::HasBackground() const {
  return GetBackgroundPath() != "" && IsAFile(GetBackgroundPath());
}

void Course::UpdateCourseStats(StepsType steps_type) {
  m_SortOrder_TotalDifficulty = 0;

  // courses with random/players best-worst songs should go at the end
  for (unsigned i = 0; i < m_vEntries.size(); i++) {
    Song* song = m_vEntries[i].song_id_.ToSong();
    if (song != nullptr) {
      continue;
    }

    if (m_SortOrder_Ranking == 2) {
      m_SortOrder_Ranking = 3;
    }
    m_SortOrder_TotalDifficulty = INT_MAX;
    return;
  }

  const Trail* trail = GetTrail(steps_type, Difficulty_Medium);

  m_SortOrder_TotalDifficulty +=
      trail != nullptr ? trail->GetTotalMeter() : 0;

  // OPTIMIZATION: Ranking info isn't dependent on style, so call it
  // sparingly. It's handled on startup and when themes change.

  LOG->Trace(
      "%s: Total feet: %d", this->m_sMainTitle.c_str(),
      m_SortOrder_TotalDifficulty);
}

bool Course::IsRanking() const {
  std::vector<RString> ranking_songs;

  split(THEME->GetMetric("ScreenRanking", "CoursesToShow"), ",", ranking_songs);

  for (unsigned i = 0; i < ranking_songs.size(); ++i) {
    if (ranking_songs[i].EqualsNoCase(m_sPath)) {
      return true;
    }
  }

  return false;
}

const CourseEntry* Course::FindFixedSong(const Song* song) const {
  for (const CourseEntry& entry : m_vEntries) {
    Song* entry_song = entry.song_id_.ToSong();
    if (song == entry_song) {
      return &entry;
    }
  }

  return nullptr;
}

void Course::GetAllCachedTrails(std::vector<Trail*>& out) {
  for (auto it = m_TrailCache.begin(); it != m_TrailCache.end(); ++it) {
    CacheData& cache_data = it->second;
    if (!cache_data.null) {
      out.push_back(&cache_data.trail);
    }
  }
}

bool Course::ShowInDemonstrationAndRanking() const {
  // Don't show endless courses in Ranking.
  // todo: make this a metric of course types not to show? -aj
  return !IsEndless();
}

void Course::CalculateRadarValues() {
  FOREACH_ENUM(StepsType, steps_type) {
    FOREACH_ENUM(CourseDifficulty, course_difficulty) {
      // For courses that aren't fixed, the radar values are meaningless.
      // Makes non-fixed courses have unknown radar values.
      if (AllSongsAreFixed()) {
        Trail* trail = GetTrail(steps_type, course_difficulty);
        if (trail == nullptr) {
          continue;
        }
        RadarValues radar_values = trail->GetRadarValues();
        m_RadarCache[CacheEntry(steps_type, course_difficulty)] = radar_values;
      } else {
        m_RadarCache[CacheEntry(steps_type, course_difficulty)] = RadarValues();
      }
    }
  }
}

bool Course::Matches(RString group, RString course) const {
  if (group.size() && group.CompareNoCase(this->m_sGroupName) != 0) {
    return false;
  }

  RString file = m_sPath;
  if (!file.empty()) {
    file.Replace("\\", "/");
    std::vector<RString> bits;
    split(file, "/", bits);
    const RString& last_bit = bits[bits.size() - 1];
    if (course.EqualsNoCase(last_bit)) {
      return true;
    }
  }

  if (course.EqualsNoCase(this->GetTranslitFullTitle())) {
    return true;
  }

  return false;
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the CourseEntry. */
class LunaCourseEntry : public Luna<CourseEntry> {
 public:
  static int GetSong(T* p, lua_State* L) {
    if (p->song_id_.ToSong()) {
      p->song_id_.ToSong()->PushSelf(L);
    } else {
      lua_pushnil(L);
    }
    return 1;
  }
  DEFINE_METHOD(IsSecret, is_secret_);
  DEFINE_METHOD(IsFixedSong, IsFixedSong());
  DEFINE_METHOD(GetGainSeconds, gain_seconds_);
  DEFINE_METHOD(GetGainLives, gain_lives_);
  DEFINE_METHOD(GetNormalModifiers, modifiers_);
  // GetTimedModifiers - table
  DEFINE_METHOD(GetNumModChanges, GetNumModChanges());
  DEFINE_METHOD(GetTextDescription, GetTextDescription());

  LunaCourseEntry() {
    ADD_METHOD(GetSong);
    // sm-ssc additions:
    ADD_METHOD(IsSecret);
    ADD_METHOD(IsFixedSong);
    ADD_METHOD(GetGainSeconds);
    ADD_METHOD(GetGainLives);
    ADD_METHOD(GetNormalModifiers);
    // ADD_METHOD( GetTimedModifiers );
    ADD_METHOD(GetNumModChanges);
    ADD_METHOD(GetTextDescription);
  }
};

LUA_REGISTER_CLASS(CourseEntry)

// Now for the Course bindings:
/** @brief Allow Lua to have access to the Course. */
class LunaCourse : public Luna<Course> {
 public:
  DEFINE_METHOD(GetPlayMode, GetPlayMode())
  static int GetDisplayFullTitle(T* p, lua_State* L) {
    lua_pushstring(L, p->GetDisplayFullTitle());
    return 1;
  }
  static int GetTranslitFullTitle(T* p, lua_State* L) {
    lua_pushstring(L, p->GetTranslitFullTitle());
    return 1;
  }
  static int HasMods(T* p, lua_State* L) {
    lua_pushboolean(L, p->HasMods());
    return 1;
  }
  static int HasTimedMods(T* p, lua_State* L) {
    lua_pushboolean(L, p->HasTimedMods());
    return 1;
  }
  DEFINE_METHOD(GetCourseType, GetCourseType())
  static int GetCourseEntry(T* p, lua_State* L) {
    std::size_t id = static_cast<std::size_t>(IArg(1));
    if (id >= p->m_vEntries.size()) {
      lua_pushnil(L);
    } else {
      p->m_vEntries[id].PushSelf(L);
    }
    return 1;
  }
  static int GetCourseEntries(T* p, lua_State* L) {
    std::vector<CourseEntry*> v;
    for (unsigned i = 0; i < p->m_vEntries.size(); ++i) {
      v.push_back(&p->m_vEntries[i]);
    }
    LuaHelpers::CreateTableFromArray<CourseEntry*>(v, L);
    return 1;
  }
  static int GetNumCourseEntries(T* p, lua_State* L) {
    lua_pushnumber(L, p->m_vEntries.size());
    return 1;
  }
  static int GetAllTrails(T* p, lua_State* L) {
    std::vector<Trail*> v;
    p->GetAllTrails(v);
    LuaHelpers::CreateTableFromArray<Trail*>(v, L);
    return 1;
  }
  static int GetBannerPath(T* p, lua_State* L) {
    RString s = p->GetBannerPath();
    if (s.empty()) {
      return 0;
    }
    LuaHelpers::Push(L, s);
    return 1;
  }
  static int GetBackgroundPath(T* p, lua_State* L) {
    RString s = p->GetBackgroundPath();
    if (s.empty()) {
      return 0;
    }
    LuaHelpers::Push(L, s);
    return 1;
  }
  static int GetCourseDir(T* p, lua_State* L) {
    lua_pushstring(L, p->m_sPath);
    return 1;
  }
  static int GetGroupName(T* p, lua_State* L) {
    lua_pushstring(L, p->m_sGroupName);
    return 1;
  }
  static int IsAutogen(T* p, lua_State* L) {
    lua_pushboolean(L, p->m_bIsAutogen);
    return 1;
  }
  static int GetEstimatedNumStages(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetEstimatedNumStages());
    return 1;
  }
  static int GetScripter(T* p, lua_State* L) {
    lua_pushstring(L, p->m_sScripter);
    return 1;
  }
  static int GetDescription(T* p, lua_State* L) {
    lua_pushstring(L, p->m_sDescription);
    return 1;
  }
  static int GetTotalSeconds(T* p, lua_State* L) {
    StepsType st = Enum::Check<StepsType>(L, 1);
    float fTotalSeconds;
    if (!p->GetTotalSeconds(st, fTotalSeconds)) {
      lua_pushnil(L);
    } else {
      lua_pushnumber(L, fTotalSeconds);
    }
    return 1;
  }
  DEFINE_METHOD(IsEndless, IsEndless())
  DEFINE_METHOD(IsNonstop, IsNonstop())
  DEFINE_METHOD(IsOni, IsOni())
  DEFINE_METHOD(GetGoalSeconds, m_fGoalSeconds)
  static int HasBanner(T* p, lua_State* L) {
    lua_pushboolean(L, p->HasBanner());
    return 1;
  }
  static int HasBackground(T* p, lua_State* L) {
    lua_pushboolean(L, p->HasBackground());
    return 1;
  }
  DEFINE_METHOD(IsAnEdit, IsAnEdit())
  static int IsPlayableIn(T* p, lua_State* L) {
    StepsType st = Enum::Check<StepsType>(L, 1);
    lua_pushboolean(L, p->IsPlayableIn(st));
    return 1;
  }
  DEFINE_METHOD(IsRanking, IsRanking())
  DEFINE_METHOD(AllSongsAreFixed, AllSongsAreFixed())

  LunaCourse() {
    ADD_METHOD(GetPlayMode);
    ADD_METHOD(GetDisplayFullTitle);
    ADD_METHOD(GetTranslitFullTitle);
    ADD_METHOD(HasMods);
    ADD_METHOD(HasTimedMods);
    ADD_METHOD(GetCourseType);
    ADD_METHOD(GetCourseEntry);
    ADD_METHOD(GetCourseEntries);
    ADD_METHOD(GetNumCourseEntries);
    ADD_METHOD(GetAllTrails);
    ADD_METHOD(GetBannerPath);
    ADD_METHOD(GetBackgroundPath);
    ADD_METHOD(GetCourseDir);
    ADD_METHOD(GetGroupName);
    ADD_METHOD(IsAutogen);
    ADD_METHOD(GetEstimatedNumStages);
    ADD_METHOD(GetScripter);
    ADD_METHOD(GetDescription);
    ADD_METHOD(GetTotalSeconds);
    ADD_METHOD(IsEndless);
    ADD_METHOD(IsNonstop);
    ADD_METHOD(IsOni);
    ADD_METHOD(GetGoalSeconds);
    ADD_METHOD(HasBanner);
    ADD_METHOD(HasBackground);
    ADD_METHOD(IsAnEdit);
    ADD_METHOD(IsPlayableIn);
    ADD_METHOD(IsRanking);
    ADD_METHOD(AllSongsAreFixed);
  }
};

LUA_REGISTER_CLASS(Course)
// lua end

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
