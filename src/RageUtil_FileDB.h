#ifndef RAGE_UTIL_FILEDB
#define RAGE_UTIL_FILEDB

#include <map>
#include <set>
#include <string>
#include <vector>

#include "RageFileManager.h"
#include "RageThreads.h"
#include "RageTimer.h"
#include "StdString.h"

struct FileSet;
struct File {
  std::string name_;
  std::string lname_;

  void SetName(const std::string& fn) {
    name_ = fn;
    lname_ = name_;
    MakeLower(lname_);
  }

  bool dir_;
  int size_;
  /* Modification time of the file.  The contents of this is undefined, except
   * that when the file has been modified, this value will change. */
  int hash_;

  /* Private data, for RageFileDrivers. */
  void* priv_;

  /* If this is non-nullptr, and dir is true, this is a pointer to the FileSet
   * containing the directory contents.  (This is a cache; it isn't always set.)
   */
  const FileSet* dirp_;

  File() {
    dir_ = false;
    dirp_ = nullptr;
    size_ = -1;
    hash_ = -1;
    priv_ = nullptr;
  }
  File(const std::string& fn) {
    SetName(fn);
    dir_ = false;
    size_ = -1;
    hash_ = -1;
    priv_ = nullptr;
    dirp_ = nullptr;
  }

  bool operator<(const File& rhs) const { return lname_ < rhs.lname_; }

  bool equal(const File& rhs) const { return lname_ == rhs.lname_; }
  bool equal(const std::string& rhs) const {
    std::string l = rhs;
    MakeLower(l);
    return lname_ == l;
  }
};

inline bool operator==(const File& lhs, const File& rhs) {
  return lhs.lname_ == rhs.lname_;
}
inline bool operator!=(const File& lhs, const File& rhs) {
  return !operator==(lhs, rhs);
}

/** @brief This represents a directory. */
struct FileSet {
  std::set<File> files_;
  RageTimer age_;

  /*
   * If m_bFilled is false, this FileSet hasn't completed being filled in yet;
   * it's owned by the thread filling it in.  Wait on FilenameDB::m_Mutex and
   * retry until it becomes true.
   */
  bool filled_;

  FileSet() { filled_ = true; }

  void GetFilesMatching(
      const std::string& beginning, const std::string& containing,
      const std::string& ending, std::vector<std::string>& out,
      bool only_dirs) const;
  void GetFilesEqualTo(
      const std::string& pat, std::vector<std::string>& out,
      bool only_dirs) const;

  RageFileManager::FileType GetFileType(const std::string& path) const;
  int GetFileSize(const std::string& path) const;
  int GetFileHash(const std::string& path) const;
};
/** @brief A container for a file listing. */
class FilenameDB {
 public:
  FilenameDB() : mutex_("FilenameDB"), expire_seconds_(-1) {}
  virtual ~FilenameDB() { FlushDirCache(); }

  void AddFile(
      const std::string& path, int size, int hash, void* priv = nullptr);
  void DelFile(const std::string& path);
  void* GetFilePriv(const std::string& path);

  /* This handles at most two * wildcards.  If we need anything more
   * complicated, we'll need to use fnmatch or regex. */
  void GetFilesSimpleMatch(
      const std::string& dir, const std::string& file,
      std::vector<std::string>& out, bool only_dirs);

  /* Search for "path" case-insensitively and replace it with the correct
   * case.  If only a portion of the path exists, resolve as much as possible.
   * Return true if the entire path was matched. */
  bool ResolvePath(std::string& path);

  RageFileManager::FileType GetFileType(const std::string& path);
  int GetFileSize(const std::string& path);
  int GetFileHash(const std::string& file_path);
  void GetDirListing(
      const std::string& path, std::vector<std::string>& add_to, bool only_dirs,
      bool return_path_too);

  void FlushDirCache(const std::string& dir = std::string());

  void GetFileSetCopy(const std::string& dir, FileSet& out);
  /* Probably slow, so override it. */
  virtual void CacheFile(const std::string& path);

 protected:
  RageEvent mutex_;

  const File* GetFile(const std::string& path);
  FileSet* GetFileSet(const std::string& dir, bool create = true);

  /* Directories we have cached: */
  std::map<std::string, FileSet*> dirs_;

  int expire_seconds_;

  void GetFilesEqualTo(
      const std::string& dir, const std::string& name,
      std::vector<std::string>& out, bool only_dirs);
  void GetFilesMatching(
      const std::string& dir, const std::string& beginning,
      const std::string& containing, const std::string& ending,
      std::vector<std::string>& out, bool only_dirs);
  void DelFileSet(std::map<std::string, FileSet*>::iterator dir);

  /* The given path wasn't cached.  Cache it. */
  virtual void PopulateFileSet(
      FileSet& /* fs */, const std::string& /* sPath */) {}
};

/* This FilenameDB must be populated in advance. */
class NullFilenameDB : public FilenameDB {
 public:
  NullFilenameDB() { expire_seconds_ = -1; }
  void CacheFile(const std::string& /* sPath */) {}
};

#endif

/**
 * @file
 * @author Glenn Maynard (c) 2003-2004
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
