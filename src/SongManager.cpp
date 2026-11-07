#include "SongManager.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ActorUtil.h"
#include "BackgroundUtil.h"
#include "Course.h"
#include "CourseLoaderCRS.h"
#include "CourseUtil.h"
#include "Difficulty.h"
#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "GameManager.h"
#include "GameState.h"
#include "Group.h"
#include "ImageCache.h"
#include "LocalizedString.h"
#include "LuaManager.h"
#include "MemoryCardManager.h"
#include "MsdFile.h"
#include "NotesLoaderDWI.h"
#include "NotesLoaderSM.h"
#include "NotesLoaderSSC.h"
#include "PlayerNumber.h"
#include "Preference.h"
#include "PrefsManager.h"
#include "Profile.h"
#include "ProfileManager.h"
#include "RageFileManager.h"
#include "RageLog.h"
#include "RageTextureID.h"
#include "RageTexturePreloader.h"
#include "RageThreads.h"
#include "RageTimer.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "RageUtil/RandomNumbers.h"
#include "Song.h"
#include "SongCacheIndex.h"
#include "SongUtil.h"
#include "SpecialFiles.h"
#include "Sprite.h"
#include "StatsManager.h"
#include "StdString.h"
#include "Steps.h"
#include "StepsUtil.h"
#include "Style.h"
#include "ThemeManager.h"
#include "ThemeMetric.h"
#include "TitleSubstitution.h"
#include "TrailUtil.h"
#include "UnlockManager.h"
#include "arch/LoadingWindow/LoadingWindow.h"
#include "global.h"

SongManager* SONGMAN =
    nullptr;  // global and accessible from anywhere in our program

/** @brief The file that contains various random attacks. */
const std::string ATTACK_FILE = "/Data/RandomAttacks.txt";
const std::string EDIT_SUBDIR = "Edits/";

static const ThemeMetric<RageColor> EXTRA_COLOR("SongManager", "ExtraColor");
static const ThemeMetric<int> EXTRA_COLOR_METER(
    "SongManager", "ExtraColorMeter");
static const ThemeMetric<bool> USE_PREFERRED_SORT_COLOR(
    "SongManager", "UsePreferredSortColor");
static const ThemeMetric<bool> USE_UNLOCK_COLOR(
    "SongManager", "UseUnlockColor");
static const ThemeMetric<RageColor> UNLOCK_COLOR("SongManager", "UnlockColor");
static const ThemeMetric<bool> MOVE_UNLOCKS_TO_BOTTOM_OF_PREFERRED_SORT(
    "SongManager", "MoveUnlocksToBottomOfPreferredSort");
static const ThemeMetric<int> EXTRA_STAGE2_DIFFICULTY_MAX(
    "SongManager", "ExtraStage2DifficultyMax");

static Preference<std::string> g_sDisabledSongs("DisabledSongs", "");
static Preference<bool> g_bHideIncompleteCourses(
    "HideIncompleteCourses", false);

std::string SONG_GROUP_COLOR_NAME(size_t i) {
  return ssprintf("SongGroupColor%i", (int)i + 1);
}
std::string COURSE_GROUP_COLOR_NAME(size_t i) {
  return ssprintf("CourseGroupColor%i", (int)i + 1);
}
std::string PROFILE_SONG_GROUP_COLOR_NAME(size_t i) {
  return ssprintf("ProfileSongGroupColor%i", (int)i + 1);
}

static const float next_loading_window_update = 0.02f;

// The fields we need to load each song. We create an entry for each song in
// every pack so the sGroupDirNameField is duplicated for all songs in a pack.
// There are options for reducing the duplication if that is important !
struct SongToLoad {
  std::string group_dir_name_;
  std::string song_dir_name_;
};

// Stores the result of loading a song, whether it succeeded or failed and the
// pointer to the song if it did succeed.
struct SongLoadResult {
  enum State { STATE_UNLOADED, STATE_LOAD_SUCCEEDED, STATE_LOAD_FAILED };
  std::atomic<State> state_ = SongLoadResult::State::STATE_UNLOADED;
  Song* song_ = NULL;
};

SongManager::SongManager() {
  // Register with Lua.
  {
    Lua* L = LUA->Get();
    lua_pushstring(L, "SONGMAN");
    this->PushSelf(L);
    lua_settable(L, LUA_GLOBALSINDEX);
    LUA->Release(L);
  }

  NUM_SONG_GROUP_COLORS.Load("SongManager", "NumSongGroupColors");
  SONG_GROUP_COLOR.Load(
      "SongManager", SONG_GROUP_COLOR_NAME, NUM_SONG_GROUP_COLORS);
  NUM_COURSE_GROUP_COLORS.Load("SongManager", "NumCourseGroupColors");
  COURSE_GROUP_COLOR.Load(
      "SongManager", COURSE_GROUP_COLOR_NAME, NUM_COURSE_GROUP_COLORS);
  NUM_PROFILE_SONG_GROUP_COLORS.Load(
      "SongManager", "NumProfileSongGroupColors");
  PROFILE_SONG_GROUP_COLORS.Load(
      "SongManager", PROFILE_SONG_GROUP_COLOR_NAME,
      NUM_PROFILE_SONG_GROUP_COLORS);
}

SongManager::~SongManager() {
  // Unregister with Lua.
  LUA->UnsetGlobal("SONGMAN");

  // Courses depend on Songs and Songs don't depend on Courses.
  // So, delete the Courses first.
  FreeCourses();
  FreeSongs();
}

void SongManager::InitAll(LoadingWindow* ld, bool only_additions) {
  std::vector<std::string> never_cache;
  split(PREFSMAN->never_cache_list_, ",", never_cache);
  for (std::vector<std::string>::iterator group = never_cache.begin();
       group != never_cache.end(); ++group) {
    groups_to_never_cache_.insert(*group);
  }
  InitSongsFromDisk(ld, only_additions);
  InitCoursesFromDisk(ld, only_additions);
  if (only_additions) {
    DeleteAutogenCourses();
  }
  InitAutogenCourses();
  InitRandomAttacks();
}

static LocalizedString RELOADING("SongManager", "Reloading...");
static LocalizedString UNLOADING_SONGS("SongManager", "Unloading songs...");
static LocalizedString UNLOADING_COURSES("SongManager", "Unloading courses...");
static LocalizedString SANITY_CHECKING_GROUPS(
    "SongManager", "Sanity checking groups...");

void SongManager::Reload(bool allow_fast_load, LoadingWindow* ld) {
  FILEMAN->FlushDirCache(SpecialFiles::SONGS_DIR);
  FILEMAN->FlushDirCache(SpecialFiles::COURSES_DIR);
  FILEMAN->FlushDirCache(EDIT_SUBDIR);

  if (ld) {
    ld->SetText(RELOADING);
  }

  // save scores before unloading songs, or the scores will be lost
  PROFILEMAN->SaveMachineProfile();
  GAMESTATE->SavePlayerProfiles();

  std::vector<std::tuple<PlayerNumber, std::string, bool>> rejoin_players;
  FOREACH_HumanPlayer(pn) {
    if (GAMESTATE->side_is_joined_[pn]) {
      if (PROFILEMAN->ProfileWasLoadedFromMemoryCard(pn)) {
        rejoin_players.push_back(std::make_tuple(pn, std::string(""), true));
      } else if (PROFILEMAN->IsPersistentProfile(pn)) {
        int num_local_profiles = PROFILEMAN->GetNumLocalProfiles();
        for (int i = 0; i < num_local_profiles; i++) {
          Profile* profile = PROFILEMAN->GetLocalProfileFromIndex(i);
          if (profile->guid_ == PROFILEMAN->GetProfile(pn)->guid_) {
            std::string profile_id = PROFILEMAN->GetLocalProfileIDFromIndex(i);
            rejoin_players.push_back(std::make_tuple(pn, profile_id, false));
            break;
          }
        }
      } else {
        rejoin_players.push_back(std::make_tuple(pn, std::string(""), false));
      }
      GAMESTATE->UnjoinPlayer(pn);
      PROFILEMAN->UnloadProfile(pn);
    }
  }

  if (ld) {
    ld->SetText(UNLOADING_COURSES);
  }

  FreeCourses();

  if (ld) {
    ld->SetText(UNLOADING_SONGS);
  }

  FreeSongs();

  const bool old_val = PREFSMAN->fast_load_;
  if (!allow_fast_load) {
    PREFSMAN->fast_load_.Set(false);
  }

  InitAll(ld, /*onlyAdditions=*/false);

  // reload scores and unlocks afterward
  PROFILEMAN->LoadMachineProfile();
  MEMCARDMAN->WaitForCheckingToComplete();
  for (auto& it : rejoin_players) {
    PlayerNumber pn;
    std::string profile_id;
    bool is_memory_card;
    std::tie(pn, profile_id, is_memory_card) = it;

    GAMESTATE->JoinPlayer(pn);
    if (is_memory_card) {
      bool success = MEMCARDMAN->MountCard(pn);
      if (success) {
        PROFILEMAN->LoadProfileFromMemoryCard(pn, true);
        MEMCARDMAN->UnmountCard(pn);
      }
    } else {
      PROFILEMAN->default_local_profile_id_[pn].Set(profile_id);
      PROFILEMAN->LoadLocalProfileFromMachine(pn);
    }
    GAMESTATE->LoadCurrentSettingsFromProfile(pn);
  }
  UNLOCKMAN->Reload();

  if (!allow_fast_load) {
    PREFSMAN->fast_load_.Set(old_val);
  }

  UpdatePreferredSort();
}

void SongManager::LoadAdditions(LoadingWindow* ld) {
  FILEMAN->FlushDirCache(SpecialFiles::SONGS_DIR);
  FILEMAN->FlushDirCache(SpecialFiles::COURSES_DIR);
  FILEMAN->FlushDirCache(EDIT_SUBDIR);

  InitAll(ld, /*onlyAdditions=*/true);

  UNLOCKMAN->Reload();

  UpdatePreferredSort();
}

void SongManager::InitSongsFromDisk(LoadingWindow* ld, bool only_additions) {
  RageTimer tm;
  // Tell SONGINDEX to not write the cache index file every time a song adds
  // an entry. -Kyz
  SONGINDEX->delay_save_cache_ = true;
  IMAGECACHE->delay_save_cache_ = true;
  LoadSongDir(SpecialFiles::SONGS_DIR, ld, only_additions);
  LoadEnabledSongsFromPref();
  SONGINDEX->SaveCacheIndex();
  SONGINDEX->delay_save_cache_ = false;
  IMAGECACHE->WriteToDisk();
  IMAGECACHE->delay_save_cache_ = false;

  LOG->Trace(
      "Found %d songs in %f seconds.", (int)songs_.size(), tm.GetDeltaTime());
}

static LocalizedString FOLDER_CONTAINS_MUSIC_FILES(
    "SongManager",
    "The folder \"%s\" appears to be a song folder.  All song folders must "
    "reside in a group folder.  For example, \"Songs/Originals/My Song\".");
bool SongManager::SanityCheckGroupDir(std::string dir) const {
  // Check to see if they put a song directly inside the group folder.
  std::vector<std::string> array_files;
  GetDirListing(dir + "/*", array_files);
  const std::vector<std::string>& audio_exts =
      ActorUtil::GetTypeExtensionList(FT_Sound);
  for (std::string& fname : array_files) {
    const std::string ext = GetExtension(fname);
    for (const std::string& aud : audio_exts) {
      if (ext == aud) {
        LOG->Warn(FOLDER_CONTAINS_MUSIC_FILES.GetValue().c_str(), dir.c_str());
        return false;
      }
    }
  }
  return true;
}

void SongManager::AddGroup(
    std::string dir, std::string group_dir_name, Group* group) {
  if (group == nullptr) {
    // Could not AddGroup 'sGroupDirName'. Group object is null.
    LOG->Warn(
        "Could not AddGroup '%s'. Group object is null.",
        group_dir_name.c_str());
    return;
  }

  unsigned j;
  for (j = 0; j < song_group_names_.size(); ++j) {
    if (group_dir_name == song_group_names_[j]) {
      break;
    }
  }

  if (j != song_group_names_.size()) {
    return;  // the group is already added
  }

  std::string banner_path;

  // Look for a group banner in this group folder
  std::vector<std::string> array_group_banners;

  // First check if there is a banner provided in pack.ini
  if (!group->GetBannerPath().empty()) {
    GetDirListing(
        dir + group_dir_name + "/" + group->GetBannerPath(),
        array_group_banners);
  }
  GetDirListing(dir + group_dir_name + "/*.png", array_group_banners);
  GetDirListing(dir + group_dir_name + "/*.jpg", array_group_banners);
  GetDirListing(dir + group_dir_name + "/*.jpeg", array_group_banners);
  GetDirListing(dir + group_dir_name + "/*.gif", array_group_banners);
  GetDirListing(dir + group_dir_name + "/*.bmp", array_group_banners);

  if (!array_group_banners.empty()) {
    banner_path = dir + group_dir_name + "/" + array_group_banners[0];
  } else {
    // Look for a group banner in the parent folder
    GetDirListing(dir + group_dir_name + ".png", array_group_banners);
    GetDirListing(dir + group_dir_name + ".jpg", array_group_banners);
    GetDirListing(dir + group_dir_name + ".jpeg", array_group_banners);
    GetDirListing(dir + group_dir_name + ".gif", array_group_banners);
    GetDirListing(dir + group_dir_name + ".bmp", array_group_banners);
    if (!array_group_banners.empty()) {
      banner_path = dir + array_group_banners[0];
    }
  }

  /* Other group graphics are a bit trickier, and usually don't exist.
   * A themer has a few options, namely checking the aspect ratio and
   * operating on it. -aj
   * TODO: Once the files are implemented in Song, bring the extensions
   * from there into here. -aj */
  // Group background

  // vector<std::string> arrayGroupBackgrounds;
  // GetDirListing(sDir + sGroupDirName + "/*-bg.png", arrayGroupBanners);
  // GetDirListing(sDir + sGroupDirName + "/*-bg.jpg", arrayGroupBanners);
  // GetDirListing(sDir + sGroupDirName + "/*-bg.jpeg", arrayGroupBanners);
  // GetDirListing(sDir + sGroupDirName + "/*-bg.gif", arrayGroupBanners);
  // GetDirListing(sDir + sGroupDirName + "/*-bg.bmp", arrayGroupBanners);
  /*
      std::string sBackgroundPath;
      if(!arrayGroupBackgrounds.empty())
          sBackgroundPath = sDir + sGroupDirName + "/" +
     arrayGroupBackgrounds[0]; else
      {
          // Look for a group background in the parent folder
          GetDirListing(sDir + sGroupDirName + "-bg.png",
     arrayGroupBackgrounds); GetDirListing(sDir + sGroupDirName + "-bg.jpg",
     arrayGroupBackgrounds); GetDirListing(sDir + sGroupDirName + "-bg.jpeg",
     arrayGroupBackgrounds); GetDirListing(sDir + sGroupDirName + "-bg.gif",
     arrayGroupBackgrounds); GetDirListing(sDir + sGroupDirName + "-bg.bmp",
     arrayGroupBackgrounds); if(!arrayGroupBackgrounds.empty()) sBackgroundPath
     = sDir + arrayGroupBackgrounds[0];
      }
  */
  song_group_banner_paths_.push_back(banner_path);
  song_group_names_.push_back(group_dir_name);

  // Add the group to its series. Resolve the first SeriesBanner we find
  // via GetDirListing if none of the groups define one we fall back to a group
  // banner at lookup time.
  if (!group->GetSeries().empty()) {
    Series& series = map_name_to_series_[group->GetSeries()];
    if (series.name_.empty()) {
      series.name_ = group->GetSeries();
    }
    series.groups_.insert(group);
    if (series.banner_path_.empty() && !group->GetSeriesBannerPath().empty()) {
      std::vector<std::string> array_series_banners;
      GetDirListing(
          dir + group_dir_name + "/" + group->GetSeriesBannerPath(),
          array_series_banners, false, true);
      if (!array_series_banners.empty()) {
        series.banner_path_ = array_series_banners[0];
      }
    }
  }
}

static LocalizedString LOADING_SONGS("SongManager", "Loading songs...");
void SongManager::LoadSongDir(
    std::string dir, LoadingWindow* ld, bool only_additions) {
  if (ld) {
    ld->SetText(LOADING_SONGS);
  }

  // Compositors and other stuff can impose some overhead on updating the
  // loading window, which slows down startup time for some people.
  // loading_window_last_update_time provides a timer so the loading window
  // isn't updated after every song and course. -Kyz
  RageTimer loading_window_last_update_time;
  loading_window_last_update_time.Touch();
  // Make sure sDir has a trailing slash.
  if (Right(dir, 1) != "/") {
    dir += "/";
  }

  // Find all group directories in "Songs" folder
  std::vector<std::string> array_group_dirs;
  GetDirListing(dir + "*", array_group_dirs, true);
  SortRStringArray(array_group_dirs);
  StripCvsAndSvn(array_group_dirs);
  StripMacResourceForks(array_group_dirs);

  std::map<std::string, std::vector<std::string>> map_group_song_dirs;
  int group_index, song_count;

  group_index = 0;
  song_count = 0;
  if (ld) {
    ld->SetIndeterminate(false);
    ld->SetTotalWork(array_group_dirs.size());
  }
  int sanity_index = 0;
  for (const std::string& group_dir_name :
       array_group_dirs)  // foreach dir in /Songs/
  {
    if (ld &&
        loading_window_last_update_time.Ago() > next_loading_window_update) {
      loading_window_last_update_time.Touch();
      ld->SetProgress(sanity_index);
      ld->SetText(
          SANITY_CHECKING_GROUPS.GetValue() +
          ssprintf("\n%s", Basename(group_dir_name).c_str()));
    }

    if (SanityCheckGroupDir(dir + group_dir_name)) {
      // Find all Song folders in this group directory
      std::vector<std::string> array_song_dirs;
      GetDirListing(dir + group_dir_name + "/*", array_song_dirs, true, true);
      StripCvsAndSvn(array_song_dirs);
      StripMacResourceForks(array_song_dirs);
      SortRStringArray(array_song_dirs);

      map_group_song_dirs[group_dir_name] = array_song_dirs;
      song_count += array_song_dirs.size();
    }
  }

  if (song_count == 0) {
    return;
  }

  if (ld) {
    ld->SetIndeterminate(false);
    ld->SetTotalWork(song_count);
  }

  int thread_count = std::thread::hardware_concurrency();
  if (thread_count <= 0) {
    thread_count = 1;
  }
  LOG->Trace("Loading songs using %d threads", thread_count);

  // We need to keep track of previously loaded groups so we don't delete them
  // if we're only loading additions.
  std::unordered_set<Group*> already_loaded_groups;

  // An entry for each song that we need to load across all packs.
  std::vector<SongToLoad> songs_to_load{};
  for (const auto& [group_dir_name, array_song_dirs] :
       map_group_song_dirs)  // foreach dir in /Songs/
  {
    LOG->Trace(
        "Attempting to load %i songs from \"%s\"", int(array_song_dirs.size()),
        (dir + group_dir_name).c_str());

    for (unsigned j = 0; j < array_song_dirs.size(); ++j)  // for each song dir
    {
      // Skip already loaded songs if onlyAdditions is set.
      if (only_additions) {
        SongID song_id;
        song_id.FromString(array_song_dirs[j]);
        if (song_id.ToSong() != nullptr) {
          continue;
        }
      }

      songs_to_load.push_back(SongToLoad{group_dir_name, array_song_dirs[j]});
    }

    const auto group_iter = map_name_to_group_.find(group_dir_name);
    if (group_iter != map_name_to_group_.end()) {
      already_loaded_groups.insert(group_iter->second);
    } else {
      map_name_to_group_[group_dir_name] = new Group(dir, group_dir_name);
    }
  }

  // Holds the index of the next unloaded song for a thread to claim.
  std::atomic<int> next_song_to_load_index = 0;

  // Create an array for the results of the songs to load. Pre-allocating it
  // helps us avoid needing to coordinate threads in 2 ways:
  // - No memory re-allocations happen as songs are completed and entries are
  // updated, which
  //   otherwise could introduce problems with threads having stale pointers.
  // - Threads atomically claim an index of a song to load, so when they update
  // that entry after
  //   loading a song, they know that no other thread will also update that
  //   entry.
  std::unique_ptr<SongLoadResult[]> loaded_songs(
      new SongLoadResult[songs_to_load.size()]);

  LOG->Trace(
      "Loading all %d songs across %d groups across %d threads",
      (int)(songs_to_load.size()), (int)(map_group_song_dirs.size()),
      thread_count);

  // Create the pool of threads that will load songs.
  std::vector<std::thread> threads{};
  threads.reserve(thread_count);

  // Define the work done in each thread.
  auto load_song_fn = [&songs_to_load, &next_song_to_load_index,
                       &loaded_songs]() {
    while (true) {
      // Atomically grab the index of the next song to load and increment it.
      // This means that each thread gets a uniquely incremented value as they
      // load songs and move on to the next.
      int song_to_load_index = next_song_to_load_index.fetch_add(1);
      if (song_to_load_index >= (int)(songs_to_load.size())) {
        // Exit the thread when there are not more songs to load.
        return;
      }

      SongToLoad& song_to_load = songs_to_load[song_to_load_index];

      // Is the LOG framework thread safe ?
      LOG->Trace("Loading %s", song_to_load.song_dir_name_.c_str());

      Song* new_song = new Song;
      if (!new_song->LoadFromSongDir(song_to_load.song_dir_name_)) {
        // The song failed to load.
        delete new_song;
        loaded_songs[song_to_load_index].state_.store(
            SongLoadResult::State::STATE_LOAD_FAILED,
            std::memory_order_release);
      } else {
        // We intentionally set the song pointer before updating the state so
        // that when the main thread checks the state, the song will be
        // populated. This may not be the case if we set the song pointer
        // afterwards.
        //
        // Alternatively, if we find this has the potential to create a race
        // condition, we could consider making this operation of setting both
        // fields a single atomic operation.
        loaded_songs[song_to_load_index].song_ = new_song;
        loaded_songs[song_to_load_index].state_.store(
            SongLoadResult::State::STATE_LOAD_SUCCEEDED,
            std::memory_order_release);
      }
    }
  };

  // Start the threads.
  for (int i = 0; i < thread_count; i++) {
    threads.emplace_back(load_song_fn);
  }

  // Here in the main thread, check songs sequentially for finishing loading.
  size_t loaded_songs_processed = 0;
  std::unordered_map<std::string, int> group_loaded_song_count;
  while (loaded_songs_processed < songs_to_load.size()) {
    SongLoadResult& loaded_song = loaded_songs[loaded_songs_processed];
    SongLoadResult::State state =
        loaded_song.state_.load(std::memory_order_acquire);

    if (state == SongLoadResult::State::STATE_LOAD_SUCCEEDED) {
      // These operations didn't appear thread safe to me which is why they are
      // in this post processing step in the main thread. Let me know if that
      // was an incorrect assumption !
      AddSongToList(loaded_song.song_);

      SongToLoad& song_to_load = songs_to_load[loaded_songs_processed];
      SongPointerVector& index_entry =
          map_song_group_index_[song_to_load.group_dir_name_];
      index_entry.push_back(loaded_song.song_);

      group_loaded_song_count[song_to_load.group_dir_name_]++;

      loaded_songs_processed++;

      // Update progress window with the song we've most recently loaded. Since
      // songs are loading in parallel but reported sequentially, this could
      // result in some jumpy behavior, althought the limiting on how often the
      // window is updated may improve that.
      if (ld &&
          loading_window_last_update_time.Ago() > next_loading_window_update) {
        std::string group_base_name = Basename(song_to_load.group_dir_name_);
        loading_window_last_update_time.Touch();
        ld->SetProgress(loaded_songs_processed);
        ld->SetText(
            LOADING_SONGS.GetValue() +
            ssprintf(
                "\n%s\n%s", group_base_name.c_str(),
                Basename(song_to_load.song_dir_name_).c_str()));
      }
    } else if (state == SongLoadResult::State::STATE_LOAD_FAILED) {
      // Even if a song fails to load, just move on.
      loaded_songs_processed++;
    } else {
      // Sleeping for a short time tells the CPU it can context switch to other
      // tasks for a bit.
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  // Wait for all of the threads to exit.
  for (int i = 0; i < thread_count; i++) {
    threads[i].join();
  }

  // Mostly unchanged group finalizing work.
  for (const auto& [group_dir_name, array_song_dirs] :
       map_group_song_dirs)  // foreach dir in /Songs/
  {
    Group* group = map_name_to_group_.at(group_dir_name);
    int loaded_song_count = group_loaded_song_count[group_dir_name];
    bool group_already_loaded =
        already_loaded_groups.find(group) != already_loaded_groups.end();

    LOG->Trace(
        "Loaded %d songs from \"%s\"", loaded_song_count,
        (dir + group_dir_name).c_str());

    // If we're only loading additions, already loaded groups should neither be
    // added nor deleted
    if (!(group_already_loaded && only_additions)) {
      // Don't add the group name if we didn't load any songs in this group.
      if (!loaded_song_count) {
        // Remove the group from the group mapping
        auto it = map_name_to_group_.find(group_dir_name);
        if (it != map_name_to_group_.end()) {
          map_name_to_group_.erase(it);
        }
        delete group;
        continue;
      }

      // Add this group to the group array.
      AddGroup(dir, group_dir_name, group);

      // Cache and load the group banner. (and background if it has one -aj)
      IMAGECACHE->CacheImage("Banner", GetSongGroupBannerPath(group_dir_name));
    }

    // Load the group sym links (if any)
    LoadGroupSymLinks(dir, group_dir_name);
  }

  if (ld) {
    ld->SetIndeterminate(true);
  }
}

// Instead of "symlinks", songs should have membership in multiple groups.
// -Chris
void SongManager::LoadGroupSymLinks(std::string dir, std::string group_folder) {
  // Find all symlink files in this folder
  std::vector<std::string> array_sym_links;
  GetDirListing(dir + group_folder + "/*.include", array_sym_links, false);
  SortRStringArray(array_sym_links);
  SongPointerVector& index_entry = map_song_group_index_[group_folder];
  for (unsigned s = 0; s < array_sym_links.size();
       s++)  // for each symlink in this dir, add it in as a song.
  {
    MsdFile msd_f;
    msd_f.ReadFile(
        dir + group_folder + "/" + array_sym_links[s].c_str(),
        false);  // don't unescape
    std::string sym_destination =
        msd_f.GetParam(0, 1);  // Should only be 1 value & param...period.

    Song* new_song = new Song;
    if (!new_song->LoadFromSongDir(sym_destination)) {
      delete new_song;  // The song failed to load.
    } else {
      const std::vector<Steps*>& steps = new_song->GetAllSteps();
      while (steps.size()) {
        new_song->DeleteSteps(steps[0]);
      }

      FOREACH_BackgroundLayer(i) new_song->GetBackgroundChanges(i).clear();

      new_song->group_name_ = group_folder;
      AddSongToList(new_song);
      index_entry.push_back(new_song);
    }
  }
}

void SongManager::PreloadSongImages() {
  if (PREFSMAN->image_cache_ != IMGCACHE_FULL) {
    return;
  }

  /* Load textures before unloading old ones, so we don't reload textures
   * that we don't need to. */
  RageTexturePreloader preload;

  const std::vector<Song*>& songs = GetAllSongs();
  for (unsigned i = 0; i < songs.size(); ++i) {
    if (!songs[i]->HasBanner()) {
      continue;
    }

    const RageTextureID id =
        Sprite::SongBannerTexture(songs[i]->GetBannerPath());
    preload.Load(id);
  }

  std::vector<Course*> courses;
  GetAllCourses(courses, false);
  for (unsigned i = 0; i < courses.size(); ++i) {
    if (!courses[i]->HasBanner()) {
      continue;
    }

    const RageTextureID id =
        Sprite::SongBannerTexture(courses[i]->GetBannerPath());
    preload.Load(id);
  }

  preload.Swap(texture_preload_);
}

void SongManager::FreeSongs() {
  song_group_names_.clear();
  song_group_banner_paths_.clear();
  map_songs_by_difficulty_.clear();
  map_preferred_section_to_songs_.clear();

  for (Song* song : songs_) {
    RageUtil::SafeDelete(song);
  }
  // Loop through all groups in the map and delete them.
  for (auto it = map_name_to_group_.begin(); it != map_name_to_group_.end();
       ++it) {
    Group* group = it->second;
    RageUtil::SafeDelete(group);
  }

  songs_.clear();
  map_name_to_group_.clear();
  map_name_to_series_.clear();
  songs_by_dir_.clear();

  // also free the songs that have been deleted from disk
  for (unsigned i = 0; i < deleted_songs_.size(); ++i) {
    RageUtil::SafeDelete(deleted_songs_[i]);
  }
  deleted_songs_.clear();

  map_song_group_index_.clear();

  popular_songs_.clear();
  shuffled_songs_.clear();
}

void SongManager::UnlistSong(Song* song) {
  // cannot immediately free song data, as it is needed temporarily for smooth
  // audio transitions, etc. Instead, remove it from the m_pSongs list and store
  // it in a special place where it can safely be deleted later.
  deleted_songs_.push_back(song);

  // remove all occurences of the song in each of our song vectors
  std::vector<Song*>* song_vectors[3] = {
      &songs_, &popular_songs_, &shuffled_songs_};
  for (int song_vec_idx = 0; song_vec_idx < 3; ++song_vec_idx) {
    std::vector<Song*>& v = *song_vectors[song_vec_idx];
    for (size_t i = 0; i < v.size(); ++i) {
      if (v[i] == song) {
        v.erase(v.begin() + i);
        --i;
      }
    }
  }
}

bool SongManager::IsGroupNeverCached(const std::string& group) const {
  return groups_to_never_cache_.find(group) != groups_to_never_cache_.end();
}

std::string SongManager::GetSongGroupBannerPath(std::string song_group) const {
  for (unsigned i = 0; i < song_group_names_.size(); ++i) {
    if (song_group == song_group_names_[i]) {
      return song_group_banner_paths_[i];
    }
  }

  return std::string();
}

std::string SongManager::GetSeriesBannerPath(std::string series_name) const {
  std::map<std::string, Series>::const_iterator it =
      map_name_to_series_.find(series_name);
  if (it == map_name_to_series_.end()) {
    return std::string();
  }
  if (!it->second.banner_path_.empty()) {
    return it->second.banner_path_;
  }

  // No group in the series defined a SeriesBanner: fall back to the
  // banner of the first group
  Group* first_group = nullptr;
  for (std::unordered_set<Group*>::const_iterator git =
           it->second.groups_.begin();
       git != it->second.groups_.end(); ++git) {
    if (first_group == nullptr ||
        (*git)->GetGroupName() < first_group->GetGroupName()) {
      first_group = *git;
    }
  }
  if (first_group != nullptr) {
    return GetSongGroupBannerPath(first_group->GetGroupName());
  }
  return std::string();
}

std::unordered_set<Group*> SongManager::GetGroupsInSeries(
    const std::string& series_name) const {
  std::map<std::string, Series>::const_iterator it =
      map_name_to_series_.find(series_name);
  if (it == map_name_to_series_.end()) {
    return std::unordered_set<Group*>();
  }
  return it->second.groups_;
}

void SongManager::GetSongGroupNames(std::vector<std::string>& add_to) const {
  add_to.insert(
      add_to.end(), song_group_names_.begin(), song_group_names_.end());
}

bool SongManager::DoesSongGroupExist(std::string song_group) const {
  return find(song_group_names_.begin(), song_group_names_.end(), song_group) !=
         song_group_names_.end();
}

RageColor SongManager::GetSongGroupColor(const std::string& song_group) const {
  for (unsigned i = 0; i < song_group_names_.size(); i++) {
    if (song_group_names_[i] == song_group) {
      return SONG_GROUP_COLOR.GetValue(i % NUM_SONG_GROUP_COLORS);
    }
  }
  FOREACH_EnabledPlayer(pn) {
    Profile* prof = PROFILEMAN->GetProfile(pn);
    if (prof != nullptr) {
      if (prof->IsCustomSongGroup(song_group)) {
        return PROFILE_SONG_GROUP_COLORS.GetValue(
            pn % NUM_PROFILE_SONG_GROUP_COLORS);
      }
    }
  }

  LuaHelpers::ReportScriptErrorFmt(
      "requested color for song group '%s' that doesn't exist",
      song_group.c_str());
  return RageColor(1, 1, 1, 1);
}

RageColor SongManager::GetSongColor(const Song* song) const {
  ASSERT(song != nullptr);

  // protected by royal freem corporation. any modification/removal of
  // this code will result in prosecution.
  if (song->main_title_ == "DVNO") {
    return RageColor(1.0f, 0.8f, 0.0f, 1.0f);
  }
  // end royal freem protection

  // Use unlock color if applicable
  const UnlockEntry* ue = UNLOCKMAN->FindSong(song);
  if (ue && USE_UNLOCK_COLOR.GetValue()) {
    return UNLOCK_COLOR.GetValue();
  }

  if (USE_PREFERRED_SORT_COLOR) {
    int sort_index = 0;
    for (const PreferredSortSection& v : preferred_song_sort_) {
      if (std::any_of(v.songs_.begin(), v.songs_.end(), [&](const Song* s) {
            return s == song;
          })) {
        return SONG_GROUP_COLOR.GetValue(sort_index % NUM_SONG_GROUP_COLORS);
      }

      sort_index += 1;
    }

    int i = preferred_song_sort_.size();
    return SONG_GROUP_COLOR.GetValue(i % NUM_SONG_GROUP_COLORS);
  } else  // TODO: Have a better fallback plan with colors?
  {
    /* XXX: Previously, this matched all notes, which set a song to "extra"
     * if it had any 10-foot steps at all, even edits or doubles.
     *
     * For now, only look at notes for the current note type. This means
     * that if a song has 10-foot steps on Doubles, it'll only show up red
     * in Doubles. That's not too bad, I think. This will also change it
     * in the song scroll, which is a little odd but harmless.
     *
     * XXX: Ack. This means this function can only be called when we have
     * a style set up, which is too restrictive. How to handle this? */
    // const StepsType st = GAMESTATE->GetCurrentStyle()->m_StepsType;
    const std::vector<Steps*>& vp_steps = song->GetAllSteps();
    for (unsigned i = 0; i < vp_steps.size(); i++) {
      const Steps* p_steps = vp_steps[i];
      switch (p_steps->GetDifficulty()) {
        case Difficulty_Challenge:
        case Difficulty_Edit:
          continue;
        default:
          break;
      }

      // if(pSteps->m_StepsType != st)
      //	continue;

      if (p_steps->GetMeter() >= EXTRA_COLOR_METER) {
        return (RageColor)EXTRA_COLOR;
      }
    }

    return GetSongGroupColor(song->group_name_);
  }
}

std::string SongManager::GetCourseGroupBannerPath(
    const std::string& course_group) const {
  std::map<std::string, CourseGroupInfo>::const_iterator iter =
      map_course_group_to_info_.find(course_group);
  if (iter == map_course_group_to_info_.end()) {
    ASSERT_M(
        0, ssprintf(
               "requested banner for course group '%s' that doesn't exist",
               course_group.c_str()));
    return std::string();
  } else {
    return iter->second.banner_path_;
  }
}

void SongManager::GetCourseGroupNames(std::vector<std::string>& add_to) const {
  for (const std::pair<const std::string, CourseGroupInfo>& iter :
       map_course_group_to_info_) {
    add_to.push_back(iter.first);
  }
}

bool SongManager::DoesCourseGroupExist(const std::string& course_group) const {
  return map_course_group_to_info_.find(course_group) !=
         map_course_group_to_info_.end();
}

RageColor SongManager::GetCourseGroupColor(
    const std::string& course_group) const {
  int index = 0;
  for (const std::pair<const std::string, CourseGroupInfo>& iter :
       map_course_group_to_info_) {
    if (iter.first == course_group) {
      return SONG_GROUP_COLOR.GetValue(index % NUM_SONG_GROUP_COLORS);
    }
    index++;
  }

  ASSERT_M(
      0, ssprintf(
             "requested color for course group '%s' that doesn't exist",
             course_group.c_str()));
  return RageColor(1, 1, 1, 1);
}

RageColor SongManager::GetCourseColor(const Course* course) const {
  // Use unlock color if applicable
  const UnlockEntry* ue = UNLOCKMAN->FindCourse(course);
  if (ue && USE_UNLOCK_COLOR.GetValue()) {
    return UNLOCK_COLOR.GetValue();
  }

  if (USE_PREFERRED_SORT_COLOR) {
    int course_index = 0;
    for (const CoursePointerVector& v : preferred_course_sort_) {
      if (std::any_of(v.begin(), v.end(), [&](const Course* s) {
            return s == course;
          })) {
        CHECKPOINT_M(ssprintf(
            "%i, NUM_COURSE_GROUP_COLORS = %i", course_index,
            NUM_COURSE_GROUP_COLORS.GetValue()));
        return COURSE_GROUP_COLOR.GetValue(
            course_index % NUM_COURSE_GROUP_COLORS);
      }
      course_index += 1;
    }

    int i = preferred_course_sort_.size();
    CHECKPOINT_M(ssprintf(
        "%i, NUM_COURSE_GROUP_COLORS = %i", i,
        NUM_COURSE_GROUP_COLORS.GetValue()));
    return COURSE_GROUP_COLOR.GetValue(i % NUM_COURSE_GROUP_COLORS);
  } else {
    return GetCourseGroupColor(course->group_name_);
  }
}

void SongManager::ResetGroupColors() {
  // Reload song/course group colors to prevent a crash when switching
  // themes in-game. (apparently not, though.) -aj
  SONG_GROUP_COLOR.Clear();
  COURSE_GROUP_COLOR.Clear();

  NUM_SONG_GROUP_COLORS.Load("SongManager", "NumSongGroupColors");
  SONG_GROUP_COLOR.Load(
      "SongManager", SONG_GROUP_COLOR_NAME, NUM_SONG_GROUP_COLORS);
  NUM_COURSE_GROUP_COLORS.Load("SongManager", "NumCourseGroupColors");
  COURSE_GROUP_COLOR.Load(
      "SongManager", COURSE_GROUP_COLOR_NAME, NUM_COURSE_GROUP_COLORS);
}

const std::vector<Song*>& SongManager::GetSongs(
    const std::string& group_name) const {
  static const std::vector<Song*> kEmpty;

  if (group_name == GROUP_ALL) {
    return songs_;
  }
  std::map<std::string, SongPointerVector, Comp>::const_iterator iter =
      map_song_group_index_.find(group_name);
  if (iter != map_song_group_index_.end()) {
    return iter->second;
  }
  FOREACH_EnabledPlayer(pn) {
    Profile* prof = PROFILEMAN->GetProfile(pn);
    if (prof != nullptr) {
      if (prof->IsCustomSongGroup(group_name)) {
        return prof->songs_groups_[group_name];
      }
    }
  }
  return kEmpty;
}

void SongManager::GetPreferredSortSongs(std::vector<Song*>& add_to) const {
  if (preferred_song_sort_.empty()) {
    add_to.insert(add_to.end(), songs_.begin(), songs_.end());
    return;
  }
  for (const PreferredSortSection& v : preferred_song_sort_) {
    add_to.insert(add_to.end(), v.songs_.begin(), v.songs_.end());
  }
}

std::string SongManager::SongToPreferredSortSectionName(
    const Song* song) const {
  for (const PreferredSortSection& v : preferred_song_sort_) {
    if (std::any_of(v.songs_.begin(), v.songs_.end(), [&](const Song* s) {
          return s == song;
        })) {
      return v.name_;
    }
  }
  return std::string();
}

void SongManager::GetPreferredSortSongsBySectionName(
    const std::string& section_name, std::vector<Song*>& add_to) const {
  // Use m_mapPreferredSectionToSongs
  std::map<std::string, SongPointerVector>::const_iterator iter =
      map_preferred_section_to_songs_.find(section_name);
  if (iter != map_preferred_section_to_songs_.end()) {
    add_to.insert(add_to.end(), iter->second.begin(), iter->second.end());
  }
}

Group* SongManager::GetGroup(const Song* song) const {
  if (song == nullptr) {
    return nullptr;
  }
  return GetGroupFromName(song->group_name_);
}

Group* SongManager::GetGroupFromName(const std::string& group_name) const {
  auto iter = map_name_to_group_.find(group_name);
  if (iter != map_name_to_group_.end()) {
    return iter->second;
  }
  FOREACH_EnabledPlayer(pn) {
    Profile* prof = PROFILEMAN->GetProfile(pn);
    if (prof != nullptr) {
      for (size_t i = 0; i < prof->groups_.size(); i++) {
        Group* grp = prof->groups_[i];

        if (grp->GetGroupName() == group_name) {
          return grp;
        }
      }
    }
  }
  return nullptr;
}

void SongManager::GetPreferredSortCourses(
    CourseType ct, std::vector<Course*>& add_to, bool include_autogen) const {
  if (preferred_course_sort_.empty()) {
    GetCourses(ct, add_to, include_autogen);
    return;
  }

  for (const CoursePointerVector& v : preferred_course_sort_) {
    for (Course* course : v) {
      if (course->GetCourseType() == ct) {
        add_to.push_back(course);
      }
    }
  }
}

int SongManager::GetNumSongs() const { return songs_.size(); }

int SongManager::GetNumLockedSongs() const {
  return std::count_if(songs_.begin(), songs_.end(), [](const Song* s) {
    return UNLOCKMAN->SongIsLocked(s);
  });
}

int SongManager::GetNumUnlockedSongs() const {
  return std::count_if(songs_.begin(), songs_.end(), [](const Song* s) {
    return UNLOCKMAN->SongIsLocked(s) & ~LOCKED_LOCK;
  });
}

int SongManager::GetNumSelectableAndUnlockedSongs() const {
  return std::count_if(songs_.begin(), songs_.end(), [](const Song* s) {
    return UNLOCKMAN->SongIsLocked(s) & ~(LOCKED_LOCK | LOCKED_SELECTABLE);
  });
}

int SongManager::GetNumSongGroups() const { return song_group_names_.size(); }

int SongManager::GetNumCourses() const { return courses_.size(); }

int SongManager::GetNumCourseGroups() const {
  return map_course_group_to_info_.size();
}

std::string SongManager::ShortenGroupName(std::string long_group_name) {
  static TitleSubst tsub("Groups");

  TitleFields title;
  title.title_ = long_group_name;
  tsub.Subst(title);
  return title.title_;
}

std::string SongManager::ShortenSeriesName(std::string long_series_name) {
  static TitleSubst tsub("Series");

  TitleFields title;
  title.title_ = long_series_name;
  tsub.Subst(title);
  return title.title_;
}

static LocalizedString LOADING_COURSES("SongManager", "Loading courses...");
void SongManager::InitCoursesFromDisk(LoadingWindow* ld, bool only_additions) {
  LOG->Trace("Loading courses.");
  if (ld) {
    ld->SetText(LOADING_COURSES);
  }

  RageTimer loading_window_last_update_time;
  loading_window_last_update_time.Touch();

  std::vector<std::string> course_group_names;
  // Find all group directories in Courses dir
  GetDirListing(
      SpecialFiles::COURSES_DIR + "*", course_group_names, true, true);
  StripCvsAndSvn(course_group_names);
  StripMacResourceForks(course_group_names);

  // Search for courses both in COURSES_DIR and in subdirectories
  course_group_names.push_back(SpecialFiles::COURSES_DIR);
  SortRStringArray(course_group_names);

  int course_index = 0;
  for (const std::string& course_group :
       course_group_names)  // for each dir in /Courses/
  {
    // Find all CRS files in this group directory
    std::vector<std::string> course_paths;
    GetDirListing(course_group + "/*.crs", course_paths, false, true);
    SortRStringArray(course_paths);

    if (ld) {
      ld->SetIndeterminate(false);
      ld->SetTotalWork(course_paths.size());
    }

    std::string base_course_group = Basename(course_group);
    for (const std::string& course_path : course_paths) {
      // Skip already loaded courses if onlyAdditions is set.
      if (only_additions) {
        CourseID course_id;
        course_id.FromPath(course_path);
        if (course_id.ToCourse() != nullptr) {
          continue;
        }
      }

      if (ld &&
          loading_window_last_update_time.Ago() > next_loading_window_update) {
        loading_window_last_update_time.Touch();
        ld->SetProgress(course_index);
        ld->SetText(
            LOADING_COURSES.GetValue() + ssprintf(
                                             "\n%s\n%s",
                                             base_course_group.c_str(),
                                             Basename(course_path).c_str()));
      }

      Course* course = new Course;
      CourseLoaderCRS::LoadFromCRSFile(course_path, *course);

      if (g_bHideIncompleteCourses.Get() && course->incomplete_) {
        delete course;
        continue;
      }

      courses_.push_back(course);
      course_index++;
    }
  }

  if (ld) {
    ld->SetIndeterminate(true);
  }

  RefreshCourseGroupInfo();
}

void SongManager::InitAutogenCourses() {
  // Create group courses for Endless and Nonstop
  std::vector<std::string> group_names;
  this->GetSongGroupNames(group_names);
  Course* course;
  for (unsigned g = 0; g < group_names.size(); g++)  // foreach Group
  {
    std::string group_name = group_names[g];

    // Generate random courses from each group.
    course = new Course;
    CourseUtil::AutogenEndlessFromGroup(group_name, Difficulty_Medium, *course);
    course->scripter_ = "Autogen";
    courses_.push_back(course);

    course = new Course;
    CourseUtil::AutogenNonstopFromGroup(group_name, Difficulty_Medium, *course);
    course->scripter_ = "Autogen";
    courses_.push_back(course);
  }

  // Generate "All Songs" endless course.
  course = new Course;
  CourseUtil::AutogenEndlessFromGroup("", Difficulty_Medium, *course);
  course->scripter_ = "Autogen";
  courses_.push_back(course);

  /* Generate Oni courses from artists. Only create courses if we have at least
   * four songs from an artist; create 3- and 4-song courses. */
  {
    /* We normally sort by translit artist. However, display artist is more
     * consistent. For example, transliterated Japanese names are alternately
     * spelled given- and family-name first, but display titles are more
     * consistent. */
    std::vector<Song*> ap_songs = this->GetAllSongs();
    SongUtil::SortSongPointerArrayByDisplayArtist(ap_songs);

    std::string cur_artist = "";
    std::string cur_artist_translit = "";
    int cur_artist_count = 0;

    std::vector<Song*> a_songs;
    unsigned i = 0;
    do {
      std::string artist = i >= ap_songs.size()
                               ? std::string("")
                               : ap_songs[i]->GetDisplayArtist();
      std::string translit_artist = i >= ap_songs.size()
                                        ? std::string("")
                                        : ap_songs[i]->GetTranslitArtist();
      if (i < ap_songs.size() && !CompareNoCase(cur_artist, artist)) {
        a_songs.push_back(ap_songs[i]);
        ++cur_artist_count;
        continue;
      }

      /* Different artist, or we're at the end. If we have enough entries for
       * the last artist, add it. Skip blanks and "Unknown artist". */
      if (cur_artist_count >= 3 && cur_artist_translit != "" &&
          CompareNoCase(cur_artist_translit, "Unknown artist") &&
          CompareNoCase(cur_artist, "Unknown artist")) {
        course = new Course;
        CourseUtil::AutogenOniFromArtist(
            cur_artist, cur_artist_translit, a_songs, Difficulty_Hard, *course);
        course->scripter_ = "Autogen";
        courses_.push_back(course);
      }

      a_songs.clear();

      if (i < ap_songs.size()) {
        cur_artist = artist;
        cur_artist_translit = translit_artist;
        cur_artist_count = 1;
        a_songs.push_back(ap_songs[i]);
      }
    } while (i++ < ap_songs.size());
  }
}

void SongManager::InitRandomAttacks() {
  GAMESTATE->random_attacks_.clear();

  if (!IsAFile(ATTACK_FILE)) {
    LOG->Trace("File Data/RandomAttacks.txt was not found");
  } else {
    MsdFile msd;

    if (!msd.ReadFile(ATTACK_FILE, true)) {
      LuaHelpers::ReportScriptErrorFmt(
          "Error opening file '%s' for reading: %s.", ATTACK_FILE.c_str(),
          msd.GetError().c_str());
    } else {
      for (unsigned i = 0; i < msd.GetNumValues(); i++) {
        int num_params = msd.GetNumParams(i);
        const MsdFile::value_t& params = msd.GetValue(i);
        std::string type = params[0];
        std::string attack = params[1];

        if (num_params > 2) {
          LuaHelpers::ReportScriptErrorFmt(
              "Got \"%s:%s\" tag with too many parameters", type.c_str(),
              attack.c_str());
          continue;
        }

        if (!EqualsNoCase(type, "ATTACK")) {
          LuaHelpers::ReportScriptErrorFmt(
              "Got \"%s:%s\" tag with wrong declaration", type.c_str(),
              attack.c_str());
          continue;
        }

        GAMESTATE->random_attacks_.push_back(attack);
      }
    }
  }
}

void SongManager::FreeCourses() {
  for (unsigned i = 0; i < courses_.size(); i++) {
    delete courses_[i];
  }
  courses_.clear();

  FOREACH_CourseType(ct) popular_courses_[ct].clear();
  shuffled_courses_.clear();

  map_course_group_to_info_.clear();
}

void SongManager::DeleteAutogenCourses() {
  std::vector<Course*> new_courses;
  for (std::vector<Course*>::iterator it = courses_.begin();
       it != courses_.end(); ++it) {
    if ((*it)->is_autogen_) {
      delete *it;
    } else {
      new_courses.push_back(*it);
    }
  }
  courses_.swap(new_courses);
  UpdatePopular();
  UpdateShuffled();
  RefreshCourseGroupInfo();
}

void SongManager::AddCourse(Course* course) {
  courses_.push_back(course);
  UpdatePopular();
  UpdateShuffled();
  map_course_group_to_info_[course->group_name_];  // insert
}

void SongManager::DeleteCourse(Course* course) {
  std::vector<Course*>::iterator iter =
      find(courses_.begin(), courses_.end(), course);
  ASSERT(iter != courses_.end());
  courses_.erase(iter);
  UpdatePopular();
  UpdateShuffled();
  RefreshCourseGroupInfo();
}

void SongManager::InvalidateCachedTrails() {
  for (Course* course : courses_) {
    if (course->IsAnEdit()) {
      course->trail_cache_.clear();
    }
  }
}

/* Called periodically to wipe out cached NoteData. This is called when we
 * change screens. */
void SongManager::Cleanup() {
  for (Song* song : shuffled_songs_) {
    if (song) {
      const std::vector<Steps*>& vp_steps = song->GetAllSteps();
      for (Steps* p_steps : vp_steps) {
        p_steps->Compress();
      }
    }
  }
}

/* Flush all Song*, Steps* and Course* caches. This is when a Song or its Steps
 * are removed or changed. This doesn't touch GAMESTATE and StageStats
 * pointers. Currently, the only time Steps are altered independently of the
 * Courses and Songs is in Edit Mode, which updates the other pointers it needs.
 */
void SongManager::Invalidate(const Song* stale_song) {
  // TODO: This is unnecessarily expensive.
  // Can we regenerate only the autogen courses that are affected?
  DeleteAutogenCourses();

  for (Course* c : this->courses_) {
    c->Invalidate(stale_song);
  }

  InitAutogenCourses();

  UpdatePopular();
  UpdateShuffled();
  RefreshCourseGroupInfo();
}

void SongManager::RegenerateNonFixedCourses() {
  for (unsigned i = 0; i < courses_.size(); i++) {
    courses_[i]->RegenerateNonFixedTrails();
  }
}

void SongManager::SetPreferences() {
  for (unsigned int i = 0; i < songs_.size(); i++) {
    // PREFSMAN->m_bAutogenSteps may have changed.
    songs_[i]->RemoveAutoGenNotes();
    songs_[i]->AddAutoGenNotes();
  }
}

void SongManager::SaveEnabledSongsToPref() {
  std::vector<std::string> disabled_songs;

  // Intentionally drop disabled song entries for songs that aren't currently
  // loaded.

  const std::vector<Song*>& songs = SONGMAN->GetAllSongs();
  for (Song* song : songs) {
    SongID sid;
    sid.FromSong(song);
    if (!song->GetEnabled()) {
      disabled_songs.push_back(sid.ToString());
    }
  }
  g_sDisabledSongs.Set(join(";", disabled_songs));
}

void SongManager::LoadEnabledSongsFromPref() {
  std::vector<std::string> disabled_songs;
  split(g_sDisabledSongs, ";", disabled_songs, true);

  for (const std::string& s : disabled_songs) {
    SongID sid;
    sid.FromString(s);
    Song* song = sid.ToSong();
    if (song) {
      song->SetEnabled(false);
    }
  }
}

void SongManager::GetStepsLoadedFromProfile(
    std::vector<Steps*>& add_to, ProfileSlot slot) const {
  const std::vector<Song*>& songs = GetAllSongs();
  for (Song* song : songs) {
    song->GetStepsLoadedFromProfile(slot, add_to);
  }
}

void SongManager::DeleteSteps(Steps* steps) {
  steps->song_->DeleteSteps(steps);
}

void SongManager::GetAllCourses(
    std::vector<Course*>& add_to, bool include_autogen) const {
  for (unsigned i = 0; i < courses_.size(); i++) {
    if (include_autogen || !courses_[i]->is_autogen_) {
      add_to.push_back(courses_[i]);
    }
  }
}

void SongManager::GetCourses(
    CourseType ct, std::vector<Course*>& add_to, bool include_autogen) const {
  for (unsigned i = 0; i < courses_.size(); i++) {
    if (courses_[i]->GetCourseType() == ct) {
      if (include_autogen || !courses_[i]->is_autogen_) {
        add_to.push_back(courses_[i]);
      }
    }
  }
}

void SongManager::GetCoursesInGroup(
    std::vector<Course*>& add_to, const std::string& course_group,
    bool include_autogen) const {
  for (unsigned i = 0; i < courses_.size(); i++) {
    if (courses_[i]->group_name_ == course_group) {
      if (include_autogen || !courses_[i]->is_autogen_) {
        add_to.push_back(courses_[i]);
      }
    }
  }
}

bool SongManager::GetExtraStageInfoFromCourse(
    bool extra2, std::string preferred_group, Song*& song_out,
    Steps*& steps_out, StepsType stype) {
  const std::string course_suffix =
      preferred_group + (extra2 ? "/extra2.crs" : "/extra1.crs");
  std::string course_path = SpecialFiles::SONGS_DIR + course_suffix;

  Course course;
  CourseLoaderCRS::LoadFromCRSFile(course_path, course);
  if (course.GetEstimatedNumStages() <= 0) {
    return false;
  }

  Trail* trail = course.GetTrail(stype);
  if (trail->entries_.empty()) {
    return false;
  }

  song_out = trail->entries_[0].song_;
  steps_out = trail->entries_[0].steps_;
  return true;
}

// Return true if n1 < n2.
bool CompareNotesPointersForExtra(const Steps* n1, const Steps* n2) {
  // Equate CHALLENGE to HARD.
  Difficulty d1 = std::min(n1->GetDifficulty(), Difficulty_Hard);
  Difficulty d2 = std::min(n2->GetDifficulty(), Difficulty_Hard);

  if (d1 < d2) {
    return true;
  }
  if (d1 > d2) {
    return false;
  }
  // n1 difficulty == n2 difficulty

  if (StepsUtil::CompareNotesPointersByMeter(n1, n2)) {
    return true;
  }
  if (StepsUtil::CompareNotesPointersByMeter(n2, n1)) {
    return false;
  }
  // n1 meter == n2 meter

  return StepsUtil::CompareNotesPointersByRadarValues(n1, n2);
}

void SongManager::GetExtraStageInfo(
    bool extra2, const Style* sd, Song*& song_out, Steps*& steps_out) {
  std::string group = GAMESTATE->preferred_song_group_;
  if (group == GROUP_ALL) {
    if (GAMESTATE->cur_song_ == nullptr) {
      // This normally shouldn't happen, but it's helpful to permit it for
      // testing.
      LuaHelpers::ReportScriptErrorFmt(
          "GetExtraStageInfo() called in GROUP_ALL, but GAMESTATE->cur_song_ "
          "== nullptr");
      GAMESTATE->cur_song_.Set(GetRandomSong());
    }
    group = GAMESTATE->cur_song_->group_name_;
  }

  ASSERT_M(
      group != "",
      ssprintf(
          "%p '%s' '%s'", static_cast<void*>(GAMESTATE->cur_song_.Get()),
          GAMESTATE->cur_song_ ? GAMESTATE->cur_song_->GetSongDir().c_str()
                               : "",
          GAMESTATE->cur_song_ ? GAMESTATE->cur_song_->group_name_.c_str()
                               : ""));

  // Check preferred group
  if (GetExtraStageInfoFromCourse(
          extra2, group, song_out, steps_out, sd->steps_type_)) {
    return;
  }

  // Optionally, check the Songs folder for extra1/2.crs files.
  if (GetExtraStageInfoFromCourse(
          extra2, "", song_out, steps_out, sd->steps_type_)) {
    return;
  }

  // Choose a hard song for the extra stage
  Song* extra1_song = nullptr;  // the absolute hardest Song and Steps.  Use
                                // this for extra stage 1.
  Steps* extra1_notes = nullptr;
  Song* extra2_song =
      nullptr;  // a medium-hard Song and Steps.  Use this for extra stage 2.
  Steps* extra2_notes = nullptr;

  const std::vector<Song*>& songs = GetSongs(group);
  for (unsigned s = 0; s < songs.size(); s++)  // foreach song
  {
    Song* song = songs[s];

    std::vector<Steps*> ap_steps;
    SongUtil::GetSteps(song, ap_steps, sd->steps_type_);
    for (unsigned n = 0; n < ap_steps.size(); n++)  // foreach Steps
    {
      Steps* p_steps = ap_steps[n];

      if (extra1_notes == nullptr ||
          CompareNotesPointersForExtra(
              extra1_notes, p_steps))  // pSteps is harder than pHardestNotes
      {
        extra1_song = song;
        extra1_notes = p_steps;
      }

      // for extra 2, we don't want to choose the hardest notes possible.  So,
      // we'll disgard Steps with meter > 8 (assuming dance)
      if (extra2 && p_steps->GetMeter() > EXTRA_STAGE2_DIFFICULTY_MAX) {
        continue;  // skip
      }
      if (extra2_notes == nullptr ||
          CompareNotesPointersForExtra(
              extra2_notes, p_steps))  // pSteps is harder than pHardestNotes
      {
        extra2_song = song;
        extra2_notes = p_steps;
      }
    }
  }

  if (extra2_song == nullptr && extra1_song != nullptr) {
    extra2_song = extra1_song;
    extra2_notes = extra1_notes;
  }

  // If there are any notes at all that match this StepsType, everything should
  // be filled out. Also, it's guaranteed that there is at least one Steps that
  // matches the StepsType because the player had to play something before
  // reaching the extra stage!
  ASSERT(extra2_song && extra1_song && extra2_notes && extra1_notes);

  song_out = (extra2 ? extra2_song : extra1_song);
  steps_out = (extra2 ? extra2_notes : extra1_notes);
}

Song* SongManager::GetRandomSong() {
  if (shuffled_songs_.empty()) {
    return nullptr;
  }

  static int i = 0;

  for (int throw_away = 0; throw_away < 100; throw_away++) {
    i++;
    wrap(i, shuffled_songs_.size());
    Song* song = shuffled_songs_[i];
    if (song->IsTutorial()) {
      continue;
    }
    if (!song->NormallyDisplayed()) {
      continue;
    }
    return song;
  }

  return nullptr;
}

Course* SongManager::GetRandomCourse() {
  if (shuffled_courses_.empty()) {
    return nullptr;
  }

  static int i = 0;

  for (int throw_away = 0; throw_away < 100; throw_away++) {
    i++;
    wrap(i, shuffled_courses_.size());
    Course* course = shuffled_courses_[i];
    if (course->is_autogen_ && !PREFSMAN->autogen_group_courses_) {
      continue;
    }
    if (course->GetCourseType() == COURSE_TYPE_ENDLESS) {
      continue;
    }
    if (UNLOCKMAN->CourseIsLocked(course)) {
      continue;
    }
    return course;
  }

  return nullptr;
}

Song* SongManager::GetSongFromDir(std::string dir) const {
  if (Right(dir, 1) != "/") {
    dir += "/";
  }

  Replace(dir, '\\', '/');
  MakeLower(dir);
  std::map<std::string, Song*>::const_iterator entry = songs_by_dir_.find(dir);
  if (entry != songs_by_dir_.end()) {
    return entry->second;
  }
  return nullptr;
}

Course* SongManager::GetCourseFromPath(std::string path) const {
  if (path == "") {
    return nullptr;
  }

  for (Course* c : courses_) {
    if (CompareNoCase(path, c->path_) == 0) {
      return c;
    }
  }

  return nullptr;
}

Course* SongManager::GetCourseFromName(std::string name) const {
  if (name == "") {
    return nullptr;
  }

  for (Course* c : courses_) {
    if (CompareNoCase(name, c->GetDisplayFullTitle()) == 0) {
      return c;
    }
  }

  return nullptr;
}

/* GetSongDir() contains a path to the song, possibly a full path, eg:
 * Songs\Group\SongName                   or
 * My Other Song Folder\Group\SongName    or
 * c:\Corny J-pop\Group\SongName
 *
 * Most course group names are "Group\SongName", so we want to match against the
 * last two elements. Let's also support "SongName" alone, since the group is
 * only important when it's potentially ambiguous.
 *
 * Let's *not* support "Songs\Group\SongName" in course files. That's probably
 * a common error, but that would result in course files floating around that
 * only work for people who put songs in "Songs"; we don't want that. */

Song* SongManager::FindSong(std::string path) const {
  Replace(path, '\\', '/');
  std::vector<std::string> bits;
  split(path, "/", bits);

  if (bits.size() == 1) {
    return FindSong("", bits[0]);
  } else if (bits.size() == 2) {
    return FindSong(bits[0], bits[1]);
  }

  return nullptr;
}

Song* SongManager::FindSong(std::string group, std::string song) const {
  // foreach song
  const std::vector<Song*>& songs = GetSongs(group.empty() ? GROUP_ALL : group);
  for (Song* s : songs) {
    if (s->Matches(group, song)) {
      return s;
    }
  }

  return nullptr;
}

Course* SongManager::FindCourse(std::string path) const {
  Replace(path, '\\', '/');
  std::vector<std::string> bits;
  split(path, "/", bits);

  if (bits.size() == 1) {
    return FindCourse("", bits[0]);
  } else if (bits.size() == 2) {
    return FindCourse(bits[0], bits[1]);
  }

  return nullptr;
}

Course* SongManager::FindCourse(std::string group, std::string name) const {
  for (Course* c : courses_) {
    if (c->Matches(group, name)) {
      return c;
    }
  }

  return nullptr;
}

void SongManager::UpdatePopular() {
  // update players best
  std::vector<Song*> best_songs = songs_;
  for (unsigned j = 0; j < best_songs.size(); ++j) {
    bool filtered = false;
    // Filter out locked songs.
    if (!best_songs[j]->NormallyDisplayed()) {
      filtered = true;
    }
    if (!filtered) {
      continue;
    }

    // Remove it.
    std::swap(best_songs[j], best_songs.back());
    best_songs.erase(best_songs.end() - 1);
    --j;
  }

  SongUtil::SortSongPointerArrayByTitle(best_songs);

  std::vector<Course*> best_courses[NUM_CourseType];
  FOREACH_ENUM(CourseType, ct) {
    GetCourses(ct, best_courses[ct], PREFSMAN->autogen_group_courses_);
    CourseUtil::SortCoursePointerArrayByTitle(best_courses[ct]);
  }

  popular_songs_ = best_songs;
  SongUtil::SortSongPointerArrayByNumPlays(
      popular_songs_, ProfileSlot_Machine, true);

  FOREACH_CourseType(ct) {
    std::vector<Course*>& courses = popular_courses_[ct];
    courses = best_courses[ct];
    CourseUtil::SortCoursePointerArrayByNumPlays(
        courses, ProfileSlot_Machine, true);
  }
}

std::map<int, std::vector<Song*>> SongManager::UpdateMeterSort(
    std::vector<Song*> songs) {
  // Empty the map
  map_songs_by_difficulty_.clear();
  std::vector<Song*> difficult_songs = songs;
  // For each song, for each step
  for (unsigned i = 0; i < difficult_songs.size(); ++i) {
    std::vector<Steps*> vp_steps;
    SongUtil::GetPlayableSteps(difficult_songs[i], vp_steps);
    for (unsigned j = 0; j < vp_steps.size(); ++j) {
      Steps* p_steps = vp_steps[j];
      // Check if the meter is already in m_mapSongsByDifficulty
      if (std::find(
              map_songs_by_difficulty_[p_steps->GetMeter()].begin(),
              map_songs_by_difficulty_[p_steps->GetMeter()].end(),
              difficult_songs[i]) !=
          map_songs_by_difficulty_[p_steps->GetMeter()].end()) {
        continue;
      } else {
        map_songs_by_difficulty_[p_steps->GetMeter()].push_back(
            difficult_songs[i]);
      }
    }
  }
  return map_songs_by_difficulty_;
}

void SongManager::UpdateShuffled() {
  // update shuffled
  shuffled_songs_ = songs_;
  std::shuffle(
      shuffled_songs_.begin(), shuffled_songs_.end(), g_RandomNumberGenerator);

  shuffled_courses_ = courses_;
  std::shuffle(
      shuffled_courses_.begin(), shuffled_courses_.end(),
      g_RandomNumberGenerator);
}

void SongManager::SetPreferredSongsFromTable(
    std::map<std::string, std::vector<Song*>> map_section_to_songs) {
  ASSERT(UNLOCKMAN != nullptr);
  preferred_song_sort_.clear();
  map_preferred_section_to_songs_.clear();

  PreferredSortSection section;
  for (const std::pair<const std::string, std::vector<Song*>>& pair :
       map_section_to_songs) {
    if (pair.second.empty()) {
      continue;
    }
    section = PreferredSortSection();
    section.name_ = pair.first;
    for (Song* song : pair.second) {
      if (song == nullptr) {
        continue;
      }
      if (UNLOCKMAN->SongIsLocked(song) & LOCKED_SELECTABLE) {
        continue;
      }

      section.songs_.push_back(song);
    }
    preferred_song_sort_.push_back(section);
    map_preferred_section_to_songs_[section.name_] = section.songs_;
  }

  if (MOVE_UNLOCKS_TO_BOTTOM_OF_PREFERRED_SORT.GetValue()) {
    // move all unlock songs to a group at the bottom
    PreferredSortSection pf_section;
    pf_section.name_ = "Unlocks";
    for (const UnlockEntry& ue : UNLOCKMAN->unlock_entries_) {
      if (ue.type_ == UnlockRewardType_Song) {
        Song* p_song = ue.song_.ToSong();
        if (p_song) {
          pf_section.songs_.push_back(p_song);
        }
      }
    }

    preferred_song_sort_.push_back(pf_section);
    map_preferred_section_to_songs_[pf_section.name_] = pf_section.songs_;
  }

  // prune empty groups
  for (int i = preferred_song_sort_.size() - 1; i >= 0; i--) {
    if (preferred_song_sort_[i].songs_.empty()) {
      map_preferred_section_to_songs_.erase(preferred_song_sort_[i].name_);
      preferred_song_sort_.erase(preferred_song_sort_.begin() + i);
    }
  }

  for (const PreferredSortSection& i : preferred_song_sort_) {
    for (const Song* j : i.songs_) {
      ASSERT(j != nullptr);
    }
  }
}

void SongManager::SetPreferredSongs(
    std::string preferred_songs, bool is_absolute) {
  ASSERT(UNLOCKMAN != nullptr);

  preferred_song_sort_.clear();
  map_preferred_section_to_songs_.clear();
  std::vector<std::string> lines;
  std::string file = preferred_songs;
  if (!is_absolute) {
    file = THEME->GetPathO("SongManager", preferred_songs);
  }
  GetFileContents(file, lines);
  if (lines.empty()) {
    return;
  }

  PreferredSortSection section;

  for (std::string line : lines) {
    bool section_divider = BeginsWith(line, "---");
    if (section_divider) {
      if (!section.songs_.empty()) {
        preferred_song_sort_.push_back(section);
        map_preferred_section_to_songs_[section.name_] = section.songs_;
        section = PreferredSortSection();
      }

      section.name_ = Right(line, line.length() - std::string("---").length());
      TrimLeft(section.name_);
      TrimRight(section.name_);
    } else {
      /* if the line ends in slash-star, check if the section exists,
       * and if it does, add all the songs in that group to the list. */
      if (EndsWith(line, "/*")) {
        std::string group =
            Left(line, line.length() - std::string("/*").length());
        if (DoesSongGroupExist(group)) {
          // add all songs in group
          const std::vector<Song*>& songs = GetSongs(group);
          for (Song* song : songs) {
            if (UNLOCKMAN->SongIsLocked(song) & LOCKED_SELECTABLE) {
              continue;
            }
            section.songs_.push_back(song);
          }
        }
      }

      Song* p_song = FindSong(line);
      if (p_song == nullptr) {
        continue;
      }
      if (UNLOCKMAN->SongIsLocked(p_song) & LOCKED_SELECTABLE) {
        continue;
      }
      section.songs_.push_back(p_song);
    }
  }

  if (!section.songs_.empty()) {
    preferred_song_sort_.push_back(section);
    map_preferred_section_to_songs_[section.name_] = section.songs_;
    section = PreferredSortSection();
  }

  if (MOVE_UNLOCKS_TO_BOTTOM_OF_PREFERRED_SORT.GetValue()) {
    // move all unlock songs to a group at the bottom
    PreferredSortSection pf_section;
    pf_section.name_ = "Unlocks";
    for (const UnlockEntry& ue : UNLOCKMAN->unlock_entries_) {
      if (ue.type_ == UnlockRewardType_Song) {
        Song* p_song = ue.song_.ToSong();
        if (p_song) {
          pf_section.songs_.push_back(p_song);
        }
      }
    }

    // NOTE(crashcringle): This code removed the unlocks from other sections
    // they might have been in. This was needed due to the previous 1:1
    // relationship between songs and section in order for the song to appear in
    // the Unlocks section correctly. Commented out for now. for
    // (std::vector<PreferredSortSection>::iterator v =
    // m_vPreferredSongSort.begin(); v != m_vPreferredSongSort.end(); ++v)
    // {
    // 	for( int i=v->vpSongs.size()-1; i>=0; i-- )
    // 	{
    // 		Song *pSong = v->vpSongs[i];
    // 		if(
    // find(PFSection.vpSongs.begin(),PFSection.vpSongs.end(),pSong) !=
    // PFSection.vpSongs.end() )
    // 		{
    // 			v->vpSongs.erase( v->vpSongs.begin()+i );
    // 		}
    // 	}
    // }

    preferred_song_sort_.push_back(pf_section);
    map_preferred_section_to_songs_[pf_section.name_] = pf_section.songs_;
  }

  // prune empty groups
  for (int i = preferred_song_sort_.size() - 1; i >= 0; i--) {
    if (preferred_song_sort_[i].songs_.empty()) {
      preferred_song_sort_.erase(preferred_song_sort_.begin() + i);
      map_preferred_section_to_songs_.erase(preferred_song_sort_[i].name_);
    }
  }

  for (const PreferredSortSection& i : preferred_song_sort_) {
    for (const Song* j : i.songs_) {
      ASSERT(j != nullptr);
    }
  }
}

void SongManager::SetPreferredCourses(
    std::string preferred_courses, bool is_absolute) {
  ASSERT(UNLOCKMAN != nullptr);

  preferred_course_sort_.clear();

  std::vector<std::string> lines;
  std::string file = preferred_courses;
  if (!is_absolute) {
    file = THEME->GetPathO("SongManager", preferred_courses);
  }
  if (!GetFileContents(file, lines)) {
    return;
  }

  std::vector<Course*> courses;

  for (std::string line : lines) {
    bool section_divider = BeginsWith(line, "---");
    if (section_divider) {
      if (!courses.empty()) {
        preferred_course_sort_.push_back(courses);
        courses.clear();
      }
      continue;
    }

    Course* course = FindCourse(line);
    if (course == nullptr) {
      continue;
    }
    if (UNLOCKMAN->CourseIsLocked(course) & LOCKED_SELECTABLE) {
      continue;
    }
    courses.push_back(course);
  }

  if (!courses.empty()) {
    preferred_course_sort_.push_back(courses);
    courses.clear();
  }

  if (MOVE_UNLOCKS_TO_BOTTOM_OF_PREFERRED_SORT.GetValue()) {
    // move all unlock Courses to a group at the bottom
    std::vector<Course*> unlock_courses;
    for (const UnlockEntry& ue : UNLOCKMAN->unlock_entries_) {
      if (ue.type_ == UnlockRewardType_Course) {
        if (ue.course_.IsValid()) {
          unlock_courses.push_back(ue.course_.ToCourse());
        }
      }
    }

    for (auto v = preferred_course_sort_.begin();
         v != preferred_course_sort_.end(); ++v) {
      for (int i = v->size() - 1; i >= 0; i--) {
        Course* course = (*v)[i];
        if (find(unlock_courses.begin(), unlock_courses.end(), course) !=
            unlock_courses.end()) {
          v->erase(v->begin() + i);
        }
      }
    }

    preferred_course_sort_.push_back(unlock_courses);
  }

  // prune empty groups
  for (int i = preferred_course_sort_.size() - 1; i >= 0; i--) {
    if (preferred_course_sort_[i].empty()) {
      preferred_course_sort_.erase(preferred_course_sort_.begin() + i);
    }
  }

  for (const CoursePointerVector& i : preferred_course_sort_) {
    for (Course* j : i) {
      ASSERT(j != nullptr);
    }
  }
}

void SongManager::UpdatePreferredSort(
    std::string preferred_songs, std::string preferred_courses) {
  SetPreferredSongs(preferred_songs);
  SetPreferredCourses(preferred_courses);
}

void SongManager::SortSongs() { SongUtil::SortSongPointerArrayByTitle(songs_); }

void SongManager::UpdateRankingCourses() {
  /* Updating the ranking courses data is fairly expensive since it involves
   * comparing strings. Do so sparingly. */
  std::vector<std::string> ranking_courses;
  split(
      THEME->GetMetric("ScreenRanking", "CoursesToShow"), ",", ranking_courses);

  for (Course* c : courses_) {
    bool lots_of_stages = c->GetEstimatedNumStages() > 7;
    c->sort_order_ranking_ = lots_of_stages ? 3 : 2;

    for (unsigned j = 0; j < ranking_courses.size(); j++) {
      if (!CompareNoCase(ranking_courses[j], c->path_)) {
        c->sort_order_ranking_ = 1;
      }
    }
  }
}

void SongManager::RefreshCourseGroupInfo() {
  map_course_group_to_info_.clear();

  for (const Course* c : courses_) {
    map_course_group_to_info_[c->group_name_];  // insert
  }
}

void SongManager::LoadStepEditsFromProfileDir(
    const std::string& profile_dir, ProfileSlot slot) {
  // Load all edit steps
  std::string dir = profile_dir + EDIT_STEPS_SUBDIR;
  SSCLoader loader_ssc;
  SMLoader loader_sm;
  int num_edits_loaded = GetNumEditsLoadedFromProfile(slot);

  // Pass 1: Flat folder (old style)
  std::vector<std::string> files;
  int size = std::min(
      (int)files.size(), MAX_EDIT_STEPS_PER_PROFILE - num_edits_loaded);
  GetDirListing(dir + "*.edit", files, false, true);

  // XXX: If some edits are invalid and they're close to the edit limit, this
  // may erroneously skip some edits, and won't warn.
  for (int i = 0; i < size; i++) {
    std::string fn = files[i];
    bool loaded_from_ssc = loader_ssc.LoadEditFromFile(fn, slot, true);
    // If we don't load the edit from a .ssc-style .edit, then we should
    // also try the .sm-style edit file. -aj
    if (!loaded_from_ssc) {
      loader_sm.LoadEditFromFile(fn, slot, true);
    }
  }

  if ((int)files.size() > MAX_EDIT_STEPS_PER_PROFILE - num_edits_loaded) {
    LuaHelpers::ReportScriptErrorFmt(
        "Profile %s has too many edits; some have been skipped.",
        ProfileSlotToString(slot).c_str());
    return;
  }

  // Some .edit files may have been invalid, so re-query instead of just +=
  // size.
  num_edits_loaded = GetNumEditsLoadedFromProfile(slot);

  // Pass 2: Group and song folders with #SONG inferred from folder (optional
  // new style)
  std::vector<std::string> groups;
  GetDirListing(dir + "*", groups, true, false);

  // XXX: Same as above, edits may be skipped in error in some cases
  for (unsigned i = 0; i < groups.size(); i++) {
    std::string group_dir = groups[i] + "/";
    std::vector<std::string> songs;
    GetDirListing(dir + group_dir + "*", songs, true, false);

    for (unsigned j = 0; j < songs.size(); j++) {
      std::vector<std::string> edits;
      std::string song_dir = group_dir + songs[j] + "/";
      // XXX There doesn't appear to be a songdir const?
      Song* given = GetSongFromDir("/Songs/" + song_dir);
      // NOTE: We don't have to check the return value of GetSongFromDir here,
      // because if it fails, it returns nullptr, which is then passed to
      // NotesLoader*.LoadEditFromFile(), which will interpret that as "we
      // couldn't infer the song from the path", which is what we want in that
      // case anyway.
      GetDirListing(dir + song_dir + "/*.edit", edits, false, true);
      size = std::min(
          (int)edits.size(), MAX_EDIT_STEPS_PER_PROFILE - num_edits_loaded);

      for (int k = 0; k < size; k++) {
        std::string fn = edits[k];
        bool loaded_from_ssc =
            loader_ssc.LoadEditFromFile(fn, slot, true, given);
        // And try again with SM
        if (!loaded_from_ssc) {
          loader_sm.LoadEditFromFile(fn, slot, true, given);
        }
      }

      if ((int)edits.size() > MAX_EDIT_STEPS_PER_PROFILE - num_edits_loaded) {
        LuaHelpers::ReportScriptErrorFmt(
            "Profile %s has too many edits; some have been skipped.",
            ProfileSlotToString(slot).c_str());
        return;
      }

      // Some .edit files may have been invalid, so re-query instead of just +=
      // size.
      num_edits_loaded = GetNumEditsLoadedFromProfile(slot);
    }
  }
}

void SongManager::LoadCourseEditsFromProfileDir(
    const std::string& profile_dir, ProfileSlot slot) {
  // Load all edit courses
  std::string dir = profile_dir + EDIT_COURSES_SUBDIR;

  std::vector<std::string> files;
  GetDirListing(dir + "*.crs", files, false, true);

  int num_edits_loaded = GetNumEditsLoadedFromProfile(slot);
  int size = std::min(
      (int)files.size(), MAX_EDIT_COURSES_PER_PROFILE - num_edits_loaded);

  for (int i = 0; i < size; i++) {
    std::string fn = files[i];

    CourseLoaderCRS::LoadEditFromFile(fn, slot);
  }
}

int SongManager::GetNumEditsLoadedFromProfile(ProfileSlot slot) const {
  int count = 0;
  for (unsigned s = 0; s < songs_.size(); s++) {
    const Song* song = songs_[s];
    std::vector<Steps*> ap_steps;
    SongUtil::GetSteps(song, ap_steps);

    for (unsigned i = 0; i < ap_steps.size(); ++i) {
      const Steps* p_steps = ap_steps[i];
      if (p_steps->WasLoadedFromProfile() &&
          p_steps->GetLoadedFromProfileSlot() == slot) {
        ++count;
      }
    }
  }
  return count;
}

void SongManager::AddSongToList(Song* new_song) {
  new_song->SetEnabled(true);
  songs_.push_back(new_song);
  std::string dir = new_song->GetSongDir();
  MakeLower(dir);
  songs_by_dir_.insert(std::make_pair(dir, new_song));
}

void SongManager::FreeAllLoadedFromProfile(ProfileSlot slot) {
  // Profile courses may refer to profile steps, so free profile courses first.
  std::vector<Course*> to_delete;
  for (Course* course : courses_) {
    if (course->GetLoadedFromProfileSlot() == ProfileSlot_Invalid) {
      continue;
    }

    if (slot == ProfileSlot_Invalid ||
        course->GetLoadedFromProfileSlot() == slot) {
      to_delete.push_back(course);
    }
  }

  /* We don't use DeleteCourse here, so we don't UpdatePopular and
   * UpdateShuffled repeatedly. */
  for (unsigned i = 0; i < to_delete.size(); ++i) {
    std::vector<Course*>::iterator iter =
        find(courses_.begin(), courses_.end(), to_delete[i]);
    ASSERT(iter != courses_.end());
    courses_.erase(iter);
    delete to_delete[i];
  }

  // Popular and Shuffled may refer to courses that we just freed.
  UpdatePopular();
  UpdateShuffled();
  RefreshCourseGroupInfo();

  // Free profile steps.
  std::set<Steps*> set_in_use;
  if (STATSMAN) {
    STATSMAN->GetStepsInUse(set_in_use);
  }
  for (Song* s : songs_) {
    s->FreeAllLoadedFromProfile(slot, &set_in_use);
  }
}

int SongManager::GetNumStepsLoadedFromProfile() {
  int count = 0;
  for (const Song* s : songs_) {
    std::vector<Steps*> all_steps = s->GetAllSteps();

    count += std::count_if(
        all_steps.begin(), all_steps.end(), [](const Steps* step) {
          return step->GetLoadedFromProfileSlot() != ProfileSlot_Invalid;
        });
  }

  return count;
}

int SongManager::GetSongRank(Song* song) {
  const int index =
      FindIndex(popular_songs_.begin(), popular_songs_.end(), song);
  return index;  // -1 means we didn't find it
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the SongManager. */
class LunaSongManager : public Luna<SongManager> {
 public:
  static int SetPreferredSongs(T* p, lua_State* L) {
    std::string preferred_songs = SArg(1);
    if (lua_gettop(L) >= 2 && !lua_isnil(L, 2)) {
      p->SetPreferredSongs(preferred_songs, BArg(2));
    } else {
      p->SetPreferredSongs(preferred_songs);
    }

    COMMON_RETURN_SELF;
  }

  static int SetPreferredSongsFromTable(T* p, lua_State* L) {
    if (!lua_istable(L, 1)) {
      return luaL_error(
          L, "Expected a table for SetPreferredSongsFromTable. Got %s.",
          luaL_typename(L, 1));
    }

    std::map<std::string, std::vector<Song*>> map_section_to_songs;
    lua_pushnil(L);  // first key
    while (lua_next(L, 1) != 0) {
      std::string section_name = SArg(-2);
      std::vector<Song*> songs;

      if (lua_istable(L, -1)) {
        lua_pushnil(L);  // first key in the inner table
        while (lua_next(L, -2) != 0) {
          // value is at index -1
          Song* song = Luna<Song>::check(L, -1);
          if (song) {
            songs.push_back(song);
          }
          lua_pop(L, 1);  // remove value, keep key for next iteration
        }
      }

      map_section_to_songs[section_name] = songs;
      lua_pop(L, 1);  // remove value, keep key for next iteration
    }

    p->SetPreferredSongsFromTable(map_section_to_songs);
    COMMON_RETURN_SELF;
  }

  static int SetPreferredCourses(T* p, lua_State* L) {
    std::string preferred_courses = SArg(1);
    if (lua_gettop(L) >= 2 && !lua_isnil(L, 2)) {
      p->SetPreferredSongs(preferred_courses, BArg(2));
    } else {
      p->SetPreferredSongs(preferred_courses);
    }
    COMMON_RETURN_SELF;
  }
  static int GetAllSongs(T* p, lua_State* L) {
    const std::vector<Song*>& v = p->GetAllSongs();
    LuaHelpers::CreateTableFromArray<Song*>(v, L);
    return 1;
  }
  static int GetAllCourses(T* p, lua_State* L) {
    std::vector<Course*> v;
    p->GetAllCourses(v, BArg(1));
    LuaHelpers::CreateTableFromArray<Course*>(v, L);
    return 1;
  }

  static int GetPreferredSortSongs(T* p, lua_State* L) {
    std::vector<Song*> v;
    p->GetPreferredSortSongs(v);
    LuaHelpers::CreateTableFromArray<Song*>(v, L);
    return 1;
  }
  static int GetPreferredSortCourses(T* p, lua_State* L) {
    std::vector<Course*> v;
    CourseType ct = Enum::Check<CourseType>(L, 1);
    p->GetPreferredSortCourses(ct, v, BArg(2));
    LuaHelpers::CreateTableFromArray<Course*>(v, L);
    return 1;
  }

  static int FindSong(T* p, lua_State* L) {
    Song* s = p->FindSong(SArg(1));
    if (s) {
      s->PushSelf(L);
    } else {
      lua_pushnil(L);
    }
    return 1;
  }
  static int FindCourse(T* p, lua_State* L) {
    Course* c = p->FindCourse(SArg(1));
    if (c) {
      c->PushSelf(L);
    } else {
      lua_pushnil(L);
    }
    return 1;
  }
  static int GetRandomSong(T* p, lua_State* L) {
    Song* s = p->GetRandomSong();
    if (s) {
      s->PushSelf(L);
    } else {
      lua_pushnil(L);
    }
    return 1;
  }
  static int GetRandomCourse(T* p, lua_State* L) {
    Course* c = p->GetRandomCourse();
    if (c) {
      c->PushSelf(L);
    } else {
      lua_pushnil(L);
    }
    return 1;
  }
  static int GetNumSongs(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetNumSongs());
    return 1;
  }
  static int GetNumLockedSongs(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetNumLockedSongs());
    return 1;
  }
  static int GetNumUnlockedSongs(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetNumUnlockedSongs());
    return 1;
  }
  static int GetNumSelectableAndUnlockedSongs(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetNumSelectableAndUnlockedSongs());
    return 1;
  }
  static int GetNumAdditionalSongs(T* p, lua_State* L) {
    lua_pushnumber(L, 0);
    return 1;
  }  // deprecated
  static int GetNumSongGroups(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetNumSongGroups());
    return 1;
  }
  static int GetNumCourses(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetNumCourses());
    return 1;
  }
  static int GetNumAdditionalCourses(T* p, lua_State* L) {
    lua_pushnumber(L, 0);
    return 1;
  }  // deprecated
  static int GetNumCourseGroups(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetNumCourseGroups());
    return 1;
  }

  /* Note: this could now be implemented as Luna<Steps>::GetSong */
  static int GetSongFromSteps(T* p, lua_State* L) {
    Song* song = nullptr;
    if (lua_isnil(L, 1)) {
      song = nullptr;
    } else {
      Steps* steps = Luna<Steps>::check(L, 1);
      song = steps->song_;
    }
    if (song) {
      song->PushSelf(L);
    } else {
      lua_pushnil(L);
    }
    return 1;
  }

  static int GetExtraStageInfo(T* p, lua_State* L) {
    bool extra2 = BArg(1);
    const Style* style = Luna<Style>::check(L, 2);
    Song* song;
    Steps* steps;

    p->GetExtraStageInfo(extra2, style, song, steps);
    song->PushSelf(L);
    steps->PushSelf(L);

    return 2;
  }
  DEFINE_METHOD(GetSongColor, GetSongColor(Luna<Song>::check(L, 1)))
  DEFINE_METHOD(GetSongGroupColor, GetSongGroupColor(SArg(1)))
  DEFINE_METHOD(GetCourseColor, GetCourseColor(Luna<Course>::check(L, 1)))

  static int GetSongRank(T* p, lua_State* L) {
    Song* song = Luna<Song>::check(L, 1);
    int index = p->GetSongRank(song);
    if (index != -1) {
      lua_pushnumber(L, index + 1);
    } else {
      lua_pushnil(L);
    }
    return 1;
  }
  /*
  static int GetSongRankFromProfile( T* p, lua_State *L )
  {
          // it's like the above but also takes in a ProfileSlot as well.
  }
  */

  static int GetSongGroupNames(T* p, lua_State* L) {
    std::vector<std::string> v;
    p->GetSongGroupNames(v);
    LuaHelpers::CreateTableFromArray<std::string>(v, L);
    return 1;
  }

  static int GetGroup(T* p, lua_State* L) {
    Song* song = Luna<Song>::check(L, 1);
    Group* group = p->GetGroup(song);
    if (group != nullptr) {
      group->PushSelf(L);
    } else {
      lua_pushnil(L);
    }
    return 1;
  }

  static int GetSongsInGroup(T* p, lua_State* L) {
    std::vector<Song*> v = p->GetSongs(SArg(1));
    LuaHelpers::CreateTableFromArray<Song*>(v, L);
    return 1;
  }

  static int GetCoursesInGroup(T* p, lua_State* L) {
    std::vector<Course*> v;
    p->GetCoursesInGroup(v, SArg(1), BArg(2));
    LuaHelpers::CreateTableFromArray<Course*>(v, L);
    return 1;
  }

  DEFINE_METHOD(ShortenGroupName, ShortenGroupName(SArg(1)))
  DEFINE_METHOD(ShortenSeriesName, ShortenSeriesName(SArg(1)))

  static int GetCourseGroupNames(T* p, lua_State* L) {
    std::vector<std::string> v;
    p->GetCourseGroupNames(v);
    LuaHelpers::CreateTableFromArray<std::string>(v, L);
    return 1;
  }

  DEFINE_METHOD(GetSongGroupBannerPath, GetSongGroupBannerPath(SArg(1)));
  DEFINE_METHOD(GetSeriesBannerPath, GetSeriesBannerPath(SArg(1)));
  DEFINE_METHOD(GetCourseGroupBannerPath, GetCourseGroupBannerPath(SArg(1)));
  DEFINE_METHOD(DoesSongGroupExist, DoesSongGroupExist(SArg(1)));
  DEFINE_METHOD(DoesCourseGroupExist, DoesCourseGroupExist(SArg(1)));

  static int GetPopularSongs(T* p, lua_State* L) {
    const std::vector<Song*>& v = p->GetPopularSongs();
    LuaHelpers::CreateTableFromArray<Song*>(v, L);
    return 1;
  }
  static int GetPopularCourses(T* p, lua_State* L) {
    CourseType ct = Enum::Check<CourseType>(L, 1);
    const std::vector<Course*>& v = p->GetPopularCourses(ct);
    LuaHelpers::CreateTableFromArray<Course*>(v, L);
    return 1;
  }
  static int SongToPreferredSortSectionName(T* p, lua_State* L) {
    const Song* song = Luna<Song>::check(L, 1);
    lua_pushstring(L, p->SongToPreferredSortSectionName(song).c_str());
    return 1;
  }
  static int GetPreferredSortSongsBySectionName(T* p, lua_State* L) {
    std::vector<Song*> v;
    p->GetPreferredSortSongsBySectionName(SArg(1), v);
    LuaHelpers::CreateTableFromArray<Song*>(v, L);
    return 1;
  }

  static int WasLoadedFromAdditionalSongs(T* p, lua_State* L) {
    lua_pushboolean(L, false);
    return 1;
  }  // deprecated
  static int WasLoadedFromAdditionalCourses(T* p, lua_State* L) {
    lua_pushboolean(L, false);
    return 1;
  }  // deprecated

  LunaSongManager() {
    ADD_METHOD(GetAllSongs);
    ADD_METHOD(GetAllCourses);
    ADD_METHOD(FindSong);
    ADD_METHOD(FindCourse);
    ADD_METHOD(GetRandomSong);
    ADD_METHOD(GetRandomCourse);
    ADD_METHOD(GetCourseGroupNames);
    ADD_METHOD(GetNumSongs);
    ADD_METHOD(GetNumLockedSongs);
    ADD_METHOD(GetNumUnlockedSongs);
    ADD_METHOD(GetNumSelectableAndUnlockedSongs);
    ADD_METHOD(GetNumAdditionalSongs);  // deprecated
    ADD_METHOD(GetNumSongGroups);
    ADD_METHOD(GetNumCourses);
    ADD_METHOD(GetNumAdditionalCourses);  // deprecated
    ADD_METHOD(GetNumCourseGroups);
    ADD_METHOD(GetSongFromSteps);
    ADD_METHOD(GetExtraStageInfo);
    ADD_METHOD(GetSongColor);
    ADD_METHOD(GetSongGroupColor);
    ADD_METHOD(GetCourseColor);
    ADD_METHOD(GetSongRank);
    ADD_METHOD(GetSongGroupNames);
    ADD_METHOD(GetGroup);
    ADD_METHOD(GetSongsInGroup);
    ADD_METHOD(GetCoursesInGroup);
    ADD_METHOD(ShortenGroupName);
    ADD_METHOD(ShortenSeriesName);
    ADD_METHOD(SetPreferredSongs);
    ADD_METHOD(SetPreferredSongsFromTable);
    ADD_METHOD(SetPreferredCourses);
    ADD_METHOD(GetPreferredSortSongs);
    ADD_METHOD(GetPreferredSortCourses);
    ADD_METHOD(GetSongGroupBannerPath);
    ADD_METHOD(GetSeriesBannerPath);
    ADD_METHOD(GetCourseGroupBannerPath);
    ADD_METHOD(DoesSongGroupExist);
    ADD_METHOD(DoesCourseGroupExist);
    ADD_METHOD(GetPopularSongs);
    ADD_METHOD(GetPopularCourses);
    ADD_METHOD(SongToPreferredSortSectionName);
    ADD_METHOD(GetPreferredSortSongsBySectionName);
    ADD_METHOD(WasLoadedFromAdditionalSongs);    // deprecated
    ADD_METHOD(WasLoadedFromAdditionalCourses);  // deprecated
  }
};

LUA_REGISTER_CLASS(SongManager)
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
