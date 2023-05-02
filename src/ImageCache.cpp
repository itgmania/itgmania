#include "global.h"

#include "ImageCache.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "Banner.h"
#include "PrefsManager.h"
#include "RageDisplay.h"
#include "RageLog.h"
#include "RageSurface.h"
#include "RageSurfaceUtils.h"
#include "RageSurfaceUtils_Dither.h"
#include "RageSurfaceUtils_Palettize.h"
#include "RageSurfaceUtils_Zoom.h"
#include "RageSurface_Load.h"
#include "RageTexture.h"
#include "RageTextureManager.h"
#include "RageUtil.h"
#include "SongCacheIndex.h"
#include "SpecialFiles.h"
#include "Sprite.h"

static Preference<bool> g_bPalettedImageCache("PalettedImageCache", false);

// Neither a global or a file scope static can be used for this because the
// order of initialization of nonlocal objects is unspecified.
#define IMAGE_CACHE_INDEX (SpecialFiles::CACHE_DIR + "images.cache")

// Call CacheImage to cache a image by path.  If the image is already cached,
// it'll be recreated.  This is efficient if the image hasn't changed, but we
// still only do this in TidyUpData for songs.
//
// Call LoadImage to load a cached image into main memory. This will call
// CacheImage only if needed. This will not do a date/size check; call
// CacheImage directly if you need that.
//
// Call LoadCachedImage to load a image into a texture and retrieve an ID for
// it. You can check if the image was actually preloaded by calling
// TEXTUREMAN->IsTextureRegistered() on the ID; it might not be if the image
// cache is missing or disabled.
//
// Note that each cache entries has two hashes. The cache path is based solely
// on the pathname; this way, loading the cache doesn't have to do a stat on
// every image. The full hash includes the file size and date, and is used only
// by CacheImage to avoid doing extra work.

ImageCache* IMAGECACHE;  // Global and accessible from anywhere in our program

static std::map<RString, RageSurface*> g_ImagePathToImage;
static int g_iDemandRefcount = 0;

RString ImageCache::GetImageCachePath(RString image_dir, RString image_path) {
  return SongCacheIndex::GetCacheFilePath(image_dir, image_path);
}

// If in on-demand mode, load all cached images.  This must be fast, so
// cache files will not be created if they don't exist; that should be done
// by CacheImage or LoadImage on startup.
void ImageCache::Demand(RString image_dir) {
  ++g_iDemandRefcount;
  if (g_iDemandRefcount > 1) {
    return;
  }

  if (PREFSMAN->m_ImageCache != IMGCACHE_LOW_RES_LOAD_ON_DEMAND) {
    return;
  }

  FOREACH_CONST_Child(&image_data_, p) {
    RString image_path = p->GetName();

    if (g_ImagePathToImage.find(image_path) != g_ImagePathToImage.end()) {
			// Already loaded.
      continue;
    }

    const RString cache_path = GetImageCachePath(image_dir, image_path);
    RageSurface* image = RageSurfaceUtils::LoadSurface(cache_path);
    if (image == nullptr) {
			// Doesn't exist.
      continue;
    }

    g_ImagePathToImage[image_path] = image;
  }
}

// Release images loaded on demand.
void ImageCache::Undemand(RString image_dir) {
  --g_iDemandRefcount;
  if (g_iDemandRefcount != 0) {
    return;
  }

  if (PREFSMAN->m_ImageCache != IMGCACHE_LOW_RES_LOAD_ON_DEMAND) {
    return;
  }

  UnloadAllImages();
}

// If in a low-res image mode, load a low-res image into memory, creating the4
// cache file if necessary. Unlike CacheImage(), the original file will not be
// examined unless the cached image doesn't exist, so the image will not be
// updated if the original file changes, for efficiency.
void ImageCache::LoadImage(RString image_dir, RString image_path) {
  if (image_path == "") {
		// Nothing to do.
    return;
  }
  if (PREFSMAN->m_ImageCache != IMGCACHE_LOW_RES_PRELOAD &&
      PREFSMAN->m_ImageCache != IMGCACHE_LOW_RES_LOAD_ON_DEMAND) {
    return;
  }

  // Load it.
  const RString cache_path = GetImageCachePath(image_dir, image_path);

  for (int tries = 0; tries < 2; ++tries) {
    if (g_ImagePathToImage.find(image_path) != g_ImagePathToImage.end()) {
      // Already loaded.
			return;
    }

    CHECKPOINT_M(ssprintf("ImageCache::LoadImage: %s", cache_path.c_str()));
    RageSurface* image = RageSurfaceUtils::LoadSurface(cache_path);
    if (image == nullptr) {
      if (tries == 0) {
        // The file doesn't exist. It's possible that the image cache file is
        // missing, so try to create it. Don't do this first, for efficiency.

        // Skip the up-to-date check; it failed to load, so it can't be up
        // to date.
        CacheImageInternal(image_dir, image_path);
        continue;
      } else {
        return;
      }
    }

    g_ImagePathToImage[image_path] = image;
  }
}

void ImageCache::OutputStats() const {
  int total_size = 0;
  for (const auto& it : g_ImagePathToImage) {
    const RageSurface* image = it.second;
    const int size = image->pitch * image->h;
    total_size += size;
  }
  LOG->Info("%i bytes of images loaded", total_size);
}

void ImageCache::UnloadAllImages() {
  for (auto& it : g_ImagePathToImage) {
    delete it.second;
  }

  g_ImagePathToImage.clear();
}

ImageCache::ImageCache() : delay_save_cache(false) { ReadFromDisk(); }

ImageCache::~ImageCache() { UnloadAllImages(); }

void ImageCache::ReadFromDisk() {
	// We don't care if this fails.
  image_data_.ReadFile(IMAGE_CACHE_INDEX);
}

struct ImageTexture : public RageTexture {
  std::uintptr_t tex_handle_;
	// Accessed by RageDisplay.
  std::uintptr_t GetTexHandle() const {
    return tex_handle_;
  };
  // This is a reference to a pointer in g_ImagePathToImage.
  RageSurface*& image_;
  int width_;
	int height_;

  ImageTexture(RageTextureID id, RageSurface*& image, int width, int height)
      : RageTexture(id),
        image_(image),
        width_(width),
        height_(height) {
    Create();
  }

  ~ImageTexture() { Destroy(); }

  void Create() {
    ASSERT(image_ != nullptr);

    // The image is preprocessed; do as little work as possible.

    // The source width is the width of the original file.
    m_iSourceWidth = width_;
    m_iSourceHeight = height_;

    // The image width (within the texture) is always the entire texture.
    // Only resize if the max texture size requires it; since these images
    // are already scaled down, this shouldn't happen often.
    if (image_->w > DISPLAY->GetMaxTextureSize() ||
        image_->h > DISPLAY->GetMaxTextureSize()) {
      LOG->Warn("Converted %s at runtime", GetID().filename.c_str());
      int width = std::min(image_->w, DISPLAY->GetMaxTextureSize());
      int height = std::min(image_->h, DISPLAY->GetMaxTextureSize());
      RageSurfaceUtils::Zoom(image_, width, height);
    }

    // We did this when we cached it.
    ASSERT(image_->w == power_of_two(image_->w));
    ASSERT(image_->h == power_of_two(image_->h));

    m_iTextureWidth = m_iImageWidth = image_->w;
    m_iTextureHeight = m_iImageHeight = image_->h;

    // Find a supported texture format. If it happens to match the stored file,
		// we won't have to do any conversion here, and that'll happen often with
		// paletted images.
    RagePixelFormat pixel_format =
				image_->format->BitsPerPixel == 8
        		? RagePixelFormat_PAL
            : RagePixelFormat_RGB5A1;
    if (!DISPLAY->SupportsTextureFormat(pixel_format)) {
      pixel_format = RagePixelFormat_RGBA4;
    }

    ASSERT(DISPLAY->SupportsTextureFormat(pixel_format));

    ASSERT(image_ != nullptr);
    tex_handle_ = DISPLAY->CreateTexture(pixel_format, image_, false);

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

  void Invalidate() {
		// don't Destroy()
		tex_handle_ = 0;
	}
};

// If a image is cached, get its ID for use.
RageTextureID ImageCache::LoadCachedImage(
    RString image_dir, RString image_path) {
  RageTextureID id(GetImageCachePath(image_dir, image_path));

  std::size_t found = image_path.find("_blank");
  if (image_path == "" || found != RString::npos) {
    return id;
  }

  // make sure Image::Load doesn't change our return value and end up reloading.
  if (image_dir == "Banner") {
    id = Sprite::SongBannerTexture(id);
  }

  // It's not in a texture. Do we have it loaded?
  if (g_ImagePathToImage.find(image_path) == g_ImagePathToImage.end()) {
    // Oops, the image is missing. Warn and continue.
    if (PREFSMAN->m_ImageCache != IMGCACHE_OFF) {
      LOG->Warn("Image cache for '%s' wasn't loaded", image_path.c_str());
    }
    return id;
  }

  // This is a reference to a pointer. ImageTexture's ctor may change it when
	// converting; this way, the conversion will end up in the map so we only have
	// to convert once.
  RageSurface*& image = g_ImagePathToImage[image_path];
  ASSERT(image != nullptr);

  int source_width = 0;
	int source_height = 0;
  image_data_.GetValue(image_path, "Width", source_width);
  image_data_.GetValue(image_path, "Height", source_height);
  if (source_width == 0 || source_height == 0) {
    LOG->UserLog("Cache file", image_path, "couldn't be loaded.");
    return id;
  }

  // Is the image already in a texture?
  if (TEXTUREMAN->IsTextureRegistered(id)) {
		// It's all set.
    return id;
  }

  RageTexture* texture =
      new ImageTexture(id, image, source_width, source_height);

  id.Policy = RageTextureID::TEX_VOLATILE;
  TEXTUREMAN->RegisterTexture(id, texture);
  TEXTUREMAN->UnloadTexture(texture);

  return id;
}

static inline int closest(int num, int n1, int n2) {
  if (std::abs(num - n1) > std::abs(num - n2)) {
    return n2;
  }
  return n1;
}

// Create or update the image cache file as necessary. If in preload mode,
// load the cache file, too. (This is done at startup.)
void ImageCache::CacheImage(RString image_dir, RString image_path) {
  if (PREFSMAN->m_ImageCache != IMGCACHE_LOW_RES_PRELOAD &&
      PREFSMAN->m_ImageCache != IMGCACHE_LOW_RES_LOAD_ON_DEMAND) {
    return;
  }

  CHECKPOINT_M(image_path);
  if (!DoesFileExist(image_path)) {
    return;
  }

  const RString cache_path = GetImageCachePath(image_dir, image_path);

  // Check the full file hash. If it's the loaded and identical, don't recache.
  if (DoesFileExist(cache_path)) {
    bool cache_up_to_date = PREFSMAN->m_bFastLoad;
    if (!cache_up_to_date) {
      unsigned cur_full_hash;
      const unsigned full_hash = GetHashForFile(image_path);
      if (image_data_.GetValue(image_path, "FullHash", cur_full_hash) &&
          cur_full_hash == full_hash) {
        cache_up_to_date = true;
      }
    }

    if (cache_up_to_date) {
      // It's identical. Just load it, if in preload.
      if (PREFSMAN->m_ImageCache == IMGCACHE_LOW_RES_PRELOAD) {
        LoadImage(image_dir, image_path);
      }

      return;
    }
  }

  // The cache file doesn't exist, or is out of date. Cache it. This will also
	// load the cache into memory if in PRELOAD.
  CacheImageInternal(image_dir, image_path);
}

void ImageCache::CacheImageInternal(RString image_dir, RString image_path) {
  RString error;
  RageSurface* image = RageSurfaceUtils::LoadFile(image_path, error);
  if (image == nullptr) {
    LOG->UserLog(
        "Cache file", image_path, "couldn't be loaded: %s", error.c_str());
    return;
  }

  const int source_width = image->w;
	const int source_height = image->h;

  int width = image->w / 2;
	int height = image->h / 2;

  // Round to the nearest power of two. This simplifies the actual texture load.
  width = closest(width, power_of_two(width), power_of_two(width) / 2);
  height = closest(height, power_of_two(height), power_of_two(height) / 2);

  // Don't resize the image to less than 32 pixels in either dimension or the
  // next power of two of the source (whichever is smaller); it's already very
  // low res.
  width = std::max(width, std::min(32, power_of_two(source_width)));
  height = std::max(height, std::min(32, power_of_two(source_height)));

  RageSurfaceUtils::Zoom(image, width, height);

  // When paletted image cache is enabled, cached images are paletted. Cached
  // 32-bit images take 1/16 as much memory, 16-bit images take 1/8, and
  // paletted images take 1/4.
  //
  // When paletted image cache is disabled, cached images are stored in 16-bit
  // RGBA. Cached 32-bit images take 1/8 as much memory, cached 16-bit images
  // take 1/4, and cached paletted images take 1/2.
  //
  // Paletted cache is disabled by default because palettization takes time,
  // causing the initial cache run to take longer. Also, newer ATI hardware
  // doesn't supported paletted textures, which would slow down runtime, because
  // we have to depalettize on use. They'd still have the same memory benefits,
  // though, since we only load one cached image into a texture at once, and the
  // speed hit may not matter on newer ATI cards. RGBA is safer, though.
  if (g_bPalettedImageCache) {
    if (image->fmt.BytesPerPixel != 1) {
      RageSurfaceUtils::Palettize(image);
    }
  } else {
    // Dither to the final format. We use A1RGB5, since that's usually supported
		// natively by both OpenGL and D3D.
    RageSurface* dst =
        CreateSurface(image->w, image->h, 16, 0x7C00, 0x03E0, 0x001F, 0x8000);

    // OrderedDither is still faster than ErrorDiffusionDither, and these images
		// are very small and only displayed briefly. */
    RageSurfaceUtils::OrderedDither(image, dst);
    delete image;
    image = dst;
  }

  const RString cache_path = GetImageCachePath(image_dir, image_path);
  RageSurfaceUtils::SaveSurface(image, cache_path);

  //. If an old image is loaded, free it.
  if (g_ImagePathToImage.find(image_path) != g_ImagePathToImage.end()) {
    RageSurface* old_img = g_ImagePathToImage[image_path];
    delete old_img;
    g_ImagePathToImage.erase(image_path);
  }

  if (PREFSMAN->m_ImageCache == IMGCACHE_LOW_RES_PRELOAD) {
    // Keep it; we're just going to load it anyway.
    g_ImagePathToImage[image_path] = image;
  } else {
    delete image;
  }

  // Remember the original size.
  image_data_.SetValue(image_path, "Path", cache_path);
  image_data_.SetValue(image_path, "Width", source_width);
  image_data_.SetValue(image_path, "Height", source_height);
  image_data_.SetValue(image_path, "FullHash", GetHashForFile(image_path));
  if (!delay_save_cache) {
    WriteToDisk();
  }
}

void ImageCache::WriteToDisk() { image_data_.WriteFile(IMAGE_CACHE_INDEX); }

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
