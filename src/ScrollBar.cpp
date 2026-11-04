#include "ScrollBar.h"

#include <cmath>
#include <string>

#include "Actor.h"
#include "RageThreads.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "ThemeManager.h"
#include "global.h"

ScrollBar::ScrollBar() {
  std::string sMetricsGroup = "ScrollBar";

  spr_middle_.Load(THEME->GetPathG(sMetricsGroup, "middle"));
  this->AddChild(spr_middle_);

  spr_top_.Load(THEME->GetPathG(sMetricsGroup, "top"));
  spr_top_->SetVertAlign(VertAlign_Bottom);
  this->AddChild(spr_top_);

  spr_bottom_.Load(THEME->GetPathG(sMetricsGroup, "bottom"));
  spr_bottom_->SetVertAlign(VertAlign_Top);
  this->AddChild(spr_bottom_);

  spr_scroll_tick_thumb_.Load(THEME->GetPathG(sMetricsGroup, "TickThumb"));
  this->AddChild(spr_scroll_tick_thumb_);

  for (unsigned i = 0; i < ARRAYLEN(spr_scroll_stretch_thumb_); i++) {
    spr_scroll_stretch_thumb_[i].Load(
        THEME->GetPathG(sMetricsGroup, "StretchThumb"));
    this->AddChild(spr_scroll_stretch_thumb_[i]);
  }

  SetBarHeight(100);
}

void ScrollBar::SetBarHeight(int iHeight) {
  bar_height_ = iHeight;
  spr_middle_->SetZoomY(bar_height_ / spr_middle_->GetUnzoomedHeight());
  spr_top_->SetY(-bar_height_ / 2.0f);
  spr_bottom_->SetY(+bar_height_ / 2.0f);
  spr_scroll_tick_thumb_->SetY(0);
  for (unsigned i = 0; i < ARRAYLEN(spr_scroll_stretch_thumb_); i++) {
    spr_scroll_stretch_thumb_[i]->SetY(0);
  }
}

void ScrollBar::SetPercentage(float fCenterPercent, float fSizePercent) {
  wrap(fCenterPercent, 1.0f);

  const int iBarContentHeight =
      static_cast<int>(spr_middle_->GetZoomedHeight());
  ASSERT(iBarContentHeight != 0);

  /* Set tick thumb */
  {
    float fY = SCALE(
        fCenterPercent, 0.0f, 1.0f, -iBarContentHeight / 2.0f,
        iBarContentHeight / 2.0f);
    fY = std::round(fY);
    spr_scroll_tick_thumb_->SetY(fY);
  }

  /* Set stretch thumb */
  float fStartPercent = fCenterPercent - fSizePercent;
  float fEndPercent = fCenterPercent + fSizePercent;

  // make sure the percent numbers are between 0 and 1
  fStartPercent = std::fmod(fStartPercent + 1, 1);
  fEndPercent = std::fmod(fEndPercent + 1, 1);

  CHECKPOINT_M("Percentages set.");
  float fPartTopY[2], fPartBottomY[2];

  if (fStartPercent < fEndPercent)  // we only need to one 1 stretch thumb part
  {
    fPartTopY[0] = SCALE(
        fStartPercent, 0.0f, 1.0f, -iBarContentHeight / 2.0f,
        +iBarContentHeight / 2.0f);
    fPartBottomY[0] = SCALE(
        fEndPercent, 0.0f, 1.0f, -iBarContentHeight / 2.0f,
        +iBarContentHeight / 2.0f);
    fPartTopY[1] = 0;
    fPartBottomY[1] = 0;
  } else  // we need two stretch thumb parts
  {
    fPartTopY[0] = SCALE(
        0.0f, 0.0f, 1.0f, -iBarContentHeight / 2.0f, +iBarContentHeight / 2.0f);
    fPartBottomY[0] = SCALE(
        fEndPercent, 0.0f, 1.0f, -iBarContentHeight / 2.0f,
        +iBarContentHeight / 2.0f);
    fPartTopY[1] = SCALE(
        fStartPercent, 0.0f, 1.0f, -iBarContentHeight / 2.0f,
        +iBarContentHeight / 2.0f);
    fPartBottomY[1] = SCALE(
        1.0f, 0.0f, 1.0f, -iBarContentHeight / 2.0f, +iBarContentHeight / 2.0f);
  }

  for (unsigned i = 0; i < ARRAYLEN(spr_scroll_stretch_thumb_); i++) {
    RectF rect(
        -spr_scroll_stretch_thumb_[i]->GetUnzoomedWidth() / 2, fPartTopY[i],
        +spr_scroll_stretch_thumb_[i]->GetUnzoomedWidth() / 2, fPartBottomY[i]);
    spr_scroll_stretch_thumb_[i]->StretchTo(rect);
  }
}

/*
 * (c) 2001-2003 Chris Danford
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
