#include "PrefsManager.h"

#include <map>
#include <string>

#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "IniFile.h"
#include "LuaManager.h"
#include "Preference.h"
#include "ProductInfo.h"
#include "RageDisplay.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "SpecialFiles.h"
#include "StdString.h"
#include "XmlFile.h"
#include "global.h"

// DEFAULTS_INI_PATH	= "Data/Defaults.ini";		// these can be
// overridden PREFERENCES_INI_PATH	// overlay on Defaults.ini, contains the
// user's choices STATIC_INI_PATH	= "Data/Static.ini";		//
// overlay on the 2 above, can't be overridden TYPE_TXT_FILE	=
// "Data/Type.txt";

PrefsManager* PREFSMAN =
    nullptr;  // global and accessible from anywhere in our program

static const char* MusicWheelUsesSectionsNames[] = {
    "Never",
    "Always",
    "ABCOnly",
};
XToString(MusicWheelUsesSections);
StringToX(MusicWheelUsesSections);
LuaXType(MusicWheelUsesSections);

static const char* AllowW1Names[] = {
    "Never",
    "CoursesOnly",
    "Everywhere",
};
XToString(AllowW1);
StringToX(AllowW1);
LuaXType(AllowW1);

static const char* MaybeNames[] = {
    "Ask",
    "No",
    "Yes",
};
XToString(Maybe);
StringToX(Maybe);
LuaXType(Maybe);

static const char* GetRankingNameNames[] = {
    "Off",
    "On",
    "List",
};
XToString(GetRankingName);
StringToX(GetRankingName);
LuaXType(GetRankingName);

static const char* RandomBackgroundModeNames[] = {
    "Off",
    "Animations",
    "RandomMovies",
};
XToString(RandomBackgroundMode);
StringToX(RandomBackgroundMode);
LuaXType(RandomBackgroundMode);

static const char* ShowDancingCharactersNames[] = {
    "Off",
    "Random",
    "Select",
};
XToString(ShowDancingCharacters);
StringToX(ShowDancingCharacters);
LuaXType(ShowDancingCharacters);

static const char* ImageCacheModeNames[] = {
    "Off", "LowResPreload", "LowResLoadOnDemand", "Full"};
XToString(ImageCacheMode);
StringToX(ImageCacheMode);
LuaXType(ImageCacheMode);

static const char* HighResolutionTexturesNames[] = {
    "Auto",
    "ForceOff",
    "ForceOn",
};
XToString(HighResolutionTextures);
StringToX(HighResolutionTextures);
LuaXType(HighResolutionTextures);

static const char* AttractSoundFrequencyNames[] = {
    "Never",       "EveryTime",   "Every2Times",
    "Every3Times", "Every4Times", "Every5Times",
};
XToString(AttractSoundFrequency);
StringToX(AttractSoundFrequency);
LuaXType(AttractSoundFrequency);

static const char* CourseSortOrdersNames[] = {
    "Preferred", "Songs", "Meter", "MeterSum", "MeterRank",
};
XToString(CourseSortOrders);
StringToX(CourseSortOrders);
LuaXType(CourseSortOrders);

static const char* BackgroundFitModeNames[] = {
    "CoverDistort",         "CoverPreserve",        "FitInside",
    "FitInsideAvoidLetter", "FitInsideAvoidPillar",
};
XToString(BackgroundFitMode);
StringToX(BackgroundFitMode);
LuaXType(BackgroundFitMode);

static const char* ProfileSortOrderNames[] = {
    "Priority",
    "Recent",
    "Alphabetical",
    "CreationTime",
};
XToString(ProfileSortOrder);
StringToX(ProfileSortOrder);
LuaXType(ProfileSortOrder);

static const char* SyncOffsetNames[] = {"NULL", "ITG"};
XToString(SyncOffset);
StringToX(SyncOffset);
LuaXType(SyncOffset);

bool g_bAutoRestart = false;
#ifdef DEBUG
#define TRUE_IF_DEBUG true
#else
#define TRUE_IF_DEBUG false
#endif

void ValidateDisplayAspectRatio(float& val) {
  if (val < 0) {
    val = 16 / 9.f;
  }
}

void ValidateSongsPerPlay(int& val) { rage_clamp(val, 0, MAX_SONGS_PER_PLAY); }

PrefsManager::PrefsManager()
    : current_game_("CurrentGame", ""),

      announcer_("Announcer", ""),
      theme_("Theme", SpecialFiles::BASE_THEME_NAME),
      default_modifiers_("DefaultModifiers", ""),

      windowed_("Windowed", true),
      display_id_("DisplayId", ""),
      display_width_("DisplayWidth", 854),
      display_height_("DisplayHeight", 480),
      display_aspect_ratio_(
          "DisplayAspectRatio", 16 / 9.f, ValidateDisplayAspectRatio),
      display_color_depth_("DisplayColorDepth", 32),
      texture_color_depth_("TextureColorDepth", 32),
      movie_color_depth_("MovieColorDepth", 32),
      stretch_backgrounds_("StretchBackgrounds", false),
      bg_fit_mode_("BackgroundFitMode", BFM_CoverPreserve),
      high_resolution_textures_(
          "HighResolutionTextures", HighResolutionTextures_Auto),
      max_texture_resolution_("MaxTextureResolution", 2048),
      refresh_rate_("RefreshRate", REFRESH_DEFAULT),
      allow_multitexture_("AllowMultitexture", true),
      fullscreen_is_borderless_window_("FullscreenIsBorderlessWindow", true),
      show_stats_("ShowStats", TRUE_IF_DEBUG),
      show_banners_("ShowBanners", true),
      show_mouse_cursor_("ShowMouseCursor", false),

      hidden_songs_("HiddenSongs", false),
      vsync_("Vsync", true),
      fast_note_rendering_("FastNoteRendering", false),
      interlaced_("Interlaced", false),
      pal_("PAL", false),
      delayed_texture_delete_("DelayedTextureDelete", false),
      delayed_model_delete_("DelayedModelDelete", false),
      image_cache_("ImageCache", IMGCACHE_LOW_RES_PRELOAD),
      fast_load_("FastLoad", true),
      never_cache_list_("NeverCacheList", ""),

      only_dedicated_menu_buttons_("OnlyDedicatedMenuButtons", false),
      menu_timer_("MenuTimer", false),

      life_difficulty_scale_("LifeDifficultyScale", 1.0f),

      rate_mods_affect_tweens_("RateModsAffectFGChanges", false),

      regen_combo_after_miss_("RegenComboAfterMiss", 5),
      max_regen_combo_after_miss_(
          "MaxRegenComboAfterMiss",
          5),  // this was 10 by default in SM3.95 -dguzek
      merciful_drain_(
          "MercifulDrain", false),  // negative life deltas are scaled by the
                                    // players life percentage
      harsh_hot_life_penalty_("HarshHotLifePenalty", true),
      minimum1_full_song_in_courses_(
          "Minimum1FullSongInCourses",
          false),  // FEoS for 1st song, FailImmediate thereafter
      fail_off_in_beginner_("FailOffInBeginner", false),
      fail_off_for_first_stage_easy_("FailOffForFirstStageEasy", false),
      merciful_beginner_("MercifulBeginner", false),
      merciful_super_meter_("MercifulSuperMeter", true),
      delayed_back_("DelayedBack", true),
      allow_hold_for_options_("AllowHoldForOptions", true),
      show_instructions_("ShowInstructions", true),
      show_caution_("ShowCaution", true),
      show_native_language_("ShowNativeLanguage", true),
      arcade_options_navigation_("ArcadeOptionsNavigation", 0),
      three_key_navigation_("ThreeKeyNavigation", false),
      music_wheel_uses_sections_(
          "MusicWheelUsesSections", MusicWheelUsesSections_ALWAYS),
      music_wheel_switch_speed_("MusicWheelSwitchSpeed", 15),
      random_prioritize_difficulty_("RandomPrioritizeDifficulty", false),
      allow_w1_("AllowW1", ALLOW_W1_EVERYWHERE),
      event_mode_("EventMode", true),
      coins_per_credit_("CoinsPerCredit", 1),
      max_num_credits_("MaxNumCredits", 20),
      songs_per_play_("SongsPerPlay", 3, ValidateSongsPerPlay),
      delayed_credits_reconcile_("DelayedCreditsReconcile", false),
      combo_continues_between_songs_("ComboContinuesBetweenSongs", false),
      allow_multiple_toasties_("AllowMultipleToasties", true),
      min_tns_to_hide_notes_("MinTNSToHideNotes", TNS_W3),
      show_song_options_("ShowSongOptions", Maybe_NO),
      percentage_scoring_("PercentageScoring", true),
      // Wow, these preference names are *seriously* long -Colby
      min_percentage_for_machine_song_high_score_(
          "MinPercentageForMachineSongHighScore",
          0.0001f),  // This is for home, who cares how bad you do?
      min_percentage_for_machine_course_high_score_(
          "MinPercentageForMachineCourseHighScore",
          0.0001f),  // don't save course scores with 0 percentage
      disqualification_("Disqualification", false),
      autogen_steps_("AutogenSteps", false),
      autogen_group_courses_("AutogenGroupCourses", true),
      only_preferred_difficulties_("OnlyPreferredDifficulties", false),
      break_combo_to_get_item_("BreakComboToGetItem", false),
      lock_course_difficulties_("LockCourseDifficulties", false),
      show_dancing_characters_("ShowDancingCharacters", SDC_Random),
      use_unlock_system_("UseUnlockSystem", false),
      global_offset_seconds_("GlobalOffsetSeconds", -0.008f),
      default_sync_offset_("DefaultSyncOffset", SyncOffset_ITG),
      progressive_lifebar_("ProgressiveLifebar", 0),
      progressive_stage_lifebar_("ProgressiveStageLifebar", 0),
      progressive_nonstop_lifebar_("ProgressiveNonstopLifebar", 0),
      show_beginner_helper_("ShowBeginnerHelper", false),
      disable_screen_saver_("DisableScreenSaver", true),
      language_(
          "Language", ""),  // ThemeManager will deal with this invalid language
      memory_card_profile_subdir_("MemoryCardProfileSubdir", PRODUCT_ID),
      product_id_("ProductID", 1),
      center_image_translate_x_("CenterImageTranslateX", 0),
      center_image_translate_y_("CenterImageTranslateY", 0),
      center_image_add_width_("CenterImageAddWidth", 0),
      center_image_add_height_("CenterImageAddHeight", 0),
      attract_sound_frequency_("AttractSoundFrequency", ASF_EVERY_TIME),
      allow_extra_stage_("AllowExtraStage", false),
      max_high_scores_per_list_for_machine_(
          "MaxHighScoresPerListForMachine", 10),
      max_high_scores_per_list_for_player_("MaxHighScoresPerListForPlayer", 3),
      allow_multiple_high_score_with_same_name_(
          "AllowMultipleHighScoreWithSameName", true),
      disable_upload_dir_("DisableUploadDir", true),
      cel_shade_models_(
          "CelShadeModels", false),  // Work-In-Progress.. disable by default.
      preferred_sort_uses_groups_("PreferredSortUsesGroups", true),
      debounce_coin_input_time_("DebounceCoinInputTime", 0),
      reset_coins_at_startup_("ResetCoinsAtStartup", false),

      pad_stick_seconds_("PadStickSeconds", 0),
      edit_record_mode_lead_in_("EditRecordModeLeadIn", 1.0f),
      edit_clear_prompt_threshold_("EditClearPromptThreshold", 50),
      force_mip_maps_("ForceMipMaps", false),
      trilinear_filtering_("TrilinearFiltering", false),
      anisotropic_filtering_("AnisotropicFiltering", false),

      sign_profile_data_("SignProfileData", false),
      profile_sort_order_("ProfileSortOrder", ProfileSortOrder_Priority),
      profile_sort_order_ascending_("ProfileSortOrderAscending", true),
      course_sort_order_("CourseSortOrder", COURSE_SORT_SONGS),
      sub_sort_by_num_steps_("SubSortByNumSteps", false),
      get_ranking_name_("GetRankingName", RANKING_ON),
      additional_song_folders_read_only_(
          "AdditionalSongFoldersReadOnly", "", nullptr,
          PreferenceType::Immutable),
      additional_song_folders_writable_(
          "AdditionalSongFoldersWritable", "", nullptr,
          PreferenceType::Immutable),
      additional_course_folders_read_only_(
          "AdditionalCourseFoldersReadOnly", "", nullptr,
          PreferenceType::Immutable),
      additional_course_folders_writable_(
          "AdditionalCourseFoldersWritable", "", nullptr,
          PreferenceType::Immutable),
      additional_folders_read_only_(
          "AdditionalFoldersReadOnly", "", nullptr, PreferenceType::Immutable),
      additional_folders_writable_(
          "AdditionalFoldersWritable", "", nullptr, PreferenceType::Immutable),
      default_theme_("DefaultTheme", "Simply Love"),
      last_seen_video_driver_("LastSeenVideoDriver", ""),
      video_renderers_(
          "VideoRenderers", ""),  // StepMania.cpp sets these on first run:
      smooth_lines_("SmoothLines", true),
      sound_write_ahead_("SoundWriteAhead", 0),
      sound_device_("SoundDevice", ""),
      sound_preferred_sample_rate_("SoundPreferredSampleRate", 0),
      lights_steps_difficulty_("LightsStepsDifficulty", "hard,medium"),
      lights_simplify_bass_("LightsSimplifyBass", false),
      lights_bass_parallel_("LightsBassParallel", false),
      allow_unaccelerated_renderer_("AllowUnacceleratedRenderer", false),
      threaded_input_("ThreadedInput", true),
      threaded_movie_decode_("ThreadedMovieDecode", true),
      test_initial_screen_("TestInitialScreen", ""),
      debug_lights_("DebugLights", false),
      monkey_input_("MonkeyInput", false),
      machine_name_("MachineName", ""),
      courses_to_show_ranking_("CoursesToShowRanking", ""),
      mute_actions_("MuteActions", false),
      allow_song_deletion_("AllowSongDeletion", false),

      quirks_mode_("QuirksMode", false),

      custom_songs_enable_("CustomSongsEnable", false),
      custom_songs_max_count_(
          "CustomSongsMaxCount", 1000),  // No limit. -- 2 Unlimited
      custom_songs_load_timeout_("CustomSongsLoadTimeout", 5.f),
      custom_songs_max_seconds_("CustomSongsMaxSeconds", 120.f),
      custom_songs_max_megabytes_("CustomSongsMaxMegabytes", 5.f),

      /* Debug: */
      debug_menu_enabled_(
          "DebugMenuEnabled", true, nullptr, PreferenceType::Immutable),
      log_to_disk_("LogToDisk", true),
#if defined(DEBUG)
      force_log_flush_("ForceLogFlush", true),
      show_log_output_("ShowLogOutput", true),
#else
      force_log_flush_("ForceLogFlush", false),
      show_log_output_("ShowLogOutput", false),
#endif
      log_skips_("LogSkips", false),
      log_checkpoints_("LogCheckpoints", false),
      show_loading_window_("ShowLoadingWindow", true),
      pseudo_localize_("PseudoLocalize", false),
      show_theme_errors_("ShowThemeErrors", false),
      additional_song_folders_(
          "AdditionalSongFolders", "", nullptr, PreferenceType::Deprecated),
      additional_course_folders_(
          "AdditionalCourseFolders", "", nullptr, PreferenceType::Deprecated),
      additional_folders_(
          "AdditionalFolders", "", nullptr, PreferenceType::Deprecated) {
  Init();
  ReadPrefsFromDisk();

  // Register with Lua.
  {
    Lua* L = LUA->Get();
    lua_pushstring(L, "PREFSMAN");
    this->PushSelf(L);
    lua_settable(L, LUA_GLOBALSINDEX);
    LUA->Release(L);
  }
}
#undef TRUE_IF_DEBUG

void PrefsManager::Init() {
  IPreference::LoadAllDefaults();

  map_game_name_to_game_prefs_.clear();
}

PrefsManager::~PrefsManager() {
  // Unregister with Lua.
  LUA->UnsetGlobal("PREFSMAN");
}

void PrefsManager::SetCurrentGame(const std::string& sGame) {
  if (current_game_.Get() == sGame) {
    return;  // redundant
  }

  if (!current_game_.Get().empty()) {
    StoreGamePrefs();
  }

  current_game_.Set(sGame);

  RestoreGamePrefs();
}

void PrefsManager::StoreGamePrefs() {
  ASSERT(!current_game_.Get().empty());

  // save off old values
  GamePrefs& gp = map_game_name_to_game_prefs_[current_game_.Get()];
  gp.announcer_ = announcer_;
  gp.theme_ = theme_;
  gp.default_modifiers_ = default_modifiers_;
}

void PrefsManager::RestoreGamePrefs() {
  ASSERT(!current_game_.Get().empty());

  // load prefs
  GamePrefs gp;
  std::map<std::string, GamePrefs>::const_iterator iter =
      map_game_name_to_game_prefs_.find(current_game_);
  if (iter != map_game_name_to_game_prefs_.end()) {
    gp = iter->second;
  }

  announcer_.Set(gp.announcer_);
  theme_.Set(gp.theme_);
  default_modifiers_.Set(gp.default_modifiers_);

  // give Static.ini a chance to clobber the saved game prefs
  ReadPrefsFromFile(
      SpecialFiles::STATIC_INI_PATH, GetPreferencesSection(), true);
}

PrefsManager::GamePrefs::GamePrefs()
    : announcer_(""),
      theme_(SpecialFiles::BASE_THEME_NAME),
      default_modifiers_("") {}

void PrefsManager::ReadPrefsFromDisk() {
  ReadDefaultsFromFile(
      SpecialFiles::DEFAULTS_INI_PATH, GetPreferencesSection());
  IPreference::LoadAllDefaults();

  ReadPrefsFromFile(SpecialFiles::PREFERENCES_INI_PATH, "Options", false);
  ReadGamePrefsFromIni(SpecialFiles::PREFERENCES_INI_PATH);
  ReadPrefsFromFile(
      SpecialFiles::STATIC_INI_PATH, GetPreferencesSection(), true);

  TranslateDeprecatedFlags();

  if (!current_game_.Get().empty()) {
    RestoreGamePrefs();
  }
}

void PrefsManager::ResetToFactoryDefaults() {
  // clobber the users prefs by initing then applying defaults
  Init();
  IPreference::LoadAllDefaults();
  ReadPrefsFromFile(
      SpecialFiles::STATIC_INI_PATH, GetPreferencesSection(), true);

  TranslateDeprecatedFlags();

  SavePrefsToDisk();
}

void PrefsManager::ReadPrefsFromFile(
    const std::string& sIni, const std::string& sSection, bool bIsStatic) {
  IniFile ini;
  if (!ini.ReadFile(sIni)) {
    return;
  }

  ReadPrefsFromIni(ini, sSection, bIsStatic);
}

static const std::string GAME_SECTION_PREFIX = "Game-";

void PrefsManager::ReadPrefsFromIni(
    const IniFile& ini, const std::string& sSection, bool bIsStatic) {
  // Apply our fallback recursively (if any) before applying ourself.
  static int s_iDepth = 0;
  s_iDepth++;
  ASSERT(s_iDepth < 100);
  std::string sFallback;
  if (ini.GetValue(sSection, "Fallback", sFallback)) {
    ReadPrefsFromIni(ini, sFallback, bIsStatic);
  }
  s_iDepth--;

  /*
  IPreference *pPref = PREFSMAN->GetPreferenceByName( *sName );
  if( pPref == nullptr )
  {
          LOG->Warn( "Unknown preference in [%s]: %s", sClassName.c_str(),
  sName->c_str() ); continue;
  }
  pPref->FromString( sVal );
  */

  const XNode* pChild = ini.GetChild(sSection);
  if (pChild) {
    IPreference::ReadAllPrefsFromNode(pChild, bIsStatic);
  }
}

void PrefsManager::ReadGamePrefsFromIni(const std::string& sIni) {
  IniFile ini;
  if (!ini.ReadFile(sIni)) {
    return;
  }

  FOREACH_CONST_Child(&ini, section) {
    std::string section_name = section->GetName();
    if (!BeginsWith(section_name, GAME_SECTION_PREFIX)) {
      continue;
    }

    std::string sGame = Right(
        section_name, section_name.length() - GAME_SECTION_PREFIX.length());
    GamePrefs& gp = map_game_name_to_game_prefs_[sGame];

    // todo: read more prefs here? -aj
    ini.GetValue(section_name, "Announcer", gp.announcer_);
    ini.GetValue(section_name, "Theme", gp.theme_);
    ini.GetValue(section_name, "DefaultModifiers", gp.default_modifiers_);
  }
}

void PrefsManager::ReadDefaultsFromFile(
    const std::string& sIni, const std::string& sSection) {
  IniFile ini;
  if (!ini.ReadFile(sIni)) {
    return;
  }

  ReadDefaultsFromIni(ini, sSection);
}

void PrefsManager::ReadDefaultsFromIni(
    const IniFile& ini, const std::string& sSection) {
  // Apply our fallback recursively (if any) before applying ourself.
  // TODO: detect circular?
  std::string sFallback;
  if (ini.GetValue(sSection, "Fallback", sFallback)) {
    ReadDefaultsFromIni(ini, sFallback);
  }

  IPreference::ReadAllDefaultsFromNode(ini.GetChild(sSection));
}

void PrefsManager::TranslateDeprecatedFlags() {
  if (!additional_folders_.Get().empty()) {
    additional_folders_writable_.Set(additional_folders_.Get());
  }
  if (!additional_song_folders_.Get().empty()) {
    additional_song_folders_writable_.Set(additional_song_folders_.Get());
  }
  if (!additional_course_folders_.Get().empty()) {
    additional_course_folders_writable_.Set(additional_course_folders_.Get());
  }
}

void PrefsManager::SavePrefsToDisk() {
  IniFile ini;
  SavePrefsToIni(ini);
  ini.WriteFile(SpecialFiles::PREFERENCES_INI_PATH);
}

void PrefsManager::SavePrefsToIni(IniFile& ini) {
  if (!current_game_.Get().empty()) {
    StoreGamePrefs();
  }

  XNode* pNode = ini.GetChild("Options");
  if (pNode == nullptr) {
    pNode = ini.AppendChild("Options");
  }
  IPreference::SavePrefsToNode(pNode);

  for (const auto& iter : map_game_name_to_game_prefs_) {
    std::string sSection = "Game-" + std::string(iter.first);

    // todo: write more values here? -aj
    ini.SetValue(sSection, "Announcer", iter.second.announcer_);
    ini.SetValue(sSection, "Theme", iter.second.theme_);
    ini.SetValue(sSection, "DefaultModifiers", iter.second.default_modifiers_);
  }
}

std::string PrefsManager::GetPreferencesSection() const {
  std::string sSection = "Options";

  // OK if this fails
  GetFileContents(SpecialFiles::TYPE_TXT_FILE, sSection, true);

  // OK if this fails
  GetCommandlineArgument("Type", &sSection);

  return sSection;
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the PrefsManager. */
class LunaPrefsManager : public Luna<PrefsManager> {
 public:
  static int GetPreference(T* p, lua_State* L) {
    std::string sName = SArg(1);
    IPreference* pPref = IPreference::GetPreferenceByName(sName);
    if (pPref == nullptr) {
      LuaHelpers::ReportScriptErrorFmt(
          "GetPreference: unknown preference \"%s\"", sName.c_str());
      lua_pushnil(L);
      return 1;
    }

    pPref->PushValue(L);
    return 1;
  }
  static int SetPreference(T* p, lua_State* L) {
    std::string sName = SArg(1);

    IPreference* pPref = IPreference::GetPreferenceByName(sName);
    if (pPref == nullptr) {
      LuaHelpers::ReportScriptErrorFmt(
          "SetPreference: unknown preference \"%s\"", sName.c_str());
      COMMON_RETURN_SELF;
    } else if (pPref->IsImmutable()) {
      LuaHelpers::ReportScriptErrorFmt(
          "SetPreference: preference \"%s\" is immutable", sName.c_str());
      COMMON_RETURN_SELF;
    }

    lua_pushvalue(L, 2);
    pPref->SetFromStack(L);
    COMMON_RETURN_SELF;
  }
  static int SetPreferenceToDefault(T* p, lua_State* L) {
    std::string sName = SArg(1);

    IPreference* pPref = IPreference::GetPreferenceByName(sName);
    if (pPref == nullptr) {
      LuaHelpers::ReportScriptErrorFmt(
          "SetPreferenceToDefault: unknown preference \"%s\"", sName.c_str());
      COMMON_RETURN_SELF;
    } else if (pPref->IsImmutable()) {
      LuaHelpers::ReportScriptErrorFmt(
          "SetPreference: preference \"%s\" is immutable", sName.c_str());
      COMMON_RETURN_SELF;
    }

    pPref->LoadDefault();
    LOG->Trace(
        "Restored preference \"%s\" to default \"%s\"", sName.c_str(),
        pPref->ToString().c_str());
    COMMON_RETURN_SELF;
  }
  static int PreferenceExists(T* p, lua_State* L) {
    std::string sName = SArg(1);

    IPreference* pPref = IPreference::GetPreferenceByName(sName);
    if (pPref == nullptr) {
      lua_pushboolean(L, false);
      return 1;
    }
    lua_pushboolean(L, true);
    return 1;
  }

  static int SavePreferences(T* p, lua_State* L) {
    p->SavePrefsToDisk();
    COMMON_RETURN_SELF;
  }

  LunaPrefsManager() {
    ADD_METHOD(GetPreference);
    ADD_METHOD(SetPreference);
    ADD_METHOD(SetPreferenceToDefault);
    ADD_METHOD(PreferenceExists);
    ADD_METHOD(SavePreferences);
  }
};

LUA_REGISTER_CLASS(PrefsManager)
// lua end

/*
 * (c) 2001-2004 Chris Danford, Chris Gomez
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
