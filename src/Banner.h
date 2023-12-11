#ifndef BANNER_H
#define BANNER_H

#include "Character.h"
#include "Course.h"
#include "GameConstantsAndTypes.h"
#include "RageTextureID.h"
#include "Song.h"
#include "Sprite.h"
#include "UnlockManager.h"

// The song/course's banner displayed in SelectMusic/Course.
class Banner : public Sprite {
 public:
  Banner();
  virtual ~Banner() {}
  virtual Banner* Copy() const;

  void Load(RageTextureID texture_id, bool is_banner);
  virtual void Load(RageTextureID texture_id) { Load(texture_id, true); }
  void LoadFromCachedBanner(const RString& path);

  virtual void Update(float delta);

  // Attempt to load the banner from a song.
  // pSong the song in question. If nullptr, there is no song.
  void LoadFromSong(Song* song);
  void LoadMode();
  void LoadFromSongGroup(const RString& song_group);
  void LoadFromCourse(const Course* course);
  void LoadCardFromCharacter(const Character* character);
  void LoadIconFromCharacter(const Character* character);
  void LoadBannerFromUnlockEntry(const UnlockEntry* unlock_entry);
  void LoadBackgroundFromUnlockEntry(const UnlockEntry* unlock_entry);
  void LoadRoulette();
  void LoadRandom();
  void LoadFromSortOrder(SortOrder sort_order);
  void LoadFallback();
  void LoadFallbackBG();
  void LoadGroupFallback();
  void LoadCourseFallback();
  void LoadFallbackCharacterIcon();

  void SetScrolling(bool scroll, float percent = 0);
  bool GetScrolling() const { return is_scrolling_; }
  float ScrollingPercent() const { return percent_scrolling_; }

  // Lua
  void PushSelf(lua_State* L);

 protected:
  bool is_scrolling_;
  float percent_scrolling_;
};

#endif  // BANNER_H

/**
 * @file
 * @author Chris Danford (c) 2001-2004
 * @section LICENSE
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
