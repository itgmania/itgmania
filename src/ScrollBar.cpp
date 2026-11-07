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
  std::string metrics_group = "ScrollBar";

  spr_middle_.Load(THEME->GetPathG(metrics_group, "middle"));
  this->AddChild(spr_middle_);

  spr_top_.Load(THEME->GetPathG(metrics_group, "top"));
  spr_top_->SetVertAlign(VertAlign_Bottom);
  this->AddChild(spr_top_);

  spr_bottom_.Load(THEME->GetPathG(metrics_group, "bottom"));
  spr_bottom_->SetVertAlign(VertAlign_Top);
  this->AddChild(spr_bottom_);

  spr_scroll_tick_thumb_.Load(THEME->GetPathG(metrics_group, "TickThumb"));
  this->AddChild(spr_scroll_tick_thumb_);

  for (unsigned i = 0; i < ARRAYLEN(spr_scroll_stretch_thumb_); i++) {
    spr_scroll_stretch_thumb_[i].Load(
        THEME->GetPathG(metrics_group, "StretchThumb"));
    this->AddChild(spr_scroll_stretch_thumb_[i]);
  }

  SetBarHeight(100);
}

void ScrollBar::SetBarHeight(int height) {
  bar_height_ = height;
  spr_middle_->SetZoomY(bar_height_ / spr_middle_->GetUnzoomedHeight());
  spr_top_->SetY(-bar_height_ / 2.0f);
  spr_bottom_->SetY(+bar_height_ / 2.0f);
  spr_scroll_tick_thumb_->SetY(0);
  for (unsigned i = 0; i < ARRAYLEN(spr_scroll_stretch_thumb_); i++) {
    spr_scroll_stretch_thumb_[i]->SetY(0);
  }
}

void ScrollBar::SetPercentage(float center_percent, float size_percent) {
  wrap(center_percent, 1.0f);

  const int bar_content_height =
      static_cast<int>(spr_middle_->GetZoomedHeight());
  ASSERT(bar_content_height != 0);

  /* Set tick thumb */
  {
    float y = SCALE(
        center_percent, 0.0f, 1.0f, -bar_content_height / 2.0f,
        bar_content_height / 2.0f);
    y = std::round(y);
    spr_scroll_tick_thumb_->SetY(y);
  }

  /* Set stretch thumb */
  float start_percent = center_percent - size_percent;
  float end_percent = center_percent + size_percent;

  // make sure the percent numbers are between 0 and 1
  start_percent = std::fmod(start_percent + 1, 1);
  end_percent = std::fmod(end_percent + 1, 1);

  CHECKPOINT_M("Percentages set.");
  float part_top_y[2], part_bottom_y[2];

  if (start_percent < end_percent)  // we only need to one 1 stretch thumb part
  {
    part_top_y[0] = SCALE(
        start_percent, 0.0f, 1.0f, -bar_content_height / 2.0f,
        +bar_content_height / 2.0f);
    part_bottom_y[0] = SCALE(
        end_percent, 0.0f, 1.0f, -bar_content_height / 2.0f,
        +bar_content_height / 2.0f);
    part_top_y[1] = 0;
    part_bottom_y[1] = 0;
  } else  // we need two stretch thumb parts
  {
    part_top_y[0] = SCALE(
        0.0f, 0.0f, 1.0f, -bar_content_height / 2.0f,
        +bar_content_height / 2.0f);
    part_bottom_y[0] = SCALE(
        end_percent, 0.0f, 1.0f, -bar_content_height / 2.0f,
        +bar_content_height / 2.0f);
    part_top_y[1] = SCALE(
        start_percent, 0.0f, 1.0f, -bar_content_height / 2.0f,
        +bar_content_height / 2.0f);
    part_bottom_y[1] = SCALE(
        1.0f, 0.0f, 1.0f, -bar_content_height / 2.0f,
        +bar_content_height / 2.0f);
  }

  for (unsigned i = 0; i < ARRAYLEN(spr_scroll_stretch_thumb_); i++) {
    RectF rect(
        -spr_scroll_stretch_thumb_[i]->GetUnzoomedWidth() / 2, part_top_y[i],
        +spr_scroll_stretch_thumb_[i]->GetUnzoomedWidth() / 2,
        part_bottom_y[i]);
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
