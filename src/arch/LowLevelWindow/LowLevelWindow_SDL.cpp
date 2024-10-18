#include "global.h"
#include "LowLevelWindow_SDL.h"
#include "arch/arch_default.h"
#include "RageLog.h"

#include <SDL3/SDL.h>

#include "DisplaySpec.h"
#include "Preference.h"
#include "RageSurface.h"
#include "RageSurface_Load.h"

Preference<bool> g_bSDLLinuxUseWayland("SDLLinuxUseWayland", true);

LowLevelWindow_SDL::LowLevelWindow_SDL()
{
    LOG->Info("[LLW_SDL] Using LowLevelWindow_SDL");
    m_pWindow = nullptr;
    m_pGlContext = nullptr;

#if defined(LINUX)
    // Nudge SDL3 to use wayland if requested (this is only needed until SDL uses wayland by default)
    if (g_bSDLLinuxUseWayland.Get())
    {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "wayland");
    }
#endif

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD);
}

LowLevelWindow_SDL::~LowLevelWindow_SDL()
{
    SDL_GL_DestroyContext(m_pGlContext);
    SDL_DestroyWindow(m_pWindow);
    SDL_Quit();
}

void* LowLevelWindow_SDL::GetProcAddress(RString s)
{
    return SDL_GL_GetProcAddress(s);
}

RString LowLevelWindow_SDL::TryVideoMode(const VideoModeParams& p, bool& bNewDeviceOut)
{
    // Ensure we're using compat profile
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);

    if (!m_pWindow)
    {
        m_pWindow = SDL_CreateWindow(p.sWindowTitle, p.width, p.height, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (!m_pWindow)
        {
            LOG->Info("[LLW_SDL::TryVideoMode] Failed to create SDL Window: %s", SDL_GetError());
            return "Failed to create SDL Window - Check logs";
        }

        SDL_SetWindowPosition(m_pWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        m_pGlContext = SDL_GL_CreateContext(m_pWindow);

        if (!m_pGlContext)
        {
            LOG->Info("[LLW_SDL::TryVideoMode] Failed to create GL Context: %s", SDL_GetError());
            return "Failed to create GL Context - Check logs";
        }

        SDL_GL_MakeCurrent(m_pWindow, m_pGlContext);
        SDL_ShowWindow(m_pWindow);

        // New window created
        bNewDeviceOut = true;
    } else
    {
        bNewDeviceOut = false;
    }

    // Adjust for any hidpi scaling & prevent division by zero
    // Note: This is rounded to the nearest 4 decimals to prevent improperly set scale values
    auto windowScale = std::floor(SDL_GetWindowDisplayScale(m_pWindow) * 1000) / 1000;
    if (windowScale <= 0.01f)
    {
        windowScale = 1.0f;
    }
    SDL_SetWindowSize(m_pWindow, static_cast<int>(static_cast<float>(p.width) / windowScale), static_cast<int>(static_cast<float>(p.height) / windowScale));

    // Retrieve the real pixel size in case we have a high-dpi display
    int widthInPixels = p.width;
    int heightInPixels = p.height;

    // We want the currentDisplay (monitor), the current display mode (resolution),
    // and the real window size in pixels (ignoring hidpi)
    auto currentDisplay = SDL_GetDisplayForWindow(m_pWindow);
    auto currentDisplayMode = SDL_GetCurrentDisplayMode(currentDisplay);
    SDL_GetWindowSizeInPixels(m_pWindow, &widthInPixels, &heightInPixels);

    LOG->Info("[LLW_SDL::TryVideoMode] Adjusting for HiDPI scale of %f. Requested: [%dx%d] Real: [%dx%d]", windowScale,
              p.width, p.height, widthInPixels, heightInPixels);

    // Adjust VSync, fullscreen, and borderless modes if needed
    SDL_GL_SetSwapInterval(p.vsync);
    SDL_SetWindowFullscreen(m_pWindow, !p.windowed);
    SDL_SyncWindow(m_pWindow);
    SDL_SetWindowBordered(m_pWindow, !p.windowed || !p.bWindowIsFullscreenBorderless);

    // If we're just resizing we should re-centre the window
    if (!bNewDeviceOut)
    {
        SDL_SetWindowPosition(m_pWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }

    if (p.sIconFile)
    {
        SetWindowIcon(p.sIconFile);
    }

    // Set the "Real Width/Height" to the HiDpi values so the scaling is correct
    m_CurrentVideoParams = ActualVideoModeParams(p, widthInPixels, heightInPixels, false);
    // Set the refresh rate to the current display's refresh rate
    // Note: The casting to int can cause refresh rates like 59.<whatever> to be truncated to 59.0.
    // I'm unsure if we want to just ceil the values here.
    m_CurrentVideoParams.rate = static_cast<int>(currentDisplayMode->refresh_rate);

    // Success!
    return "";
}

/**
 * Enumerate a list of displays and display resolutions (or modes).
 * Each display is named, and uses the id: SDL_<order number>.
 * @param out
 */
void LowLevelWindow_SDL::GetDisplaySpecs(DisplaySpecs& out) const
{
    int displayCount = 0;
    const auto displayId = SDL_GetDisplays(&displayCount);

    if (displayCount <= 0)
    {
        LOG->Info("[LLW_SDL::GetDisplaySpecs] Failed to query displays!");
        return;
    }

    int currentWidth = 0;
    int currentHeight = 0;
    SDL_GetWindowSizeInPixels(m_pWindow, &currentWidth, &currentHeight);
    LOG->Info("[LLW_SDL::GetDisplaySpecs] Current size: [%dx%d]", currentWidth, currentHeight);

    int displayModeCount = 0;
    for (int di = 0; di < displayCount; di++)
    {
        SDL_Rect displayBounds;
        std::set<DisplayMode> screenModes;
        const auto displayModes = SDL_GetFullscreenDisplayModes(displayId[di], &displayModeCount);

        SDL_GetDisplayUsableBounds(displayId[di], &displayBounds);

        bool foundCurrentMode = false;
        DisplayMode currentMode = {};
        for (int dmi = 0; dmi < displayModeCount; dmi++)
        {
            auto mode = displayModes[dmi];
            DisplayMode m = {
                static_cast<unsigned int>(mode->w), static_cast<unsigned int>(mode->h), static_cast<double>(mode->refresh_rate)
            };
            screenModes.insert(m);

            if (mode->w == currentWidth && mode->h == currentHeight)
            {
                currentMode = m;
                foundCurrentMode = true;
            }
        }

        // If we haven't found our existing mode, let's create a dummy entry so we don't assert in debug
        if (!foundCurrentMode)
        {
            // For current refresh rate
            auto currentDisplayMode = SDL_GetCurrentDisplayMode(displayId[di]);
            DisplayMode m = {
                static_cast<unsigned int>(currentWidth), static_cast<unsigned int>(currentHeight),
                static_cast<double>(currentDisplayMode->refresh_rate)
            };
            screenModes.insert(m);
            currentMode = m;
        }

        RString displayName = SDL_GetDisplayName(displayId[di]);
        RectI bounds = {displayBounds.x, displayBounds.y, displayBounds.w, displayBounds.h};

        auto id = ssprintf("SDL3_%d", di);
        auto name = ssprintf("%s", displayName.c_str());
        const DisplaySpec screenSpec(id, name, screenModes, currentMode, bounds, true);
        out.insert(screenSpec);
    }
}

void LowLevelWindow_SDL::LogDebugInformation() const
{
    const auto compiled = SDL_VERSION;
    const auto linked = SDL_GetVersion();

    LOG->Info("SDL3 Version (Compiled): %d.%d.%d",
              SDL_VERSIONNUM_MAJOR(compiled),
              SDL_VERSIONNUM_MINOR(compiled),
              SDL_VERSIONNUM_MICRO(compiled));
    LOG->Info("SDL3 Version (Linked): %d.%d.%d",
              SDL_VERSIONNUM_MAJOR(linked),
              SDL_VERSIONNUM_MINOR(linked),
              SDL_VERSIONNUM_MICRO(linked));
    LOG->Info("SDL3 Video Driver: %s", SDL_GetCurrentVideoDriver());
#if defined(LINUX)
    LOG->Info("SDL3 Wants Wayland: %d", g_bSDLLinuxUseWayland.Get());
#endif
}

void LowLevelWindow_SDL::SwapBuffers()
{
    SDL_GL_SwapWindow(m_pWindow);
}

void LowLevelWindow_SDL::Update()
{
    LowLevelWindow::Update();
}

const ActualVideoModeParams LowLevelWindow_SDL::GetActualVideoModeParams() const
{
    return m_CurrentVideoParams;
}

/**
 * Sets the main window's icon.
 * Note: This currently isn't supported on any Wayland desktops, but it will work once it is.
 * @param filePath Path to the icon
 */
void LowLevelWindow_SDL::SetWindowIcon(const RString& filePath) const
{
    RString sError;
    RageSurface* pImg = RageSurfaceUtils::LoadFile(filePath, sError);
    if (pImg == nullptr)
    {
        LOG->Info("[LLW_SDL::SetWindowIcon] Couldn't open icon \"%s\": %s", filePath.c_str(), sError.c_str());
        return;
    }

    SDL_Surface* pSurface = SDL_CreateSurfaceFrom(pImg->w, pImg->h, SDL_PIXELFORMAT_RGBA32, pImg->pixels, pImg->pitch);
    if (!pSurface)
    {
        LOG->Info("[LLW_SDL::SetWindowIcon] Failed to create surface from icon: %s.\nError: %s", filePath.c_str(), SDL_GetError());
        return;
    }
    if (!SDL_SetWindowIcon(m_pWindow, pSurface))
    {
        LOG->Info("[LLW_SDL::SetWindowIcon] Failed to create surface from icon: %s.\nError: %s", filePath.c_str(), SDL_GetError());
        // Don't return here, we still need to delete the surface
    }
    SDL_DestroySurface(pSurface);
}
