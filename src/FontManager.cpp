#include "global.h"

#include "FontManager.h"

#include <map>

#include "Font.h"
#include "RageLog.h"
#include "RageUtil.h"

// Global and accessible from anywhere in our program.
FontManager* FONT = nullptr;

// Map from file name to a texture holder.
typedef std::pair<RString, RString> FontName;
static std::map<FontName, Font*> kPathToFontMap;

FontManager::FontManager() {}

FontManager::~FontManager() {
  for (auto it = kPathToFontMap.begin(); it != kPathToFontMap.end(); ++it) {
    const FontName& font_name = it->first;
    Font* font = it->second;
    if (font->ref_count_ > 0) {
      LOG->Trace(
          "FONT LEAK: '%s', RefCount = %d.", font_name.first.c_str(),
          font->ref_count_);
    }
    delete font;
  }
}

Font* FontManager::LoadFont(
    const RString& font_or_texture_file_path, RString chars) {
  // Convert the path to lowercase so that we don't load duplicates. Really,
  // this does not solve the duplicate problem. We could have two copies of
  // the same bitmap if there are equivalent but different paths
  // (e.g. "graphics\blah.png" and "..\stepmania\graphics\blah.png" ).
  CHECKPOINT_M(ssprintf(
      "FontManager::LoadFont(%s).", font_or_texture_file_path.c_str()));
  const FontName new_name(font_or_texture_file_path, chars);
  auto it = kPathToFontMap.find(new_name);
  if (it != kPathToFontMap.end()) {
    Font* font = it->second;
    font->ref_count_++;
    return font;
  }

  Font* font = new Font;
  font->Load(font_or_texture_file_path, chars);
  kPathToFontMap[new_name] = font;
  return font;
}

Font* FontManager::CopyFont(Font* font) {
  ++font->ref_count_;
  return font;
}

void FontManager::UnloadFont(Font* font) {
  CHECKPOINT_M(ssprintf("FontManager::UnloadFont(%s).", font->path_.c_str()));

  for (auto it = kPathToFontMap.begin(); it != kPathToFontMap.end(); ++it) {
    if (it->second != font) {
      continue;
    }

    ASSERT_M(
        font->ref_count_ > 0,
        "Attempting to unload a font with zero ref count!");

    it->second->ref_count_--;

    if (font->ref_count_ == 0) {
			// Free the texture...
      delete it->second;
			// ...and remove the key in the map
      kPathToFontMap.erase(it);
    }
    return;
  }

  FAIL_M(ssprintf("Unloaded an unknown font (%p)", static_cast<void*>(font)));
}

/*
 * (c) 2001-2003 Chris Danford, Glenn Maynard
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
