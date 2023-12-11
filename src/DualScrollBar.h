#ifndef DUAL_SCROLLBAR_H
#define DUAL_SCROLLBAR_H

#include "ActorFrame.h"
#include "AutoActor.h"
#include "PlayerNumber.h"

// A scrollbar with two independent thumbs.
class DualScrollBar : public ActorFrame {
 public:
  DualScrollBar();

  void Load(const RString& type);
  void SetBarHeight(float height) { bar_height_ = height; }
  void SetBarTime(float time) { bar_time_ = time; }
  void SetPercentage(PlayerNumber pn, float percent);
  void EnablePlayer(PlayerNumber pn, bool on);

 private:
  // The height of the scrollbar.
  float bar_height_;
  float bar_time_;

  AutoActor scroll_thumb_over_half_[NUM_PLAYERS];
  AutoActor scroll_thumb_under_half_[NUM_PLAYERS];
};

#endif  // DUAL_SCROLLBAR_H

/**
 * @file
 * @author Glenn Maynard, Chris Danford (c) 2001-2004
 * @section LICENSE
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
