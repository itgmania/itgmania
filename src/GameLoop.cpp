#include "global.h"

#include "GameLoop.h"

#include <cmath>
#include <vector>

#include "GameSoundManager.h"
#include "GameState.h"
#include "InputFilter.h"
#include "InputMapper.h"
#include "LightsManager.h"
#include "MemoryCardManager.h"
#include "PrefsManager.h"
#include "RageDisplay.h"
#include "RageFileManager.h"
#include "RageInput.h"
#include "RageLog.h"
#include "RageSoundManager.h"
#include "RageTextureManager.h"
#include "RageTimer.h"
#include "ScreenManager.h"
#include "SongManager.h"
#include "ThemeManager.h"
#include "arch/ArchHooks/ArchHooks.h"

static RageTimer g_GameplayTimer;

static Preference<bool> g_bNeverBoostAppPriority(
    "NeverBoostAppPriority", false);

// Experimental: force a specific update rate. This prevents big animation
// jumps on frame skips. 0 to disable.
static Preference<float> g_fConstantUpdateDeltaSeconds(
    "ConstantUpdateDeltaSeconds", 0);

void HandleInputEvents(float fDeltaTime);

static float g_fUpdateRate = 1;
void GameLoop::SetUpdateRate(float update_rate) { g_fUpdateRate = update_rate; }

static void CheckGameLoopTimerSkips(float delta) {
  if (!PREFSMAN->m_bLogSkips) {
    return;
  }

  static int last_fps = 0;
  int this_fps = DISPLAY->GetFPS();

  // If vsync is on, and we have a solid framerate (vsync == refresh and we've
  // sustained this for at least one second), we expect the amount of time for
  // the last frame to be 1/FPS.
  if (this_fps != DISPLAY->GetActualVideoModeParams().rate ||
      this_fps != last_fps) {
    last_fps = this_fps;
    return;
  }

  const float expected_time = 1.0f / this_fps;
  const float difference = delta - expected_time;
  if (std::abs(difference) > 0.002f && std::abs(difference) < 0.100f) {
    LOG->Trace(
        "GameLoop timer skip: %i FPS, expected %.3f, got %.3f (%.3f "
        "difference)",
        this_fps, expected_time, delta, difference);
  }
}

static bool ChangeAppPriority() {
  if (g_bNeverBoostAppPriority.Get()) {
    return false;
  }

  // if using NTPAD don't boost or else input is laggy
#if defined(_WINDOWS)
  {
    std::vector<InputDeviceInfo> devices;

    // This can get called before INPUTMAN is constructed.
    if (INPUTMAN) {
      INPUTMAN->GetDevicesAndDescriptions(devices);
      if (std::any_of(
              devices.begin(), devices.end(), [](const InputDeviceInfo& d) {
                return d.sDesc.find("NTPAD") != std::string::npos;
              })) {
        LOG->Trace("Using NTPAD.  Don't boost priority.");
        return false;
      }
    }
  }
#endif

  // If this is a debug build, don't. It makes the VC debugger sluggish.
#if defined(WIN32) && defined(DEBUG)
  return false;
#else
  return true;
#endif
}

static void CheckFocus() {
  if (!HOOKS->AppFocusChanged()) {
    return;
  }

  // If we lose focus, we may lose input events, especially key releases.
  INPUTFILTER->Reset();

  if (ChangeAppPriority()) {
    if (HOOKS->AppHasFocus()) {
      HOOKS->BoostPriority();
    } else {
      HOOKS->UnBoostPriority();
    }
  }
}

// On the next update, change themes, and load sNewScreen.
static RString g_NewTheme;
static RString g_NewGame;
void GameLoop::ChangeTheme(const RString& new_theme) { g_NewTheme = new_theme; }

void GameLoop::ChangeGame(const RString& new_game, const RString& new_theme) {
  g_NewGame = new_game;
  g_NewTheme = new_theme;
}

#include "Game.h"
#include "GameManager.h"
#include "StepMania.h"  // XXX
namespace {
void DoChangeTheme() {
  SAFE_DELETE(SCREENMAN);
  TEXTUREMAN->DoDelayedDelete();

  // In case the previous theme overloaded class bindings, reinitialize them.
  LUA->RegisterTypes();

  // We always need to force the theme to reload because we cleared the lua
  // state by calling RegisterTypes so the scripts in Scripts/ need to run.
  THEME->SwitchThemeAndLanguage(
      g_NewTheme, THEME->GetCurLanguage(), PREFSMAN->m_bPseudoLocalize, true);
  PREFSMAN->m_sTheme.Set(g_NewTheme);

  // Apply the new window title, icon and aspect ratio.
  StepMania::ApplyGraphicOptions();

  SCREENMAN = new ScreenManager();

  StepMania::ResetGame();
  SCREENMAN->ThemeChanged();
  // NOTE(kyx): The previous system for changing the theme fetched the
  // "NextScreen" metric from the current theme, then changed the theme, then
  // tried to set the new screen to the name that had been fetched.
  // If the new screen didn't exist in the new theme, there would be a
  // crash.
  // So now the correct thing to do is for a theme to specify its entry
  // point after a theme change, ensuring that we are going to a valid
  // screen and not crashing.
  RString new_screen = THEME->GetMetric("Common", "InitialScreen");
  if (THEME->HasMetric("Common", "AfterThemeChangeScreen")) {
    RString after_screen = THEME->GetMetric("Common", "AfterThemeChangeScreen");
    if (SCREENMAN->IsScreenNameValid(after_screen)) {
      new_screen = after_screen;
    }
  }
  if (!SCREENMAN->IsScreenNameValid(new_screen)) {
    new_screen = "ScreenInitialScreenIsInvalid";
  }
  SCREENMAN->SetNewScreen(new_screen);

  g_NewTheme = RString();
}

void DoChangeGame() {
  const Game* game = GAMEMAN->StringToGame(g_NewGame);
  ASSERT(game != nullptr);
  GAMESTATE->SetCurGame(game);

  bool theme_changing = false;
  // The prefs allow specifying a different default theme to use for each
  // game type.  So if a theme name isn't passed in, fetch from the prefs.
  if (g_NewTheme.empty()) {
    g_NewTheme = PREFSMAN->m_sTheme;
  }
  if (g_NewTheme != THEME->GetCurThemeName() &&
      THEME->IsThemeSelectable(g_NewTheme)) {
    theme_changing = true;
  }

  if (theme_changing) {
    SAFE_DELETE(SCREENMAN);
    TEXTUREMAN->DoDelayedDelete();
    LUA->RegisterTypes();
    THEME->SwitchThemeAndLanguage(
        g_NewTheme, THEME->GetCurLanguage(), PREFSMAN->m_bPseudoLocalize);
    PREFSMAN->m_sTheme.Set(g_NewTheme);
    StepMania::ApplyGraphicOptions();
    SCREENMAN = new ScreenManager();
  }
  StepMania::ResetGame();
  RString new_screen = THEME->GetMetric("Common", "InitialScreen");
  RString after_screen;
  if (theme_changing) {
    SCREENMAN->ThemeChanged();
    if (THEME->HasMetric("Common", "AfterGameAndThemeChangeScreen")) {
      after_screen =
          THEME->GetMetric("Common", "AfterGameAndThemeChangeScreen");
    }
  } else {
    if (THEME->HasMetric("Common", "AfterGameChangeScreen")) {
      after_screen = THEME->GetMetric("Common", "AfterGameChangeScreen");
    }
  }
  if (SCREENMAN->IsScreenNameValid(after_screen)) {
    new_screen = after_screen;
  }
  SCREENMAN->SetNewScreen(new_screen);

  // Set the input scheme for the new game, and load keymaps.
  if (INPUTMAPPER) {
    INPUTMAPPER->SetInputScheme(&game->input_scheme);
    INPUTMAPPER->ReadMappingsFromDisk();
  }
  // NOTE(aj): Reload metrics to force a refresh of
	// CommonMetrics::DIFFICULTIES_TO_SHOW, mainly if we're not switching themes.
	// I'm not sure if this was the case going from theme to theme, but if it was,
	// it should be fixed now. There's probably be a better way to do it, but I'm
	// not sure what it'd be.
  THEME->UpdateLuaGlobals();
  THEME->ReloadMetrics();
  g_NewGame = RString();
  g_NewTheme = RString();
}

}  // namespace

static bool m_bUpdatedDuringVBLANK = false;
void GameLoop::UpdateAllButDraw(bool is_running_from_vblank) {
  // if we are running our once per frame routine and we were already run from
  // VBLANK, we did the work already
  if (!is_running_from_vblank && m_bUpdatedDuringVBLANK) {
    m_bUpdatedDuringVBLANK = false;
    return;  // would it kill us to run it again or do we want to draw asap?
  }

  // if vblank called us, we will tell the game loop we received an update for
  // the frame it wants to process
  if (is_running_from_vblank) {
    m_bUpdatedDuringVBLANK = true;
  } else {
    m_bUpdatedDuringVBLANK = false;
  }

  // Update our stuff
  float delta = g_GameplayTimer.GetDeltaTime();

  if (g_fConstantUpdateDeltaSeconds > 0) {
    delta = g_fConstantUpdateDeltaSeconds;
  }

  CheckGameLoopTimerSkips(delta);

  delta *= g_fUpdateRate;

  // Update SOUNDMAN early (before any RageSound::GetPosition calls), to flush
  // position data.
  SOUNDMAN->Update();

  // NOTE(Glenn): Update song beat information -before- calling update on all
	// the classes that depend on it. If you don't do this first, the classes are
	// all acting on old information and will lag. (but no longer fatally, due to
  // timestamping).
  SOUND->Update(delta);
  TEXTUREMAN->Update(delta);
  GAMESTATE->Update(delta);
  SCREENMAN->Update(delta);
  MEMCARDMAN->Update();

  // Important: Process input AFTER updating game logic, or input will be acting
	// on song beat from last frame
  HandleInputEvents(delta);

  // bandaid for low max audio sample counter
  SOUNDMAN->low_sample_count_workaround();
  LIGHTSMAN->Update(delta);
}

void GameLoop::RunGameLoop() {
  // People may want to do something else while songs are loading, so do this
	// after loading songs.
  if (ChangeAppPriority()) {
    HOOKS->BoostPriority();
  }

  while (!ArchHooks::UserQuit()) {
    if (!g_NewGame.empty()) {
      DoChangeGame();
    }
    if (!g_NewTheme.empty()) {
      DoChangeTheme();
    }

    CheckFocus();

    UpdateAllButDraw(false);

    if (INPUTMAN->DevicesChanged()) {
			// Fix "buttons stuck" once per frame if button held while unplugged.
      INPUTFILTER->Reset();
      INPUTMAN->LoadDrivers();
      RString sMessage;
      if (INPUTMAPPER->CheckForChangedInputDevicesAndRemap(sMessage)) {
        SCREENMAN->SystemMessage(sMessage);
      }
    }

    SCREENMAN->Draw();
  }

  // If we ended mid-game, finish up.
  GAMESTATE->SaveLocalData();

  if (ChangeAppPriority()) {
    HOOKS->UnBoostPriority();
  }
}

class ConcurrentRenderer {
 public:
  ConcurrentRenderer();
  ~ConcurrentRenderer();

  void Start();
  void Stop();

 private:
  RageThread thread_;
  RageEvent event_;
  bool shutdown_;
  void RenderThread();
  static int StartRenderThread(void* p);

  enum State {
    RENDERING_IDLE,
    RENDERING_START,
    RENDERING_ACTIVE,
    RENDERING_END
  };
  State state_;
};
static ConcurrentRenderer* g_pConcurrentRenderer = nullptr;

ConcurrentRenderer::ConcurrentRenderer() : event_("ConcurrentRenderer") {
  shutdown_ = false;
  state_ = RENDERING_IDLE;

  thread_.SetName("ConcurrentRenderer");
  thread_.Create(StartRenderThread, this);
}

ConcurrentRenderer::~ConcurrentRenderer() {
  ASSERT(state_ == RENDERING_IDLE);
  shutdown_ = true;
  thread_.Wait();
}

void ConcurrentRenderer::Start() {
  DISPLAY->BeginConcurrentRenderingMainThread();

  event_.Lock();
  ASSERT(state_ == RENDERING_IDLE);
  state_ = RENDERING_START;
  event_.Signal();
  while (state_ != RENDERING_ACTIVE) {
    event_.Wait();
  }
  event_.Unlock();
}

void ConcurrentRenderer::Stop() {
  event_.Lock();
  ASSERT(state_ == RENDERING_ACTIVE);
  state_ = RENDERING_END;
  event_.Signal();
  while (state_ != RENDERING_IDLE) {
    event_.Wait();
  }
  event_.Unlock();

  DISPLAY->EndConcurrentRenderingMainThread();
}

void ConcurrentRenderer::RenderThread() {
  ASSERT(SCREENMAN != nullptr);

  while (!shutdown_) {
    event_.Lock();
    while (state_ == RENDERING_IDLE && !shutdown_) {
      event_.Wait();
    }
    event_.Unlock();

    if (state_ == RENDERING_START) {
      // We're starting to render. Set up, and then kick the event to wake up
			// the calling thread.
      DISPLAY->BeginConcurrentRendering();
      HOOKS->SetupConcurrentRenderingThread();

      LOG->Trace("ConcurrentRenderer::RenderThread start");

      event_.Lock();
      state_ = RENDERING_ACTIVE;
      event_.Signal();
      event_.Unlock();
    }

    // This is started during Update(). The next thing the game loop will do is
		// Draw, so shift operations around to put Draw at the top. This makes sure
		// updates are seamless.
    if (state_ == RENDERING_ACTIVE) {
      SCREENMAN->Draw();

      float fDeltaTime = g_GameplayTimer.GetDeltaTime();
      SCREENMAN->Update(fDeltaTime);
    }

    if (state_ == RENDERING_END) {
      LOG->Trace("ConcurrentRenderer::RenderThread done");

      DISPLAY->EndConcurrentRendering();

      event_.Lock();
      state_ = RENDERING_IDLE;
      event_.Signal();
      event_.Unlock();
    }
  }
}

int ConcurrentRenderer::StartRenderThread(void* p) {
  ((ConcurrentRenderer*)p)->RenderThread();
  return 0;
}

void GameLoop::StartConcurrentRendering() {
  if (g_pConcurrentRenderer == nullptr) {
    g_pConcurrentRenderer = new ConcurrentRenderer;
  }
  g_pConcurrentRenderer->Start();
}

void GameLoop::FinishConcurrentRendering() { g_pConcurrentRenderer->Stop(); }

/*
 * (c) 2001-2005 Chris Danford, Glenn Maynard
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
