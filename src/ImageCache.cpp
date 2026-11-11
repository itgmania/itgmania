#include "ImageCache.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "CommonMetrics.h"
#include "Preference.h"
#include "PrefsManager.h"
#include "RageDisplay.h"
#include "RageFileManager.h"
#include "RageLog.h"
#include "RageSurface.h"
#include "RageSurfaceUtils.h"
#include "RageSurfaceUtils_Dither.h"
#include "RageSurfaceUtils_Palettize.h"
#include "RageSurfaceUtils_Zoom.h"
#include "RageSurface_Load.h"
#include "RageTexture.h"
#include "RageTextureID.h"
#include "RageTextureManager.h"
#include "RageThreads.h"
#include "RageUtil.h"
#include "SongCacheIndex.h"
#include "SpecialFiles.h"
#include "Sprite.h"
#include "XmlFile.h"
#include "global.h"

static Preference<bool> paletted_image_cache("PalettedImageCache", false);

/* Neither a global or a file scope static can be used for this because
 * the order of initialization of nonlocal objects is unspecified. */
// const std::string IMAGE_CACHE_INDEX = SpecialFiles::CACHE_DIR +
// "images.cache";
#define IMAGE_CACHE_INDEX (SpecialFiles::kCacheDir + "images.cache")

/* Maximum resolution a cached banner image is scaled down to. Must be powers
 * of two. */
static const int kMaxBannerCacheWidth = 256;
static const int kMaxBannerCacheHeight = 128;

static int nearest_power_of_two(int v) {
  const int up = power_of_two(v);
  const int down = up / 2;
  const int nearest = (std::abs(v - up) > std::abs(v - down)) ? down : up;
  return std::max(1, nearest);
}

/* Bump this to invalidate the on-disk image cache. */
static const int kImageCacheVersion = 1;

/* Call CacheImage to cache a image by path.  If the image is already
 * cached, it'll be recreated.  This is efficient if the image hasn't changed,
 * but we still only do this in TidyUpData for songs.
 *
 * Call LoadImage to load a cached image into main memory.  This will call
 * CacheImage only if needed.  This will not do a date/size check; call
 * CacheImage directly if you need that.
 *
 * Call LoadCachedImage to load a image into a texture and retrieve an ID
 * for it.  You can check if the image was actually preloaded by calling
 * TEXTUREMAN->IsTextureRegistered() on the ID; it might not be if the image
 * cache is missing or disabled.
 *
 * Note that each cache entries has two hashes.  The cache path is based soley
 * on the pathname; this way, loading the cache doesn't have to do a stat on
 * every image.  The full hash includes the file size and date, and is used only
 * by CacheImage to avoid doing extra work.
 */

ImageCache* IMAGECACHE;  // global and accessible from anywhere in our program

static std::map<std::string, RageSurface*> image_path_to_image;
static int demand_refcount = 0;

/* Synchronizes access to g_ImagePathToImage and ImageCache::ImageData. */
static RageMutex image_cache_mutex("ImageCache");

std::string ImageCache::GetImageCachePath(
    std::string image_dir, std::string image_path) {
  return SongCacheIndex::GetCacheFilePath(image_dir, image_path);
}

/* If in on-demand mode, load all cached images.  This must be fast, so
 * cache files will not be created if they don't exist; that should be done
 * by CacheImage or LoadImage on startup. */
void ImageCache::Demand(std::string image_dir) {
  ++demand_refcount;
  if (demand_refcount > 1) {
    return;
  }

  if (PREFSMAN->image_cache_ != IMGCACHE_LOW_RES_LOAD_ON_DEMAND) {
    return;
  }

  LockMut(image_cache_mutex);
  FOREACH_CONST_Child(&image_data_, p) {
    std::string image_path = p->GetName();

    if (image_path_to_image.find(image_path) != image_path_to_image.end()) {
      continue; /* already loaded */
    }

    const std::string cache_path = GetImageCachePath(image_dir, image_path);
    RageSurface* image = RageSurfaceUtils::LoadSurface(cache_path);
    if (image == nullptr) {
      continue; /* doesn't exist */
    }

    image_path_to_image[image_path] = image;
  }
}

/* Release images loaded on demand. */
void ImageCache::Undemand(std::string image_dir) {
  --demand_refcount;
  if (demand_refcount != 0) {
    return;
  }

  if (PREFSMAN->image_cache_ != IMGCACHE_LOW_RES_LOAD_ON_DEMAND) {
    return;
  }

  UnloadAllImages();
}

/* If in a low-res image mode, load a low-res image into memory, creating
 * the cache file if necessary.  Unlike CacheImage(), the original file will
 * not be examined unless the cached image doesn't exist, so the image will
 * not be updated if the original file changes, for efficiency. */
void ImageCache::LoadImage(std::string image_dir, std::string image_path) {
  if (image_path == "") {
    return;  // nothing to do
  }
  if (PREFSMAN->image_cache_ != IMGCACHE_LOW_RES_PRELOAD &&
      PREFSMAN->image_cache_ != IMGCACHE_LOW_RES_LOAD_ON_DEMAND) {
    return;
  }

  /* Load it. */
  const std::string cache_path = GetImageCachePath(image_dir, image_path);

  for (int tries = 0; tries < 2; ++tries) {
    {
      LockMut(image_cache_mutex);
      if (image_path_to_image.find(image_path) != image_path_to_image.end()) {
        return; /* already loaded */
      }
    }

    CHECKPOINT_M(ssprintf("ImageCache::LoadImage: %s", cache_path.c_str()));
    RageSurface* image = RageSurfaceUtils::LoadSurface(cache_path);
    if (image == nullptr) {
      if (tries == 0) {
        /* The file doesn't exist.  It's possible that the image cache file is
         * missing, so try to create it.  Don't do this first, for efficiency.
         */
        // LOG->Trace( "Cached image load of '%s' ('%s') failed, trying to cache
        // ...", sImagePath.c_str(), sCachePath.c_str() );

        /* Skip the up-to-date check; it failed to load, so it can't be up
         * to date. */
        CacheImageInternal(image_dir, image_path);
        continue;
      } else {
        // LOG->Trace( "Cached image load of '%s' ('%s') failed",
        // sImagePath.c_str(), sCachePath.c_str() );
        return;
      }
    }

    {
      LockMut(image_cache_mutex);
      if (image_path_to_image.find(image_path) != image_path_to_image.end()) {
        /* already loaded */
        delete image;
      } else {
        image_path_to_image[image_path] = image;
      }
    }
  }
}

void ImageCache::UnloadAllImages() {
  LockMut(image_cache_mutex);
  for (auto& it : image_path_to_image) {
    delete it.second;
  }

  image_path_to_image.clear();
}

ImageCache::ImageCache() : delay_save_cache_(false) { ReadFromDisk(); }

ImageCache::~ImageCache() { UnloadAllImages(); }

/* Remove all (non-directory) files in dir, which must end in '/'. */
static void EmptyDir(std::string dir) {
  ASSERT(dir[dir.size() - 1] == '/');

  std::vector<std::string> file_names;
  GetDirListing(dir, file_names);
  for (unsigned i = 0; i < file_names.size(); ++i) {
    if (!IsADirectory(dir + file_names[i])) {
      FILEMAN->Remove(dir + file_names[i]);
    }
  }
}

void ImageCache::ReadFromDisk() {
  LockMut(image_cache_mutex);
  image_data_.ReadFile(IMAGE_CACHE_INDEX);  // don't care if this fails

  int cache_version = -1;
  image_data_.GetValue("Cache", "CacheVersion", cache_version);
  if (cache_version == kImageCacheVersion) {
    return;
  }

  LOG->Trace("Image cache is out of date. Deleting all cached images.");
  std::vector<std::string> image_dir;
  split(CommonMetrics::IMAGES_TO_CACHE, ",", image_dir);
  for (unsigned i = 0; i < image_dir.size(); ++i) {
    EmptyDir(SpecialFiles::kCacheDir + image_dir[i] + "/");
  }

  image_data_.Clear();
  image_data_.SetValue("Cache", "CacheVersion", kImageCacheVersion);
  image_data_.WriteFile(IMAGE_CACHE_INDEX);
}

struct ImageTexture : public RageTexture {
  uintptr_t tex_handle_;
  uintptr_t GetTexHandle() const {
    return tex_handle_;
  };  // accessed by RageDisplay
  /* This is a reference to a pointer in g_ImagePathToImage. */
  RageSurface*& image_;
  int width_, height_;

  ImageTexture(RageTextureID id, RageSurface*& image, int width, int height)
      : RageTexture(id), image_(image), width_(width), height_(height) {
    Create();
  }

  ~ImageTexture() { Destroy(); }

  void Create() {
    ASSERT(image_ != nullptr);

    /* The image is preprocessed; do as little work as possible. */

    /* The source width is the width of the original file. */
    source_width_ = width_;
    source_height_ = height_;

    // To avoid the overhead of querying the hardware's maximum texture size
    // every time a texture is loaded, we can cache the value returned by
    // DISPLAY->GetMaxTextureSize() in RageBitmapTexture::Create(). This value
    // remains consistent throughout runtime. For example, when using OpenGL,
    // this is the value of GL_MAX_TEXTURE_SIZE. --sukibaby
    static const int kMaxTextureSize = DISPLAY->GetMaxTextureSize();

    /* The image width (within the texture) is always the entire texture.
     * Only resize if the max texture size requires it; since these images
     * are already scaled down, this shouldn't happen often. */
    if (image_->w_ > kMaxTextureSize || image_->h_ > kMaxTextureSize) {
      LOG->Warn("Converted %s at runtime", GetID().filename_.c_str());
      int width = std::min(image_->w_, kMaxTextureSize);
      int height = std::min(image_->h_, kMaxTextureSize);
      RageSurfaceUtils::Zoom(image_, width, height);
    }

    /* We did this when we cached it. */
    ASSERT(image_->w_ == power_of_two(image_->w_));
    ASSERT(image_->h_ == power_of_two(image_->h_));

    texture_width_ = image_width_ = image_->w_;
    texture_height_ = image_height_ = image_->h_;

    /* Find a supported texture format. If it happens to match the stored
     * file, we won't have to do any conversion here, and that'll happen
     * often with paletted images. */
    RagePixelFormat pf = image_->format_->bits_per_pixel_ == 8
                             ? RagePixelFormat_PAL
                             : RagePixelFormat_RGB5A1;
    if (!DISPLAY->SupportsTextureFormat(pf)) {
      pf = RagePixelFormat_RGBA4;
    }

    ASSERT(DISPLAY->SupportsTextureFormat(pf));

    ASSERT(image_ != nullptr);
    tex_handle_ = DISPLAY->CreateTexture(pf, image_, false);

    CreateFrameRects();
  }

  void Destroy() {
    if (tex_handle_) {
      DISPLAY->DeleteTexture(tex_handle_);
    }
    tex_handle_ = 0;
  }

  void Reload() {
    Destroy();
    Create();
  }

  void Invalidate() { tex_handle_ = 0; /* don't Destroy() */ }
};

/* If a image is cached, get its ID for use. */
RageTextureID ImageCache::LoadCachedImage(
    std::string image_dir, std::string image_path) {
  RageTextureID id(GetImageCachePath(image_dir, image_path));

  size_t found = image_path.find("_blank");
  if (image_path == "" || found != std::string::npos) {
    return id;
  }

  // LOG->Trace( "ImageCache::LoadCachedImage(%s): %s", sImagePath.c_str(),
  // ID.filename.c_str() );

  LockMut(image_cache_mutex);

  /* Hack: make sure Image::Load doesn't change our return value and end up
   * reloading. */
  if (image_dir == "Banner") {
    id = Sprite::SongBannerTexture(id);
  }

  /* It's not in a texture.  Do we have it loaded? */
  if (image_path_to_image.find(image_path) == image_path_to_image.end()) {
    /* Oops, the image is missing.  Warn and continue. */
    if (PREFSMAN->image_cache_ != IMGCACHE_OFF) {
      LOG->Warn("Image cache for '%s' wasn't loaded", image_path.c_str());
    }
    return id;
  }

  /* This is a reference to a pointer.  ImageTexture's ctor may change it
   * when converting; this way, the conversion will end up in the map so we
   * only have to convert once. */
  RageSurface*& image = image_path_to_image[image_path];
  ASSERT(image != nullptr);

  int source_width = 0, source_height = 0;
  image_data_.GetValue(image_path, "Width", source_width);
  image_data_.GetValue(image_path, "Height", source_height);
  if (source_width == 0 || source_height == 0) {
    LOG->UserLog("Cache file", image_path, "couldn't be loaded.");
    return id;
  }

  /* Is the image already in a texture? */
  if (TEXTUREMAN->IsTextureRegistered(id)) {
    return id; /* It's all set. */
  }

  // LOG->Trace( "Loading image texture %s; src %ix%i; image %ix%i",
  //	    ID.filename.c_str(), iSourceWidth, iSourceHeight, pImage->w,
  // pImage->h );
  RageTexture* texture =
      new ImageTexture(id, image, source_width, source_height);

  id.policy_ = RageTextureID::TEX_VOLATILE;
  TEXTUREMAN->RegisterTexture(id, texture);
  TEXTUREMAN->UnloadTexture(texture);

  return id;
}

/* Create or update the image cache file as necessary.  If in preload mode,
 * load the cache file, too.  (This is done at startup.) */
void ImageCache::CacheImage(std::string image_dir, std::string image_path) {
  if (PREFSMAN->image_cache_ != IMGCACHE_LOW_RES_PRELOAD &&
      PREFSMAN->image_cache_ != IMGCACHE_LOW_RES_LOAD_ON_DEMAND) {
    return;
  }

  CHECKPOINT_M(image_path);
  if (!DoesFileExist(image_path)) {
    return;
  }

  const std::string cache_path = GetImageCachePath(image_dir, image_path);

  /* Check the full file hash.  If it's the loaded and identical, don't recache.
   */
  if (DoesFileExist(cache_path)) {
    bool cache_up_to_date = PREFSMAN->fast_load_;
    if (!cache_up_to_date) {
      unsigned cur_full_hash;
      const unsigned full_hash = GetHashForFile(image_path);
      {
        LockMut(image_cache_mutex);
        if (image_data_.GetValue(image_path, "FullHash", cur_full_hash) &&
            cur_full_hash == full_hash) {
          cache_up_to_date = true;
        }
      }
    }

    if (cache_up_to_date) {
      /* It's identical.  Just load it, if in preload. */
      if (PREFSMAN->image_cache_ == IMGCACHE_LOW_RES_PRELOAD) {
        LoadImage(image_dir, image_path);
      }

      return;
    }
  }

  /* The cache file doesn't exist, or is out of date.  Cache it.  This
   * will also load the cache into memory if in PRELOAD. */
  CacheImageInternal(image_dir, image_path);
}

void ImageCache::CacheImageInternal(
    std::string image_dir, std::string image_path) {
  std::string error;
  RageSurface* image = RageSurfaceUtils::LoadFile(image_path, error);
  if (image == nullptr) {
    LOG->UserLog(
        "Cache file", image_path, "couldn't be loaded: %s", error.c_str());
    return;
  }

  const int source_width = image->w_, source_height = image->h_;

  /* Halve each dimension, round to the nearest power of two */
  int width = nearest_power_of_two(source_width / 2);
  int height = nearest_power_of_two(source_height / 2);

  if (image_dir == "Banner") {
    width = std::min(width, kMaxBannerCacheWidth);
    height = std::min(height, kMaxBannerCacheHeight);
  }

  RageSurfaceUtils::Zoom(image, width, height);

  /*
   * When paletted image cache is enabled, cached images are paletted.  Cached
   * 32-bit images take 1/16 as much memory, 16-bit images take 1/8, and
   * paletted images take 1/4.
   *
   * When paletted image cache is disabled, cached images are stored in 16-bit
   * RGBA.  Cached 32-bit images take 1/8 as much memory, cached 16-bit images
   * take 1/4, and cached paletted images take 1/2.
   *
   * Paletted cache is disabled by default because palettization takes time,
   * causing the initial cache run to take longer.  Also, newer ATI hardware
   * doesn't supported paletted textures, which would slow down runtime, because
   * we have to depalettize on use.  They'd still have the same memory benefits,
   * though, since we only load one cached image into a texture at once, and the
   * speed hit may not matter on newer ATI cards.  RGBA is safer, though.
   */
  if (paletted_image_cache) {
    if (image->fmt_.bytes_per_pixel_ != 1) {
      RageSurfaceUtils::Palettize(image);
    }
  } else {
    /* Dither to the final format.  We use A1RGB5, since that's usually
     * supported natively by both OpenGL and D3D. */
    RageSurface* dst =
        CreateSurface(image->w_, image->h_, 16, 0x7C00, 0x03E0, 0x001F, 0x8000);

    /* OrderedDither is still faster than ErrorDiffusionDither, and
     * these images are very small and only displayed briefly. */
    RageSurfaceUtils::OrderedDither(image, dst);
    delete image;
    image = dst;
  }

  const std::string cache_path = GetImageCachePath(image_dir, image_path);
  RageSurfaceUtils::SaveSurface(image, cache_path);

  {
    LockMut(image_cache_mutex);

    /* If an old image is loaded, free it. */
    if (image_path_to_image.find(image_path) != image_path_to_image.end()) {
      RageSurface* oldimg = image_path_to_image[image_path];
      delete oldimg;
      image_path_to_image.erase(image_path);
    }

    if (PREFSMAN->image_cache_ == IMGCACHE_LOW_RES_PRELOAD) {
      /* Keep it; we're just going to load it anyway. */
      image_path_to_image[image_path] = image;
    } else {
      delete image;
    }

    /* Remember the original size. */
    image_data_.SetValue(image_path, "Path", cache_path);
    image_data_.SetValue(image_path, "Width", source_width);
    image_data_.SetValue(image_path, "Height", source_height);
    image_data_.SetValue(image_path, "FullHash", GetHashForFile(image_path));
  }

  if (!delay_save_cache_) {
    WriteToDisk();
  }
}

void ImageCache::WriteToDisk() {
  LockMut(image_cache_mutex);
  image_data_.WriteFile(IMAGE_CACHE_INDEX);
}

/*
 * (c) 2003 Glenn Maynard
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
