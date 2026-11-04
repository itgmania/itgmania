#ifndef OPTIONS_CURSOR_H
#define OPTIONS_CURSOR_H

#include <string>

#include "ActorFrame.h"
#include "AutoActor.h"
#include "Tween.h"
/** @brief A cursor for ScreenOptions. */
class OptionsCursor : public ActorFrame {
 public:
  /** @brief Set up a default OptionsCursor. */
  OptionsCursor();
  /**
   * @brief Set up an OptionsCursor based on an existing copy.
   * @param cpy the OptionsCursor we are copying. */
  OptionsCursor(const OptionsCursor& cpy);

  void Load(const std::string& sMetricsGroup, bool bLoadCanGos);

  void StopTweening() override;
  void BeginTweening(float fSecs, TweenType tt = TWEEN_LINEAR) override;
  void SetBarWidth(int iWidth);
  int GetBarWidth() const;
  void SetCanGo(bool bCanGoLeft, bool bCanGoRight);

 protected:
  AutoActor spr_middle_;
  AutoActor spr_left_;
  AutoActor spr_right_;

  AutoActor spr_can_go_left_;
  AutoActor spr_can_go_right_;

  // save the metrics-set X because it gets obliterated on a call to SetBarWidth
  int original_left_x_;
  int original_right_x_;
  int original_can_go_left_x_;
  int original_can_go_right_x_;
};

#endif

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
