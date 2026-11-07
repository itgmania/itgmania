/* RageTextureManager - Interface for loading textures. */

#ifndef RAGE_TEXTURE_MANAGER_H
#define RAGE_TEXTURE_MANAGER_H

#include "RageSurface.h"
#include "RageTexture.h"
#include "RageTextureID.h"

struct RageTextureManagerPrefs {
  int texture_color_depth_;
  int movie_color_depth_;
  bool delayed_delete_;
  int max_texture_resolution_;
  bool high_resolution_textures_;
  bool mip_maps_;

  RageTextureManagerPrefs()
      : texture_color_depth_(16),
        movie_color_depth_(16),
        delayed_delete_(false),
        max_texture_resolution_(1024),
        high_resolution_textures_(true),
        mip_maps_(false) {}
  RageTextureManagerPrefs(
      int texture_color_depth, int movie_color_depth, bool delayed_delete,
      int max_texture_resolution, bool high_resolution_textures, bool mip_maps)
      : texture_color_depth_(texture_color_depth),
        movie_color_depth_(movie_color_depth),
        delayed_delete_(delayed_delete),
        max_texture_resolution_(max_texture_resolution),
        high_resolution_textures_(high_resolution_textures),
        mip_maps_(mip_maps) {}

  bool operator!=(const RageTextureManagerPrefs& rhs) const {
    return texture_color_depth_ != rhs.texture_color_depth_ ||
           movie_color_depth_ != rhs.movie_color_depth_ ||
           delayed_delete_ != rhs.delayed_delete_ ||
           max_texture_resolution_ != rhs.max_texture_resolution_ ||
           high_resolution_textures_ != rhs.high_resolution_textures_ ||
           mip_maps_ != rhs.mip_maps_;
  }
};

class RageTextureManager {
 public:
  RageTextureManager();
  ~RageTextureManager();
  void Update(float delta_time);

  RageTexture* LoadTexture(RageTextureID id);
  RageTexture* CopyTexture(RageTexture* copy);  // returns a ref to the same
                                                // texture, not a deep copy
  bool IsTextureRegistered(RageTextureID id) const;
  void RegisterTexture(RageTextureID id, RageTexture* p);
  void UnloadTexture(RageTexture* t);
  void ReloadAll();

  bool SetPrefs(RageTextureManagerPrefs prefs);
  RageTextureManagerPrefs GetPrefs() { return prefs_; };

  RageTextureID::TexPolicy GetDefaultTexturePolicy() const {
    return texture_policy_;
  }
  void SetDefaultTexturePolicy(RageTextureID::TexPolicy p) {
    texture_policy_ = p;
  }

  // call this between Screens
  void DeleteCachedTextures() { GarbageCollect(screen_changed); }

  // call this on switch theme
  void DoDelayedDelete() { GarbageCollect(delayed_delete); }

  void InvalidateTextures();

  void AdjustTextureID(RageTextureID& id) const;

  void DisableOddDimensionWarning() {}
  void EnableOddDimensionWarning() {}

  RageTextureID GetDefaultTextureID();
  RageTextureID GetScreenTextureID();
  RageSurface* GetScreenSurface();

 private:
  void DeleteTexture(RageTexture* t);
  enum GCType { screen_changed, delayed_delete };
  void GarbageCollect(GCType type);
  RageTexture* LoadTextureInternal(RageTextureID id);

  RageTextureManagerPrefs prefs_;
  RageTextureID::TexPolicy texture_policy_;
};

extern RageTextureManager*
    TEXTUREMAN;  // global and accessible from anywhere in our program

#endif

/*
 * Copyright (c) 2001-2004 Chris Danford, Glenn Maynard
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
