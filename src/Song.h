#ifndef SONG_H
#define SONG_H

#include <array>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "Attack.h"
#include "Difficulty.h"
#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "RageUtil_AutoPtr.h"
#include "Steps.h"
#include "TimingData.h"

class Style;
class StepsID;
struct lua_State;
struct BackgroundChange;

void FixupPath(std::string& path, const std::string& song_path);
std::string GetSongAssetPath(std::string path, const std::string& song_path);

/** @brief The version of the .ssc file format. */
const static float STEPFILE_VERSION_NUMBER = 0.83f;

/** @brief How many edits for this song can each profile have? */
const int MAX_EDITS_PER_SONG_PER_PROFILE = 15;
/** @brief How many edits for this song can be available? */
const int MAX_EDITS_PER_SONG = MAX_EDITS_PER_SONG_PER_PROFILE * NUM_ProfileSlot;

extern const int FILE_CACHE_VERSION;

/** @brief The different background layers available. */
enum BackgroundLayer {
  BACKGROUND_LAYER_1,
  BACKGROUND_LAYER_2,
  // BACKGROUND_LAYER_3, // StepCollection get
  NUM_BackgroundLayer,
  BACKGROUND_LAYER_Invalid
};

/** @brief A custom foreach loop for the different background layers. */
#define FOREACH_BackgroundLayer(bl) FOREACH_ENUM(BackgroundLayer, bl)

/** @brief The collection of lyrics for the Song. */
struct LyricSegment {
  float start_time_;  /** @brief When does the lyric show up? */
  std::string lyric_; /** @brief The lyrics themselves. */
  RageColor color_;   /** @brief The color of the lyrics. */
};

/** @brief Holds all non-Steps metadata for one song.
 *  This is separate from Song to make copying all non-Steps metadata easier
 *  without manually specifying every individual member variable.
 */
struct SongData {
  SongData();

  /** @brief When should this song be displayed in the music wheel? */
  enum SelectionDisplay {
    SHOW_ALWAYS, /**< always show on the wheel. */
    SHOW_NEVER /**< never show on the wheel (unless song hiding is turned off).
                */
  } selection_display_;

  /**
   * @brief Filename associated with this file.
   * This will always have a .SSC extension. If we loaded a .SSC, this will
   * point to it, but if we loaded any other type, this will point to
   * a generated .SSC filename. */
  std::string song_file_name_;

  /** @brief The group this Song is in. */
  std::string group_name_;

  /** @brief The base directory name that this Song is in. */
  std::string song_name_;

  /**
   * @brief the Profile this came from.
   * This is ProfileSlot_Invalid if it wasn't loaded from a profile. */
  ProfileSlot loaded_from_profile_;
  bool enabled_;

  /** @brief The title of the Song. */
  std::string main_title_;
  /** @brief The subtitle of the Song, if it exists. */
  std::string sub_title_;
  /** @brief The artist of the Song, if it exists. */
  std::string artist_;
  /** @brief The transliterated title of the Song, if it exists. */
  std::string main_title_translit_;
  /** @brief The transliterated subtitle of the Song, if it exists. */
  std::string sub_title_translit_;
  /** @brief The transliterated artist of the Song, if it exists. */
  std::string artist_translit_;

  /** @brief The version of the song/file. */
  float version_;
  /** @brief The genre of the song/file. */
  std::string genre_;

  /**
   * @brief The person who worked with the song file who should be credited.
   * This is read and saved, but never actually used. */
  std::string credit_;

  std::string origin_;  // song origin (for .ssc format)

  std::string music_file_;
  std::string preview_file_;

  /** @brief The length of the music file. */
  float music_length_seconds_;
  float music_sample_start_seconds_;
  float music_sample_length_seconds_;
  DisplayBPM display_bpm_type_;
  float specified_bpm_min_;
  float specified_bpm_max_;  // if a range, then Min != Max

  std::string banner_file_;  // typically a 16:5 ratio graphic (e.g. 256x80)
  std::string jacket_file_;  // typically square (e.g. 192x192, 256x256)
  std::string cd_file_;      // square (e.g. 128x128 [DDR 1st-3rd])
  std::string disc_file_;  // rectangular (e.g. 256x192 [Pump], 200x150 [MGD3])
  /** @brief The location of the lyrics file, if it exists. */
  std::string lyrics_file_;
  std::string background_file_;
  std::string cd_title_file_;
  std::string preview_vid_file_;

  AttackArray attacks_;
  std::vector<std::string> attack_string_;

  // For loading only:
  bool has_music_, has_banner_, has_background_;

  /** @brief The Song's TimingData. */
  TimingData song_timing_;

  typedef std::vector<BackgroundChange> VBackgroundChange;

  /**
   * @brief The list of LyricSegments.
   * This must be sorted before gameplay. */
  std::vector<LyricSegment> lyric_segments_;

  /**
   * @brief An array of keysound file names (e.g. "beep.wav").
   * The index in this array corresponds to the index in TapNote.
   * If you  change the index in here, you must change all NoteData too.
   * Any note that doesn't have a value in the range of this array
   * means "this note doesn't have a keysound". */
  std::vector<std::string> keysound_file_;

 protected:
  std::string song_dir_;
  std::string pre_customify_song_dir_;

  /** @brief The first second that a note is hit. */
  float first_second_;
  /** @brief The last second that a note is hit. */
  float last_second_;
  /** @brief The last second of the song for playing purposes. */
  float specified_last_second_;
  /**
   * @brief The background changes (sorted by layer) that are for this Song.
   * This uses an AutoPtr instead of a raw pointer so that the
   * auto gen'd copy constructor works correctly.
   * This must be sorted before gameplay. */
  AutoPtrCopyOnWrite<VBackgroundChange>
      background_changes_[NUM_BackgroundLayer];
  /**
   * @brief The foreground changes that are for this Song.
   * This uses an AutoPtr instead of a raw pointer so that the
   * auto gen'd copy constructor works correctly.
   * This must be sorted before gameplay. */
  AutoPtrCopyOnWrite<VBackgroundChange> foreground_changes_;

  bool loaded_from_autosave_;
};

/** @brief Holds all music metadata and steps for one song. */
class Song : public SongData {
 public:
  void SetSongDir(const std::string dir) { song_dir_ = dir; }
  std::string GetSongDir() { return song_dir_; }
  std::string GetPreCustomifyDir() { return pre_customify_song_dir_; }

  Song();
  ~Song();
  Song(const Song&) = delete;
  Song& operator=(const Song&) = delete;
  Song(Song&&) = delete;
  Song& operator=(Song&&) = delete;
  void Reset();
  void CopyMetadataFrom(const SongData& other);

  /**
   * @brief Load a song from the chosen directory.
   *
   * This assumes that there is no song present right now.
   * @param sDir the song directory from which to load.
   *
   * TODO: make this a static factory method.
   */
  bool LoadFromSongDir(
      std::string dir, bool load_autosave = false,
      ProfileSlot from_profile = ProfileSlot_Invalid);
  // This one takes the effort to reuse Steps pointers as best as it can
  bool ReloadFromSongDir(std::string dir);
  bool ReloadFromSongDir() { return ReloadFromSongDir(GetSongDir()); }
  void LoadEditsFromSongDir(std::string dir);

  bool HasAutosaveFile();
  bool LoadAutosaveFile();

  /**
   * @brief Call this after loading a song to clean up invalid data.
   * @param fromCache was this data loaded from the cache file? */
  void TidyUpData(bool from_cache = false);

  /**
   * @brief Normalize background changes parsed from an SM/SSC simfile.
   */
  void FixupBackgroundChanges();

  /**
   * @brief Get the new step stats, and determine the last second at the same
   * time. This is called by TidyUpData, after saving the Song.
   * @param wipeNoteData release each chart's decompressed NoteData after
   * computing its stats. */
  void ReCalculateStepStatsAndLastSecond(bool wipe_note_data);
  /**
   * @brief Translate any titles that aren't in english.
   * This is called by TidyUpData. */
  void TranslateTitles();

  /**
   * @brief Save to the new SSC file format.
   * @param sPath the path where we're saving the file.
   * @param autosave a flag to determine if we're saving an autosave file.
   */
  bool SaveToSSCFile(std::string path, bool autosave = false);
  /** @brief Save to the SSC and SM files no matter what. */
  void Save(bool autosave = false);
  /**
   * @brief Save the current Song to a cache file using the preferred format.
   * @return its success or failure. */
  bool SaveToCacheFile();
  /**
   * @brief Save the current Song to a SM file.
   * @return its success or failure. */
  bool SaveToSMFile();

  void RemoveAutosave();
  bool WasLoadedFromAutosave() const { return loaded_from_autosave_; }

  const std::string& GetSongFilePath() const;
  std::string GetCacheFilePath() const;

  void AddAutoGenNotes();
  /**
   * @brief Automatically generate steps from one type to another.
   * @param ntTo the StepsType we're making.
   * @param ntFrom the StepsType we're generating from.
   */
  void AutoGen(StepsType to, StepsType from);
  void RemoveAutoGenNotes();

  // Directory this song data came from:
  const std::string& GetSongDir() const { return song_dir_; }

  /* If PREFSMAN->m_bShowNative is off, these are the same as GetTranslit*
   * below. Otherwise, they return the main titles. */
  std::string GetDisplayMainTitle() const;
  std::string GetDisplaySubTitle() const;
  std::string GetDisplayArtist() const;
  std::string GetMainTitle() const;

  /**
   * @brief Retrieve the transliterated title, or the main title if there is no
   * translit.
   * @return the proper title. */
  std::string GetTranslitMainTitle() const {
    return main_title_translit_.size() ? main_title_translit_ : main_title_;
  }
  /**
   * @brief Retrieve the transliterated subtitle, or the main subtitle if there
   * is no translit.
   * @return the proper subtitle. */
  std::string GetTranslitSubTitle() const {
    return sub_title_translit_.size() ? sub_title_translit_ : sub_title_;
  }
  /**
   * @brief Retrieve the transliterated artist, or the main artist if there is
   * no translit.
   * @return the proper artist. */
  std::string GetTranslitArtist() const {
    return artist_translit_.size() ? artist_translit_ : artist_;
  }

  // "title subtitle"
  std::string GetDisplayFullTitle() const;
  std::string GetTranslitFullTitle() const;

  static std::string GetSongAssetPath(
      std::string path, const std::string& song_path);
  std::string GetMusicPath() const;
  std::string GetBannerPath() const;
  std::string GetJacketPath() const;
  std::string GetCDImagePath() const;
  std::string GetDiscPath() const;
  std::string GetLyricsPath() const;
  std::string GetBackgroundPath() const;
  std::string GetCDTitlePath() const;
  std::string GetPreviewVidPath() const;
  std::string GetPreviewMusicPath() const;
  float GetPreviewStartSeconds() const;

  std::string GetCacheFile(std::string path);

  bool HasMusic() const;
  /**
   * @brief Does this song have a banner?
   * @return true if it does, false otherwise. */
  bool HasBanner() const;
  /**
   * @brief Does this song have a background image?
   * @return true if it does, false otherwise. */
  bool HasBackground() const;
  bool HasJacket() const;
  bool HasCDImage() const;
  bool HasDisc() const;
  bool HasCDTitle() const;
  // bool HasMovieBackground() const;
  bool HasBGChanges() const;
  bool HasLyrics() const;
  bool HasAttacks() const;
  bool HasPreviewVid() const;

  bool Matches(std::string group, std::string song) const;

  float GetFirstBeat() const;
  float GetFirstSecondNoOffset() const;
  float GetFirstSecond() const;
  float GetLastBeat() const;
  float GetLastSecondNoOffset() const;
  float GetLastSecond() const;
  float GetSpecifiedLastBeat() const;

  float GetSpecifiedLastSecond() const;

  void SetFirstSecondNoOffset(const float f);
  void SetLastSecondNoOffset(const float f);
  void SetSpecifiedLastSecond(const float f);

 private:
  void TidyUpData(
      bool from_cache, const std::set<std::string>& blacklisted_images);

  /**
   * @brief Derive the song directory, name, group, and source profile from a
   * directory. sDir must end in a slash. */
  void SetSongDirAndGroup(const std::string& dir, ProfileSlot from_profile);

 public:
  const std::vector<BackgroundChange>& GetBackgroundChanges(
      BackgroundLayer bl) const;
  std::vector<BackgroundChange>& GetBackgroundChanges(BackgroundLayer bl);
  const std::vector<BackgroundChange>& GetForegroundChanges() const;
  std::vector<BackgroundChange>& GetForegroundChanges();

  /* [splittiming]
          void AddBPMSegment( const BPMSegment &seg ) { m_Timing.AddBPMSegment(
     seg ); } void AddStopSegment( const StopSegment &seg ) {
     m_Timing.AddStopSegment( seg ); } void AddWarpSegment( const WarpSegment
     &seg ) { m_Timing.AddWarpSegment( seg ); }
  */
  void AddBackgroundChange(BackgroundLayer layer, BackgroundChange seg);
  void AddForegroundChange(BackgroundChange seg);
  void AddLyricSegment(LyricSegment seg);

  void GetDisplayBpms(DisplayBpms& add_to) const;
  /* [splittiming]
          float GetBPMAtBeat( float fBeat ) const { return
     m_Timing.GetBPMAtBeat( fBeat ); } void SetBPMAtBeat( float fBeat, float
     fBPM ) { m_Timing.SetBPMAtBeat( fBeat, fBPM ); } BPMSegment&
     GetBPMSegmentAtBeat( float fBeat ) { return m_Timing.GetBPMSegmentAtBeat(
     fBeat ); }
  */

  std::unique_ptr<Steps> CreateSteps();
  void InitSteps(Steps& steps);

  /* [splittiming]
  float SongGetBeatFromElapsedTime( float fElapsedTime ) const
  {
          return m_SongTiming.GetBeatFromElapsedTime( fElapsedTime );
  }
  float StepsGetBeatFromElapsedTime( float fElapsedTime, const Steps &steps )
  const
  {
          return steps.m_Timing.GetBeatFromElapsedTime( fElapsedTime );
  }

  float SongGetElapsedTimeFromBeat( float fBeat ) const
  {
          return m_SongTiming.GetElapsedTimeFromBeat( fBeat );
  }
  float StepsGetElapsedTimeFromBeat( float fBeat, const Steps &steps ) const
  {
          return steps.m_Timing.GetElapsedTimeFromBeat( fBeat );
  }
  */

  /* [splittiming]
  float GetBeatFromElapsedTime( float fElapsedTime ) const
  {
          return m_Timing.GetBeatFromElapsedTime( fElapsedTime );
  }
  float GetElapsedTimeFromBeat( float fBeat ) const { return
  m_Timing.GetElapsedTimeFromBeat( fBeat ); }
  */

  bool HasSignificantBpmChangesOrStops() const;
  float GetStepsSeconds() const;
  bool IsLong() const;
  bool IsMarathon() const;

  bool HasStepsType(StepsType st) const;
  bool HasStepsTypeAndDifficulty(StepsType st, Difficulty dc) const;
  // TODO: Allow for a non const version.
  const std::vector<Steps*>& GetAllSteps() const { return steps_; }
  const std::vector<std::unique_ptr<Steps>>& GetUnknownStyleSteps() const {
    return unknown_style_steps_;
  }
  const std::vector<Steps*>& GetStepsByStepsType(StepsType st) const {
    return steps_by_type_[st];
  }
  bool IsEasy(StepsType st) const;
  bool IsTutorial() const;
  bool HasEdits(StepsType st) const;

  void SetEnabled(bool b) { enabled_ = b; }
  bool GetEnabled() const { return enabled_; }
  /**
   * @brief Determine if the song should be shown on the MusicWheel normally.
   * Songs that are not displayed normally may still be available during
   * random selection, extra stages, or other special conditions.
   * @return true if displayed normally, false otherwise. */
  bool NormallyDisplayed() const;
  bool ShowInDemonstrationAndRanking() const;

  /**
   * @brief Add the chosen Steps to the Song.
   * @param steps the new steps. */
  void AddSteps(std::unique_ptr<Steps> steps);
  void DeleteSteps(const Steps* steps, bool re_auto_gen = true);

  void FreeAllLoadedFromProfile(
      ProfileSlot slot = ProfileSlot_Invalid,
      const std::set<Steps*>* set_in_use = nullptr);
  bool WasLoadedFromProfile() const {
    return loaded_from_profile_ != ProfileSlot_Invalid;
  }
  void GetStepsLoadedFromProfile(
      ProfileSlot slot, std::vector<Steps*>& steps_out) const;
  int GetNumStepsLoadedFromProfile(ProfileSlot slot) const;
  bool IsEditAlreadyLoaded(const Steps& steps) const;

  bool IsStepsUsingDifferentTiming(Steps* steps) const;
  bool AnyChartUsesSplitTiming() const;

  std::string GetAttackString() const {
    return join(":", this->attack_string_);
  }

  // Lua
  void PushSelf(lua_State* L);

 private:
  std::vector<std::unique_ptr<Steps>> owned_steps_;
  /** @brief the Steps that belong to this Song. */
  std::vector<Steps*> steps_;
  /** @brief the Steps of a particular StepsType that belong to this Song. */
  std::array<std::vector<Steps*>, NUM_StepsType> steps_by_type_;
  /** @brief the Steps that are of unrecognized Styles. */
  std::vector<std::unique_ptr<Steps>> unknown_style_steps_;
};

#endif

/**
 * @file
 * @author Chris Danford, Glenn Maynard (c) 2001-2004
 * @section LICENSE
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
