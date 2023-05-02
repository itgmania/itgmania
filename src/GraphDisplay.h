#ifndef GRAPH_DISPLAY_H
#define GRAPH_DISPLAY_H

#include <vector>

#include "ActorFrame.h"
#include "AutoActor.h"
#include "GraphDisplay.h"
#include "PlayerStageStats.h"
#include "StageStats.h"

// A graph of the player's life over the course of Gameplay, used on Evaluation.
class GraphDisplay : public ActorFrame {
 public:
  GraphDisplay();
  ~GraphDisplay();
  virtual GraphDisplay* Copy() const;

  void Load(RString metrics_group);
  void Set(
      const StageStats& stage_stats,
      const PlayerStageStats& player_stage_stats);

  // Lua
  virtual void PushSelf(lua_State* L);

 private:
  void UpdateVerts();

  std::vector<float> values_;

  RectF quad_vertices_;

  std::vector<Actor*> song_boundaries_;
  AutoActor barely_;
  AutoActor backing_;
  AutoActor song_boundary_;

  GraphLine* graph_line_;
  GraphBody* graph_body_;
};

#endif  // GRAPH_DISPLAY_H

/*
 * (c) 2003 Glenn Maynard
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
