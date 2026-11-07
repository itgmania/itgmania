#ifndef BGANIMATIONLAYER_H
#define BGANIMATIONLAYER_H

#include <string>
#include <vector>

#include "ActorFrame.h"
#include "RageTypes.h"

class XNode;

/** @brief Layer elements used by BGAnimation. */
class BGAnimationLayer : public ActorFrame {
 public:
  BGAnimationLayer();
  ~BGAnimationLayer();

  void LoadFromAniLayerFile(const std::string& path);
  void LoadFromNode(const XNode* node);

  void UpdateInternal(float delta_time);

 protected:
  std::vector<RageVector3> particle_velocity_;

  enum Type {
    TYPE_SPRITE,
    TYPE_PARTICLES,
    TYPE_TILES,
    NUM_TYPES,
  } type_;

  // stretch stuff
  float tex_coord_velocity_x_;
  float tex_coord_velocity_y_;

  // particles stuff
  bool particles_bounce_;

  // tiles stuff
  int num_tiles_wide_;
  int num_tiles_high_;
  float tiles_start_x_;
  float tiles_start_y_;
  float tiles_spacing_x_;
  float tiles_spacing_y_;
  float tile_velocity_x_;
  float tile_velocity_y_;
};

#endif

/**
 * @file
 * @author Ben Nordstrom, Chris Danford, Glenn Maynard (c) 2001-2004
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
