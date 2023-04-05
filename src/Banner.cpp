#include "global.h"

#include "Banner.h"

#include "ActorUtil.h"
#include "Character.h"
#include "CharacterManager.h"
#include "Course.h"
#include "ImageCache.h"
#include "PrefsManager.h"
#include "RageTextureManager.h"
#include "RageUtil.h"
#include "Song.h"
#include "SongManager.h"
#include "ThemeMetric.h"
#include "UnlockManager.h"

REGISTER_ACTOR_CLASS(Banner);

ThemeMetric<bool> SCROLL_RANDOM("Banner", "ScrollRandom");
ThemeMetric<bool> SCROLL_ROULETTE("Banner", "ScrollRoulette");
ThemeMetric<bool> SCROLL_MODE("Banner", "ScrollMode");
ThemeMetric<bool> SCROLL_SORT_ORDER("Banner", "ScrollSortOrder");
ThemeMetric<float> SCROLL_SPEED_DIVISOR("Banner", "ScrollSpeedDivisor");

Banner::Banner() {
  is_scrolling_ = false;
  percent_scrolling_ = 0;
}

// Ugly: if sIsBanner is false, we're actually loading something other than a
// banner.
void Banner::Load(RageTextureID texture_id, bool is_banner) {
  if (texture_id.filename == "") {
    LoadFallback();
    return;
  }

  if (is_banner) {
    texture_id = SongBannerTexture(texture_id);
  }

  percent_scrolling_ = 0;
  is_scrolling_ = false;

  TEXTUREMAN->DisableOddDimensionWarning();
  TEXTUREMAN->VolatileTexture(texture_id);
  Sprite::Load(texture_id);
  TEXTUREMAN->EnableOddDimensionWarning();
};

void Banner::LoadFromCachedBanner(const RString& path) {
  if (path.empty()) {
    LoadFallback();
    return;
  }

  RageTextureID texture_id;
  bool is_low_res = (PREFSMAN->m_ImageCache != IMGCACHE_FULL);
  if (!is_low_res) {
    texture_id = Sprite::SongBannerTexture(path);
  } else {
    // Try to load the low quality version.
    texture_id = IMAGECACHE->LoadCachedImage("Banner", path);
  }

  if (TEXTUREMAN->IsTextureRegistered(texture_id)) {
    Load(texture_id);
  } else if (IsAFile(path)) {
    Load(path);
  } else {
    LoadFallback();
  }
}

void Banner::Update(float delta) {
  Sprite::Update(delta);

  if (is_scrolling_) {
    percent_scrolling_ += delta / (float)SCROLL_SPEED_DIVISOR;
    percent_scrolling_ -= (int)percent_scrolling_;

    const RectF* texture_rect = GetCurrentTextureCoordRect();

    float texture_coords[8] = {
        0 + percent_scrolling_, texture_rect->top,     // top left
        0 + percent_scrolling_, texture_rect->bottom,  // bottom left
        1 + percent_scrolling_, texture_rect->bottom,  // bottom right
        1 + percent_scrolling_, texture_rect->top,     // top right
    };
    Sprite::SetCustomTextureCoords(texture_coords);
  }
}

void Banner::SetScrolling(bool scroll, float percent) {
  is_scrolling_ = scroll;
  percent_scrolling_ = percent;

  // Set up the texture coord rects for the current state.
  Update(0);
}

void Banner::LoadFromSong(Song* song) {
  if (song == nullptr) {
    // nullptr means no song
    LoadFallback();
  } else if (song->HasBanner()) {
    Load(song->GetBannerPath());
  } else {
    LoadFallback();
  }

  is_scrolling_ = false;
}

void Banner::LoadMode() {
  Load(THEME->GetPathG("Banner", "Mode"));
  is_scrolling_ = (bool)SCROLL_MODE;
}

void Banner::LoadFromSongGroup(const RString& song_group) {
  RString group_banner_path = SONGMAN->GetSongGroupBannerPath(song_group);
  if (group_banner_path != "") {
    Load(group_banner_path);
  } else {
    LoadGroupFallback();
  }
  is_scrolling_ = false;
}

void Banner::LoadFromCourse(const Course* course) {
  if (course == nullptr) {
    // nullptr means no course
    LoadFallback();
  } else if (course->GetBannerPath() != "") {
    Load(course->GetBannerPath());
  } else {
    LoadCourseFallback();
  }

  is_scrolling_ = false;
}

void Banner::LoadCardFromCharacter(const Character* character) {
  if (character == nullptr) {
    LoadFallback();
  } else if (character->GetCardPath() != "") {
    Load(character->GetCardPath());
  } else {
    LoadFallback();
  }

  is_scrolling_ = false;
}

void Banner::LoadIconFromCharacter(const Character* character) {
  if (character == nullptr) {
    LoadFallbackCharacterIcon();
  } else if (character->GetIconPath() != "") {
    Load(character->GetIconPath(), false);
  } else {
    LoadFallbackCharacterIcon();
  }

  is_scrolling_ = false;
}

void Banner::LoadBannerFromUnlockEntry(const UnlockEntry* unlock_entry) {
  if (unlock_entry == nullptr) {
    LoadFallback();
  } else {
    RString sFile = unlock_entry->GetBannerFile();
    Load(sFile);
    is_scrolling_ = false;
  }
}

void Banner::LoadBackgroundFromUnlockEntry(const UnlockEntry* unlock_entry) {
  if (unlock_entry == nullptr) {
    LoadFallback();
  } else {
    RString sFile = unlock_entry->GetBackgroundFile();
    Load(sFile);
    is_scrolling_ = false;
  }
}

void Banner::LoadFallback() {
  Load(THEME->GetPathG("Common", "fallback banner"));
}

void Banner::LoadFallbackBG() {
  Load(THEME->GetPathG("Common", "fallback background"));
}

void Banner::LoadGroupFallback() {
  Load(THEME->GetPathG("Banner", "group fallback"));
}

void Banner::LoadCourseFallback() {
  Load(THEME->GetPathG("Banner", "course fallback"));
}

void Banner::LoadFallbackCharacterIcon() {
  Character* character = CHARMAN->GetDefaultCharacter();
  if (character && !character->GetIconPath().empty()) {
    Load(character->GetIconPath(), false);
  } else {
    LoadFallback();
  }
}

void Banner::LoadRoulette() {
  Load(THEME->GetPathG("Banner", "roulette"));
  is_scrolling_ = (bool)SCROLL_ROULETTE;
}

void Banner::LoadRandom() {
  Load(THEME->GetPathG("Banner", "random"));
  is_scrolling_ = (bool)SCROLL_RANDOM;
}

void Banner::LoadFromSortOrder(SortOrder sort_order) {
  // TODO: See if the check for nullptr/PREFERRED(?) is needed.
  if (sort_order == SortOrder_Invalid) {
    LoadFallback();
  } else {
    if (sort_order != SORT_GROUP && sort_order != SORT_RECENT) {
      Load(THEME->GetPathG(
          "Banner", ssprintf("%s", SortOrderToString(sort_order).c_str())));
    }
  }
  is_scrolling_ = (bool)SCROLL_SORT_ORDER;
}

// lua start
#include "LuaBinding.h"

// Allow Lua to have access to the Banner.
class LunaBanner : public Luna<Banner> {
 public:
  static int scaletoclipped(T* p, lua_State* L) {
    p->ScaleToClipped(FArg(1), FArg(2));
    COMMON_RETURN_SELF;
  }

  static int ScaleToClipped(T* p, lua_State* L) {
    p->ScaleToClipped(FArg(1), FArg(2));
    COMMON_RETURN_SELF;
  }

  static int LoadFromSong(T* p, lua_State* L) {
    if (lua_isnil(L, 1)) {
      p->LoadFromSong(nullptr);
    } else {
      Song* pS = Luna<Song>::check(L, 1);
      p->LoadFromSong(pS);
    }
    COMMON_RETURN_SELF;
  }

  static int LoadFromCourse(T* p, lua_State* L) {
    if (lua_isnil(L, 1)) {
      p->LoadFromCourse(nullptr);
    } else {
      Course* pC = Luna<Course>::check(L, 1);
      p->LoadFromCourse(pC);
    }
    COMMON_RETURN_SELF;
  }

  static int LoadFromCachedBanner(T* p, lua_State* L) {
    p->LoadFromCachedBanner(SArg(1));
    COMMON_RETURN_SELF;
  }

  static int LoadIconFromCharacter(T* p, lua_State* L) {
    if (lua_isnil(L, 1)) {
      p->LoadIconFromCharacter(nullptr);
    } else {
      Character* pC = Luna<Character>::check(L, 1);
      p->LoadIconFromCharacter(pC);
    }
    COMMON_RETURN_SELF;
  }

  static int LoadCardFromCharacter(T* p, lua_State* L) {
    if (lua_isnil(L, 1)) {
      p->LoadIconFromCharacter(nullptr);
    } else {
      Character* pC = Luna<Character>::check(L, 1);
      p->LoadIconFromCharacter(pC);
    }
    COMMON_RETURN_SELF;
  }

  static int LoadBannerFromUnlockEntry(T* p, lua_State* L) {
    if (lua_isnil(L, 1)) {
      p->LoadBannerFromUnlockEntry(nullptr);
    } else {
      UnlockEntry* pUE = Luna<UnlockEntry>::check(L, 1);
      p->LoadBannerFromUnlockEntry(pUE);
    }
    COMMON_RETURN_SELF;
  }

  static int LoadBackgroundFromUnlockEntry(T* p, lua_State* L) {
    if (lua_isnil(L, 1)) {
      p->LoadBackgroundFromUnlockEntry(nullptr);
    } else {
      UnlockEntry* pUE = Luna<UnlockEntry>::check(L, 1);
      p->LoadBackgroundFromUnlockEntry(pUE);
    }
    COMMON_RETURN_SELF;
  }

  static int LoadFromSongGroup(T* p, lua_State* L) {
    p->LoadFromSongGroup(SArg(1));
    COMMON_RETURN_SELF;
  }

  static int LoadFromSortOrder(T* p, lua_State* L) {
    if (lua_isnil(L, 1)) {
      p->LoadFromSortOrder(SortOrder_Invalid);
    } else {
      SortOrder so = Enum::Check<SortOrder>(L, 1);
      p->LoadFromSortOrder(so);
    }
    COMMON_RETURN_SELF;
  }

  static int GetScrolling(T* p, lua_State* L) {
    lua_pushboolean(L, p->GetScrolling());
    return 1;
  }

  static int SetScrolling(T* p, lua_State* L) {
    p->SetScrolling(BArg(1), FArg(2));
    COMMON_RETURN_SELF;
  }

  static int GetPercentScrolling(T* p, lua_State* L) {
    lua_pushnumber(L, p->ScrollingPercent());
    return 1;
  }

  LunaBanner() {
    ADD_METHOD(scaletoclipped);
    ADD_METHOD(ScaleToClipped);
    ADD_METHOD(LoadFromSong);
    ADD_METHOD(LoadFromCourse);
    ADD_METHOD(LoadFromCachedBanner);
    ADD_METHOD(LoadIconFromCharacter);
    ADD_METHOD(LoadCardFromCharacter);
    ADD_METHOD(LoadBannerFromUnlockEntry);
    ADD_METHOD(LoadBackgroundFromUnlockEntry);
    ADD_METHOD(LoadFromSongGroup);
    ADD_METHOD(LoadFromSortOrder);
    ADD_METHOD(GetScrolling);
    ADD_METHOD(SetScrolling);
    ADD_METHOD(GetPercentScrolling);
  }
};

LUA_REGISTER_DERIVED_CLASS(Banner, Sprite)
// lua end

/*
 * (c) 2001-2004 Chris Danford
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
