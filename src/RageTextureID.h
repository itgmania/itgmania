/* RageTextureID - An identifier for a texture and associated loading
 * parameters. */

#ifndef RAGE_TEXTURE_ID_H
#define RAGE_TEXTURE_ID_H

#include <string>

/* A unique texture is identified by a RageTextureID.  (Loading the
 * same file with two different dither settings is considered two
 * different textures, for example.)  See RageTexture.cpp for explanations
 * of these. */
struct RageTextureID {
  std::string filename_;

  // Maximum size of the texture, per dimension.
  int max_size_;

  // Generate mipmaps for this texture
  bool mip_maps_;

  /* Maximum number of bits for alpha.  In 16-bit modes, lowering
   * this gives more bits for color values. (0, 1 or 4) */
  int alpha_bits_;

  /* If this is greater than -1, then the image will be loaded as a luma/alpha
   * map, eg. I4A4.  At most 8 bits per pixel will be used  This only actually
   * happens when paletted textures are supported.
   *
   * If the sum of alpha and grayscale bits is <= 4, and the system supports
   * 4-bit palettes, then the image will be loaded with 4bpp.
   *
   * This may be set to 0, resulting in an alpha map with all pixels white. */
  int grayscale_bits_;

  /* Preferred color depth of the image. (This is overridden for
   * paletted images and transparencies.)  -1 for default. */
  int color_depth_;

  // If true and color precision is being lost, dither. (slow)
  bool dither_;

  // If true, resize the image to fill the internal texture. (slow)
  bool stretch_;

  /* If true, enable HOT PINK color keying. (deprecated but needed for
   * banners) */
  bool hot_pink_color_key_;  // #FF00FF

  // These hints will be used in addition to any in the filename.
  std::string additional_texture_hints_;

  /* Used by RageTextureManager. Order is important; see RageTextureManager.cpp.
   * Note that this property is not considered for ordering/equality. Loading
   * a texture with a different loading policy will reuse the same texture with
   * a different policy. */
  enum TexPolicy { TEX_VOLATILE, TEX_DEFAULT } policy_;

  void Init();

  RageTextureID()
      : filename_(std::string()),
        max_size_(0),
        mip_maps_(false),
        alpha_bits_(0),
        grayscale_bits_(0),
        color_depth_(0),
        dither_(false),
        stretch_(false),
        hot_pink_color_key_(false),
        additional_texture_hints_(std::string()),
        policy_(TEX_DEFAULT) {
    Init();
  }
  RageTextureID(const std::string& fn)
      : filename_(std::string()),
        max_size_(0),
        mip_maps_(false),
        alpha_bits_(0),
        grayscale_bits_(0),
        color_depth_(0),
        dither_(false),
        stretch_(false),
        hot_pink_color_key_(false),
        additional_texture_hints_(std::string()),
        policy_(TEX_DEFAULT) {
    Init();
    SetFilename(fn);
  }
  void SetFilename(const std::string& fn);
};

inline bool operator==(const RageTextureID& lhs, const RageTextureID& rhs) {
#define EQUAL(a) (lhs.a == rhs.a)
  return EQUAL(filename_) && EQUAL(max_size_) && EQUAL(mip_maps_) &&
         EQUAL(alpha_bits_) && EQUAL(grayscale_bits_) && EQUAL(color_depth_) &&
         EQUAL(dither_) && EQUAL(stretch_) && EQUAL(hot_pink_color_key_) &&
         EQUAL(additional_texture_hints_);
  // EQUAL(Policy); // don't do this
#undef EQUAL
}

inline bool operator!=(const RageTextureID& lhs, const RageTextureID& rhs) {
  return !operator==(lhs, rhs);
}

inline bool operator<(const RageTextureID& lhs, const RageTextureID& rhs) {
#define COMP(a)                   \
  if (lhs.a < rhs.a) return true; \
  if (lhs.a > rhs.a) return false;
  COMP(filename_);
  COMP(max_size_);
  COMP(mip_maps_);
  COMP(alpha_bits_);
  COMP(grayscale_bits_);
  COMP(color_depth_);
  COMP(dither_);
  COMP(stretch_);
  COMP(hot_pink_color_key_);
  COMP(additional_texture_hints_);
  // COMP(Policy); // don't do this
#undef COMP
  return false;
}

inline bool operator>(const RageTextureID& lhs, const RageTextureID& rhs) {
  return operator<(rhs, lhs);
}
inline bool operator<=(const RageTextureID& lhs, const RageTextureID& rhs) {
  return !operator<(rhs, lhs);
}
inline bool operator>=(const RageTextureID& lhs, const RageTextureID& rhs) {
  return !operator<(lhs, rhs);
}

#endif

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
