/* RageTexture - Abstract class for a texture and metadata.  */

#ifndef RAGE_TEXTURE_H
#define RAGE_TEXTURE_H

#include <cstdint>
#include <string>
#include <vector>

#include "RageTextureID.h"
#include "RageTypes.h"

struct lua_State;
class RageTexture {
 public:
  RageTexture(RageTextureID file);
  virtual ~RageTexture() = 0;
  virtual void Update(float /* fDeltaTime */) {}
  virtual void Reload() {}
  virtual void Invalidate() {
  } /* only called by RageTextureManager::InvalidateTextures */
  virtual uintptr_t GetTexHandle() const = 0;  // accessed by RageDisplay

  // movie texture/animated texture stuff
  virtual void SetPosition(float /* fSeconds */) {}  // seek
  virtual void UpdateMovie(float /* fSeconds */) {}  // decode and update
  virtual void SetPlaybackRate(float) {}
  virtual bool IsAMovie() const { return false; }
  virtual void SetLooping(bool) {}

  int GetSourceWidth() const { return source_width_; }
  int GetSourceHeight() const { return source_height_; }
  int GetTextureWidth() const { return texture_width_; }
  int GetTextureHeight() const { return texture_height_; }
  int GetImageWidth() const { return image_width_; }
  int GetImageHeight() const { return image_height_; }

  int GetFramesWide() const { return frames_wide_; }
  int GetFramesHigh() const { return frames_high_; }

  int GetSourceFrameWidth() const { return GetSourceWidth() / GetFramesWide(); }
  int GetSourceFrameHeight() const {
    return GetSourceHeight() / GetFramesHigh();
  }

  // Use these to convert between the different coordinate systems:
  float GetSourceToImageCoordsRatioX() const {
    return float(GetImageWidth()) / GetSourceWidth();
  }
  float GetImageToTexCoordsRatioX() const { return 1.0f / GetTextureWidth(); }
  float GetSourceToTexCoordsRatioX() const {
    return GetSourceToImageCoordsRatioX() * GetImageToTexCoordsRatioX();
  }
  float GetSourceToImageCoordsRatioY() const {
    return float(GetImageHeight()) / GetSourceHeight();
  }
  float GetImageToTexCoordsRatioY() const { return 1.0f / GetTextureHeight(); }
  float GetSourceToTexCoordsRatioY() const {
    return GetSourceToImageCoordsRatioY() * GetImageToTexCoordsRatioY();
  }

  const RectF* GetTextureCoordRect(int frame_no) const;
  int GetNumFrames() const { return frames_wide_ * frames_high_; }

  // Used by RageTextureManager. Order is important; see RageTextureManager.cpp.
  const RageTextureID::TexPolicy& GetPolicy() const { return id_.policy_; }
  RageTextureID::TexPolicy& GetPolicy() { return id_.policy_; }
  int ref_count_;
  bool was_used_;

  // The ID that we were asked to load:
  const RageTextureID& GetID() const { return id_; }

  static void GetFrameDimensionsFromFileName(
      std::string path, int* frames_wide, int* frames_high,
      int source_width = 0, int source_height = 0);

  // Lua
  virtual void PushSelf(lua_State* L);

 private:
  /* We might change settings when loading (due to hints, hardware
   * limitations, etc). The data actually loaded is here: */
  RageTextureID id_;

 protected:
  int source_width_,
      source_height_;  // dimensions of the original image loaded from disk
  int texture_width_, texture_height_;  // dimensions of the texture in memory
  int image_width_, image_height_;  // dimensions of the image in the texture
  int frames_wide_, frames_high_;   // The number of frames of animation in
                                    // each row and column of this texture
  std::vector<RectF>
      texture_coord_rects_;  // size = m_iFramesWide * m_iFramesHigh

  virtual void CreateFrameRects();
};

#endif

/*
 * Copyright (c) 2001-2004 Chris Danford
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
