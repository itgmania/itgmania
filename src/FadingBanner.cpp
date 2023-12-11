#include "global.h"

#include "FadingBanner.h"

#include "ActorUtil.h"
#include "Course.h"
#include "ImageCache.h"
#include "PrefsManager.h"
#include "RageLog.h"
#include "RageTextureManager.h"
#include "Song.h"
#include "SongManager.h"
#include "ThemeManager.h"
#include "ThemeMetric.h"

REGISTER_ACTOR_CLASS(FadingBanner);

// Allow fading from one banner to another. We can handle two fades at once;
// this is used to fade from an old banner to a low-quality banner to a high-
// quality banner smoothly.
//
// index_latest_ is the latest banner loaded, and the one that we'll end up
// displaying when the fades stop.
FadingBanner::FadingBanner() {
  moving_fast_ = false;
  skip_next_banner_update_ = false;
  index_latest_ = 0;
  for (int i = 0; i < NUM_BANNERS; i++) {
    banner_[i].SetName("Banner");
    ActorUtil::LoadAllCommandsAndOnCommand(banner_[i], "FadingBanner");
    this->AddChild(&banner_[i]);
  }
}

void FadingBanner::ScaleToClipped(float width, float height) {
  for (int i = 0; i < NUM_BANNERS; ++i) {
    banner_[i].ScaleToClipped(width, height);
  }
}

void FadingBanner::UpdateInternal(float delta) {
  // update children manually
  // ActorFrame::UpdateInternal( delta );
  Actor::UpdateInternal(delta);

  if (!skip_next_banner_update_) {
    for (int i = 0; i < NUM_BANNERS; ++i) {
      banner_[i].Update(delta);
    }
  }

  skip_next_banner_update_ = false;
}

void FadingBanner::DrawPrimitives() {
  // draw manually
  //	ActorFrame::DrawPrimitives();

  // Render the latest banner first.
  for (int i = 0; i < NUM_BANNERS; ++i) {
    int index = index_latest_ - i;
    wrap(index, NUM_BANNERS);
    banner_[index].Draw();
  }
}

void FadingBanner::Load(RageTextureID id, bool low_rest_to_high_res) {
  BeforeChange(low_rest_to_high_res);
  banner_[index_latest_].Load(id);

  /// XXX: Hack to keep movies from updating multiple times.
  // We need to either completely disallow movies in banners or support
  // them. There are a number of files that use them currently in the
  // wild. If we wanted to support them, then perhaps we should use an
  // all-black texture for the low quality texture.
  RageTexture* texture = banner_[index_latest_].GetTexture();
  if (!texture || !texture->IsAMovie()) {
    return;
  }
  banner_[index_latest_].SetSecondsIntoAnimation(0.f);
  for (int i = 1; i < NUM_BANNERS; ++i) {
    int index = index_latest_ - i;
    wrap(index, NUM_BANNERS);
    if (banner_[index].GetTexturePath() == id.filename) {
      banner_[index].UnloadTexture();
    }
  }
}

// If low_rest_to_high_res is true, we're fading from a low-res banner to the
// corresponding high-res banner.
void FadingBanner::BeforeChange(bool low_rest_to_high_res) {
  RString command;
  if (low_rest_to_high_res) {
    command = "FadeFromCached";
  } else {
    command = "FadeOff";
  }

  banner_[index_latest_].PlayCommand(command);
  ++index_latest_;
  wrap(index_latest_, NUM_BANNERS);

  banner_[index_latest_].PlayCommand("ResetFade");

  // We're about to load a banner. It'll probably cause a frame skip or two.
  // Skip an update, so the fade-in doesn't skip.
  skip_next_banner_update_ = true;
}

// If this returns true, a low-resolution banner was loaded, and the full-res
// banner should be loaded later.
bool FadingBanner::LoadFromCachedBanner(const RString& path) {
  // If we're already on the given banner, don't fade again.
  if (path != "" && banner_[index_latest_].GetTexturePath() == path) {
    return false;
  }

  if (path == "") {
    LoadFallback();
    return false;
  }

  // If we're currently fading to the given banner, go through this again,
  // which will cause the fade-in to be further delayed.

  RageTextureID id;
  bool low_res = (PREFSMAN->m_ImageCache != IMGCACHE_FULL);
  if (!low_res) {
    id = Sprite::SongBannerTexture(path);
  } else {
    // Try to load the low quality version.
    id = IMAGECACHE->LoadCachedImage("Banner", path);
  }

  if (!TEXTUREMAN->IsTextureRegistered(id)) {
    // Oops. We couldn't load a banner quickly. We can load the actual
    // banner, but that's slow, so we don't want to do that when we're moving
    // fast on the music wheel. In that case, we should just keep the banner
    // that's there (or load a "moving fast" banner). Once we settle down,
    // we'll get called again and load the real banner.

    if (moving_fast_) {
      return false;
    }

    if (IsAFile(path)) {
      Load(path);
    } else {
      LoadFallback();
    }

    return false;
  }

  Load(id);

  return low_res;
}

void FadingBanner::LoadFromSong(const Song* song) {
  if (song == nullptr) {
    LoadFallback();
    return;
  }

  // Don't call HasBanner. That'll do disk access and cause the music wheel
  // to skip.
  RString path = song->GetBannerPath();
  if (path.empty()) {
    LoadFallback();
  } else {
    LoadFromCachedBanner(path);
  }
}

void FadingBanner::LoadMode() {
  BeforeChange();
  banner_[index_latest_].LoadMode();
}

void FadingBanner::LoadFromSongGroup(RString song_group) {
  const RString group_banner_path = SONGMAN->GetSongGroupBannerPath(song_group);
  LoadFromCachedBanner(group_banner_path);
}

void FadingBanner::LoadFromCourse(const Course* course) {
  if (course == nullptr) {
    LoadFallback();
    return;
  }

  // Don't call HasBanner. That'll do disk access and cause the music wheel
  // to skip.
  RString path = course->GetBannerPath();
  if (path.empty()) {
    LoadCourseFallback();
  } else {
    LoadFromCachedBanner(path);
  }
}

void FadingBanner::LoadIconFromCharacter(Character* character) {
  BeforeChange();
  banner_[index_latest_].LoadIconFromCharacter(character);
}

void FadingBanner::LoadBannerFromUnlockEntry(const UnlockEntry* unlock_entry) {
  BeforeChange();
  banner_[index_latest_].LoadBannerFromUnlockEntry(unlock_entry);
}

void FadingBanner::LoadRoulette() {
  BeforeChange();
  banner_[index_latest_].LoadRoulette();
  banner_[index_latest_].PlayCommand("Roulette");
}

void FadingBanner::LoadRandom() {
  BeforeChange();
  banner_[index_latest_].LoadRandom();
  banner_[index_latest_].PlayCommand("Random");
}

void FadingBanner::LoadFromSortOrder(SortOrder sort_order) {
  BeforeChange();
  banner_[index_latest_].LoadFromSortOrder(sort_order);
}

void FadingBanner::LoadFallback() {
  BeforeChange();
  banner_[index_latest_].LoadFallback();
}

void FadingBanner::LoadCourseFallback() {
  BeforeChange();
  banner_[index_latest_].LoadCourseFallback();
}

void FadingBanner::LoadCustom(RString banner) {
  BeforeChange();
  banner_[index_latest_].Load(THEME->GetPathG("Banner", banner));
  banner_[index_latest_].PlayCommand(banner);
}

// lua start
#include "LuaBinding.h"

// Allow Lua to have access to the FadingBanner.
class LunaFadingBanner : public Luna<FadingBanner> {
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
  static int LoadFromSongGroup(T* p, lua_State* L) {
    p->LoadFromSongGroup(SArg(1));
    COMMON_RETURN_SELF;
  }
  static int LoadRandom(T* p, lua_State* L) {
    p->LoadRandom();
    COMMON_RETURN_SELF;
  }
  static int LoadRoulette(T* p, lua_State* L) {
    p->LoadRoulette();
    COMMON_RETURN_SELF;
  }
  static int LoadCourseFallback(T* p, lua_State* L) {
    p->LoadCourseFallback();
    COMMON_RETURN_SELF;
  }
  static int LoadFallback(T* p, lua_State* L) {
    p->LoadFallback();
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
  static int GetLatestIndex(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetLatestIndex());
    return 1;
  }

  LunaFadingBanner() {
    ADD_METHOD(scaletoclipped);
    ADD_METHOD(ScaleToClipped);
    ADD_METHOD(LoadFromSong);
    ADD_METHOD(LoadFromSongGroup);
    ADD_METHOD(LoadFromCourse);
    ADD_METHOD(LoadIconFromCharacter);
    ADD_METHOD(LoadCardFromCharacter);
    ADD_METHOD(LoadRandom);
    ADD_METHOD(LoadRoulette);
    ADD_METHOD(LoadCourseFallback);
    ADD_METHOD(LoadFallback);
    ADD_METHOD(LoadFromSortOrder);
    ADD_METHOD(GetLatestIndex);
    // ADD_METHOD( GetBanner );
  }
};

LUA_REGISTER_DERIVED_CLASS(FadingBanner, ActorFrame)
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
