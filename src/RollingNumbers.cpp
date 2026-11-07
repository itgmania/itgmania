#include "RollingNumbers.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "Actor.h"
#include "ActorUtil.h"
#include "BitmapText.h"
#include "LuaManager.h"
#include "RageTypes.h"
#include "RageUtil.h"

REGISTER_ACTOR_CLASS(RollingNumbers);

RollingNumbers::RollingNumbers() {
  current_number_ = 0;
  target_number_ = 0;
  score_velocity_ = 0;
  metrics_loaded_ = false;
}

void RollingNumbers::Load(const std::string& metrics_group) {
  metrics_loaded_ = true;
  TEXT_FORMAT.Load(metrics_group, "TextFormat");
  APPROACH_SECONDS.Load(metrics_group, "ApproachSeconds");
  COMMIFY.Load(metrics_group, "Commify");
  LEADING_ZERO_MULTIPLY_COLOR.Load(metrics_group, "LeadingZeroMultiplyColor");

  UpdateText();
}

void RollingNumbers::DrawPart(
    const RageColor* diffuse, const RageColor& stroke, float crop_left,
    float crop_right) {
  for (int i = 0; i < NUM_DIFFUSE_COLORS; ++i) {
    temp_state_->diffuse_[i] = diffuse[i];
  }
  SetCurrStrokeColor(stroke);
  temp_state_->crop_.left_ = crop_left;
  temp_state_->crop_.right_ = crop_right;
  BitmapText::DrawPrimitives();
}

void RollingNumbers::DrawPrimitives() noexcept {
  if (!metrics_loaded_) {
    return;
  }
  RageColor diffuse_orig[NUM_DIFFUSE_COLORS];
  RageColor diffuse_temp[NUM_DIFFUSE_COLORS];
  RageColor stroke_orig = GetCurrStrokeColor();
  RageColor stroke_temp = stroke_orig * LEADING_ZERO_MULTIPLY_COLOR;
  for (int i = 0; i < NUM_DIFFUSE_COLORS; ++i) {
    diffuse_orig[i] = temp_state_->diffuse_[i];
    diffuse_temp[i] = temp_state_->diffuse_[i] * LEADING_ZERO_MULTIPLY_COLOR;
  }
  float original_crop_left = temp_state_->crop_.left_;
  float original_crop_right = temp_state_->crop_.right_;

  std::string s = this->GetText();
  int i;
  // find the first non-zero non-comma character, or the last character
  for (i = 0; i < (int)(s.length() - 1); i++) {
    if (s[i] != '0' && s[i] != ',') {
      break;
    }
  }

  // Rewind to the first number, even if it's a zero.  If the string is "0000",
  // we want the last zero to show in the regular color.
  for (; i >= 0; i--) {
    if (s[i] >= '0' && s[i] <= '9') {
      break;
    }
  }
  float f = i / (float)s.length();

  // draw leading part
  DrawPart(
      diffuse_temp, stroke_temp, std::max((float)0, original_crop_left),
      std::max(1 - f, original_crop_right));
  // draw regular color part
  DrawPart(
      diffuse_orig, stroke_orig, std::max(f, original_crop_left),
      std::max(0.0f, original_crop_right));

  temp_state_->crop_.left_ = original_crop_left;
  temp_state_->crop_.right_ = original_crop_right;
}

void RollingNumbers::Update(float delta_time) {
  if (current_number_ != target_number_) {
    fapproach(
        current_number_, target_number_,
        std::abs(score_velocity_) * delta_time);
    UpdateText();
  }

  BitmapText::Update(delta_time);
}

void RollingNumbers::SetTargetNumber(float target_number) {
  if (!metrics_loaded_) {
    LuaHelpers::ReportScriptError(
        "You must use Load to load the metrics for a RollingNumbers actor "
        "before doing anything else.");
    return;
  }
  if (target_number == target_number_) {  // no change
    return;
  }
  target_number_ = target_number;
  float approach_secs = APPROACH_SECONDS.GetValue();
  if (approach_secs > 0) {
    score_velocity_ = (target_number_ - current_number_) / approach_secs;
  } else {
    score_velocity_ = (target_number_ - current_number_);
  }
}

void RollingNumbers::UpdateText() {
  if (!metrics_loaded_) {
    return;
  }
  std::string s = ssprintf(TEXT_FORMAT.GetValue().c_str(), current_number_);
  if (COMMIFY) {
    s = Commify(s);
  }
  SetText(s);
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the RollingNumbers. */
class LunaRollingNumbers : public Luna<RollingNumbers> {
 public:
  static int Load(T* p, lua_State* L) {
    p->Load(SArg(1));
    COMMON_RETURN_SELF;
  }
  static int targetnumber(T* p, lua_State* L) {
    p->SetTargetNumber(FArg(1));
    COMMON_RETURN_SELF;
  }

  LunaRollingNumbers() {
    ADD_METHOD(Load);
    ADD_METHOD(targetnumber);
  }
};

LUA_REGISTER_DERIVED_CLASS(RollingNumbers, BitmapText)

// lua end

/*
 * (c) 2001-2004 Chris Danford
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
