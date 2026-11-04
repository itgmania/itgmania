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
  std::string sMetricsGroup = "StreamDisplay";

  transform_pill_.SetFromReference(
      THEME->GetMetricR(sMetricsGroup, "PillTransformFunction"));
  VELOCITY_MULTIPLIER.Load(sMetricsGroup, "VelocityMultiplier");
  VELOCITY_MIN.Load(sMetricsGroup, "VelocityMin");
  VELOCITY_MAX.Load(sMetricsGroup, "VelocityMax");
  SPRING_MULTIPLIER.Load(sMetricsGroup, "SpringMultiplier");
  VISCOSITY_MULTIPLIER.Load(sMetricsGroup, "ViscosityMultiplier");

  float fTextureCoordScaleX =
      THEME->GetMetricF(sMetricsGroup, "TextureCoordScaleX");
  int iNumPills =
      static_cast<int>(THEME->GetMetricF(sMetricsGroup, "NumPills"));
  always_bounce_ = THEME->GetMetricB(sMetricsGroup, "AlwaysBounceNormalBar");

  FOREACH_ENUM(StreamType, st) {
    ASSERT(spr_pill_[st].empty());

    for (int i = 0; i < iNumPills; i++) {
      Sprite* pSpr = new Sprite;

      pSpr->Load(THEME->GetPathG(sMetricsGroup, StreamTypeToString(st)));
      spr_pill_[st].push_back(pSpr);

      transform_pill_.TransformItemDirect(*pSpr, -1, i, iNumPills);
      float f = 1 / fTextureCoordScaleX;
      pSpr->SetCustomTextureRect(RectF(f * i, 0, f * (i + 1), 1));

      this->AddChild(pSpr);
    }
  }
}

void StreamDisplay::Update(float fDeltaSecs) {
  ActorFrame::Update(fDeltaSecs);

  // HACK:  Tweaking these values is very difficult.  Update the
  // "physics" many times so that the spring motion appears faster
  for (int i = 0; i < 10; i++) {
    const float fDelta = percent_ - trailing_percent_;

    // Don't apply spring and viscous forces if we're full or empty.
    // Just move straight to either full or empty.
    if (percent_ <= 0 || percent_ >= 1) {
      if (std::abs(fDelta) < 0.00001f) {
        velocity_ = 0;  // prevent div/0
      } else {
        velocity_ = (fDelta / std::abs(fDelta)) * VELOCITY_MULTIPLIER;
      }
    } else {
      const float fSpringForce = fDelta * SPRING_MULTIPLIER;
      velocity_ += fSpringForce * fDeltaSecs;

      const float fViscousForce = -velocity_ * VISCOSITY_MULTIPLIER;
      if (!always_bounce_) {
        velocity_ += fViscousForce * fDeltaSecs;
      }
    }

    rage_clamp(velocity_, VELOCITY_MIN, VELOCITY_MAX);

    trailing_percent_ += velocity_ * fDeltaSecs;
  }

  // Don't clamp life percentage a little outside the visible range so
  // that the clamp doesn't dampen the "jiggle" of the meter.
  rage_clamp(trailing_percent_, -0.1f, 1.1f);

  // set crop of pills
  const float fPillWidthPercent = 1.0f / spr_pill_[0].size();
  FOREACH_ENUM(StreamType, st) {
    for (int i = 0; i < (int)spr_pill_[st].size(); i++) {
      Sprite* pSpr = spr_pill_[st][i];
      float fPercentFilledThisPill = SCALE(
          trailing_percent_, fPillWidthPercent * i, fPillWidthPercent * (i + 1),
          0.0f, 1.0f);
      rage_clamp(fPercentFilledThisPill, 0.0f, 1.0f);

      // XXX scale by current song speed

      pSpr->SetCropRight(1.0f - fPercentFilledThisPill);
      pSpr->SetTexCoordVelocity(-1, 0);

      // Optimization: Don't draw pills that are covered up
      switch (st) {
        DEFAULT_FAIL(st);
        case StreamType_Normal:
          pSpr->SetVisible(passing_alpha_ < 1 || hot_alpha_ < 1);
          pSpr->SetDiffuseAlpha(1);
          break;
        case StreamType_Passing:
          pSpr->SetDiffuseAlpha(passing_alpha_);
          pSpr->SetVisible(hot_alpha_ < 1);
          break;
        case StreamType_Hot:
          pSpr->SetDiffuseAlpha(hot_alpha_);
          break;
      }
    }
  }
}

void StreamDisplay::SetPercent(float fPercent) {
#ifdef DEBUG
  float fLifeMultiplier = THEME->GetMetricF("LifeMeterBar", "LifeMultiplier");
#endif
  DEBUG_ASSERT(fPercent >= 0.0f && fPercent <= 1.0f * fLifeMultiplier);
  if (std::isnan(fPercent)) {
    DEBUG_ASSERT_M(0, "fPercent is NaN");
    fPercent = 1;
  }
  if (!std::isfinite(fPercent)) {
    DEBUG_ASSERT_M(0, "fPercent is infinite");
    fPercent = 1;
  }

  float fDeltaPercent = fPercent - percent_;
  velocity_ += fDeltaPercent;
  percent_ = fPercent;
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
