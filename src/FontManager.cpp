#include "FontManager.h"

#include <map>
#include <string>
#include <utility>

#include "Font.h"
#include "RageLog.h"
#include "RageThreads.h"
#include "RageUtil.h"
#include "global.h"

FontManager* FONT =
    nullptr;  // global and accessible from anywhere in our program

// map from file name to a texture holder
typedef std::pair<std::string, std::string> FontName;
static std::map<FontName, Font*> map_path_to_font;

FontManager::FontManager() {}

FontManager::~FontManager() {
  for (std::map<FontName, Font*>::iterator i = map_path_to_font.begin();
       i != map_path_to_font.end(); ++i) {
    const FontName& fn = i->first;
    Font* font = i->second;
    if (font->ref_count_ > 0) {
      LOG->Trace(
          "FONT LEAK: '%s', RefCount = %d.", fn.first.c_str(),
          font->ref_count_);
    }
    delete font;
  }
}

Font* FontManager::LoadFont(
    const std::string& font_or_texture_file_path, std::string chars) {
  Font* font;
  /* Convert the path to lowercase so that we don't load duplicates. Really,
   * this does not solve the duplicate problem. We could have two copies of
   * the same bitmap if there are equivalent but different paths
   * (e.g. "graphics\blah.png" and "..\stepmania\graphics\blah.png" ). */

  CHECKPOINT_M(ssprintf(
      "FontManager::LoadFont(%s).", font_or_texture_file_path.c_str()));
  const FontName new_name(font_or_texture_file_path, chars);
  std::map<FontName, Font*>::iterator p = map_path_to_font.find(new_name);
  if (p != map_path_to_font.end()) {
    font = p->second;
    font->ref_count_++;
    return font;
  }

  Font* f = new Font;
  f->Load(font_or_texture_file_path, chars);
  map_path_to_font[new_name] = f;
  return f;
}

Font* FontManager::CopyFont(Font* font) {
  ++font->ref_count_;
  return font;
}

void FontManager::UnloadFont(Font* fp) {
  CHECKPOINT_M(ssprintf("FontManager::UnloadFont(%s).", fp->path_.c_str()));

  for (std::map<FontName, Font*>::iterator i = map_path_to_font.begin();
       i != map_path_to_font.end(); ++i) {
    if (i->second != fp) {
      continue;
    }

    ASSERT_M(
        fp->ref_count_ > 0, "Attempting to unload a font with zero ref count!");

    i->second->ref_count_--;

    if (fp->ref_count_ == 0) {
      delete i->second;           // free the texture
      map_path_to_font.erase(i);  // and remove the key in the map
    }
    return;
  }

  FAIL_M(ssprintf("Unloaded an unknown font (%p)", static_cast<void*>(fp)));
}

/*
void FontManager::PruneFonts() {
        for( std::map<FontName, Font*>::iterator i = g_mapPathToFont.begin();i
!= g_mapPathToFont.end();) { Font *fp=i->second; if(fp->m_iRefCount==0) { delete
fp; g_mapPathToFont.erase(i); i = g_mapPathToFont.end(); } else {
                        ++i;
                }
        }
}
*/

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
