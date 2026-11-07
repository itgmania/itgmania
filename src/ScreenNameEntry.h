#ifndef SCREEN_NAME_ENTRY_H
#define SCREEN_NAME_ENTRY_H

#include <string>
#include <vector>

#include "Actor.h"
#include "BitmapText.h"
#include "InputQueue.h"
#include "PlayerNumber.h"
#include "RageSound.h"
#include "ReceptorArrowRow.h"
#include "ScreenMessage.h"
#include "ScreenWithMenuElements.h"

/** @brief Enter a name for a new high score using a gimmicky interface. */
class ScreenNameEntry : public ScreenWithMenuElements {
 public:
  ScreenNameEntry();
  virtual void Init();
  virtual void Update(float delta_time);
  virtual bool Input(const InputEventPlus& input);
  virtual void HandleScreenMessage(const ScreenMessage sm);

  virtual bool MenuStart(const InputEventPlus& input);

 private:
  class ScrollingText : public Actor {
   public:
    ScrollingText() : done_(true) {}
    inline void SetDone() { done_ = true; }
    void Init(const std::string& name, const std::vector<float>& xs);
    virtual bool EarlyAbortDraw() const { return done_; }
    virtual void DrawPrimitives();
    char GetClosestChar(float fake_beat) const;

   private:
    float GetClosestCharYOffset(float fake_beat) const;

    std::vector<float> xs_;
    bool done_;
    BitmapText stamp_;
  };

  enum { ABS_MAX_RANKING_NAME_LENGTH = 10 };
  bool AnyStillEntering() const;

  ReceptorArrowRow receptor_arrow_row_[NUM_PLAYERS];
  BitmapText text_selected_chars_[NUM_PLAYERS][ABS_MAX_RANKING_NAME_LENGTH];
  BitmapText text_category_[NUM_PLAYERS];
  RageSound sound_step_;

  float fake_beat_;
  std::string selected_name_[NUM_PLAYERS];
  bool still_entering_name_[NUM_PLAYERS];

  ScrollingText text_[NUM_PLAYERS];
  std::vector<int> col_to_string_index_[NUM_PLAYERS];
};

#endif

/*
 * (c) 2001-2006 Chris Danford, Steve Checkoway
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
