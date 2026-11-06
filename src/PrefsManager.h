#ifndef PREFSMANAGER_H
#define PREFSMANAGER_H

#include <map>
#include <string>

#include "GameConstantsAndTypes.h"
#include "Preference.h"

class IniFile;

void ValidateDisplayAspectRatio(float& val);
void ValidateSongsPerPlay(int& val);

/** @brief How many songs can be played during a normal game max?
 *
 * This assumes no extra stages, no event mode, no course modes. */
const int MAX_SONGS_PER_PLAY = 7;

enum MusicWheelUsesSections {
  MusicWheelUsesSections_NEVER,
  MusicWheelUsesSections_ALWAYS,
  MusicWheelUsesSections_ABC_ONLY,
  NUM_MusicWheelUsesSections,
  MusicWheelUsesSections_Invalid
};
/** @brief The options for allowing the W1 timing. */
enum AllowW1 {
  ALLOW_W1_NEVER,        /**< The W1 timing is not used. */
  ALLOW_W1_COURSES_ONLY, /**< The W1 timing is used for courses only. */
  ALLOW_W1_EVERYWHERE,   /**< The W1 timing is used for all modes. */
  NUM_AllowW1,
  AllowW1_Invalid
};
enum Maybe { Maybe_ASK, Maybe_NO, Maybe_YES, NUM_Maybe, Maybe_Invalid };
enum GetRankingName {
  RANKING_OFF,
  RANKING_ON,
  RANKING_LIST,
  NUM_GetRankingName,
  GetRankingName_Invalid
};
enum RandomBackgroundMode {
  BGMODE_OFF,
  BGMODE_ANIMATIONS,
  BGMODE_RANDOMMOVIES,
  NUM_RandomBackgroundMode,
  RandomBackgroundMode_Invalid
};
enum ShowDancingCharacters {
  SDC_Off,
  SDC_Random,
  SDC_Select,
  NUM_ShowDancingCharacters,
  ShowDancingCharacters_Invalid
};
enum ImageCacheMode {
  IMGCACHE_OFF,
  IMGCACHE_LOW_RES_PRELOAD,         // preload low-res on start
  IMGCACHE_LOW_RES_LOAD_ON_DEMAND,  // preload low-res on screen load
  IMGCACHE_FULL,
  NUM_ImageCacheMode,
  ImageCacheMode_Invalid
};
enum HighResolutionTextures {
  HighResolutionTextures_Auto,
  HighResolutionTextures_ForceOff,
  HighResolutionTextures_ForceOn,
  NUM_HighResolutionTextures,
  HighResolutionTextures_Invalid,
};
enum AttractSoundFrequency {
  ASF_NEVER,
  ASF_EVERY_TIME,
  ASF_EVERY_2_TIMES,
  ASF_EVERY_3_TIMES,
  ASF_EVERY_4_TIMES,
  ASF_EVERY_5_TIMES,
  NUM_AttractSoundFrequency,
  AttractSoundFrequency_Invalid
};
enum CourseSortOrders {
  COURSE_SORT_PREFERRED,
  COURSE_SORT_SONGS,
  COURSE_SORT_METER,
  COURSE_SORT_METER_SUM,
  COURSE_SORT_RANK,
  NUM_CourseSortOrders,
  CourseSortOrders_Invalid
};
enum BackgroundFitMode {
  BFM_CoverDistort,
  BFM_CoverPreserve,
  BFM_FitInside,
  BFM_FitInsideAvoidLetter,
  BFM_FitInsideAvoidPillar,
  NUM_BackgroundFitMode,
  BackgroundFitMode_Invalid
};

// Profile sort orders exist for sorting the list of profiles.
// Regardless of sort order, Guest profiles are always at the top of the list
// and Test profiles are always at the bottom of the list.
enum ProfileSortOrder {
  ProfileSortOrder_Priority,      // Sort based on the Priority defined in a
                                  // profile's Type.ini
  ProfileSortOrder_Recent,        // Sorts profiles by most recently used.
  ProfileSortOrder_Alphabetical,  // Sorts profiles alphabetically.
  ProfileSortOrder_CreationTime,  // Sorts profiles by creation time.
  NUM_ProfileSortOrder,
  ProfileSortOrder_Invalid
};

enum SyncOffset {
  SyncOffset_NULL,
  SyncOffset_ITG,
  NUM_SyncOffset,
  SyncOffset_Invalid
};

/** @brief Holds user-chosen preferences that are saved between sessions. */
class PrefsManager {
 public:
  PrefsManager();
  ~PrefsManager();

  void Init();

  void SetCurrentGame(const std::string& sGame);
  std::string GetCurrentGame() { return current_game_; }

 protected:
  Preference<std::string> current_game_;

 public:
  // Game-specific prefs.  Copy these off and save them every time the game
  // changes
  Preference<std::string> announcer_;
  Preference<std::string> theme_;
  Preference<std::string> default_modifiers_;

 protected:
  void StoreGamePrefs();
  void RestoreGamePrefs();
  struct GamePrefs {
    // See GamePrefs::GamePrefs in PrefsManager.cpp for some default settings
    GamePrefs();

    std::string announcer_;
    std::string theme_;
    std::string default_modifiers_;
  };
  std::map<std::string, GamePrefs> map_game_name_to_game_prefs_;

 public:
  Preference<bool> windowed_;
  Preference<std::string> display_id_;
  Preference<int> display_width_;
  Preference<int> display_height_;
  Preference<float> display_aspect_ratio_;
  Preference<int> display_color_depth_;
  Preference<int> texture_color_depth_;
  Preference<int> movie_color_depth_;
  Preference<bool> stretch_backgrounds_;
  Preference<BackgroundFitMode> bg_fit_mode_;
  Preference<HighResolutionTextures> high_resolution_textures_;
  Preference<int> max_texture_resolution_;
  Preference<int> refresh_rate_;
  Preference<bool> allow_multitexture_;
  Preference<bool> fullscreen_is_borderless_window_;
  Preference<bool> show_stats_;
  Preference<bool> show_banners_;
  Preference<bool> show_mouse_cursor_;

  Preference<bool> hidden_songs_;
  Preference<bool> vsync_;
  Preference<bool> fast_note_rendering_;
  Preference<bool> interlaced_;
  Preference<bool> pal_;
  Preference<bool> delayed_texture_delete_;
  Preference<bool> delayed_model_delete_;
  Preference<ImageCacheMode> image_cache_;
  Preference<bool> fast_load_;
  Preference<std::string> never_cache_list_;

  Preference<bool> only_dedicated_menu_buttons_;
  Preference<bool> menu_timer_;

  Preference<float> life_difficulty_scale_;

  /**
   * Whether ratemod should affect playback speeds of FGChanges or not.
   * Originally introduced in NotITG.
   */
  Preference<bool> rate_mods_affect_tweens_;

  // Whoever added these: Please add a comment saying what they do. -Chris
  Preference<int> regen_combo_after_miss_;  // combo that must be met after a
                                            // Miss to regen life
  Preference<int>
      max_regen_combo_after_miss_;   // caps RegenComboAfterMiss if multiple
                                     // Misses occur in rapid succession
  Preference<bool> merciful_drain_;  // negative life deltas are scaled by the
                                     // players life percentage
  Preference<bool> harsh_hot_life_penalty_;         // See LifeMeterBar.cpp -Kyz
  Preference<bool> minimum1_full_song_in_courses_;  // FEoS for 1st song,
                                                    // FailImmediate thereafter
  Preference<bool> fail_off_in_beginner_;
  Preference<bool> fail_off_for_first_stage_easy_;
  Preference<bool> merciful_beginner_;  // don't subtract from percent score or
                                        // grade DP, larger W5 window
  Preference<bool> merciful_super_meter_;  // negative super deltas are scaled
                                           // by the players life percentage
  Preference<bool> delayed_back_;
  Preference<bool> allow_hold_for_options_;
  Preference<bool> show_instructions_;  // how to play a mode
  Preference<bool> show_caution_;
  Preference<bool> show_native_language_;
  Preference<int> arcade_options_navigation_;
  Preference<bool> three_key_navigation_;
  Preference<MusicWheelUsesSections> music_wheel_uses_sections_;
  Preference<int> music_wheel_switch_speed_;
  // Should Random sorts prioritize finding a song of your preferred difficulty?
  // This means that if your preferred difficulty is Challenge, it will find a
  // random song with a Challenge difficulty (If it can't find one after so many
  // attempts, it will stop prioritizing difficulty and find any random playable
  // song)
  Preference<bool> random_prioritize_difficulty_;
  Preference<AllowW1>
      allow_w1_;  // this should almost always be on, given use cases. -aj
  Preference<bool> event_mode_;
  Preference<int> coins_per_credit_;
  Preference<int> max_num_credits_;
  Preference<int> songs_per_play_;
  Preference<bool> delayed_credits_reconcile_;  // zuh?
  Preference<bool> combo_continues_between_songs_;
  Preference<bool> allow_multiple_toasties_;
  Preference<TapNoteScore> min_tns_to_hide_notes_;
  Preference<Maybe> show_song_options_;
  Preference<bool> percentage_scoring_;
  Preference<float> min_percentage_for_machine_song_high_score_;
  Preference<float> min_percentage_for_machine_course_high_score_;
  Preference<bool> disqualification_;
  Preference<bool> autogen_steps_;
  Preference<bool> autogen_group_courses_;
  Preference<bool> only_preferred_difficulties_;
  Preference<bool> break_combo_to_get_item_;
  Preference<bool> lock_course_difficulties_;
  Preference<ShowDancingCharacters> show_dancing_characters_;
  Preference<bool> use_unlock_system_;
  Preference<float> global_offset_seconds_;
  Preference<SyncOffset> default_sync_offset_;
  Preference<int> progressive_lifebar_;
  Preference<int> progressive_stage_lifebar_;
  Preference<int> progressive_nonstop_lifebar_;
  Preference<bool> show_beginner_helper_;
  Preference<bool> disable_screen_saver_;
  Preference<std::string> language_;
  Preference<std::string>
      memory_card_profile_subdir_;  // the directory on a memory card to look in
                                    // for a profile
  Preference<int> product_id_;      // Saved in HighScore to track what software
                                    // version a score came from.
  Preference<int> center_image_translate_x_;
  Preference<int> center_image_translate_y_;
  Preference<int> center_image_add_width_;
  Preference<int> center_image_add_height_;
  Preference<AttractSoundFrequency> attract_sound_frequency_;
  Preference<bool> allow_extra_stage_;
  Preference<int> max_high_scores_per_list_for_machine_;
  Preference<int> max_high_scores_per_list_for_player_;
  Preference<bool> allow_multiple_high_score_with_same_name_;
  Preference<bool> disable_upload_dir_;
  Preference<bool> cel_shade_models_;
  Preference<bool> preferred_sort_uses_groups_;
  Preference<float>
      debounce_coin_input_time_;  // allow users to apply a distinct debounce to
                                  // coin input
  Preference<bool> reset_coins_at_startup_;

  // Number of seconds it takes for a button on the controller to release
  // after pressed.
  Preference<float> pad_stick_seconds_;

  // Lead in time before recording starts in edit mode.
  Preference<float> edit_record_mode_lead_in_;
  // User is prompted on clearing an area with more than this note count.
  Preference<int> edit_clear_prompt_threshold_;

  // Useful for non 4:3 displays and resolutions < 640x480 where texels don't
  // map directly to pixels.
  Preference<bool> force_mip_maps_;
  Preference<bool> trilinear_filtering_;  // has no effect without mipmaps on
  Preference<bool>
      anisotropic_filtering_;  // has no effect without mipmaps on.  Not
                               // mutually exclusive with trilinear.

  // If true, then signatures created when writing profile data and verified
  // when reading profile data. Leave this false if you want to use a profile
  // on different machines that don't have the same key, or else the
  // profile's data will be discarded.
  Preference<bool> sign_profile_data_;

  // Used to control the ordering of the player profiles.
  // See the the definition of ProfileSortOrder above about the available sort
  // options.
  Preference<ProfileSortOrder> profile_sort_order_;

  // Determines whether the ProfileSortOrder is in ascending order (true) or
  // descending order (false).
  Preference<bool> profile_sort_order_ascending_;

  // course ranking
  Preference<CourseSortOrders> course_sort_order_;
  Preference<bool> sub_sort_by_num_steps_;
  Preference<GetRankingName> get_ranking_name_;

  Preference<std::string> additional_song_folders_read_only_;
  Preference<std::string> additional_song_folders_writable_;
  Preference<std::string> additional_course_folders_read_only_;
  Preference<std::string> additional_course_folders_writable_;
  Preference<std::string> additional_folders_read_only_;
  Preference<std::string> additional_folders_writable_;

  // failsafe
  Preference<std::string> default_theme_;

  Preference<std::string> last_seen_video_driver_;
  Preference<std::string> video_renderers_;  // StepMania.cpp sets these on
                                             // first run based on the card
  Preference<bool> smooth_lines_;
  Preference<int> sound_write_ahead_;
  Preference<std::string> sound_device_;
  Preference<int> sound_preferred_sample_rate_;
  Preference<std::string> lights_steps_difficulty_;
  Preference<bool> lights_simplify_bass_;
  Preference<bool> lights_bass_parallel_;
  Preference<bool> allow_unaccelerated_renderer_;
  Preference<bool> threaded_input_;
  Preference<bool> threaded_movie_decode_;
  Preference<std::string> test_initial_screen_;
  Preference<bool> debug_lights_;
  Preference<bool> monkey_input_;
  Preference<std::string> machine_name_;
  Preference<std::string> courses_to_show_ranking_;
  Preference<bool> mute_actions_;
  Preference<bool>
      allow_song_deletion_;  // Allow the user to remove songs from their
                             // collection through UI / keyboard shortcut

  /** @brief Enable some quirky behavior used by some older versions of
   * StepMania. */
  Preference<bool> quirks_mode_;

  Preference<bool> custom_songs_enable_;
  Preference<unsigned int> custom_songs_max_count_;
  Preference<float> custom_songs_load_timeout_;
  Preference<float> custom_songs_max_seconds_;
  Preference<float> custom_songs_max_megabytes_;

  // Debug:
  Preference<bool> debug_menu_enabled_;
  Preference<bool> log_to_disk_;
  Preference<bool> force_log_flush_;
  Preference<bool> show_log_output_;
  Preference<bool> log_skips_;
  Preference<bool> log_checkpoints_;
  Preference<bool> show_loading_window_;
  Preference<bool> pseudo_localize_;
  Preference<bool> show_theme_errors_;

  void ReadPrefsFromIni(
      const IniFile& ini, const std::string& sSection, bool bIsStatic);
  void ReadGamePrefsFromIni(const std::string& sIni);
  void ReadDefaultsFromIni(const IniFile& ini, const std::string& sSection);
  void SavePrefsToIni(IniFile& ini);

  void ReadPrefsFromDisk();
  void SavePrefsToDisk();

  void ResetToFactoryDefaults();

  std::string GetPreferencesSection() const;

  // Lua
  void PushSelf(lua_State* L);

 protected:
  void ReadPrefsFromFile(
      const std::string& sIni, const std::string& sSection, bool bIsStatic);
  void ReadDefaultsFromFile(
      const std::string& sIni, const std::string& sSection);
  void TranslateDeprecatedFlags();

  Preference<std::string> additional_song_folders_;    // deprecated
  Preference<std::string> additional_course_folders_;  // deprecated
  Preference<std::string> additional_folders_;         // deprecated
};

/* This is global, because it can be accessed by crash handlers and error
 * handlers that are run after PREFSMAN shuts down (and probably don't want to
 * deref that pointer anyway). */
extern bool g_bAutoRestart;

extern PrefsManager*
    PREFSMAN;  // global and accessible from anywhere in our program

#endif

/**
 * @file
 * @author Chris Danford, Chris Gomez (c) 2001-2004
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
