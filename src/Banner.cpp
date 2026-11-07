#include "Banner.h"

#include <string>

#include "Actor.h"
#include "ActorUtil.h"
#include "Character.h"
#include "CharacterManager.h"
#include "Course.h"
#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "ImageCache.h"
#include "LuaManager.h"
#include "PrefsManager.h"
#include "RageTextureID.h"
#include "RageTextureManager.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "Song.h"
#include "SongManager.h"
#include "Sprite.h"
#include "ThemeManager.h"
#include "ThemeMetric.h"
#include "UnlockManager.h"

REGISTER_ACTOR_CLASS(Banner);

ThemeMetric<bool> SCROLL_RANDOM("Banner", "ScrollRandom");
ThemeMetric<bool> SCROLL_ROULETTE("Banner", "ScrollRoulette");
ThemeMetric<bool> SCROLL_MODE("Banner", "ScrollMode");
ThemeMetric<bool> SCROLL_SORT_ORDER("Banner", "ScrollSortOrder");
ThemeMetric<float> SCROLL_SPEED_DIVISOR("Banner", "ScrollSpeedDivisor");

Banner::Banner() {
  scrolling_ = false;
  percent_scrolling_ = 0;

  // By default, don't let banners scale their rate with tab/tilde.
  // This helps with animated/video banners.
  Actor::SetRateScalingEnabled(false);
}

// Ugly: if sIsBanner is false, we're actually loading something other than a
// banner.
void Banner::Load(RageTextureID id, bool is_banner) {
  if (id.filename_ == "") {
    LoadFallback();
    return;
  }

  if (is_banner) {
    id = SongBannerTexture(id);
  }

  percent_scrolling_ = 0;
  scrolling_ = false;

  TEXTUREMAN->DisableOddDimensionWarning();
  Sprite::Load(id);
  TEXTUREMAN->EnableOddDimensionWarning();
};

void Banner::LoadFromCachedBanner(const std::string& path) {
  if (path.empty()) {
    LoadFallback();
    return;
  }

  RageTextureID id;
  bool low_res = (PREFSMAN->image_cache_ != IMGCACHE_FULL);
  if (!low_res) {
    id = Sprite::SongBannerTexture(path);
  } else {
    // Try to load the low quality version.
    id = IMAGECACHE->LoadCachedImage("Banner", path);
  }

  if (TEXTUREMAN->IsTextureRegistered(id)) {
    Load(id);
  } else if (IsAFile(path)) {
    Load(path);
  } else {
    LoadFallback();
  }
}

void Banner::Update(float delta_time) {
  Sprite::Update(delta_time);

  if (scrolling_) {
    percent_scrolling_ += delta_time / (float)SCROLL_SPEED_DIVISOR;
    percent_scrolling_ -= (int)percent_scrolling_;

    const RectF* texture_rect = GetCurrentTextureCoordRect();

    float tex_coords[8] = {
        0 + percent_scrolling_, texture_rect->top_,     // top left
        0 + percent_scrolling_, texture_rect->bottom_,  // bottom left
        1 + percent_scrolling_, texture_rect->bottom_,  // bottom right
        1 + percent_scrolling_, texture_rect->top_,     // top right
    };
    Sprite::SetCustomTextureCoords(tex_coords);
  }
}

void Banner::SetScrolling(bool scroll, float percent) {
  scrolling_ = scroll;
  percent_scrolling_ = percent;

  // Set up the texture coord rects for the current state.
  Update(0);
}

void Banner::LoadFromSong(Song* song)  // nullptr means no song
{
  if (song == nullptr) {
    LoadFallback();
  } else if (song->HasBanner()) {
    Load(song->GetBannerPath());
  } else {
    LoadFallback();
  }

  scrolling_ = false;
}

void Banner::LoadMode() {
  Load(THEME->GetPathG("Banner", "Mode"));
  scrolling_ = (bool)SCROLL_MODE;
}

void Banner::LoadFromSongGroup(std::string song_group) {
  std::string group_banner_path = SONGMAN->GetSongGroupBannerPath(song_group);
  if (group_banner_path != "") {
    Load(group_banner_path);
  } else {
    LoadGroupFallback();
  }
  scrolling_ = false;
}

void Banner::LoadFromCourse(const Course* course)  // nullptr means no course
{
  if (course == nullptr) {
    LoadFallback();
  } else if (course->GetBannerPath() != "") {
    Load(course->GetBannerPath());
  } else {
    LoadCourseFallback();
  }

  scrolling_ = false;
}

void Banner::LoadIconFromCharacter(const Character* character) {
  if (character == nullptr) {
    LoadFallbackCharacterIcon();
  } else if (character->GetIconPath() != "") {
    Load(character->GetIconPath(), false);
  } else {
    LoadFallbackCharacterIcon();
  }

  scrolling_ = false;
}

void Banner::LoadBannerFromUnlockEntry(const UnlockEntry* ue) {
  if (ue == nullptr) {
    LoadFallback();
  } else {
    std::string file = ue->GetBannerFile();
    Load(file);
    scrolling_ = false;
  }
}

void Banner::LoadBackgroundFromUnlockEntry(const UnlockEntry* ue) {
  if (ue == nullptr) {
    LoadFallback();
  } else {
    std::string file = ue->GetBackgroundFile();
    Load(file);
    scrolling_ = false;
  }
}

void Banner::LoadFallback() {
  Load(THEME->GetPathG("Common", "fallback banner"));
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
  scrolling_ = (bool)SCROLL_ROULETTE;
}

void Banner::LoadRandom() {
  Load(THEME->GetPathG("Banner", "random"));
  scrolling_ = (bool)SCROLL_RANDOM;
}

void Banner::LoadFromSortOrder(SortOrder so) {
  // TODO: See if the check for nullptr/PREFERRED(?) is needed.
  switch (so) {
    case SortOrder_Invalid:
      LoadFallback();
      break;
    case SORT_SERIES:
    case SORT_GROUP:
      break;
    // This is necessary to prevent multiple banners from matching for
    // SORT_RECENT and SORT_POPULARITY
    case SORT_RECENT:
    case SORT_RECENT_P1:
    case SORT_RECENT_P2:
      Load(THEME->GetPathG(
          "Banner", ssprintf("%s", SortOrderToString(SORT_RECENT).c_str())));
      break;
    case SORT_POPULARITY:
    case SORT_POPULARITY_P1:
    case SORT_POPULARITY_P2:
      Load(THEME->GetPathG(
          "Banner",
          ssprintf("%s", SortOrderToString(SORT_POPULARITY).c_str())));
      break;
    default:
      Load(THEME->GetPathG(
          "Banner", ssprintf("%s", SortOrderToString(so).c_str())));
      break;
  }
  scrolling_ = (bool)SCROLL_SORT_ORDER;
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the Banner. */
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
      Song* s = Luna<Song>::check(L, 1);
      p->LoadFromSong(s);
    }
    COMMON_RETURN_SELF;
  }
  static int LoadFromCourse(T* p, lua_State* L) {
    if (lua_isnil(L, 1)) {
      p->LoadFromCourse(nullptr);
    } else {
      Course* c = Luna<Course>::check(L, 1);
      p->LoadFromCourse(c);
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
      Character* c = Luna<Character>::check(L, 1);
      p->LoadIconFromCharacter(c);
    }
    COMMON_RETURN_SELF;
  }
  static int LoadCardFromCharacter(T* p, lua_State* L) {
    if (lua_isnil(L, 1)) {
      p->LoadIconFromCharacter(nullptr);
    } else {
      Character* c = Luna<Character>::check(L, 1);
      p->LoadIconFromCharacter(c);
    }
    COMMON_RETURN_SELF;
  }
  static int LoadBannerFromUnlockEntry(T* p, lua_State* L) {
    if (lua_isnil(L, 1)) {
      p->LoadBannerFromUnlockEntry(nullptr);
    } else {
      UnlockEntry* ue = Luna<UnlockEntry>::check(L, 1);
      p->LoadBannerFromUnlockEntry(ue);
    }
    COMMON_RETURN_SELF;
  }
  static int LoadBackgroundFromUnlockEntry(T* p, lua_State* L) {
    if (lua_isnil(L, 1)) {
      p->LoadBackgroundFromUnlockEntry(nullptr);
    } else {
      UnlockEntry* ue = Luna<UnlockEntry>::check(L, 1);
      p->LoadBackgroundFromUnlockEntry(ue);
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
