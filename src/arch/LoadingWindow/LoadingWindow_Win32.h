/* LoadingWindow_Win32 - Loading window using a Windows dialog box. */

#ifndef LOADING_WINDOW_WIN32_H
#define LOADING_WINDOW_WIN32_H

#include <windows.h>

#include <string>

#include "LoadingWindow.h"
#include "archutils/Win32/AppInstance.h"

class LoadingWindow_Win32 : public LoadingWindow {
 public:
  LoadingWindow_Win32();
  ~LoadingWindow_Win32();

  void Paint();
  void SetText(std::string text);
  void SetIcon(const RageSurface* icon);
  void SetSplash(const RageSurface* splash);
  void SetProgress(const int progress);
  void SetTotalWork(const int total_work);
  void SetIndeterminate(bool indeterminate);

 private:
  AppInstance handle_;
  HWND hwnd_;
  std::string text_[3];
  HICON icon_;

  static INT_PTR CALLBACK
  WndProc(HWND hwnd, UINT msg, WPARAM w_param, LPARAM l_param);
};
#define USE_LOADING_WINDOW_WIN32

#endif

/*
 * (c) 2002-2004 Glenn Maynard
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
