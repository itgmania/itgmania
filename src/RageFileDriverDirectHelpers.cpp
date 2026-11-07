#include "RageFileDriverDirectHelpers.h"

#include <cerrno>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "RageThreads.h"
#include "RageUtil.h"
#include "RageUtil_FileDB.h"
#include "StdString.h"
#include "global.h"

#if defined(_WIN32)
// clang-format off
#include <windows.h>
#include <io.h>
// clang-format on
#else
#include <dirent.h>
#endif

std::string DoPathReplace(const std::string& path) {
  std::string temp_path = path;
  return temp_path;
}

#if defined(_WIN32)
static bool WinMoveFileInternal(
    const std::string& old_path, const std::string& new_path) {
  if (MoveFileEx(
          old_path.c_str(), new_path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
    return true;
  }

  DWORD err = GetLastError();
  // Possible error values stored by GetLastError():
  //
  // - ERROR_PATH_NOT_FOUND - 3
  //  - implies something in the file path does not exist
  //
  // - ERROR_SHARING_VIOLATION - 32
  //  - implies a need to use a temporary name somewhere
  //
  // - ERROR_FILE_EXISTS, ERROR_ALREADY_EXISTS - 80
  //  - implies MOVEFILE_REPLACE_EXISTING flag is not set,
  //    but it is usually expected behavior when this occurs
  //
  // - ERROR_INVALID_PARAMETER - 87
  //  - implies the file paths are invalid
  //
  // - ERROR_NOT_SAME_DEVICE - 17
  //  - implies the file paths are on different devices

  if (err) {
    // Log the error with the specific error code
    WARN(ssprintf(
        "MoveFileEx(%s, %s) failed: %lu", old_path.c_str(), new_path.c_str(),
        err));

    // Check if the error is related to the file not existing
    if (err != ERROR_FILE_EXISTS && err != ERROR_ALREADY_EXISTS) {
      return false;
    }
  }

  if (!DeleteFile(new_path.c_str())) {
    return false;
  }

  return !!MoveFile(old_path.c_str(), new_path.c_str());
}

bool WinMoveFile(std::string old_path, std::string new_path) {
  if (WinMoveFileInternal(DoPathReplace(old_path), DoPathReplace(new_path))) {
    return true;
  }
  if (GetLastError() != ERROR_ACCESS_DENIED) {
    return false;
  }
  /* Try turning off the read-only bit on the file we're overwriting. */
  SetFileAttributes(DoPathReplace(new_path).c_str(), FILE_ATTRIBUTE_NORMAL);

  return WinMoveFileInternal(DoPathReplace(old_path), DoPathReplace(new_path));
}
#endif

/* mkdir -p.  Doesn't fail if Path already exists and is a directory. */
bool CreateDirectories(std::string path) {
  // XXX: handle "//foo/bar" paths in Windows
  std::vector<std::string> parts;
  std::string curpath;

  // If Path is absolute, add the initial slash ("ignore empty" will remove it).
  if (Left(path, 1) == "/") {
    curpath = "/";
  }

  // Ignore empty, so eg. "/foo/bar//baz" doesn't try to create "/foo/bar"
  // twice.
  split(path, "/", parts, true);

  for (unsigned i = 0; i < parts.size(); ++i) {
    if (i) {
      curpath += "/";
    }
    curpath += parts[i];

#if defined(_WIN32)
    if (curpath.size() == 2 && curpath[1] == ':') /* C: */
    {
      /* Don't try to create the drive letter alone. */
      continue;
    }
#endif

    if (DoMkdir(curpath.c_str(), 0777) == 0) {
      continue;
    }

#if defined(_WIN32)
    /* When creating a directory that already exists over Samba, Windows is
     * returning ENOENT instead of EEXIST. */
    /* I can't reproduce this anymore.  If we get ENOENT, log it but keep
     * going. */
    if (errno == ENOENT) {
      WARN(
          ssprintf("Couldn't create %s: %s", curpath.c_str(), strerror(errno)));
      errno = EEXIST;
    }
#endif

    if (errno == EEXIST) {
      /* Make sure it's a directory. */
      struct stat st;
      if (DoStat(curpath.c_str(), &st) != -1 && !(st.st_mode & S_IFDIR)) {
        WARN(ssprintf(
            "Couldn't create %s: path exists and is not a directory",
            curpath.c_str()));
        return false;
      }

      continue;  // we expect to see this error
    }

    WARN(ssprintf("Couldn't create %s: %s", curpath.c_str(), strerror(errno)));
    return false;
  }

  return true;
}

DirectFilenameDB::DirectFilenameDB(std::string root) {
  expire_seconds_ = 30;
  SetRoot(root);
}

void DirectFilenameDB::SetRoot(std::string root) {
  root_ = root;

  // "\abcd\" -> "/abcd/":
  Replace(root_, "\\", "/");

  // "/abcd/" -> "/abcd":
  if (Right(root_, 1) == "/") {
    root_.erase(root_.size() - 1, 1);
  }
}

void DirectFilenameDB::CacheFile(const std::string& path) {
  CHECKPOINT_M(root_ + path);
  std::string dir = Dirname(path);
  FileSet* file_set = GetFileSet(dir, false);
  if (file_set == nullptr) {
    // This directory isn't cached so do nothing.
    mutex_.Unlock();  // Locked by GetFileSet()
    return;
  }
  while (!file_set->filled_) {
    mutex_.Wait();
  }

#if defined(_WIN32)
  WIN32_FIND_DATA fd;
  HANDLE find_handle = DoFindFirstFile((root_ + path).c_str(), &fd);
  if (find_handle == INVALID_HANDLE_VALUE) {
    mutex_.Unlock();  // Locked by GetFileSet()
    return;
  }
  File f(fd.cFileName);
  f.dir_ = !!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
  f.size_ = static_cast<int64_t>(fd.nFileSizeHigh) << 32 | fd.nFileSizeLow;
  f.hash_ = static_cast<int64_t>(fd.ftLastWriteTime.dwHighDateTime) << 32 |
            fd.ftLastWriteTime.dwLowDateTime;

  file_set->files_.insert(f);
  FindClose(find_handle);
#else
  File f(Basename(path));

  struct stat st;
  if (DoStat((root_ + path).c_str(), &st) == -1) {
    if (errno != ENOENT) {
      // Warn in this case since this is unexpected (maybe due to some actual
      // filesystem issues like perms/long paths/etc).
      // ENOENT is expected in some cases (like when we remove the file
      // intentionally on Ctrl+Shift+R reloads/using the tempfile + rename
      // flow, etc) so we don't want to log that as a warning.
      WARN(ssprintf(
          "Couldn't stat '%s' while caching file metadata: %s", path.c_str(),
          strerror(errno)));
    }
    // Remove any old cached records and return early so we con't insert
    // any bad metadata for a file that doesn't exist.
    file_set->files_.erase(f);
    mutex_.Unlock();  // Locked by GetFileSet()
    return;
  } else {
    f.dir_ = S_ISDIR(st.st_mode);
    f.size_ = static_cast<int>(st.st_size);
    f.hash_ = st.st_mtime;
  }

  file_set->files_.insert(f);
#endif
  mutex_.Unlock();  // Locked by GetFileSet()
}

void DirectFilenameDB::PopulateFileSet(
    FileSet& fs, const std::string& raw_path) {
  std::string path = raw_path;

  // Resolve path cases (path/Path -> PATH/path).
  ResolvePath(path);

  fs.age_.GetDeltaTime();  // reset
  fs.files_.clear();

#if defined(_WIN32)
  WIN32_FIND_DATA fd;

  if (path.size() > 0 && Right(path, 1) == "/") {
    path.erase(path.size() - 1);
  }

  HANDLE find_handle = DoFindFirstFile((root_ + path + "/*").c_str(), &fd);
  CHECKPOINT_M(root_ + path + "/*");

  if (find_handle == INVALID_HANDLE_VALUE) {
    return;
  }

  do {
    if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) {
      continue;
    }

    File f;
    f.SetName(fd.cFileName);
    f.dir_ = !!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
    f.size_ = fd.nFileSizeLow;
    f.hash_ = fd.ftLastWriteTime.dwLowDateTime;

    fs.files_.insert(f);
  } while (FindNextFile(find_handle, &fd));
  FindClose(find_handle);
#else
  /* Ugly: POSIX threads are not guaranteed to have per-thread cwds, and only
   * a few systems have openat() or equivalent; one or the other is needed
   * to do efficient, thread-safe directory traversal.  Instead, we have to
   * use absolute paths, which forces the system to re-parse the directory
   * for each file.  This isn't a major issue, since most large directory
   * scans are I/O-bound. */

  DIR* dir = opendir((root_ + path).c_str());
  if (dir == nullptr) {
    return;
  }

  while (struct dirent* ent = readdir(dir)) {
    if (!strcmp(ent->d_name, ".")) {
      continue;
    }
    if (!strcmp(ent->d_name, "..")) {
      continue;
    }

    File f(ent->d_name);

    struct stat st;
    if (DoStat((root_ + path + "/" + ent->d_name).c_str(), &st) == -1) {
      int error = errno;
      /* If it's a broken symlink, ignore it.  Otherwise, warn. */
      if (lstat((root_ + path + "/" + ent->d_name).c_str(), &st) == 0) {
        continue;
      }

      /* Huh? */
      WARN(ssprintf(
          "Got file '%s' in '%s' from list, but can't stat? (%s)", ent->d_name,
          path.c_str(), strerror(error)));
      continue;
    } else {
      f.dir_ = (st.st_mode & S_IFDIR);
      f.size_ = (int)st.st_size;
      f.hash_ = st.st_mtime;
    }

    fs.files_.insert(f);
  }

  closedir(dir);
#endif

  /*
   * Check for any ".ignore" markers.  If a marker exists, hide the marker and
   * its corresponding file. For example, if "file.xml.ignore" exists, hide both
   * it and "file.xml" by removing them from the file set. Ignore markers are
   * used for convenience during build staging and are not used in
   * performance-critical situations.  To avoid incurring some of the overheard
   * due to ignore markers, delete the file instead instead of using an ignore
   * marker.
   */
  static const std::string kIgnoreMarkerBeginning = "ignore-";

  std::vector<std::string> files_to_remove;
  for (std::set<File>::iterator iter =
           fs.files_.lower_bound(kIgnoreMarkerBeginning);
       iter != fs.files_.end(); ++iter) {
    if (!BeginsWith(iter->lname_, kIgnoreMarkerBeginning)) {
      break;
    }
    std::string file_l_name_to_ignore = Right(
        iter->lname_, iter->lname_.length() - kIgnoreMarkerBeginning.length());
    files_to_remove.push_back(iter->name_);
    files_to_remove.push_back(file_l_name_to_ignore);
  }

  for (const std::string& iter : files_to_remove) {
    // Erase the file corresponding to the ignore marker
    File file_to_delete;
    file_to_delete.SetName(iter);
    std::set<File>::iterator iter2 = fs.files_.find(file_to_delete);
    if (iter2 != fs.files_.end()) {
      fs.files_.erase(iter2);
    }
  }
}

/*
 * Copyright (c) 2003-2005 Glenn Maynard, Chris Danford, Renaud Lepage
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
