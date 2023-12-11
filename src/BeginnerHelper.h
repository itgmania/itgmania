#ifndef BEGINNER_HELPER_H
#define BEGINNER_HELPER_H

#include <array>

#include "ActorFrame.h"
#include "AutoActor.h"
#include "Character.h"
#include "Model.h"
#include "NoteData.h"
#include "PlayerNumber.h"
#include "Sprite.h"
#include "ThemeMetric.h"

// If A dancing character that follows the steps of the Song.
class BeginnerHelper : public ActorFrame {
 public:
  BeginnerHelper();
  ~BeginnerHelper();

  bool Init(int dance_pad_type);
  bool IsInitialized() { return initialized_; }
  static bool CanUse(PlayerNumber pn);
  void AddPlayer(PlayerNumber pn, const NoteData& node_data);
  void ShowStepCircle(PlayerNumber pn, int step);
  bool m_bShowBackground;

  void Update(float delta);
  virtual void DrawPrimitives();

 protected:
  void Step(PlayerNumber pn, int step);

  std::array<NoteData, NUM_PLAYERS> note_data_;
  std::array<bool, NUM_PLAYERS> players_enabled_;
  std::array<Model*, NUM_PLAYERS> dancers_;
  Model* dance_pad_;
  Sprite flash_;
  AutoActor background_;
  // More memory, but much easier to manage.
  std::array<std::array<Sprite, 4>, NUM_PLAYERS> step_circles_;

  int last_row_checked_;
  int last_row_flashed_;
  bool initialized_;

  ThemeMetric<bool> SHOW_DANCE_PAD;
};
#endif  // BEGINNER_HELPER_H

/**
 * @file
 * @author Kevin Slaughter, Tracy Ward (c) 2003
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
