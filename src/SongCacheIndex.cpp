#include "SongCacheIndex.h"

#include <cstddef>
#include <string>
#include <vector>

#include "CommonMetrics.h"
#include "RageFileManager.h"
#include "RageLog.h"
#include "RageThreads.h"
#include "RageUtil.h"
#include "Song.h"
#include "SpecialFiles.h"
#include "StdString.h"
#include "global.h"

/*
 * A quick explanation of song cache hashes: Each song has two hashes; a hash of
 * the song path, and a hash of the song directory.  The former is
 * Song::GetCacheFilePath; it stays the same if the contents of the directory
 * change.  The latter is GetHashForDirectory(m_sSongDir), and changes on each
 * modification.
 *
 * The file hash is used as the cache filename.  We don't want to use the
 * directory hash: if we do that, then we'll write a new cache file every time
 * the song changes, and they'll accumulate or we'll have to be careful to
 * delete them.
 *
 * The directory hash is stored in here, indexed by the song path, and used to
 * determine if a song has changed.
 *
 * Another advantage of this system is that we can load songs from cache given
 * only their path; we don't have to actually look in the directory (to find out
 * the directory hash) in order to find the cache file.
 */
#define CACHE_INDEX SpecialFiles::kCacheDir + "index.cache"

SongCacheIndex*
    SONGINDEX;  // global and accessible from anywhere in our program

std::string SongCacheIndex::GetCacheFilePath(
    const std::string& group, const std::string& path) {
  /* Don't use GetHashForFile, since we don't want to spend time
   * checking the file size and date. */
  std::string s;

  if (path.size() > 2 && path[0] == '/' && path[path.size() - 1] == '/') {
    s.assign(path, 1, path.size() - 2);
  } else if (path.size() > 1 && path[0] == '/') {
    s.assign(path, 1, path.size() - 1);
  } else {
    s = path;
  }
  /* Change slashes and invalid utf-8 characters to _.
   * http://en.wikipedia.org/wiki/UTF-8
   * macOS doesn't support precomposed unicode characters in files names and
   * so we should probably replace them with combining diacritics.
   * XXX How do we do this and is it even worth it? */
  const char* invalid = "/\xc0\xc1\xfe\xff\xf8\xf9\xfa\xfb\xfc\xfd\xf5\xf6\xf7";
  for (size_t pos = s.find_first_of(invalid); pos != std::string::npos;
       pos = s.find_first_of(invalid, pos)) {
    s[pos] = '_';
  }
  // CACHE_DIR ends with a /.
  return ssprintf(
      "%s%s/%s", SpecialFiles::kCacheDir.c_str(), group.c_str(), s.c_str());
}

SongCacheIndex::SongCacheIndex() : mutex_("SongCacheIndex") {
  ReadCacheIndex();
}

SongCacheIndex::~SongCacheIndex() {}

static void EmptyDir(std::string dir) {
  ASSERT(dir[dir.size() - 1] == '/');

  std::vector<std::string> cache_file_names;
  GetDirListing(dir, cache_file_names);
  for (unsigned i = 0; i < cache_file_names.size(); i++) {
    if (!IsADirectory(dir + cache_file_names[i])) {
      FILEMAN->Remove(dir + cache_file_names[i]);
    }
  }
}

void SongCacheIndex::ReadCacheIndex() {
  LockMutex l(mutex_);

  cache_index_.ReadFile(CACHE_INDEX);  // don't care if this fails

  int cache_version = -1;
  cache_index_.GetValue("Cache", "CacheVersion", cache_version);
  if (cache_version == kFileCacheVersion) {
    return;  // OK
  }

  LOG->Trace("Cache format is out of date.  Deleting all cache files.");
  EmptyDir(SpecialFiles::kCacheDir);
  EmptyDir(SpecialFiles::kCacheDir + "Songs/");
  EmptyDir(SpecialFiles::kCacheDir + "Courses/");

  std::vector<std::string> image_dir;
  split(CommonMetrics::IMAGES_TO_CACHE, ",", image_dir);
  for (unsigned c = 0; c < image_dir.size(); c++) {
    EmptyDir(SpecialFiles::kCacheDir + image_dir[c] + "/");
  }

  cache_index_.Clear();
  /* This is right now in place because our song file paths are apparently being
   * cached in two distinct areas, and songs were loading from paths in FILEMAN.
   * This is admittedly a hack for now, but this does bring up a good question
   * on whether we really need a dedicated cache for future versions of
   * StepMania.
   */
  FILEMAN->FlushDirCache();
}

void SongCacheIndex::SaveCacheIndex() {
  LockMutex l(mutex_);
  cache_index_.WriteFile(CACHE_INDEX);
}

void SongCacheIndex::AddCacheIndex(const std::string& path, unsigned hash) {
  LockMutex l(mutex_);
  if (hash == 0) {
    ++hash; /* no 0 hash values */
  }
  cache_index_.SetValue("Cache", "CacheVersion", kFileCacheVersion);
  cache_index_.SetValue("Cache", MangleName(path), hash);
  if (!delay_save_cache_) {
    cache_index_.WriteFile(CACHE_INDEX);
  }
}

unsigned SongCacheIndex::GetCacheHash(const std::string& path) const {
  LockMutex l(mutex_);
  unsigned dir_hash = 0;
  if (!cache_index_.GetValue("Cache", MangleName(path), dir_hash)) {
    return 0;
  }
  if (dir_hash == 0) {
    ++dir_hash; /* no 0 hash values */
  }
  return dir_hash;
}

std::string SongCacheIndex::MangleName(const std::string& name) {
  /* We store paths in an INI.  We can't store '='. */
  std::string ret = name;
  Replace(ret, "=", "");
  return ret;
}

/*
 * (c) 2002-2003 Glenn Maynard
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
