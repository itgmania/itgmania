#include "MovieTexture_Null.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "RageDisplay.h"
#include "RageLog.h"
#include "RageSurface.h"
#include "RageTextureID.h"
#include "RageUtil.h"
#include "arch/MovieTexture/MovieTexture.h"
#include "global.h"

class MovieTexture_Null : public RageMovieTexture {
 public:
  MovieTexture_Null(RageTextureID id);
  virtual ~MovieTexture_Null();
  void Invalidate() { tex_handle_ = 0; }
  uintptr_t GetTexHandle() const { return tex_handle_; }
  void Update(float /* delta */) {}
  void Reload() {}
  void SetPosition(float /* seconds */) {}
  void SetPlaybackRate(float) {}

 private:
  uintptr_t tex_handle_;
};

MovieTexture_Null::MovieTexture_Null(RageTextureID id) : RageMovieTexture(id) {
  LOG->Trace("MovieTexture_Null::MovieTexture_Null(ID)");
  tex_handle_ = 0;

  RageTextureID actual_id = GetID();

  actual_id.alpha_bits_ = 0;
  int size = 64;
  source_width_ = size;
  source_height_ = size;
  image_width_ = size;
  image_height_ = size;
  texture_width_ = power_of_two(size);
  texture_height_ = texture_width_;
  frames_wide_ = 1;
  frames_high_ = 1;

  CreateFrameRects();

  RagePixelFormat pixfmt = RagePixelFormat_RGBA4;
  if (!DISPLAY->SupportsTextureFormat(pixfmt)) {
    pixfmt = RagePixelFormat_RGBA8;
  }
  ASSERT(DISPLAY->SupportsTextureFormat(pixfmt));

  const RageDisplay::RagePixelFormatDesc* pfd =
      DISPLAY->GetPixelFormatDesc(pixfmt);
  RageSurface* img = CreateSurface(
      size, size, pfd->bpp_, pfd->masks_[0], pfd->masks_[1], pfd->masks_[2],
      pfd->masks_[3]);
  memset(img->pixels_, 0, static_cast<size_t>(img->pitch_) * img->h_);

  tex_handle_ = DISPLAY->CreateTexture(pixfmt, img, false);

  delete img;
}

MovieTexture_Null::~MovieTexture_Null() { DISPLAY->DeleteTexture(tex_handle_); }

REGISTER_MOVIE_TEXTURE_CLASS(Null);

RageMovieTexture* RageMovieTextureDriver_Null::Create(
    RageTextureID id, std::string& error) {
  return new MovieTexture_Null(id);
}

/*
 * (c) 2003 Steve Checkoway
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
