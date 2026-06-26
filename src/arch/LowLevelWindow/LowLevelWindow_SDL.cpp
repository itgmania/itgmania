#include "LowLevelWindow_SDL.h"

#include "DisplaySpec.h"
#include "PrefsManager.h"
#include "RageDisplay.h"  // VideoModeParams
#include "RageDisplay_OGL_Helpers.h"
#include "RageException.h"
#include "RageLog.h"
#include "RageTimer.h"
#include "global.h"
using namespace RageDisplay_Legacy_Helpers;

#include <GL/glew.h>
#include <SDL2/SDL.h>

#include <cmath>
#include <cstdint>
#include <set>
#include <string>

LowLevelWindow_SDL::LowLevelWindow_SDL()
    : m_window(nullptr), m_glContext(nullptr), m_bWasWindowed(true) {
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    std::string sError =
        ssprintf("SDL_Init(SDL_INIT_VIDEO) failed: %s", SDL_GetError());
    FAIL_M(sError.c_str());
  }

  LOG->Info("SDL Video Driver: %s", SDL_GetCurrentVideoDriver());
}

LowLevelWindow_SDL::~LowLevelWindow_SDL() {
  DestroyGLContext();
  if (m_window) {
    SDL_DestroyWindow(m_window);
    m_window = nullptr;
  }
  SDL_Quit();
}

void LowLevelWindow_SDL::RestoreOutputConfig() {
  // SDL handles this automatically
}

void* LowLevelWindow_SDL::GetProcAddress(std::string s) {
  return SDL_GL_GetProcAddress(s.c_str());
}

bool LowLevelWindow_SDL::CreateGLContext() {
  if (!m_window) {
    return false;
  }

  m_glContext = SDL_GL_CreateContext(m_window);
  if (!m_glContext) {
    LOG->Warn("Failed to create OpenGL context: %s", SDL_GetError());
    return false;
  }

  SDL_GL_MakeCurrent(m_window, m_glContext);

  return true;
}

void LowLevelWindow_SDL::DestroyGLContext() {
  if (m_glContext) {
    SDL_GL_DeleteContext(m_glContext);
    m_glContext = nullptr;
  }
}

std::string LowLevelWindow_SDL::TryVideoMode(
    const VideoModeParams& p, bool& bNewDeviceOut) {
  static bool bLoggedParamsOnce = false;

  if (m_glContext == nullptr || p.bpp != CurrentParams.bpp ||
      m_bWasWindowed != p.windowed) {
    bool bFirstRun = m_glContext == nullptr;
    bNewDeviceOut = true;

    DestroyGLContext();
    if (m_window) {
      SDL_DestroyWindow(m_window);
      m_window = nullptr;
    }

    // Set GL attributes before creating the window/context.
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    // Prefer a compatibility context; avoid requesting too-new core profiles.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 4);

    Uint32 flags = SDL_WINDOW_OPENGL;
    if (!p.windowed) {
      flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }

    m_window = SDL_CreateWindow(
        p.sWindowTitle.c_str(), SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED, p.width, p.height, flags);

    if (!m_window) {
      return ssprintf("Failed to create SDL window: %s", SDL_GetError());
    }

    if (!CreateGLContext()) {
      return "Failed to create OpenGL context";
    }

    // Apply vsync preference (SDL returns 0 on success, -1 on failure).
    {
      const int swapInterval = p.vsync ? 1 : 0;
      const int ret = SDL_GL_SetSwapInterval(swapInterval);
      if (ret != 0) {
        LOG->Warn(
            "SDL_GL_SetSwapInterval(%d) failed: %s", swapInterval,
            SDL_GetError());
      }
    }

    // First GLEW init
    if (bFirstRun) {
      GLenum err = glewInit();
      if (err != GLEW_OK) {
        return ssprintf("glewInit failed: %s", glewGetErrorString(err));
      }
    }

    // Compute actual window size (used by RageDisplay viewport).
    int windowWidth = 0;
    int windowHeight = 0;
    SDL_GetWindowSize(m_window, &windowWidth, &windowHeight);

    // Get the refresh rate from the display
    int displayIndex = SDL_GetWindowDisplayIndex(m_window);
    if (displayIndex < 0) {
      displayIndex = 0;  // Fallback to primary display
    }

    SDL_DisplayMode mode;
    int refreshRate = p.rate;
    if (SDL_GetCurrentDisplayMode(displayIndex, &mode) == 0 &&
        mode.refresh_rate > 0) {
      refreshRate = mode.refresh_rate;
    } else {
      if (refreshRate <= 0) {
        refreshRate = 60;
      }
    }

    CurrentParams = ActualVideoModeParams(
        p, windowWidth, windowHeight, /*renderOffscreen=*/false);
    CurrentParams.rate = refreshRate;
    m_bWasWindowed = p.windowed;
  } else {
    bNewDeviceOut = false;

    if (!p.windowed && m_bWasWindowed) {
      SDL_SetWindowBordered(m_window, SDL_FALSE);
      // enter borderless fullscreen
      SDL_SetWindowFullscreen(m_window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    } else if (p.windowed && !m_bWasWindowed) {
      // exit fullscreen
      SDL_SetWindowFullscreen(m_window, 0);
      // restore borders
      SDL_SetWindowBordered(m_window, SDL_TRUE);
    }

    int w, h;
    SDL_GetWindowSize(m_window, &w, &h);
    if (w != p.width || h != p.height) {
      SDL_SetWindowSize(m_window, p.width, p.height);
    }

    int windowWidth = 0;
    int windowHeight = 0;
    SDL_GetWindowSize(m_window, &windowWidth, &windowHeight);

    // update refresh rate from display
    int displayIndex = SDL_GetWindowDisplayIndex(m_window);
    if (displayIndex < 0) {
      displayIndex = 0;
    }

    SDL_DisplayMode mode;
    int refreshRate = p.rate;
    if (SDL_GetCurrentDisplayMode(displayIndex, &mode) == 0 &&
        mode.refresh_rate > 0) {
      refreshRate = mode.refresh_rate;
    } else {
      if (refreshRate <= 0) {
        refreshRate = 60;
      }
    }

    CurrentParams = ActualVideoModeParams(
        p, windowWidth, windowHeight, /*renderOffscreen=*/false);
    CurrentParams.rate = refreshRate;
    m_bWasWindowed = p.windowed;

    // Apply vsync preference (SDL returns 0 on success, -1 on failure).
    {
      const int swapInterval = p.vsync ? 1 : 0;
      const int ret = SDL_GL_SetSwapInterval(swapInterval);
      if (ret != 0) {
        LOG->Warn(
            "SDL_GL_SetSwapInterval(%d) failed: %s", swapInterval,
            SDL_GetError());
      }
    }
  }

  LOG->Info(
      "Video Mode: %dx%d %d-bit %s", p.width, p.height, p.bpp,
      p.windowed ? "windowed" : "fullscreen");

  if (!bLoggedParamsOnce) {
    bLoggedParamsOnce = true;

    const int swapInterval = SDL_GL_GetSwapInterval();
    const Uint32 windowFlags =
        (m_window != nullptr) ? SDL_GetWindowFlags(m_window) : 0;

#ifdef DEBUG
    LOG->Info(
        "SDL ActualVideoModeParams: windowed=%d displayId='%s'",
        CurrentParams.windowed ? 1 : 0, CurrentParams.sDisplayId.c_str());
    LOG->Info(
        "SDL ActualVideoModeParams: render=%dx%d window=%dx%d bpp=%d",
        CurrentParams.width, CurrentParams.height, CurrentParams.windowWidth,
        CurrentParams.windowHeight, CurrentParams.bpp);
    LOG->Info(
        "SDL ActualVideoModeParams: rate=%d vsync=%d swapInterval=%d "
        "fullscreenBorderless=%d",
        CurrentParams.rate, CurrentParams.vsync ? 1 : 0, swapInterval,
        CurrentParams.bWindowIsFullscreenBorderless ? 1 : 0);
    LOG->Info(
        "SDL ActualVideoModeParams: aspect=%.6f interlaced=%d smoothLines=%d "
        "trilinear=%d aniso=%d",
        CurrentParams.fDisplayAspectRatio, CurrentParams.interlaced ? 1 : 0,
        CurrentParams.bSmoothLines ? 1 : 0,
        CurrentParams.bTrilinearFiltering ? 1 : 0,
        CurrentParams.bAnisotropicFiltering ? 1 : 0);
    LOG->Info(
        "SDL ActualVideoModeParams: renderOffscreen=%d",
        CurrentParams.renderOffscreen ? 1 : 0);
    LOG->Info(
        "SDL Window: flags=0x%08x", static_cast<unsigned int>(windowFlags));
#endif
  }

  return "";
}

void LowLevelWindow_SDL::Update() {
  SDL_PumpEvents();
  SDL_ShowCursor(PREFSMAN->m_bShowMouseCursor ? SDL_ENABLE : SDL_DISABLE);
}

void LowLevelWindow_SDL::LogDebugInformation() const {
  if (m_window) {
    int w, h;
    SDL_GetWindowSize(m_window, &w, &h);
    LOG->Info("SDL Window Size: %dx%d", w, h);
    LOG->Info(
        "SDL Window Refresh Rate: %d Hz", GetActualVideoModeParams().rate);
  }

#ifdef DEBUG
  LOG->Info(
      "SDL Version: %d.%d.%d", SDL_MAJOR_VERSION, SDL_MINOR_VERSION,
      SDL_PATCHLEVEL);
  LOG->Info(
      "OpenGL Vendor: %s",
      reinterpret_cast<const char*>(glGetString(GL_VENDOR)));
  LOG->Info(
      "OpenGL Renderer: %s",
      reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
  LOG->Info(
      "OpenGL Version: %s",
      reinterpret_cast<const char*>(glGetString(GL_VERSION)));
#endif
}

bool LowLevelWindow_SDL::IsSoftwareRenderer(std::string& sError) {
  if (!m_glContext) {
    return true;
  }

  const char* renderer =
      reinterpret_cast<const char*>(glGetString(GL_RENDERER));
  if (renderer &&
      (strstr(renderer, "software") || strstr(renderer, "Software"))) {
    sError = "Using software renderer";
    return true;
  }

  return false;
}

void LowLevelWindow_SDL::SwapBuffers() {
  SDL_GL_SwapWindow(m_window);
  // The screensaver is disabled by default since SDL 2.0.2, so there's no need
  // to check for m_bDisableScreenSaver.
}

void LowLevelWindow_SDL::GetDisplaySpecs(DisplaySpecs& out) const {
  out.clear();

  int numDisplays = SDL_GetNumVideoDisplays();
  if (numDisplays < 1) {
    LOG->Warn("No displays detected");
    return;
  }

  for (int d = 0; d < numDisplays; ++d) {
    std::set<DisplayMode> displayModes;
    DisplayMode currentMode{};
    bool hasCurrentMode = false;

    int numModes = SDL_GetNumDisplayModes(d);
    for (int m = 0; m < numModes; ++m) {
      SDL_DisplayMode mode;
      if (SDL_GetDisplayMode(d, m, &mode) == 0) {
        DisplayMode displayMode;
        displayMode.width = mode.w;
        displayMode.height = mode.h;
        displayMode.refreshRate =
            mode.refresh_rate > 0 ? mode.refresh_rate : 60.0;
        displayModes.insert(displayMode);
      }
    }

    // Get current display mode if available
    SDL_DisplayMode currentSDLMode;
    if (SDL_GetCurrentDisplayMode(d, &currentSDLMode) == 0) {
      currentMode.width = currentSDLMode.w;
      currentMode.height = currentSDLMode.h;
      currentMode.refreshRate =
          currentSDLMode.refresh_rate > 0 ? currentSDLMode.refresh_rate : 60.0;
      hasCurrentMode = true;
    }

    // Create display ID
    std::string displayId = std::string("SDL-Display-") + std::to_string(d);
    std::string displayName = std::string("Display ") + std::to_string(d + 1);

    // Add the display spec to output
    if (hasCurrentMode && !displayModes.empty()) {
      // Get the bounds for the current mode
      SDL_Rect displayBounds;
      SDL_GetDisplayBounds(d, &displayBounds);
      RectI bounds(
          displayBounds.x, displayBounds.y, displayBounds.x + displayBounds.w,
          displayBounds.y + displayBounds.h);
      out.insert(DisplaySpec(
          displayId, displayName, displayModes, currentMode, bounds));
    } else if (!displayModes.empty()) {
      // Display has no active mode
      out.insert(DisplaySpec(displayId, displayName, displayModes));
    }
  }
}

/* The following functions are intentionally not implemented */
bool LowLevelWindow_SDL::SupportsRenderToTexture() const { return false; }
RenderTarget* LowLevelWindow_SDL::CreateRenderTarget() { return nullptr; }
bool LowLevelWindow_SDL::SupportsFullscreenBorderlessWindow() const {
  return true;
}
bool LowLevelWindow_SDL::SupportsThreadedRendering() { return false; }
void LowLevelWindow_SDL::BeginConcurrentRenderingMainThread() {}
void LowLevelWindow_SDL::EndConcurrentRenderingMainThread() {}
void LowLevelWindow_SDL::BeginConcurrentRendering() {}
void LowLevelWindow_SDL::EndConcurrentRendering() {}