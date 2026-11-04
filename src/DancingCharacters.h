#ifndef DancingCharacters_H
#define DancingCharacters_H

#include <array>

#include "ActorFrame.h"
#include "PlayerNumber.h"
class Model;

/** @brief Characters that react to how the players are doing. */
class DancingCharacters : public ActorFrame {
 public:
  DancingCharacters();
  virtual ~DancingCharacters();

  void LoadNextSong();

  virtual void Update(float fDelta);
  virtual void DrawPrimitives();
  bool draw_danger_light_;

 protected:
  std::array<Model*, NUM_PLAYERS> character_;

  /** @brief How far away is the camera from the dancer? */
  float camera_distance_;
  float camera_pan_y_start_;
  float camera_pan_y_end_;
  float look_at_height_;
  float camera_height_start_;
  float camera_height_end_;
  float this_camera_start_beat_;
  float this_camera_end_beat_;
};

#endif

/**
 * @file
 * @author Chris Danford (c) 2003-2004
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
