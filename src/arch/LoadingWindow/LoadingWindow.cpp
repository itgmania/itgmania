#include "LoadingWindow.h"

#include <string>
#include <vector>

#include "PrefsManager.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "StdString.h"
#include "arch/LoadingWindow/LoadingWindow_Null.h"
#include "arch/arch_default.h"
#include "global.h"

LoadingWindow::LoadingWindow() : progress_(0), total_work_(1) {}

LoadingWindow* LoadingWindow::Create() {
  if (!PREFSMAN->show_loading_window_) {
    return new LoadingWindow_Null;
  }
#if defined(UNIX) && !defined(HAVE_GTK)
  return new LoadingWindow_Null;
#endif
  // Don't load nullptr by default.
  const std::string drivers = "win32,macosx,gtk";
  std::vector<std::string> drivers_to_try;
  split(drivers, ",", drivers_to_try, true);

  ASSERT(drivers_to_try.size() != 0);

  std::string driver;
  LoadingWindow* ret = nullptr;

  for (unsigned i = 0; ret == nullptr && i < drivers_to_try.size(); ++i) {
    driver = drivers_to_try[i];

#ifdef USE_LOADING_WINDOW_MACOSX
    if (!CompareNoCase(drivers_to_try[i], "MacOSX")) {
      ret = new LoadingWindow_MacOSX;
    }
#endif
#ifdef USE_LOADING_WINDOW_GTK
    if (!CompareNoCase(drivers_to_try[i], "Gtk")) {
      ret = new LoadingWindow_Gtk;
    }
#endif
#ifdef USE_LOADING_WINDOW_WIN32
    if (!CompareNoCase(drivers_to_try[i], "Win32")) {
      ret = new LoadingWindow_Win32;
    }
#endif
    if (!CompareNoCase(drivers_to_try[i], "Null")) {
      ret = new LoadingWindow_Null;
    }

    if (ret == nullptr) {
      continue;
    }

    std::string error = ret->Init();
    if (error != "") {
      LOG->Info(
          "Couldn't load driver %s: %s", drivers_to_try[i].c_str(),
          error.c_str());
      RageUtil::SafeDelete(ret);
    }
  }

  if (ret) {
    LOG->Info("Loading window: %s", driver.c_str());

    ret->SetIndeterminate(true);
  }

  return ret;
}

/*
 * (c) 2002-2005 Glenn Maynard
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
