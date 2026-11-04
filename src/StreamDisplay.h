/* StreamDisplay -  */
#ifndef StreamDisplay_H
#define StreamDisplay_H

#include <string>
#include <vector>

#include "ActorFrame.h"
#include "LuaExpressionTransform.h"
#include "Sprite.h"
#include "ThemeMetric.h"

enum StreamType {
  StreamType_Normal,
  StreamType_Passing,
  StreamType_Hot,
  NUM_StreamType,
};

class StreamDisplay : public ActorFrame {
 public:
  StreamDisplay();

  virtual void Update(float fDeltaSecs);

  void Load(const std::string& sMetricsGroup);

  void SetPercent(float fPercent);
  void SetPassingAlpha(float fPassingAlpha) { passing_alpha_ = fPassingAlpha; }
  void SetHotAlpha(float fHotAlpha) { hot_alpha_ = fHotAlpha; }

  float GetPercent() { return percent_; }

 private:
  std::vector<Sprite*> spr_pill_[NUM_StreamType];

  LuaExpressionTransform
      transform_pill_;  // params: self,offsetFromCenter,itemIndex,numItems
  ThemeMetric<float> VELOCITY_MULTIPLIER;
  ThemeMetric<float> VELOCITY_MIN;
  ThemeMetric<float> VELOCITY_MAX;
  ThemeMetric<float> SPRING_MULTIPLIER;
  ThemeMetric<float> VISCOSITY_MULTIPLIER;

  float percent_;  // percent filled
  float
      trailing_percent_;  // this approaches m_fPercent, use this value to draw
  float velocity_;        // velocity of m_fTrailingPercent

  float passing_alpha_;
  float hot_alpha_;

  bool always_bounce_;
};

#endif

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
