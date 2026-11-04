#include "RageTextureID.h"

#include <string>

#include "RageTextureManager.h"
#include "RageUtil.h"

void RageTextureID::Init() {
  max_size_ = 2048;
  mip_maps_ = false;  // Most sprites (especially text) look worse with mip maps
  alpha_bits_ = 4;
  grayscale_bits_ = -1;
  dither_ = false;
  stretch_ = false;
  color_depth_ = -1;  // default
  hot_pink_color_key_ = false;
  additional_texture_hints_ = "";
  policy_ = TEXTUREMAN->GetDefaultTexturePolicy();
}

void RageTextureID::SetFilename(const std::string& fn) {
  filename_ = fn;
  CollapsePath(filename_);
}

/*
 * Copyright (c) 2003-2004 Chris Danford, Glenn Maynard
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
