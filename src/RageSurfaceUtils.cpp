#include "RageSurfaceUtils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "RageFile.h"
#include "RageLog.h"
#include "RageSurface.h"
#include "RageUtil.h"
#include "RageUtil/Endian.h"
#include "global.h"

uint32_t RageSurfaceUtils::decodepixel(const uint8_t* p, int bpp) {
  switch (bpp) {
    case 1:
      return *p;
    case 2:
      return *(uint16_t*)p;
    case 3:
      if constexpr (Endian::kBig) {
        return p[0] << 16 | p[1] << 8 | p[2];
      } else {
        return p[0] | p[1] << 8 | p[2] << 16;
      }

    case 4:
      return *(uint32_t*)p;
    default:
      return 0;  // shouldn't happen, but avoids warnings
  }
}

void RageSurfaceUtils::encodepixel(uint8_t* p, int bpp, uint32_t pixel) {
  switch (bpp) {
    case 1:
      *p = uint8_t(pixel);
      break;
    case 2:
      *(uint16_t*)p = uint16_t(pixel);
      break;
    case 3:
      if constexpr (Endian::kBig) {
        p[0] = uint8_t((pixel >> 16) & 0xff);
        p[1] = uint8_t((pixel >> 8) & 0xff);
        p[2] = uint8_t(pixel & 0xff);
      } else {
        p[0] = uint8_t(pixel & 0xff);
        p[1] = uint8_t((pixel >> 8) & 0xff);
        p[2] = uint8_t((pixel >> 16) & 0xff);
      }
      break;
    case 4:
      *(uint32_t*)p = pixel;
      break;
  }
}

// Get and set colors without scaling to 0..255.
void RageSurfaceUtils::GetRawRGBAV(
    uint32_t pixel, const RageSurfaceFormat& fmt, uint8_t* v) {
  if (fmt.bytes_per_pixel_ == 1) {
    v[0] = fmt.palette_->colors_[pixel].r_;
    v[1] = fmt.palette_->colors_[pixel].g_;
    v[2] = fmt.palette_->colors_[pixel].b_;
    v[3] = fmt.palette_->colors_[pixel].a_;
  } else {
    v[0] = uint8_t((pixel & fmt.rmask_) >> fmt.rshift_);
    v[1] = uint8_t((pixel & fmt.gmask_) >> fmt.gshift_);
    v[2] = uint8_t((pixel & fmt.bmask_) >> fmt.bshift_);
    v[3] = uint8_t((pixel & fmt.amask_) >> fmt.ashift_);
  }
}

void RageSurfaceUtils::GetRawRGBAV(
    const uint8_t* p, const RageSurfaceFormat& fmt, uint8_t* v) {
  uint32_t pixel = decodepixel(p, fmt.bytes_per_pixel_);
  GetRawRGBAV(pixel, fmt, v);
}

void RageSurfaceUtils::GetRGBAV(
    uint32_t pixel, const RageSurface* src, uint8_t* v) {
  GetRawRGBAV(pixel, src->fmt_, v);
  const RageSurfaceFormat* fmt = src->format_;
  for (int c = 0; c < 4; ++c) {
    v[c] = v[c] << fmt->loss_[c];
  }

  // Correct for surfaces that don't have an alpha channel.
  if (fmt->loss_[3] == 8) {
    v[3] = 255;
  }
}

// Inverse of GetRawRGBAV.
uint32_t RageSurfaceUtils::SetRawRGBAV(
    const RageSurfaceFormat* fmt, const uint8_t* v) {
  return v[0] << fmt->rshift_ | v[1] << fmt->gshift_ | v[2] << fmt->bshift_ |
         v[3] << fmt->ashift_;
}

void RageSurfaceUtils::SetRawRGBAV(
    uint8_t* p, const RageSurface* src, const uint8_t* v) {
  uint32_t pixel = SetRawRGBAV(src->format_, v);
  encodepixel(p, src->format_->bytes_per_pixel_, pixel);
}

// Inverse of GetRGBAV.
uint32_t RageSurfaceUtils::SetRGBAV(
    const RageSurfaceFormat* fmt, const uint8_t* v) {
  return (v[0] >> fmt->loss_[0]) << fmt->shift_[0] |
         (v[1] >> fmt->loss_[1]) << fmt->shift_[1] |
         (v[2] >> fmt->loss_[2]) << fmt->shift_[2] |
         (v[3] >> fmt->loss_[3]) << fmt->shift_[3];
}

void RageSurfaceUtils::GetBitsPerChannel(
    const RageSurfaceFormat* fmt, uint32_t bits[4]) {
  // The actual bits stored in each color is 8-loss.
  for (int c = 0; c < 4; ++c) {
    bits[c] = 8 - fmt->loss_[c];
  }
}

void RageSurfaceUtils::CopySurface(const RageSurface* src, RageSurface* dest) {
  // Copy the palette, if we have one.
  if (src->format_->bits_per_pixel_ == 8 &&
      dest->format_->bits_per_pixel_ == 8) {
    ASSERT(dest->fmt_.palette_ != nullptr);
    *dest->fmt_.palette_ = *src->fmt_.palette_;
  }

  Blit(src, dest, -1, -1);
}

bool RageSurfaceUtils::ConvertSurface(
    const RageSurface* src, RageSurface*& dst, int width, int height, int bpp,
    uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
  dst = CreateSurface(width, height, bpp, r, g, b, a);

  // If the formats are the same, no conversion is needed. Ignore the palette.
  if (width == src->w_ && height == src->h_ &&
      src->format_->Equivalent(*dst->format_)) {
    delete dst;
    dst = nullptr;
    return false;
  }

  CopySurface(src, dst);
  return true;
}

void RageSurfaceUtils::ConvertSurface(
    RageSurface*& image, int width, int height, int bpp, uint32_t r, uint32_t g,
    uint32_t b, uint32_t a) {
  RageSurface* ret_image;
  if (!ConvertSurface(image, ret_image, width, height, bpp, r, g, b, a)) {
    return;
  }

  delete image;
  image = ret_image;
}

// Local helper for FixHiddenAlpha.
static void FindAlphaRGB(
    const RageSurface* img, uint8_t& r, uint8_t& g, uint8_t& b, bool reverse) {
  r = g = b = 0;

  // If we have no alpha, there's no alpha color.
  if (img->format_->bits_per_pixel_ > 8 && !img->format_->amask_) {
    return;
  }

  // Eww. Sorry. Iterate front-to-back or in reverse.
  for (int y = reverse ? img->h_ - 1 : 0; reverse ? (y >= 0) : (y < img->h_);
       reverse ? (--y) : (++y)) {
    uint8_t* row = (uint8_t*)img->pixels_ + img->pitch_ * y;
    if (reverse) {
      row += img->format_->bytes_per_pixel_ * (img->w_ - 1);
    }

    for (int x = 0; x < img->w_; ++x) {
      uint32_t val =
          RageSurfaceUtils::decodepixel(row, img->format_->bytes_per_pixel_);
      if (img->format_->bits_per_pixel_ == 8) {
        if (img->format_->palette_->colors_[val].a_) {
          // This color isn't fully transparent, so grab it.
          r = img->format_->palette_->colors_[val].r_;
          g = img->format_->palette_->colors_[val].g_;
          b = img->format_->palette_->colors_[val].b_;
          return;
        }
      } else {
        if (val & img->format_->amask_) {
          // This color isn't fully transparent, so grab it.
          img->format_->GetRGB(val, &r, &g, &b);
          return;
        }
      }

      if (reverse) {
        row -= img->format_->bytes_per_pixel_;
      } else {
        row += img->format_->bytes_per_pixel_;
      }
    }
  }

  // Huh?  The image is completely transparent.
  r = g = b = 0;
}

/* Local helper for FixHiddenAlpha. Set the underlying RGB values of all pixels
 * in img that are completely transparent. */
static void SetAlphaRGB(
    const RageSurface* img, uint8_t r, uint8_t g, uint8_t b) {
  // If it's a paletted surface, all we have to do is change the palette.
  if (img->format_->bits_per_pixel_ == 8) {
    for (int c = 0; c < img->format_->palette_->ncolors_; ++c) {
      if (img->format_->palette_->colors_[c].a_) {
        continue;
      }
      img->format_->palette_->colors_[c].r_ = r;
      img->format_->palette_->colors_[c].g_ = g;
      img->format_->palette_->colors_[c].b_ = b;
    }
    return;
  }

  // If it's RGBA and there's no alpha channel, we have nothing to do.
  if (img->format_->bits_per_pixel_ > 8 && !img->format_->amask_) {
    return;
  }

  uint32_t trans;
  img->format_->MapRGBA(r, g, b, 0, trans);
  for (int y = 0; y < img->h_; ++y) {
    uint8_t* row = img->pixels_ + img->pitch_ * y;

    for (int x = 0; x < img->w_; ++x) {
      uint32_t val =
          RageSurfaceUtils::decodepixel(row, img->format_->bytes_per_pixel_);
      if (val != trans && !(val & img->format_->amask_)) {
        RageSurfaceUtils::encodepixel(
            row, img->format_->bytes_per_pixel_, trans);
      }

      row += img->format_->bytes_per_pixel_;
    }
  }
}

/* When we scale up images (which we always do in high res), pixels
 * that are completely transparent can be blended with opaque pixels,
 * causing their RGB elements to show. This is visible in many textures
 * as a pixel-wide border in the wrong color. This is tricky to fix.
 * We need to set the RGB components of completely transparent pixels
 * to a reasonable color.
 *
 * Most images have a single border color. For these, the transparent
 * color is easy: search through the image top-bottom-left-right,
 * find the first non-transparent pixel, and pull out its RGB.
 *
 * A few images don't. We can only make a guess here. After the above
 * search, do the same in reverse (bottom-top-right-left). If the color
 * we find is different, just set the border color to black.
 */
void RageSurfaceUtils::FixHiddenAlpha(RageSurface* img) {
  // If there are no alpha bits, there's nothing to fix.
  if (img->format_->bits_per_pixel_ != 8 && img->format_->amask_ == 0) {
    return;
  }

  uint8_t r, g, b;
  FindAlphaRGB(img, r, g, b, false);

  uint8_t cr, cg, cb;  // compare
  FindAlphaRGB(img, cr, cg, cb, true);

  if (cr != r || cg != g || cb != b) {
    r = g = b = 0;
  }

  SetAlphaRGB(img, r, g, b);
}

/* Scan the surface to see what level of alpha it uses. This can be used to
 * find the best surface format for a texture; eg. a TRAIT_BOOL_TRANSPARENCY or
 * TRAIT_NO_TRANSPARENCY surface can use RGB5A1 instead of RGBA4 for greater
 * color resolution; a TRAIT_NO_TRANSPARENCY could also use R5G6B5. */
int RageSurfaceUtils::FindSurfaceTraits(const RageSurface* img) {
  const int kNeedsNoAlpha = 0, kNeedsBoolAlpha = 1, kNeedsFullAlpha = 2;
  int alpha_type = kNeedsNoAlpha;

  uint32_t max_alpha;
  if (img->format_->bits_per_pixel_ == 8) {
    // Short circuit if we already know we have no transparency.
    bool have_non_opaque = false;
    for (int c = 0; !have_non_opaque && c < img->format_->palette_->ncolors_;
         ++c) {
      if (img->format_->palette_->colors_[c].a_ != 0xFF) {
        have_non_opaque = true;
      }
    }

    if (!have_non_opaque) {
      return TRAIT_NO_TRANSPARENCY;
    }

    max_alpha = 0xFF;
  } else {
    // Short circuit if we already know we have no transparency.
    if (img->format_->amask_ == 0) {
      return TRAIT_NO_TRANSPARENCY;
    }

    max_alpha = img->format_->amask_;
  }

  for (int y = 0; y < img->h_; ++y) {
    uint8_t* row = (uint8_t*)img->pixels_ + img->pitch_ * y;

    for (int x = 0; x < img->w_; ++x) {
      uint32_t val = decodepixel(row, img->format_->bytes_per_pixel_);

      uint32_t alpha;
      if (img->format_->bits_per_pixel_ == 8) {
        alpha = img->format_->palette_->colors_[val].a_;
      } else {
        alpha = (val & img->format_->amask_);
      }

      if (alpha == 0) {
        alpha_type = std::max(alpha_type, kNeedsBoolAlpha);
      } else if (alpha != max_alpha) {
        alpha_type = std::max(alpha_type, kNeedsFullAlpha);
      }

      row += img->format_->bytes_per_pixel_;
    }
  }

  int ret = 0;
  switch (alpha_type) {
    case kNeedsNoAlpha:
      ret |= TRAIT_NO_TRANSPARENCY;
      break;
    case kNeedsBoolAlpha:
      ret |= TRAIT_BOOL_TRANSPARENCY;
      break;
    case kNeedsFullAlpha:
      break;
    default:
      FAIL_M(ssprintf("Invalid alpha type: %i", alpha_type));
  }

  return ret;
}

/* Simplified:
 *
 * No source alpha.
 * Palette -> palette blits assume the palette is identical (no mapping).
 * No color key.
 * No general blitting rects. */

static bool blit_same_type(
    const RageSurface* src_surf, const RageSurface* dst_surf, int width,
    int height) {
  if (src_surf->format_->bytes_per_pixel_ !=
          dst_surf->format_->bytes_per_pixel_ ||
      src_surf->format_->rmask_ != dst_surf->format_->rmask_ ||
      src_surf->format_->gmask_ != dst_surf->format_->gmask_ ||
      src_surf->format_->bmask_ != dst_surf->format_->bmask_ ||
      src_surf->format_->amask_ != dst_surf->format_->amask_) {
    return false;
  }

  const uint8_t* src = src_surf->pixels_;
  uint8_t* dst = dst_surf->pixels_;

  // If possible, memcpy the whole thing.
  if (src_surf->w_ == width && dst_surf->w_ == width &&
      src_surf->pitch_ == dst_surf->pitch_) {
    memcpy(dst, src, static_cast<size_t>(height) * src_surf->pitch_);
    return true;
  }

  // The rows don't line up, so memcpy row by row.
  while (height--) {
    memcpy(
        dst, src,
        static_cast<size_t>(width) * src_surf->format_->bytes_per_pixel_);
    src += src_surf->pitch_;
    dst += dst_surf->pitch_;
  }

  return true;
}

/* Rescaling blit with no ckey. This is used to update movies in
 * D3D, so optimization is very important. */
static bool blit_rgba_to_rgba(
    const RageSurface* src_surf, const RageSurface* dst_surf, int width,
    int height) {
  if (src_surf->format_->bytes_per_pixel_ == 1 ||
      dst_surf->format_->bytes_per_pixel_ == 1) {
    return false;
  }

  const uint8_t* src = src_surf->pixels_;
  uint8_t* dst = dst_surf->pixels_;

  // Bytes to skip at the end of a line.
  const int srcskip =
      src_surf->pitch_ - width * src_surf->format_->bytes_per_pixel_;
  const int dstskip =
      dst_surf->pitch_ - width * dst_surf->format_->bytes_per_pixel_;

  const std::array<uint32_t, 4>& src_shifts = src_surf->format_->shift_;
  const std::array<uint32_t, 4>& dst_shifts = dst_surf->format_->shift_;
  const std::array<uint32_t, 4>& src_masks = src_surf->format_->mask_;
  const std::array<uint32_t, 4>& dst_masks = dst_surf->format_->mask_;

  uint8_t lookup[4][256];
  for (int c = 0; c < 4; ++c) {
    const uint32_t max_src_val = src_masks[c] >> src_shifts[c];
    const uint32_t max_dst_val = dst_masks[c] >> dst_shifts[c];
    ASSERT(max_src_val <= 0xFF);
    ASSERT(max_dst_val <= 0xFF);

    if (src_masks[c] == 0) {
      /* The source is missing a channel. Alpha defaults to opaque, other
       * channels default to 0. */
      if (c == 3) {
        lookup[c][0] = (uint8_t)max_dst_val;
      } else {
        lookup[c][0] = 0;
      }
    } else {
      /* Calculate a color conversion table. There are a few ways we can do
       * this (each list is the resulting table for 4->2 bit):
       *
       * SCALE( i, 0, max_src_val+1, 0, max_dst_val+1 );
       * { 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3 }
       * SCALE( i, 0, max_src_val, 0, max_dst_val );
       * { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 3 }
       * std::lrint( ((float) i / max_src_val) * max_dst_val )
       * { 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3 }
       *
       * We use the first for increasing resolution, since it gives the most
       * even distribution.
       *
       * 2->4 bit:
       * SCALE( i, 0, max_src_val+1, 0, max_dst_val+1 );
       * { 0, 4, 8, 12 }
       * SCALE( i, 0, max_src_val, 0, max_dst_val );
       * { 0, 5, 10, 15 }
       * std::lrint( ((float) i / max_src_val) * max_dst_val )
       * { 0, 5, 10, 15 }
       *
       * The latter two are equivalent and give an even distribution; we use the
       * second, since the first doesn't scale max_src_val to max_dst_val.
       *
       * Having separate formulas for increasing and decreasing resolution seems
       * strange; what's wrong here? */
      if (max_src_val > max_dst_val) {
        for (uint32_t i = 0; i <= max_src_val; ++i) {
          lookup[c][i] =
              (uint8_t)SCALE(i, 0, max_src_val + 1, 0, max_dst_val + 1);
        }
      } else {
        for (uint32_t i = 0; i <= max_src_val; ++i) {
          lookup[c][i] = (uint8_t)SCALE(i, 0, max_src_val, 0, max_dst_val);
        }
      }
    }
  }

  while (height--) {
    int x = 0;
    while (x++ < width) {
      unsigned int pixel = RageSurfaceUtils::decodepixel(
          src, src_surf->format_->bytes_per_pixel_);

      // Convert pixel to the destination format.
      unsigned int opixel = 0;
      for (int c = 0; c < 4; ++c) {
        int l_src = (pixel & src_masks[c]) >> src_shifts[c];
        opixel |= lookup[c][l_src] << dst_shifts[c];
      }

      // Store it.
      RageSurfaceUtils::encodepixel(
          dst, dst_surf->format_->bytes_per_pixel_, opixel);

      src += src_surf->format_->bytes_per_pixel_;
      dst += dst_surf->format_->bytes_per_pixel_;
    }

    src += srcskip;
    dst += dstskip;
  }

  return true;
}

static bool blit_generic(
    const RageSurface* src_surf, const RageSurface* dst_surf, int width,
    int height) {
  if (src_surf->format_->bytes_per_pixel_ != 1 ||
      dst_surf->format_->bytes_per_pixel_ == 1) {
    return false;
  }

  const uint8_t* src = src_surf->pixels_;
  uint8_t* dst = dst_surf->pixels_;

  // Bytes to skip at the end of a line.
  const int srcskip =
      src_surf->pitch_ - width * src_surf->format_->bytes_per_pixel_;
  const int dstskip =
      dst_surf->pitch_ - width * dst_surf->format_->bytes_per_pixel_;

  while (height--) {
    int x = 0;
    while (x++ < width) {
      unsigned int pixel = RageSurfaceUtils::decodepixel(
          src, src_surf->format_->bytes_per_pixel_);

      uint8_t colors[4];
      // Convert pixel to the destination RGBA.
      colors[0] = src_surf->format_->palette_->colors_[pixel].r_;
      colors[1] = src_surf->format_->palette_->colors_[pixel].g_;
      colors[2] = src_surf->format_->palette_->colors_[pixel].b_;
      colors[3] = src_surf->format_->palette_->colors_[pixel].a_;
      pixel = RageSurfaceUtils::SetRGBAV(dst_surf->format_, colors);

      // Store it.
      RageSurfaceUtils::encodepixel(
          dst, dst_surf->format_->bytes_per_pixel_, pixel);

      src += src_surf->format_->bytes_per_pixel_;
      dst += dst_surf->format_->bytes_per_pixel_;
    }

    src += srcskip;
    dst += dstskip;
  }

  return true;
}

// Blit src onto dst.
void RageSurfaceUtils::Blit(
    const RageSurface* src, RageSurface* dst, int width, int height) {
  if (width == -1) {
    width = src->w_;
  }
  if (height == -1) {
    height = src->h_;
  }
  width = std::min(src->w_, dst->w_);
  height = std::min(src->h_, dst->h_);

  /* Try each blit until we find one that works; run them in order of
   * efficiency, so we use the fastest blit possible. */
  do {
    // RGBA->RGBA with the same format, or PAL->PAL. Simple copy.
    if (blit_same_type(src, dst, width, height)) {
      break;
    }

    // RGBA->RGBA with different formats.
    if (blit_rgba_to_rgba(src, dst, width, height)) {
      break;
    }

    // PAL->RGBA.
    if (blit_generic(src, dst, width, height)) {
      break;
    }

    FAIL_M("We don't do RGBA->PAL");
  } while (0);

  /* The destination surface may be larger than the source. For example, we may
   * be blitting a 200x200 image onto a 256x256 surface for OpenGL. Normally,
   * that extra space isn't actually used; we'll only render the image space.
   * However, bilinear filtering will cause the lines of pixels at 201x... and
   * ...x201 to be visible. We need to make sure those pixels make sense.
   *
   * Previously, we just cleared the image to transparent or the color key. This
   * has two problems. First, we may not have space for a color key (an image
   * with 256 non-transparent palette colors). Second, that's not completely
   * correct; it'll force the outside border of the image to filter to
   * transparent. If the image is being tiled with another image, that may leave
   * seams.
   *
   * (In some cases, filtering to transparent is preferable, particularly when
   * displaying a sprite in perspective. If you want that, add blank space to
   * the image explicitly.)
   *
   * Copy the last column (200x... -> 201x...), then the last row (...x200 ->
   * ...x201). */

  CorrectBorderPixels(dst, width, height);
}

/* If only width x height of img is actually going to be used, and there's extra
 * space on the surface, duplicate the last row and column to ensure that we
 * don't pull in unexpected data when rendering with bilinear filtering.
 *
 * We do this if there's memory available, even if that space extends outside
 * of the image (in the per-line padding or after the end).  This way, surfaces
 * can be padded to power-of-two dimensions by the image loaders, and if no
 * other adjustments are needed, they can be passed directly to the renderer
 * without doing any extra copies. */
void RageSurfaceUtils::CorrectBorderPixels(
    RageSurface* img, int width, int height) {
  if (width * img->fmt_.bytes_per_pixel_ < img->pitch_) {
    // Duplicate the last column.
    int offset = img->format_->bytes_per_pixel_ * (width - 1);
    uint8_t* p = (uint8_t*)img->pixels_ + offset;

    for (int y = 0; y < height; ++y) {
      uint32_t pixel = decodepixel(p, img->format_->bytes_per_pixel_);
      encodepixel(
          p + img->format_->bytes_per_pixel_, img->format_->bytes_per_pixel_,
          pixel);

      p += img->pitch_;
    }
  }

  if (height < img->h_) {
    // Duplicate the last row.
    uint8_t* srcp = img->pixels_;
    srcp += img->pitch_ * (height - 1);
    memcpy(srcp + img->pitch_, srcp, img->pitch_);
  }
}

struct SurfaceHeader {
  int width_, height_, pitch_;
  int rmask_, gmask_, bmask_, amask_;
  int bpp_;
};

// Save and load RageSurfaces to disk, in a very fast, nonportable way.
bool RageSurfaceUtils::SaveSurface(const RageSurface* img, std::string file) {
  RageFile f;
  if (!f.Open(file, RageFile::WRITE)) {
    return false;
  }

  SurfaceHeader h;
  memset(&h, 0, sizeof(h));

  h.height_ = img->h_;
  h.width_ = img->w_;
  h.pitch_ = img->pitch_;
  h.rmask_ = img->format_->rmask_;
  h.gmask_ = img->format_->gmask_;
  h.bmask_ = img->format_->bmask_;
  h.amask_ = img->format_->amask_;
  h.bpp_ = img->format_->bits_per_pixel_;

  f.Write(&h, sizeof(h));

  if (h.bpp_ == 8) {
    f.Write(
        &img->format_->palette_->ncolors_,
        sizeof(img->format_->palette_->ncolors_));
    f.Write(
        img->format_->palette_->colors_,
        img->format_->palette_->ncolors_ * sizeof(RageSurfaceColor));
  }

  f.Write(img->pixels_, static_cast<size_t>(img->h_) * img->pitch_);

  return true;
}

RageSurface* RageSurfaceUtils::LoadSurface(std::string file) {
  RageFile f;
  if (!f.Open(file)) {
    return nullptr;
  }

  SurfaceHeader h;
  if (f.Read(&h, sizeof(h)) != sizeof(h)) {
    return nullptr;
  }

  RageSurfacePalette palette;
  if (h.bpp_ == 8) {
    if (f.Read(&palette.ncolors_, sizeof(palette.ncolors_)) !=
        sizeof(palette.ncolors_)) {
      return nullptr;
    }
    ASSERT_M(palette.ncolors_ <= 256, ssprintf("%i", palette.ncolors_));
    if (f.Read(palette.colors_, palette.ncolors_ * sizeof(RageSurfaceColor)) !=
        int(palette.ncolors_ * sizeof(RageSurfaceColor))) {
      return nullptr;
    }
  }

  // Create the surface.
  RageSurface* img = CreateSurface(
      h.width_, h.height_, h.bpp_, h.rmask_, h.gmask_, h.bmask_, h.amask_);
  ASSERT(img != nullptr);

  /* If the pitch has changed, this surface is either corrupt, or was
   * created with a different version whose CreateSurface() behavior
   * was different. */
  if (h.pitch_ != img->pitch_) {
    LOG->Trace(
        "Error loading \"%s\": expected pitch %i, got %i (%ibpp, %i width)",
        file.c_str(), h.pitch_, img->pitch_, h.bpp_, h.width_);
    delete img;
    return nullptr;
  }

  const size_t expected_size = static_cast<size_t>(h.height_) * h.pitch_;
  const size_t bytes_read =
      static_cast<size_t>(f.Read(img->pixels_, expected_size));
  if (bytes_read != expected_size) {
    delete img;
    return nullptr;
  }

  // Set the palette.
  if (h.bpp_ == 8) {
    *img->fmt_.palette_ = palette;
  }

  return img;
}

/* This converts an image to a special 8-bit paletted format. The palette is set
 * up so that palette indexes look like regular, packed components.
 *
 * For example, an image with 8 bits of grayscale and 0 bits of alpha has a
 * palette that looks like { 0,0,0,255 }, { 1,1,1,255 }, { 2,2,2,255 }, ... {
 * 255,255,255,255 }. This results in index components that can be treated as
 * grayscale values.
 *
 * An image with 2 bits of grayscale and 2 bits of alpha look like
 * { 0,0,0,0  }, { 85,85,85,0  }, { 170,170,170,0  }, { 255,255,255,0  },
 * { 0,0,0,85 }, { 85,85,85,85 }, { 170,170,170,85 }, { 255,255,255,85 }, ...
 *
 * This results in index components that can be pulled apart like regular packed
 * values: the first two bits of the index are the grayscale component, and the
 * next two bits are the alpha component.
 *
 * This gives us a generic way to handle arbitrary 8-bit texture formats. */
RageSurface* RageSurfaceUtils::PalettizeToGrayscale(
    const RageSurface* src_surf, unsigned int gray_bits,
    unsigned int alpha_bits) {
  alpha_bits = std::min(alpha_bits, 8 - src_surf->format_->loss_[3]);

  const unsigned int total_bits = gray_bits + alpha_bits;
  ASSERT(total_bits <= 8);

  RageSurface* dst_surf =
      CreateSurface(src_surf->w_, src_surf->h_, 8, 0, 0, 0, 0);

  // Set up the palette.
  const unsigned int total_colors = 1u << total_bits;
  const unsigned int ivalues = 1u << gray_bits;  // number of intensity values
  const unsigned int ishift = 0u;                // intensity shift
  const unsigned int imask = ((1u << gray_bits) - 1u)
                             << ishift;  // intensity mask
  const unsigned int iloss = 8u - gray_bits;

  const unsigned int avalues = 1u << alpha_bits;  // number of alpha values
  const unsigned int ashift = gray_bits;          // alpha shift
  const unsigned int amask = ((1u << alpha_bits) - 1u) << ashift;  // alpha mask
  const unsigned int aloss = 8u - alpha_bits;

  for (size_t index = 0; index < total_colors; ++index) {
    const unsigned int i = (index & imask) >> ishift;
    const unsigned int a = (index & amask) >> ashift;

    // if only one intensity value, always fullbright
    const uint8_t scaled_i =
        ivalues == 1
            ? 255
            : std::clamp(std::lrint(i * (255.0f / (ivalues - 1))), 0L, 255L);

    // if only one alpha value, always opaque
    const uint8_t scaled_a =
        avalues == 1
            ? 255
            : std::clamp(std::lrint(a * (255.0f / (avalues - 1))), 0L, 255L);

    RageSurfaceColor c;
    c.r_ = scaled_i;
    c.g_ = scaled_i;
    c.b_ = scaled_i;
    c.a_ = scaled_a;

    dst_surf->fmt_.palette_->colors_[index] = c;
  }

  const uint8_t* src = src_surf->pixels_;
  uint8_t* dst = dst_surf->pixels_;

  int height = src_surf->h_;
  int width = src_surf->w_;

  // Bytes to skip at the end of a line.
  const int srcskip =
      src_surf->pitch_ - width * src_surf->format_->bytes_per_pixel_;
  const int dstskip =
      dst_surf->pitch_ - width * dst_surf->format_->bytes_per_pixel_;

  while (height--) {
    int x = 0;
    while (x++ < width) {
      unsigned int pixel =
          decodepixel(src, src_surf->format_->bytes_per_pixel_);

      uint8_t colors[4];
      GetRGBAV(pixel, src_surf, colors);

      int ival = 0;
      ival += colors[0];
      ival += colors[1];
      ival += colors[2];
      ival /= 3;

      pixel = (ival >> iloss) << ishift | (colors[3] >> aloss) << ashift;

      // Store it.
      *dst = uint8_t(pixel);

      src += src_surf->format_->bytes_per_pixel_;
      dst += dst_surf->format_->bytes_per_pixel_;
    }

    src += srcskip;
    dst += dstskip;
  }

  return dst_surf;
}

RageSurface* RageSurfaceUtils::MakeDummySurface(int height, int width) {
  RageSurface* ret_image = CreateSurface(width, height, 8, 0, 0, 0, 0);

  RageSurfaceColor pink(0xFF, 0x10, 0xFF, 0xFF);
  ret_image->fmt_.palette_->colors_[0] = pink;

  memset(
      ret_image->pixels_, 0,
      static_cast<size_t>(ret_image->h_) * ret_image->pitch_);

  return ret_image;
}

/* HACK: Some banners and textures have #F800F8 as the color key.
 * Search the edge for it; if we find it, use that as the color key. */
static bool ImageUsesOffHotPink(const RageSurface* img) {
  uint32_t off_hot_pink;
  if (!img->format_->MapRGBA(0xF8, 0, 0xF8, 0xFF, off_hot_pink)) {
    return false;
  }

  const uint8_t* p = img->pixels_;
  for (int x = 0; x < img->w_; ++x) {
    uint32_t val =
        RageSurfaceUtils::decodepixel(p, img->format_->bytes_per_pixel_);
    if (val == off_hot_pink) {
      return true;
    }
    p += img->format_->bytes_per_pixel_;
  }

  p = img->pixels_;
  p += img->pitch_ * (img->h_ - 1);
  for (int i = 0; i < img->w_; i++) {
    uint32_t val =
        RageSurfaceUtils::decodepixel(p, img->format_->bytes_per_pixel_);
    if (val == off_hot_pink) {
      return true;
    }
    p += img->format_->bytes_per_pixel_;
  }
  return false;
}

/* Set #FF00FF and #F800F8 to transparent. img may be reallocated if it has no
 * alpha bits. */
void RageSurfaceUtils::ApplyHotPinkColorKey(RageSurface*& img) {
  if (img->format_->bits_per_pixel_ == 8) {
    uint32_t color;
    if (img->format_->MapRGBA(0xF8, 0, 0xF8, 0xFF, color)) {
      img->format_->palette_->colors_[color].a_ = 0;
    }
    if (img->format_->MapRGBA(0xFF, 0, 0xFF, 0xFF, color)) {
      img->format_->palette_->colors_[color].a_ = 0;
    }
    return;
  }

  // RGBA. Make sure we have alpha.
  if (img->format_->amask_ == 0) {
    // We don't have any alpha.  Try to enable it without copying.
    /* XXX: need to scan the surface and make sure the new alpha bit is always 1
     */
    /*
    const int used_bits = img->format->Rmask | img->format->Gmask |
    img->format->Bmask;

    for( int i = 0; img->format->Amask == 0 && i < img->format->BitsPerPixel;
    ++i )
    {
            if( (used_bits & (1<<i)) )
            {
                    img->format->Amask = 1<<i;
                    img->format->Aloss = 7;
                    img->format->Ashift = (uint8_t) i;
            }
    }
    */
    // If we didn't have any free bits, convert to make room.
    if (img->format_->amask_ == 0) {
      ConvertSurface(
          img, img->w_, img->h_, 32, 0xFF000000, 0x00FF0000, 0x0000FF00,
          0x000000FF);
    }
  }

  uint32_t hot_pink;

  bool have_color_key;
  if (ImageUsesOffHotPink(img)) {
    have_color_key = img->format_->MapRGBA(0xF8, 0, 0xF8, 0xFF, hot_pink);
  } else {
    have_color_key = img->format_->MapRGBA(0xFF, 0, 0xFF, 0xFF, hot_pink);
  }
  if (!have_color_key) {
    return;
  }

  for (int y = 0; y < img->h_; ++y) {
    uint8_t* row = img->pixels_ + img->pitch_ * y;

    for (int x = 0; x < img->w_; ++x) {
      uint32_t val = decodepixel(row, img->format_->bytes_per_pixel_);
      if (val == hot_pink) {
        encodepixel(row, img->format_->bytes_per_pixel_, 0);
      }

      row += img->format_->bytes_per_pixel_;
    }
  }
}

void RageSurfaceUtils::FlipVertically(RageSurface* img) {
  const int pitch = img->pitch_;
  const int bytes_per_row = img->format_->bytes_per_pixel_ * img->w_;
  char* row = new char[bytes_per_row];

  for (int y = 0; y < img->h_ / 2; y++) {
    int y2 = img->h_ - 1 - y;
    memcpy(row, img->pixels_ + pitch * y, bytes_per_row);
    memcpy(img->pixels_ + pitch * y, img->pixels_ + pitch * y2, bytes_per_row);
    memcpy(img->pixels_ + pitch * y2, row, bytes_per_row);
  }

  delete[] row;
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
