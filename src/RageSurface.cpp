#include "RageSurface.h"

#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

#include "RageUtil.h"
#include "global.h"

int32_t RageSurfacePalette::FindColor(const RageSurfaceColor& color) const {
  for (int i = 0; i < ncolors_; ++i) {
    if (colors_[i] == color) {
      return i;
    }
  }
  return -1;
}

RageSurfaceFormat::RageSurfaceFormat()
    : mask_(),
      shift_(),
      loss_(),
      rmask_(mask_[0]),
      gmask_(mask_[1]),
      bmask_(mask_[2]),
      amask_(mask_[3]),
      rshift_(shift_[0]),
      gshift_(shift_[1]),
      bshift_(shift_[2]),
      ashift_(shift_[3]) {}

RageSurfaceFormat::RageSurfaceFormat(const RageSurfaceFormat& cpy)
    : mask_(),
      shift_(),
      loss_(),
      rmask_(mask_[0]),
      gmask_(mask_[1]),
      bmask_(mask_[2]),
      amask_(mask_[3]),
      rshift_(shift_[0]),
      gshift_(shift_[1]),
      bshift_(shift_[2]),
      ashift_(shift_[3]) {
  bytes_per_pixel_ = cpy.bytes_per_pixel_;
  bits_per_pixel_ = cpy.bits_per_pixel_;
  mask_ = cpy.mask_;
  shift_ = cpy.shift_;
  loss_ = cpy.loss_;
  if (palette_) {
    palette_ = std::make_unique<RageSurfacePalette>(*palette_);
  }
}

void RageSurfaceFormat::GetRGB(
    uint32_t val, uint8_t* r, uint8_t* g, uint8_t* b) const {
  if (bytes_per_pixel_ == 1) {
    ASSERT(palette_ != nullptr);
    *r = palette_->colors_[val].r_;
    *g = palette_->colors_[val].g_;
    *b = palette_->colors_[val].b_;
  } else {
    *r = int8_t((val & mask_[0]) >> shift_[0] << loss_[0]);
    *g = int8_t((val & mask_[1]) >> shift_[1] << loss_[1]);
    *b = int8_t((val & mask_[2]) >> shift_[2] << loss_[2]);
  }
}

bool RageSurfaceFormat::MapRGBA(
    uint8_t r, uint8_t g, uint8_t b, uint8_t a, uint32_t& val) const {
  if (bytes_per_pixel_ == 1) {
    RageSurfaceColor c(r, g, b, a);
    int32_t n = palette_->FindColor(c);
    if (n == -1) {
      return false;
    }
    val = (uint32_t)n;
  } else {
    val = (r >> loss_[0] << shift_[0]) | (g >> loss_[1] << shift_[1]) |
          (b >> loss_[2] << shift_[2]) | (a >> loss_[3] << shift_[3]);
  }
  return true;
}

bool RageSurfaceFormat::Equivalent(const RageSurfaceFormat& rhs) const {
#define COMP(a) \
  if (a != rhs.a) return false;
  COMP(bytes_per_pixel_);
  COMP(rmask_);
  COMP(gmask_);
  COMP(bmask_);
  COMP(amask_);

  return true;
}

RageSurface::RageSurface() {
  format_ = &fmt_;
  pixels_ = nullptr;
  pixels_owned_ = true;
}

RageSurface::~RageSurface() {
  if (pixels_owned_) {
    delete[] pixels_;
  }
}

static int GetShiftFromMask(uint32_t mask) {
  if (!mask) {
    return 0;
  }

  int shift = 0;
  while ((mask & 1) == 0) {
    mask >>= 1;
    ++shift;
  }
  return shift;
}

static int GetBitsFromMask(uint32_t mask) {
  if (!mask) {
    return 0;
  }

  mask >>= GetShiftFromMask(mask);

  int bits = 0;
  while ((mask & 1) == 1) {
    mask >>= 1;
    ++bits;
  }
  return bits;
}

void SetupFormat(
    RageSurfaceFormat& fmt, int width, int height, int bits_per_pixel,
    uint32_t rmask, uint32_t gmask, uint32_t bmask, uint32_t amask) {
  fmt.bits_per_pixel_ = bits_per_pixel;
  fmt.bytes_per_pixel_ = bits_per_pixel / 8;
  if (fmt.bytes_per_pixel_ == 1) {
    ZERO(fmt.mask_);
    ZERO(fmt.shift_);

    // Loss for paletted textures is zero; the actual palette entries are 8-bit.
    ZERO(fmt.loss_);

    fmt.palette_ = std::make_unique<RageSurfacePalette>();
    fmt.palette_->ncolors_ = 256;
  } else {
    fmt.mask_[0] = rmask;
    fmt.mask_[1] = gmask;
    fmt.mask_[2] = bmask;
    fmt.mask_[3] = amask;

    fmt.shift_[0] = GetShiftFromMask(rmask);
    fmt.shift_[1] = GetShiftFromMask(gmask);
    fmt.shift_[2] = GetShiftFromMask(bmask);
    fmt.shift_[3] = GetShiftFromMask(amask);

    fmt.loss_[0] = (uint8_t)(8 - GetBitsFromMask(rmask));
    fmt.loss_[1] = (uint8_t)(8 - GetBitsFromMask(gmask));
    fmt.loss_[2] = (uint8_t)(8 - GetBitsFromMask(bmask));
    fmt.loss_[3] = (uint8_t)(8 - GetBitsFromMask(amask));
  }
}

RageSurface* CreateSurface(
    int width, int height, int bits_per_pixel, uint32_t rmask, uint32_t gmask,
    uint32_t bmask, uint32_t amask) {
  RageSurface* img = new RageSurface;

  SetupFormat(
      img->fmt_, width, height, bits_per_pixel, rmask, gmask, bmask, amask);

  img->w_ = width;
  img->h_ = height;
  img->flags_ = 0;
  img->pitch_ = width * bits_per_pixel / 8;
  img->pixels_ = new uint8_t[img->pitch_ * height];

  /*
  if( BitsPerPixel == 8 )
  {
          pImg->fmt.palette = new RageSurfacePalette;
  }
  */

  return img;
}

RageSurface* CreateSurfaceFrom(
    int width, int height, int bits_per_pixel, uint32_t rmask, uint32_t gmask,
    uint32_t bmask, uint32_t amask, uint8_t* pixels, uint32_t pitch) {
  RageSurface* img = new RageSurface;

  SetupFormat(
      img->fmt_, width, height, bits_per_pixel, rmask, gmask, bmask, amask);

  img->w_ = width;
  img->h_ = height;
  img->flags_ = 0;
  img->pitch_ = pitch;
  img->pixels_ = pixels;
  img->pixels_owned_ = false;

  return img;
}

/*
 * (c) 2001-2004 Glenn Maynard, Chris Danford
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
