/** @brief Font - stores a font, used by BitmapText. */

#ifndef FONT_H
#define FONT_H

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "RageTexture.h"
#include "RageTypes.h"

class FontPage;
class RageTexture;
class IniFile;

/** @brief The textures used by the font. */
struct FontPageTextures {
  /** @brief The primary texture drawn underneath Main. */
  RageTexture* texture_main_;
  /** @brief an optional texture drawn underneath Main.
   *
   * This can help to acheive complicated layer styles. */
  RageTexture* texture_stroke_;

  /** @brief Set up the initial textures. */
  FontPageTextures() : texture_main_(nullptr), texture_stroke_(nullptr) {}

  bool operator==(const struct FontPageTextures& other) const {
    return texture_main_ == other.texture_main_ &&
           texture_stroke_ == other.texture_stroke_;
  }

  bool operator!=(const struct FontPageTextures& other) const {
    return !operator==(other);
  }
};

/** @brief The components of a glyph (not technically a character). */
struct glyph {
  /** @brief the FontPage that is needed. */
  FontPage* page_;
  /** @brief the textures for the glyph. */
  FontPageTextures font_page_textures_;
  FontPageTextures* GetFontPageTextures() const {
    return const_cast<FontPageTextures*>(&font_page_textures_);
  }

  /** @brief Number of pixels to advance horizontally after drawing this
   * character. */
  int hadvance_;

  /** @brief Width of the actual rendered character. */
  float width_;
  /** @brief Height of the actual rendered character. */
  float height_;

  /** @brief Number of pixels to offset this character when rendering. */
  float hshift_;  // , m_fVshift;

  /** @brief Texture coordinate rect. */
  RectF tex_rect_;

  /** @brief Set up the glyph with default values. */
  glyph()
      : page_(nullptr),
        font_page_textures_(),
        hadvance_(0),
        width_(0),
        height_(0),
        hshift_(0),
        tex_rect_() {}
};

/** @brief The settings used for the FontPage. */
struct FontPageSettings {
  std::string texture_path_;

  int draw_extra_pixels_left_, draw_extra_pixels_right_, add_to_all_widths_,
      line_spacing_, top_, baseline_, default_width_, advance_extra_pixels_;
  float scale_all_widths_by_;
  std::string texture_hints_;

  std::map<wchar_t, int> char_to_glyph_no_;
  // If a value is missing, the width of the texture frame is used.
  std::map<int, int> map_glyph_widths_;

  /** @brief The initial settings for the FontPage. */
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
        map_glyph_widths_() {}

  /**
   * @brief Map a range from a character map to glyphs.
   * @param sMapping the intended mapping.
   * @param iMapOffset the number of maps to offset.
   * @param iGlyphOffset the number of glyphs to offset.
   * @param iCount the range to map. If -1, the range is the entire map.
   * @return the empty string on success, or an error message on failure. */
  std::string MapRange(
      std::string mapping, int map_offset, int glyph_offset, int count);
};

class FontPage {
 public:
  FontPage();
  ~FontPage();

  void Load(const FontPageSettings& cfg);

  // Page-global properties.
  int height_;
  int line_spacing_;
  float vshift_;
  int GetCenter() const { return height_ / 2; }

  // Remember these only for GetLineWidthInSourcePixels.
  int draw_extra_pixels_left_, draw_extra_pixels_right_;

  FontPageTextures font_page_textures_;

  // XXX: remove?
  std::string texture_path_;

  /** @brief All glyphs in this list will point to m_pTexture. */
  std::vector<glyph> glyphs_;

  std::map<wchar_t, int> char_to_glyph_no_;

 private:
  void SetExtraPixels(int draw_extra_pixels_left, int draw_extra_pixels_right);
  void SetTextureCoords(
      const std::vector<int>& widths, int advance_extra_pixels);
};

class Font {
 public:
  int ref_count_;
  std::string path_;

  Font();
  ~Font();

  const glyph& GetGlyph(wchar_t c) const;

  int GetLineWidthInSourcePixels(const std::wstring& line) const;
  size_t GetGlyphsThatFit(const std::wstring& line, int* width) const;

  bool FontCompleteForString(const std::wstring& str) const;

  /**
   * @brief Add a FontPage to this font.
   * @param fp the FontPage to be added.
   */
  void AddPage(FontPage* fp);

  /**
   * @brief Steal all of a font's pages.
   * @param f the font whose pages we are stealing. */
  void MergeFont(Font& f);

  void Load(const std::string& font_or_texture_file_path, std::string chars);
  void Unload();

  // Load font-wide settings.
  void CapsOnly();

  int GetHeight() const { return default_->height_; }
  int GetCenter() const { return default_->GetCenter(); }
  int GetLineSpacing() const { return default_->line_spacing_; }

  void SetDefaultGlyph(FontPage* page);

  bool IsRightToLeft() const { return right_to_left_; };
  bool IsDistanceField() const { return distance_field_; };
  const RageColor& GetDefaultStrokeColor() const {
    return default_stroke_color_;
  };

 private:
  /** @brief List of pages and fonts that we use (and are responsible for
   * freeing). */
  std::vector<FontPage*> pages_;

  /**
   * @brief This is the primary fontpage of this font.
   *
   * The font-wide height, center, etc. is pulled from it.
   * (This is one of pages[].) */
  FontPage* default_;

  /** @brief Map from characters to glyphs. */
  std::map<wchar_t, glyph*> char_to_glyph_;
  /** @brief Each glyph is part of one of the pages[]. */
  glyph* char_to_glyph_cache_[128];

  /**
   * @brief True for Hebrew, Arabic, Urdu fonts.
   *
   * This will also change the way glyphs from the default FontPage are
   * rendered. There may be a better way to handle this. */
  bool right_to_left_;

  bool distance_field_;

  RageColor default_stroke_color_;

  /** @brief We keep this around only for reloading. */
  std::string chars_;

  void LoadFontPageSettings(
      FontPageSettings& cfg, IniFile& ini, const std::string& texture_path,
      const std::string& page_name, std::string chars);
  static void GetFontPaths(
      const std::string& font_or_texture_file_path,
      std::vector<std::string>& texture_paths);
  std::string GetPageNameFromFileName(const std::string& filename);

  Font(const Font& rhs);
  Font& operator=(const Font& rhs);
};

/**
 * @brief Last private-use Unicode character:
 *
 * This is in the header to reduce file dependencies. */
const wchar_t kFontDefaultGlyph = 0xF8FF;

#endif

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
