#include "Course.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "Attack.h"
#include "CourseLoaderCRS.h"
#include "Difficulty.h"
#include "EnumHelper.h"
#include "Game.h"
#include "GameConstantsAndTypes.h"
#include "GameManager.h"
#include "GameState.h"
#include "LuaManager.h"
#include "PlayerNumber.h"
#include "Preference.h"
#include "PrefsManager.h"
#include "ProfileManager.h"
#include "RadarValues.h"
#include "RageLog.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "RageUtil/RandomNumbers.h"
#include "Song.h"
#include "SongCacheIndex.h"
#include "SongUtil.h"
#include "StdString.h"
#include "Steps.h"
#include "StepsUtil.h"
#include "Style.h"
#include "ThemeManager.h"
#include "Trail.h"
#include "UnlockManager.h"
#include "global.h"

static Preference<int> MAX_SONGS_IN_EDIT_COURSE("MaxSongsInEditCourse", -1);

static const char* SongSortNames[] = {
    "Randomize", "MostPlays", "FewestPlays", "TopGrades", "LowestGrades",
};
XToString(SongSort);
XToLocalizedString(SongSort);
StringToX(SongSort);

struct OldStyleStringToSongSortMapHolder {
  std::map<std::string, SongSort> conversion_map_;

  OldStyleStringToSongSortMapHolder() {
    conversion_map_["best"] = SongSort_MostPlays;
    conversion_map_["worst"] = SongSort_FewestPlays;
    conversion_map_["gradebest"] = SongSort_TopGrades;
    conversion_map_["gradeworst"] = SongSort_LowestGrades;
  }
};

OldStyleStringToSongSortMapHolder OldStyleStringToSongSortMapHolder_converter;

SongSort OldStyleStringToSongSort(const std::string& ss) {
  std::string s2 = ss;
  MakeLower(s2);
  std::map<std::string, SongSort>::iterator diff =
      OldStyleStringToSongSortMapHolder_converter.conversion_map_.find(s2);
  if (diff !=
      OldStyleStringToSongSortMapHolder_converter.conversion_map_.end()) {
    return diff->second;
  }
  return SongSort_Invalid;
}

/* Maximum lower value of ranges when difficult: */
const int MAX_BOTTOM_RANGE = 10;

#define SORT_PREFERRED_COLOR THEME->GetMetricC("Course", "SortPreferredColor")
#define SORT_LEVEL1_COLOR THEME->GetMetricC("Course", "SortLevel1Color")
#define SORT_LEVEL2_COLOR THEME->GetMetricC("Course", "SortLevel2Color")
#define SORT_LEVEL3_COLOR THEME->GetMetricC("Course", "SortLevel3Color")
#define SORT_LEVEL4_COLOR THEME->GetMetricC("Course", "SortLevel4Color")
#define SORT_LEVEL5_COLOR THEME->GetMetricC("Course", "SortLevel5Color")

// #define INCLUDE_BEGINNER_STEPS	THEME->GetMetricB(
// "Course","IncludeBeginnerSteps" );

std::string CourseEntry::GetTextDescription() const {
  std::vector<std::string> vsEntryDescription;
  Song* pSong = song_id_.ToSong();
  if (pSong) {
    vsEntryDescription.push_back(pSong->GetTranslitFullTitle());
  } else {
    vsEntryDescription.push_back("Random");
  }
  if (song_criteria_.group_names_.size() > 0) {
    vsEntryDescription.push_back(
        "Groups: " + join(",", song_criteria_.group_names_));
  }
  if (song_criteria_.use_song_genre_allowed_list_) {
    vsEntryDescription.push_back(
        join(",", song_criteria_.song_genre_allowed_list_));
  }
  if (song_criteria_.artist_names_.size() > 0) {
    vsEntryDescription.push_back(
        "Artists: " + join(",", song_criteria_.artist_names_));
  }
  if (steps_criteria_.difficulty_ != Difficulty_Invalid &&
      steps_criteria_.difficulty_ != Difficulty_Medium) {
    vsEntryDescription.push_back(
        CourseDifficultyToLocalizedString(steps_criteria_.difficulty_));
  }
  if (steps_criteria_.low_meter_ != -1) {
    vsEntryDescription.push_back(
        ssprintf("Low meter: %d", steps_criteria_.low_meter_));
  }
  if (steps_criteria_.high_meter_ != -1) {
    vsEntryDescription.push_back(
        ssprintf("High meter: %d", steps_criteria_.high_meter_));
  }
  if (song_criteria_.min_bpm_ != -1) {
    vsEntryDescription.push_back(
        ssprintf("Min BPM: %.3f", song_criteria_.min_bpm_));
  }
  if (song_criteria_.max_bpm_ != -1) {
    vsEntryDescription.push_back(
        ssprintf("Max BPM: %.3f", song_criteria_.max_bpm_));
  }
  if (song_criteria_.min_duration_seconds_ != -1) {
    vsEntryDescription.push_back(ssprintf(
        "Min Duration: %.3f seconds", song_criteria_.min_duration_seconds_));
  }
  if (song_criteria_.max_duration_seconds_ != -1) {
    vsEntryDescription.push_back(ssprintf(
        "Max Duration: %.3f seconds", song_criteria_.max_duration_seconds_));
  }

  if (song_sort_ != SongSort_Randomize) {
    vsEntryDescription.push_back(
        "Sort: " + SongSortToLocalizedString(song_sort_));
  }
  if (song_sort_ != SongSort_Randomize && choose_index_ != -1) {
    vsEntryDescription.push_back(
        "Choose " + FormatNumberAndSuffix(choose_index_ + 1) + " match");
  }
  int iNumModChanges = GetNumModChanges();
  if (iNumModChanges != 0) {
    vsEntryDescription.push_back(ssprintf("%d mod changes", iNumModChanges));
  }
  if (gain_seconds_ != 0) {
    vsEntryDescription.push_back(ssprintf("Low meter: %.0f", gain_seconds_));
  }

  std::string s = join(",", vsEntryDescription);
  return s;
}

int CourseEntry::GetNumModChanges() const {
  int iNumModChanges = 0;
  if (!modifiers_.empty()) {
    iNumModChanges++;
  }
  iNumModChanges += attacks_.size();
  return iNumModChanges;
}

Course::Course()
    : is_autogen_(false),
      path_(""),
      main_title_(""),
      main_title_translit_(""),
      sub_title_(""),
      sub_title_translit_(""),
      scripter_(""),
      description_(""),
      banner_path_(""),
      background_path_(""),
      group_name_(""),
      repeat_(false),
      goal_seconds_(0),
      shuffle_(false),
      lives_(-1),
      sort_by_meter_(false),
      incomplete_(false),
      entries_(),
      sort_order_total_difficulty_(0),
      sort_order_ranking_(0),
      loaded_from_profile_(ProfileSlot_Invalid),
      trail_cache_(),
      trail_cache_seed_(0),
      radar_cache_(),
      set_styles_() {
  FOREACH_ENUM(Difficulty, dc)
  custom_meter_[dc] = -1;
}

CourseType Course::GetCourseType() const {
  if (repeat_) {
    return COURSE_TYPE_ENDLESS;
  }
  if (lives_ > 0) {
    return COURSE_TYPE_ONI;
  }
  if (!entries_.empty() && entries_[0].gain_seconds_ > 0) {
    return COURSE_TYPE_SURVIVAL;
  }
  return COURSE_TYPE_NONSTOP;
}

PlayMode Course::GetPlayMode() const {
  CourseType ct = GetCourseType();
  switch (ct) {
    case COURSE_TYPE_ENDLESS:
      return PLAY_MODE_ENDLESS;
    case COURSE_TYPE_ONI:
      return PLAY_MODE_ONI;
    case COURSE_TYPE_SURVIVAL:
      return PLAY_MODE_ONI;
    case COURSE_TYPE_NONSTOP:
      return PLAY_MODE_NONSTOP;
    default:
      FAIL_M(ssprintf("Invalid course type: %i", ct));
  }
}

void Course::RevertFromDisk() {
  // trying to catch invalid an Course
  ASSERT(!path_.empty());

  CourseLoaderCRS::LoadFromCRSFile(path_, *this);
}

std::string Course::GetCacheFilePath() const {
  return SongCacheIndex::GetCacheFilePath("Courses", path_);
}

void Course::Init() {
  is_autogen_ = false;
  path_ = "";

  main_title_ = "";
  main_title_translit_ = "";
  sub_title_ = "";
  sub_title_translit_ = "";
  scripter_ = "";
  description_ = "";

  banner_path_ = "";
  background_path_ = "";
  group_name_ = "";

  repeat_ = false;
  goal_seconds_ = 0;
  shuffle_ = false;

  lives_ = -1;
  FOREACH_ENUM(Difficulty, dc)
  custom_meter_[dc] = -1;
  sort_by_meter_ = false;

  entries_.clear();

  sort_order_total_difficulty_ = 0;
  sort_order_ranking_ = 0;

  loaded_from_profile_ = ProfileSlot_Invalid;
  incomplete_ = false;
  trail_cache_.clear();
  trail_cache_seed_ = 0;
  radar_cache_.clear();
}

bool Course::IsPlayableIn(StepsType st) const {
  Trail t;
  FOREACH_ShownCourseDifficulty(cd) {
    if (GetTrailUnsorted(st, cd, t)) {
      return true;
    }
  }

  // No valid trail for this StepsType.
  return false;
}

struct SortTrailEntry {
  TrailEntry entry_;
  int sort_meter_;

  SortTrailEntry() : entry_(), sort_meter_(0) {}

  bool operator<(const SortTrailEntry& rhs) const {
    return sort_meter_ < rhs.sort_meter_;
  }
};

std::string Course::GetDisplayMainTitle() const {
  if (!PREFSMAN->show_native_language_) {
    return GetTranslitMainTitle();
  }
  return main_title_;
}

std::string Course::GetDisplaySubTitle() const {
  if (!PREFSMAN->show_native_language_) {
    return GetTranslitSubTitle();
  }
  return sub_title_;
}

std::string Course::GetDisplayFullTitle() const {
  std::string sTitle = GetDisplayMainTitle();
  std::string sSubTitle = GetDisplaySubTitle();

  if (!sSubTitle.empty()) {
    sTitle += " " + sSubTitle;
  }
  return sTitle;
}

std::string Course::GetTranslitFullTitle() const {
  std::string sTitle = GetTranslitMainTitle();
  std::string sSubTitle = GetTranslitSubTitle();

  if (!sSubTitle.empty()) {
    sTitle += " " + sSubTitle;
  }
  return sTitle;
}

/* This is called by many simple functions, like Course::GetTotalSeconds, and
 * may be called on all songs to sort.  It can take time to execute, so we cache
 * the results.  Returned pointers remain valid for the lifetime of the Course.
 * If the course difficulty doesn't exist, nullptr is returned. */
Trail* Course::GetTrail(StepsType st, CourseDifficulty cd) const {
  ASSERT(cd != Difficulty_Invalid);

  // Check to see if the Trail cache is out of date
  if (trail_cache_seed_ != GAMESTATE->stage_seed_) {
    RegenerateNonFixedTrails();
    trail_cache_seed_ = GAMESTATE->stage_seed_;
  }

  // Look in the Trail cache
  {
    TrailCache_t::iterator it = trail_cache_.find(CacheEntry(st, cd));
    if (it != trail_cache_.end()) {
      CacheData& cache = it->second;
      if (cache.null_) {
        return nullptr;
      }
      return &cache.trail_;
    }
  }

  return GetTrailForceRegenCache(st, cd);
}

Trail* Course::GetTrailForceRegenCache(
    StepsType st, CourseDifficulty cd) const {
  // Construct a new Trail, add it to the cache, then return it.
  CacheData& cache = trail_cache_[CacheEntry(st, cd)];
  Trail& trail = cache.trail_;
  trail.Init();
  if (!GetTrailSorted(st, cd, trail)) {
    // This course difficulty doesn't exist.
    cache.null_ = true;
    return nullptr;
  }

  // If we have cached RadarValues for this trail, insert them.
  {
    RadarCache_t::const_iterator it = radar_cache_.find(CacheEntry(st, cd));
    if (it != radar_cache_.end()) {
      const RadarValues& rv = it->second;
      trail.SetRadarValues(rv);
    }
  }

  cache.null_ = false;
  return &cache.trail_;
}

bool Course::GetTrailSorted(
    StepsType st, CourseDifficulty cd, Trail& trail) const {
  if (!GetTrailUnsorted(st, cd, trail)) {
    return false;
  }

  if (this->sort_by_meter_) {
    /* Sort according to Difficulty_Medium, since the order of songs
     * must not change across difficulties. */
    Trail SortTrail;
    if (cd == Difficulty_Medium) {
      SortTrail = trail;
    } else {
      bool bOK = GetTrailUnsorted(st, Difficulty_Medium, SortTrail);

      /* If we have any other difficulty, we must have Difficulty_Medium. */
      ASSERT(bOK);
    }
    ASSERT_M(
        trail.entries_.size() == SortTrail.entries_.size(),
        ssprintf(
            "%i %i", int(trail.entries_.size()),
            int(SortTrail.entries_.size())));

    std::vector<SortTrailEntry> entries;
    for (unsigned i = 0; i < trail.entries_.size(); ++i) {
      SortTrailEntry ste;
      ste.entry_ = trail.entries_[i];
      ste.sort_meter_ = SortTrail.entries_[i].steps_->GetMeter();
      entries.push_back(ste);
    }

    stable_sort(entries.begin(), entries.end());
    for (unsigned i = 0; i < trail.entries_.size(); ++i) {
      trail.entries_[i] = entries[i].entry_;
    }
  }

  if (trail.entries_.empty()) {
    return false;
  }
  return true;
}

// TODO: Move Course initialization after PROFILEMAN is created
static void CourseSortSongs(
    SongSort sort, std::vector<Song*>& vpPossibleSongs, RandomGen& rnd) {
  switch (sort) {
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
      // SongUtil::SortSongPointerArrayByGrades() will crash if called in a
      // state where there's no current master player (for instance when
      // returning to the Title Menu). A workaround is to just not call it if we
      // know that GAMESTATE->GetMasterPlayerNumber() == PlayerNumber_Invalid
      if (PROFILEMAN &&
          GAMESTATE->GetMasterPlayerNumber() != PlayerNumber_Invalid) {
        SongUtil::SortSongPointerArrayByGrades(
            vpPossibleSongs, true);  // descending
      }
      break;
    case SongSort_LowestGrades:
      if (PROFILEMAN &&
          GAMESTATE->GetMasterPlayerNumber() != PlayerNumber_Invalid) {
        SongUtil::SortSongPointerArrayByGrades(
            vpPossibleSongs, false);  // ascending
      }
      break;
    default:
      LOG->Trace(
          "CourseSortSongs sort= %d | %s invalid??", sort,
          SongSortToString(sort).c_str());
      break;
  }
}

namespace {
struct SongIsEqual {
  const Song* song_;
  SongIsEqual(const Song* pSong) : song_(pSong) {}
  bool operator()(const SongAndSteps& sas) const { return sas.song_ == song_; }
};
}  // namespace

bool Course::GetTrailUnsorted(
    StepsType st, CourseDifficulty cd, Trail& trail) const {
  trail.Init();

  // XXX: Why are beginner and challenge excluded here? -Wolfman2000
  // No idea, probably an obsolete design decision from ITG, removing
  // exclusion here, but there's some other area that prevents it too. -Kyz
  /*
  switch( cd )
  {
          case Difficulty_Beginner:
                  return false;
          case Difficulty_Challenge:
                  return false;
          default: break;
  }
  */

  // Construct a new Trail, add it to the cache, then return it.
  // Different seed for each course, but the same for the whole round:
  RandomGen rnd(GAMESTATE->stage_seed_ + GetHashForString(main_title_));

  std::vector<CourseEntry> tmp_entries;
  if (shuffle_) {
    /* Always randomize the same way per round.  Otherwise, the displayed course
     * will change every time it's viewed, and the displayed order will have no
     * bearing on what you'll actually play. */
    tmp_entries = entries_;
    std::shuffle(tmp_entries.begin(), tmp_entries.end(), rnd);
  }

  const std::vector<CourseEntry>& entries = shuffle_ ? tmp_entries : entries_;

  trail.steps_type_ = st;
  trail.course_type_ = GetCourseType();
  trail.course_difficulty_ = cd;
  trail.entries_.reserve(entries.size());

  // Set to true if CourseDifficulty is able to change something.
  bool bCourseDifficultyIsSignificant = (cd == Difficulty_Medium);

  // Resolve each entry to a Song and Steps.
  if (trail.course_type_ == COURSE_TYPE_ENDLESS) {
    GetTrailUnsortedEndless(
        entries, trail, st, cd, rnd, bCourseDifficultyIsSignificant);
  } else {
    std::vector<SongAndSteps> vSongAndSteps;
    for (auto e = entries.begin(); e != entries.end(); ++e) {
      SongAndSteps resolved;  // fill this in
      SongCriteria soc = e->song_criteria_;

      Song* pSong = e->song_id_.ToSong();
      if (pSong) {
        soc.use_song_allowed_list_ = true;
        soc.song_allowed_list_.push_back(pSong);
      }
      soc.tutorial_ = SongCriteria::Tutorial_No;
      soc.locked_ = SongCriteria::Locked_Unlocked;
      if (!soc.use_song_allowed_list_) {
        soc.max_stages_for_song_ = 1;
      }

      StepsCriteria stc = e->steps_criteria_;
      stc.st_ = st;
      stc.locked_ = StepsCriteria::Locked_Unlocked;

      const bool bSameSongCriteria =
          e != entries.begin() && (e - 1)->song_criteria_ == soc;
      const bool bSameStepsCriteria =
          e != entries.begin() && (e - 1)->steps_criteria_ == stc;

      if (pSong) {
        vSongAndSteps.clear();
        StepsUtil::GetAllMatching(pSong, stc, vSongAndSteps);
      } else if (
          vSongAndSteps.empty() || !(bSameSongCriteria && bSameStepsCriteria)) {
        vSongAndSteps.clear();
        StepsUtil::GetAllMatching(soc, stc, vSongAndSteps);
      }

      // It looks bad to have the same song 2x in a row in a randomly generated
      // course. Don't allow the same song to be played 2x in a row, unless
      // there's only one song in vpPossibleSongs.
      if (trail.entries_.size() > 0 && vSongAndSteps.size() > 1) {
        const TrailEntry& teLast = trail.entries_.back();
        RemoveIf(vSongAndSteps, SongIsEqual(teLast.song_));
      }

      // if there are no songs to choose from, abort this CourseEntry
      if (vSongAndSteps.empty()) {
        continue;
      }

      std::vector<Song*> vpSongs;
      typedef std::vector<Steps*> StepsVector;
      std::map<Song*, StepsVector> mapSongToSteps;
      for (std::vector<SongAndSteps>::const_iterator sas =
               vSongAndSteps.begin();
           sas != vSongAndSteps.end(); ++sas) {
        StepsVector& v = mapSongToSteps[sas->song_];

        v.push_back(sas->steps_);
        if (v.size() == 1) {
          vpSongs.push_back(sas->song_);
        }
      }

      CourseSortSongs(e->song_sort_, vpSongs, rnd);

      ASSERT(e->choose_index_ >= 0);
      if (e->choose_index_ < int(vSongAndSteps.size())) {
        resolved.song_ = vpSongs[e->choose_index_];
        const std::vector<Steps*>& mappedSongs = mapSongToSteps[resolved.song_];
        resolved.steps_ = mappedSongs[RandomInt(mappedSongs.size())];
      } else {
        continue;
      }

      /* If we're not COURSE_DIFFICULTY_REGULAR, then we should be choosing
       * steps that are either easier or harder than the base difficulty.  If no
       * such steps exist, then just use the one we already have. */
      Difficulty dc = resolved.steps_->GetDifficulty();
      int iLowMeter = e->steps_criteria_.low_meter_;
      int iHighMeter = e->steps_criteria_.high_meter_;
      if (cd != Difficulty_Medium && !e->no_difficult_) {
        Difficulty new_dc = (Difficulty)(dc + cd - Difficulty_Medium);
        new_dc = std::clamp(
            new_dc, (Difficulty)0, (Difficulty)(Difficulty_Edit - 1));
        /*
        // re-edit this code to work using the metric.
        Difficulty new_dc;
        if( INCLUDE_BEGINNER_STEPS )
        {
        // don't factor in the course difficulty if we're including
        // beginner steps -aj
        new_dc = clamp( dc, Difficulty_Beginner, (Difficulty)(Difficulty_Edit-1)
        );
        }
        else
        {
        new_dc = (Difficulty)(dc + cd - Difficulty_Medium);
        new_dc = clamp( new_dc, (Difficulty)0, (Difficulty)(Difficulty_Edit-1)
        );
        }
        */

        bool bChangedDifficulty = false;
        if (new_dc != dc) {
          Steps* pNewSteps =
              SongUtil::GetStepsByDifficulty(resolved.song_, st, new_dc);
          if (pNewSteps) {
            dc = new_dc;
            resolved.steps_ = pNewSteps;
            bChangedDifficulty = true;
            bCourseDifficultyIsSignificant = true;
          }
        }

        /* Hack: We used to adjust low_meter/high_meter above while searching
         * for songs.  However, that results in a different song being chosen
         * for difficult courses, which is bad when LockCourseDifficulties is
         * disabled; each player can end up with a different song.  Instead,
         * choose based on the original range, bump the steps based on course
         * difficulty, and then retroactively tweak the low_meter/high_meter so
         * course displays line up. */
        if (e->steps_criteria_.difficulty_ == Difficulty_Invalid &&
            bChangedDifficulty) {
          /* Minimum and maximum to add to make the meter range contain the
           * actual meter: */
          int iMinDist = resolved.steps_->GetMeter() - iHighMeter;
          int iMaxDist = resolved.steps_->GetMeter() - iLowMeter;

          /* Clamp the possible adjustments to try to avoid going under 1 or
           * over MAX_BOTTOM_RANGE. */
          iMinDist = std::min(std::max(iMinDist, -iLowMeter + 1), iMaxDist);
          iMaxDist = std::max(
              std::min(iMaxDist, MAX_BOTTOM_RANGE - iHighMeter), iMinDist);

          int iAdd;
          if (iMaxDist == iMinDist) {
            iAdd = iMaxDist;
          } else {
            iAdd = std::floor((iMinDist + iMaxDist) / 2);
          }
          iLowMeter += iAdd;
          iHighMeter += iAdd;
        }
      }

      TrailEntry te;
      te.song_ = resolved.song_;
      te.steps_ = resolved.steps_;
      te.modifiers_ = e->modifiers_;
      te.attacks_ = e->attacks_;
      te.secret_ = e->secret_;
      te.low_meter_ = iLowMeter;
      te.high_meter_ = iHighMeter;

      /* If we chose based on meter (not difficulty), then store
       * Difficulty_Invalid, so other classes can tell that we used meter. */
      if (e->steps_criteria_.difficulty_ == Difficulty_Invalid) {
        te.dc_ = Difficulty_Invalid;
      } else {
        /* Otherwise, store the actual difficulty we got
         * (post-course-difficulty). This may or may not be the same as
         * e.difficulty. */
        te.dc_ = dc;
      }
      trail.entries_.push_back(te);

      // LOG->Trace( "Chose: %s, %d", te.pSong->GetSongDir().c_str(),
      // te.pSteps->GetMeter() );

      if (IsAnEdit() && MAX_SONGS_IN_EDIT_COURSE > 0 &&
          int(trail.entries_.size()) >= MAX_SONGS_IN_EDIT_COURSE) {
        break;
      }
    }
  }

  /* Hack: If any entry was non-FIXED, or m_bShuffle is set, then radar values
   * for this trail will be meaningless as they'll change every time. Pre-cache
   * empty data. XXX: How can we do this cleanly, without propagating lots of
   * otherwise unnecessary data (course entry types, m_bShuffle) to Trail, or
   * storing a Course pointer in Trail (yuck)? */
  if (!AllSongsAreFixed() || shuffle_) {
    trail.radar_values_cached_ = true;
    trail.cached_radar_values_ = RadarValues();
  }

  /* If we have a manually-entered meter for this difficulty, use it. */
  if (custom_meter_[cd] != -1) {
    trail.specified_meter_ = custom_meter_[cd];
  }

  /* If the course difficulty never actually changed anything, then this
   * difficulty is equivalent to Difficulty_Medium; it doesn't exist. */
  return bCourseDifficultyIsSignificant && trail.entries_.size() > 0;
}

void Course::GetTrailUnsortedEndless(
    const std::vector<CourseEntry>& entries, Trail& trail, StepsType& st,
    CourseDifficulty& cd, RandomGen& rnd,
    bool& bCourseDifficultyIsSignificant) const {
  typedef std::vector<Steps*> StepsVector;

  std::set<Song*> alreadySelected;
  Song* lastSongSelected = nullptr;
  std::vector<SongAndSteps> vSongAndSteps;
  for (auto e = entries.begin(); e != entries.end(); ++e) {
    SongAndSteps resolved;  // fill this in
    SongCriteria soc = e->song_criteria_;

    Song* pSong = e->song_id_.ToSong();
    if (pSong) {
      soc.use_song_allowed_list_ = true;
      soc.song_allowed_list_.push_back(pSong);
    }
    soc.tutorial_ = SongCriteria::Tutorial_No;
    soc.locked_ = SongCriteria::Locked_Unlocked;
    if (!soc.use_song_allowed_list_) {
      soc.max_stages_for_song_ = 1;
    }

    StepsCriteria stc = e->steps_criteria_;
    stc.st_ = st;
    stc.locked_ = StepsCriteria::Locked_Unlocked;

    const bool bSameSongCriteria =
        e != entries.begin() && (e - 1)->song_criteria_ == soc;
    const bool bSameStepsCriteria =
        e != entries.begin() && (e - 1)->steps_criteria_ == stc;

    // If we're doing the same wildcard search as last entry,
    // we can just reuse the vSongAndSteps vector.
    if (pSong) {
      vSongAndSteps.clear();
      StepsUtil::GetAllMatchingEndless(pSong, stc, vSongAndSteps);
    } else if (
        vSongAndSteps.empty() || !(bSameSongCriteria && bSameStepsCriteria)) {
      vSongAndSteps.clear();
      StepsUtil::GetAllMatching(soc, stc, vSongAndSteps);
    }

    // if there are no songs to choose from, abort this CourseEntry
    if (vSongAndSteps.empty()) {
      continue;
    }

    // if we're doing a RANDOM wildcard search, try to avoid repetition
    if (vSongAndSteps.size() > 1 &&
        e->song_sort_ == SongSort::SongSort_Randomize) {
      // Make a backup of the steplist so we can revert if we overfilter
      std::vector<SongAndSteps> revertList = vSongAndSteps;
      // Filter candidate list via blacklist
      RemoveIf(vSongAndSteps, [&](const SongAndSteps& ss) {
        return std::find(
                   alreadySelected.begin(), alreadySelected.end(), ss.song_) !=
               alreadySelected.end();
      });
      // If every candidate is in the blacklist, pick random song that wasn't
      // played last (Repeat songs may still occur if song after this is fixed;
      // this algorithm doesn't look ahead)
      if (vSongAndSteps.empty()) {
        vSongAndSteps = revertList;
        RemoveIf(vSongAndSteps, SongIsEqual(lastSongSelected));

        // If the song that was played last was the only candidate, give up pick
        // randomly
        if (vSongAndSteps.empty()) {
          vSongAndSteps = revertList;
        }
      }
    }

    std::vector<Song*> vpSongs;
    std::map<Song*, StepsVector> songStepMap;
    // Build list of songs for sorting, and mapping of songs to steps
    // simultaneously
    for (auto& ss : vSongAndSteps) {
      StepsVector& stepsForSong = songStepMap[ss.song_];
      // If we haven't noted this song yet, add it to the song list
      if (stepsForSong.size() == 0) {
        vpSongs.push_back(ss.song_);
      }

      stepsForSong.push_back(ss.steps_);
    }

    ASSERT(e->choose_index_ >= 0);
    // If we're trying to pick BEST100 when only 99 songs exist,
    // we have a problem, so bail out
    if (static_cast<size_t>(e->choose_index_) >= vpSongs.size()) {
      continue;
    }

    // Otherwise, pick random steps corresponding to the selected song
    CourseSortSongs(e->song_sort_, vpSongs, rnd);
    resolved.song_ = vpSongs[e->choose_index_];
    const std::vector<Steps*>& songSteps = songStepMap[resolved.song_];
    resolved.steps_ = songSteps[RandomInt(songSteps.size())];

    lastSongSelected = resolved.song_;
    alreadySelected.emplace(resolved.song_);

    /* If we're not COURSE_DIFFICULTY_REGULAR, then we should be choosing steps
     * that are either easier or harder than the base difficulty.  If no such
     * steps exist, then just use the one we already have. */
    Difficulty dc = resolved.steps_->GetDifficulty();
    int iLowMeter = e->steps_criteria_.low_meter_;
    int iHighMeter = e->steps_criteria_.high_meter_;
    if (cd != Difficulty_Medium && !e->no_difficult_) {
      Difficulty new_dc = (Difficulty)(dc + cd - Difficulty_Medium);
      if (dc != Difficulty_Medium) {
        new_dc = cd;
      }
      new_dc =
          std::clamp(new_dc, (Difficulty)0, (Difficulty)(Difficulty_Edit - 1));
      /*
      // re-edit this code to work using the metric.
      Difficulty new_dc;
      if( INCLUDE_BEGINNER_STEPS )
      {
              // don't factor in the course difficulty if we're including
              // beginner steps -aj
              new_dc = clamp( dc, Difficulty_Beginner,
      (Difficulty)(Difficulty_Edit-1) );
      }
      else
      {
              new_dc = (Difficulty)(dc + cd - Difficulty_Medium);
              new_dc = clamp( new_dc, (Difficulty)0,
      (Difficulty)(Difficulty_Edit-1) );
      }
      */

      bool bChangedDifficulty = false;
      if (new_dc != dc) {
        Steps* pNewSteps =
            SongUtil::GetStepsByDifficulty(resolved.song_, st, new_dc);
        if (pNewSteps) {
          dc = new_dc;
          resolved.steps_ = pNewSteps;
          bChangedDifficulty = true;
          bCourseDifficultyIsSignificant = true;
        }
      }

      /* Hack: We used to adjust low_meter/high_meter above while searching for
       * songs.  However, that results in a different song being chosen for
       * difficult courses, which is bad when LockCourseDifficulties is
       * disabled; each player can end up with a different song.  Instead,
       * choose based on the original range, bump the steps based on course
       * difficulty, and then retroactively tweak the low_meter/high_meter so
       * course displays line up. */
      if (e->steps_criteria_.difficulty_ == Difficulty_Invalid &&
          bChangedDifficulty) {
        /* Minimum and maximum to add to make the meter range contain the actual
         * meter: */
        int iMinDist = resolved.steps_->GetMeter() - iHighMeter;
        int iMaxDist = resolved.steps_->GetMeter() - iLowMeter;

        /* Clamp the possible adjustments to try to avoid going under 1 or over
         * MAX_BOTTOM_RANGE. */
        iMinDist = std::min(std::max(iMinDist, -iLowMeter + 1), iMaxDist);
        iMaxDist = std::max(
            std::min(iMaxDist, MAX_BOTTOM_RANGE - iHighMeter), iMinDist);

        int iAdd;
        if (iMaxDist == iMinDist) {
          iAdd = iMaxDist;
        } else {
          iAdd = std::floor((iMinDist + iMaxDist) / 2);
        }
        iLowMeter += iAdd;
        iHighMeter += iAdd;
      }
    }

    TrailEntry te;
    te.song_ = resolved.song_;
    te.steps_ = resolved.steps_;
    te.modifiers_ = e->modifiers_;
    te.attacks_ = e->attacks_;
    te.secret_ = e->secret_;
    te.low_meter_ = iLowMeter;
    te.high_meter_ = iHighMeter;

    /* If we chose based on meter (not difficulty), then store
     * Difficulty_Invalid, so other classes can tell that we used meter. */
    if (e->steps_criteria_.difficulty_ == Difficulty_Invalid) {
      te.dc_ = Difficulty_Invalid;
    } else {
      /* Otherwise, store the actual difficulty we got (post-course-difficulty).
       * This may or may not be the same as e.difficulty. */
      te.dc_ = dc;
    }

    trail.entries_.push_back(te);
    // LOG->Trace( "Chose: %s, %d", te.pSong->GetSongDir().c_str(),
    // te.pSteps->GetMeter() );

    if (IsAnEdit() && MAX_SONGS_IN_EDIT_COURSE > 0 &&
        int(trail.entries_.size()) >= MAX_SONGS_IN_EDIT_COURSE) {
      break;
    }
  }
}

void Course::GetTrails(std::vector<Trail*>& AddTo, StepsType st) const {
  FOREACH_ShownCourseDifficulty(cd) {
    Trail* pTrail = GetTrail(st, cd);
    if (pTrail == nullptr) {
      continue;
    }
    AddTo.push_back(pTrail);
  }
}

void Course::GetAllTrails(std::vector<Trail*>& AddTo) const {
  std::vector<StepsType> vStepsTypesToShow;
  GAMEMAN->GetStepsTypesForGame(GAMESTATE->cur_game_, vStepsTypesToShow);
  for (const StepsType& st : vStepsTypesToShow) {
    GetTrails(AddTo, st);
  }
}

int Course::GetMeter(StepsType st, CourseDifficulty cd) const {
  if (custom_meter_[cd] != -1) {
    return custom_meter_[cd];
  }
  const Trail* pTrail = GetTrail(st);
  if (pTrail != nullptr) {
    return pTrail->GetMeter();
  }
  return 0;
}

bool Course::HasMods() const {
  return std::any_of(
      entries_.begin(), entries_.end(),
      [](const CourseEntry& e) { return !e.attacks_.empty(); });
}

bool Course::HasTimedMods() const {
  // What makes this different from the SM4 implementation is that
  // HasTimedMods now searches for bGlobal in the attacks; if one of
  // them is false, it has timed mods. Also returning false will probably
  // take longer than expected. -aj
  for (const CourseEntry& e : entries_) {
    if (e.attacks_.empty()) {
      continue;
    }
    if (std::any_of(e.attacks_.begin(), e.attacks_.end(), [](const Attack& a) {
          return !a.global_;
        })) {
      return true;
    }
  }
  return false;
}

bool Course::AllSongsAreFixed() const {
  return std::all_of(
      entries_.begin(), entries_.end(),
      [](const CourseEntry& e) { return e.IsFixedSong(); });
}

const Style* Course::GetCourseStyle(const Game* pGame, int iNumPlayers) const {
  std::vector<const Style*> vpStyles;
  GAMEMAN->GetCompatibleStyles(pGame, iNumPlayers, vpStyles);

  for (const Style* pStyle : vpStyles) {
    for (const std::string& style : set_styles_) {
      if (!CompareNoCase(style, pStyle->name_)) {
        return pStyle;
      }
    }
  }
  return nullptr;
}

void Course::InvalidateTrailCache() { trail_cache_.clear(); }

void Course::Invalidate(const Song* pStaleSong) {
  for (const CourseEntry& e : entries_) {
    Song* pSong = e.song_id_.ToSong();
    if (pSong == pStaleSong)  // a fixed entry that references the stale Song
    {
      RevertFromDisk();
      return;
    }
  }

  // Invalidate any Trails that contain this song.
  // If we find a Trail that contains this song, then it's part of a
  // non-fixed entry. So, regenerating the Trail will force different
  // songs to be chosen.
  FOREACH_ENUM(StepsType, st) {
    FOREACH_ShownCourseDifficulty(cd) {
      TrailCache_t::iterator it = trail_cache_.find(CacheEntry(st, cd));
      if (it == trail_cache_.end()) {
        continue;
      }
      CacheData& cache = it->second;
      if (!cache.null_) {
        if (GetTrail(st, cd)->ContainsSong(pStaleSong)) {
          trail_cache_.erase(it);
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

  for (const auto& e : trail_cache_) {
    const CacheEntry& ce = e.first;
    GetTrailForceRegenCache(ce.first, ce.second);
  }
}

RageColor Course::GetColor() const {
  // FIXME: Calculate the meter.
  int iMeter = 5;

  switch (PREFSMAN->course_sort_order_) {
    case COURSE_SORT_PREFERRED:
      return SORT_PREFERRED_COLOR;  // This will also be used for autogen'd
                                    // courses in some cases.

    case COURSE_SORT_SONGS:
      if (entries_.size() >= 7) {
        return SORT_LEVEL2_COLOR;
      } else if (entries_.size() >= 4) {
        return SORT_LEVEL4_COLOR;
      } else {
        return SORT_LEVEL5_COLOR;
      }

    case COURSE_SORT_METER:
      if (!AllSongsAreFixed()) {
        return SORT_LEVEL1_COLOR;
      } else if (iMeter > 9) {
        return SORT_LEVEL2_COLOR;
      } else if (iMeter >= 7) {
        return SORT_LEVEL3_COLOR;
      } else if (iMeter >= 5) {
        return SORT_LEVEL4_COLOR;
      } else {
        return SORT_LEVEL5_COLOR;
      }

    case COURSE_SORT_METER_SUM:
      if (!AllSongsAreFixed()) {
        return SORT_LEVEL1_COLOR;
      }
      if (sort_order_total_difficulty_ >= 40) {
        return SORT_LEVEL2_COLOR;
      }
      if (sort_order_total_difficulty_ >= 30) {
        return SORT_LEVEL3_COLOR;
      }
      if (sort_order_total_difficulty_ >= 20) {
        return SORT_LEVEL4_COLOR;
      } else {
        return SORT_LEVEL5_COLOR;
      }

    case COURSE_SORT_RANK:
      if (sort_order_ranking_ == 3) {
        return SORT_LEVEL1_COLOR;
      } else if (sort_order_ranking_ == 2) {
        return SORT_LEVEL3_COLOR;
      } else if (sort_order_ranking_ == 1) {
        return SORT_LEVEL5_COLOR;
      } else {
        return SORT_LEVEL4_COLOR;
      }
    default:
      FAIL_M(ssprintf(
          "Invalid course sort %d.", int(PREFSMAN->course_sort_order_)));
  }
}

bool Course::GetTotalSeconds(StepsType st, float& fSecondsOut) const {
  if (!AllSongsAreFixed()) {
    return false;
  }

  Trail* trail = GetTrail(st, Difficulty_Medium);
  if (!trail) {
    for (int cd = 0; cd < NUM_CourseDifficulty; ++cd) {
      trail = GetTrail(st, (CourseDifficulty)cd);
      if (trail) {
        break;
      }
    }
    if (!trail) {
      return false;
    }
  }

  fSecondsOut = trail->GetLengthSeconds();
  return true;
}

bool Course::CourseHasBestOrWorst() const {
  for (const CourseEntry& e : entries_) {
    if (e.song_sort_ == SongSort_MostPlays && e.choose_index_ != -1) {
      return true;
    }
    if (e.song_sort_ == SongSort_FewestPlays && e.choose_index_ != -1) {
      return true;
    }
  }

  return false;
}

std::string Course::GetBannerPath() const {
  if (banner_path_.empty()) {
    return std::string();
  }
  if (banner_path_[0] == '/') {
    return banner_path_;
  }
  return Dirname(path_) + banner_path_;
}

std::string Course::GetBackgroundPath() const {
  if (background_path_.empty()) {
    return std::string();
  }
  if (background_path_[0] == '/') {
    return background_path_;
  }
  return Dirname(path_) + background_path_;
}

bool Course::HasBanner() const {
  return GetBannerPath() != "" && IsAFile(GetBannerPath());
}

bool Course::HasBackground() const {
  return GetBackgroundPath() != "" && IsAFile(GetBackgroundPath());
}

void Course::UpdateCourseStats(StepsType st) {
  sort_order_total_difficulty_ = 0;

  // courses with random/players best-worst songs should go at the end
  for (unsigned i = 0; i < entries_.size(); i++) {
    Song* pSong = entries_[i].song_id_.ToSong();
    if (pSong != nullptr) {
      continue;
    }

    if (sort_order_ranking_ == 2) {
      sort_order_ranking_ = 3;
    }
    sort_order_total_difficulty_ = INT_MAX;
    return;
  }

  const Trail* pTrail = GetTrail(st, Difficulty_Medium);

  sort_order_total_difficulty_ +=
      pTrail != nullptr ? pTrail->GetTotalMeter() : 0;

  // OPTIMIZATION: Ranking info isn't dependent on style, so call it
  // sparingly. It's handled on startup and when themes change.

  LOG->Trace(
      "%s: Total feet: %d", this->main_title_.c_str(),
      sort_order_total_difficulty_);
}

bool Course::IsRanking() const {
  std::vector<std::string> rankingsongs;

  split(THEME->GetMetric("ScreenRanking", "CoursesToShow"), ",", rankingsongs);

  for (unsigned i = 0; i < rankingsongs.size(); i++) {
    if (EqualsNoCase(rankingsongs[i], path_)) {
      return true;
    }
  }

  return false;
}

const CourseEntry* Course::FindFixedSong(const Song* pSong) const {
  for (const CourseEntry& entry : entries_) {
    Song* lSong = entry.song_id_.ToSong();
    if (pSong == lSong) {
      return &entry;
    }
  }

  return nullptr;
}

bool Course::ShowInDemonstrationAndRanking() const {
  // Don't show endless courses in Ranking.
  // todo: make this a metric of course types not to show? -aj
  return !IsEndless();
}

void Course::CalculateRadarValues() {
  FOREACH_ENUM(StepsType, st) {
    FOREACH_ENUM(CourseDifficulty, cd) {
      // For courses that aren't fixed, the radar values are meaningless.
      // Makes non-fixed courses have unknown radar values.
      if (AllSongsAreFixed()) {
        Trail* pTrail = GetTrail(st, cd);
        if (pTrail == nullptr) {
          continue;
        }
        RadarValues rv = pTrail->GetRadarValues();
        radar_cache_[CacheEntry(st, cd)] = rv;
      } else {
        radar_cache_[CacheEntry(st, cd)] = RadarValues();
      }
    }
  }
}

bool Course::Matches(std::string sGroup, std::string sCourse) const {
  if (sGroup.size() && CompareNoCase(sGroup, this->group_name_) != 0) {
    return false;
  }

  std::string sFile = path_;
  if (!sFile.empty()) {
    Replace(sFile, "\\", "/");
    std::vector<std::string> bits;
    split(sFile, "/", bits);
    const std::string& sLastBit = bits[bits.size() - 1];
    if (EqualsNoCase(sCourse, sLastBit)) {
      return true;
    }
  }

  if (EqualsNoCase(sCourse, this->GetTranslitFullTitle())) {
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
  DEFINE_METHOD(IsSecret, secret_);
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
    lua_pushstring(L, p->GetDisplayFullTitle().c_str());
    return 1;
  }
  static int GetTranslitFullTitle(T* p, lua_State* L) {
    lua_pushstring(L, p->GetTranslitFullTitle().c_str());
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
    size_t id = static_cast<size_t>(IArg(1));
    if (id >= p->entries_.size()) {
      lua_pushnil(L);
    } else {
      p->entries_[id].PushSelf(L);
    }
    return 1;
  }
  static int GetCourseEntries(T* p, lua_State* L) {
    std::vector<CourseEntry*> v;
    for (unsigned i = 0; i < p->entries_.size(); ++i) {
      v.push_back(&p->entries_[i]);
    }
    LuaHelpers::CreateTableFromArray<CourseEntry*>(v, L);
    return 1;
  }
  static int GetNumCourseEntries(T* p, lua_State* L) {
    lua_pushnumber(L, p->entries_.size());
    return 1;
  }
  static int GetAllTrails(T* p, lua_State* L) {
    std::vector<Trail*> v;
    p->GetAllTrails(v);
    LuaHelpers::CreateTableFromArray<Trail*>(v, L);
    return 1;
  }
  static int GetBannerPath(T* p, lua_State* L) {
    std::string s = p->GetBannerPath();
    if (s.empty()) {
      return 0;
    }
    LuaHelpers::Push(L, s);
    return 1;
  }
  static int GetBackgroundPath(T* p, lua_State* L) {
    std::string s = p->GetBackgroundPath();
    if (s.empty()) {
      return 0;
    }
    LuaHelpers::Push(L, s);
    return 1;
  }
  static int GetCourseDir(T* p, lua_State* L) {
    lua_pushstring(L, p->path_.c_str());
    return 1;
  }
  static int GetGroupName(T* p, lua_State* L) {
    lua_pushstring(L, p->group_name_.c_str());
    return 1;
  }
  static int IsAutogen(T* p, lua_State* L) {
    lua_pushboolean(L, p->is_autogen_);
    return 1;
  }
  static int GetEstimatedNumStages(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetEstimatedNumStages());
    return 1;
  }
  static int GetScripter(T* p, lua_State* L) {
    lua_pushstring(L, p->scripter_.c_str());
    return 1;
  }
  static int GetDescription(T* p, lua_State* L) {
    lua_pushstring(L, p->description_.c_str());
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
  DEFINE_METHOD(GetGoalSeconds, goal_seconds_)
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
