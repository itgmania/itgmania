#include "RageBitmapTexture.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "RageDisplay.h"
#include "RageLog.h"
#include "RageSurface.h"
#include "RageSurfaceUtils.h"
#include "RageSurfaceUtils_Dither.h"
#include "RageSurfaceUtils_Zoom.h"
#include "RageSurface_Load.h"
#include "RageTexture.h"
#include "RageTextureID.h"
#include "RageTextureManager.h"
#include "RageUtil.h"
#include "RageUtil/Regex.h"
#include "StdString.h"
#include "StepMania.h"
#include "arch/Dialog/Dialog.h"
#include "global.h"

static void GetResolutionFromFileName(
    std::string sPath, int& iWidth, int& iHeight) {
  /* Match:
   *  Foo (res 512x128).png
   * Also allow, eg:
   *  Foo (dither, res 512x128).png
   * Be careful that this doesn't get mixed up with frame dimensions. */
  static Regex re("\\([^\\)]*res ([0-9]+)x([0-9]+).*\\)");

  std::vector<std::string> asMatches;
  if (!re.Compare(sPath, asMatches)) {
    return;
  }

  // Check for nonsense values.  Some people might not intend the hint. -Kyz
  int maybe_width = StringToInt(asMatches[0]);
  int maybe_height = StringToInt(asMatches[1]);
  if (maybe_width <= 0 || maybe_height <= 0) {
    return;
  }
  iWidth = maybe_width;
  iHeight = maybe_height;
}

RageBitmapTexture::RageBitmapTexture(RageTextureID name)
    : RageTexture(name), tex_handle_(0) {
  Create();
}

RageBitmapTexture::~RageBitmapTexture() { Destroy(); }

void RageBitmapTexture::Reload() {
  Destroy();
  Create();
}

/*
 * Each dwMaxSize, dwTextureColorDepth and iAlphaBits are maximums; we may
 * use less.  iAlphaBits must be 0, 1 or 4.
 *
 * XXX: change iAlphaBits == 4 to iAlphaBits == 8 to indicate "as much alpha
 * as needed", since that's what it really is; still only use 4 in 16-bit
 * textures.
 *
 * Dither forces dithering when loading 16-bit textures.
 * Stretch forces the loaded image to fill the texture completely.
 */
void RageBitmapTexture::Create() {
  RageTextureID actualID = GetID();

  ASSERT(actualID.filename_ != "");

  /* Load the image into a RageSurface. */
  std::string error;
  RageSurface* pImg = nullptr;
  if (actualID.filename_ == TEXTUREMAN->GetScreenTextureID().filename_) {
    pImg = TEXTUREMAN->GetScreenSurface();
  } else {
    pImg = RageSurfaceUtils::LoadFile(actualID.filename_, error);
  }

  /* Tolerate corrupt/unknown images. */
  if (pImg == nullptr) {
    std::string warning = ssprintf(
        "RageBitmapTexture: Couldn't load %s: %s", actualID.filename_.c_str(),
        error.c_str());
    LOG->Warn("%s", warning.c_str());
    Dialog::OK(warning, "missing_texture");
    pImg = RageSurfaceUtils::MakeDummySurface(64, 64);
    ASSERT(pImg != nullptr);
  }

  if (actualID.hot_pink_color_key_) {
    RageSurfaceUtils::ApplyHotPinkColorKey(pImg);
  }

  {
    /* Do this after setting the color key for paletted images; it'll also
     * return TRAIT_NO_TRANSPARENCY if the color key is never used. */
    int iTraits = RageSurfaceUtils::FindSurfaceTraits(pImg);
    if (iTraits & RageSurfaceUtils::TRAIT_NO_TRANSPARENCY) {
      actualID.alpha_bits_ = 0;
    } else if (iTraits & RageSurfaceUtils::TRAIT_BOOL_TRANSPARENCY) {
      actualID.alpha_bits_ = 1;
    }
  }

  // look in the file name for a format hints
  std::string sHintString =
      GetID().filename_ + actualID.additional_texture_hints_;
  MakeLower(sHintString);

  if (sHintString.find("32bpp") != std::string::npos) {
    actualID.color_depth_ = 32;
  } else if (sHintString.find("16bpp") != std::string::npos) {
    actualID.color_depth_ = 16;
  }
  if (sHintString.find("dither") != std::string::npos) {
    actualID.dither_ = true;
  }
  if (sHintString.find("stretch") != std::string::npos) {
    actualID.stretch_ = true;
  }
  if (sHintString.find("mipmaps") != std::string::npos) {
    actualID.mip_maps_ = true;
  }
  if (sHintString.find("nomipmaps") != std::string::npos) {
    actualID.mip_maps_ = false;  // check for "nomipmaps" after "mipmaps"
  }

  /* If the image is marked grayscale, then use all bits not used for alpha
   * for the intensity.  This way, if an image has no alpha, you get an 8-bit
   * grayscale; if it only has boolean transparency, you get a 7-bit grayscale.
   */
  if (sHintString.find("grayscale") != std::string::npos) {
    actualID.grayscale_bits_ = 8 - actualID.alpha_bits_;
  }

  /* This indicates that the only component in the texture is alpha; assume all
   * color is white. */
  if (sHintString.find("alphamap") != std::string::npos) {
    actualID.grayscale_bits_ = 0;
  }

  /* No iGrayscaleBits for images that are already paletted.  We don't support
   * that; and that hint is intended for use on images that are already
   * grayscale, it's not intended to change a color image into a grayscale
   * image. */
  if (actualID.grayscale_bits_ != -1 && pImg->format_->bits_per_pixel_ == 8) {
    actualID.grayscale_bits_ = -1;
  }

  // To avoid the overhead of querying the hardware's maximum texture size
  // every time a texture is loaded, we can cache the value returned by
  // DISPLAY->GetMaxTextureSize() in RageBitmapTexture::Create(). This value
  // remains consistent throughout runtime. For example, when using OpenGL,
  // this is the value of GL_MAX_TEXTURE_SIZE. --sukibaby
  static const int iMaxTextureSize = DISPLAY->GetMaxTextureSize();

  /* Cap the max texture size to the hardware max. */
  actualID.max_size_ = std::min(actualID.max_size_, iMaxTextureSize);

  /* Save information about the source. */
  source_width_ = pImg->w_;
  source_height_ = pImg->h_;

  /* in-game image dimensions are the same as the source graphic */
  image_width_ = source_width_;
  image_height_ = source_height_;

  /* if "doubleres" (high resolution) and we're not allowing high res textures,
   * then image dimensions are half of the source */
  if (sHintString.find("doubleres") != std::string::npos) {
    if (!StepMania::GetHighResolutionTextures()) {
      image_width_ = image_width_ / 2;
      image_height_ = image_height_ / 2;
    }
  }

  /* image size cannot exceed max size */
  image_width_ = std::min(image_width_, actualID.max_size_);
  image_height_ = std::min(image_height_, actualID.max_size_);

  /* Texture dimensions need to be a power of two; jump to the next. */
  texture_width_ = power_of_two(image_width_);
  texture_height_ = power_of_two(image_height_);

  /* If we're under 8x8, increase it, to avoid filtering problems on odd
   * hardware. */
  if (texture_width_ < 8 || texture_height_ < 8) {
    actualID.stretch_ = true;
    texture_width_ = std::max(8, texture_width_);
    texture_height_ = std::max(8, texture_height_);
  }

  ASSERT_M(
      texture_width_ <= actualID.max_size_,
      ssprintf("w %i, %i", texture_width_, actualID.max_size_));
  ASSERT_M(
      texture_height_ <= actualID.max_size_,
      ssprintf("h %i, %i", texture_height_, actualID.max_size_));

  if (actualID.stretch_) {
    /* The hints asked for the image to be stretched to the texture size,
     * probably for tiling. */
    image_width_ = texture_width_;
    image_height_ = texture_height_;
  }

  if (pImg->w_ != image_width_ || pImg->h_ != image_height_) {
    RageSurfaceUtils::Zoom(pImg, image_width_, image_height_);
  }

  if (actualID.grayscale_bits_ != -1 &&
      DISPLAY->SupportsTextureFormat(RagePixelFormat_PAL)) {
    RageSurface* pGrayscale = RageSurfaceUtils::PalettizeToGrayscale(
        pImg, actualID.grayscale_bits_, actualID.alpha_bits_);

    delete pImg;
    pImg = pGrayscale;
  }

  // Figure out which texture format we want the renderer to use.
  RagePixelFormat pixfmt;

  // If the source is palleted, always load as paletted if supported.
  if (pImg->format_->bits_per_pixel_ == 8 &&
      DISPLAY->SupportsTextureFormat(RagePixelFormat_PAL)) {
    pixfmt = RagePixelFormat_PAL;
  } else {
    // not paletted
    switch (actualID.color_depth_) {
      case 16: {
        // Bits of alpha in the source:
        int iSourceAlphaBits = 8 - pImg->format_->loss_[3];

        // Don't use more than we were hinted to.
        iSourceAlphaBits = std::min(actualID.alpha_bits_, iSourceAlphaBits);

        switch (iSourceAlphaBits) {
          case 0:
          case 1:
            pixfmt = RagePixelFormat_RGB5A1;
            break;
          default:
            pixfmt = RagePixelFormat_RGBA4;
            break;
        }
      } break;
      case 32:
        pixfmt = RagePixelFormat_RGBA8;
        break;
      default:
        FAIL_M(ssprintf("%i", actualID.color_depth_));
    }
  }

  // Make we're using a supported format. Every card supports either RGBA8 or
  // RGBA4.
  if (!DISPLAY->SupportsTextureFormat(pixfmt)) {
    pixfmt = RagePixelFormat_RGBA8;
    if (!DISPLAY->SupportsTextureFormat(pixfmt)) {
      pixfmt = RagePixelFormat_RGBA4;
    }
  }

  /* Dither if appropriate.
   * XXX: This is a special case: don't bother dithering to RGBA8888.
   * We actually want to dither only if the destination has greater color depth
   * on at least one color channel than the source. For example, it doesn't
   * make sense to do this when pixfmt is RGBA5551 if the image is only RGBA555.
   */
  if (actualID.dither_ &&
      (pixfmt == RagePixelFormat_RGBA4 || pixfmt == RagePixelFormat_RGB5A1)) {
    // Dither down to the destination format.
    const RageDisplay::RagePixelFormatDesc* pfd =
        DISPLAY->GetPixelFormatDesc(pixfmt);
    RageSurface* dst = CreateSurface(
        pImg->w_, pImg->h_, pfd->bpp_, pfd->masks_[0], pfd->masks_[1],
        pfd->masks_[2], pfd->masks_[3]);

    RageSurfaceUtils::ErrorDiffusionDither(pImg, dst);
    delete pImg;
    pImg = dst;
  }

  /* This needs to be done *after* the final resize, since that resize
   * may introduce new alpha bits that need to be set.  It needs to be
   * done *before* we set up the palette, since it might change it. */
  RageSurfaceUtils::FixHiddenAlpha(pImg);

  /* Scale up to the texture size, if needed. */
  RageSurfaceUtils::ConvertSurface(
      pImg, texture_width_, texture_height_, pImg->fmt_.bits_per_pixel_,
      pImg->fmt_.mask_[0], pImg->fmt_.mask_[1], pImg->fmt_.mask_[2],
      pImg->fmt_.mask_[3]);

  tex_handle_ = DISPLAY->CreateTexture(pixfmt, pImg, actualID.mip_maps_);

  CreateFrameRects();

  delete pImg;

  // Check for hints that override the apparent "size".
  GetResolutionFromFileName(actualID.filename_, source_width_, source_height_);

  /* if "doubleres" (high resolution) then we want the image to appear in-game
   * with dimensions 1/2 of the source. So, cut down the source dimension here
   * after everything above is finished operating with the real image
   * source dimensions. */
  if (sHintString.find("doubleres") != std::string::npos) {
    source_width_ = source_width_ / 2;
    source_height_ = source_height_ / 2;
  }

  std::string sProperties;
  sProperties += RagePixelFormatToString(pixfmt) + " ";
  if (actualID.alpha_bits_ == 0) {
    sProperties += "opaque ";
  }
  if (actualID.alpha_bits_ == 1) {
    sProperties += "matte ";
  }
  if (actualID.stretch_) {
    sProperties += "stretch ";
  }
  if (actualID.dither_) {
    sProperties += "dither ";
  }
  sProperties.erase(sProperties.size() - 1);
  // LOG->Trace( "RageBitmapTexture: Loaded '%s' (%ux%u); %s, source %d,%d;
  // image %d,%d.", 	actualID.filename.c_str(), GetTextureWidth(),
  // GetTextureHeight(), 	sProperties.c_str(), m_iSourceWidth,
  // m_iSourceHeight, 	m_iImageWidth, m_iImageHeight );
}

void RageBitmapTexture::Destroy() { DISPLAY->DeleteTexture(tex_handle_); }

/*
 * Copyright (c) 2001-2004 Chris Danford, Glenn Maynard
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
