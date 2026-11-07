#include "StepMania.h"

#include <cstdlib>
#include <utility>

#include "DateTime.h"
#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "GameInput.h"
#include "PlayerNumber.h"
#include "Preference.h"
#include "RageException.h"
#include "RageInputDevice.h"
#include "RageUtil.h"
#include "StdString.h"
#include "ThemeMetric.h"
#include "global.h"

// Rage global classes
#include "CodeDetector.h"
#include "CommandLineActions.h"
#include "CommonMetrics.h"
#include "Game.h"
#include "GameSoundManager.h"
#include "InputEventPlus.h"
#include "LocalizedString.h"
#include "ProductInfo.h"
#include "RageDisplay.h"
#include "RageInput.h"
#include "RageLog.h"
#include "RageSoundManager.h"
#include "RageSurface.h"
#include "RageSurface_Load.h"
#include "RageTextureManager.h"
#include "RageThreads.h"
#include "RageTimer.h"
#include "RageUtil/Regex.h"
#include "Screen.h"
#include "arch/ArchHooks/ArchHooks.h"
#include "arch/Dialog/Dialog.h"
#include "arch/LoadingWindow/LoadingWindow.h"

#if !defined(SUPPORT_OPENGL) && !defined(SUPPORT_D3D)
#define SUPPORT_OPENGL
#endif

// StepMania global classes
#include <cmath>
#include <ctime>
#include <string>
#include <vector>

#include "ActorUtil.h"
#include "AnnouncerManager.h"
#include "Bookkeeper.h"
#include "CharacterManager.h"
#include "CryptManager.h"
#include "FontManager.h"
#include "GameLoop.h"
#include "GameManager.h"
#include "GameState.h"
#include "ImageCache.h"
#include "InputFilter.h"
#include "InputMapper.h"
#include "InputQueue.h"
#include "LightsManager.h"
#include "LuaDebugManager.h"
#include "LuaManager.h"
#include "MemoryCardManager.h"
#include "MessageManager.h"
#include "ModelManager.h"
#include "NetworkManager.h"
#include "NoteSkinManager.h"
#include "PrefsManager.h"
#include "Profile.h"
#include "ProfileManager.h"
#include "RageFileManager.h"
#include "ScreenManager.h"
#include "SongCacheIndex.h"
#include "SongManager.h"
#include "SpecialFiles.h"
#include "StatsManager.h"
#include "ThemeManager.h"
#include "UnlockManager.h"
#include "ver.h"

void ShutdownGame();
bool HandleGlobalInputs(const InputEventPlus& input);
void HandleInputEvents(float delta_time);

static Preference<bool> g_bAllowMultipleInstances(
    "AllowMultipleInstances", false);

void StepMania::GetPreferredVideoModeParams(VideoModeParams& params_out) {
  // resolution handling code that probably needs fixing
  int width = PREFSMAN->display_width_;
  if (PREFSMAN->windowed_) {
    // float fRatio = PREFSMAN->m_iDisplayHeight;
    // iWidth = PREFSMAN->m_iDisplayHeight * fRatio;
    width =
        std::ceil(PREFSMAN->display_height_ * PREFSMAN->display_aspect_ratio_);
    // ceil causes the width to come out odd when it shouldn't.
    // 576 * 1.7778 = 1024.0128, which is rounded to 1025. -Kyz
    width -= width % 2;
  }

  params_out = VideoModeParams(
      PREFSMAN->windowed_ || PREFSMAN->fullscreen_is_borderless_window_,
      PREFSMAN->display_id_, width, PREFSMAN->display_height_,
      PREFSMAN->display_color_depth_, PREFSMAN->refresh_rate_, PREFSMAN->vsync_,
      PREFSMAN->interlaced_, PREFSMAN->smooth_lines_,
      PREFSMAN->trilinear_filtering_, PREFSMAN->anisotropic_filtering_,
      !PREFSMAN->windowed_ && PREFSMAN->fullscreen_is_borderless_window_,
      CommonMetrics::WINDOW_TITLE, THEME->GetPathG("Common", "window icon"),
      PREFSMAN->pal_, PREFSMAN->display_aspect_ratio_);
}

static LocalizedString COLOR("StepMania", "color");
static LocalizedString TEXTURE("StepMania", "texture");
static LocalizedString WINDOWED("StepMania", "Windowed");
static LocalizedString FULLSCREEN("StepMania", "Fullscreen");
static LocalizedString ANNOUNCER_("StepMania", "Announcer");
static LocalizedString VSYNC("StepMania", "Vsync");
static LocalizedString NO_VSYNC("StepMania", "NoVsync");
static LocalizedString SMOOTH_LINES("StepMania", "SmoothLines");
static LocalizedString NO_SMOOTH_LINES("StepMania", "NoSmoothLines");

static std::string GetActualGraphicOptionsString() {
  const VideoModeParams& params = DISPLAY->GetActualVideoModeParams();
  std::string format = "%s %s %dx%d %d " + COLOR.GetValue() + " %d " +
                       TEXTURE.GetValue() + " %dHz %s %s";
  std::string log = ssprintf(
      format.c_str(), DISPLAY->GetApiDescription().c_str(),
      (params.windowed_ ? WINDOWED : FULLSCREEN).GetValue().c_str(),
      (int)params.width_, (int)params.height_, (int)params.bpp_,
      (int)PREFSMAN->texture_color_depth_, (int)params.rate_,
      (params.vsync_ ? VSYNC : NO_VSYNC).GetValue().c_str(),
      (PREFSMAN->smooth_lines_ ? SMOOTH_LINES : NO_SMOOTH_LINES)
          .GetValue()
          .c_str());
  return log;
}

static void StoreActualGraphicOptions() {
  /* Store the settings that RageDisplay was actually able to use so that
   * we don't go through the process of auto-detecting a usable video mode
   * every time. */
  const VideoModeParams& params = DISPLAY->GetActualVideoModeParams();
  PREFSMAN->windowed_.Set(
      params.windowed_ && !params.window_is_fullscreen_borderless_);
  if (!params.windowed_ && !params.window_is_fullscreen_borderless_) {
    // In all other cases, want to preserve the value of this preference,
    // but if DISPLAY decides to go fullscreen exclusive, we'll persist that
    // decision
    PREFSMAN->fullscreen_is_borderless_window_.Set(false);
  }

  /* If we're windowed, we may have tweaked the width based on the aspect ratio.
   * Don't save this new value over the preferred value. */
  if (!PREFSMAN->windowed_) {
    PREFSMAN->display_width_.Set(params.width_);
    PREFSMAN->display_height_.Set(params.height_);
  }
  PREFSMAN->display_color_depth_.Set(params.bpp_);
  if (PREFSMAN->refresh_rate_ != REFRESH_DEFAULT) {
    PREFSMAN->refresh_rate_.Set(params.rate_);
  }
  PREFSMAN->vsync_.Set(params.vsync_);

  Dialog::SetWindowed(params.windowed_);
}

static RageDisplay* CreateDisplay();

bool StepMania::GetHighResolutionTextures() {
  switch (PREFSMAN->high_resolution_textures_) {
    default:
    case HighResolutionTextures_Auto: {
      int height = PREFSMAN->display_height_;
      return height > THEME->GetMetricI("Common", "ScreenHeight");
    }
    case HighResolutionTextures_ForceOn:
      return true;
    case HighResolutionTextures_ForceOff:
      return false;
  }
}

static void update_centering() {
  DISPLAY->ChangeCentering(
      PREFSMAN->center_image_translate_x_, PREFSMAN->center_image_translate_y_,
      PREFSMAN->center_image_add_width_, PREFSMAN->center_image_add_height_);
}

static void StartDisplay() {
  if (DISPLAY != nullptr) {
    return;  // already started
  }

  DISPLAY = CreateDisplay();

  update_centering();

  TEXTUREMAN = new RageTextureManager;
  TEXTUREMAN->SetPrefs(RageTextureManagerPrefs(
      PREFSMAN->texture_color_depth_, PREFSMAN->movie_color_depth_,
      PREFSMAN->delayed_texture_delete_, PREFSMAN->max_texture_resolution_,
      StepMania::GetHighResolutionTextures(), PREFSMAN->force_mip_maps_));

  MODELMAN = new ModelManager;
  MODELMAN->SetPrefs(ModelManagerPrefs(PREFSMAN->delayed_model_delete_));
}

void StepMania::ApplyGraphicOptions() {
  bool need_reload = false;

  VideoModeParams params;
  GetPreferredVideoModeParams(params);
  std::string error = DISPLAY->SetVideoMode(params, need_reload);
  if (error != "") {
    RageException::Throw("%s", error.c_str());
  }

  update_centering();

  need_reload |= TEXTUREMAN->SetPrefs(RageTextureManagerPrefs(
      PREFSMAN->texture_color_depth_, PREFSMAN->movie_color_depth_,
      PREFSMAN->delayed_texture_delete_, PREFSMAN->max_texture_resolution_,
      StepMania::GetHighResolutionTextures(), PREFSMAN->force_mip_maps_));

  need_reload |=
      MODELMAN->SetPrefs(ModelManagerPrefs(PREFSMAN->delayed_model_delete_));

  if (need_reload) {
    TEXTUREMAN->ReloadAll();
  }

  StoreActualGraphicOptions();
  if (SCREENMAN) {
    SCREENMAN->SystemMessage(GetActualGraphicOptionsString());
  }

  // Give the input handlers a chance to re-open devices as necessary.
  INPUTMAN->WindowReset();
}

static bool CheckVideoDefaultSettings();

void StepMania::ResetPreferences() {
  PREFSMAN->ResetToFactoryDefaults();
  SOUNDMAN->SetMixVolume();
  CheckVideoDefaultSettings();
  ApplyGraphicOptions();
}

/* Shutdown all global singletons. Note that this may be called partway through
 * initialization, due to an object failing to initialize, in which case some of
 * these may still be nullptr. */
void ShutdownGame() {
  /* First, tell SOUNDMAN that we're shutting down. This signals sound drivers
   * to stop sounds, which we want to do before any threads that may have
   * started sounds are closed; this prevents annoying DirectSound glitches and
   * delays. */
  if (SOUNDMAN) {
    SOUNDMAN->Shutdown();
  }

  /* Reset all lights to off.
   * This is done before ~LightsManager as some drivers use SCREENMAN
   * and similar when setting lights. */
  if (LIGHTSMAN) {
    LIGHTSMAN->TurnOffAllLights();
  }

  RageUtil::SafeDelete(NETWORK);
  RageUtil::SafeDelete(SCREENMAN);
  RageUtil::SafeDelete(STATSMAN);
  RageUtil::SafeDelete(MESSAGEMAN);
  /* Delete INPUTMAN before the other INPUTFILTER handlers, or an input
   * driver may try to send a message to INPUTFILTER after we delete it. */
  RageUtil::SafeDelete(INPUTMAN);
  RageUtil::SafeDelete(INPUTQUEUE);
  RageUtil::SafeDelete(INPUTMAPPER);
  RageUtil::SafeDelete(INPUTFILTER);
  RageUtil::SafeDelete(MODELMAN);
  RageUtil::SafeDelete(PROFILEMAN);  // PROFILEMAN needs the songs still loaded
  RageUtil::SafeDelete(CHARMAN);
  RageUtil::SafeDelete(UNLOCKMAN);
  RageUtil::SafeDelete(CRYPTMAN);
  RageUtil::SafeDelete(MEMCARDMAN);
  RageUtil::SafeDelete(SONGMAN);
  RageUtil::SafeDelete(IMAGECACHE);
  RageUtil::SafeDelete(SONGINDEX);
  RageUtil::SafeDelete(SOUND);  // uses GAMESTATE, PREFSMAN
  RageUtil::SafeDelete(PREFSMAN);
  RageUtil::SafeDelete(GAMESTATE);
  RageUtil::SafeDelete(GAMEMAN);
  RageUtil::SafeDelete(NOTESKIN);
  RageUtil::SafeDelete(THEME);
  RageUtil::SafeDelete(ANNOUNCER);
  RageUtil::SafeDelete(BOOKKEEPER);
  RageUtil::SafeDelete(LIGHTSMAN);
  RageUtil::SafeDelete(SOUNDMAN);
  RageUtil::SafeDelete(FONT);
  RageUtil::SafeDelete(TEXTUREMAN);
  RageUtil::SafeDelete(DISPLAY);
  Dialog::Shutdown();
  RageUtil::SafeDelete(LUADEBUG);
  RageUtil::SafeDelete(LOG);
  RageUtil::SafeDelete(FILEMAN);
  RageUtil::SafeDelete(LUA);
  RageUtil::SafeDelete(HOOKS);
}

static void HandleException(const std::string& error) {
  if (g_bAutoRestart) {
    HOOKS->RestartProgram();
  }

  // Shut down first, so we exit graphics mode before trying to open a dialog.
  ShutdownGame();

  // Throw up a pretty error dialog.
  Dialog::Error(error);
  Dialog::Shutdown();  // Shut it back down.
}

void StepMania::ResetGame() {
  GAMESTATE->Reset();

  if (!THEME->DoesThemeExist(THEME->GetCurThemeName())) {
    std::string game_name = GAMESTATE->GetCurrentGame()->name_;
    if (!THEME->DoesThemeExist(game_name)) {
      game_name = PREFSMAN->default_theme_;  // was previously "default" -aj
    }
    THEME->SwitchThemeAndLanguage(
        game_name, THEME->GetCurLanguage(), PREFSMAN->pseudo_localize_);
    TEXTUREMAN->DoDelayedDelete();
  }

  PREFSMAN->SavePrefsToDisk();
}

ThemeMetric<std::string> INITIAL_SCREEN("Common", "InitialScreen");
std::string StepMania::GetInitialScreen() {
  if (PREFSMAN->test_initial_screen_.Get() != "" &&
      SCREENMAN->IsScreenNameValid(PREFSMAN->test_initial_screen_)) {
    return PREFSMAN->test_initial_screen_;
  }
  std::string screen_name = INITIAL_SCREEN.GetValue();
  if (!SCREENMAN->IsScreenNameValid(screen_name)) {
    screen_name = "ScreenInitialScreenIsInvalid";
  }
  return screen_name;
}
ThemeMetric<std::string> SELECT_MUSIC_SCREEN("Common", "SelectMusicScreen");
std::string StepMania::GetSelectMusicScreen() {
  return SELECT_MUSIC_SCREEN.GetValue();
}

#if defined(_WIN32)
#include "RageDisplay_D3D.h"
#include "archutils/Win32/VideoDriverInfo.h"
#endif

#if defined(SUPPORT_OPENGL)
#include "RageDisplay_OGL.h"
#endif

#include "RageDisplay_Null.h"

struct VideoCardDefaults {
  std::string driver_regex_;
  std::string video_renderers_;
  int width_;
  int height_;
  int display_color_;
  int texture_color_;
  int movie_color_;
  int texture_size_;
  bool smooth_lines_;

  VideoCardDefaults() {}
  VideoCardDefaults(
      std::string driver_regex, std::string video_renderers, int width,
      int height, int display_color, int texture_color, int movie_color,
      int texture_size, bool smooth_lines) {
    driver_regex_ = driver_regex;
    video_renderers_ = video_renderers;
    width_ = width;
    height_ = height;
    display_color_ = display_color;
    texture_color_ = texture_color;
    movie_color_ = movie_color;
    texture_size_ = texture_size;
    smooth_lines_ = smooth_lines;
  }
} const g_VideoCardDefaults[] = {
    // These lines correspond to the struct defined above.
    VideoCardDefaults(
        "OpenGL",   // Video card name (generic Mac/Linux)
        "opengl",   // Available renderers
        1280, 720,  // Default resolution
        32,         // Display color
        32,         // Texture color
        32,         // Movie color
        2048,       // Texture size
        true        // Smooth lines
        ),
    VideoCardDefaults(
        "",            // Video card name (generic Windows)
        "opengl,d3d",  // Available renderers
        1280, 720,     // Default resolution
        32,            // Display color
        32,            // Texture color
        32,            // Movie color
        2048,          // Texture size
        true           // Smooth lines
        ),
};

static std::string GetVideoDriverName() {
#if defined(_WIN32)
  return GetPrimaryVideoDriverName();
#else
  return "OpenGL";
#endif
}

bool CheckVideoDefaultSettings() {
  // Video card changed since last run
  std::string video_driver = GetVideoDriverName();

  LOG->Trace(
      "Last seen video driver: %s",
      PREFSMAN->last_seen_video_driver_.Get().c_str());

  VideoCardDefaults defaults;

  unsigned i;
  for (i = 0; i < ARRAYLEN(g_VideoCardDefaults); i++) {
    defaults = g_VideoCardDefaults[i];

    std::string driver_regex = defaults.driver_regex_;
    Regex regex(driver_regex);
    if (regex.Compare(video_driver)) {
      LOG->Trace(
          "Card matches '%s'.",
          driver_regex.size() ? driver_regex.c_str() : "(unknown card)");
      break;
    }
  }
  if (i >= ARRAYLEN(g_VideoCardDefaults)) {
    FAIL_M("Failed to match video driver");
  }

  bool set_default_video_params = false;
  if (PREFSMAN->video_renderers_.Get() == "") {
    set_default_video_params = true;
    LOG->Trace("Applying defaults for %s.", video_driver.c_str());
  } else if (PREFSMAN->last_seen_video_driver_.Get() != video_driver) {
    set_default_video_params = true;
    LOG->Trace(
        "Video card has changed from %s to %s.  Applying new defaults.",
        PREFSMAN->last_seen_video_driver_.Get().c_str(), video_driver.c_str());
  }

  if (set_default_video_params) {
    PREFSMAN->video_renderers_.Set(defaults.video_renderers_);
    PREFSMAN->display_width_.Set(defaults.width_);
    PREFSMAN->display_height_.Set(defaults.height_);
    PREFSMAN->display_color_depth_.Set(defaults.display_color_);
    PREFSMAN->texture_color_depth_.Set(defaults.texture_color_);
    PREFSMAN->movie_color_depth_.Set(defaults.movie_color_);
    PREFSMAN->max_texture_resolution_.Set(defaults.texture_size_);
    PREFSMAN->smooth_lines_.Set(defaults.smooth_lines_);
    // this only worked when we started in fullscreen by default. -aj
    // PREFSMAN->m_fDisplayAspectRatio.Set( HOOKS->GetDisplayAspectRatio() );
    // now that we start in windowed mode, use the new default aspect ratio.
    PREFSMAN->display_aspect_ratio_.Set(PREFSMAN->display_aspect_ratio_);

    // Update last seen video card
    PREFSMAN->last_seen_video_driver_.Set(GetVideoDriverName());
  } else if (CompareNoCase(
                 PREFSMAN->video_renderers_.Get(), defaults.video_renderers_)) {
    LOG->Warn(
        "Video renderer list has been changed from '%s' to '%s'",
        defaults.video_renderers_.c_str(),
        PREFSMAN->video_renderers_.Get().c_str());
  }

  LOG->Info("Video renderers: '%s'", PREFSMAN->video_renderers_.Get().c_str());
  return set_default_video_params;
}

static LocalizedString ERROR_INITIALIZING_CARD(
    "StepMania", "There was an error while initializing your video card.");
static LocalizedString ERROR_DONT_FILE_BUG(
    "StepMania",
    "Please do not file this error as a bug!  Use the web page below to "
    "troubleshoot this problem.");
static LocalizedString ERROR_VIDEO_DRIVER("StepMania", "Video Driver: %s");
static LocalizedString ERROR_NO_VIDEO_RENDERERS(
    "StepMania", "No video renderers attempted.");
static LocalizedString ERROR_INITIALIZING("StepMania", "Initializing %s...");
static LocalizedString ERROR_UNKNOWN_VIDEO_RENDERER(
    "StepMania", "Unknown video renderer value: %s");

RageDisplay* CreateDisplay() {
  /* We never want to bother users with having to decide which API to use.
   *
   * Some cards simply are too troublesome with OpenGL to ever use it, eg.
   * Voodoos. If D3D8 isn't installed on those, complain and refuse to run (by
   * default). For others, always use OpenGL.  Allow forcing to D3D as an
   * advanced option.
   *
   * If we're missing acceleration when we load D3D8 due to a card being in the
   * D3D list, it means we need drivers and that they do exist.
   *
   * If we try to load OpenGL and we're missing acceleration, it may mean:
   *  1. We're missing drivers, and they just need upgrading.
   *  2. The card doesn't have drivers, and it should be using D3D8.
   *     In other words, it needs an entry in this table.
   *  3. The card doesn't have drivers for either.  (Sorry, no S3 868s.)
   *     Can't play.
   * In this case, fail to load; don't silently fall back on D3D.  We don't want
   * people unknowingly using D3D8 with old drivers (and reporting obscure bugs
   * due to driver problems).  We'll probably get bug reports for all three
   * types. #2 is the only case that's actually a bug.
   *
   * Actually, right now we're falling back. I'm not sure which behavior is
   * better.
   */

  // bool bAppliedDefaults = CheckVideoDefaultSettings();
  CheckVideoDefaultSettings();

  VideoModeParams params;
  StepMania::GetPreferredVideoModeParams(params);

  std::string error =
      ERROR_INITIALIZING_CARD.GetValue() + "\n\n" +
      ERROR_DONT_FILE_BUG.GetValue() + "\n\n" VIDEO_TROUBLESHOOTING_URL "\n\n" +
      ssprintf(
          ERROR_VIDEO_DRIVER.GetValue().c_str(), GetVideoDriverName().c_str()) +
      "\n\n";

  std::vector<std::string> renderers;
  split(PREFSMAN->video_renderers_, ",", renderers, true);

  if (renderers.empty()) {
    RageException::Throw("%s", ERROR_NO_VIDEO_RENDERERS.GetValue().c_str());
  }

  RageDisplay* ret = nullptr;
  for (unsigned i = 0; i < renderers.size(); i++) {
    std::string renderer = renderers[i];

    if (CompareNoCase(renderer, "opengl") == 0) {
#if defined(SUPPORT_OPENGL)
      ret = new RageDisplay_Legacy;
#endif
    } else if (CompareNoCase(renderer, "d3d") == 0) {
// TODO: ANGLE/RageDisplay_Modern
#if defined(SUPPORT_D3D)
      ret = new RageDisplay_D3D;
#endif
    } else if (CompareNoCase(renderer, "null") == 0) {
      return new RageDisplay_Null;
    } else {
      RageException::Throw(
          ERROR_UNKNOWN_VIDEO_RENDERER.GetValue().c_str(), renderer.c_str());
    }

    if (ret == nullptr) {
      continue;
    }

    std::string s_error =
        ret->Init(params, PREFSMAN->allow_unaccelerated_renderer_);
    if (!s_error.empty()) {
      error +=
          ssprintf(ERROR_INITIALIZING.GetValue().c_str(), renderer.c_str()) +
          "\n" + s_error;
      RageUtil::SafeDelete(ret);
      error += "\n\n\n";
      continue;
    }

    break;  // the display is ready to go
  }

  if (ret == nullptr) {
    RageException::Throw("%s", error.c_str());
  }

  return ret;
}

static void SwitchToLastPlayedGame() {
  const Game* game = GAMEMAN->StringToGame(PREFSMAN->GetCurrentGame());

  // If the active game type isn't actually available, revert to the default.
  if (game == nullptr) {
    game = GAMEMAN->GetDefaultGame();
  }

  if (!GAMEMAN->IsGameEnabled(game) && game != GAMEMAN->GetDefaultGame()) {
    game = GAMEMAN->GetDefaultGame();
    LOG->Warn(
        "Default NoteSkin for \"%s\" missing, reverting to \"%s\"", game->name_,
        GAMEMAN->GetDefaultGame()->name_);
  }

  ASSERT(GAMEMAN->IsGameEnabled(game));

  StepMania::InitializeCurrentGame(game);
}

// This function is meant to only be called during start up.
void StepMania::InitializeCurrentGame(const Game* g) {
  ASSERT(g != nullptr);
  ASSERT(GAMESTATE != nullptr);
  ASSERT(ANNOUNCER != nullptr);
  ASSERT(THEME != nullptr);

  GAMESTATE->SetCurGame(g);

  std::string announcer = PREFSMAN->announcer_;
  std::string theme = PREFSMAN->theme_;
  std::string gametype = GAMESTATE->GetCurrentGame()->name_;
  std::string language = PREFSMAN->language_;

  if (announcer.empty()) {
    announcer = GAMESTATE->GetCurrentGame()->name_;
  }
  std::string arg_cur_game;
  if (GetCommandlineArgument("game", &arg_cur_game) &&
      arg_cur_game != gametype) {
    const Game* new_game = GAMEMAN->StringToGame(arg_cur_game);
    if (new_game == nullptr) {
      LOG->Warn("%s is not a known game type, ignoring.", arg_cur_game.c_str());
    } else {
      PREFSMAN->SetCurrentGame(gametype);
      GAMESTATE->SetCurGame(new_game);
    }
  }

  // It doesn't matter if sTheme is blank or invalid, THEME->STAL will set
  // a selectable theme for us. -Kyz

  // process gametype, theme and language command line arguments;
  // these change the preferences in order for transparent loading -aj
  std::string arg_theme;
  if (GetCommandlineArgument("theme", &arg_theme) && arg_theme != theme) {
    theme = arg_theme;
    // set theme in preferences too for correct behavior  -aj
    PREFSMAN->theme_.Set(theme);
  }

  std::string arg_language;
  if (GetCommandlineArgument("language", &arg_language)) {
    language = arg_language;
    // set language in preferences too for correct behavior -aj
    PREFSMAN->language_.Set(language);
  }

  // it's OK to call these functions with names that don't exist.
  ANNOUNCER->SwitchAnnouncer(announcer);
  THEME->SwitchThemeAndLanguage(theme, language, PREFSMAN->pseudo_localize_);

  // Set the input scheme for the new game, and load keymaps.
  if (INPUTMAPPER) {
    INPUTMAPPER->SetInputScheme(&g->input_scheme_);
    INPUTMAPPER->ReadMappingsFromDisk();
  }
}

static void MountTreeOfZips(const std::string& dir) {
  std::vector<std::string> dirs;
  dirs.push_back(dir);

  while (dirs.size()) {
    std::string path = dirs.back();
    dirs.pop_back();

    if (!IsADirectory(path)) {
      continue;
    }

    std::vector<std::string> zips;
    GetDirListing(path + "/*.zip", zips, false, true);
    GetDirListing(path + "/*.smzip", zips, false, true);

    for (const auto& zip : zips) {
      if (!IsAFile(zip)) {
        continue;
      }

      LOG->Trace("VFS: found %s", zip.c_str());
      FILEMAN->Mount("zip", zip, "/");
    }

    GetDirListing(path + "/*", dirs, true, true);
  }
}

static void MountFolders(
    const std::string& type, const std::string& real_path_list,
    const std::string& mount_point) {
  if (!real_path_list.empty()) {
    std::vector<std::string> dirs;
    split(real_path_list, ",", dirs, true);
    for (const auto& dir : dirs) {
      FILEMAN->Mount(type, dir, mount_point);
    }
  }
}

static void WriteLogHeader() {
  LOG->Info("%s%s", PRODUCT_FAMILY, product_version);

  LOG->Info(
      "Compiled %s @ %s (build %s)", version_date, version_time,
      ::sm_version_git_hash);

  time_t cur_time;
  time(&cur_time);
  struct tm now;
  localtime_r(&cur_time, &now);

  LOG->Info(
      "Log starting %.4d-%.2d-%.2d %.2d:%.2d:%.2d", 1900 + now.tm_year,
      now.tm_mon + 1, now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec);
  LOG->Trace(" ");

  if (g_argc > 1) {
    std::string args;
    for (int i = 1; i < g_argc; ++i) {
      if (i > 1) {
        args += " ";
      }

      // surround all params with some marker, as they might have whitespace.
      // using [[ and ]], as they are not likely to be in the params.
      args += ssprintf("[[%s]]", g_argv[i]);
    }
    LOG->Info("Command line args (count=%d): %s", (g_argc - 1), args.c_str());
  }
}

static void ApplyLogPreferences() {
  LOG->SetShowLogOutput(PREFSMAN->show_log_output_);
  LOG->SetLogToDisk(PREFSMAN->log_to_disk_);
  LOG->SetInfoToDisk(PREFSMAN->log_to_disk_);
  LOG->SetUserLogToDisk(PREFSMAN->log_to_disk_);
  LOG->SetFlushing(PREFSMAN->force_log_flush_);
  Checkpoints::LogCheckpoints(PREFSMAN->log_checkpoints_);
}

static LocalizedString COULDNT_OPEN_LOADING_WINDOW(
    "LoadingWindow", "Couldn't open any loading windows.");

int sm_main(int argc, char* argv[]) {
  RageThreadRegister thread("Main thread");
  RageException::SetCleanupHandler(HandleException);

  SetCommandlineArguments(argc, argv);

  // Set up arch hooks first.  This may set up crash handling.
  HOOKS = ArchHooks::Create();
  HOOKS->Init();

  LUA = new LuaManager;
  HOOKS->RegisterWithLua();

  // Initialize the file extension type lists so everything can ask ActorUtil
  // what the type of a file is.
  ActorUtil::InitFileTypeLists();

  // Almost everything uses this to read and write files.  Load this early.
  FILEMAN = new RageFileManager(argv[0]);
  FILEMAN->ProtectPath(SpecialFiles::DEFAULTS_INI_PATH);
  FILEMAN->ProtectPath(SpecialFiles::STATIC_INI_PATH);
  FILEMAN->ProtectPath(SpecialFiles::PREFERENCES_INI_PATH);
  FILEMAN->MountInitialFilesystems();

  bool portable = DoesFileExist("/Portable.ini");
  if (!portable) {
    FILEMAN->MountUserFilesystems();
  }

  // Set this up next. Do this early, since it's needed for
  // RageException::Throw.
  LOG = new RageLog;

  // Whew--we should be able to crash safely now!

  // load preferences and mount any alternative trees.
  PREFSMAN = new PrefsManager;

  /* Allow HOOKS to check for multiple instances.  We need to do this after
   * PREFS is initialized, so ArchHooks can use a preference to turn this off.
   * We want to do this before ApplyLogPreferences, so if we exit because of
   * another instance, we don't try to clobber its log.  We also want to do this
   * before opening the loading window, so if we give focus away, we don't flash
   * the window. */
  if (!g_bAllowMultipleInstances.Get() &&
      HOOKS->CheckForMultipleInstances(argc, argv)) {
    ShutdownGame();
    return 0;
  }

  ApplyLogPreferences();

  WriteLogHeader();

  // Set up alternative filesystem trees.
  MountFolders("dirro", PREFSMAN->additional_folders_read_only_.Get(), "/");
  MountFolders("dir", PREFSMAN->additional_folders_writable_.Get(), "/");
  MountFolders(
      "dirro", PREFSMAN->additional_song_folders_read_only_.Get(), "/Songs");
  MountFolders(
      "dir", PREFSMAN->additional_song_folders_writable_.Get(), "/Songs");
  MountFolders(
      "dirro", PREFSMAN->additional_course_folders_read_only_.Get(),
      "/Courses");
  MountFolders(
      "dir", PREFSMAN->additional_course_folders_writable_.Get(), "/Courses");

  MountTreeOfZips(SpecialFiles::PACKAGES_DIR);

  /* One of the above filesystems might contain files that affect preferences
   * (e.g. Data/Static.ini). Re-read preferences. */
  PREFSMAN->ReadPrefsFromDisk();
  ApplyLogPreferences();

  // This needs PREFSMAN.
  Dialog::Init();

  // Set up the messaging system early to have well defined code.
  MESSAGEMAN = new MessageManager;

  // Create game objects
  GAMESTATE = new GameState;

  // This requires PREFSMAN, for PREFSMAN->m_bShowLoadingWindow.
  LoadingWindow* loading_window = LoadingWindow::Create();
  if (loading_window == nullptr) {
    RageException::Throw("%s", COULDNT_OPEN_LOADING_WINDOW.GetValue().c_str());
  }

  srand(time(nullptr));  // seed number generator

  /* Do this early, so we have debugging output if anything else fails. LOG and
   * Dialog must be set up first. It shouldn't take long, but it might take a
   * little time; do this after the LoadingWindow is shown, since we don't want
   * that to appear delayed. */
  HOOKS->DumpDebugInfo();

  std::string lua_debug_server_address;
  if (GetCommandlineArgument("lua-debugger", &lua_debug_server_address)) {
    LUADEBUG = new LuaDebugManager();
    LUADEBUG->Start(
        lua_debug_server_address,
        GetCommandlineArgument("lua-debugger-paused"));
  }

  GAMEMAN = new GameManager;
  THEME = new ThemeManager;
  ANNOUNCER = new AnnouncerManager;
  NOTESKIN = new NoteSkinManager;

  // Switch to the last used game type, and set up the theme and announcer.
  SwitchToLastPlayedGame();

  CommandLineActions::Handle(loading_window);

  if (GetCommandlineArgument("dopefish")) {
    GAMESTATE->dopefish_ = true;
  }

  {
    /* Now that THEME is loaded, load the icon and splash for the current
     * theme into the loading window. */
    std::string error;
    RageSurface* surface = RageSurfaceUtils::LoadFile(
        THEME->GetPathG("Common", "window icon"), error);
    if (surface != nullptr) {
      loading_window->SetIcon(surface);
    }
    delete surface;
    surface =
        RageSurfaceUtils::LoadFile(THEME->GetPathG("Common", "splash"), error);
    if (surface != nullptr) {
      loading_window->SetSplash(surface);
    }
    delete surface;
  }

  if (PREFSMAN->sound_write_ahead_) {
    LOG->Info(
        "Sound writeahead has been overridden to %i",
        PREFSMAN->sound_write_ahead_.Get());
  }

  SOUNDMAN = new RageSoundManager;
  SOUNDMAN->Init();
  SOUNDMAN->SetMixVolume();
  SOUND = new GameSoundManager;
  BOOKKEEPER = new Bookkeeper;
  LIGHTSMAN = new LightsManager;
  INPUTFILTER = new InputFilter;
  INPUTMAPPER = new InputMapper;

  StepMania::InitializeCurrentGame(GAMESTATE->GetCurrentGame());

  INPUTQUEUE = new InputQueue;
  SONGINDEX = new SongCacheIndex;
  IMAGECACHE = new ImageCache;

  // depends on SONGINDEX:
  SONGMAN = new SongManager;
  SONGMAN->InitAll(
      loading_window, /*onlyAdditions=*/false);  // this takes a long time
  CRYPTMAN = new CryptManager;  // need to do this before ProfileMan
  if (PREFSMAN->sign_profile_data_) {
    CRYPTMAN->GenerateGlobalKeys();
  }
  MEMCARDMAN = new MemoryCardManager;
  CHARMAN = new CharacterManager;
  PROFILEMAN = new ProfileManager;
  PROFILEMAN->Init();  // must load after SONGMAN
  UNLOCKMAN = new UnlockManager;
  SONGMAN->UpdatePopular();
  SONGMAN->UpdatePreferredSort();
  NETWORK = new NetworkManager;
  STATSMAN = new StatsManager;

  // Initialize which courses are ranking courses here.
  SONGMAN->UpdateRankingCourses();

  RageUtil::SafeDelete(loading_window);  // destroy this before init'ing Display

  /* If the user has tried to quit during the loading, do it before creating
   * the main window. This prevents going to full screen just to quit. */
  if (ArchHooks::UserQuit()) {
    ShutdownGame();
    return 0;
  }

  StartDisplay();

  StoreActualGraphicOptions();
  LOG->Info("%s", GetActualGraphicOptionsString().c_str());

  SONGMAN->PreloadSongImages();

  /* Input handlers can have dependences on the video system so
   * INPUTMAN must be initialized after DISPLAY. */
  INPUTMAN = new RageInput;

  // These things depend on the TextureManager, so do them after!
  FONT = new FontManager;
  SCREENMAN = new ScreenManager;

  StepMania::ResetGame();

  /* Now that GAMESTATE is reset, tell SCREENMAN to update the theme (load
   * overlay screens and global sounds), and load the initial screen. */
  SCREENMAN->ThemeChanged();
  SCREENMAN->SetNewScreen(StepMania::GetInitialScreen());

  // Do this after ThemeChanged so that we can show a system message
  std::string message;
  if (INPUTMAPPER->CheckForChangedInputDevicesAndRemap(message)) {
    SCREENMAN->SystemMessage(message);
  }

  CodeDetector::RefreshCacheItems();

  // Run the main loop.
  GameLoop::RunGameLoop();

  PREFSMAN->SavePrefsToDisk();

  ShutdownGame();

  return 0;
}

std::string StepMania::SaveScreenshot(
    std::string dir, bool save_compressed, bool make_signature,
    std::string name_prefix, std::string name_suffix) {
  /* As of sm-ssc v1.0 rc2, screenshots are no longer named by an arbitrary
   * index. This was causing naming issues for some unknown reason, so we have
   * changed the screenshot names to a non-blocking format: date and time.
   * As before, we ignore the extension. -aj */
  std::string file_name_no_extension =
      name_prefix + DateTime::GetNowDateTime().GetString() + name_suffix;
  // replace space with underscore.
  Replace(file_name_no_extension, " ", "_");
  // colons are illegal in filenames.
  Replace(file_name_no_extension, ":", "");

  // Save the screenshot. If writing lossy to a memcard, use
  // SAVE_LOSSY_LOW_QUAL, so we don't eat up lots of space.
  RageDisplay::GraphicsFileFormat fmt;
  if (save_compressed) {
    fmt = RageDisplay::SAVE_LOSSY_HIGH_QUAL;
  } else {
    fmt = RageDisplay::SAVE_LOSSLESS_SENSIBLE;
  }

  std::string file_name =
      file_name_no_extension + "." + (save_compressed ? "jpg" : "png");
  std::string path = dir + file_name;

  if (!DISPLAY->SaveScreenshot(path, fmt)) {
    SCREENMAN->PlayInvalidSound();
    return std::string();
  }

  SCREENMAN->PlayScreenshotSound();

  if (PREFSMAN->sign_profile_data_ && make_signature) {
    CryptManager::SignFileToFile(path);
  }

  return file_name;
}

void StepMania::InsertCoin(int num, bool count_in_bookkeeping) {
  if (count_in_bookkeeping) {
    LIGHTSMAN->PulseCoinCounter();
    BOOKKEEPER->CoinInserted();
  }
  int num_coins_old = GAMESTATE->coins_;

  // Don't allow GAMESTATE's coin count to become negative.
  if (GAMESTATE->coins_ + num >= 0) {
    GAMESTATE->coins_.Set(GAMESTATE->coins_ + num);
  }

  int credits = GAMESTATE->coins_ / PREFSMAN->coins_per_credit_;
  bool max_credits = credits >= PREFSMAN->max_num_credits_;
  if (max_credits) {
    GAMESTATE->coins_.Set(
        PREFSMAN->max_num_credits_ * PREFSMAN->coins_per_credit_);
  }

  LOG->Trace(
      "%i coins inserted, %i needed to play", GAMESTATE->coins_.Get(),
      PREFSMAN->coins_per_credit_.Get());

  // On InsertCoin, make sure to update Coins file
  BOOKKEEPER->WriteCoinsFile(GAMESTATE->coins_.Get());

  // If inserting coins, play an appropriate sound; if deducting coins, don't
  // play anything.
  if (num > 0) {
    if (num_coins_old != GAMESTATE->coins_) {
      SCREENMAN->PlayCoinSound();
    } else {
      SCREENMAN->PlayInvalidSound();
    }
  }

  /* If AutoJoin and a player is already joined, then try to join a player.
   * (If no players are joined, they'll join on the first JoinInput.) */
  if (GAMESTATE->auto_join_.Get() && GAMESTATE->GetNumSidesJoined() > 0) {
    if (GAMESTATE->GetNumSidesJoined() > 0 && GAMESTATE->JoinPlayers()) {
      SCREENMAN->PlayStartSound();
    }
  }

  // TODO: remove this redundant message and things that depend on it
  Message msg("CoinInserted");
  // below params are unused
  // msg.SetParam( "Coins", GAMESTATE->m_iCoins );
  // msg.SetParam( "Inserted", iNum );
  // msg.SetParam( "MaxCredits", bMaxCredits );
  MESSAGEMAN->Broadcast(msg);
}

void StepMania::InsertCredit() {
  InsertCoin(PREFSMAN->coins_per_credit_, false);
}

void StepMania::ClearCredits() {
  LOG->Trace("%i coins cleared", GAMESTATE->coins_.Get());
  GAMESTATE->coins_.Set(0);
  SCREENMAN->PlayInvalidSound();

  // Update Coins file to make sure credits are cleared.
  BOOKKEEPER->WriteCoinsFile(GAMESTATE->coins_.Get());

  // TODO: remove this redundant message and things that depend on it
  Message msg("CoinInserted");
  // below params are unused
  // msg.SetParam( "Coins", GAMESTATE->m_iCoins );
  // msg.SetParam( "Clear", true );
  MESSAGEMAN->Broadcast(msg);
}

/* Returns true if the key has been handled and should be discarded, false if
 * the key should be sent on to screens. */
static LocalizedString SERVICE_SWITCH_PRESSED(
    "StepMania", "Service switch pressed");
static LocalizedString RELOADED_METRICS("ThemeManager", "Reloaded metrics");
static LocalizedString RELOADED_METRICS_AND_TEXTURES(
    "ThemeManager", "Reloaded metrics and textures");
static LocalizedString RELOADED_SCRIPTS("ThemeManager", "Reloaded scripts");
static LocalizedString RELOADED_OVERLAY_SCREENS(
    "ThemeManager", "Reloaded overlay screens");
bool HandleGlobalInputs(const InputEventPlus& input) {
  // None of the globals keys act on types other than FIRST_PRESS
  if (input.type_ != IET_FIRST_PRESS) {
    return false;
  }

  switch (input.menu_i_) {
    case GAME_BUTTON_OPERATOR:
      /* Global operator key, to get quick access to the options menu. Don't
       * do this if we're on a "system menu", which includes the editor
       * (to prevent quitting without storing changes). */
      if (SCREENMAN->AllowOperatorMenuButton()) {
        SCREENMAN->SystemMessage(SERVICE_SWITCH_PRESSED);
        SCREENMAN->PopAllScreens();
        GAMESTATE->Reset();
        SCREENMAN->SetNewScreen(CommonMetrics::OPERATOR_MENU_SCREEN);
      }
      return true;

    case GAME_BUTTON_COIN:
      // Handle a coin insertion.
      if (GAMESTATE->IsEditing())  // no coins while editing
      {
        LOG->Trace("Ignored coin insertion (editing)");
        break;
      }
      StepMania::InsertCoin();
      return false;  // Attract needs to know because it goes to TitleMenu on >
                     // 1 credit
    default:
      break;
  }

  /* Re-added for StepMania 3.9 theming veterans, plus it's just faster than
   * the debug menu. The Shift button only reloads the metrics, unlike in 3.9
   * (where it saved bookkeeping and machine profile). -aj */
  bool is_shift_held =
      INPUTFILTER->IsBeingPressed(
          DeviceInput(DEVICE_KEYBOARD, KEY_LSHIFT), &input.input_list_) ||
      INPUTFILTER->IsBeingPressed(
          DeviceInput(DEVICE_KEYBOARD, KEY_RSHIFT), &input.input_list_);
  bool is_ctrl_held =
      INPUTFILTER->IsBeingPressed(
          DeviceInput(DEVICE_KEYBOARD, KEY_LCTRL), &input.input_list_) ||
      INPUTFILTER->IsBeingPressed(
          DeviceInput(DEVICE_KEYBOARD, KEY_RCTRL), &input.input_list_);
  if (input.device_i_ == DeviceInput(DEVICE_KEYBOARD, KEY_F2)) {
    if (is_shift_held && !is_ctrl_held) {
      // Shift+F2: refresh metrics,noteskin cache and CodeDetector cache only
      THEME->ReloadMetrics();
      NOTESKIN->RefreshNoteSkinData(GAMESTATE->cur_game_);
      CodeDetector::RefreshCacheItems();
      SCREENMAN->SystemMessage(RELOADED_METRICS);
    } else if (is_ctrl_held && !is_shift_held) {
      // Ctrl+F2: reload scripts only
      if (NETWORK != nullptr) {
        NETWORK->CloseAllWebSockets();
      }
      THEME->UpdateLuaGlobals();
      SCREENMAN->SystemMessage(RELOADED_SCRIPTS);
    } else if (is_ctrl_held && is_shift_held) {
      // Shift+Ctrl+F2: reload overlay screens (and metrics, since themers
      // are likely going to do this after changing metrics.)
      THEME->ReloadMetrics();
      SCREENMAN->ReloadOverlayScreens();
      SCREENMAN->SystemMessage(RELOADED_OVERLAY_SCREENS);
    } else {
      // F2 alone: refresh metrics, textures, noteskins, codedetector cache
      THEME->ReloadMetrics();
      TEXTUREMAN->ReloadAll();
      NOTESKIN->RefreshNoteSkinData(GAMESTATE->cur_game_);
      CodeDetector::RefreshCacheItems();
      SCREENMAN->SystemMessage(RELOADED_METRICS_AND_TEXTURES);
    }

    return true;
  }

  if (input.device_i_ == DeviceInput(DEVICE_KEYBOARD, KEY_PAUSE)) {
    Message msg("ToggleConsoleDisplay");
    MESSAGEMAN->Broadcast(msg);
    return true;
  }

#if !defined(MACOSX)
  if (input.device_i_ == DeviceInput(DEVICE_KEYBOARD, KEY_F4)) {
    if (INPUTFILTER->IsBeingPressed(
            DeviceInput(DEVICE_KEYBOARD, KEY_RALT), &input.input_list_) ||
        INPUTFILTER->IsBeingPressed(
            DeviceInput(DEVICE_KEYBOARD, KEY_LALT), &input.input_list_)) {
      // pressed Alt+F4
      ArchHooks::SetUserQuit();
      return true;
    }
  }
#else
  if (input.device_i_ == DeviceInput(DEVICE_KEYBOARD, KEY_Cq) &&
      (INPUTFILTER->IsBeingPressed(
           DeviceInput(DEVICE_KEYBOARD, KEY_LMETA), &input.input_list_) ||
       INPUTFILTER->IsBeingPressed(
           DeviceInput(DEVICE_KEYBOARD, KEY_RMETA), &input.input_list_))) {
    /* The user quit is handled by the menu item so we don't need to set it
     * here; however, we do want to return that it has been handled since
     * this will happen first. */
    return true;
  }
#endif

  bool do_screenshot =
#if defined(MACOSX)
      // Notebooks don't have F13. Use cmd-F12 as well.
      input.device_i_ == DeviceInput(DEVICE_KEYBOARD, KEY_PRTSC) ||
      input.device_i_ == DeviceInput(DEVICE_KEYBOARD, KEY_F13) ||
      (input.device_i_ == DeviceInput(DEVICE_KEYBOARD, KEY_F12) &&
       (INPUTFILTER->IsBeingPressed(
            DeviceInput(DEVICE_KEYBOARD, KEY_LMETA), &input.input_list_) ||
        INPUTFILTER->IsBeingPressed(
            DeviceInput(DEVICE_KEYBOARD, KEY_RMETA), &input.input_list_)));
#else
      /* The default Windows message handler will capture the desktop window
       * upon pressing PrntScrn, or will capture the foreground with focus upon
       * pressing Alt+PrntScrn. Windows will do this whether or not we save a
       * screenshot ourself by dumping the frame buffer. */
      // "if pressing PrintScreen and not pressing Alt"
      input.device_i_ == DeviceInput(DEVICE_KEYBOARD, KEY_PRTSC) &&
      !INPUTFILTER->IsBeingPressed(
          DeviceInput(DEVICE_KEYBOARD, KEY_LALT), &input.input_list_) &&
      !INPUTFILTER->IsBeingPressed(
          DeviceInput(DEVICE_KEYBOARD, KEY_RALT), &input.input_list_);
#endif
  if (do_screenshot) {
    // If holding Shift save uncompressed, else save compressed
    bool holding_shift =
        (INPUTFILTER->IsBeingPressed(
             DeviceInput(DEVICE_KEYBOARD, KEY_LSHIFT)) ||
         INPUTFILTER->IsBeingPressed(DeviceInput(DEVICE_KEYBOARD, KEY_RSHIFT)));
    bool save_compressed = !holding_shift;
    RageTimer timer;
    StepMania::SaveScreenshot("Screenshots/", save_compressed, false, "", "");
    LOG->Trace("Screenshot took %f seconds.", timer.GetDeltaTime());
    return true;  // handled
  }

  if (input.device_i_ == DeviceInput(DEVICE_KEYBOARD, KEY_ENTER) &&
      (INPUTFILTER->IsBeingPressed(
           DeviceInput(DEVICE_KEYBOARD, KEY_RALT), &input.input_list_) ||
       INPUTFILTER->IsBeingPressed(
           DeviceInput(DEVICE_KEYBOARD, KEY_LALT), &input.input_list_))) {
    // alt-enter
    /* In macOS, this is a menu item and will be handled as such. This will
     * happen first and then the lower priority GUI thread will happen second,
     * causing the window to toggle twice. Another solution would be to put
     * a timer in ArchHooks::SetToggleWindowed() and just not set the bool
     * it if it's been less than, say, half a second. */
#if !defined(MACOSX)
    ArchHooks::SetToggleWindowed();
#endif
    return true;
  }

  return false;
}

void HandleInputEvents(float delta_time) {
  INPUTFILTER->Update(delta_time);

  /* Hack: If the topmost screen hasn't been updated yet, don't process input,
   * since we must not send inputs to a screen that hasn't at least had one
   * update yet. (The first Update should be the very first thing a screen
   * gets.) We'll process it next time. Call Update above, so the inputs are
   * read and timestamped. */
  if (SCREENMAN->GetTopScreen()->IsFirstUpdate()) {
    return;
  }

  std::vector<InputEvent> array;
  INPUTFILTER->GetInputEvents(array);

  // If we don't have focus, discard input.
  if (!HOOKS->AppHasFocus()) {
    return;
  }

  if (!array.empty()) {
    GameLoop::ResetInputIdleTimer();
  }

  for (unsigned i = 0; i < array.size(); i++) {
    InputEventPlus input;
    input.device_i_ = array[i].di_;
    input.type_ = array[i].type_;
    swap(input.input_list_, array[i].button_state_);

    INPUTMAPPER->DeviceToGame(input.device_i_, input.game_i_);

    if (input.game_i_.IsValid()) {
      input.menu_i_ =
          INPUTMAPPER->GameButtonToMenuButton(input.game_i_.button_);
      input.pn_ =
          INPUTMAPPER->ControllerToPlayerNumber(input.game_i_.controller_);
    }

    INPUTQUEUE->RememberInput(input);

    // When a GameButton is pressed, stop repeating other keys on the same
    // controller.
    if (input.type_ == IET_FIRST_PRESS && input.menu_i_ != GameButton_Invalid) {
      FOREACH_ENUM(GameButton, m) {
        if (input.menu_i_ != m) {
          INPUTMAPPER->RepeatStopKey(m, input.pn_);
        }
      }
    }

    if (HandleGlobalInputs(input)) {
      continue;  // skip
    }

    // check back in event mode
    if (GAMESTATE->IsEventMode() &&
        CodeDetector::EnteredCode(
            input.game_i_.controller_, CODE_BACK_IN_EVENT_MODE)) {
      input.menu_i_ = GAME_BUTTON_BACK;
    }

    SCREENMAN->Input(input);
  }

  if (ArchHooks::GetAndClearToggleWindowed()) {
    PREFSMAN->windowed_.Set(!PREFSMAN->windowed_);
    StepMania::ApplyGraphicOptions();
  }
}

#include "LuaManager.h"
int LuaFunc_SaveScreenshot(lua_State* L);
int LuaFunc_SaveScreenshot(lua_State* L) {
  // If pn is provided, save to that player's profile.
  // Otherwise, save to the machine.
  PlayerNumber pn = Enum::Check<PlayerNumber>(L, 1, true);
  bool compress = lua_toboolean(L, 2) > 0;
  bool sign = lua_toboolean(L, 3) > 0;
  std::string prefix = luaL_optstring(L, 4, "");
  std::string suffix = luaL_optstring(L, 5, "");
  std::string dir;
  if (pn == PlayerNumber_Invalid) {
    dir = "Screenshots/";
  } else {
    dir = PROFILEMAN->GetProfileDir((ProfileSlot)pn) + "Screenshots/";
    if (PROFILEMAN->ProfileWasLoadedFromMemoryCard(pn)) {
      MEMCARDMAN->MountCard(pn);
    }
  }
  std::string filename =
      StepMania::SaveScreenshot(dir, compress, sign, prefix, suffix);
  if (pn != PlayerNumber_Invalid) {
    if (PROFILEMAN->ProfileWasLoadedFromMemoryCard(pn)) {
      MEMCARDMAN->UnmountCard(pn);
    }
  }
  std::string path = dir + filename;
  lua_pushboolean(L, !filename.empty());
  lua_pushstring(L, path.c_str());
  return 2;
}
void LuaFunc_Register_SaveScreenshot(lua_State* L);
void LuaFunc_Register_SaveScreenshot(lua_State* L) {
  lua_register(L, "SaveScreenshot", LuaFunc_SaveScreenshot);
}
REGISTER_WITH_LUA_FUNCTION(LuaFunc_Register_SaveScreenshot);

static int LuaFunc_update_centering(lua_State* L) {
  update_centering();
  return 0;
}
LUAFUNC_REGISTER_COMMON(update_centering);

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
