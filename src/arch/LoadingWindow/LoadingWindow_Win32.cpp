#include "LoadingWindow_Win32.h"

// clang-format off
#include <windows.h>
#include <commctrl.h>
// clang-format on

#include <cmath>
#include <string>
#include <vector>

#include "LocalizedString.h"
#include "ProductInfo.h"
#include "RageFileManager.h"
#include "RageLog.h"
#include "RageSurface.h"
#include "RageSurfaceUtils.h"
#include "RageSurfaceUtils_Zoom.h"
#include "RageSurface_Load.h"
#include "RageUtil.h"
#include "arch/ArchHooks/ArchHooks.h"
#include "archutils/win32/ErrorStrings.h"
#include "archutils/win32/WindowIcon.h"
#include "archutils/win32/WindowsResources.h"
#include "global.h"

static HBITMAP g_hBitmap = nullptr;

/* Load a RageSurface into a GDI surface. */
static HBITMAP LoadWin32Surface(const RageSurface* splash, HWND hwnd) {
  RageSurface* s = CreateSurface(
      splash->w_, splash->h_, 32, 0xFF000000, 0x00FF0000, 0x0000FF00, 0);
  RageSurfaceUtils::Blit(splash, s, -1, -1);

  /* Resize the splash image to fit the dialog.  Stretch to fit horizontally,
   * maintaining aspect ratio. */
  {
    RECT r;
    GetClientRect(hwnd, &r);

    int width = r.right;
    float ratio = (float)width / s->w_;
    int height = std::lrint(s->h_ * ratio);

    RageSurfaceUtils::Zoom(s, width, height);
  }

  HDC screen = GetDC(nullptr);
  ASSERT_M(screen != nullptr, werr_ssprintf(GetLastError(), "hScreen"));

  HBITMAP bitmap = CreateCompatibleBitmap(screen, s->w_, s->h_);
  ASSERT_M(
      bitmap != nullptr,
      werr_ssprintf(GetLastError(), "CreateCompatibleBitmap"));

  HDC bitmap_dc = CreateCompatibleDC(screen);
  SelectObject(bitmap_dc, bitmap);

  /* This is silly, but simple.  We only do this once, on a small image. */
  for (int y = 0; y < s->h_; ++y) {
    const unsigned char* line = ((unsigned char*)s->pixels_) + (y * s->pitch_);
    for (int x = 0; x < s->w_; ++x) {
      const unsigned char* data = line + (x * s->format_->bytes_per_pixel_);

      SetPixelV(bitmap_dc, x, y, RGB(data[3], data[2], data[1]));
    }
  }

  SelectObject(bitmap_dc, nullptr);
  DeleteObject(bitmap_dc);

  ReleaseDC(nullptr, screen);

  delete s;
  return bitmap;
}

static HBITMAP LoadWin32Surface(std::string file, HWND hwnd) {
  std::string error;
  RageSurface* surface = RageSurfaceUtils::LoadFile(file, error);
  if (surface == nullptr) {
    return nullptr;
  }

  HBITMAP ret = LoadWin32Surface(surface, hwnd);
  delete surface;
  return ret;
}

INT_PTR CALLBACK LoadingWindow_Win32::WndProc(
    HWND hwnd, UINT msg, WPARAM w_param, LPARAM l_param) {
  switch (msg) {
    case WM_INITDIALOG: {
      std::vector<std::string> vs;
      GetDirListing("Data/splash*.png", vs, false, true);
      if (!vs.empty()) {
        g_hBitmap = LoadWin32Surface(vs[0], hwnd);
      }
    }
      if (g_hBitmap == nullptr) {
        g_hBitmap = LoadWin32Surface("Data/splash.bmp", hwnd);
      }
      SendMessage(
          GetDlgItem(hwnd, IDC_SPLASH), STM_SETIMAGE, (WPARAM)IMAGE_BITMAP,
          (LPARAM)(HANDLE)g_hBitmap);
      SetWindowTextA(hwnd, PRODUCT_ID);
      break;

    case WM_DESTROY:
      DeleteObject(g_hBitmap);
      g_hBitmap = nullptr;
      break;
  }

  return FALSE;
}

void LoadingWindow_Win32::SetIcon(const RageSurface* icon) {
  if (icon_ != nullptr) {
    DestroyIcon(icon_);
  }

  icon_ = IconFromSurface(icon);
  if (icon_ != nullptr) {
    SetClassLongPtrA(hwnd_, GCLP_HICON, reinterpret_cast<LONG_PTR>(icon_));
  }
}

void LoadingWindow_Win32::SetSplash(const RageSurface* splash) {
  if (g_hBitmap != nullptr) {
    DeleteObject(g_hBitmap);
    g_hBitmap = nullptr;
  }

  g_hBitmap = LoadWin32Surface(splash, hwnd_);
  if (g_hBitmap != nullptr) {
    SendDlgItemMessage(
        hwnd_, IDC_SPLASH, STM_SETIMAGE, (WPARAM)IMAGE_BITMAP,
        (LPARAM)(HANDLE)g_hBitmap);
  }
}

LoadingWindow_Win32::LoadingWindow_Win32() {
  icon_ = nullptr;
  hwnd_ = CreateDialog(
      handle_.Get(), MAKEINTRESOURCE(IDD_LOADING_DIALOG), nullptr, WndProc);
  ASSERT(hwnd_ != nullptr);
  for (unsigned i = 0; i < 3; ++i) {
    text_[i] = "ABC"; /* always set on first call */
  }
  SetText("");
  Paint();
}

LoadingWindow_Win32::~LoadingWindow_Win32() {
  if (hwnd_) {
    DestroyWindow(hwnd_);
  }
  if (icon_ != nullptr) {
    DestroyIcon(icon_);
  }
}

void LoadingWindow_Win32::Paint() {
  SendMessage(hwnd_, WM_PAINT, 0, 0);

  /* Process all queued messages since the last paint.  This allows the window
   * to come back if it loses focus during load. */
  MSG msg;
  while (PeekMessage(&msg, hwnd_, 0, 0, PM_NOREMOVE)) {
    GetMessage(&msg, hwnd_, 0, 0);
    DispatchMessage(&msg);
  }
}

void LoadingWindow_Win32::SetText(std::string text) {
  std::vector<std::string> message_lines;
  split(text, "\n", message_lines, false);
  while (message_lines.size() < 3) {
    message_lines.push_back("");
  }

  const int msgid[] = {
      IDC_STATIC_MESSAGE1, IDC_STATIC_MESSAGE2, IDC_STATIC_MESSAGE3};
  for (unsigned i = 0; i < 3; ++i) {
    if (text_[i] == message_lines[i]) {
      continue;
    }
    text_[i] = message_lines[i];

    HWND hwnd_item = ::GetDlgItem(hwnd_, msgid[i]);
    ::SetWindowText(hwnd_item, ConvertUTF8ToACP(message_lines[i]).c_str());
  }
}

void LoadingWindow_Win32::SetProgress(const int progress) {
  progress_ = progress;
  HWND hwnd_item = ::GetDlgItem(hwnd_, IDC_PROGRESS);
  ::SendMessage(hwnd_item, PBM_SETPOS, progress, 0);
  Paint();
}

void LoadingWindow_Win32::SetTotalWork(const int total_work) {
  total_work_ = total_work;
  HWND hwnd_item = ::GetDlgItem(hwnd_, IDC_PROGRESS);
  SendMessage(hwnd_item, PBM_SETRANGE32, 0, total_work);
}

void LoadingWindow_Win32::SetIndeterminate(bool indeterminate) {
  HWND hwnd_item = ::GetDlgItem(hwnd_, IDC_PROGRESS);

  if (indeterminate) {
    SetWindowLong(
        hwnd_item, GWL_STYLE,
        PBS_MARQUEE | GetWindowLong(hwnd_item, GWL_STYLE));
    SendMessage(hwnd_item, PBM_SETMARQUEE, 1, 0);
  } else {
    SendMessage(hwnd_item, PBM_SETMARQUEE, 0, 0);
    SetWindowLong(
        hwnd_item, GWL_STYLE,
        (~PBS_MARQUEE) & GetWindowLong(hwnd_item, GWL_STYLE));
  }
}

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
