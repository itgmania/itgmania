#include "RageFileDriverDirectHelpers.h"

#include <chrono>
#include <filesystem>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#include "RageThreads.h"
#include "RageUtil.h"
#include "RageUtil_FileDB.h"
#include "StdString.h"
#include "global.h"

#if defined(_WIN32)
// clang-format off
#include <windows.h>
// clang-format on
#endif

namespace filesystem = std::filesystem;

static bool GetFileMetadata(
    const filesystem::path& path, File& file, std::error_code& error) {
  const filesystem::file_status status = filesystem::status(path, error);
  if (error || !filesystem::exists(status)) {
    if (!error) {
      error = std::make_error_code(std::errc::no_such_file_or_directory);
    }
    return false;
  }

  file.dir = filesystem::is_directory(status);
  if (filesystem::is_regular_file(status)) {
    const auto size = filesystem::file_size(path, error);
    if (!error) {
      file.size = static_cast<int>(size);
    }
  }

  error.clear();
  const auto writeTime = filesystem::last_write_time(path, error);
  if (!error) {
    file.hash = static_cast<int>(writeTime.time_since_epoch().count());
  }
  error.clear();
  return true;
}

std::string DoPathReplace(const std::string& sPath) {
  std::string TempPath = sPath;
  return TempPath;
}

#if defined(_WIN32)
static bool WinMoveFileInternal(
    const std::string& sOldPath, const std::string& sNewPath) {
  if (MoveFileEx(
          sOldPath.c_str(), sNewPath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
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
        "MoveFileEx(%s, %s) failed: %lu", sOldPath.c_str(), sNewPath.c_str(),
        err));

    // Check if the error is related to the file not existing
    if (err != ERROR_FILE_EXISTS && err != ERROR_ALREADY_EXISTS) {
      return false;
    }
  }

  if (!DeleteFile(sNewPath.c_str())) {
    return false;
  }

  return !!MoveFile(sOldPath.c_str(), sNewPath.c_str());
}

bool WinMoveFile(std::string sOldPath, std::string sNewPath) {
  if (WinMoveFileInternal(DoPathReplace(sOldPath), DoPathReplace(sNewPath))) {
    return true;
  }
  if (GetLastError() != ERROR_ACCESS_DENIED) {
    return false;
  }
  /* Try turning off the read-only bit on the file we're overwriting. */
  SetFileAttributes(DoPathReplace(sNewPath).c_str(), FILE_ATTRIBUTE_NORMAL);

  return WinMoveFileInternal(DoPathReplace(sOldPath), DoPathReplace(sNewPath));
}
#endif

/* mkdir -p.  Doesn't fail if Path already exists and is a directory. */
bool CreateDirectories(std::string Path) {
  namespace fs = std::filesystem;
  const fs::path path = fs::u8path(Path);
  std::error_code error;
  fs::create_directories(path, error);
  if (!error && fs::is_directory(path, error)) {
    return true;
  }

  if (!error) {
    error = std::make_error_code(std::errc::not_a_directory);
  }
  WARN(ssprintf(
      "Couldn't create %s: %s", Path.c_str(), error.message().c_str()));
  return false;
}

DirectFilenameDB::DirectFilenameDB(std::string root_) {
  ExpireSeconds = 30;
  SetRoot(root_);
}

void DirectFilenameDB::SetRoot(std::string root_) {
  root = root_;

  // "\abcd\" -> "/abcd/":
  Replace(root, "\\", "/");

  // "/abcd/" -> "/abcd":
  if (Right(root, 1) == "/") {
    root.erase(root.size() - 1, 1);
  }
}

void DirectFilenameDB::CacheFile(const std::string& sPath) {
  CHECKPOINT_M(root + sPath);
  std::string sDir = Dirname(sPath);
  FileSet* pFileSet = GetFileSet(sDir, false);
  if (pFileSet == nullptr) {
    // This directory isn't cached so do nothing.
    m_Mutex.Unlock();  // Locked by GetFileSet()
    return;
  }
  while (!pFileSet->m_bFilled) {
    m_Mutex.Wait();
  }

  File f(Basename(sPath));
  std::error_code error;
  if (!GetFileMetadata(filesystem::u8path(root + sPath), f, error)) {
    if (error != std::errc::no_such_file_or_directory) {
      // Warn in this case since this is unexpected (maybe due to some actual
      // filesystem issues like perms/long paths/etc).
      // ENOENT is expected in some cases (like when we remove the file
      // intentionally on Ctrl+Shift+R reloads/using the tempfile + rename
      // flow, etc) so we don't want to log that as a warning.
      WARN(ssprintf(
          "Couldn't stat '%s' while caching file metadata: %s", sPath.c_str(),
          error.message().c_str()));
    }
    // Remove any old cached records and return early so we con't insert
    // any bad metadata for a file that doesn't exist.
    pFileSet->files.erase(f);
    m_Mutex.Unlock();  // Locked by GetFileSet()
    return;
  }

  pFileSet->files.insert(f);
  m_Mutex.Unlock();  // Locked by GetFileSet()
}

void DirectFilenameDB::PopulateFileSet(FileSet& fs, const std::string& path) {
  std::string sPath = path;

  // Resolve path cases (path/Path -> PATH/path).
  ResolvePath(sPath);

  fs.age.GetDeltaTime();  // reset
  fs.files.clear();

  const filesystem::path directory = filesystem::u8path(root + sPath);
  std::error_code iterationError;
  filesystem::directory_iterator entry(directory, iterationError);
  if (iterationError) {
    return;
  }

  const filesystem::directory_iterator end;
  while (entry != end) {
    const filesystem::directory_entry current = *entry;
    std::error_code statusError;
    File file(current.path().filename().u8string());
    if (!GetFileMetadata(current.path(), file, statusError)) {
      std::error_code linkError;
      if (!(current.is_symlink(linkError) && !linkError)) {
        WARN(ssprintf(
            "Got file '%s' in '%s' from list, but can't stat: %s",
            current.path().u8string().c_str(), sPath.c_str(),
            statusError.message().c_str()));
      }
    } else {
      fs.files.insert(file);
    }

    entry.increment(iterationError);
    if (iterationError) {
      WARN(ssprintf(
          "Couldn't continue listing '%s': %s", directory.u8string().c_str(),
          iterationError.message().c_str()));
      break;
    }
  }

  /*
   * Check for any ".ignore" markers.  If a marker exists, hide the marker and
   * its corresponding file. For example, if "file.xml.ignore" exists, hide both
   * it and "file.xml" by removing them from the file set. Ignore markers are
   * used for convenience during build staging and are not used in
   * performance-critical situations.  To avoid incurring some of the overheard
   * due to ignore markers, delete the file instead instead of using an ignore
   * marker.
   */
  static const std::string IGNORE_MARKER_BEGINNING = "ignore-";

  std::vector<std::string> vsFilesToRemove;
  for (std::set<File>::iterator iter =
           fs.files.lower_bound(IGNORE_MARKER_BEGINNING);
       iter != fs.files.end(); ++iter) {
    if (!BeginsWith(iter->lname, IGNORE_MARKER_BEGINNING)) {
      break;
    }
    std::string sFileLNameToIgnore = Right(
        iter->lname, iter->lname.length() - IGNORE_MARKER_BEGINNING.length());
    vsFilesToRemove.push_back(iter->name);
    vsFilesToRemove.push_back(sFileLNameToIgnore);
  }

  for (const std::string& iter : vsFilesToRemove) {
    // Erase the file corresponding to the ignore marker
    File fileToDelete;
    fileToDelete.SetName(iter);
    std::set<File>::iterator iter2 = fs.files.find(fileToDelete);
    if (iter2 != fs.files.end()) {
      fs.files.erase(iter2);
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
