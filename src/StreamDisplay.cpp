#include "StreamDisplay.h"

#include <cfloat>
#include <cmath>
#include <string>

#include "ActorFrame.h"
#include "EnumHelper.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "Sprite.h"
#include "ThemeManager.h"
#include "global.h"

static const char* StreamTypeNames[] = {
    "Normal",
    "Passing",
    "Hot",
};
XToString(StreamType);

StreamDisplay::StreamDisplay() {
  percent_ = 0;
  trailing_percent_ = 0;
  velocity_ = 0;
  passing_alpha_ = 0;
  hot_alpha_ = 0;
  always_bounce_ = false;

  delete_children_ = true;
}

void StreamDisplay::Load(
    const std::string& /* unreferenced: _sMetricsGroup  */) {
  // XXX: actually load from the metrics group passed in -aj
  std::string metrics_group = "StreamDisplay";

  transform_pill_.SetFromReference(
      THEME->GetMetricR(metrics_group, "PillTransformFunction"));
  VELOCITY_MULTIPLIER.Load(metrics_group, "VelocityMultiplier");
  VELOCITY_MIN.Load(metrics_group, "VelocityMin");
  VELOCITY_MAX.Load(metrics_group, "VelocityMax");
  SPRING_MULTIPLIER.Load(metrics_group, "SpringMultiplier");
  VISCOSITY_MULTIPLIER.Load(metrics_group, "ViscosityMultiplier");

  float texture_coord_scale_x =
      THEME->GetMetricF(metrics_group, "TextureCoordScaleX");
  int num_pills =
      static_cast<int>(THEME->GetMetricF(metrics_group, "NumPills"));
  always_bounce_ = THEME->GetMetricB(metrics_group, "AlwaysBounceNormalBar");

  FOREACH_ENUM(StreamType, st) {
    ASSERT(spr_pill_[st].empty());

    for (int i = 0; i < num_pills; i++) {
      Sprite* spr = new Sprite;

      spr->Load(THEME->GetPathG(metrics_group, StreamTypeToString(st)));
      spr_pill_[st].push_back(spr);

      transform_pill_.TransformItemDirect(*spr, -1, i, num_pills);
      float f = 1 / texture_coord_scale_x;
      spr->SetCustomTextureRect(RectF(f * i, 0, f * (i + 1), 1));

      this->AddChild(spr);
    }
  }
}

void StreamDisplay::Update(float delta_secs) {
  ActorFrame::Update(delta_secs);

  // HACK:  Tweaking these values is very difficult.  Update the
  // "physics" many times so that the spring motion appears faster
  for (int i = 0; i < 10; i++) {
    const float delta = percent_ - trailing_percent_;

    // Don't apply spring and viscous forces if we're full or empty.
    // Just move straight to either full or empty.
    if (percent_ <= 0 || percent_ >= 1) {
      if (std::abs(delta) < 0.00001f) {
        velocity_ = 0;  // prevent div/0
      } else {
        velocity_ = (delta / std::abs(delta)) * VELOCITY_MULTIPLIER;
      }
    } else {
      const float spring_force = delta * SPRING_MULTIPLIER;
      velocity_ += spring_force * delta_secs;

      const float viscous_force = -velocity_ * VISCOSITY_MULTIPLIER;
      if (!always_bounce_) {
        velocity_ += viscous_force * delta_secs;
      }
    }

    rage_clamp(velocity_, VELOCITY_MIN, VELOCITY_MAX);

    trailing_percent_ += velocity_ * delta_secs;
  }

  // Don't clamp life percentage a little outside the visible range so
  // that the clamp doesn't dampen the "jiggle" of the meter.
  rage_clamp(trailing_percent_, -0.1f, 1.1f);

  // set crop of pills
  const float pill_width_percent = 1.0f / spr_pill_[0].size();
  FOREACH_ENUM(StreamType, st) {
    for (int i = 0; i < (int)spr_pill_[st].size(); i++) {
      Sprite* spr = spr_pill_[st][i];
      float percent_filled_this_pill = SCALE(
          trailing_percent_, pill_width_percent * i,
          pill_width_percent * (i + 1), 0.0f, 1.0f);
      rage_clamp(percent_filled_this_pill, 0.0f, 1.0f);

      // XXX scale by current song speed

      spr->SetCropRight(1.0f - percent_filled_this_pill);
      spr->SetTexCoordVelocity(-1, 0);

      // Optimization: Don't draw pills that are covered up
      switch (st) {
        DEFAULT_FAIL(st);
        case StreamType_Normal:
          spr->SetVisible(passing_alpha_ < 1 || hot_alpha_ < 1);
          spr->SetDiffuseAlpha(1);
          break;
        case StreamType_Passing:
          spr->SetDiffuseAlpha(passing_alpha_);
          spr->SetVisible(hot_alpha_ < 1);
          break;
        case StreamType_Hot:
          spr->SetDiffuseAlpha(hot_alpha_);
          break;
      }
    }
  }
}

void StreamDisplay::SetPercent(float percent) {
#ifdef DEBUG
  float life_multiplier = THEME->GetMetricF("LifeMeterBar", "LifeMultiplier");
#endif
  DEBUG_ASSERT(percent >= 0.0f && percent <= 1.0f * life_multiplier);
  if (std::isnan(percent)) {
    DEBUG_ASSERT_M(0, "fPercent is NaN");
    percent = 1;
  }
  if (!std::isfinite(percent)) {
    DEBUG_ASSERT_M(0, "fPercent is infinite");
    percent = 1;
  }

  float delta_percent = percent - percent_;
  velocity_ += delta_percent;
  percent_ = percent;
}

/*
 * (c) 2003-2004 Chris Danford
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
