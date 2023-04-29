#ifndef FADING_BANNER_H
#define FADING_BANNER_H

#include "ActorFrame.h"
#include "Banner.h"
#include "RageTimer.h"

// FadingBanner - Fades between two banners.
class FadingBanner : public ActorFrame {
 public:
  FadingBanner();
  virtual FadingBanner* Copy() const;

  void ScaleToClipped(float width, float height);

  // If you previously loaded a cached banner, and are now loading the full-
  // resolution banner, set low_res_to_high_res to true.
  void Load(RageTextureID id, bool low_res_to_high_res = false);
  void LoadFromSong(const Song* song);  // nullptr means no song
  void LoadMode();
  void LoadFromSongGroup(RString song_group);
  void LoadFromCourse(const Course* course);
  void LoadIconFromCharacter(Character* character);
  void LoadBannerFromUnlockEntry(const UnlockEntry* unlock_entry);
  void LoadRoulette();
  void LoadRandom();
  void LoadFromSortOrder(SortOrder sort_order);
  void LoadFallback();
  void LoadCourseFallback();
  void LoadCustom(RString banner);

  bool LoadFromCachedBanner(const RString& path);

  void SetMovingFast(bool fast) { moving_fast_ = fast; }
  virtual void UpdateInternal(float delta);
  virtual void DrawPrimitives();

  int GetLatestIndex() { return index_latest_; }
  Banner GetBanner(int i) { return banner_[i]; }

  // Lua
  void PushSelf(lua_State* L);

 protected:
  void BeforeChange(bool low_res_to_high_res = false);

  static const int NUM_BANNERS = 5;
  Banner banner_[NUM_BANNERS];
  int index_latest_;

  bool moving_fast_;
  bool skip_next_banner_update_;
};

#endif  // FADING_BANNER_H

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
