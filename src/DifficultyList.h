/* StepsDisplayList - Shows all available difficulties for a Song/Course. */
#ifndef DIFFICULTY_LIST_H
#define DIFFICULTY_LIST_H

#include <vector>

#include "Actor.h"
#include "ActorFrame.h"
#include "AutoActor.h"
#include "Difficulty.h"
#include "MessageManager.h"
#include "PlayerNumber.h"
#include "StepsDisplay.h"
#include "ThemeMetric.h"

class Song;
class Steps;

class StepsDisplayList : public ActorFrame {
 public:
  StepsDisplayList();
  virtual ~StepsDisplayList();
  virtual StepsDisplayList* Copy() const;
  virtual void LoadFromNode(const XNode* node);

  void HandleMessage(const Message& msg);

  void SetFromGameState();

  // Lua
  void PushSelf(lua_State* L);

 private:
  void UpdatePositions();
  void PositionItems();
  int GetCurrentRowIndex(PlayerNumber pn) const;

  ThemeMetric<float> ITEMS_SPACING_Y;
  ThemeMetric<int> NUM_SHOWN_ITEMS;
  ThemeMetric<apActorCommands> MOVE_COMMAND;

  AutoActor cursors_[kNumPlayers];
  ActorFrame cursor_frames_[kNumPlayers];  // contains Cursor so that color can
                                           // fade independent of other tweens

  struct Line {
    StepsDisplay meter_;
  };
  std::vector<Line> lines_;

  bool shown_;

  struct Row {
    Row() {
      steps_ = nullptr;
      dc_ = Difficulty_Invalid;
      y_ = 0;
      hidden_ = false;
    }

    const Steps* steps_;
    Difficulty dc_;
    float y_;
    bool hidden_;  // currently off screen
  };

  std::vector<Row> rows_;
};

#endif

/*
 * (c) 2003-2004 Glenn Maynard
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
