#include "ScreenReloadSongs.h"

#include <string>

#include "BitmapText.h"
#include "RageTimer.h"
#include "Screen.h"
#include "ScreenDimensions.h"
#include "ScreenManager.h"
#include "ScreenMessage.h"
#include "SongManager.h"
#include "ThemeManager.h"
#include "arch/LoadingWindow/LoadingWindow.h"
#include "global.h"

static const int DrawFrameRate = 20;
class ScreenReloadSongsLoadingWindow : public LoadingWindow {
  RageTimer last_draw_;
  BitmapText& bitmap_text_;

 public:
  ScreenReloadSongsLoadingWindow(BitmapText& bt) : bitmap_text_(bt) {}

  void SetText(std::string str) {
    bitmap_text_.SetText(str);
    Paint();
  }

  void Paint() {
    /* We load songs much faster than we draw frames. Cap the draw rate,
     * so we don't slow down the reload. */
    if (last_draw_.Ago() < 1.0f / DrawFrameRate) {
      return;
    }
    last_draw_.GetDeltaTime();

    SCREENMAN->Draw();
  }
};

/* This could be cleaned up: show progress, for example.  Let's not use
 * this for the initial load, since we don't want to start up the display
 * until we finish loading songs; that way, people can continue to use their
 * computer while songs load. */
REGISTER_SCREEN_CLASS(ScreenReloadSongs);

ScreenReloadSongs::ScreenReloadSongs() {}

void ScreenReloadSongs::Init() {
  Screen::Init();

  updates_ = 0;

  loading_.SetName("LoadingText");
  loading_.LoadFromFont(THEME->GetPathF(name_, "LoadingText"));
  loading_.SetXY(SCREEN_CENTER_X, SCREEN_CENTER_Y);
  this->AddChild(&loading_);

  loading_window_ = new ScreenReloadSongsLoadingWindow(loading_);
}

ScreenReloadSongs::~ScreenReloadSongs() { delete loading_window_; }

void ScreenReloadSongs::Update(float fDeltaTime) {
  Screen::Update(fDeltaTime);

  /* Start the reload on the second update. On the first (0), SCREENMAN->Draw
   * won't draw. */
  ++updates_;
  if (updates_ != 2) {
    return;
  }

  ASSERT(!IsFirstUpdate());

  bool onlyLoadAdditions = THEME->GetMetricB(name_, "OnlyLoadAdditions");
  if (onlyLoadAdditions) {
    SONGMAN->LoadAdditions(loading_window_);
  } else {
    SONGMAN->Reload(false, loading_window_);
  }

  SCREENMAN->PostMessageToTopScreen(SM_GoToNextScreen, 0);
}

/*
 * (c) 2003-2004 Glenn Maynard
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
