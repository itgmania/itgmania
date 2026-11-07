#include "GraphicsWindow.h"

#include <windows.h>

#include <set>
#include <string>

#include "CommandLineActions.h"
#include "DirectXHelpers.h"
#include "DisplaySpec.h"
#include "PrefsManager.h"
#include "ProductInfo.h"
#include "RageDisplay.h"
#include "RageException.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "arch/ArchHooks/ArchHooks.h"
#include "archutils/Win32/AppInstance.h"
#include "archutils/Win32/Crash.h"
#include "archutils/Win32/ErrorStrings.h"
#include "archutils/Win32/GetFileInformation.h"
#include "archutils/Win32/WindowIcon.h"
#include "global.h"

static const std::string g_sClassName = PRODUCT_ID;

static HWND g_hWndMain;
static HDC g_HDC;
static VideoModeParams g_CurrentParams;
static bool g_bResolutionChanged = false;
static bool g_bHasFocus = true;
static HICON g_hIcon = nullptr;
static bool m_bWideWindowClass;
static bool g_bD3D = false;

// If we're fullscreen, this is the mode we set.
static DEVMODE g_FullScreenDevMode;
static bool g_bRecreatingVideoMode = false;

static UINT g_iQueryCancelAutoPlayMessage = 0;

static std::string GetNewWindow() {
  HWND h = GetForegroundWindow();
  if (h == nullptr) {
    return "(NULL)";
  }

  DWORD process_id;
  GetWindowThreadProcessId(h, &process_id);

  std::string name;
  GetProcessFileName(process_id, name);

  name = Basename(name);

  return name;
}

static LRESULT CALLBACK
GraphicsWindow_WndProc(HWND wnd, UINT msg, WPARAM w_param, LPARAM l_param) {
  CHECKPOINT_M(ssprintf("%p, %u, %08x, %08x", wnd, msg, w_param, l_param));

  // Suppress autorun.
  if (msg == g_iQueryCancelAutoPlayMessage) {
    return true;
  }

  switch (msg) {
    case WM_ACTIVATE: {
      const bool inactive = (LOWORD(w_param) == WA_INACTIVE);
      const bool minimized = (HIWORD(w_param) != 0);
      const bool had_focus = g_bHasFocus;
      g_bHasFocus = !inactive && !minimized;
      LOG->Trace(
          "WM_ACTIVATE (%i, %i): %s", inactive, minimized,
          g_bHasFocus ? "has focus" : "doesn't have focus");
      if (!g_bHasFocus) {
        std::string name = GetNewWindow();
        static std::set<std::string> lost_focus_to;
        lost_focus_to.insert(name);
        std::string str;
        for (std::set<std::string>::const_iterator it = lost_focus_to.begin();
             it != lost_focus_to.end(); ++it) {
          str += (str.size() ? ", " : "") + *it;
        }

        LOG->MapLog("LOST_FOCUS", "Lost focus to: %s", str.c_str());
      }

      if (!g_bD3D && !g_CurrentParams.windowed_ && !g_bRecreatingVideoMode) {
        /* In OpenGL (not D3D), it's our job to unset and reset the
         * full-screen video mode when we focus changes, and to hide
         * and show the window. Hiding is done in WM_KILLFOCUS,
         * because that's where most other apps seem to do it. */
        if (g_bHasFocus && !had_focus) {
          ChangeDisplaySettingsEx(
              g_CurrentParams.display_id_.c_str(), &g_FullScreenDevMode,
              nullptr, CDS_FULLSCREEN, nullptr);
          ShowWindow(g_hWndMain, SW_SHOWNORMAL);
          SetWindowPos(
              g_hWndMain, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        } else if (!g_bHasFocus && had_focus) {
          ChangeDisplaySettingsEx(
              g_CurrentParams.display_id_.c_str(), nullptr, nullptr, 0,
              nullptr);
        }
      }

      return 0;
    }
    case WM_KILLFOCUS:
      if (!g_bD3D && !g_CurrentParams.windowed_ && !g_bRecreatingVideoMode) {
        ShowWindow(g_hWndMain, SW_SHOWMINNOACTIVE);
      }
      break;

      /* Is there any reason we should care what size the user resizes
       * the window to? (who? -aj)
       * Short answer: yes. -aj */
      //	case WM_GETMINMAXINFO:

    case WM_SETCURSOR:
      if (!g_CurrentParams.windowed_) {
        // We check if the preference is false because we don't want
        // to show the cursor if the user has set it to false.
        if (!PREFSMAN->show_mouse_cursor_.Get()) {
          SetCursor(nullptr);
        }
        return 1;
      }
      break;

    case WM_SYSCOMMAND:
      switch (w_param & 0xFFF0) {
        case SC_MONITORPOWER:
        case SC_SCREENSAVE:
          return 0;
      }
      break;

    case WM_PAINT: {
      PAINTSTRUCT ps;
      BeginPaint(wnd, &ps);
      EndPaint(wnd, &ps);
      break;
    }

    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    case WM_MOUSEWHEEL:  // might want to use this for
                         // GET_WHEEL_DELTA_WPARAM(w_param) -aj
      // We handle all input ourself, via DirectInput.
      return 0;

    case WM_CLOSE:
      LOG->Trace("WM_CLOSE: shutting down");
      ArchHooks::SetUserQuit();
      return 0;

    case WM_WINDOWPOSCHANGED: {
      /* If we're fullscreen and don't have focus, our window is hidden,
       * so GetClientRect isn't meaningful. */
      if (!g_CurrentParams.windowed_ && !g_bHasFocus) {
        break;
      }

      RECT rect;
      GetClientRect(wnd, &rect);

      int width = rect.right - rect.left;
      int height = rect.bottom - rect.top;
      if (g_CurrentParams.width_ != width ||
          g_CurrentParams.height_ != height) {
        g_CurrentParams.width_ = width;
        g_CurrentParams.height_ = height;
        g_bResolutionChanged = true;
      }
      break;
    }
    case WM_COPYDATA: {
      PCOPYDATASTRUCT my_cds = (PCOPYDATASTRUCT)l_param;
      std::string command_line((char*)my_cds->lpData, my_cds->cbData);
      CommandLineActions::CommandLineArgs args;
      split(command_line, "|", args.argv_, false);
      CommandLineActions::ToProcess.push_back(args);
      break;
    }
  }

  CHECKPOINT_M(ssprintf("%p, %u, %08x, %08x", wnd, msg, w_param, l_param));

  if (m_bWideWindowClass) {
    return DefWindowProcW(wnd, msg, w_param, l_param);
  } else {
    return DefWindowProcA(wnd, msg, w_param, l_param);
  }
}

static void AdjustVideoModeParams(VideoModeParams& p) {
  DEVMODE dm;
  ZERO(dm);
  dm.dmSize = sizeof(dm);
  if (!EnumDisplaySettings(p.display_id_.c_str(), ENUM_CURRENT_SETTINGS, &dm)) {
    p.rate_ = 60;
    LOG->Warn(
        "%s",
        werr_ssprintf(GetLastError(), "EnumDisplaySettings failed").c_str());
    return;
  }

  /* On a nForce 2 IGP on Windows 98, dm.dmDisplayFrequency sometimes
   * (but not always) is 0.
   *
   * MSDN: When you call the EnumDisplaySettings function, the
   * dmDisplayFrequency member may return with the value 0 or 1.
   * These values represent the display hardware's default refresh rate.
   * This default rate is typically set by switches on a display card or
   * computer motherboard, or by a configuration program that does not
   * use Win32 display functions such as ChangeDisplaySettings. */
  if (!(dm.dmFields & DM_DISPLAYFREQUENCY) || dm.dmDisplayFrequency == 0 ||
      dm.dmDisplayFrequency == 1) {
    p.rate_ = 60;
    LOG->Warn(
        "EnumDisplaySettings doesn't know what the refresh rate is. %d %d %d",
        dm.dmPelsWidth, dm.dmPelsHeight, dm.dmBitsPerPel);
  } else {
    p.rate_ = dm.dmDisplayFrequency;
  }
}

/* Set the display mode to the given size, bit depth and refresh.
 * The refresh setting may be ignored. */
std::string GraphicsWindow::SetScreenMode(const VideoModeParams& p) {
  if (p.windowed_) {
    // We're going windowed. If we were previously fullscreen, reset.
    ChangeDisplaySettingsEx(
        p.display_id_.c_str(), nullptr, nullptr, 0, nullptr);

    return std::string();
  }

  DEVMODE dev_mode;
  ZERO(dev_mode);
  dev_mode.dmSize = sizeof(DEVMODE);
  dev_mode.dmPelsWidth = p.width_;
  dev_mode.dmPelsHeight = p.height_;
  dev_mode.dmBitsPerPel = p.bpp_;
  dev_mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;

  if (p.rate_ != REFRESH_DEFAULT) {
    dev_mode.dmDisplayFrequency = p.rate_;
    dev_mode.dmFields |= DM_DISPLAYFREQUENCY;
  }
  ChangeDisplaySettingsEx(p.display_id_.c_str(), nullptr, nullptr, 0, nullptr);

  int ret = ChangeDisplaySettingsEx(
      p.display_id_.c_str(), &dev_mode, nullptr, CDS_FULLSCREEN, nullptr);
  if (ret != DISP_CHANGE_SUCCESSFUL &&
      (dev_mode.dmFields & DM_DISPLAYFREQUENCY)) {
    dev_mode.dmFields &= ~DM_DISPLAYFREQUENCY;
    ret = ChangeDisplaySettingsEx(
        p.display_id_.c_str(), &dev_mode, nullptr, CDS_FULLSCREEN, nullptr);
  }

  // XXX: append error
  if (ret != DISP_CHANGE_SUCCESSFUL) {
    return "Couldn't set screen mode";
  }

  g_FullScreenDevMode = dev_mode;
  return std::string();
}

static int GetWindowStyle(bool windowed, bool window_is_fullscreen_borderless) {
  if (windowed && !window_is_fullscreen_borderless) {
    return WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
  } else {
    return WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
  }
}

/* Set the final window size, set the window text and icon, and then unhide the
 * window. */
void GraphicsWindow::CreateGraphicsWindow(
    const VideoModeParams& p, bool force_recreate_window) {
  auto reset_device_mode = [=](DEVMODE& mode) {
    ZeroMemory(&mode, sizeof(DEVMODE));
    mode.dmSize = sizeof(DEVMODE);
    mode.dmDriverExtra = 0;
  };

  auto device_mode_is_valid = [=](const DEVMODE& mode) {
    return (mode.dmFields & DM_PELSWIDTH) && (mode.dmFields & DM_PELSHEIGHT) &&
           (mode.dmFields & DM_DISPLAYFREQUENCY) &&
           (mode.dmBitsPerPel >= 32 || !(mode.dmFields & DM_BITSPERPEL));
  };

  g_CurrentParams = p;

  // Adjust g_CurrentParams to reflect the actual display settings.
  AdjustVideoModeParams(g_CurrentParams);

  DEVMODE devmode;
  reset_device_mode(devmode);

  // Top left corner of monitor
  POINTL pos;
  // Set default position to (0,0), which is the primary display device as a
  // fallback
  pos.x = 0;
  pos.y = 0;

  // Look for the preferred display's position.
  if (EnumDisplaySettingsEx(
          p.display_id_.c_str(), ENUM_CURRENT_SETTINGS, &devmode, 0) &&
      device_mode_is_valid(devmode) && (devmode.dmFields & DM_POSITION)) {
    pos = devmode.dmPosition;
    reset_device_mode(devmode);
  }

  if (g_hWndMain == nullptr || force_recreate_window) {
    int window_style =
        GetWindowStyle(p.windowed_, p.window_is_fullscreen_borderless_);

    AppInstance inst;
    HWND wnd = CreateWindow(
        g_sClassName.c_str(), "app", window_style, 0, 0, 0, 0, nullptr, nullptr,
        inst, nullptr);
    if (wnd == nullptr) {
      RageException::Throw(
          "%s", werr_ssprintf(GetLastError(), "CreateWindow").c_str());
    }

    /* If an old window exists, transfer focus to the new window before
     * deleting it, or some other window may temporarily get focus, which
     * can cause it to be resized. */
    if (g_hWndMain != nullptr) {
      // While we change to the new window, don't do ChangeDisplaySettings in
      // WM_ACTIVATE.
      g_bRecreatingVideoMode = true;
      SetForegroundWindow(wnd);
      g_bRecreatingVideoMode = false;

      GraphicsWindow::DestroyGraphicsWindow();
    }

    g_hWndMain = wnd;
    CrashHandler::SetForegroundWindow(g_hWndMain);
    g_HDC = GetDC(g_hWndMain);
  }

  // Update the window title.
  do {
    if (m_bWideWindowClass) {
      if (SetWindowText(
              g_hWndMain, ConvertUTF8ToACP(p.window_title_).c_str())) {
        break;
      }
    }

    SetWindowTextA(g_hWndMain, ConvertUTF8ToACP(p.window_title_).c_str());
  } while (0);

  // Update the window icon.
  if (g_hIcon != nullptr) {
    SetClassLongPtrA(
        g_hWndMain, GCLP_HICON,
        reinterpret_cast<LONG_PTR>(LoadIcon(nullptr, IDI_APPLICATION)));
    DestroyIcon(g_hIcon);
    g_hIcon = nullptr;
  }
  g_hIcon = IconFromFile(p.icon_file_);
  if (g_hIcon != nullptr) {
    SetClassLongPtrA(
        g_hWndMain, GCLP_HICON, reinterpret_cast<LONG_PTR>(g_hIcon));
  }

  /* The window style may change as a result of switching to or from fullscreen;
   * apply it. Don't change the WS_VISIBLE bit. */
  int window_style =
      GetWindowStyle(p.windowed_, p.window_is_fullscreen_borderless_);
  if (GetWindowLong(g_hWndMain, GWL_STYLE) & WS_VISIBLE) {
    window_style |= WS_VISIBLE;
  }
  SetWindowLong(g_hWndMain, GWL_STYLE, window_style);

  // Set rectangle for window based on the preferred display and resolution
  RECT window_rect;
  SetRect(&window_rect, pos.x, pos.y, pos.x + p.width_, pos.y + p.height_);
  AdjustWindowRect(&window_rect, window_style, FALSE);

  // LOG->Warn( "w = %d, h = %d", p.width_, p.height_ );

  const int width = window_rect.right - window_rect.left;
  const int height = window_rect.bottom - window_rect.top;

  int x = pos.x, y = pos.y;
  /* If windowed and not fullscreen borderless, center the window on the primary
   * display. Otherwise the window will be centered on the last display option
   * that was hovered before selecting Windowed in Options*/
  if (p.windowed_ && !p.window_is_fullscreen_borderless_) {
    if (EnumDisplaySettingsEx(NULL, ENUM_CURRENT_SETTINGS, &devmode, 0) &&
        device_mode_is_valid(devmode) && (devmode.dmFields & DM_POSITION)) {
      window_rect.left = devmode.dmPosition.x;
      window_rect.top = devmode.dmPosition.y;
      window_rect.right = devmode.dmPosition.x + devmode.dmPelsWidth;
      window_rect.bottom = devmode.dmPosition.y + devmode.dmPelsHeight;
    }

    x = window_rect.left + (window_rect.right - window_rect.left - width) / 2;
    y = window_rect.top + (window_rect.bottom - window_rect.top - height) / 2;
  }

  reset_device_mode(devmode);

  /* Move and resize the window. SWP_FRAMECHANGED causes the above
   * SetWindowLong to take effect. */
  if (!SetWindowPos(
          g_hWndMain, HWND_NOTOPMOST, x, y, width, height,
          SWP_FRAMECHANGED | SWP_SHOWWINDOW)) {
    LOG->Warn("%s", werr_ssprintf(GetLastError(), "SetWindowPos").c_str());
  }

  SetForegroundWindow(g_hWndMain);

  /* Pump messages quickly, to make sure the window is completely set up.
   * If we don't do this, then starting up in a D3D fullscreen window may
   * cause all other windows on the system to be resized. */
  MSG msg;
  while (PeekMessage(&msg, nullptr, 0, 0, PM_NOREMOVE)) {
    GetMessage(&msg, nullptr, 0, 0);
    DispatchMessage(&msg);
  }
}

/** @brief Shut down the window, but don't reset the video mode. */
void GraphicsWindow::DestroyGraphicsWindow() {
  if (g_HDC != nullptr) {
    ReleaseDC(g_hWndMain, g_HDC);
    g_HDC = nullptr;
  }

  CHECKPOINT;

  if (g_hWndMain != nullptr) {
    DestroyWindow(g_hWndMain);
    g_hWndMain = nullptr;
    CrashHandler::SetForegroundWindow(g_hWndMain);
  }

  CHECKPOINT;

  if (g_hIcon != nullptr) {
    DestroyIcon(g_hIcon);
    g_hIcon = nullptr;
  }

  CHECKPOINT;

  MSG msg;
  while (PeekMessage(&msg, nullptr, 0, 0, PM_NOREMOVE)) {
    CHECKPOINT;
    GetMessage(&msg, nullptr, 0, 0);
    CHECKPOINT;
    DispatchMessage(&msg);
  }

  CHECKPOINT;
}

void GraphicsWindow::Initialize(bool d3d) {
  // A few things need to be handled differently for D3D.
  g_bD3D = d3d;

  AppInstance inst;
  do {
    const std::wstring class_name = RStringToWstring(g_sClassName);
    WNDCLASSW window_class_w = {
        CS_OWNDC | CS_BYTEALIGNCLIENT,
        GraphicsWindow_WndProc,
        0,                              /* cbClsExtra */
        0,                              /* cbWndExtra */
        inst,                           /* hInstance */
        nullptr,                        /* set icon later */
        LoadCursor(nullptr, IDC_ARROW), /* default cursor */
        nullptr,                        /* hbrBackground */
        nullptr,                        /* lpszMenuName */
        class_name.c_str()              /* lpszClassName */
    };

    m_bWideWindowClass = true;
    if (RegisterClassW(&window_class_w)) {
      break;
    }

    WNDCLASS window_class_a = {
        CS_OWNDC | CS_BYTEALIGNCLIENT,
        GraphicsWindow_WndProc,
        0,                              /* cbClsExtra */
        0,                              /* cbWndExtra */
        inst,                           /* hInstance */
        nullptr,                        /* set icon later */
        LoadCursor(nullptr, IDC_ARROW), /* default cursor */
        nullptr,                        /* hbrBackground */
        nullptr,                        /* lpszMenuName */
        g_sClassName.c_str()            /* lpszClassName */
    };

    m_bWideWindowClass = false;
    if (!RegisterClassA(&window_class_a)) {
      RageException::Throw(
          "%s", werr_ssprintf(GetLastError(), "RegisterClass").c_str());
    }
  } while (0);

  g_iQueryCancelAutoPlayMessage = RegisterWindowMessage("QueryCancelAutoPlay");
}

void GraphicsWindow::Shutdown() {
  DestroyGraphicsWindow();

  /* Return to the desktop resolution, if needed.
   * It'd be nice to not do this: Windows will do it when we quit, and if
   * we're shutting down OpenGL to try D3D, this will cause extra mode
   * switches. However, we need to do this before displaying dialogs. */
  ChangeDisplaySettingsEx(
      g_CurrentParams.display_id_.c_str(), nullptr, nullptr, 0, nullptr);

  AppInstance inst;
  UnregisterClass(g_sClassName.c_str(), inst);
}

HDC GraphicsWindow::GetHDC() {
  ASSERT(g_HDC != nullptr);
  return g_HDC;
}

const VideoModeParams& GraphicsWindow::GetParams() { return g_CurrentParams; }

void GraphicsWindow::Update() {
  MSG msg;
  while (PeekMessage(&msg, nullptr, 0, 0, PM_NOREMOVE)) {
    GetMessage(&msg, nullptr, 0, 0);
    DispatchMessage(&msg);
  }

  HOOKS->SetHasFocus(g_bHasFocus);

  if (g_bResolutionChanged && DISPLAY != nullptr) {
    // LOG->Warn( "Changing resolution" );

    /* Let DISPLAY know that our resolution has changed. (Note that
     * ResolutionChanged() can come back here, so reset g_bResolutionChanged
     * first.) */
    g_bResolutionChanged = false;
    DISPLAY->ResolutionChanged();
  }
}

HWND GraphicsWindow::GetHwnd() { return g_hWndMain; }

void GraphicsWindow::GetDisplaySpecs(DisplaySpecs& out) {
  auto reset_device_mode = [=](DEVMODE& mode) {
    ZeroMemory(&mode, sizeof(DEVMODE));
    mode.dmSize = sizeof(DEVMODE);
    mode.dmDriverExtra = 0;
  };

  auto device_mode_is_valid = [=](const DEVMODE& mode) {
    return (mode.dmFields & DM_PELSWIDTH) && (mode.dmFields & DM_PELSHEIGHT) &&
           (mode.dmFields & DM_DISPLAYFREQUENCY) &&
           (mode.dmBitsPerPel >= 32 || !(mode.dmFields & DM_BITSPERPEL));
  };

  DEVMODE devmode;
  reset_device_mode(devmode);

  DWORD device_iter = 0;
  DISPLAY_DEVICE dd;
  ZeroMemory(&dd, sizeof(dd));
  dd.cb = sizeof(dd);

  // Loop through all displays and their display settings to create a
  // DisplaySpec for each display.
  while (EnumDisplayDevices(NULL, device_iter++, &dd, 0)) {
    int mode = 0;
    std::set<DisplayMode> disp_modes;

    while (EnumDisplaySettingsEx(dd.DeviceName, mode++, &devmode, 0)) {
      if (device_mode_is_valid(devmode)) {
        DisplayMode m = {
            devmode.dmPelsWidth, devmode.dmPelsHeight,
            static_cast<double>(devmode.dmDisplayFrequency)};
        disp_modes.insert(m);
      }
      reset_device_mode(devmode);
    }

    // Clean up the device name to store as the DisplaySpec ID
    std::string device_name;
    device_name = dd.DeviceName;
    TrimRight(device_name);

    // Make the DisplaySpec Name more friendly by removing the "\\.\" prefix on
    // most Windows display devices
    std::string mode_name;
    mode_name = device_name;
    TrimLeft(mode_name, "\\");
    TrimLeft(mode_name, ".");
    TrimLeft(mode_name, "\\");

    // Get the current display mode
    // Set the ENUM_CURRENT_SETTINGS flag so that we only store DisplaySpecs for
    // valid displays
    if (EnumDisplaySettingsEx(
            dd.DeviceName, ENUM_CURRENT_SETTINGS, &devmode, 0) &&
        device_mode_is_valid(devmode) && (devmode.dmFields & DM_POSITION)) {
      DisplayMode m = {
          devmode.dmPelsWidth, devmode.dmPelsHeight,
          static_cast<double>(devmode.dmDisplayFrequency)};
      RectI bounds = {
          devmode.dmPosition.x, devmode.dmPosition.y,
          static_cast<int>(m.width_), static_cast<int>(m.height_)};
      out.insert(DisplaySpec(device_name, mode_name, disp_modes, m, bounds));
    } else if (!disp_modes.empty()) {
      LOG->Warn("Could not retrieve valid current display mode");
      out.insert(DisplaySpec(device_name, mode_name, *disp_modes.begin()));
    }
  }
  if (out.size() == 0) {
    LOG->Warn("Could not retrieve *any* DisplaySpec's!");
  }
  ZeroMemory(&dd, sizeof(dd));
  dd.cb = sizeof(dd);
  reset_device_mode(devmode);
}

/*
 * (c) 2004 Glenn Maynard
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
