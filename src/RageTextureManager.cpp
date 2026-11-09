/*
 * Texture garbage collection policies:
 *
 * Default: When DelayedDelete is off, delete unused textures immediately.
 *          When on, only delete textures when we change themes, on
 * DoDelayedDelete().
 *
 * Volatile: Delete unused textures once they've been used at least once. Ignore
 *           DelayedDelete.
 *
 *           This is for banners.  We don't want to load all low-quality banners
 * in memory at once, since it might be ten megs of textures, and we don't need
 * to, since we can reload them very quickly.  We don't want to keep high
 * quality textures in memory, either, although it's unlikely that a player
 * could actually view all banners long enough to transition to them all in the
 * course of one song select screen.
 *
 * If a texture is loaded as DEFAULT that was already loaded as VOLATILE,
 * DEFAULT overrides.
 */

#include "RageTextureManager.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <map>
#include <string>
#include <utility>

#include "ActorUtil.h"
#include "RageBitmapTexture.h"
#include "RageDisplay.h"
#include "RageException.h"
#include "RageLog.h"
#include "RageTexture.h"
#include "RageTextureID.h"
#include "RageThreads.h"
#include "RageUtil.h"
#include "arch/MovieTexture/MovieTexture.h"
#include "global.h"

RageTextureManager* TEXTUREMAN =
    nullptr;  // global and accessible from anywhere in our program

namespace {
std::map<RageTextureID, RageTexture*> map_path_to_texture;
std::map<RageTextureID, RageTexture*> textures_to_update;
std::map<RageTexture*, RageTextureID> texture_ids_by_pointer;
};  // namespace

RageTextureManager::RageTextureManager()
    : texture_policy_(RageTextureID::TEX_DEFAULT) {}

RageTextureManager::~RageTextureManager() {
  for (std::pair<const RageTextureID&, RageTexture*> i : map_path_to_texture) {
    RageTexture* texture = i.second;
    if (texture->ref_count_) {
      LOG->Trace(
          "TEXTUREMAN LEAK: '%s', RefCount = %d.", i.first.filename_.c_str(),
          texture->ref_count_);
    }
    RageUtil::SafeDelete(texture);
  }
  textures_to_update.clear();
  texture_ids_by_pointer.clear();
}

void RageTextureManager::Update(float delta_time) {
  for (std::pair<const RageTextureID&, RageTexture*> i : textures_to_update) {
    RageTexture* texture = i.second;
    texture->Update(delta_time);
  }
}

void RageTextureManager::AdjustTextureID(RageTextureID& id) const {
  if (id.color_depth_ == -1) {
    id.color_depth_ = prefs_.texture_color_depth_;
  }
  id.max_size_ = std::min(id.max_size_, prefs_.max_texture_resolution_);
  if (prefs_.mip_maps_) {
    id.mip_maps_ = true;
  }
}

bool RageTextureManager::IsTextureRegistered(RageTextureID id) const {
  AdjustTextureID(id);
  return map_path_to_texture.find(id) != map_path_to_texture.end();
}

/* If you've set up a texture yourself, register it here so it can be referenced
 * and deleted by ID.  This takes ownership; the texture will be freed according
 * to its GC policy. */
void RageTextureManager::RegisterTexture(
    RageTextureID id, RageTexture* texture) {
  AdjustTextureID(id);

  /* Make sure we don't already have a texture with this ID.  If we do, the
   * caller should have used it. */
  std::map<RageTextureID, RageTexture*>::iterator p =
      map_path_to_texture.find(id);
  if (p != map_path_to_texture.end()) {
    /* Oops, found the texture. */
    RageException::Throw(
        "Custom texture \"%s\" already registered!", id.filename_.c_str());
  }

  map_path_to_texture[id] = texture;
  texture_ids_by_pointer[texture] = id;
}

static const std::string kDefaultTextureName = "__blank__";
RageTextureID RageTextureManager::GetDefaultTextureID() {
  return RageTextureID(kDefaultTextureName);
}

static const std::string kScreenTextureName = "__screen__";
RageTextureID RageTextureManager::GetScreenTextureID() {
  return RageTextureID(kScreenTextureName);
}

RageSurface* RageTextureManager::GetScreenSurface() {
  return DISPLAY->CreateScreenshot();
}

class RageTexture_Default : public RageTexture {
 public:
  RageTexture_Default() : RageTexture(RageTextureID()), tex_handle_(0) {
    source_width_ = source_height_ = 1;
    texture_width_ = texture_height_ = 1;
    image_width_ = image_height_ = 1;
    CreateFrameRects();
  }
  uintptr_t GetTexHandle() const { return tex_handle_; }

 private:
  uintptr_t tex_handle_;
};

// Load and unload textures from disk.
RageTexture* RageTextureManager::LoadTextureInternal(RageTextureID id) {
  CHECKPOINT_M(
      ssprintf("RageTextureManager::LoadTexture(%s).", id.filename_.c_str()));

  AdjustTextureID(id);

  /* We could have two copies of the same bitmap if there are equivalent but
   * different paths, e.g. "Bitmaps\me.bmp" and "..\Rage PC
   * Edition\Bitmaps\me.bmp". */
  std::map<RageTextureID, RageTexture*>::iterator p =
      map_path_to_texture.find(id);
  if (p != map_path_to_texture.end()) {
    /* Found the texture.  Just increase the refcount and return it. */
    RageTexture* texture = p->second;
    texture->ref_count_++;
    return texture;
  }

  // The texture is not already loaded.  Load it.

  RageTexture* texture;
  if (id.filename_ == kDefaultTextureName) {
    texture = new RageTexture_Default;
  } else if (ActorUtil::GetFileType(id.filename_) == FT_Movie) {
    texture = RageMovieTexture::Create(id);
  } else {
    texture = new RageBitmapTexture(id);
  }

  map_path_to_texture[id] = texture;
  texture_ids_by_pointer[texture] = id;

  return texture;
}

/* Load a normal texture.  Use this call to actually use a texture. */
RageTexture* RageTextureManager::LoadTexture(RageTextureID id) {
  RageTexture* texture = LoadTextureInternal(id);
  if (texture) {
    texture->was_used_ = true;
  }
  return texture;
}

RageTexture* RageTextureManager::CopyTexture(RageTexture* copy) {
  ++copy->ref_count_;
  return copy;
}

void RageTextureManager::UnloadTexture(RageTexture* t) {
  if (t == nullptr) {
    return;
  }

  t->ref_count_--;
  ASSERT_M(
      t->ref_count_ >= 0,
      ssprintf("%i, %s", t->ref_count_, t->GetID().filename_.c_str()));

  if (t->ref_count_) {
    return; /* Can't unload textures that are still referenced. */
  }

  bool delete_this = false;

  /* Always unload movies, so we don't waste time decoding. */
  if (t->IsAMovie()) {
    delete_this = true;
  }

  /* Delete normal textures immediately unless m_bDelayedDelete is is on. */
  if (t->GetPolicy() == RageTextureID::TEX_DEFAULT && !prefs_.delayed_delete_) {
    delete_this = true;
  }

  /* Delete volatile textures after they've been used at least once. */
  if (t->GetPolicy() == RageTextureID::TEX_VOLATILE && t->was_used_) {
    delete_this = true;
  }

  if (delete_this) {
    DeleteTexture(t);
  }
}

void RageTextureManager::DeleteTexture(RageTexture* t) {
  ASSERT(t->ref_count_ == 0);
  // LOG->Trace( "RageTextureManager: deleting '%s'.",
  // t->GetID().filename.c_str() );

  std::map<RageTexture*, RageTextureID>::iterator id_entry =
      texture_ids_by_pointer.find(t);
  if (id_entry != texture_ids_by_pointer.end()) {
    std::map<RageTextureID, RageTexture*>::iterator tex_entry =
        map_path_to_texture.find(id_entry->second);
    if (tex_entry != map_path_to_texture.end()) {
      map_path_to_texture.erase(tex_entry);
      RageUtil::SafeDelete(t);
    }
    std::map<RageTextureID, RageTexture*>::iterator tex_update_entry =
        textures_to_update.find(id_entry->second);
    if (tex_update_entry != textures_to_update.end()) {
      textures_to_update.erase(tex_update_entry);
    }
    texture_ids_by_pointer.erase(id_entry);
    return;
  }

  FAIL_M("Tried to delete a texture that wasn't in the ids by pointer list.");
}

void RageTextureManager::GarbageCollect(GCType type) {
  // Search for old textures with refcount==0 to unload
  LOG->Trace("Performing texture garbage collection.");

  for (std::map<RageTextureID, RageTexture*>::iterator i =
           map_path_to_texture.begin();
       i != map_path_to_texture.end();) {
    std::map<RageTextureID, RageTexture*>::iterator j = i;
    i++;

    std::string path = j->first.filename_;
    RageTexture* t = j->second;

    if (t->ref_count_) {
      continue; /* Can't unload textures that are still referenced. */
    }

    bool delete_this = false;
    if (type == screen_changed) {
      RageTextureID::TexPolicy policy = t->GetPolicy();
      switch (policy) {
        case RageTextureID::TEX_DEFAULT:
          /* If m_bDelayedDelete, wait until delayed_delete.  If
           * !m_bDelayedDelete, it should have been deleted when it reached no
           * references, but we might have just changed the preference. */
          if (!prefs_.delayed_delete_) {
            delete_this = true;
          }
          break;
        case RageTextureID::TEX_VOLATILE:
          delete_this = true;
          break;
        default:
          FAIL_M(ssprintf("Invalid texture policy: %i", policy));
      }
    }

    /* This happens when we change themes; free all textures. */
    if (type == delayed_delete) {
      delete_this = true;
    }

    if (delete_this) {
      DeleteTexture(t);
    }
  }
}

void RageTextureManager::ReloadAll() {
  DisableOddDimensionWarning();

  /* Let's get rid of all unreferenced textures, so we don't reload a
   * ton of cached data that we're not necessarily going to use. */
  DoDelayedDelete();

  for (const auto& i : map_path_to_texture) {
    i.second->Reload();
  }

  EnableOddDimensionWarning();
}

/* In some cases, changing the display mode will reset the rendering context,
 * releasing all textures.  We don't want to reload immediately if that happens,
 * since we might be changing texture preferences too, which also may have to
 * reload textures.  Instead, tell all textures that their texture ID is
 * invalid, so it doesn't try to free it later when we really do reload (since
 * that ID might be associated with a different texture).  Ack. */
void RageTextureManager::InvalidateTextures() {
  for (const auto& i : map_path_to_texture) {
    RageTexture* texture = i.second;
    texture->Invalidate();
  }
}

bool RageTextureManager::SetPrefs(RageTextureManagerPrefs prefs) {
  bool need_reload = false;
  if (prefs_ != prefs) {
    need_reload = true;
  }

  prefs_ = prefs;

  ASSERT(
      prefs_.texture_color_depth_ == 16 || prefs_.texture_color_depth_ == 32);
  ASSERT(prefs_.movie_color_depth_ == 16 || prefs_.movie_color_depth_ == 32);
  return need_reload;
}

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
