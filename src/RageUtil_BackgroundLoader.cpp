#include "RageUtil_BackgroundLoader.h"

#include <map>
#include <string>
#include <vector>

#include "RageFile.h"
#include "RageFileManager.h"
#include "RageLog.h"
#include "RageThreads.h"
#include "RageUtil.h"
#include "global.h"

/* If we're on an OS with a good caching system, writing to our own cache will
 * only waste memory.  In that case, just read the file, to force it into system
 * cache. If we're on a system with an unreliable cache, read it into our own
 * cache.  This helps in Windows, where even reading directory entries off of a
 * CD--even if they've just been read--can cause long delays if the disk is in
 * use. */
static const bool g_bWriteToCache = true;

static const bool g_bEnableBackgroundLoading = false;

BackgroundLoader::BackgroundLoader()
    : start_sem_("BackgroundLoaderSem"), mutex_("BackgroundLoaderMutex") {
  if (!g_bEnableBackgroundLoading) {
    return;
  }

  cache_path_prefix_ = ssprintf("@mem/%p", static_cast<void*>(this));

  shutdown_thread_ = false;
  thread_is_active_ = thread_should_abort_ = false;
  load_thread_.SetName("BackgroundLoader");
  load_thread_.Create(LoadThread_Start, this);
}

static void DeleteEmptyDirectories(std::string dir) {
  std::vector<std::string> new_dirs;
  GetDirListing(dir + "/*", new_dirs, false, true);
  for (unsigned i = 0; i < new_dirs.size(); ++i) {
    ASSERT_M(IsADirectory(new_dirs[i]), new_dirs[i]);
    DeleteEmptyDirectories(new_dirs[i]);
  }

  FILEMAN->Remove(dir);
}

BackgroundLoader::~BackgroundLoader() {
  if (!g_bEnableBackgroundLoading) {
    return;
  }

  Abort();

  shutdown_thread_ = true;
  start_sem_.Post();
  load_thread_.Wait();

  /* Delete all leftover cached files. */
  std::map<std::string, int>::iterator it;
  for (it = finished_requests_.begin(); it != finished_requests_.end(); ++it) {
    FILEMAN->Remove(GetCachePath(it->first));
  }

  /* m_sCachePathPrefix should be filled with several empty directories.  Delete
   * them and m_sCachePathPrefix, so we don't leak them. */
  DeleteEmptyDirectories(cache_path_prefix_);
}

/* Pull a request out of m_CacheRequests. */
std::string BackgroundLoader::GetRequest() {
  if (!g_bEnableBackgroundLoading) {
    return std::string();
  }

  LockMut(mutex_);
  if (!cache_requests_.size()) {
    return std::string();
  }

  std::string ret;
  ret = cache_requests_.front();
  cache_requests_.erase(cache_requests_.begin(), cache_requests_.begin() + 1);
  return ret;
}

std::string BackgroundLoader::GetCachePath(std::string path) const {
  return cache_path_prefix_ + path;
}

void BackgroundLoader::LoadThread() {
  while (!shutdown_thread_) {
    /* Wait for a request.  It's normal for this to wait for a long time; don't
     * fail on timeout. */
    start_sem_.Wait(false);

    std::string file = GetRequest();
    if (file.empty()) {
      continue;
    }

    {
      /* If the file already exists, short circuit. */
      LockMut(mutex_);
      std::map<std::string, int>::iterator it;
      it = finished_requests_.find(file);
      if (it != finished_requests_.end()) {
        ++it->second;
        LOG->Trace(
            "XXX: request %s done loading (already done), cnt now %i",
            file.c_str(), finished_requests_[file]);
        continue;
      }
    }

    thread_is_active_ = true;

    LOG->Trace("XXX: reading %s", file.c_str());

    std::string cache_path = GetCachePath(file);

    /* Open the file and read it. */
    RageFile src;
    if (src.Open(file)) {
      /* If we're writing to a file cache ... */
      RageFile dst;

      bool write_to_cache = g_bWriteToCache;
      if (write_to_cache) {
        write_to_cache = dst.Open(cache_path, RageFile::WRITE);
      }
      LOG->Trace("XXX: go on '%s' to '%s'", file.c_str(), cache_path.c_str());

      char buf[1024 * 4];
      while (!thread_should_abort_ && !src.AtEOF()) {
        int got = src.Read(buf, sizeof(buf));
        if (got > 0 && write_to_cache) {
          dst.Write(buf, got);
        }
      }
      if (write_to_cache) {
        dst.Close();
      }

      LOG->Trace("XXX: done");
    }
    src.Close();

    LockMut(mutex_);
    if (!thread_should_abort_) {
      ++finished_requests_[file];
      LOG->Trace(
          "XXX: request %s done loading, cnt now %i", file.c_str(),
          finished_requests_[file]);
    } else {
      FILEMAN->Remove(cache_path);

      LOG->Trace("XXX: request %s aborted", file.c_str());
    }

    thread_should_abort_ = false;
    thread_is_active_ = false;
  }
}

void BackgroundLoader::CacheFile(const std::string& file) {
  if (!g_bEnableBackgroundLoading) {
    return;
  }

  if (file == "") {
    return;
  }

  LockMut(mutex_);
  cache_requests_.push_back(file);
  start_sem_.Post();
}

bool BackgroundLoader::IsCacheFileFinished(
    const std::string& file, std::string& actual_path) {
  if (!g_bEnableBackgroundLoading) {
    actual_path = file;
    return true;
  }

  LockMut(mutex_);

  if (file == "") {
    actual_path = "";
    return true;
  }

  std::map<std::string, int>::iterator it;
  it = finished_requests_.find(file);
  if (it == finished_requests_.end()) {
    return false;
  }

  LOG->Trace("XXX: %s finished (%i)", file.c_str(), it->second);
  if (g_bWriteToCache) {
    actual_path = GetCachePath(file);
  } else {
    actual_path = file;
  }

  return true;
}

void BackgroundLoader::FinishedWithCachedFile(std::string file) {
  if (!g_bEnableBackgroundLoading) {
    return;
  }

  if (file == "") {
    return;
  }

  std::map<std::string, int>::iterator it;
  it = finished_requests_.find(file);
  ASSERT_M(it != finished_requests_.end(), file);

  --it->second;
  ASSERT_M(it->second >= 0, ssprintf("%i", it->second));
  if (!it->second) {
    finished_requests_.erase(it);
    FILEMAN->Remove(GetCachePath(file));
  }
}

void BackgroundLoader::Abort() {
  if (!g_bEnableBackgroundLoading) {
    return;
  }

  LockMut(mutex_);

  /* Clear any pending requests. */
  while (!GetRequest().empty());

  /* Clear any previously finished requests. */
  while (finished_requests_.size()) {
    FinishedWithCachedFile(finished_requests_.begin()->first);
  }

  /* Tell the thread to abort any request it's handling now. */
  if (thread_is_active_) {
    thread_should_abort_ = true;
  }
}

/*
 * (c) 2004 Glenn Maynard
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
