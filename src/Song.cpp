#include "Song.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <ctime>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ActorUtil.h"
#include "BackgroundUtil.h"
#include "CommonMetrics.h"
#include "Difficulty.h"
#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "GameManager.h"
#include "GameState.h"
#include "Group.h"
#include "ImageCache.h"
#include "LuaManager.h"
#include "LyricsLoader.h"
#include "NoteData.h"
#include "NoteDataUtil.h"
#include "NotesLoader.h"
#include "NotesLoaderSM.h"
#include "NotesLoaderSSC.h"
#include "NotesWriterSM.h"
#include "NotesWriterSSC.h"
#include "Preference.h"
#include "PrefsManager.h"
#include "ProfileManager.h"
#include "RageFile.h"
#include "RageFileManager.h"
#include "RageLog.h"
#include "RageSoundReader.h"
#include "RageSoundReader_FileReader.h"
#include "RageSurface.h"
#include "RageSurface_Load.h"
#include "RageTextureID.h"
#include "RageTextureManager.h"
#include "RageThreads.h"
#include "RageUtil.h"
#include "RageUtil_AutoPtr.h"
#include "SongCacheBinary.h"
#include "SongCacheIndex.h"
#include "SongManager.h"
#include "SongUtil.h"
#include "Sprite.h"
#include "StdString.h"
#include "Steps.h"
#include "StepsUtil.h"
#include "Style.h"
#include "TitleSubstitution.h"
#include "UnlockManager.h"
#include "global.h"

/**
 * @brief The internal version of the cache for StepMania.
 *
 * Increment this value to invalidate the current cache. */
const int FILE_CACHE_VERSION = 234;

/** @brief How long does a song sample last by default? */
const float DEFAULT_MUSIC_SAMPLE_LENGTH = 12.f;

static Preference<float> g_fLongVerSongSeconds("LongVerSongSeconds", 60 * 2.5f);
static Preference<float> g_fMarathonVerSongSeconds(
    "MarathonVerSongSeconds", 60 * 5.f);
static Preference<bool> g_BackUpAllSongSaves("BackUpAllSongSaves", false);

static std::vector<std::string> GetImageCacheDirs() {
  std::vector<std::string> image_dirs;
  split(CommonMetrics::IMAGES_TO_CACHE, ",", image_dirs);
  return image_dirs;
}

Song::Song() {
  FOREACH_BackgroundLayer(i) background_changes_[i] =
      AutoPtrCopyOnWrite<VBackgroundChange>(new VBackgroundChange);
  foreground_changes_ =
      AutoPtrCopyOnWrite<VBackgroundChange>(new VBackgroundChange);

  loaded_from_profile_ = ProfileSlot_Invalid;
  version_ = STEPFILE_VERSION_NUMBER;
  music_sample_start_seconds_ = -1;
  music_sample_length_seconds_ = DEFAULT_MUSIC_SAMPLE_LENGTH;
  music_length_seconds_ = 0;
  first_second_ = -1;
  last_second_ = -1;
  specified_last_second_ = -1;
  selection_display_ = SHOW_ALWAYS;
  enabled_ = true;
  display_bpm_type_ = DISPLAY_BPM_ACTUAL;
  specified_bpm_min_ = 0;
  specified_bpm_max_ = 0;
  has_music_ = false;
  has_banner_ = false;
  has_background_ = false;
  loaded_from_autosave_ = false;
}

Song::~Song() {
  for (Steps* s : steps_) {
    RageUtil::SafeDelete(s);
  }
  steps_.clear();
  for (Steps* s : unknown_style_steps_) {
    RageUtil::SafeDelete(s);
  }
  unknown_style_steps_.clear();

  // It's the responsibility of the owner of this Song to make sure
  // that all pointers to this Song and its Steps are invalidated.
}

void Song::DetachSteps() {
  steps_.clear();
  FOREACH_ENUM(StepsType, st)
  steps_by_type_[st].clear();
  unknown_style_steps_.clear();
}

float Song::GetFirstSecond() const {
  return GetFirstSecondNoOffset() -
         GAMESTATE->song_options_.GetCurrent().music_rate_ *
             PREFSMAN->global_offset_seconds_;
}

float Song::GetFirstBeat() const {
  return this->song_timing_.GetBeatFromElapsedTimeNoOffset(this->first_second_);
}

float Song::GetLastSecond() const {
  return GetLastSecondNoOffset() -
         GAMESTATE->song_options_.GetCurrent().music_rate_ *
             PREFSMAN->global_offset_seconds_;
}

float Song::GetLastBeat() const {
  return this->song_timing_.GetBeatFromElapsedTimeNoOffset(this->last_second_);
}

float Song::GetSpecifiedLastBeat() const {
  return this->song_timing_.GetBeatFromElapsedTimeNoOffset(
      this->specified_last_second_);
}

float Song::GetFirstSecondNoOffset() const { return this->first_second_; }

float Song::GetLastSecondNoOffset() const { return this->last_second_; }

float Song::GetSpecifiedLastSecond() const {
  return this->specified_last_second_;
}

void Song::SetFirstSecondNoOffset(const float f) { this->first_second_ = f; }

void Song::SetLastSecondNoOffset(const float f) { this->last_second_ = f; }

void Song::SetSpecifiedLastSecond(const float f) {
  this->specified_last_second_ = f;
}

// Reset to an empty song.
void Song::Reset() {
  for (Steps* s : steps_) {
    RageUtil::SafeDelete(s);
  }
  steps_.clear();
  FOREACH_ENUM(StepsType, st)
  steps_by_type_[st].clear();
  for (Steps* s : unknown_style_steps_) {
    RageUtil::SafeDelete(s);
  }
  unknown_style_steps_.clear();

  Song empty;
  *this = empty;

  // It's the responsibility of the owner of this Song to make sure
  // that all pointers to this Song and its Steps are invalidated.
}

void Song::AddBackgroundChange(BackgroundLayer layer, BackgroundChange seg) {
  // Delete old background change at this start beat, if any.
  auto& changes = GetBackgroundChanges(layer);
  for (std::vector<BackgroundChange>::iterator bgc = changes.begin();
       bgc != changes.end(); ++bgc) {
    if (bgc->start_beat_ == seg.start_beat_) {
      GetBackgroundChanges(layer).erase(bgc);
      break;
    }
  }

  ASSERT(layer >= 0 && layer < NUM_BackgroundLayer);
  BackgroundUtil::AddBackgroundChange(GetBackgroundChanges(layer), seg);
}

void Song::AddForegroundChange(BackgroundChange seg) {
  BackgroundUtil::AddBackgroundChange(GetForegroundChanges(), seg);
}

void Song::AddLyricSegment(LyricSegment seg) { lyric_segments_.push_back(seg); }

Steps* Song::CreateSteps() {
  Steps* steps = new Steps(this);
  InitSteps(steps);
  return steps;
}

void Song::InitSteps(Steps* steps) {
  // TimingData is initially empty (i.e. defaults to song timing)
  steps->SetDisplayBPM(this->display_bpm_type_);
  steps->SetMinBPM(this->specified_bpm_min_);
  steps->SetMaxBPM(this->specified_bpm_max_);
}

void Song::GetDisplayBpms(DisplayBpms& add_to) const {
  if (display_bpm_type_ == DISPLAY_BPM_SPECIFIED) {
    add_to.Add(specified_bpm_min_);
    add_to.Add(specified_bpm_max_);
  } else {
    float min_bpm, max_bpm;
    song_timing_.GetActualBPM(min_bpm, max_bpm);
    add_to.Add(min_bpm);
    add_to.Add(max_bpm);
  }
}

std::string Song::GetCacheFilePath() const {
  return SongCacheIndex::GetCacheFilePath("Songs", song_dir_);
}

// Get the path to the simfile containing this song's data.
const std::string& Song::GetSongFilePath() const {
  ASSERT_M(
      !song_file_name_.empty(),
      ssprintf(
          "The song %s has no filename associated with it!",
          this->main_title_.c_str()));
  return song_file_name_;
}

void Song::SetSongDirAndGroup(
    const std::string& dir, ProfileSlot from_profile) {
  ASSERT_M(Right(dir, 1) == "/", dir);
  song_dir_ = dir;

  std::vector<std::string> directory_parts;
  split(song_dir_, "/", directory_parts, false);
  song_name_ = directory_parts[directory_parts.size() - 2];
  ASSERT(song_name_ != "");
  // save group name
  if (from_profile == ProfileSlot_Invalid) {
    ASSERT(directory_parts.size() >= 4); /* e.g. "/Songs/Slow/Taps/" */
    group_name_ =
        directory_parts[directory_parts.size() - 3];  // second from last item
    ASSERT(group_name_ != "");
  } else {
    LOG->Trace("Loading song from profile2.");
    group_name_ =
        PROFILEMAN->GetProfile(from_profile)->GetCustomSongsGroupNamePrefix();

    bool is_sub_folder = directory_parts.size() > 6;
    if (is_sub_folder) {
      std::string sub_folder_name = directory_parts[directory_parts.size() - 3];
      group_name_ += (" - " + sub_folder_name);
    }
  }
  loaded_from_profile_ = from_profile;
}

/* If PREFSMAN->m_bFastLoad is true, always load from cache if possible.
 * Don't read the contents of sDir if we can avoid it. That means we can't call
 * HasMusic(), HasBanner() or GetHashForDirectory().
 * If true, check the directory hash and reload the song from scratch if it's
 * changed.
 */
bool Song::LoadFromSongDir(
    std::string dir, bool load_autosave, ProfileSlot from_profile) {
  ASSERT_M(dir != "", "Songs can't be loaded from an empty directory!");
  ASSERT_M(
      steps_.empty() && unknown_style_steps_.empty(),
      "LoadFromSongDir expects an empty Song; call Reset() first.");

  // make sure there is a trailing slash at the end of sDir
  if (Right(dir, 1) != "/") {
    dir += "/";
  }

  SetSongDirAndGroup(dir, from_profile);

  bool use_cache = loaded_from_profile_ == ProfileSlot_Invalid;

  std::string cache_file_path;
  if (loaded_from_profile_ == ProfileSlot_Invalid) {
    // First, look in the cache for this song (without loading NoteData)
    unsigned cache_hash = SONGINDEX->GetCacheHash(song_dir_);
    cache_file_path = GetCacheFilePath();

    if (!DoesFileExist(cache_file_path)) {
      use_cache = false;
    } else if (
        !PREFSMAN->fast_load_ && GetHashForDirectory(song_dir_) != cache_hash) {
      use_cache = false;
    }  // this cache is out of date
    else if (load_autosave) {
      use_cache = false;
    }
  }

  if (use_cache) {
    bool loaded_from_cache = SongCacheBinary::Read(*this, cache_file_path);

    // If cache loading failed, fall back to parsing the source files.
    if (!loaded_from_cache) {
      LOG->Warn(
          "Couldn't load cache for '%s'; falling back to source simfiles.",
          song_dir_.c_str());
      // Read() can fail after partially filling the song, so discard it and
      // rebuild the location fields from the inputs before parsing source.
      Reset();
      SetSongDirAndGroup(dir, from_profile);
      use_cache = false;
    }

    if (use_cache &&
        (main_title_ == "" || (music_file_ == "" && keysound_file_.empty()))) {
      LOG->Warn(
          "Main title or music file for '%s' came up blank, forced to fall "
          "back on TidyUpData to fix title and paths.  Do not use # or ; in a "
          "song title.",
          song_dir_.c_str());
      // Tell TidyUpData that it's not loaded from the cache because it needs
      // to hit the song folder to find the files that weren't found. -Kyz
      TidyUpData(false);
    }
  }

  if (!use_cache) {
    std::set<std::string> blacklisted_images;

    // There was no entry in the cache for this song, or it was out of date.
    // Let's load it from a file, then write a cache entry.

    if (!NotesLoader::LoadFromDir(
            dir, *this, blacklisted_images, load_autosave)) {
      LOG->UserLog("Song", dir, "has no SSC, SM, SMA, or DWI files.");

      std::vector<std::string> audios;
      FILEMAN->GetDirListingWithMultipleExtensions(
          dir, ActorUtil::GetTypeExtensionList(FT_Sound), audios);
      bool has_music = !audios.empty();

      if (!has_music) {
        LOG->UserLog(
            "Song", dir,
            "has no music file either. Ignoring this song directory.");
        return false;
      }
      // Make sure we have a future filename figured out.
      std::vector<std::string> folders;
      split(dir, "/", folders);
      std::string song_name = folders[2] + ".ssc";
      this->song_file_name_ = dir + song_name;
      // Continue on with a blank Song so that people can make adjustments using
      // the editor.
    }
    // If edits are not cached, looking for them causes a substantial hit to
    // loading time. -Kyz
    LoadEditsFromSongDir(dir);

    TidyUpData(false, blacklisted_images);
    // Don't save a cache file if the autosave is being loaded, because the
    // cache file would contain the autosave filename. -Kyz
    // Songs loaded from removable profile are never cached, on the
    // assumption that they'll change frequently and have invalid cache
    // entries. -Kyz
    if (!load_autosave && loaded_from_profile_ == ProfileSlot_Invalid) {
      // save a cache file so we don't have to parse it all over again next time
      if (!SaveToCacheFile()) {
        cache_file_path = std::string();
      }
    }
  }

  if (loaded_from_profile_ != ProfileSlot_Invalid) {
    if (music_length_seconds_ > PREFSMAN->custom_songs_max_seconds_) {
      LOG->Trace("Custom song %s is too long.", song_dir_.c_str());
      return false;
    }
    RageFile music;
    if (!music.Open(GetMusicPath(), RageFile::READ)) {
      LOG->Trace("Custom song %s could not open music.", song_dir_.c_str());
      return false;
    }
    if (music.GetFileSize() > PREFSMAN->custom_songs_max_megabytes_ * 1000000) {
      LOG->Trace("Custom song %s music file is too big.", song_dir_.c_str());
      return false;
    }
    pre_customify_song_dir_ = song_dir_;
    song_dir_ = custom_songify_path(song_dir_);
    banner_file_.clear();
    jacket_file_.clear();
    cd_file_.clear();
    disc_file_.clear();
    lyrics_file_.clear();
    background_file_.clear();
    cd_title_file_.clear();
  }

  // Normally TidyUpData would handle setting the m_fBeat0GroupOffsetInSeconds.
  // That is to keep the song start and end times become inaccurate in some
  // cases. SInce there are cases where TidyUpData isn't called, we still want
  // to guarantee the m_fBeat0GroupOffsetInSeconds is always set so we do that
  // here.
  float offset = PREFSMAN->default_sync_offset_ == SyncOffset_NULL ? 0 : -0.009;
  if (SONGMAN->GetGroupFromName(group_name_) != nullptr) {
    offset = SONGMAN->GetGroupFromName(group_name_)->GetSyncOffset();
  }
  song_timing_.beat0_group_offset_in_seconds_ = offset;

  for (Steps* s : steps_) {
    s->timing_.beat0_group_offset_in_seconds_ = offset;
    if (loaded_from_profile_ != ProfileSlot_Invalid) {
      s->ChangeFilenamesForCustomSong();
    }
    /* Compress all Steps. During initial caching, this will remove cached
     * NoteData; during cached loads, this will just remove cached SMData. */
    s->Compress();
  }

  // Load the cached Images, if it's not loaded already.
  if (PREFSMAN->image_cache_ == IMGCACHE_LOW_RES_PRELOAD) {
    for (const std::string& image : GetImageCacheDirs()) {
      IMAGECACHE->LoadImage(image, GetCacheFile(image));
    }
  }

  // Add AutoGen pointers. (These aren't cached.)
  AddAutoGenNotes();

  if (!has_music_) {
    LOG->UserLog("Song", dir, "has no music; ignored.");
    return false;  // don't load this song
  }
  return true;  // do load this song
}

/* This function feels EXTREMELY hacky - copying things on top of pointers so
 * they don't break elsewhere.  Maybe it could be rewritten to politely ask the
 * Song/Steps objects to reload themselves. -- djpohly */
bool Song::ReloadFromSongDir(std::string dir) {
  // Remove the cache file to force the song to reload from its dir instead
  // of loading from the cache. -Kyz
  FILEMAN->Remove(GetCacheFilePath());
  // Also invalidate the cache entry so the next load sees the previous removal.
  FILEMAN->FlushDirCache(Dirname(GetCacheFilePath()));

  RemoveAutoGenNotes();
  std::vector<Steps*> v_old_steps = steps_;

  Song copy;
  if (!copy.LoadFromSongDir(dir)) {
    return false;
  }
  copy.RemoveAutoGenNotes();
  *this = copy;

  if (SONGMAN->GetGroup(this) != nullptr) {
    song_timing_.beat0_group_offset_in_seconds_ =
        SONGMAN->GetGroup(this)->GetSyncOffset();
  } else {
    song_timing_.beat0_group_offset_in_seconds_ =
        PREFSMAN->default_sync_offset_ == SyncOffset_NULL ? 0 : -0.009;
    LOG->Warn(
        "Song %s has no group, using default sync offset.",
        main_title_.c_str());
  }

  /* Go through the steps, first setting their Song pointer to this song
   * (instead of the copy used above), and constructing a map to let us
   * easily find the new steps. */
  std::map<StepsID, Steps*> m_new_steps;
  for (std::vector<Steps*>::const_iterator it = steps_.begin();
       it != steps_.end(); ++it) {
    (*it)->song_ = this;
    StepsID id;
    id.FromSteps(*it);
    m_new_steps[id] = *it;

    // Reapply the Group Offset if the steps have their own timing data.
    if (m_new_steps[id]->timing_.empty()) {
      continue;
    }
    if (SONGMAN->GetGroup(this) != nullptr) {
      m_new_steps[id]->timing_.beat0_group_offset_in_seconds_ =
          SONGMAN->GetGroup(this)->GetSyncOffset();
    } else {
      m_new_steps[id]->timing_.beat0_group_offset_in_seconds_ =
          PREFSMAN->default_sync_offset_ == SyncOffset_NULL ? 0 : -0.009;
      LOG->Warn(
          "Song %s has no group, using default sync offset.",
          main_title_.c_str());
    }
  }

  // Now we wipe out the new pointers, which were shallow copied and not deep
  // copied...
  steps_.clear();
  FOREACH_ENUM(StepsType, i)
  steps_by_type_[i].clear();

  /* Then we copy as many Steps as possible on top of the old pointers.
   * The only pointers that change are pointers to Steps that are not in the
   * reverted file, which we delete, and pointers to Steps that are in the
   * reverted file but not the original *this, which we create new copies of.
   * We have to go through these hoops because many places assume the Steps
   * pointers don't change - even though there are other ways they can change,
   * such as deleting a Steps via the editor. */
  for (std::vector<Steps*>::const_iterator it_old = v_old_steps.begin();
       it_old != v_old_steps.end(); ++it_old) {
    StepsID id;
    id.FromSteps(*it_old);
    std::map<StepsID, Steps*>::iterator it_new = m_new_steps.find(id);
    if (it_new == m_new_steps.end()) {
      // This stepchart didn't exist in the file we reverted from
      delete *it_old;
    } else {
      Steps* old_steps = *it_old;
      *old_steps = *(it_new->second);
      AddSteps(old_steps);
      m_new_steps.erase(it_new);
    }
  }
  // The leftovers in the map are steps that didn't exist before we reverted
  for (std::map<StepsID, Steps*>::const_iterator it = m_new_steps.begin();
       it != m_new_steps.end(); ++it) {
    Steps* new_steps = new Steps(this);
    *new_steps = *(it->second);
    AddSteps(new_steps);
  }

  AddAutoGenNotes();
  // Reload any images associated with the song. -Kyz
  std::vector<std::string> to_reload;
  to_reload.reserve(7);
  to_reload.push_back(banner_file_);
  to_reload.push_back(jacket_file_);
  to_reload.push_back(cd_file_);
  to_reload.push_back(disc_file_);
  to_reload.push_back(background_file_);
  to_reload.push_back(cd_title_file_);
  to_reload.push_back(preview_vid_file_);
  for (std::vector<std::string>::iterator file = to_reload.begin();
       file != to_reload.end(); ++file) {
    RageTextureID id(*file);
    if (TEXTUREMAN->IsTextureRegistered(id)) {
      RageTexture* tex = TEXTUREMAN->LoadTexture(id);
      if (tex) {
        tex->Reload();
      }
    }
  }
  return true;
}

void Song::LoadEditsFromSongDir(std::string dir) {
  // Load any .edit files in the song folder.
  // Doing this BEFORE setting up AutoGen just in case.
  std::vector<std::string> vs;
  GetDirListing(dir + "*.edit", vs, false, false);
  // XXX: I'm sure there's a StepMania way of doing this, but familiar with this
  // codebase I am not.
  for (unsigned int i = 0; i < vs.size(); ++i) {
    // Try SSCLoader
    SSCLoader ssc;
    if (ssc.LoadEditFromFile(dir + vs[i], ProfileSlot_Invalid, true, this) !=
        true) {
      // No dice? Try SMLoader then. If SMLoader fails too, well whatever.
      // We don't have to do anything to fail gracefully.
      SMLoader sm;
      sm.LoadEditFromFile(dir + vs[i], ProfileSlot_Invalid, true, this);
    }
  }
  // Note: If vs.empty() then this loop is skipped entirely (vs.size() == 0)
}

bool Song::HasAutosaveFile() {
  if (song_file_name_.empty()) {
    return false;
  }
  std::string autosave_path = SetExtension(song_file_name_, "ats");
  return FILEMAN->DoesFileExist(autosave_path);
}

bool Song::LoadAutosaveFile() {
  if (song_file_name_.empty()) {
    return false;
  }
  // Save these strings because they need to be restored after the reset.
  // The filenames need to point to the original instead of the autosave for
  // things like load from disk to work. -Kyz
  std::string dir = GetSongDir();
  std::string song_timing_file = song_timing_.file_;
  std::string song_file = song_file_name_;
  // Reset needs to be used to remove all the steps and other things that
  // will be loaded from the autosave. -Kyz
  Reset();
  if (LoadFromSongDir(dir, true)) {
    loaded_from_autosave_ = true;
    song_file_name_ = song_file;
    song_timing_.file_ = song_timing_file;
    return true;
  }
  // Loading the autosave failed, reload the original. -Kyz
  // LoadFromSongDir assumes an empty Song, and the failed autosave load left
  // this one populated, so clear it first.
  Reset();
  LoadFromSongDir(dir, false);
  return false;
}

/* Fix up song paths. If there's a leading "./", be sure to keep it: it's
 * a signal that the path is from the root directory, not the song directory.
 * Other than a leading "./", song paths must never contain "." or "..". */
void FixupPath(std::string& path, const std::string& song_path) {
  // Replace backslashes with slashes in all paths.
  FixSlashesInPlace(path);

  /* Many imported files contain erroneous whitespace before or after
   * filenames. Paths usually don't actually start or end with spaces,
   * so let's just remove it. */
  Trim(path);
}

void Song::TidyUpData(bool from_cache) {
  Song::TidyUpData(from_cache, std::set<std::string>());
}

// Songs in BlacklistImages will never be autodetected as song images.
void Song::TidyUpData(
    bool from_cache, const std::set<std::string>& blacklisted_images) {
  // We need to do this before calling any of HasMusic, HasHasCDTitle, etc.
  ASSERT_M(Left(song_dir_, 3) != "../", song_dir_);  // meaningless
  FixupPath(song_dir_, "");
  FixupPath(music_file_, song_dir_);
  FixupPath(banner_file_, song_dir_);
  FixupPath(jacket_file_, song_dir_);
  FixupPath(cd_file_, song_dir_);
  FixupPath(disc_file_, song_dir_);
  FixupPath(lyrics_file_, song_dir_);
  FixupPath(background_file_, song_dir_);
  FixupPath(cd_title_file_, song_dir_);

  CHECKPOINT_M("Looking for images...");

  song_timing_.TidyUpData(false);

  // Apply the group offset to the song timing before we do anything else.
  float offset = PREFSMAN->default_sync_offset_ == SyncOffset_NULL ? 0 : -0.009;
  if (SONGMAN->GetGroupFromName(group_name_) != nullptr) {
    offset = SONGMAN->GetGroupFromName(group_name_)->GetSyncOffset();
  }
  song_timing_.beat0_group_offset_in_seconds_ = offset;

  for (Steps* s : steps_) {
    s->timing_.TidyUpData(true);
    // Apply the group offset to the step timing as well.
    s->timing_.beat0_group_offset_in_seconds_ = offset;
  }

  if (!from_cache) {
    if (this->artist_ == "The Dancing Monkeys Project" &&
        this->main_title_.find_first_of('-') != std::string::npos) {
      // Dancing Monkeys had a bug/feature where the artist was replaced.
      // Restore it.
      std::vector<std::string> title_parts;
      split(this->main_title_, "-", title_parts);
      this->artist_ = title_parts.front();
      Trim(this->artist_);
      title_parts.erase(title_parts.begin());
      this->main_title_ = join("-", title_parts);
      Trim(this->main_title_);
    }

    Trim(main_title_);
    Trim(sub_title_);
    Trim(artist_);

    // Fall back on the song directory name.
    if (main_title_ == "") {
      NotesLoader::GetMainAndSubTitlesFromFullTitle(
          Basename(this->GetSongDir()), main_title_, sub_title_);
    }

    if (artist_ == "") {
      artist_ = "Unknown artist";
    }
    TranslateTitles();

    // Set the has flags before tidying so that tidying can check them instead
    // of using the has functions that hit the disk. -Kyz
    // These will be written to cache, for Song::LoadFromSongDir to use later.
    has_music_ = HasMusic();
    has_banner_ = HasBanner();
    has_background_ = HasBackground();

    for (const std::string& image : GetImageCacheDirs()) {
      IMAGECACHE->LoadImage(image, GetCacheFile(image));
    }

    // There are several things that need to find a file from the dir with a
    // particular extension or type of extension.  So fetch a list of all
    // files in the dir once, then split that list into the different things
    // we need. -Kyz
    std::vector<std::string> song_dir_listing;
    FILEMAN->GetDirListing(song_dir_ + "*", song_dir_listing, false, false);
    std::vector<std::string> music_list;
    std::vector<std::string> image_list;
    std::vector<std::string> movie_list;
    std::vector<std::string> lyric_list;
    std::vector<std::string> lyric_extensions(1, "lrc");
    // Using a pair didn't work, so these two vectors have to be kept in
    // sync instead. -Kyz
    std::vector<std::vector<std::string>*> lists_to_fill;
    std::vector<const std::vector<std::string>*> fill_exts;
    lists_to_fill.reserve(4);
    fill_exts.reserve(4);
    lists_to_fill.push_back(&music_list);
    fill_exts.push_back(&ActorUtil::GetTypeExtensionList(FT_Sound));
    // Disable bg and banner images and movies for custom songs.  Loading
    // them takes too long. -Kyz
    if (loaded_from_profile_ == ProfileSlot_Invalid) {
      lists_to_fill.push_back(&image_list);
      fill_exts.push_back(&ActorUtil::GetTypeExtensionList(FT_Bitmap));
      lists_to_fill.push_back(&movie_list);
      fill_exts.push_back(&ActorUtil::GetTypeExtensionList(FT_Movie));
    }
    lists_to_fill.push_back(&lyric_list);
    fill_exts.push_back(&lyric_extensions);
    for (std::vector<std::string>::iterator filename = song_dir_listing.begin();
         filename != song_dir_listing.end(); ++filename) {
      bool matched_something = false;
      std::string file_ext = MakeLower(GetExtension(*filename));
      if (!file_ext.empty()) {
        for (size_t tf = 0; tf < lists_to_fill.size(); ++tf) {
          for (std::vector<std::string>::const_iterator ext =
                   fill_exts[tf]->begin();
               ext != fill_exts[tf]->end(); ++ext) {
            if (file_ext == *ext) {
              lists_to_fill[tf]->push_back(*filename);
              matched_something = true;
              break;
            }
          }
          if (matched_something) {
            break;
          }
        }
      }
    }

    if (!has_music_) {
      // If the first song is "intro", and we have more than one available,
      // don't use it--it's probably a KSF intro music file, which we don't
      // (yet) support.
      if (!music_list.empty()) {
        LOG->Trace(
            "Song '%s' points to a music file that doesn't exist, found music "
            "file '%s'",
            song_dir_.c_str(), music_list[0].c_str());
        has_music_ = true;
        music_file_ = music_list[0];
        if (music_list.size() > 1 &&
            !CompareNoCase(Left(music_file_, 5), "intro")) {
          music_file_ = music_list[1];
        }
      }
    }
    // This must be done before radar calculation.
    if (has_music_) {
      std::string error;
      RageSoundReader* sample =
          RageSoundReader_FileReader::OpenFile(GetMusicPath(), error);
      /* XXX: Checking if the music file exists eliminates a warning
       * originating from BMS files (which have no music file, per se)
       * but it's something of a hack. */
      if (sample == nullptr && music_file_ != "") {
        LOG->UserLog(
            "Sound file", GetMusicPath(), "couldn't be opened: %s",
            error.c_str());

        // Don't use this file.
        music_file_ = "";
      } else if (sample != nullptr) {
        music_length_seconds_ = sample->GetLength() / 1000.0f;
        delete sample;

        if (music_length_seconds_ < 0) {
          // It failed; bad file or something. It's already logged a warning.
          music_length_seconds_ = 100;  // guess
        } else if (music_length_seconds_ == 0) {
          LOG->UserLog("Sound file", GetMusicPath(), "is empty.");
        }
      }
    } else  // ! HasMusic()
    {
      music_length_seconds_ = 100;  // guess
      LOG->UserLog(
          "Song", GetSongDir(), "has no music file; guessing at %f seconds",
          music_length_seconds_);
    }
    if (music_length_seconds_ < 0) {
      LOG->UserLog(
          "Sound file", GetMusicPath(), "has a negative length %f.",
          music_length_seconds_);
      music_length_seconds_ = 0;
    }
    if (!preview_file_.empty() &&
        music_sample_length_seconds_ <=
            0.00f) {  // if there's a preview file and sample length isn't
                      // specified, set sample length to length of preview file
      std::string error;
      RageSoundReader* sample =
          RageSoundReader_FileReader::OpenFile(GetPreviewMusicPath(), error);
      if (sample == nullptr && music_file_ != "") {
        LOG->UserLog(
            "Sound file", GetPreviewMusicPath(), "couldn't be opened: %s",
            error.c_str());

        // Don't use this file.
        preview_file_ = "";
        music_sample_length_seconds_ = DEFAULT_MUSIC_SAMPLE_LENGTH;
      } else if (sample != nullptr) {
        music_sample_length_seconds_ = sample->GetLength() / 1000.0f;
        delete sample;

        if (music_sample_length_seconds_ < 0) {
          // It failed; bad file or something. It's already logged a warning.
          music_sample_length_seconds_ = DEFAULT_MUSIC_SAMPLE_LENGTH;
        } else if (music_sample_length_seconds_ == 0) {
          LOG->UserLog("Sound file", GetPreviewMusicPath(), "is empty.");
        }
      }
    } else {  // no preview file, calculate sample from music as normal

      if (music_sample_start_seconds_ == -1 ||
          music_sample_length_seconds_ == 0 ||
          music_sample_start_seconds_ + music_sample_length_seconds_ >
              this->music_length_seconds_) {
        const TimingData& timing = this->song_timing_;
        music_sample_start_seconds_ =
            timing.GetElapsedTimeFromBeatNoOffset(100);

        if (music_sample_start_seconds_ + music_sample_length_seconds_ >
            this->music_length_seconds_) {
          // Attempt to get a reasonable default.
          int beat = std::lrint(
              this->song_timing_.GetBeatFromElapsedTimeNoOffset(
                  this->GetLastSecondNoOffset()) /
              2);
          beat -= beat % 4;
          music_sample_start_seconds_ =
              timing.GetElapsedTimeFromBeatNoOffset((float)beat);
        }
      }

      // The old logic meant that you couldn't have sample lengths that go
      // forever, e.g. those in Donkey Konga. I never liked that. -freem
      if (music_sample_length_seconds_ <= 0.00f) {
        music_sample_length_seconds_ = DEFAULT_MUSIC_SAMPLE_LENGTH;
      }
    }

    if (!HasLyrics()) {
      // Check if there is a lyric file in here
      if (!lyric_list.empty()) {
        lyrics_file_ = lyric_list[0];
      }
    }

    // Since banner, bg, and cdtitle are disabled for custom songs, don't try
    // to find them. -Kyz
    if (loaded_from_profile_ == ProfileSlot_Invalid) {
      // Here's the problem:  We have a directory full of images. We want to
      // determine which image is the banner, which is the background, and
      // which is the CDTitle.

      // For blank args to FindFirstFilenameContaining. -Kyz
      std::vector<std::string> empty_list;

      bool has_jacket = HasJacket();
      bool has_cdimage = HasCDImage();
      bool has_disc = HasDisc();
      bool has_cdtitle = HasCDTitle();

      // First, check the file name for hints.
      if (!has_banner_) {
        /* If a nonexistant banner file is specified, and we can't find a
         * replacement, don't wipe out the old value. */
        // m_sBannerFile = "";

        // find an image with "banner" in the file name
        std::vector<std::string> contains(1, "banner");
        /* Some people do things differently for the sake of being different.
         * Don't match eg. abnormal, numbness. */
        std::vector<std::string> ends_with(1, " bn");
        has_banner_ = FindFirstFilenameContaining(
            image_list, banner_file_, empty_list, contains, ends_with);
      }

      if (!has_background_) {
        // m_sBackgroundFile = "";

        // find an image with "bg" or "background" in the file name
        std::vector<std::string> contains(1, "background");
        std::vector<std::string> ends_with(1, "bg");
        has_background_ = FindFirstFilenameContaining(
            image_list, background_file_, empty_list, contains, ends_with);
      }

      if (!has_jacket) {
        // find an image with "jacket" or "albumart" in the filename.
        std::vector<std::string> starts_with(1, "jk_");
        std::vector<std::string> contains;
        contains.reserve(2);
        contains.push_back("jacket");
        contains.push_back("albumart");
        has_jacket = FindFirstFilenameContaining(
            image_list, jacket_file_, starts_with, contains, empty_list);
      }

      if (!has_cdimage) {
        // CD image, a la ddr 1st-3rd (not to be confused with CDTitles)
        // find an image with "-cd" at the end of the filename.
        std::vector<std::string> ends_with(1, "-cd");
        has_cdimage = FindFirstFilenameContaining(
            image_list, cd_file_, empty_list, empty_list, ends_with);
      }

      if (!has_disc) {
        // a rectangular graphic, not to be confused with CDImage above.
        std::vector<std::string> ends_with;
        ends_with.reserve(2);
        ends_with.push_back(" disc");
        ends_with.push_back(" title");
        has_disc = FindFirstFilenameContaining(
            image_list, disc_file_, empty_list, empty_list, ends_with);
      }

      if (!has_cdtitle) {
        // find an image with "cdtitle" in the file name
        std::vector<std::string> contains(1, "cdtitle");
        has_cdtitle = FindFirstFilenameContaining(
            image_list, cd_title_file_, empty_list, contains, empty_list);
      }

      /* Now, For the images we still haven't found,
       * look at the image dimensions of the remaining unclassified images. */
      for (unsigned int i = 0; i < image_list.size(); ++i)  // foreach image
      {
        if (has_banner_ && has_background_ && has_cdtitle) {
          break;  // done
        }

        // ignore DWI "-char" graphics
        std::string lower = image_list[i];
        MakeLower(lower);
        if (blacklisted_images.find(lower) != blacklisted_images.end()) {
          continue;  // skip
        }

        // Skip any image that we've already classified

        if (has_banner_ && EqualsNoCase(banner_file_, image_list[i])) {
          continue;  // skip
        }

        if (has_background_ && EqualsNoCase(background_file_, image_list[i])) {
          continue;  // skip
        }

        if (has_cdtitle && EqualsNoCase(cd_title_file_, image_list[i])) {
          continue;  // skip
        }

        if (has_jacket && EqualsNoCase(jacket_file_, image_list[i])) {
          continue;  // skip
        }

        if (has_disc && EqualsNoCase(disc_file_, image_list[i])) {
          continue;  // skip
        }

        if (has_cdimage && EqualsNoCase(cd_file_, image_list[i])) {
          continue;  // skip
        }

        std::string path = song_dir_ + image_list[i];

        // We only care about the dimensions.
        std::string error;
        RageSurface* img = RageSurfaceUtils::LoadFile(path, error, true);
        if (!img) {
          LOG->UserLog(
              "Graphic file", path, "couldn't be loaded: %s", error.c_str());
          continue;
        }

        const int width = img->w_;
        const int height = img->h_;
        delete img;

        if (!has_background_ && width >= 320 && height >= 240) {
          background_file_ = image_list[i];
          has_background_ = true;
          continue;
        }

        if (!has_banner_ && 100 <= width && width <= 320 && 50 <= height &&
            height <= 240) {
          banner_file_ = image_list[i];
          has_banner_ = true;
          continue;
        }

        /* Some songs have overlarge banners. Check if the ratio is reasonable
         * (over 2:1; usually over 3:1), and large (not a cdtitle). */
        if (!has_banner_ && width > 200 && float(width) / height > 2.0f) {
          banner_file_ = image_list[i];
          has_banner_ = true;
          continue;
        }

        /* Agh. DWI's inline title images are triggering this, resulting in
         * kanji, etc., being used as a CDTitle for songs with none. Some
         * sample data from random incarnations:
         *   42x50 35x50 50x50 144x49
         * It looks like ~50 height is what people use to align to DWI's font.
         *
         * My tallest CDTitle is 44. Let's cut off in the middle and hope for
         * the best. -(who? -aj) */
        /* The proper size of a CDTitle is 64x48 or sometihng. Simfile artists
         * typically don't give a shit about this (see Cetaka's fucking banner
         * -sized CDTitle). This is also subverted in certain designs (beta
         * Mungyodance 3 simfiles, for instance, used the CDTitle to hold
         * various information about the song in question). As it stands,
         * I'm keeping this code until I figure out wtf to do -aj
         */
        if (!has_cdtitle && width <= 100 && height <= 48) {
          cd_title_file_ = image_list[i];
          has_cdtitle = true;
          continue;
        }

        // Jacket files typically have the same width and height.
        if (!has_jacket && width == height) {
          jacket_file_ = image_list[i];
          has_jacket = true;
          continue;
        }

        // Disc images are typically rectangular; make sure we have a banner
        // already.
        if (!has_disc && (width > height) && has_banner_) {
          if (image_list[i] != banner_file_) {
            disc_file_ = image_list[i];
            has_disc = true;
          }
          continue;
        }

        // CD images are the same as Jackets, typically the same width and
        // height
        if (!has_cdimage && width == height) {
          cd_file_ = image_list[i];
          has_cdimage = true;
          continue;
        }
      }
      // If no BGChanges are specified and there are movies in the song
      // directory, then assume they are DWI style where the movie begins at
      // beat 0.
      if (!HasBGChanges()) {
        /* Use this->GetBeatFromElapsedTime(0) instead of 0 to start when the
         * music starts. */
        if (movie_list.size() == 1) {
          this->AddBackgroundChange(
              BACKGROUND_LAYER_1,
              BackgroundChange(0, movie_list[0], "", 1.f, SBE_StretchNoLoop));
        }
      }
      // Clear fields for files that turned out to not exist.
#define CLEAR_NOT_HAS(has_name, field_name) \
  if (!has_name) {                          \
    field_name = "";                        \
  }
      CLEAR_NOT_HAS(has_banner_, banner_file_);
      CLEAR_NOT_HAS(has_background_, background_file_);
      CLEAR_NOT_HAS(has_jacket, jacket_file_);
      CLEAR_NOT_HAS(has_cdimage, cd_file_);
      CLEAR_NOT_HAS(has_disc, disc_file_);
      CLEAR_NOT_HAS(has_cdtitle, cd_title_file_);
#undef CLEAR_NOT_HAS
    }
    // Don't allow multiple Steps of the same StepsType and Difficulty
    // (except for edits). We should be able to use difficulty names as
    // unique identifiers for steps. */
    SongUtil::AdjustDuplicateSteps(this);
  }

  /* Generate these before we autogen notes, so the new notes can inherit
   * their source's values. On cache loads the stats and first/last second
   * already came from the cache file -- nothing to recalculate. */
  if (!from_cache) {
    ReCalculateStepStatsAndLastSecond(true);
  }
  // If the music length is suspiciously shorter than the last second, adjust
  // the length.  This prevents the ogg patch from setting a false length. -Kyz
  if (music_length_seconds_ < last_second_ - 10.0f) {
    music_length_seconds_ = last_second_;
  }
}

void Song::FixupBackgroundChanges() {
  /*
   * Hack: if the song has any changes at all (so it won't use a random BGA)
   * and doesn't end with "-nosongbg-", add a song background BGC.  Remove
   * "-nosongbg-" if it exists.
   *
   * This way, songs that were created earlier, when we added the song BG
   * at the end by default, will still behave as expected; all new songs will
   * have to add an explicit song BG tag if they want one.  This is really a
   * formatting hack only; nothing outside of the loaders ever sees
   * "-nosongbg-".
   */
  std::vector<BackgroundChange>& bg = GetBackgroundChanges(BACKGROUND_LAYER_1);
  if (bg.empty()) {
    return;
  }

  /* BGChanges have been sorted. On the odd chance that a BGChange exists
   * with a very high beat, search the whole list. */
  bool has_no_song_bg_tag = false;

  for (unsigned i = 0; !has_no_song_bg_tag && i < bg.size(); ++i) {
    if (!CompareNoCase(bg[i].def_.file1_, NO_SONG_BG_FILE)) {
      bg.erase(bg.begin() + i);
      has_no_song_bg_tag = true;
    }
  }

  // If there's no -nosongbg- tag, add the song BG.
  if (!has_no_song_bg_tag) {
    do {
      float last_beat = GetLastBeat();
      /* If BGChanges already exist after the last beat, don't add the
       * background in the middle. */
      if (!bg.empty() && bg.back().start_beat_ - 0.0001f >= last_beat) {
        break;
      }

      // If the last BGA is already the song BGA, don't add a duplicate.
      if (!bg.empty() &&
          !CompareNoCase(bg.back().def_.file1_, background_file_)) {
        break;
      }

      if (!IsAFile(GetBackgroundPath())) {
        break;
      }

      bg.push_back(BackgroundChange(last_beat, background_file_));
    } while (0);
  }
}

void Song::TranslateTitles() {
  static TitleSubst tsub("Songs");

  TitleFields title;
  title.LoadFromStrings(
      main_title_, sub_title_, artist_, main_title_translit_,
      sub_title_translit_, artist_translit_);
  tsub.Subst(title);
  title.SaveToStrings(
      main_title_, sub_title_, artist_, main_title_translit_,
      sub_title_translit_, artist_translit_);
}

void Song::ReCalculateStepStatsAndLastSecond(bool wipe_note_data) {
  float local_first = FLT_MAX;  // inf
  // Make sure we're at least as long as the specified amount below.
  float local_last = this->specified_last_second_;

  for (unsigned i = 0; i < steps_.size(); i++) {
    Steps* steps = steps_[i];
    steps->CalculateStepStats(music_length_seconds_);

    // Must initialize before the gotos.
    NoteData temp_note_data;
    steps->GetNoteData(temp_note_data);

    // calculate lastSecond

    /* 1. If it's autogen, then first/last beat will come from the parent.
     * 2. Don't calculate with edits unless the song only contains an edit
     * chart, like those in Mungyodance 3. Otherwise, edits installed on
     * the machine could extend the length of the song. */
    if (!steps->IsAutogen() && !(steps->IsAnEdit() && steps_.size() > 1)) {
      // Don't set first/last beat based on lights.  They often start very
      // early and end very late.
      if (steps->steps_type_ == StepsType_lights_cabinet) {
        continue;  // no need to wipe this.
      }

      /* Many songs have stray, empty song patterns. Ignore them, so they
       * don't force the first beat of the whole song to 0. */
      if (temp_note_data.GetLastRow() != 0) {
        local_first = std::min(
            local_first, steps->GetTimingData()->GetElapsedTimeFromBeatNoOffset(
                             temp_note_data.GetFirstBeat()));
        local_last = std::max(
            local_last, steps->GetTimingData()->GetElapsedTimeFromBeatNoOffset(
                            temp_note_data.GetLastBeat()));
      }
    }

    // Wipe NoteData
    if (wipe_note_data) {
      NoteData dummy;
      dummy.SetNumTracks(temp_note_data.GetNumTracks());
      steps->SetNoteData(dummy);
    }
  }

  // Yes, for some reason we can have freaky stuff take place here.
  this->first_second_ = (local_first < local_last) ? local_first : 0;
  this->last_second_ = local_last;
}

bool Song::HasStepsType(StepsType st) const {
  return SongUtil::GetOneSteps(this, st) != nullptr;
}

bool Song::HasStepsTypeAndDifficulty(StepsType st, Difficulty dc) const {
  return SongUtil::GetOneSteps(this, st, dc) != nullptr;
}

void Song::Save(bool autosave) {
  LOG->Trace("Song::SaveToSongFile()");

  ReCalculateStepStatsAndLastSecond(false);
  TranslateTitles();

  // Save the new files. These calls make backups on their own.
  if (!SaveToSSCFile(GetSongFilePath(), autosave)) {
    return;
  }
  // Skip saving the cache, sm, and .old files if we are autosaving.  The
  // cache file should not contain the autosave filename. -Kyz
  if (autosave) {
    return;
  }
  SaveToCacheFile();
  // If one of the charts uses split timing, then it cannot be accurately
  // saved in the .sm format.  So saving the .sm is disabled.
  if (!AnyChartUsesSplitTiming()) {
    SaveToSMFile();
  }
}

bool Song::SaveToSMFile() {
  const std::string path = SetExtension(GetSongFilePath(), "sm");
  LOG->Trace("Song::SaveToSMFile(%s)", path.c_str());

  // If the file exists, make a backup.
  if (IsAFile(path)) {
    FileCopy(path, path + ".old");
  }

  std::vector<Steps*> steps_to_save;
  for (Steps* steps : steps_) {
    if (steps->IsAutogen()) {
      continue;  // don't write autogen notes
    }

    // Only save steps that weren't loaded from a profile.
    if (steps->WasLoadedFromProfile()) {
      continue;
    }

    steps_to_save.push_back(steps);
  }
  for (Steps* s : unknown_style_steps_) {
    steps_to_save.push_back(s);
  }

  return NotesWriterSM::Write(path, *this, steps_to_save);
}

bool Song::SaveToSSCFile(std::string s_path, bool autosave) {
  std::string path = SetExtension(s_path, "ssc");
  if (autosave) {
    path = SetExtension(s_path, "ats");
  }

  LOG->Trace("Song::SaveToSSCFile('%s')", path.c_str());

  // If the file exists, make a backup.
  if (!autosave && IsAFile(path)) {
    FileCopy(path, path + ".old");
  }

  std::vector<Steps*> steps_to_save;
  for (Steps* steps : steps_) {
    if (steps->IsAutogen()) {
      continue;  // don't write autogen notes
    }

    // Only save steps that weren't loaded from a profile.
    if (steps->WasLoadedFromProfile()) {
      continue;
    }

    steps->SetFilename(path);
    steps_to_save.push_back(steps);
  }
  for (Steps* s : unknown_style_steps_) {
    steps_to_save.push_back(s);
  }

  if (autosave) {
    return NotesWriterSSC::Write(path, *this, steps_to_save);
  }

  if (!NotesWriterSSC::Write(path, *this, steps_to_save)) {
    return false;
  }

  RemoveAutosave();

  if (g_BackUpAllSongSaves.Get()) {
    std::string ext = GetExtension(path);
    std::string backup_file = SetExtension(path, "");

    time_t cur_time;
    time(&cur_time);
    struct tm now;
    localtime_r(&cur_time, &now);

    backup_file += ssprintf(
        "-%04i-%02i-%02i--%02i-%02i-%02i", 1900 + now.tm_year, now.tm_mon + 1,
        now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec);
    backup_file = SetExtension(backup_file, ext);
    backup_file += ssprintf(".old");

    if (FileCopy(path, backup_file)) {
      LOG->Trace("Backed up %s to %s", path.c_str(), backup_file.c_str());
    } else {
      LOG->Trace(
          "Failed to back up %s to %s", path.c_str(), backup_file.c_str());
    }
  }

  // Mark these steps saved to disk.
  for (Steps* s : steps_to_save) {
    s->SetSavedToDisk(true);
  }

  return true;
}

bool Song::SaveToCacheFile() {
  if (SONGMAN->IsGroupNeverCached(group_name_)) {
    return true;
  }
  SONGINDEX->AddCacheIndex(song_dir_, GetHashForDirectory(song_dir_));
  const std::string path = GetCacheFilePath();
  return SongCacheBinary::Write(*this, path);
}

void Song::RemoveAutosave() {
  std::string autosave_path = SetExtension(song_file_name_, "ats");
  if (FILEMAN->DoesFileExist(autosave_path)) {
    // Change all the steps to point to the actual file, not the autosave
    // file.  -Kyz
    std::string extension = GetExtension(song_file_name_);
    for (size_t i = 0; i < steps_.size(); ++i) {
      if (!steps_[i]->IsAutogen()) {
        steps_[i]->SetFilename(
            SetExtension(steps_[i]->GetFilename(), extension));
      }
    }
    FILEMAN->Remove(autosave_path);
    loaded_from_autosave_ = false;
  }
}

void Song::AddAutoGenNotes() {
  bool has_notes[NUM_StepsType];
  memset(has_notes, 0, sizeof(has_notes));
  for (unsigned i = 0; i < steps_.size(); i++)  // foreach Steps
  {
    if (steps_[i]->IsAutogen()) {
      continue;
    }

    StepsType st = steps_[i]->steps_type_;
    has_notes[st] = true;
  }

  FOREACH_ENUM(StepsType, missing) {
    if (has_notes[missing]) {
      continue;
    }

    if (!PREFSMAN->autogen_steps_) {
      continue;
    }
    if (!GAMEMAN->GetStepsTypeInfo(missing).allow_autogen_) {
      continue;
    }

    // missing Steps of this type
    int num_tracks_of_missing = GAMEMAN->GetStepsTypeInfo(missing).num_tracks_;

    // look for closest match
    StepsType best_match = StepsType_Invalid;
    int best_track_difference = INT_MAX;

    FOREACH_ENUM(StepsType, st) {
      if (!has_notes[st]) {
        continue;
      }

      // has (non-autogen) Steps of this type
      const int num_tracks = GAMEMAN->GetStepsTypeInfo(st).num_tracks_;
      const int track_difference = std::abs(num_tracks - num_tracks_of_missing);
      if (track_difference < best_track_difference) {
        best_match = st;
        best_track_difference = track_difference;
      }
    }

    if (best_match != StepsType_Invalid) {
      AutoGen(missing, best_match);
    }
  }
}

void Song::AutoGen(StepsType to, StepsType from) {
  // int iNumTracksOfTo = GAMEMAN->StepsTypeToNumTracks(ntTo);

  for (unsigned int j = 0; j < steps_.size(); j++) {
    const Steps* original_notes = steps_[j];
    if (original_notes->steps_type_ == from) {
      Steps* new_notes = new Steps(this);
      new_notes->AutogenFrom(original_notes, to);
      this->AddSteps(new_notes);
    }
  }
}

void Song::RemoveAutoGenNotes() {
  FOREACH_ENUM(StepsType, st) {
    for (int j = steps_by_type_[st].size() - 1; j >= 0; j--) {
      if (steps_by_type_[st][j]->IsAutogen()) {
        // delete m_vpSteps[j]; // delete below
        steps_by_type_[st].erase(steps_by_type_[st].begin() + j);
      }
    }
  }

  for (int j = steps_.size() - 1; j >= 0; j--) {
    if (steps_[j]->IsAutogen()) {
      delete steps_[j];
      steps_.erase(steps_.begin() + j);
    }
  }
}

bool Song::IsEasy(StepsType st) const {
  /* Very fast songs and songs with wide tempo changes are hard for new
   * players, even if they have beginner steps. */
  DisplayBpms bpms;
  this->GetDisplayBpms(bpms);
  if (bpms.GetMax() >= 250 || bpms.GetMax() - bpms.GetMin() >= 75) {
    return false;
  }

  /* The easy marker indicates which songs a beginner, having selected
   * "beginner", can play and actually get a very easy song: if there are
   * actual beginner steps, or if the light steps are 1- or 2-foot. */
  const Steps* beginner_notes =
      SongUtil::GetStepsByDifficulty(this, st, Difficulty_Beginner);
  if (beginner_notes) {
    return true;
  }

  const Steps* easy_notes =
      SongUtil::GetStepsByDifficulty(this, st, Difficulty_Easy);
  if (easy_notes && easy_notes->GetMeter() == 1) {
    return true;
  }

  return false;
}

bool Song::IsTutorial() const {
  // A Song is considered a Tutorial if it has only Beginner steps.
  for (const Steps* s : steps_) {
    if (s->steps_type_ == StepsType_lights_cabinet) {
      continue;  // ignore
    }
    if (s->GetDifficulty() != Difficulty_Beginner) {
      return false;
    }
  }

  return true;
}

bool Song::HasEdits(StepsType st) const {
  return std::any_of(steps_.begin(), steps_.end(), [&](const Steps* step) {
    return step->steps_type_ == st && step->GetDifficulty() == Difficulty_Edit;
  });
}

bool Song::NormallyDisplayed() const {
  return UNLOCKMAN == nullptr || !UNLOCKMAN->SongIsLocked(this);
}

bool Song::ShowInDemonstrationAndRanking() const {
  return !IsTutorial() && NormallyDisplayed();
}

// Hack: see Song::TidyUpData comments.
bool Song::HasMusic() const {
  // If we have keys, we always have music.
  if (keysound_file_.size() != 0) {
    return true;
  }

  return music_file_ != "" && IsAFile(GetMusicPath());
}
bool Song::HasBanner() const {
  return banner_file_ != "" && IsAFile(GetBannerPath());
}
bool Song::HasLyrics() const {
  return lyrics_file_ != "" && IsAFile(GetLyricsPath());
}
bool Song::HasBackground() const {
  return background_file_ != "" && IsAFile(GetBackgroundPath());
}
bool Song::HasCDTitle() const {
  return cd_title_file_ != "" && IsAFile(GetCDTitlePath());
}
bool Song::HasBGChanges() const {
  FOREACH_BackgroundLayer(i) {
    if (!GetBackgroundChanges(i).empty()) {
      return true;
    }
  }
  return false;
}
bool Song::HasAttacks() const { return !attacks_.empty(); }
bool Song::HasJacket() const {
  return jacket_file_ != "" && IsAFile(GetJacketPath());
}
bool Song::HasDisc() const {
  return disc_file_ != "" && IsAFile(GetDiscPath());
}
bool Song::HasCDImage() const {
  return cd_file_ != "" && IsAFile(GetCDImagePath());
}
bool Song::HasPreviewVid() const {
  return preview_vid_file_ != "" && IsAFile(GetPreviewVidPath());
}

const std::vector<BackgroundChange>& Song::GetBackgroundChanges(
    BackgroundLayer bl) const {
  return *(background_changes_[bl]);
}
std::vector<BackgroundChange>& Song::GetBackgroundChanges(BackgroundLayer bl) {
  return *(background_changes_[bl].Get());
}

const std::vector<BackgroundChange>& Song::GetForegroundChanges() const {
  return *foreground_changes_;
}
std::vector<BackgroundChange>& Song::GetForegroundChanges() {
  return *foreground_changes_.Get();
}

// We want to return a filename, We use this function for that.
std::string Song::GetCacheFile(std::string type) {
  if (type == "Banner") {
    return GetBannerPath();
  }
  if (type == "Background") {
    return GetBackgroundPath();
  }
  if (type == "CDTitle") {
    return GetCDTitlePath();
  }
  if (type == "Jacket") {
    return GetJacketPath();
  }
  if (type == "CDImage") {
    return GetCDImagePath();
  }
  if (type == "Disc") {
    return GetDiscPath();
  }

  return "";
}

std::string Song::GetSongAssetPath(
    std::string path, const std::string& song_path) {
  if (path == "") {
    return std::string();
  }

  std::string rel_path = song_path + path;

  /* If there's no path in the file, the file is in the same directory as the
   * song. (This is the preferred configuration.) */
  if (path.find('/') == std::string::npos) {
    return rel_path;
  }

  if (DoesFileExist(rel_path)) {
    return rel_path;
  }

  // The song contains a path; treat it as relative to the top SM directory.
  if (Left(path, 3) == "../") {
    // The path begins with "../".  Resolve it wrt. the song directory.
    path = rel_path;
  }

  CollapsePath(path);

  /* If the path still begins with "../", then there were an unreasonable number
   * of them at the beginning of the path. Ignore the path entirely. */
  if (Left(path, 3) == "../") {
    return std::string();
  }

  return path;
}

/* Note that supplying a path relative to the top-level directory is only for
 * compatibility with DWI. We prefer paths relative to the song directory. */
std::string Song::GetMusicPath() const {
  return GetSongAssetPath(music_file_, song_dir_);
}

std::string Song::GetBannerPath() const {
  return GetSongAssetPath(banner_file_, song_dir_);
}

std::string Song::GetLyricsPath() const {
  return GetSongAssetPath(lyrics_file_, song_dir_);
}

std::string Song::GetCDTitlePath() const {
  return GetSongAssetPath(cd_title_file_, song_dir_);
}

std::string Song::GetBackgroundPath() const {
  return GetSongAssetPath(background_file_, song_dir_);
}

std::string Song::GetJacketPath() const {
  return GetSongAssetPath(jacket_file_, song_dir_);
}

std::string Song::GetDiscPath() const {
  return GetSongAssetPath(disc_file_, song_dir_);
}

std::string Song::GetCDImagePath() const {
  return GetSongAssetPath(cd_file_, song_dir_);
}

std::string Song::GetPreviewVidPath() const {
  return GetSongAssetPath(preview_vid_file_, song_dir_);
}

std::string Song::GetPreviewMusicPath() const {
  if (preview_file_.empty()) {
    return GetMusicPath();
  }
  return GetSongAssetPath(preview_file_, song_dir_);
}

float Song::GetPreviewStartSeconds() const {
  if (preview_file_.empty()) {
    return music_sample_start_seconds_;
  }
  return 0.0f;
}

std::string Song::GetDisplayMainTitle() const {
  if (!PREFSMAN->show_native_language_) {
    return GetTranslitMainTitle();
  }
  return main_title_;
}

std::string Song::GetDisplaySubTitle() const {
  if (!PREFSMAN->show_native_language_) {
    return GetTranslitSubTitle();
  }
  return sub_title_;
}

std::string Song::GetDisplayArtist() const {
  if (!PREFSMAN->show_native_language_) {
    return GetTranslitArtist();
  }
  return artist_;
}

std::string Song::GetMainTitle() const { return main_title_; }

std::string Song::GetDisplayFullTitle() const {
  std::string title = GetDisplayMainTitle();
  std::string sub_title = GetDisplaySubTitle();

  if (!sub_title.empty()) {
    title += " " + sub_title;
  }
  return title;
}

std::string Song::GetTranslitFullTitle() const {
  std::string title = GetTranslitMainTitle();
  std::string sub_title = GetTranslitSubTitle();

  if (!sub_title.empty()) {
    title += " " + sub_title;
  }
  return title;
}

void Song::AddSteps(Steps* steps) {
  // Songs of unknown stepstype are saved as a forwards compatibility feature
  // so that editing a simfile made by a future version that has a new style
  // won't delete those steps. -Kyz
  if (steps->steps_type_ != StepsType_Invalid) {
    steps_.push_back(steps);
    ASSERT_M(
        steps->steps_type_ < NUM_StepsType, ssprintf("%i", steps->steps_type_));
    steps_by_type_[steps->steps_type_].push_back(steps);
  } else {
    unknown_style_steps_.push_back(steps);
  }
}

void Song::DeleteSteps(const Steps* p_steps, bool re_auto_gen) {
  ASSERT(!p_steps->IsAutogen());

  // Avoid any stale Note::parent pointers by removing all AutoGen'd Steps,
  // then adding them again.

  if (re_auto_gen) {
    RemoveAutoGenNotes();
  }

  std::vector<Steps*>& vp_steps = steps_by_type_[p_steps->steps_type_];
  for (int j = vp_steps.size() - 1; j >= 0; j--) {
    if (vp_steps[j] == p_steps) {
      // delete vpSteps[j]; // delete below
      vp_steps.erase(vp_steps.begin() + j);
      break;
    }
  }

  for (int j = steps_.size() - 1; j >= 0; j--) {
    if (steps_[j] == p_steps) {
      delete steps_[j];
      steps_.erase(steps_.begin() + j);
      break;
    }
  }

  if (re_auto_gen) {
    AddAutoGenNotes();
  }
}

bool Song::Matches(std::string group, std::string song) const {
  if (group.size() && CompareNoCase(group, this->group_name_) != 0) {
    return false;
  }

  // match on song dir or title (ala DWI)
  if (!CompareNoCase(song, song_name_)) {
    return true;
  }
  if (!CompareNoCase(song, this->GetTranslitFullTitle())) {
    return true;
  }

  return false;
}

/* If apInUse is set, it contains a list of steps which are in use elsewhere,
 * and should not be deleted. */
void Song::FreeAllLoadedFromProfile(
    ProfileSlot slot, const std::set<Steps*>* set_in_use) {
  /* DeleteSteps will remove and recreate autogen notes, which may reorder
   * m_vpSteps, so be careful not to skip over entries. */
  std::vector<Steps*> to_remove;
  for (int s = steps_.size() - 1; s >= 0; s--) {
    Steps* steps = steps_[s];
    if (!steps->WasLoadedFromProfile()) {
      continue;
    }
    if (slot != ProfileSlot_Invalid &&
        steps->GetLoadedFromProfileSlot() != slot) {
      continue;
    }
    if (set_in_use != nullptr && set_in_use->find(steps) != set_in_use->end()) {
      continue;
    }
    to_remove.push_back(steps);
  }

  for (unsigned i = 0; i < to_remove.size(); ++i) {
    this->DeleteSteps(to_remove[i]);
  }
}

void Song::GetStepsLoadedFromProfile(
    ProfileSlot slot, std::vector<Steps*>& steps_out) const {
  for (unsigned s = 0; s < steps_.size(); s++) {
    Steps* steps = steps_[s];
    if (steps->GetLoadedFromProfileSlot() == slot) {
      steps_out.push_back(steps);
    }
  }
}

int Song::GetNumStepsLoadedFromProfile(ProfileSlot slot) const {
  int count = 0;
  for (unsigned s = 0; s < steps_.size(); s++) {
    Steps* steps = steps_[s];
    if (steps->GetLoadedFromProfileSlot() == slot) {
      count++;
    }
  }
  return count;
}

bool Song::IsEditAlreadyLoaded(Steps* steps) const {
  ASSERT_M(
      steps->GetDifficulty() == Difficulty_Edit,
      ssprintf(
          "The %s chart for %s is no edit, thus it can't be checked for "
          "loading.",
          DifficultyToString(steps->GetDifficulty()).c_str(),
          this->main_title_.c_str()));

  for (unsigned i = 0; i < steps_.size(); i++) {
    Steps* other = steps_[i];
    if (other->GetDifficulty() == Difficulty_Edit &&
        other->steps_type_ == steps->steps_type_ &&
        other->GetHash() == steps->GetHash()) {
      return true;
    }
  }

  return false;
}

bool Song::IsStepsUsingDifferentTiming(Steps* steps) const {
  // XXX This no longer depends on Song at all
  return !steps->timing_.empty();
}

bool Song::AnyChartUsesSplitTiming() const {
  for (Steps* s : steps_) {
    if (!s->timing_.empty()) {
      return true;
    }
  }
  return false;
}

bool Song::HasSignificantBpmChangesOrStops() const {
  if (song_timing_.HasStops() || song_timing_.HasDelays()) {
    return true;
  }

  // Don't consider BPM changes that only are only for maintaining sync as
  // a real BpmChange.
  if (display_bpm_type_ == DISPLAY_BPM_SPECIFIED) {
    if (specified_bpm_min_ != specified_bpm_max_) {
      return true;
    }
  } else if (song_timing_.HasBpmChanges()) {
    return true;
  }

  return false;
}

float Song::GetStepsSeconds() const {
  return this->GetLastSecondNoOffset() - this->GetFirstSecondNoOffset();
}

bool Song::IsLong() const {
  return !IsMarathon() && music_length_seconds_ >= g_fLongVerSongSeconds;
}

bool Song::IsMarathon() const {
  return music_length_seconds_ >= g_fMarathonVerSongSeconds;
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the Song. */
class LunaSong : public Luna<Song> {
 public:
  static int GetDisplayFullTitle(T* p, lua_State* L) {
    lua_pushstring(L, p->GetDisplayFullTitle().c_str());
    return 1;
  }
  static int GetTranslitFullTitle(T* p, lua_State* L) {
    lua_pushstring(L, p->GetTranslitFullTitle().c_str());
    return 1;
  }
  static int GetDisplayMainTitle(T* p, lua_State* L) {
    lua_pushstring(L, p->GetDisplayMainTitle().c_str());
    return 1;
  }
  static int GetMainTitle(T* p, lua_State* L) {
    lua_pushstring(L, p->GetMainTitle().c_str());
    return 1;
  }
  static int GetTranslitMainTitle(T* p, lua_State* L) {
    lua_pushstring(L, p->GetTranslitMainTitle().c_str());
    return 1;
  }
  static int GetDisplaySubTitle(T* p, lua_State* L) {
    lua_pushstring(L, p->GetDisplaySubTitle().c_str());
    return 1;
  }
  static int GetTranslitSubTitle(T* p, lua_State* L) {
    lua_pushstring(L, p->GetTranslitSubTitle().c_str());
    return 1;
  }
  static int GetDisplayArtist(T* p, lua_State* L) {
    lua_pushstring(L, p->GetDisplayArtist().c_str());
    return 1;
  }
  static int GetTranslitArtist(T* p, lua_State* L) {
    lua_pushstring(L, p->GetTranslitArtist().c_str());
    return 1;
  }
  static int GetGenre(T* p, lua_State* L) {
    lua_pushstring(L, p->genre_.c_str());
    return 1;
  }
  static int GetOrigin(T* p, lua_State* L) {
    lua_pushstring(L, p->origin_.c_str());
    return 1;
  }
  static int GetAllSteps(T* p, lua_State* L) {
    const std::vector<Steps*>& v = p->GetAllSteps();
    LuaHelpers::CreateTableFromArray<Steps*>(v, L);
    return 1;
  }
  static int GetStepsByStepsType(T* p, lua_State* L) {
    StepsType st = Enum::Check<StepsType>(L, 1);
    const std::vector<Steps*>& v = p->GetStepsByStepsType(st);
    LuaHelpers::CreateTableFromArray<Steps*>(v, L);
    return 1;
  }
  static int GetSongDir(T* p, lua_State* L) {
    lua_pushstring(L, p->GetSongDir().c_str());
    return 1;
  }
  static int GetMusicPath(T* p, lua_State* L) {
    std::string s = p->GetMusicPath();
    if (!s.empty()) {
      lua_pushstring(L, s.c_str());
    } else {
      lua_pushnil(L);
    }
    return 1;
  }
  static int GetBannerPath(T* p, lua_State* L) {
    std::string s = p->GetBannerPath();
    if (!s.empty()) {
      lua_pushstring(L, s.c_str());
    } else {
      lua_pushnil(L);
    }
    return 1;
  }
  static int GetBackgroundPath(T* p, lua_State* L) {
    std::string s = p->GetBackgroundPath();
    if (!s.empty()) {
      lua_pushstring(L, s.c_str());
    } else {
      lua_pushnil(L);
    }
    return 1;
  }
  static int GetPreviewVidPath(T* p, lua_State* L) {
    std::string s = p->GetPreviewVidPath();
    if (!s.empty()) {
      lua_pushstring(L, s.c_str());
    } else {
      lua_pushnil(L);
    }
    return 1;
  }
  static int GetPreviewMusicPath(T* p, lua_State* L) {
    std::string s = p->GetPreviewMusicPath();
    lua_pushstring(L, s.c_str());
    return 1;
  }
  static int GetJacketPath(T* p, lua_State* L) {
    std::string s = p->GetJacketPath();
    if (!s.empty()) {
      lua_pushstring(L, s.c_str());
    } else {
      lua_pushnil(L);
    }
    return 1;
  }
  static int GetCDImagePath(T* p, lua_State* L) {
    std::string s = p->GetCDImagePath();
    if (!s.empty()) {
      lua_pushstring(L, s.c_str());
    } else {
      lua_pushnil(L);
    }
    return 1;
  }
  static int GetDiscPath(T* p, lua_State* L) {
    std::string s = p->GetDiscPath();
    if (!s.empty()) {
      lua_pushstring(L, s.c_str());
    } else {
      lua_pushnil(L);
    }
    return 1;
  }
  static int GetCDTitlePath(T* p, lua_State* L) {
    std::string s = p->GetCDTitlePath();
    if (!s.empty()) {
      lua_pushstring(L, s.c_str());
    } else {
      lua_pushnil(L);
    }
    return 1;
  }
  static int GetLyricsPath(T* p, lua_State* L) {
    std::string s = p->GetLyricsPath();
    if (!s.empty()) {
      lua_pushstring(L, s.c_str());
    } else {
      lua_pushnil(L);
    }
    return 1;
  }
  static int GetSongFilePath(T* p, lua_State* L) {
    lua_pushstring(L, p->GetSongFilePath().c_str());
    return 1;
  }
  static int IsTutorial(T* p, lua_State* L) {
    lua_pushboolean(L, p->IsTutorial());
    return 1;
  }
  static int IsEnabled(T* p, lua_State* L) {
    lua_pushboolean(L, p->GetEnabled());
    return 1;
  }
  static int IsCustomSong(T* p, lua_State* L) {
    lua_pushboolean(L, p->loaded_from_profile_ != ProfileSlot_Invalid);
    return 1;
  }
  static int GetGroupName(T* p, lua_State* L) {
    lua_pushstring(L, p->group_name_.c_str());
    return 1;
  }

  static int MusicLengthSeconds(T* p, lua_State* L) {
    lua_pushnumber(L, p->music_length_seconds_);
    return 1;
  }
  static int GetSampleStart(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetPreviewStartSeconds());
    return 1;
  }
  static int GetSampleLength(T* p, lua_State* L) {
    lua_pushnumber(L, p->music_sample_length_seconds_);
    return 1;
  }
  static int IsLong(T* p, lua_State* L) {
    lua_pushboolean(L, p->IsLong());
    return 1;
  }
  static int IsMarathon(T* p, lua_State* L) {
    lua_pushboolean(L, p->IsMarathon());
    return 1;
  }
  static int HasStepsType(T* p, lua_State* L) {
    StepsType st = Enum::Check<StepsType>(L, 1);
    lua_pushboolean(L, p->HasStepsType(st));
    return 1;
  }
  static int HasStepsTypeAndDifficulty(T* p, lua_State* L) {
    StepsType st = Enum::Check<StepsType>(L, 1);
    Difficulty dc = Enum::Check<Difficulty>(L, 2);
    lua_pushboolean(L, p->HasStepsTypeAndDifficulty(st, dc));
    return 1;
  }
  static int IsStepsUsingDifferentTiming(T* p, lua_State* L) {
    lua_pushboolean(
        L, p->IsStepsUsingDifferentTiming(Luna<Steps>::check(L, 1, true)));
    return 1;
  }
  /* TODO: HasStepsTypeAndDifficulty and GetOneSteps should be in
   * a SongUtil Lua table and a method of Steps. */
  static int GetOneSteps(T* p, lua_State* L) {
    StepsType st = Enum::Check<StepsType>(L, 1);
    Difficulty dc = Enum::Check<Difficulty>(L, 2);
    Steps* steps = SongUtil::GetOneSteps(p, st, dc);
    if (steps) {
      steps->PushSelf(L);
    } else {
      lua_pushnil(L);
    }
    return 1;
  }
  static int GetTimingData(T* p, lua_State* L) {
    p->song_timing_.PushSelf(L);
    return 1;
  }
  static int GetBGChanges(T* p, lua_State* L) {
    const std::vector<BackgroundChange>& changes =
        p->GetBackgroundChanges(BACKGROUND_LAYER_1);
    lua_createtable(L, changes.size(), 0);
    for (size_t c = 0; c < changes.size(); ++c) {
      lua_createtable(L, 0, 8);
      lua_pushnumber(L, changes[c].start_beat_);
      lua_setfield(L, -2, "start_beat");
      lua_pushnumber(L, changes[c].rate_);
      lua_setfield(L, -2, "rate");
      LuaHelpers::Push(L, changes[c].transition_);
      lua_setfield(L, -2, "transition");
      LuaHelpers::Push(L, changes[c].def_.effect_);
      lua_setfield(L, -2, "effect");
      LuaHelpers::Push(L, changes[c].def_.file1_);
      lua_setfield(L, -2, "file1");
      LuaHelpers::Push(L, changes[c].def_.file2_);
      lua_setfield(L, -2, "file2");
      LuaHelpers::Push(L, changes[c].def_.color1_);
      lua_setfield(L, -2, "color1");
      LuaHelpers::Push(L, changes[c].def_.color2_);
      lua_setfield(L, -2, "color2");
      lua_rawseti(L, -2, c + 1);
    }
    return 1;
  }
  // has functions
  static int HasMusic(T* p, lua_State* L) {
    lua_pushboolean(L, p->HasMusic());
    return 1;
  }
  static int HasBanner(T* p, lua_State* L) {
    lua_pushboolean(L, p->HasBanner());
    return 1;
  }
  static int HasBackground(T* p, lua_State* L) {
    lua_pushboolean(L, p->HasBackground());
    return 1;
  }
  static int HasPreviewVid(T* p, lua_State* L) {
    lua_pushboolean(L, p->HasPreviewVid());
    return 1;
  }
  static int HasJacket(T* p, lua_State* L) {
    lua_pushboolean(L, p->HasJacket());
    return 1;
  }
  static int HasDisc(T* p, lua_State* L) {
    lua_pushboolean(L, p->HasDisc());
    return 1;
  }
  static int HasCDImage(T* p, lua_State* L) {
    lua_pushboolean(L, p->HasCDImage());
    return 1;
  }
  static int HasCDTitle(T* p, lua_State* L) {
    lua_pushboolean(L, p->HasCDTitle());
    return 1;
  }
  static int HasBGChanges(T* p, lua_State* L) {
    lua_pushboolean(L, p->HasBGChanges());
    return 1;
  }
  static int HasLyrics(T* p, lua_State* L) {
    lua_pushboolean(L, p->HasLyrics());
    return 1;
  }
  // functions that AJ loves
  static int HasSignificantBPMChangesOrStops(T* p, lua_State* L) {
    lua_pushboolean(L, p->HasSignificantBpmChangesOrStops());
    return 1;
  }
  static int HasEdits(T* p, lua_State* L) {
    StepsType st = Enum::Check<StepsType>(L, 1);
    lua_pushboolean(L, p->HasEdits(st));
    return 1;
  }
  static int IsEasy(T* p, lua_State* L) {
    StepsType st = Enum::Check<StepsType>(L, 1);
    lua_pushboolean(L, p->IsEasy(st));
    return 1;
  }
  static int GetStepsSeconds(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetStepsSeconds());
    return 1;
  }
  static int NormallyDisplayed(T* p, lua_State* L) {
    lua_pushboolean(L, p->NormallyDisplayed());
    return 1;
  }
  static int ShowInDemonstrationAndRanking(T* p, lua_State* L) {
    lua_pushboolean(L, p->ShowInDemonstrationAndRanking());
    return 1;
  }
  static int GetFirstSecond(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetFirstSecond());
    return 1;
  }
  static int GetLastSecond(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetLastSecond());
    return 1;
  }
  static int GetFirstBeat(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetFirstBeat());
    return 1;
  }
  static int GetLastBeat(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetLastBeat());
    return 1;
  }
  static int HasAttacks(T* p, lua_State* L) {
    lua_pushboolean(L, p->HasAttacks());
    return 1;
  }
  static int GetDisplayBpms(T* p, lua_State* L) {
    DisplayBpms temp;
    p->GetDisplayBpms(temp);
    float min = temp.GetMin();
    float max = temp.GetMax();
    std::vector<float> bp_ms;
    bp_ms.push_back(min);
    bp_ms.push_back(max);
    LuaHelpers::CreateTableFromArray(bp_ms, L);
    return 1;
  }
  static int IsDisplayBpmSecret(T* p, lua_State* L) {
    DisplayBpms temp;
    p->GetDisplayBpms(temp);
    lua_pushboolean(L, temp.IsSecret());
    return 1;
  }
  static int IsDisplayBpmConstant(T* p, lua_State* L) {
    DisplayBpms temp;
    p->GetDisplayBpms(temp);
    lua_pushboolean(L, temp.BpmIsConstant());
    return 1;
  }
  static int IsDisplayBpmRandom(T* p, lua_State* L) {
    lua_pushboolean(L, p->display_bpm_type_ == DISPLAY_BPM_RANDOM);
    return 1;
  }
  static int ReloadFromSongDir(T* p, lua_State* L) {
    p->ReloadFromSongDir();
    COMMON_RETURN_SELF;
  }

  LunaSong() {
    ADD_METHOD(GetDisplayFullTitle);
    ADD_METHOD(GetTranslitFullTitle);
    ADD_METHOD(GetDisplayMainTitle);
    ADD_METHOD(GetMainTitle);
    ADD_METHOD(GetTranslitMainTitle);
    ADD_METHOD(GetDisplaySubTitle);
    ADD_METHOD(GetTranslitSubTitle);
    ADD_METHOD(GetDisplayArtist);
    ADD_METHOD(GetTranslitArtist);
    ADD_METHOD(GetGenre);
    ADD_METHOD(GetOrigin);
    ADD_METHOD(GetAllSteps);
    ADD_METHOD(GetStepsByStepsType);
    ADD_METHOD(GetSongDir);
    ADD_METHOD(GetMusicPath);
    ADD_METHOD(GetBannerPath);
    ADD_METHOD(GetBackgroundPath);
    ADD_METHOD(GetJacketPath);
    ADD_METHOD(GetCDImagePath);
    ADD_METHOD(GetDiscPath);
    ADD_METHOD(GetCDTitlePath);
    ADD_METHOD(GetLyricsPath);
    ADD_METHOD(GetSongFilePath);
    ADD_METHOD(IsTutorial);
    ADD_METHOD(IsEnabled);
    ADD_METHOD(IsCustomSong);
    ADD_METHOD(GetGroupName);
    ADD_METHOD(MusicLengthSeconds);
    ADD_METHOD(GetSampleStart);
    ADD_METHOD(GetSampleLength);
    ADD_METHOD(IsLong);
    ADD_METHOD(IsMarathon);
    ADD_METHOD(HasStepsType);
    ADD_METHOD(HasStepsTypeAndDifficulty);
    ADD_METHOD(GetOneSteps);
    ADD_METHOD(GetTimingData);
    ADD_METHOD(GetBGChanges);
    ADD_METHOD(HasMusic);
    ADD_METHOD(HasBanner);
    ADD_METHOD(HasBackground);
    ADD_METHOD(HasJacket);
    ADD_METHOD(HasCDImage);
    ADD_METHOD(HasDisc);
    ADD_METHOD(HasCDTitle);
    ADD_METHOD(HasBGChanges);
    ADD_METHOD(HasLyrics);
    ADD_METHOD(HasSignificantBPMChangesOrStops);
    ADD_METHOD(HasEdits);
    ADD_METHOD(IsEasy);
    ADD_METHOD(GetStepsSeconds);
    ADD_METHOD(NormallyDisplayed);
    ADD_METHOD(GetFirstBeat);
    ADD_METHOD(GetFirstSecond);
    ADD_METHOD(GetLastBeat);
    ADD_METHOD(GetLastSecond);
    ADD_METHOD(HasAttacks);
    ADD_METHOD(GetDisplayBpms);
    ADD_METHOD(IsDisplayBpmSecret);
    ADD_METHOD(IsDisplayBpmConstant);
    ADD_METHOD(IsDisplayBpmRandom);
    ADD_METHOD(IsStepsUsingDifferentTiming);
    ADD_METHOD(ShowInDemonstrationAndRanking);
    ADD_METHOD(HasPreviewVid);
    ADD_METHOD(GetPreviewVidPath);
    ADD_METHOD(GetPreviewMusicPath);
    ADD_METHOD(ReloadFromSongDir);
  }
};

LUA_REGISTER_CLASS(Song)

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
