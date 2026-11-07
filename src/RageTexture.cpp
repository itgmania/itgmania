#include "RageTexture.h"

#include <cstring>
#include <string>
#include <vector>

#include "LuaManager.h"
#include "RageTextureID.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "RageUtil/Regex.h"

RageTexture::RageTexture(RageTextureID name)
    : ref_count_(1),
      was_used_(false),
      id_(name),
      source_width_(0),
      source_height_(0),
      texture_width_(0),
      texture_height_(0),
      image_width_(0),
      image_height_(0),
      frames_wide_(1),
      frames_high_(1) {}

RageTexture::~RageTexture() {}

void RageTexture::CreateFrameRects() {
  GetFrameDimensionsFromFileName(
      GetID().filename_, &frames_wide_, &frames_high_, source_width_,
      source_height_);

  // Fill in the m_FrameRects with the bounds of each frame in the animation.
  texture_coord_rects_.clear();
  texture_coord_rects_.reserve(
      static_cast<size_t>(frames_wide_ * frames_high_));

  float frame_width =
      (image_width_ / static_cast<float>(texture_width_)) / frames_wide_;
  float frame_height =
      (image_height_ / static_cast<float>(texture_height_)) / frames_high_;

  for (int j = 0; j < frames_high_; j++)  // traverse along Y
  {
    float top = j * frame_height;
    float bottom = (j + 1) * frame_height;

    for (int i = 0; i < frames_wide_;
         i++)  // traverse along X (important that this is the inner loop)
    {
      float left = i * frame_width;
      float right = (i + 1) * frame_width;

      RectF frect(
          left, top, right,
          bottom);  // these will all be in the range [0.0,1.0]
      texture_coord_rects_.push_back(frect);  // the index of this array element
                                              // will be (i + j*m_iFramesWide)

      // LOG->Trace( "Adding frect%d %f %f %f %f", (i + j*m_iFramesWide),
      // frect.left, frect.top, frect.right, frect.bottom );
    }
  }
}

void RageTexture::GetFrameDimensionsFromFileName(
    std::string path, int* frames_wide, int* frames_high, int source_width,
    int source_height) {
  static Regex match(" ([0-9]+)x([0-9]+)([\\. ]|$)");
  std::vector<std::string> as_match;
  if (!match.Compare(path, as_match)) {
    *frames_wide = *frames_high = 1;
    return;
  }
  // Check for nonsense values.  Some people might not intend the hint. -Kyz
  int maybe_width = StringToInt(as_match[0]);
  int maybe_height = StringToInt(as_match[1]);
  if (maybe_width <= 0 || maybe_height <= 0) {
    *frames_wide = *frames_high = 1;
    return;
  }
  // Font.cpp uses this function, but can't pass in a texture size.  Other
  // textures can pass in a size though, and having more frames than pixels
  // makes no sense. -Kyz
  if (source_width > 0 && source_height > 0) {
    if (maybe_width > source_width || maybe_height > source_height) {
      *frames_wide = *frames_high = 1;
      return;
    }
  }
  *frames_wide = maybe_width;
  *frames_high = maybe_height;
}

const RectF* RageTexture::GetTextureCoordRect(int frame_no) const {
  return &texture_coord_rects_[frame_no % GetNumFrames()];
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the RageTexture. */
class LunaRageTexture : public Luna<RageTexture> {
 public:
  static int position(T* p, lua_State* L) {
    p->SetPosition(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int loop(T* p, lua_State* L) {
    p->SetLooping(BIArg(1));
    COMMON_RETURN_SELF;
  }
  static int rate(T* p, lua_State* L) {
    p->SetPlaybackRate(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int GetTextureCoordRect(T* p, lua_State* L) {
    const RectF* rect = p->GetTextureCoordRect(IArg(1));
    lua_pushnumber(L, rect->left_);
    lua_pushnumber(L, rect->top_);
    lua_pushnumber(L, rect->right_);
    lua_pushnumber(L, rect->bottom_);
    return 4;
  }
  static int GetNumFrames(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetNumFrames());
    return 1;
  }
  static int Reload(T* p, lua_State* L) {
    p->Reload();
    COMMON_RETURN_SELF;
  }
  DEFINE_METHOD(GetSourceWidth, GetSourceWidth());
  DEFINE_METHOD(GetSourceHeight, GetSourceHeight());
  DEFINE_METHOD(GetTextureWidth, GetTextureWidth());
  DEFINE_METHOD(GetTextureHeight, GetTextureHeight());
  DEFINE_METHOD(GetImageWidth, GetImageWidth());
  DEFINE_METHOD(GetImageHeight, GetImageHeight());
  DEFINE_METHOD(GetPath, GetID().filename_);

  LunaRageTexture() {
    ADD_METHOD(position);
    ADD_METHOD(loop);
    ADD_METHOD(rate);
    ADD_METHOD(GetTextureCoordRect);
    ADD_METHOD(GetNumFrames);
    ADD_METHOD(Reload);
    ADD_METHOD(GetSourceWidth);
    ADD_METHOD(GetSourceHeight);
    ADD_METHOD(GetTextureWidth);
    ADD_METHOD(GetTextureHeight);
    ADD_METHOD(GetImageWidth);
    ADD_METHOD(GetImageHeight);
    ADD_METHOD(GetPath);
  }
};

LUA_REGISTER_CLASS(RageTexture)
// lua end

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
