#include "Utils.h"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "stb_image_write.h"

Surface::Surface(const Surface& cpy) {
  iWidth = cpy.iWidth;
  iHeight = cpy.iHeight;
  iPitch = cpy.iPitch;
  if (cpy.pRGBA) {
    pRGBA = new unsigned char[iHeight * iPitch];
  } else {
    pRGBA = NULL;
  }
}

void BitmapToSurface(HBITMAP hBitmap, Surface* pSurf) {
  HDC hDC = CreateCompatibleDC(NULL);
  HGDIOBJ hOldBitmap = SelectObject(hDC, hBitmap);

  BITMAPINFO bi;
  std::memset(&bi, 0, sizeof(bi));
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
  GetDIBits(hDC, hBitmap, 0, 0, NULL, &bi, DIB_RGB_COLORS);

  pSurf->iWidth = bi.bmiHeader.biWidth;
  pSurf->iHeight = bi.bmiHeader.biHeight;
  pSurf->iPitch = bi.bmiHeader.biWidth * 4;
  pSurf->pRGBA =
      (unsigned char*)new unsigned char[pSurf->iWidth * pSurf->iHeight * 4];

  bi.bmiHeader.biHeight = -bi.bmiHeader.biHeight;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  bi.bmiHeader.biSizeImage = pSurf->iHeight * pSurf->iWidth * 4;
  // LONG   biXPelsPerMeter;
  // LONG   biYPelsPerMeter;
  //  DWORD  biClrUsed;
  //  DWORD  biClrImportant;

  if (!GetDIBits(
          hDC, hBitmap, 0, pSurf->iHeight, pSurf->pRGBA, &bi, DIB_RGB_COLORS)) {
    int q = 1;
  }

  SelectObject(hDC, hOldBitmap);
  DeleteDC(hDC);
}

void GrayScaleToAlpha(
    Surface* pSurf)  // void *pImg, int iWidth, int iHeight, int iPitch )
{
  char* p = (char*)pSurf->pRGBA;
  for (int iY = 0; iY < pSurf->iHeight; ++iY) {
    unsigned* pos = (unsigned*)p;
    for (int iX = 0; iX < pSurf->iWidth; ++iX) {
      unsigned pixel = pos[iX];
      pixel <<= 24;
      pixel |= 0x00FFFFFF;
      pos[iX] = pixel;
    }
    p += pSurf->iPitch;
  }
}

void GetBounds(const Surface* pSurf, RECT* out) {
  out->left = out->right = out->top = out->bottom = 0;

  int FirstCol = 9999, LastCol = 0, FirstRow = 9999, LastRow = 0;
  const char* p = (const char*)pSurf->pRGBA;
  for (int row = 0; row < pSurf->iHeight; ++row) {
    const unsigned* pos = (const unsigned*)p;
    for (int col = 0; col < pSurf->iWidth; ++col) {
      unsigned pixel = pos[col];
      if ((pixel & 0xFFFFFF) == 0) {
        continue; /* black */
      }

      FirstCol = std::min(FirstCol, col);
      LastCol = std::max(LastCol, col);
      FirstRow = std::min(FirstRow, row);
      LastRow = std::max(LastRow, row);
    }
    p += pSurf->iPitch;
  }

  if (FirstCol != 9999 && FirstRow != 9999) {
    out->left = FirstCol;
    out->top = FirstRow;
    out->right = LastCol + 1;
    out->bottom = LastRow + 1;
  }
}

namespace {
struct StbWriteState {
  FILE* file;
  char* error;
  bool failed;
};

void FileWriteCallback(void* context, void* data, int size) {
  StbWriteState* state = (StbWriteState*)context;
  if (state->failed || size <= 0) {
    return;
  }

  size_t wrote = std::fwrite(data, 1, static_cast<size_t>(size), state->file);
  if (wrote == static_cast<size_t>(size)) {
    return;
  }

  state->failed = true;
  std::snprintf(state->error, 1024, "%s", std::strerror(errno));
}
}  // namespace

bool SavePNG(FILE* f, char szErrorbuf[1024], const Surface* pSurf) {
  szErrorbuf[0] = 0;

  if (f == NULL) {
    std::snprintf(szErrorbuf, 1024, "%s", "file is not open");
    return false;
  }

  StbWriteState state;
  state.file = f;
  state.error = szErrorbuf;
  state.failed = false;

  int previous_compression = stbi_write_png_compression_level;
  stbi_write_png_compression_level = 1;

  int wrote = stbi_write_png_to_func(
      FileWriteCallback, &state, pSurf->iWidth, pSurf->iHeight, 4, pSurf->pRGBA,
      pSurf->iPitch);

  stbi_write_png_compression_level = previous_compression;

  if (state.failed) {
    return false;
  }

  if (wrote == 0) {
    std::snprintf(
        szErrorbuf, 1024, "%s", "stb_image_write failed to write PNG");
    return false;
  }

  if (std::fflush(f) != 0) {
    std::snprintf(szErrorbuf, 1024, "%s", std::strerror(errno));
    return false;
  }

  return true;
}

/*
 * Copyright (c) 2003-2007 Glenn Maynard
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
