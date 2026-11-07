#include "OptionsCursor.h"

#include <cmath>
#include <string>

#include "ActorFrame.h"
#include "ActorUtil.h"
#include "RageTypes.h"
#include "ThemeManager.h"
#include "Tween.h"

OptionsCursor::OptionsCursor() {
  original_left_x_ = 0;
  original_right_x_ = 0;
  original_can_go_left_x_ = 0;
  original_can_go_right_x_ = 0;
}

OptionsCursor::OptionsCursor(const OptionsCursor& cpy)
    : ActorFrame(cpy),
      spr_middle_(cpy.spr_middle_),
      spr_left_(cpy.spr_left_),
      spr_right_(cpy.spr_right_),
      spr_can_go_left_(cpy.spr_can_go_left_),
      spr_can_go_right_(cpy.spr_can_go_right_),
      original_left_x_(cpy.original_left_x_),
      original_right_x_(cpy.original_right_x_),
      original_can_go_left_x_(cpy.original_can_go_left_x_),
      original_can_go_right_x_(cpy.original_can_go_right_x_) {
  // Re-add children, or m_SubActors will point to cpy's children and not our
  // own.
  sub_actors_.clear();
  this->AddChild(spr_middle_);
  this->AddChild(spr_left_);
  this->AddChild(spr_right_);
  if (spr_can_go_left_) {
    this->AddChild(spr_can_go_left_);
  }
  if (spr_can_go_right_) {
    this->AddChild(spr_can_go_right_);
  }
}

void OptionsCursor::Load(const std::string& sMetricsGroup, bool load_can_gos) {
#define LOAD_SPR(spr, name)                                           \
  spr.Load(THEME->GetPathG(sMetricsGroup, name));                     \
  spr->SetName(name);                                                 \
  ActorUtil::LoadAllCommandsAndSetXYAndOnCommand(spr, sMetricsGroup); \
  this->AddChild(spr);

  LOAD_SPR(spr_middle_, "Middle");
  LOAD_SPR(spr_left_, "Left");
  LOAD_SPR(spr_right_, "Right");
  if (load_can_gos) {
    LOAD_SPR(spr_can_go_left_, "CanGoLeft");
    LOAD_SPR(spr_can_go_right_, "CanGoRight");
  }
#undef LOAD_SPR

  original_left_x_ = static_cast<int>(spr_left_->GetX());
  original_right_x_ = static_cast<int>(spr_right_->GetX());
  if (load_can_gos) {
    original_can_go_left_x_ = static_cast<int>(spr_can_go_left_->GetX());
    original_can_go_right_x_ = static_cast<int>(spr_can_go_right_->GetX());
  }

  SetCanGo(false, false);
}

void OptionsCursor::SetCanGo(bool can_go_left, bool can_go_right) {
  if (spr_can_go_left_) {
    spr_can_go_left_->EnableAnimation(can_go_left);
    spr_can_go_right_->EnableAnimation(can_go_right);

    spr_can_go_left_->SetDiffuse(
        can_go_left ? RageColor(1, 1, 1, 1) : RageColor(1, 1, 1, 0));
    spr_can_go_right_->SetDiffuse(
        can_go_right ? RageColor(1, 1, 1, 1) : RageColor(1, 1, 1, 0));
  }
}

void OptionsCursor::StopTweening() {
  ActorFrame::StopTweening();

  spr_middle_->StopTweening();
  spr_left_->StopTweening();
  spr_right_->StopTweening();

  if (spr_can_go_left_) {
    spr_can_go_left_->StopTweening();
    spr_can_go_right_->StopTweening();
  }
}

void OptionsCursor::BeginTweening(float secs, TweenType tt) {
  ActorFrame::BeginTweening(secs);

  spr_middle_->BeginTweening(secs);
  spr_left_->BeginTweening(secs);
  spr_right_->BeginTweening(secs);

  if (spr_can_go_left_) {
    spr_can_go_left_->BeginTweening(secs);
    spr_can_go_right_->BeginTweening(secs);
  }
}

void OptionsCursor::SetBarWidth(int i_width) {
  float f_width =
      std::ceil(i_width / 2.0f) * 2.0f;  // round up to nearest even number

  spr_middle_->ZoomToWidth(f_width);

  spr_left_->SetX(original_left_x_ - f_width / 2);
  spr_right_->SetX(original_right_x_ + f_width / 2);
  if (spr_can_go_left_) {
    spr_can_go_left_->SetX(original_can_go_left_x_ - f_width / 2);
    spr_can_go_right_->SetX(original_can_go_right_x_ + f_width / 2);
  }
}

int OptionsCursor::GetBarWidth() const {
  float width = spr_middle_->GetZoomX() * spr_middle_->GetUnzoomedWidth();
  return (int)width;
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
