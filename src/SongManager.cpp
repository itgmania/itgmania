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

void SongManager::InitAll(LoadingWindow* ld, bool onlyAdditions) {
  std::vector<std::string> never_cache;
  split(PREFSMAN->never_cache_list_, ",", never_cache);
  for (std::vector<std::string>::iterator group = never_cache.begin();
       group != never_cache.end(); ++group) {
    groups_to_never_cache_.insert(*group);
  }
  InitSongsFromDisk(ld, onlyAdditions);
  InitCoursesFromDisk(ld, onlyAdditions);
  if (onlyAdditions) {
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

void SongManager::Reload(bool bAllowFastLoad, LoadingWindow* ld) {
  FILEMAN->FlushDirCache(SpecialFiles::SONGS_DIR);
  FILEMAN->FlushDirCache(SpecialFiles::COURSES_DIR);
  FILEMAN->FlushDirCache(EDIT_SUBDIR);

  if (ld) {
    ld->SetText(RELOADING);
  }

  // save scores before unloading songs, or the scores will be lost
  PROFILEMAN->SaveMachineProfile();
  GAMESTATE->SavePlayerProfiles();

  std::vector<std::tuple<PlayerNumber, std::string, bool>> rejoinPlayers;
  FOREACH_HumanPlayer(pn) {
    if (GAMESTATE->side_is_joined_[pn]) {
      if (PROFILEMAN->ProfileWasLoadedFromMemoryCard(pn)) {
        rejoinPlayers.push_back(std::make_tuple(pn, std::string(""), true));
      } else if (PROFILEMAN->IsPersistentProfile(pn)) {
        int numLocalProfiles = PROFILEMAN->GetNumLocalProfiles();
        for (int i = 0; i < numLocalProfiles; i++) {
          Profile* profile = PROFILEMAN->GetLocalProfileFromIndex(i);
          if (profile->guid_ == PROFILEMAN->GetProfile(pn)->guid_) {
            std::string profileID = PROFILEMAN->GetLocalProfileIDFromIndex(i);
            rejoinPlayers.push_back(std::make_tuple(pn, profileID, false));
            break;
          }
        }
      } else {
        rejoinPlayers.push_back(std::make_tuple(pn, std::string(""), false));
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

  const bool oldVal = PREFSMAN->fast_load_;
  if (!bAllowFastLoad) {
    PREFSMAN->fast_load_.Set(false);
  }

  InitAll(ld, /*onlyAdditions=*/false);

  // reload scores and unlocks afterward
  PROFILEMAN->LoadMachineProfile();
  MEMCARDMAN->WaitForCheckingToComplete();
  for (auto& it : rejoinPlayers) {
    PlayerNumber pn;
    std::string profileID;
    bool isMemoryCard;
    std::tie(pn, profileID, isMemoryCard) = it;

    GAMESTATE->JoinPlayer(pn);
    if (isMemoryCard) {
      bool success = MEMCARDMAN->MountCard(pn);
      if (success) {
        PROFILEMAN->LoadProfileFromMemoryCard(pn, true);
        MEMCARDMAN->UnmountCard(pn);
      }
    } else {
      PROFILEMAN->default_local_profile_id_[pn].Set(profileID);
      PROFILEMAN->LoadLocalProfileFromMachine(pn);
    }
    GAMESTATE->LoadCurrentSettingsFromProfile(pn);
  }
  UNLOCKMAN->Reload();

  if (!bAllowFastLoad) {
    PREFSMAN->fast_load_.Set(oldVal);
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

void SongManager::InitSongsFromDisk(LoadingWindow* ld, bool onlyAdditions) {
  RageTimer tm;
  // Tell SONGINDEX to not write the cache index file every time a song adds
  // an entry. -Kyz
  SONGINDEX->delay_save_cache_ = true;
  IMAGECACHE->delay_save_cache_ = true;
  LoadSongDir(SpecialFiles::SONGS_DIR, ld, onlyAdditions);
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
bool SongManager::SanityCheckGroupDir(std::string sDir) const {
  // Check to see if they put a song directly inside the group folder.
  std::vector<std::string> arrayFiles;
  GetDirListing(sDir + "/*", arrayFiles);
  const std::vector<std::string>& audio_exts =
      ActorUtil::GetTypeExtensionList(FT_Sound);
  for (std::string& fname : arrayFiles) {
    const std::string ext = GetExtension(fname);
    for (const std::string& aud : audio_exts) {
      if (ext == aud) {
        LOG->Warn(FOLDER_CONTAINS_MUSIC_FILES.GetValue().c_str(), sDir.c_str());
        return false;
      }
    }
  }
  return true;
}

void SongManager::AddGroup(
    std::string sDir, std::string sGroupDirName, Group* group) {
  if (group == nullptr) {
    // Could not AddGroup 'sGroupDirName'. Group object is null.
    LOG->Warn(
        "Could not AddGroup '%s'. Group object is null.",
        sGroupDirName.c_str());
    return;
  }

  unsigned j;
  for (j = 0; j < song_group_names_.size(); ++j) {
    if (sGroupDirName == song_group_names_[j]) {
      break;
    }
  }

  if (j != song_group_names_.size()) {
    return;  // the group is already added
  }

  std::string sBannerPath;

  // Look for a group banner in this group folder
  std::vector<std::string> arrayGroupBanners;

  // First check if there is a banner provided in pack.ini
  if (!group->GetBannerPath().empty()) {
    GetDirListing(
        sDir + sGroupDirName + "/" + group->GetBannerPath(), arrayGroupBanners);
  }
  GetDirListing(sDir + sGroupDirName + "/*.png", arrayGroupBanners);
  GetDirListing(sDir + sGroupDirName + "/*.jpg", arrayGroupBanners);
  GetDirListing(sDir + sGroupDirName + "/*.jpeg", arrayGroupBanners);
  GetDirListing(sDir + sGroupDirName + "/*.gif", arrayGroupBanners);
  GetDirListing(sDir + sGroupDirName + "/*.bmp", arrayGroupBanners);

  if (!arrayGroupBanners.empty()) {
    sBannerPath = sDir + sGroupDirName + "/" + arrayGroupBanners[0];
  } else {
    // Look for a group banner in the parent folder
    GetDirListing(sDir + sGroupDirName + ".png", arrayGroupBanners);
    GetDirListing(sDir + sGroupDirName + ".jpg", arrayGroupBanners);
    GetDirListing(sDir + sGroupDirName + ".jpeg", arrayGroupBanners);
    GetDirListing(sDir + sGroupDirName + ".gif", arrayGroupBanners);
    GetDirListing(sDir + sGroupDirName + ".bmp", arrayGroupBanners);
    if (!arrayGroupBanners.empty()) {
      sBannerPath = sDir + arrayGroupBanners[0];
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
  song_group_banner_paths_.push_back(sBannerPath);
  song_group_names_.push_back(sGroupDirName);

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
      std::vector<std::string> arraySeriesBanners;
      GetDirListing(
          sDir + sGroupDirName + "/" + group->GetSeriesBannerPath(),
          arraySeriesBanners, false, true);
      if (!arraySeriesBanners.empty()) {
        series.banner_path_ = arraySeriesBanners[0];
      }
    }
  }
}

static LocalizedString LOADING_SONGS("SongManager", "Loading songs...");
void SongManager::LoadSongDir(
    std::string sDir, LoadingWindow* ld, bool onlyAdditions) {
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
  if (Right(sDir, 1) != "/") {
    sDir += "/";
  }

  // Find all group directories in "Songs" folder
  std::vector<std::string> arrayGroupDirs;
  GetDirListing(sDir + "*", arrayGroupDirs, true);
  SortRStringArray(arrayGroupDirs);
  StripCvsAndSvn(arrayGroupDirs);
  StripMacResourceForks(arrayGroupDirs);

  std::map<std::string, std::vector<std::string>> mapGroupSongDirs;
  int groupIndex, songCount;

  groupIndex = 0;
  songCount = 0;
  if (ld) {
    ld->SetIndeterminate(false);
    ld->SetTotalWork(arrayGroupDirs.size());
  }
  int sanity_index = 0;
  for (const std::string& sGroupDirName :
       arrayGroupDirs)  // foreach dir in /Songs/
  {
    if (ld &&
        loading_window_last_update_time.Ago() > next_loading_window_update) {
      loading_window_last_update_time.Touch();
      ld->SetProgress(sanity_index);
      ld->SetText(
          SANITY_CHECKING_GROUPS.GetValue() +
          ssprintf("\n%s", Basename(sGroupDirName).c_str()));
    }

    if (SanityCheckGroupDir(sDir + sGroupDirName)) {
      // Find all Song folders in this group directory
      std::vector<std::string> arraySongDirs;
      GetDirListing(sDir + sGroupDirName + "/*", arraySongDirs, true, true);
      StripCvsAndSvn(arraySongDirs);
      StripMacResourceForks(arraySongDirs);
      SortRStringArray(arraySongDirs);

      mapGroupSongDirs[sGroupDirName] = arraySongDirs;
      songCount += arraySongDirs.size();
    }
  }

  if (songCount == 0) {
    return;
  }

  if (ld) {
    ld->SetIndeterminate(false);
    ld->SetTotalWork(songCount);
  }

  int threadCount = std::thread::hardware_concurrency();
  if (threadCount <= 0) {
    threadCount = 1;
  }
  LOG->Trace("Loading songs using %d threads", threadCount);

  // We need to keep track of previously loaded groups so we don't delete them
  // if we're only loading additions.
  std::unordered_set<Group*> alreadyLoadedGroups;

  // An entry for each song that we need to load across all packs.
  std::vector<SongToLoad> songsToLoad{};
  for (const auto& [sGroupDirName, arraySongDirs] :
       mapGroupSongDirs)  // foreach dir in /Songs/
  {
    LOG->Trace(
        "Attempting to load %i songs from \"%s\"", int(arraySongDirs.size()),
        (sDir + sGroupDirName).c_str());

    for (unsigned j = 0; j < arraySongDirs.size(); ++j)  // for each song dir
    {
      // Skip already loaded songs if onlyAdditions is set.
      if (onlyAdditions) {
        SongID songID;
        songID.FromString(arraySongDirs[j]);
        if (songID.ToSong() != nullptr) {
          continue;
        }
      }

      songsToLoad.push_back(SongToLoad{sGroupDirName, arraySongDirs[j]});
    }

    const auto groupIter = map_name_to_group_.find(sGroupDirName);
    if (groupIter != map_name_to_group_.end()) {
      alreadyLoadedGroups.insert(groupIter->second);
    } else {
      map_name_to_group_[sGroupDirName] = new Group(sDir, sGroupDirName);
    }
  }

  // Holds the index of the next unloaded song for a thread to claim.
  std::atomic<int> nextSongToLoadIndex = 0;

  // Create an array for the results of the songs to load. Pre-allocating it
  // helps us avoid needing to coordinate threads in 2 ways:
  // - No memory re-allocations happen as songs are completed and entries are
  // updated, which
  //   otherwise could introduce problems with threads having stale pointers.
  // - Threads atomically claim an index of a song to load, so when they update
  // that entry after
  //   loading a song, they know that no other thread will also update that
  //   entry.
  std::unique_ptr<SongLoadResult[]> loadedSongs(
      new SongLoadResult[songsToLoad.size()]);

  LOG->Trace(
      "Loading all %d songs across %d groups across %d threads",
      (int)(songsToLoad.size()), (int)(mapGroupSongDirs.size()), threadCount);

  // Create the pool of threads that will load songs.
  std::vector<std::thread> threads{};
  threads.reserve(threadCount);

  // Define the work done in each thread.
  auto loadSongFn = [&songsToLoad, &nextSongToLoadIndex, &loadedSongs]() {
    while (true) {
      // Atomically grab the index of the next song to load and increment it.
      // This means that each thread gets a uniquely incremented value as they
      // load songs and move on to the next.
      int songToLoadIndex = nextSongToLoadIndex.fetch_add(1);
      if (songToLoadIndex >= (int)(songsToLoad.size())) {
        // Exit the thread when there are not more songs to load.
        return;
      }

      SongToLoad& songToLoad = songsToLoad[songToLoadIndex];

      // Is the LOG framework thread safe ?
      LOG->Trace("Loading %s", songToLoad.song_dir_name_.c_str());

      Song* pNewSong = new Song;
      if (!pNewSong->LoadFromSongDir(songToLoad.song_dir_name_)) {
        // The song failed to load.
        delete pNewSong;
        loadedSongs[songToLoadIndex].state_.store(
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
        loadedSongs[songToLoadIndex].song_ = pNewSong;
        loadedSongs[songToLoadIndex].state_.store(
            SongLoadResult::State::STATE_LOAD_SUCCEEDED,
            std::memory_order_release);
      }
    }
  };

  // Start the threads.
  for (int i = 0; i < threadCount; i++) {
    threads.emplace_back(loadSongFn);
  }

  // Here in the main thread, check songs sequentially for finishing loading.
  size_t loadedSongsProcessed = 0;
  std::unordered_map<std::string, int> groupLoadedSongCount;
  while (loadedSongsProcessed < songsToLoad.size()) {
    SongLoadResult& loadedSong = loadedSongs[loadedSongsProcessed];
    SongLoadResult::State state =
        loadedSong.state_.load(std::memory_order_acquire);

    if (state == SongLoadResult::State::STATE_LOAD_SUCCEEDED) {
      // These operations didn't appear thread safe to me which is why they are
      // in this post processing step in the main thread. Let me know if that
      // was an incorrect assumption !
      AddSongToList(loadedSong.song_);

      SongToLoad& songToLoad = songsToLoad[loadedSongsProcessed];
      SongPointerVector& index_entry =
          map_song_group_index_[songToLoad.group_dir_name_];
      index_entry.push_back(loadedSong.song_);

      groupLoadedSongCount[songToLoad.group_dir_name_]++;

      loadedSongsProcessed++;

      // Update progress window with the song we've most recently loaded. Since
      // songs are loading in parallel but reported sequentially, this could
      // result in some jumpy behavior, althought the limiting on how often the
      // window is updated may improve that.
      if (ld &&
          loading_window_last_update_time.Ago() > next_loading_window_update) {
        std::string group_base_name = Basename(songToLoad.group_dir_name_);
        loading_window_last_update_time.Touch();
        ld->SetProgress(loadedSongsProcessed);
        ld->SetText(
            LOADING_SONGS.GetValue() +
            ssprintf(
                "\n%s\n%s", group_base_name.c_str(),
                Basename(songToLoad.song_dir_name_).c_str()));
      }
    } else if (state == SongLoadResult::State::STATE_LOAD_FAILED) {
      // Even if a song fails to load, just move on.
      loadedSongsProcessed++;
    } else {
      // Sleeping for a short time tells the CPU it can context switch to other
      // tasks for a bit.
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  // Wait for all of the threads to exit.
  for (int i = 0; i < threadCount; i++) {
    threads[i].join();
  }

  // Mostly unchanged group finalizing work.
  for (const auto& [sGroupDirName, arraySongDirs] :
       mapGroupSongDirs)  // foreach dir in /Songs/
  {
    Group* group = map_name_to_group_.at(sGroupDirName);
    int loadedSongCount = groupLoadedSongCount[sGroupDirName];
    bool groupAlreadyLoaded =
        alreadyLoadedGroups.find(group) != alreadyLoadedGroups.end();

    LOG->Trace(
        "Loaded %d songs from \"%s\"", loadedSongCount,
        (sDir + sGroupDirName).c_str());

    // If we're only loading additions, already loaded groups should neither be
    // added nor deleted
    if (!(groupAlreadyLoaded && onlyAdditions)) {
      // Don't add the group name if we didn't load any songs in this group.
      if (!loadedSongCount) {
        // Remove the group from the group mapping
        auto it = map_name_to_group_.find(sGroupDirName);
        if (it != map_name_to_group_.end()) {
          map_name_to_group_.erase(it);
        }
        delete group;
        continue;
      }

      // Add this group to the group array.
      AddGroup(sDir, sGroupDirName, group);

      // Cache and load the group banner. (and background if it has one -aj)
      IMAGECACHE->CacheImage("Banner", GetSongGroupBannerPath(sGroupDirName));
    }

    // Load the group sym links (if any)
    LoadGroupSymLinks(sDir, sGroupDirName);
  }

  if (ld) {
    ld->SetIndeterminate(true);
  }
}

// Instead of "symlinks", songs should have membership in multiple groups.
// -Chris
void SongManager::LoadGroupSymLinks(
    std::string sDir, std::string sGroupFolder) {
  // Find all symlink files in this folder
  std::vector<std::string> arraySymLinks;
  GetDirListing(sDir + sGroupFolder + "/*.include", arraySymLinks, false);
  SortRStringArray(arraySymLinks);
  SongPointerVector& index_entry = map_song_group_index_[sGroupFolder];
  for (unsigned s = 0; s < arraySymLinks.size();
       s++)  // for each symlink in this dir, add it in as a song.
  {
    MsdFile msdF;
    msdF.ReadFile(
        sDir + sGroupFolder + "/" + arraySymLinks[s].c_str(),
        false);  // don't unescape
    std::string sSymDestination =
        msdF.GetParam(0, 1);  // Should only be 1 value & param...period.

    Song* pNewSong = new Song;
    if (!pNewSong->LoadFromSongDir(sSymDestination)) {
      delete pNewSong;  // The song failed to load.
    } else {
      const std::vector<Steps*>& vpSteps = pNewSong->GetAllSteps();
      while (vpSteps.size()) {
        pNewSong->DeleteSteps(vpSteps[0]);
      }

      FOREACH_BackgroundLayer(i) pNewSong->GetBackgroundChanges(i).clear();

      pNewSong->group_name_ = sGroupFolder;
      AddSongToList(pNewSong);
      index_entry.push_back(pNewSong);
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

    const RageTextureID ID =
        Sprite::SongBannerTexture(songs[i]->GetBannerPath());
    preload.Load(ID);
  }

  std::vector<Course*> courses;
  GetAllCourses(courses, false);
  for (unsigned i = 0; i < courses.size(); ++i) {
    if (!courses[i]->HasBanner()) {
      continue;
    }

    const RageTextureID ID =
        Sprite::SongBannerTexture(courses[i]->GetBannerPath());
    preload.Load(ID);
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
  std::vector<Song*>* songVectors[3] = {
      &songs_, &popular_songs_, &shuffled_songs_};
  for (int songVecIdx = 0; songVecIdx < 3; ++songVecIdx) {
    std::vector<Song*>& v = *songVectors[songVecIdx];
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

std::string SongManager::GetSongGroupBannerPath(std::string sSongGroup) const {
  for (unsigned i = 0; i < song_group_names_.size(); ++i) {
    if (sSongGroup == song_group_names_[i]) {
      return song_group_banner_paths_[i];
    }
  }

  return std::string();
}

std::string SongManager::GetSeriesBannerPath(std::string sSeriesName) const {
  std::map<std::string, Series>::const_iterator it =
      map_name_to_series_.find(sSeriesName);
  if (it == map_name_to_series_.end()) {
    return std::string();
  }
  if (!it->second.banner_path_.empty()) {
    return it->second.banner_path_;
  }

  // No group in the series defined a SeriesBanner: fall back to the
  // banner of the first group
  Group* firstGroup = nullptr;
  for (std::unordered_set<Group*>::const_iterator git =
           it->second.groups_.begin();
       git != it->second.groups_.end(); ++git) {
    if (firstGroup == nullptr ||
        (*git)->GetGroupName() < firstGroup->GetGroupName()) {
      firstGroup = *git;
    }
  }
  if (firstGroup != nullptr) {
    return GetSongGroupBannerPath(firstGroup->GetGroupName());
  }
  return std::string();
}

std::unordered_set<Group*> SongManager::GetGroupsInSeries(
    const std::string& sSeriesName) const {
  std::map<std::string, Series>::const_iterator it =
      map_name_to_series_.find(sSeriesName);
  if (it == map_name_to_series_.end()) {
    return std::unordered_set<Group*>();
  }
  return it->second.groups_;
}

void SongManager::GetSongGroupNames(std::vector<std::string>& AddTo) const {
  AddTo.insert(AddTo.end(), song_group_names_.begin(), song_group_names_.end());
}

bool SongManager::DoesSongGroupExist(std::string sSongGroup) const {
  return find(song_group_names_.begin(), song_group_names_.end(), sSongGroup) !=
         song_group_names_.end();
}

RageColor SongManager::GetSongGroupColor(const std::string& sSongGroup) const {
  for (unsigned i = 0; i < song_group_names_.size(); i++) {
    if (song_group_names_[i] == sSongGroup) {
      return SONG_GROUP_COLOR.GetValue(i % NUM_SONG_GROUP_COLORS);
    }
  }
  FOREACH_EnabledPlayer(pn) {
    Profile* prof = PROFILEMAN->GetProfile(pn);
    if (prof != nullptr) {
      if (prof->IsCustomSongGroup(sSongGroup)) {
        return PROFILE_SONG_GROUP_COLORS.GetValue(
            pn % NUM_PROFILE_SONG_GROUP_COLORS);
      }
    }
  }

  LuaHelpers::ReportScriptErrorFmt(
      "requested color for song group '%s' that doesn't exist",
      sSongGroup.c_str());
  return RageColor(1, 1, 1, 1);
}

RageColor SongManager::GetSongColor(const Song* pSong) const {
  ASSERT(pSong != nullptr);

  // protected by royal freem corporation. any modification/removal of
  // this code will result in prosecution.
  if (pSong->main_title_ == "DVNO") {
    return RageColor(1.0f, 0.8f, 0.0f, 1.0f);
  }
  // end royal freem protection

  // Use unlock color if applicable
  const UnlockEntry* pUE = UNLOCKMAN->FindSong(pSong);
  if (pUE && USE_UNLOCK_COLOR.GetValue()) {
    return UNLOCK_COLOR.GetValue();
  }

  if (USE_PREFERRED_SORT_COLOR) {
    int sortIndex = 0;
    for (const PreferredSortSection& v : preferred_song_sort_) {
      if (std::any_of(v.songs_.begin(), v.songs_.end(), [&](const Song* s) {
            return s == pSong;
          })) {
        return SONG_GROUP_COLOR.GetValue(sortIndex % NUM_SONG_GROUP_COLORS);
      }

      sortIndex += 1;
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
    const std::vector<Steps*>& vpSteps = pSong->GetAllSteps();
    for (unsigned i = 0; i < vpSteps.size(); i++) {
      const Steps* pSteps = vpSteps[i];
      switch (pSteps->GetDifficulty()) {
        case Difficulty_Challenge:
        case Difficulty_Edit:
          continue;
        default:
          break;
      }

      // if(pSteps->m_StepsType != st)
      //	continue;

      if (pSteps->GetMeter() >= EXTRA_COLOR_METER) {
        return (RageColor)EXTRA_COLOR;
      }
    }

    return GetSongGroupColor(pSong->group_name_);
  }
}

std::string SongManager::GetCourseGroupBannerPath(
    const std::string& sCourseGroup) const {
  std::map<std::string, CourseGroupInfo>::const_iterator iter =
      map_course_group_to_info_.find(sCourseGroup);
  if (iter == map_course_group_to_info_.end()) {
    ASSERT_M(
        0, ssprintf(
               "requested banner for course group '%s' that doesn't exist",
               sCourseGroup.c_str()));
    return std::string();
  } else {
    return iter->second.banner_path_;
  }
}

void SongManager::GetCourseGroupNames(std::vector<std::string>& AddTo) const {
  for (const std::pair<const std::string, CourseGroupInfo>& iter :
       map_course_group_to_info_) {
    AddTo.push_back(iter.first);
  }
}

bool SongManager::DoesCourseGroupExist(const std::string& sCourseGroup) const {
  return map_course_group_to_info_.find(sCourseGroup) !=
         map_course_group_to_info_.end();
}

RageColor SongManager::GetCourseGroupColor(
    const std::string& sCourseGroup) const {
  int iIndex = 0;
  for (const std::pair<const std::string, CourseGroupInfo>& iter :
       map_course_group_to_info_) {
    if (iter.first == sCourseGroup) {
      return SONG_GROUP_COLOR.GetValue(iIndex % NUM_SONG_GROUP_COLORS);
    }
    iIndex++;
  }

  ASSERT_M(
      0, ssprintf(
             "requested color for course group '%s' that doesn't exist",
             sCourseGroup.c_str()));
  return RageColor(1, 1, 1, 1);
}

RageColor SongManager::GetCourseColor(const Course* pCourse) const {
  // Use unlock color if applicable
  const UnlockEntry* pUE = UNLOCKMAN->FindCourse(pCourse);
  if (pUE && USE_UNLOCK_COLOR.GetValue()) {
    return UNLOCK_COLOR.GetValue();
  }

  if (USE_PREFERRED_SORT_COLOR) {
    int courseIndex = 0;
    for (const CoursePointerVector& v : preferred_course_sort_) {
      if (std::any_of(v.begin(), v.end(), [&](const Course* s) {
            return s == pCourse;
          })) {
        CHECKPOINT_M(ssprintf(
            "%i, NUM_COURSE_GROUP_COLORS = %i", courseIndex,
            NUM_COURSE_GROUP_COLORS.GetValue()));
        return COURSE_GROUP_COLOR.GetValue(
            courseIndex % NUM_COURSE_GROUP_COLORS);
      }
      courseIndex += 1;
    }

    int i = preferred_course_sort_.size();
    CHECKPOINT_M(ssprintf(
        "%i, NUM_COURSE_GROUP_COLORS = %i", i,
        NUM_COURSE_GROUP_COLORS.GetValue()));
    return COURSE_GROUP_COLOR.GetValue(i % NUM_COURSE_GROUP_COLORS);
  } else {
    return GetCourseGroupColor(pCourse->group_name_);
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
    const std::string& sGroupName) const {
  static const std::vector<Song*> vEmpty;

  if (sGroupName == GROUP_ALL) {
    return songs_;
  }
  std::map<std::string, SongPointerVector, Comp>::const_iterator iter =
      map_song_group_index_.find(sGroupName);
  if (iter != map_song_group_index_.end()) {
    return iter->second;
  }
  FOREACH_EnabledPlayer(pn) {
    Profile* prof = PROFILEMAN->GetProfile(pn);
    if (prof != nullptr) {
      if (prof->IsCustomSongGroup(sGroupName)) {
        return prof->songs_groups_[sGroupName];
      }
    }
  }
  return vEmpty;
}

void SongManager::GetPreferredSortSongs(std::vector<Song*>& AddTo) const {
  if (preferred_song_sort_.empty()) {
    AddTo.insert(AddTo.end(), songs_.begin(), songs_.end());
    return;
  }
  for (const PreferredSortSection& v : preferred_song_sort_) {
    AddTo.insert(AddTo.end(), v.songs_.begin(), v.songs_.end());
  }
}

std::string SongManager::SongToPreferredSortSectionName(
    const Song* pSong) const {
  for (const PreferredSortSection& v : preferred_song_sort_) {
    if (std::any_of(v.songs_.begin(), v.songs_.end(), [&](const Song* s) {
          return s == pSong;
        })) {
      return v.name_;
    }
  }
  return std::string();
}

void SongManager::GetPreferredSortSongsBySectionName(
    const std::string& sSectionName, std::vector<Song*>& AddTo) const {
  // Use m_mapPreferredSectionToSongs
  std::map<std::string, SongPointerVector>::const_iterator iter =
      map_preferred_section_to_songs_.find(sSectionName);
  if (iter != map_preferred_section_to_songs_.end()) {
    AddTo.insert(AddTo.end(), iter->second.begin(), iter->second.end());
  }
}

Group* SongManager::GetGroup(const Song* pSong) const {
  if (pSong == nullptr) {
    return nullptr;
  }
  return GetGroupFromName(pSong->group_name_);
}

Group* SongManager::GetGroupFromName(const std::string& sGroupName) const {
  auto iter = map_name_to_group_.find(sGroupName);
  if (iter != map_name_to_group_.end()) {
    return iter->second;
  }
  FOREACH_EnabledPlayer(pn) {
    Profile* prof = PROFILEMAN->GetProfile(pn);
    if (prof != nullptr) {
      for (size_t i = 0; i < prof->groups_.size(); i++) {
        Group* grp = prof->groups_[i];

        if (grp->GetGroupName() == sGroupName) {
          return grp;
        }
      }
    }
  }
  return nullptr;
}

void SongManager::GetPreferredSortCourses(
    CourseType ct, std::vector<Course*>& AddTo, bool bIncludeAutogen) const {
  if (preferred_course_sort_.empty()) {
    GetCourses(ct, AddTo, bIncludeAutogen);
    return;
  }

  for (const CoursePointerVector& v : preferred_course_sort_) {
    for (Course* pCourse : v) {
      if (pCourse->GetCourseType() == ct) {
        AddTo.push_back(pCourse);
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

std::string SongManager::ShortenGroupName(std::string sLongGroupName) {
  static TitleSubst tsub("Groups");

  TitleFields title;
  title.title_ = sLongGroupName;
  tsub.Subst(title);
  return title.title_;
}

std::string SongManager::ShortenSeriesName(std::string sLongSeriesName) {
  static TitleSubst tsub("Series");

  TitleFields title;
  title.title_ = sLongSeriesName;
  tsub.Subst(title);
  return title.title_;
}

static LocalizedString LOADING_COURSES("SongManager", "Loading courses...");
void SongManager::InitCoursesFromDisk(LoadingWindow* ld, bool onlyAdditions) {
  LOG->Trace("Loading courses.");
  if (ld) {
    ld->SetText(LOADING_COURSES);
  }

  RageTimer loading_window_last_update_time;
  loading_window_last_update_time.Touch();

  std::vector<std::string> vsCourseGroupNames;
  // Find all group directories in Courses dir
  GetDirListing(
      SpecialFiles::COURSES_DIR + "*", vsCourseGroupNames, true, true);
  StripCvsAndSvn(vsCourseGroupNames);
  StripMacResourceForks(vsCourseGroupNames);

  // Search for courses both in COURSES_DIR and in subdirectories
  vsCourseGroupNames.push_back(SpecialFiles::COURSES_DIR);
  SortRStringArray(vsCourseGroupNames);

  int courseIndex = 0;
  for (const std::string& sCourseGroup :
       vsCourseGroupNames)  // for each dir in /Courses/
  {
    // Find all CRS files in this group directory
    std::vector<std::string> vsCoursePaths;
    GetDirListing(sCourseGroup + "/*.crs", vsCoursePaths, false, true);
    SortRStringArray(vsCoursePaths);

    if (ld) {
      ld->SetIndeterminate(false);
      ld->SetTotalWork(vsCoursePaths.size());
    }

    std::string base_course_group = Basename(sCourseGroup);
    for (const std::string& sCoursePath : vsCoursePaths) {
      // Skip already loaded courses if onlyAdditions is set.
      if (onlyAdditions) {
        CourseID courseID;
        courseID.FromPath(sCoursePath);
        if (courseID.ToCourse() != nullptr) {
          continue;
        }
      }

      if (ld &&
          loading_window_last_update_time.Ago() > next_loading_window_update) {
        loading_window_last_update_time.Touch();
        ld->SetProgress(courseIndex);
        ld->SetText(
            LOADING_COURSES.GetValue() + ssprintf(
                                             "\n%s\n%s",
                                             base_course_group.c_str(),
                                             Basename(sCoursePath).c_str()));
      }

      Course* pCourse = new Course;
      CourseLoaderCRS::LoadFromCRSFile(sCoursePath, *pCourse);

      if (g_bHideIncompleteCourses.Get() && pCourse->incomplete_) {
        delete pCourse;
        continue;
      }

      courses_.push_back(pCourse);
      courseIndex++;
    }
  }

  if (ld) {
    ld->SetIndeterminate(true);
  }

  RefreshCourseGroupInfo();
}

void SongManager::InitAutogenCourses() {
  // Create group courses for Endless and Nonstop
  std::vector<std::string> saGroupNames;
  this->GetSongGroupNames(saGroupNames);
  Course* pCourse;
  for (unsigned g = 0; g < saGroupNames.size(); g++)  // foreach Group
  {
    std::string sGroupName = saGroupNames[g];

    // Generate random courses from each group.
    pCourse = new Course;
    CourseUtil::AutogenEndlessFromGroup(
        sGroupName, Difficulty_Medium, *pCourse);
    pCourse->scripter_ = "Autogen";
    courses_.push_back(pCourse);

    pCourse = new Course;
    CourseUtil::AutogenNonstopFromGroup(
        sGroupName, Difficulty_Medium, *pCourse);
    pCourse->scripter_ = "Autogen";
    courses_.push_back(pCourse);
  }

  // Generate "All Songs" endless course.
  pCourse = new Course;
  CourseUtil::AutogenEndlessFromGroup("", Difficulty_Medium, *pCourse);
  pCourse->scripter_ = "Autogen";
  courses_.push_back(pCourse);

  /* Generate Oni courses from artists. Only create courses if we have at least
   * four songs from an artist; create 3- and 4-song courses. */
  {
    /* We normally sort by translit artist. However, display artist is more
     * consistent. For example, transliterated Japanese names are alternately
     * spelled given- and family-name first, but display titles are more
     * consistent. */
    std::vector<Song*> apSongs = this->GetAllSongs();
    SongUtil::SortSongPointerArrayByDisplayArtist(apSongs);

    std::string sCurArtist = "";
    std::string sCurArtistTranslit = "";
    int iCurArtistCount = 0;

    std::vector<Song*> aSongs;
    unsigned i = 0;
    do {
      std::string sArtist = i >= apSongs.size()
                                ? std::string("")
                                : apSongs[i]->GetDisplayArtist();
      std::string sTranslitArtist = i >= apSongs.size()
                                        ? std::string("")
                                        : apSongs[i]->GetTranslitArtist();
      if (i < apSongs.size() && !CompareNoCase(sCurArtist, sArtist)) {
        aSongs.push_back(apSongs[i]);
        ++iCurArtistCount;
        continue;
      }

      /* Different artist, or we're at the end. If we have enough entries for
       * the last artist, add it. Skip blanks and "Unknown artist". */
      if (iCurArtistCount >= 3 && sCurArtistTranslit != "" &&
          CompareNoCase(sCurArtistTranslit, "Unknown artist") &&
          CompareNoCase(sCurArtist, "Unknown artist")) {
        pCourse = new Course;
        CourseUtil::AutogenOniFromArtist(
            sCurArtist, sCurArtistTranslit, aSongs, Difficulty_Hard, *pCourse);
        pCourse->scripter_ = "Autogen";
        courses_.push_back(pCourse);
      }

      aSongs.clear();

      if (i < apSongs.size()) {
        sCurArtist = sArtist;
        sCurArtistTranslit = sTranslitArtist;
        iCurArtistCount = 1;
        aSongs.push_back(apSongs[i]);
      }
    } while (i++ < apSongs.size());
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
        int iNumParams = msd.GetNumParams(i);
        const MsdFile::value_t& sParams = msd.GetValue(i);
        std::string sType = sParams[0];
        std::string sAttack = sParams[1];

        if (iNumParams > 2) {
          LuaHelpers::ReportScriptErrorFmt(
              "Got \"%s:%s\" tag with too many parameters", sType.c_str(),
              sAttack.c_str());
          continue;
        }

        if (!EqualsNoCase(sType, "ATTACK")) {
          LuaHelpers::ReportScriptErrorFmt(
              "Got \"%s:%s\" tag with wrong declaration", sType.c_str(),
              sAttack.c_str());
          continue;
        }

        GAMESTATE->random_attacks_.push_back(sAttack);
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
  std::vector<Course*> vNewCourses;
  for (std::vector<Course*>::iterator it = courses_.begin();
       it != courses_.end(); ++it) {
    if ((*it)->is_autogen_) {
      delete *it;
    } else {
      vNewCourses.push_back(*it);
    }
  }
  courses_.swap(vNewCourses);
  UpdatePopular();
  UpdateShuffled();
  RefreshCourseGroupInfo();
}

void SongManager::AddCourse(Course* pCourse) {
  courses_.push_back(pCourse);
  UpdatePopular();
  UpdateShuffled();
  map_course_group_to_info_[pCourse->group_name_];  // insert
}

void SongManager::DeleteCourse(Course* pCourse) {
  std::vector<Course*>::iterator iter =
      find(courses_.begin(), courses_.end(), pCourse);
  ASSERT(iter != courses_.end());
  courses_.erase(iter);
  UpdatePopular();
  UpdateShuffled();
  RefreshCourseGroupInfo();
}

void SongManager::InvalidateCachedTrails() {
  for (Course* pCourse : courses_) {
    if (pCourse->IsAnEdit()) {
      pCourse->trail_cache_.clear();
    }
  }
}

/* Called periodically to wipe out cached NoteData. This is called when we
 * change screens. */
void SongManager::Cleanup() {
  for (Song* pSong : shuffled_songs_) {
    if (pSong) {
      const std::vector<Steps*>& vpSteps = pSong->GetAllSteps();
      for (Steps* pSteps : vpSteps) {
        pSteps->Compress();
      }
    }
  }
}

/* Flush all Song*, Steps* and Course* caches. This is when a Song or its Steps
 * are removed or changed. This doesn't touch GAMESTATE and StageStats
 * pointers. Currently, the only time Steps are altered independently of the
 * Courses and Songs is in Edit Mode, which updates the other pointers it needs.
 */
void SongManager::Invalidate(const Song* pStaleSong) {
  // TODO: This is unnecessarily expensive.
  // Can we regenerate only the autogen courses that are affected?
  DeleteAutogenCourses();

  for (Course* c : this->courses_) {
    c->Invalidate(pStaleSong);
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
  std::vector<std::string> vsDisabledSongs;

  // Intentionally drop disabled song entries for songs that aren't currently
  // loaded.

  const std::vector<Song*>& apSongs = SONGMAN->GetAllSongs();
  for (Song* pSong : apSongs) {
    SongID sid;
    sid.FromSong(pSong);
    if (!pSong->GetEnabled()) {
      vsDisabledSongs.push_back(sid.ToString());
    }
  }
  g_sDisabledSongs.Set(join(";", vsDisabledSongs));
}

void SongManager::LoadEnabledSongsFromPref() {
  std::vector<std::string> asDisabledSongs;
  split(g_sDisabledSongs, ";", asDisabledSongs, true);

  for (const std::string& s : asDisabledSongs) {
    SongID sid;
    sid.FromString(s);
    Song* pSong = sid.ToSong();
    if (pSong) {
      pSong->SetEnabled(false);
    }
  }
}

void SongManager::GetStepsLoadedFromProfile(
    std::vector<Steps*>& AddTo, ProfileSlot slot) const {
  const std::vector<Song*>& vSongs = GetAllSongs();
  for (Song* song : vSongs) {
    song->GetStepsLoadedFromProfile(slot, AddTo);
  }
}

void SongManager::DeleteSteps(Steps* pSteps) {
  pSteps->song_->DeleteSteps(pSteps);
}

void SongManager::GetAllCourses(
    std::vector<Course*>& AddTo, bool bIncludeAutogen) const {
  for (unsigned i = 0; i < courses_.size(); i++) {
    if (bIncludeAutogen || !courses_[i]->is_autogen_) {
      AddTo.push_back(courses_[i]);
    }
  }
}

void SongManager::GetCourses(
    CourseType ct, std::vector<Course*>& AddTo, bool bIncludeAutogen) const {
  for (unsigned i = 0; i < courses_.size(); i++) {
    if (courses_[i]->GetCourseType() == ct) {
      if (bIncludeAutogen || !courses_[i]->is_autogen_) {
        AddTo.push_back(courses_[i]);
      }
    }
  }
}

void SongManager::GetCoursesInGroup(
    std::vector<Course*>& AddTo, const std::string& sCourseGroup,
    bool bIncludeAutogen) const {
  for (unsigned i = 0; i < courses_.size(); i++) {
    if (courses_[i]->group_name_ == sCourseGroup) {
      if (bIncludeAutogen || !courses_[i]->is_autogen_) {
        AddTo.push_back(courses_[i]);
      }
    }
  }
}

bool SongManager::GetExtraStageInfoFromCourse(
    bool bExtra2, std::string sPreferredGroup, Song*& pSongOut,
    Steps*& pStepsOut, StepsType stype) {
  const std::string sCourseSuffix =
      sPreferredGroup + (bExtra2 ? "/extra2.crs" : "/extra1.crs");
  std::string sCoursePath = SpecialFiles::SONGS_DIR + sCourseSuffix;

  Course course;
  CourseLoaderCRS::LoadFromCRSFile(sCoursePath, course);
  if (course.GetEstimatedNumStages() <= 0) {
    return false;
  }

  Trail* pTrail = course.GetTrail(stype);
  if (pTrail->entries_.empty()) {
    return false;
  }

  pSongOut = pTrail->entries_[0].song_;
  pStepsOut = pTrail->entries_[0].steps_;
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
    bool bExtra2, const Style* sd, Song*& pSongOut, Steps*& pStepsOut) {
  std::string sGroup = GAMESTATE->preferred_song_group_;
  if (sGroup == GROUP_ALL) {
    if (GAMESTATE->cur_song_ == nullptr) {
      // This normally shouldn't happen, but it's helpful to permit it for
      // testing.
      LuaHelpers::ReportScriptErrorFmt(
          "GetExtraStageInfo() called in GROUP_ALL, but GAMESTATE->cur_song_ "
          "== nullptr");
      GAMESTATE->cur_song_.Set(GetRandomSong());
    }
    sGroup = GAMESTATE->cur_song_->group_name_;
  }

  ASSERT_M(
      sGroup != "",
      ssprintf(
          "%p '%s' '%s'", static_cast<void*>(GAMESTATE->cur_song_.Get()),
          GAMESTATE->cur_song_ ? GAMESTATE->cur_song_->GetSongDir().c_str()
                               : "",
          GAMESTATE->cur_song_ ? GAMESTATE->cur_song_->group_name_.c_str()
                               : ""));

  // Check preferred group
  if (GetExtraStageInfoFromCourse(
          bExtra2, sGroup, pSongOut, pStepsOut, sd->steps_type_)) {
    return;
  }

  // Optionally, check the Songs folder for extra1/2.crs files.
  if (GetExtraStageInfoFromCourse(
          bExtra2, "", pSongOut, pStepsOut, sd->steps_type_)) {
    return;
  }

  // Choose a hard song for the extra stage
  Song* pExtra1Song = nullptr;  // the absolute hardest Song and Steps.  Use
                                // this for extra stage 1.
  Steps* pExtra1Notes = nullptr;
  Song* pExtra2Song =
      nullptr;  // a medium-hard Song and Steps.  Use this for extra stage 2.
  Steps* pExtra2Notes = nullptr;

  const std::vector<Song*>& apSongs = GetSongs(sGroup);
  for (unsigned s = 0; s < apSongs.size(); s++)  // foreach song
  {
    Song* pSong = apSongs[s];

    std::vector<Steps*> apSteps;
    SongUtil::GetSteps(pSong, apSteps, sd->steps_type_);
    for (unsigned n = 0; n < apSteps.size(); n++)  // foreach Steps
    {
      Steps* pSteps = apSteps[n];

      if (pExtra1Notes == nullptr ||
          CompareNotesPointersForExtra(
              pExtra1Notes, pSteps))  // pSteps is harder than pHardestNotes
      {
        pExtra1Song = pSong;
        pExtra1Notes = pSteps;
      }

      // for extra 2, we don't want to choose the hardest notes possible.  So,
      // we'll disgard Steps with meter > 8 (assuming dance)
      if (bExtra2 && pSteps->GetMeter() > EXTRA_STAGE2_DIFFICULTY_MAX) {
        continue;  // skip
      }
      if (pExtra2Notes == nullptr ||
          CompareNotesPointersForExtra(
              pExtra2Notes, pSteps))  // pSteps is harder than pHardestNotes
      {
        pExtra2Song = pSong;
        pExtra2Notes = pSteps;
      }
    }
  }

  if (pExtra2Song == nullptr && pExtra1Song != nullptr) {
    pExtra2Song = pExtra1Song;
    pExtra2Notes = pExtra1Notes;
  }

  // If there are any notes at all that match this StepsType, everything should
  // be filled out. Also, it's guaranteed that there is at least one Steps that
  // matches the StepsType because the player had to play something before
  // reaching the extra stage!
  ASSERT(pExtra2Song && pExtra1Song && pExtra2Notes && pExtra1Notes);

  pSongOut = (bExtra2 ? pExtra2Song : pExtra1Song);
  pStepsOut = (bExtra2 ? pExtra2Notes : pExtra1Notes);
}

Song* SongManager::GetRandomSong() {
  if (shuffled_songs_.empty()) {
    return nullptr;
  }

  static int i = 0;

  for (int iThrowAway = 0; iThrowAway < 100; iThrowAway++) {
    i++;
    wrap(i, shuffled_songs_.size());
    Song* pSong = shuffled_songs_[i];
    if (pSong->IsTutorial()) {
      continue;
    }
    if (!pSong->NormallyDisplayed()) {
      continue;
    }
    return pSong;
  }

  return nullptr;
}

Course* SongManager::GetRandomCourse() {
  if (shuffled_courses_.empty()) {
    return nullptr;
  }

  static int i = 0;

  for (int iThrowAway = 0; iThrowAway < 100; iThrowAway++) {
    i++;
    wrap(i, shuffled_courses_.size());
    Course* pCourse = shuffled_courses_[i];
    if (pCourse->is_autogen_ && !PREFSMAN->autogen_group_courses_) {
      continue;
    }
    if (pCourse->GetCourseType() == COURSE_TYPE_ENDLESS) {
      continue;
    }
    if (UNLOCKMAN->CourseIsLocked(pCourse)) {
      continue;
    }
    return pCourse;
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

Course* SongManager::GetCourseFromPath(std::string sPath) const {
  if (sPath == "") {
    return nullptr;
  }

  for (Course* c : courses_) {
    if (CompareNoCase(sPath, c->path_) == 0) {
      return c;
    }
  }

  return nullptr;
}

Course* SongManager::GetCourseFromName(std::string sName) const {
  if (sName == "") {
    return nullptr;
  }

  for (Course* c : courses_) {
    if (CompareNoCase(sName, c->GetDisplayFullTitle()) == 0) {
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

Song* SongManager::FindSong(std::string sPath) const {
  Replace(sPath, '\\', '/');
  std::vector<std::string> bits;
  split(sPath, "/", bits);

  if (bits.size() == 1) {
    return FindSong("", bits[0]);
  } else if (bits.size() == 2) {
    return FindSong(bits[0], bits[1]);
  }

  return nullptr;
}

Song* SongManager::FindSong(std::string sGroup, std::string sSong) const {
  // foreach song
  const std::vector<Song*>& vSongs =
      GetSongs(sGroup.empty() ? GROUP_ALL : sGroup);
  for (Song* s : vSongs) {
    if (s->Matches(sGroup, sSong)) {
      return s;
    }
  }

  return nullptr;
}

Course* SongManager::FindCourse(std::string sPath) const {
  Replace(sPath, '\\', '/');
  std::vector<std::string> bits;
  split(sPath, "/", bits);

  if (bits.size() == 1) {
    return FindCourse("", bits[0]);
  } else if (bits.size() == 2) {
    return FindCourse(bits[0], bits[1]);
  }

  return nullptr;
}

Course* SongManager::FindCourse(std::string sGroup, std::string sName) const {
  for (Course* c : courses_) {
    if (c->Matches(sGroup, sName)) {
      return c;
    }
  }

  return nullptr;
}

void SongManager::UpdatePopular() {
  // update players best
  std::vector<Song*> apBestSongs = songs_;
  for (unsigned j = 0; j < apBestSongs.size(); ++j) {
    bool bFiltered = false;
    // Filter out locked songs.
    if (!apBestSongs[j]->NormallyDisplayed()) {
      bFiltered = true;
    }
    if (!bFiltered) {
      continue;
    }

    // Remove it.
    std::swap(apBestSongs[j], apBestSongs.back());
    apBestSongs.erase(apBestSongs.end() - 1);
    --j;
  }

  SongUtil::SortSongPointerArrayByTitle(apBestSongs);

  std::vector<Course*> apBestCourses[NUM_CourseType];
  FOREACH_ENUM(CourseType, ct) {
    GetCourses(ct, apBestCourses[ct], PREFSMAN->autogen_group_courses_);
    CourseUtil::SortCoursePointerArrayByTitle(apBestCourses[ct]);
  }

  popular_songs_ = apBestSongs;
  SongUtil::SortSongPointerArrayByNumPlays(
      popular_songs_, ProfileSlot_Machine, true);

  FOREACH_CourseType(ct) {
    std::vector<Course*>& vpCourses = popular_courses_[ct];
    vpCourses = apBestCourses[ct];
    CourseUtil::SortCoursePointerArrayByNumPlays(
        vpCourses, ProfileSlot_Machine, true);
  }
}

std::map<int, std::vector<Song*>> SongManager::UpdateMeterSort(
    std::vector<Song*> songs) {
  // Empty the map
  map_songs_by_difficulty_.clear();
  std::vector<Song*> apDifficultSongs = songs;
  // For each song, for each step
  for (unsigned i = 0; i < apDifficultSongs.size(); ++i) {
    std::vector<Steps*> vpSteps;
    SongUtil::GetPlayableSteps(apDifficultSongs[i], vpSteps);
    for (unsigned j = 0; j < vpSteps.size(); ++j) {
      Steps* pSteps = vpSteps[j];
      // Check if the meter is already in m_mapSongsByDifficulty
      if (std::find(
              map_songs_by_difficulty_[pSteps->GetMeter()].begin(),
              map_songs_by_difficulty_[pSteps->GetMeter()].end(),
              apDifficultSongs[i]) !=
          map_songs_by_difficulty_[pSteps->GetMeter()].end()) {
        continue;
      } else {
        map_songs_by_difficulty_[pSteps->GetMeter()].push_back(
            apDifficultSongs[i]);
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
    std::map<std::string, std::vector<Song*>> mapSectionToSongs) {
  ASSERT(UNLOCKMAN != nullptr);
  preferred_song_sort_.clear();
  map_preferred_section_to_songs_.clear();

  PreferredSortSection section;
  for (const std::pair<const std::string, std::vector<Song*>>& pair :
       mapSectionToSongs) {
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
    PreferredSortSection PFSection;
    PFSection.name_ = "Unlocks";
    for (const UnlockEntry& ue : UNLOCKMAN->unlock_entries_) {
      if (ue.type_ == UnlockRewardType_Song) {
        Song* pSong = ue.song_.ToSong();
        if (pSong) {
          PFSection.songs_.push_back(pSong);
        }
      }
    }

    preferred_song_sort_.push_back(PFSection);
    map_preferred_section_to_songs_[PFSection.name_] = PFSection.songs_;
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
    std::string sPreferredSongs, bool bIsAbsolute) {
  ASSERT(UNLOCKMAN != nullptr);

  preferred_song_sort_.clear();
  map_preferred_section_to_songs_.clear();
  std::vector<std::string> asLines;
  std::string sFile = sPreferredSongs;
  if (!bIsAbsolute) {
    sFile = THEME->GetPathO("SongManager", sPreferredSongs);
  }
  GetFileContents(sFile, asLines);
  if (asLines.empty()) {
    return;
  }

  PreferredSortSection section;

  for (std::string sLine : asLines) {
    bool bSectionDivider = BeginsWith(sLine, "---");
    if (bSectionDivider) {
      if (!section.songs_.empty()) {
        preferred_song_sort_.push_back(section);
        map_preferred_section_to_songs_[section.name_] = section.songs_;
        section = PreferredSortSection();
      }

      section.name_ =
          Right(sLine, sLine.length() - std::string("---").length());
      TrimLeft(section.name_);
      TrimRight(section.name_);
    } else {
      /* if the line ends in slash-star, check if the section exists,
       * and if it does, add all the songs in that group to the list. */
      if (EndsWith(sLine, "/*")) {
        std::string group =
            Left(sLine, sLine.length() - std::string("/*").length());
        if (DoesSongGroupExist(group)) {
          // add all songs in group
          const std::vector<Song*>& vSongs = GetSongs(group);
          for (Song* song : vSongs) {
            if (UNLOCKMAN->SongIsLocked(song) & LOCKED_SELECTABLE) {
              continue;
            }
            section.songs_.push_back(song);
          }
        }
      }

      Song* pSong = FindSong(sLine);
      if (pSong == nullptr) {
        continue;
      }
      if (UNLOCKMAN->SongIsLocked(pSong) & LOCKED_SELECTABLE) {
        continue;
      }
      section.songs_.push_back(pSong);
    }
  }

  if (!section.songs_.empty()) {
    preferred_song_sort_.push_back(section);
    map_preferred_section_to_songs_[section.name_] = section.songs_;
    section = PreferredSortSection();
  }

  if (MOVE_UNLOCKS_TO_BOTTOM_OF_PREFERRED_SORT.GetValue()) {
    // move all unlock songs to a group at the bottom
    PreferredSortSection PFSection;
    PFSection.name_ = "Unlocks";
    for (const UnlockEntry& ue : UNLOCKMAN->unlock_entries_) {
      if (ue.type_ == UnlockRewardType_Song) {
        Song* pSong = ue.song_.ToSong();
        if (pSong) {
          PFSection.songs_.push_back(pSong);
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

    preferred_song_sort_.push_back(PFSection);
    map_preferred_section_to_songs_[PFSection.name_] = PFSection.songs_;
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
    std::string sPreferredCourses, bool bIsAbsolute) {
  ASSERT(UNLOCKMAN != nullptr);

  preferred_course_sort_.clear();

  std::vector<std::string> asLines;
  std::string sFile = sPreferredCourses;
  if (!bIsAbsolute) {
    sFile = THEME->GetPathO("SongManager", sPreferredCourses);
  }
  if (!GetFileContents(sFile, asLines)) {
    return;
  }

  std::vector<Course*> vpCourses;

  for (std::string sLine : asLines) {
    bool bSectionDivider = BeginsWith(sLine, "---");
    if (bSectionDivider) {
      if (!vpCourses.empty()) {
        preferred_course_sort_.push_back(vpCourses);
        vpCourses.clear();
      }
      continue;
    }

    Course* pCourse = FindCourse(sLine);
    if (pCourse == nullptr) {
      continue;
    }
    if (UNLOCKMAN->CourseIsLocked(pCourse) & LOCKED_SELECTABLE) {
      continue;
    }
    vpCourses.push_back(pCourse);
  }

  if (!vpCourses.empty()) {
    preferred_course_sort_.push_back(vpCourses);
    vpCourses.clear();
  }

  if (MOVE_UNLOCKS_TO_BOTTOM_OF_PREFERRED_SORT.GetValue()) {
    // move all unlock Courses to a group at the bottom
    std::vector<Course*> vpUnlockCourses;
    for (const UnlockEntry& ue : UNLOCKMAN->unlock_entries_) {
      if (ue.type_ == UnlockRewardType_Course) {
        if (ue.course_.IsValid()) {
          vpUnlockCourses.push_back(ue.course_.ToCourse());
        }
      }
    }

    for (auto v = preferred_course_sort_.begin();
         v != preferred_course_sort_.end(); ++v) {
      for (int i = v->size() - 1; i >= 0; i--) {
        Course* pCourse = (*v)[i];
        if (find(vpUnlockCourses.begin(), vpUnlockCourses.end(), pCourse) !=
            vpUnlockCourses.end()) {
          v->erase(v->begin() + i);
        }
      }
    }

    preferred_course_sort_.push_back(vpUnlockCourses);
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
    std::string sPreferredSongs, std::string sPreferredCourses) {
  SetPreferredSongs(sPreferredSongs);
  SetPreferredCourses(sPreferredCourses);
}

void SongManager::SortSongs() { SongUtil::SortSongPointerArrayByTitle(songs_); }

void SongManager::UpdateRankingCourses() {
  /* Updating the ranking courses data is fairly expensive since it involves
   * comparing strings. Do so sparingly. */
  std::vector<std::string> RankingCourses;
  split(
      THEME->GetMetric("ScreenRanking", "CoursesToShow"), ",", RankingCourses);

  for (Course* c : courses_) {
    bool bLotsOfStages = c->GetEstimatedNumStages() > 7;
    c->sort_order_ranking_ = bLotsOfStages ? 3 : 2;

    for (unsigned j = 0; j < RankingCourses.size(); j++) {
      if (!CompareNoCase(RankingCourses[j], c->path_)) {
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
    const std::string& sProfileDir, ProfileSlot slot) {
  // Load all edit steps
  std::string sDir = sProfileDir + EDIT_STEPS_SUBDIR;
  SSCLoader loaderSSC;
  SMLoader loaderSM;
  int iNumEditsLoaded = GetNumEditsLoadedFromProfile(slot);

  // Pass 1: Flat folder (old style)
  std::vector<std::string> vsFiles;
  int size = std::min(
      (int)vsFiles.size(), MAX_EDIT_STEPS_PER_PROFILE - iNumEditsLoaded);
  GetDirListing(sDir + "*.edit", vsFiles, false, true);

  // XXX: If some edits are invalid and they're close to the edit limit, this
  // may erroneously skip some edits, and won't warn.
  for (int i = 0; i < size; i++) {
    std::string fn = vsFiles[i];
    bool bLoadedFromSSC = loaderSSC.LoadEditFromFile(fn, slot, true);
    // If we don't load the edit from a .ssc-style .edit, then we should
    // also try the .sm-style edit file. -aj
    if (!bLoadedFromSSC) {
      loaderSM.LoadEditFromFile(fn, slot, true);
    }
  }

  if ((int)vsFiles.size() > MAX_EDIT_STEPS_PER_PROFILE - iNumEditsLoaded) {
    LuaHelpers::ReportScriptErrorFmt(
        "Profile %s has too many edits; some have been skipped.",
        ProfileSlotToString(slot).c_str());
    return;
  }

  // Some .edit files may have been invalid, so re-query instead of just +=
  // size.
  iNumEditsLoaded = GetNumEditsLoadedFromProfile(slot);

  // Pass 2: Group and song folders with #SONG inferred from folder (optional
  // new style)
  std::vector<std::string> vsGroups;
  GetDirListing(sDir + "*", vsGroups, true, false);

  // XXX: Same as above, edits may be skipped in error in some cases
  for (unsigned i = 0; i < vsGroups.size(); i++) {
    std::string sGroupDir = vsGroups[i] + "/";
    std::vector<std::string> vsSongs;
    GetDirListing(sDir + sGroupDir + "*", vsSongs, true, false);

    for (unsigned j = 0; j < vsSongs.size(); j++) {
      std::vector<std::string> vsEdits;
      std::string sSongDir = sGroupDir + vsSongs[j] + "/";
      // XXX There doesn't appear to be a songdir const?
      Song* given = GetSongFromDir("/Songs/" + sSongDir);
      // NOTE: We don't have to check the return value of GetSongFromDir here,
      // because if it fails, it returns nullptr, which is then passed to
      // NotesLoader*.LoadEditFromFile(), which will interpret that as "we
      // couldn't infer the song from the path", which is what we want in that
      // case anyway.
      GetDirListing(sDir + sSongDir + "/*.edit", vsEdits, false, true);
      size = std::min(
          (int)vsEdits.size(), MAX_EDIT_STEPS_PER_PROFILE - iNumEditsLoaded);

      for (int k = 0; k < size; k++) {
        std::string fn = vsEdits[k];
        bool bLoadedFromSSC = loaderSSC.LoadEditFromFile(fn, slot, true, given);
        // And try again with SM
        if (!bLoadedFromSSC) {
          loaderSM.LoadEditFromFile(fn, slot, true, given);
        }
      }

      if ((int)vsEdits.size() > MAX_EDIT_STEPS_PER_PROFILE - iNumEditsLoaded) {
        LuaHelpers::ReportScriptErrorFmt(
            "Profile %s has too many edits; some have been skipped.",
            ProfileSlotToString(slot).c_str());
        return;
      }

      // Some .edit files may have been invalid, so re-query instead of just +=
      // size.
      iNumEditsLoaded = GetNumEditsLoadedFromProfile(slot);
    }
  }
}

void SongManager::LoadCourseEditsFromProfileDir(
    const std::string& sProfileDir, ProfileSlot slot) {
  // Load all edit courses
  std::string sDir = sProfileDir + EDIT_COURSES_SUBDIR;

  std::vector<std::string> vsFiles;
  GetDirListing(sDir + "*.crs", vsFiles, false, true);

  int iNumEditsLoaded = GetNumEditsLoadedFromProfile(slot);
  int size = std::min(
      (int)vsFiles.size(), MAX_EDIT_COURSES_PER_PROFILE - iNumEditsLoaded);

  for (int i = 0; i < size; i++) {
    std::string fn = vsFiles[i];

    CourseLoaderCRS::LoadEditFromFile(fn, slot);
  }
}

int SongManager::GetNumEditsLoadedFromProfile(ProfileSlot slot) const {
  int iCount = 0;
  for (unsigned s = 0; s < songs_.size(); s++) {
    const Song* pSong = songs_[s];
    std::vector<Steps*> apSteps;
    SongUtil::GetSteps(pSong, apSteps);

    for (unsigned i = 0; i < apSteps.size(); ++i) {
      const Steps* pSteps = apSteps[i];
      if (pSteps->WasLoadedFromProfile() &&
          pSteps->GetLoadedFromProfileSlot() == slot) {
        ++iCount;
      }
    }
  }
  return iCount;
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
  std::vector<Course*> apToDelete;
  for (Course* pCourse : courses_) {
    if (pCourse->GetLoadedFromProfileSlot() == ProfileSlot_Invalid) {
      continue;
    }

    if (slot == ProfileSlot_Invalid ||
        pCourse->GetLoadedFromProfileSlot() == slot) {
      apToDelete.push_back(pCourse);
    }
  }

  /* We don't use DeleteCourse here, so we don't UpdatePopular and
   * UpdateShuffled repeatedly. */
  for (unsigned i = 0; i < apToDelete.size(); ++i) {
    std::vector<Course*>::iterator iter =
        find(courses_.begin(), courses_.end(), apToDelete[i]);
    ASSERT(iter != courses_.end());
    courses_.erase(iter);
    delete apToDelete[i];
  }

  // Popular and Shuffled may refer to courses that we just freed.
  UpdatePopular();
  UpdateShuffled();
  RefreshCourseGroupInfo();

  // Free profile steps.
  std::set<Steps*> setInUse;
  if (STATSMAN) {
    STATSMAN->GetStepsInUse(setInUse);
  }
  for (Song* s : songs_) {
    s->FreeAllLoadedFromProfile(slot, &setInUse);
  }
}

int SongManager::GetNumStepsLoadedFromProfile() {
  int iCount = 0;
  for (const Song* s : songs_) {
    std::vector<Steps*> vpAllSteps = s->GetAllSteps();

    iCount += std::count_if(
        vpAllSteps.begin(), vpAllSteps.end(), [](const Steps* step) {
          return step->GetLoadedFromProfileSlot() != ProfileSlot_Invalid;
        });
  }

  return iCount;
}

int SongManager::GetSongRank(Song* pSong) {
  const int index =
      FindIndex(popular_songs_.begin(), popular_songs_.end(), pSong);
  return index;  // -1 means we didn't find it
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the SongManager. */
class LunaSongManager : public Luna<SongManager> {
 public:
  static int SetPreferredSongs(T* p, lua_State* L) {
    std::string sPreferredSongs = SArg(1);
    if (lua_gettop(L) >= 2 && !lua_isnil(L, 2)) {
      p->SetPreferredSongs(sPreferredSongs, BArg(2));
    } else {
      p->SetPreferredSongs(sPreferredSongs);
    }

    COMMON_RETURN_SELF;
  }

  static int SetPreferredSongsFromTable(T* p, lua_State* L) {
    if (!lua_istable(L, 1)) {
      return luaL_error(
          L, "Expected a table for SetPreferredSongsFromTable. Got %s.",
          luaL_typename(L, 1));
    }

    std::map<std::string, std::vector<Song*>> mapSectionToSongs;
    lua_pushnil(L);  // first key
    while (lua_next(L, 1) != 0) {
      std::string sectionName = SArg(-2);
      std::vector<Song*> songs;

      if (lua_istable(L, -1)) {
        lua_pushnil(L);  // first key in the inner table
        while (lua_next(L, -2) != 0) {
          // value is at index -1
          Song* pSong = Luna<Song>::check(L, -1);
          if (pSong) {
            songs.push_back(pSong);
          }
          lua_pop(L, 1);  // remove value, keep key for next iteration
        }
      }

      mapSectionToSongs[sectionName] = songs;
      lua_pop(L, 1);  // remove value, keep key for next iteration
    }

    p->SetPreferredSongsFromTable(mapSectionToSongs);
    COMMON_RETURN_SELF;
  }

  static int SetPreferredCourses(T* p, lua_State* L) {
    std::string sPreferredCourses = SArg(1);
    if (lua_gettop(L) >= 2 && !lua_isnil(L, 2)) {
      p->SetPreferredSongs(sPreferredCourses, BArg(2));
    } else {
      p->SetPreferredSongs(sPreferredCourses);
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
    Song* pS = p->FindSong(SArg(1));
    if (pS) {
      pS->PushSelf(L);
    } else {
      lua_pushnil(L);
    }
    return 1;
  }
  static int FindCourse(T* p, lua_State* L) {
    Course* pC = p->FindCourse(SArg(1));
    if (pC) {
      pC->PushSelf(L);
    } else {
      lua_pushnil(L);
    }
    return 1;
  }
  static int GetRandomSong(T* p, lua_State* L) {
    Song* pS = p->GetRandomSong();
    if (pS) {
      pS->PushSelf(L);
    } else {
      lua_pushnil(L);
    }
    return 1;
  }
  static int GetRandomCourse(T* p, lua_State* L) {
    Course* pC = p->GetRandomCourse();
    if (pC) {
      pC->PushSelf(L);
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
    Song* pSong = nullptr;
    if (lua_isnil(L, 1)) {
      pSong = nullptr;
    } else {
      Steps* pSteps = Luna<Steps>::check(L, 1);
      pSong = pSteps->song_;
    }
    if (pSong) {
      pSong->PushSelf(L);
    } else {
      lua_pushnil(L);
    }
    return 1;
  }

  static int GetExtraStageInfo(T* p, lua_State* L) {
    bool bExtra2 = BArg(1);
    const Style* pStyle = Luna<Style>::check(L, 2);
    Song* pSong;
    Steps* pSteps;

    p->GetExtraStageInfo(bExtra2, pStyle, pSong, pSteps);
    pSong->PushSelf(L);
    pSteps->PushSelf(L);

    return 2;
  }
  DEFINE_METHOD(GetSongColor, GetSongColor(Luna<Song>::check(L, 1)))
  DEFINE_METHOD(GetSongGroupColor, GetSongGroupColor(SArg(1)))
  DEFINE_METHOD(GetCourseColor, GetCourseColor(Luna<Course>::check(L, 1)))

  static int GetSongRank(T* p, lua_State* L) {
    Song* pSong = Luna<Song>::check(L, 1);
    int index = p->GetSongRank(pSong);
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
    Song* pSong = Luna<Song>::check(L, 1);
    Group* pGroup = p->GetGroup(pSong);
    if (pGroup != nullptr) {
      pGroup->PushSelf(L);
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
    const Song* pSong = Luna<Song>::check(L, 1);
    lua_pushstring(L, p->SongToPreferredSortSectionName(pSong).c_str());
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
