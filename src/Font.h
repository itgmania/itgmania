#ifndef FONT_H
#define FONT_H

#include <cstddef>
#include <map>
#include <vector>

#include "IniFile.h"
#include "RageTexture.h"
#include "RageTextureID.h"
#include "RageTypes.h"
#include "RageUtil.h"

// TODO(teejusb): Move this to its own file.
class FontPage;

// The textures used by the font.
struct FontPageTextures {
  // The primary texture drawn underneath Main.
  RageTexture* texture_main_;
  // An optional texture drawn underneath Main.
  // This can help to acheive complicated layer styles.
  RageTexture* texture_stroke_;

  // Set up the initial textures.
  FontPageTextures() : texture_main_(nullptr), texture_stroke_(nullptr) {}

  bool operator==(const struct FontPageTextures& other) const {
    return texture_main_ == other.texture_main_ &&
           texture_stroke_ == other.texture_stroke_;
  }

  bool operator!=(const struct FontPageTextures& other) const {
    return !operator==(other);
  }
};

// The components of a glyph (not technically a character).
struct glyph {
  // the FontPage that is needed.
  FontPage* page_;
  // the textures for the glyph.
  FontPageTextures font_page_textures_;
  FontPageTextures* GetFontPageTextures() const {
    return const_cast<FontPageTextures*>(&font_page_textures_);
  }

  // Number of pixels to advance horizontally after drawing this character.
  int horiz_advance_;

  // Width of the actual rendered character.
  float width_;
  // Height of the actual rendered character.
  float height_;

  // Number of pixels to offset this character when rendering.
  float horiz_shift_;

  // Texture coordinate rect.
  RectF texture_rect_;

  // Set up the glyph with default values.
  glyph()
      : page_(nullptr),
        font_page_textures_(),
        horiz_advance_(0),
        width_(0),
        height_(0),
        horiz_shift_(0),
        texture_rect_() {}
};

// The settings used for the FontPage.
struct FontPageSettings {
  RString texture_path_;

  int draw_extra_pixels_left_;
	int draw_extra_pixels_right_;
	int add_to_all_widths_;
  int line_spacing_;
	int top_;
	int baseline_;
	int default_width_;
  int advance_extra_pixels_;
  float scale_all_widths_by_;
  RString texture_hints_;

  std::map<wchar_t, int> char_to_glyph_no_;
  // If a value is missing, the width of the texture frame is used.
  std::map<int, int> glyph_widths_map_;

  // The initial settings for the FontPage.
  FontPageSettings()
      : texture_path_(""),
        draw_extra_pixels_left_(0),
        draw_extra_pixels_right_(0),
        add_to_all_widths_(0),
        line_spacing_(-1),
        top_(-1),
        baseline_(-1),
        default_width_(-1),
        advance_extra_pixels_(1),
        scale_all_widths_by_(1),
        texture_hints_("default"),
        char_to_glyph_no_(),
        glyph_widths_map_() {}

   // Map a range from a character map to glyphs.
   // - mapping, the intended mapping.
   // - map_offset, the number of maps to offset.
   // - glyph_offset, the number of glyphs to offset.
   // - count, the range to map. If -1, the range is the entire map.
   // Returns the empty string on success, or an error message on failure.
  RString MapRange(
      RString mapping, int map_offset, int glyph_offset, int count);
};

class FontPage {
 public:
  FontPage();
  ~FontPage();

  void Load(const FontPageSettings& cfg);

  // Page-global properties.
  int height_;
  int line_spacing_;
  float vert_shift_;
  int GetCenter() const { return height_ / 2; }

  // Remember these only for GetLineWidthInSourcePixels.
  int draw_extra_pixels_left_;
	int draw_extra_pixels_right_;

  FontPageTextures font_page_textures_;

  // TODO: remove?
  RString texture_path_;

  // All glyphs in this list will point to m_pTexture.
  std::vector<glyph> glyphs_;

  std::map<wchar_t, int> char_to_glyph_no_;

 private:
  void SetExtraPixels(int draw_extra_pixels_left, int draw_extra_pixels_right);
  void SetTextureCoords(
      const std::vector<int>& widths, int advance_extra_pixels);
};

// Stores a font, used by BitmapText.
class Font {
 public:
  int ref_count_;
  RString path_;

  Font();
  ~Font();

  const glyph& GetGlyph(wchar_t c) const;

  int GetLineWidthInSourcePixels(const std::wstring& line) const;
  int GetLineHeightInSourcePixels(const std::wstring& line) const;
  std::size_t GetGlyphsThatFit(const std::wstring& line, int* width) const;

  bool FontCompleteForString(const std::wstring& str) const;

  // Add a FontPage to this font.
  void AddPage(FontPage* font_page);

  // Steal all of a font's pages.
  void MergeFont(Font& font);

  void Load(const RString& font_or_texture_file_path, RString chars);
  void Unload();
  void Reload();

  // Load font-wide settings.
  void CapsOnly();

  int GetHeight() const { return default_font_page_->height_; }
  int GetCenter() const { return default_font_page_->GetCenter(); }
  int GetLineSpacing() const { return default_font_page_->line_spacing_; }

  void SetDefaultGlyph(FontPage* font_page);

  bool IsRightToLeft() const { return right_to_left_; };
  bool IsDistanceField() const { return distance_field_; };
  const RageColor& GetDefaultStrokeColor() const {
    return default_stroke_color_;
  };

 private:
  // List of pages and fonts that we use (and are responsible for freeing).
  std::vector<FontPage*> pages_;

  // This is the primary fontpage of this font.
  // The font-wide height, center, etc. is pulled from it.
  // (This is one of pages[].)
  FontPage* default_font_page_;

  // Map from characters to glyphs.
  std::map<wchar_t, glyph*> char_to_glyph_map_;
  // Each glyph is part of one of the pages[].
  glyph* char_to_glyph_cache_[128];

  // True for Hebrew, Arabic, Urdu fonts.
  // This will also change the way glyphs from the default FontPage are
  // rendered. There may be a better way to handle this.
  bool right_to_left_;

  bool distance_field_;

  RageColor default_stroke_color_;

  // We keep this around only for reloading.
  RString chars_;

  void LoadFontPageSettings(
      FontPageSettings& cfg, IniFile& ini, const RString& texture_path,
      const RString& page_name, RString chars);
  static void GetFontPaths(
      const RString& font_or_texture_file_path,
      std::vector<RString>& texture_paths);
  RString GetPageNameFromFileName(const RString& filename);

  Font(const Font& rhs);
  Font& operator=(const Font& rhs);
};

// Last private-use Unicode character:
// This is in the header to reduce file dependencies. 
const wchar_t FONT_DEFAULT_GLYPH = 0xF8FF;

#endif  // FONT_H

/**
 * @file
 * @author Glenn Maynard, Chris Danford (c) 2001-2004
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
