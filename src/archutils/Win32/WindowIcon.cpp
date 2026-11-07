#include "archutils/Win32/WindowIcon.h"

#include <wingdi.h>

#include <cstdint>
#include <string>

#include "RageLog.h"
#include "RageSurface.h"
#include "RageSurfaceUtils.h"
#include "RageSurface_Load.h"
#include "RageUtil.h"
#include "archutils/Win32/ErrorStrings.h"
#include "global.h"

HICON IconFromSurface(const RageSurface* src_img) {
  RageSurface* img;

  {
    /* Round the width up to a multiple of 8, convert to 32-bit BGR, and reduce
     * to one-bit alpha. */
    int width = src_img->w_;
    width = (width + 7) & ~7;

    img = CreateSurface(
        width, src_img->h_, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
    RageSurfaceUtils::Blit(src_img, img);
  }

  RageSurfaceUtils::FlipVertically(img);

  int size = sizeof(BITMAPINFOHEADER);
  int size_image = 0;
  size_image += img->h_ * img->pitch_;   /* image */
  size_image += (img->h_ * img->w_) / 8; /* mask */

  BITMAPINFOHEADER* bitmap = (BITMAPINFOHEADER*)malloc(size + size_image);
  memset(bitmap, 0, size + size_image);

  bitmap->biSize = sizeof(BITMAPINFOHEADER);
  bitmap->biWidth = img->w_;
  bitmap->biHeight = img->h_ * 2;
  bitmap->biPlanes = 1;
  bitmap->biBitCount = 32;
  bitmap->biCompression = BI_RGB;
  bitmap->biSizeImage = img->h_ * img->pitch_;

  uint8_t* image = ((uint8_t*)bitmap) + size;
  uint8_t* mask = image + img->h_ * img->pitch_;

  memcpy(image, img->pixels_, img->h_ * img->pitch_);

  int mask_pitch = img->w_ / 8;
  for (int y = 0; y < img->h_; ++y) {
    int bit = 0x80;
    uint32_t* row = (uint32_t*)(image + y * img->pitch_);
    uint8_t* mask_row = mask + y * mask_pitch;
    for (int x = 0; x < img->w_; ++x) {
      if (!(row[x] & img->fmt_.mask_[3])) {
        /* Transparent; set this mask bit. */
        *mask_row |= bit;
        row[x] = 0;
      }

      bit >>= 1;
      if (bit == 0) {
        bit = 0x80;
        ++mask_row;
      }
    }
  }

  HICON icon = CreateIconFromResourceEx(
      (BYTE*)bitmap, size + size_image, TRUE, 0x00030000, img->w_, img->h_,
      LR_DEFAULTCOLOR);

  delete img;
  img = nullptr;
  free(bitmap);

  if (icon == nullptr) {
    LOG->Trace(
        "%s",
        werr_ssprintf(GetLastError(), "CreateIconFromResourceEx").c_str());
    return nullptr;
  }

  return icon;
}

HICON IconFromFile(const std::string& icon_file) {
  std::string error;
  RageSurface* img = RageSurfaceUtils::LoadFile(icon_file, error);
  if (img == nullptr) {
    LOG->Warn(
        "Couldn't open icon \"%s\": %s", icon_file.c_str(), error.c_str());
    return nullptr;
  }

  HICON icon = IconFromSurface(img);
  delete img;
  return icon;
}

/*
 * (c) 2004 Glenn Maynard
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
