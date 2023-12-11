#ifndef GHOSTARROWROW_H
#define GHOSTARROWROW_H

#include <vector>

#include "ActorFrame.h"
#include "GameConstantsAndTypes.h"
#include "NoteDisplay.h"
#include "NoteTypes.h"
#include "PlayerState.h"

class PlayerState;
// Row of GhostArrow Actors.
class GhostArrowRow : public ActorFrame {
 public:
  virtual ~GhostArrowRow();
  virtual void Update(float delta);
  virtual void DrawPrimitives();

  void Load(const PlayerState* player_state, float y_reverse_offset);
  void SetColumnRenderers(std::vector<NoteColumnRenderer>& renderers);

  void DidTapNote(int col, TapNoteScore tns, bool bright);
  void DidHoldNote(int col, HoldNoteScore hns, bool bright);
  void SetHoldShowing(int col, const TapNote& tn);

 protected:
  float y_reverse_offset_pixels_;
  const PlayerState* player_state_;

  const std::vector<NoteColumnRenderer>* renderers_;
  std::vector<Actor*> ghost_;
  std::vector<TapNoteSubType> hold_showing_;
  std::vector<TapNoteSubType> last_hold_showing_;
};

#endif  // GHOSTARROWROW_H

/**
 * @file
 * @author Chris Danford (c) 2001-2004
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
