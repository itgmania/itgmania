#include "global.h"

#include "DualScrollBar.h"

#include "RageUtil.h"
#include "ThemeManager.h"

DualScrollBar::DualScrollBar() {
  bar_height_ = 100;
  bar_time_ = 1;
}

void DualScrollBar::Load(const RString& type) {
  FOREACH_PlayerNumber(pn) {
    scroll_thumb_under_half_[pn].Load(
        THEME->GetPathG(type, ssprintf("thumb p%i", pn + 1)));
    scroll_thumb_under_half_[pn]->SetName(ssprintf("ThumbP%i", pn + 1));
    this->AddChild(scroll_thumb_under_half_[pn]);
  }

  FOREACH_PlayerNumber(pn) {
    scroll_thumb_over_half_[pn].Load(
        THEME->GetPathG(type, ssprintf("thumb p%i", pn + 1)));
    scroll_thumb_over_half_[pn]->SetName(ssprintf("ThumbP%i", pn + 1));
    this->AddChild(scroll_thumb_over_half_[pn]);
  }

  scroll_thumb_under_half_[0]->SetCropLeft(.5f);
  scroll_thumb_under_half_[1]->SetCropRight(.5f);

  scroll_thumb_over_half_[0]->SetCropRight(.5f);
  scroll_thumb_over_half_[1]->SetCropLeft(.5f);

  FOREACH_PlayerNumber(pn) SetPercentage(pn, 0);

  FinishTweening();
}

void DualScrollBar::EnablePlayer(PlayerNumber pn, bool on) {
  scroll_thumb_under_half_[pn]->SetVisible(on);
  scroll_thumb_over_half_[pn]->SetVisible(on);
}

void DualScrollBar::SetPercentage(PlayerNumber pn, float percent) {
  const float bottom =
      bar_height_ / 2 - scroll_thumb_under_half_[pn]->GetZoomedHeight() / 2;
  const float top = -bottom;

  // Position both thumbs.
  scroll_thumb_under_half_[pn]->StopTweening();
  scroll_thumb_under_half_[pn]->BeginTweening(bar_time_);
  scroll_thumb_under_half_[pn]->SetY(SCALE(percent, 0, 1, top, bottom));

  scroll_thumb_over_half_[pn]->StopTweening();
  scroll_thumb_over_half_[pn]->BeginTweening(bar_time_);
  scroll_thumb_over_half_[pn]->SetY(SCALE(percent, 0, 1, top, bottom));
}

/*
 * (c) 2001-2004 Glenn Maynard, Chris Danford
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
