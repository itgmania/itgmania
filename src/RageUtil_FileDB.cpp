#include "RageUtil_FileDB.h"

#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "RageFileManager.h"
#include "RageLog.h"
#include "RageThreads.h"
#include "RageUtil.h"
#include "StdString.h"
#include "global.h"

/* Search for "beginning*containing*ending". */
void FileSet::GetFilesMatching(
    const std::string& raw_beginning, const std::string& raw_containing,
    const std::string& raw_ending, std::vector<std::string>& out,
    bool only_dirs) const {
  /* "files" is a case-insensitive mapping, by filename.  Use lower_bound to
   * figure out where to start. */
  std::string beginning = raw_beginning;
  MakeLower(beginning);
  std::string containing = raw_containing;
  MakeLower(containing);
  std::string ending = raw_ending;
  MakeLower(ending);

  std::set<File>::const_iterator i = files_.lower_bound(File(beginning));
  for (; i != files_.end(); ++i) {
    const File& f = *i;

    if (only_dirs && !f.dir_) {
      continue;
    }

    const std::string& path = f.lname_;

    /* Check sBeginning. Once we hit a filename that no longer matches
     * sBeginning, we're past all possible matches in the sort, so stop. */
    if (beginning.size() > path.size()) {
      break; /* can't start with it */
    }
    if (path.compare(0, beginning.size(), beginning)) {
      break; /* doesn't start with it */
    }

    /* Position the end starts on: */
    int end_pos = int(path.size()) - int(ending.size());

    /* Check end. */
    if (end_pos < 0) {
      continue; /* can't end with it */
    }
    if (path.compare(end_pos, std::string::npos, ending)) {
      continue; /* doesn't end with it */
    }

    /* Check sContaining.  Do this last, since it's the slowest (substring
     * search instead of string match). */
    if (!containing.empty()) {
      size_t pos = path.find(containing, beginning.size());
      if (pos == path.npos) {
        continue; /* doesn't contain it */
      }
      if (pos + containing.size() > unsigned(end_pos)) {
        continue; /* found it but it overlaps with the end */
      }
    }

    out.push_back(f.name_);
  }
}

void FileSet::GetFilesEqualTo(
    const std::string& str, std::vector<std::string>& out,
    bool only_dirs) const {
  std::set<File>::const_iterator i = files_.find(File(str));
  if (i == files_.end()) {
    return;
  }

  if (only_dirs && !i->dir_) {
    return;
  }

  out.push_back(i->name_);
}

RageFileManager::FileType FileSet::GetFileType(const std::string& path) const {
  std::set<File>::const_iterator i = files_.find(File(path));
  if (i == files_.end()) {
    return RageFileManager::TYPE_NONE;
  }

  return i->dir_ ? RageFileManager::TYPE_DIR : RageFileManager::TYPE_FILE;
}

int FileSet::GetFileSize(const std::string& path) const {
  std::set<File>::const_iterator i = files_.find(File(path));
  if (i == files_.end()) {
    return -1;
  }
  return i->size_;
}

int FileSet::GetFileHash(const std::string& path) const {
  std::set<File>::const_iterator i = files_.find(File(path));
  if (i == files_.end()) {
    return -1;
  }
  // signed int overflow is undefined behavior.
  return int(unsigned(i->hash_) + unsigned(i->size_));
}

/*
 * Given "foo/bar/baz/" or "foo/bar/baz", return "foo/bar/" and "baz".
 * "foo" -> "", "foo"
 */
static void SplitPath(std::string path, std::string& dir, std::string& name) {
  CollapsePath(path);
  if (Right(path, 1) == "/") {
    path.erase(path.size() - 1);
  }

  size_t sep = path.find_last_of('/');
  if (sep == std::string::npos) {
    dir = "";
    name = path;
  } else {
    dir = path.substr(0, sep + 1);
    name = path.substr(sep + 1);
  }
}

RageFileManager::FileType FilenameDB::GetFileType(const std::string& path) {
  ASSERT(!mutex_.IsLockedByThisThread());

  std::string dir, name;
  SplitPath(path, dir, name);

  if (name == "/") {
    return RageFileManager::TYPE_DIR;
  }

  const FileSet* fs = GetFileSet(dir);
  RageFileManager::FileType ret = fs->GetFileType(name);
  mutex_.Unlock(); /* locked by GetFileSet */
  return ret;
}

int FilenameDB::GetFileSize(const std::string& path) {
  ASSERT(!mutex_.IsLockedByThisThread());

  std::string dir, name;
  SplitPath(path, dir, name);

  const FileSet* fs = GetFileSet(dir);
  int ret = fs->GetFileSize(name);
  mutex_.Unlock(); /* locked by GetFileSet */
  return ret;
}

int FilenameDB::GetFileHash(const std::string& path) {
  ASSERT(!mutex_.IsLockedByThisThread());

  std::string dir, name;
  SplitPath(path, dir, name);

  const FileSet* fs = GetFileSet(dir);
  int ret = fs->GetFileHash(name);
  mutex_.Unlock(); /* locked by GetFileSet */
  return ret;
}

/* path should be fully collapsed, so we can operate in-place: no . or .. */
bool FilenameDB::ResolvePath(std::string& path) {
  if (path == "/" || path == "") {
    return true;
  }

  /* Split path into components. */
  int begin = 0, size = -1;

  /* Resolve each component. */
  std::string ret = "";
  const FileSet* fs = nullptr;

  static const std::string kSlash("/");
  for (;;) {
    split(path, kSlash, begin, size, true);
    if (begin == (int)path.size()) {
      break;
    }

    if (fs == nullptr) {
      fs = GetFileSet(ret);
    } else {
      mutex_.Lock(); /* for access to fs */
    }

    std::string p = path.substr(begin, size);
    ASSERT_M(p.size() != 1 || p[0] != '.', path);                 // no .
    ASSERT_M(p.size() != 2 || p[0] != '.' || p[1] != '.', path);  // no ..
    std::set<File>::const_iterator it = fs->files_.find(File(p));

    /* If there were no matches, the path isn't found. */
    if (it == fs->files_.end()) {
      mutex_.Unlock(); /* locked by GetFileSet */
      return false;
    }

    ret += "/" + it->name_;

    fs = it->dirp_;

    mutex_.Unlock(); /* locked by GetFileSet */
  }

  if (path.size() && path[path.size() - 1] == '/') {
    path = ret + "/";
  } else {
    path = ret;
  }
  return true;
}

void FilenameDB::GetFilesMatching(
    const std::string& dir, const std::string& beginning,
    const std::string& containing, const std::string& ending,
    std::vector<std::string>& out, bool only_dirs) {
  ASSERT(!mutex_.IsLockedByThisThread());

  const FileSet* fs = GetFileSet(dir);
  fs->GetFilesMatching(beginning, containing, ending, out, only_dirs);
  mutex_.Unlock(); /* locked by GetFileSet */
}

void FilenameDB::GetFilesEqualTo(
    const std::string& dir, const std::string& file,
    std::vector<std::string>& out, bool only_dirs) {
  ASSERT(!mutex_.IsLockedByThisThread());

  const FileSet* fs = GetFileSet(dir);
  fs->GetFilesEqualTo(file, out, only_dirs);
  mutex_.Unlock(); /* locked by GetFileSet */
}

void FilenameDB::GetFilesSimpleMatch(
    const std::string& dir, const std::string& mask,
    std::vector<std::string>& out, bool only_dirs) {
  /* Does this contain a wildcard? */
  size_t first_pos = mask.find_first_of('*');
  if (first_pos == mask.npos) {
    /* No; just do a regular search. */
    GetFilesEqualTo(dir, mask, out, only_dirs);
    return;
  }
  size_t second_pos = mask.find_first_of('*', first_pos + 1);
  if (second_pos == mask.npos) {
    /* Only one *: "A*B". */
    /* XXX: "_blank.png*.png" shouldn't match the file "_blank.png". */
    GetFilesMatching(
        dir, mask.substr(0, first_pos), std::string(),
        mask.substr(first_pos + 1), out, only_dirs);
    return;
  }

  /* Two *s: "A*B*C". */
  GetFilesMatching(
      dir, mask.substr(0, first_pos),
      mask.substr(first_pos + 1, second_pos - first_pos - 1),
      mask.substr(second_pos + 1), out, only_dirs);
}

/*
 * Get the FileSet for dir; if create is true, create the FileSet if necessary.
 *
 * We want to unlock the object while we populate FileSets, so m_Mutex should
 * not be locked when this is called.  It will be locked on return; the caller
 * must unlock it.
 */
FileSet* FilenameDB::GetFileSet(const std::string& raw_dir, bool create) {
  std::string dir = raw_dir;

  /* Creating can take a long time; don't hold the lock if we might do that. */
  if (create && mutex_.IsLockedByThisThread() && LOG) {
    LOG->Warn("FilenameDB::GetFileSet: mutex_ was locked");
  }

  /* Normalize the path. */
  Replace(dir, "\\", "/"); /* foo\bar -> foo/bar */
  Replace(dir, "//", "/"); /* foo//bar -> foo/bar */

  if (dir == "") {
    dir = "/";
  }

  std::string lower = dir;
  MakeLower(lower);

  mutex_.Lock();

  for (;;) {
    /* Look for the directory. */
    std::map<std::string, FileSet*>::iterator i = dirs_.find(lower);
    if (!create) {
      if (i == dirs_.end()) {
        return nullptr;
      }
      return i->second;
    }

    /* We're allowed to create.  If the directory wasn't found, break out and
     * create it. */
    if (i == dirs_.end()) {
      break;
    }

    /* This directory already exists.  If it's still being filled in by another
     * thread, wait for it. */
    FileSet* file_set = i->second;
    if (!file_set->filled_) {
      mutex_.Wait();

      /* Beware: when we unlock m_Mutex to wait for it to finish filling,
       * we give up our claim to dirs, so i may be invalid.  Start over
       * and re-search. */
      continue;
    }

    if (expire_seconds_ == -1 || file_set->age_.Ago() < expire_seconds_) {
      /* Found it, and it hasn't expired. */
      return file_set;
    }

    /* It's expired.  Delete the old entry. */
    this->DelFileSet(i);
    break;
  }

  /* Create the FileSet and insert it.  Set it to !m_bFilled, so if other
   * threads happen to try to use this directory before we finish filling it,
   * they'll wait. */
  FileSet* ret = new FileSet;
  ret->filled_ = false;
  dirs_[lower] = ret;

  /* Unlock while we populate the directory.  This way, reads to other
   * directories won't block if this takes a while. */
  mutex_.Unlock();
  ASSERT(!mutex_.IsLockedByThisThread());
  PopulateFileSet(*ret, dir);

  /* If this isn't the root directory, we want to set the dirp pointer of our
   * parent to the newly-created directory.  Find the pointer we need to set. Be
   * careful of order of operations, here: since we just unlocked, any
   * this->dirs searches we did previously are no longer valid. */
  FileSet** parent_dirp = nullptr;
  if (dir != "/") {
    std::string parent_dir = Dirname(dir);
    if (parent_dir == "./") {
      parent_dir = "";
    }

    /* This also re-locks m_Mutex for us. */
    FileSet* parent = GetFileSet(parent_dir);
    if (parent != nullptr) {
      std::set<File>::iterator it = parent->files_.find(File(Basename(dir)));
      if (it != parent->files_.end()) {
        parent_dirp = const_cast<FileSet**>(&it->dirp_);
      }
    }
  } else {
    mutex_.Lock();
  }

  if (parent_dirp != nullptr) {
    *parent_dirp = ret;
  }

  ret->age_.Touch();
  ret->filled_ = true;

  /* Signal the event, to wake up any other threads that might be waiting for
   * this directory.  Leave the mutex locked; those threads will wake up when
   * the current operation completes. */
  mutex_.Broadcast();

  return ret;
}

/* Add the file or directory "sPath".  sPath is a directory if it ends with
 * a slash. */
void FilenameDB::AddFile(
    const std::string& raw_path, int size, int hash, void* priv) {
  std::string path(raw_path);

  if (path == "" || path == "/") {
    return;
  }

  if (path[0] != '/') {
    path = "/" + path;
  }

  std::vector<std::string> parts;
  split(path, "/", parts, false);

  std::vector<std::string>::const_iterator begin = parts.begin();
  std::vector<std::string>::const_iterator end = parts.end();

  bool is_dir = true;
  if (path[path.size() - 1] != '/') {
    is_dir = false;
  } else {
    --end;
  }

  /* Skip the leading slash. */
  ++begin;

  do {
    /* Combine all but the last part. */
    std::string dir = "/" + join("/", begin, end - 1);
    if (dir != "/") {
      dir += "/";
    }
    const std::string& fn = *(end - 1);
    FileSet* fs = GetFileSet(dir);
    ASSERT(mutex_.IsLockedByThisThread());

    // const_cast to cast away the constness that is only needed for the name
    File& f = const_cast<File&>(*fs->files_.insert(fn).first);
    f.dir_ = is_dir;
    if (!is_dir) {
      f.size_ = size;
      f.hash_ = hash;
      f.priv_ = priv;
    }
    mutex_.Unlock(); /* locked by GetFileSet */
    is_dir = true;

    --end;
  } while (begin != end);
}

/* Remove the given FileSet, and all dirp pointers to it.  This means the cache
 * has expired, not that the directory is necessarily gone; don't actually
 * delete the file from the parent. */
void FilenameDB::DelFileSet(std::map<std::string, FileSet*>::iterator dir) {
  /* If this isn't locked, dir may not be valid. */
  ASSERT(mutex_.IsLockedByThisThread());

  if (dir == dirs_.end()) {
    return;
  }

  FileSet* fs = dir->second;

  /* Remove any stale dirp pointers. */
  for (std::map<std::string, FileSet*>::iterator it = dirs_.begin();
       it != dirs_.end(); ++it) {
    FileSet* clean = it->second;

    if (!clean->filled_) {
      // This directory is still being filled. Don't access the files set
      // while another thread is modifying it.
      continue;
    }

    for (std::set<File>::iterator f = clean->files_.begin();
         f != clean->files_.end(); ++f) {
      File& ff = (File&)*f;
      if (ff.dirp_ == fs) {
        ff.dirp_ = nullptr;
      }
    }
  }

  delete fs;
  dirs_.erase(dir);
}

void FilenameDB::DelFile(const std::string& path) {
  LockMut(mutex_);
  std::string lower = path;
  MakeLower(lower);

  std::map<std::string, FileSet*>::iterator fsi = dirs_.find(lower);
  DelFileSet(fsi);

  /* Delete sPath from its parent. */
  std::string dir, name;
  SplitPath(path, dir, name);
  FileSet* parent = GetFileSet(dir, false);
  if (parent) {
    parent->files_.erase(name);
  }

  mutex_.Unlock(); /* locked by GetFileSet */
}

void FilenameDB::FlushDirCache(const std::string& /* sDir */) {
  FileSet* file_set = nullptr;
  mutex_.Lock();

  for (;;) {
    if (dirs_.empty()) {
      break;
    }

    /* Grab the first entry.  Take it out of the list while we hold the
     * lock, to guarantee that we own it. */
    file_set = dirs_.begin()->second;

    dirs_.erase(dirs_.begin());

    /* If it's being filled, we don't really own it until it's finished being
     * filled, so wait. */
    while (!file_set->filled_) {
      mutex_.Wait();
    }
    delete file_set;
  }

#if 0
	/* XXX: This is tricky, we want to flush all of the subdirectories of
	 * sDir, but once we unlock the mutex, we basically have to start over.
	 * It's just an optimization though, so it can wait. */
	{
		if( it != dirs.end() )
		{
			pFileSet = it->second;
			dirs.erase( it );
			while( !pFileSet->filled_ )
				mutex_.Wait();
			delete pFileSet;

			if( sDir != "/" )
			{
				std::string sParent = Dirname( sDir );
				if( sParent == "./" )
					sParent = "";
				sParent.MakeLower();
				it = dirs.find( sParent );
				if( it != dirs.end() )
				{
					FileSet *pParent = it->second;
					std::set<File>::iterator fileit = pParent->files.find( File(Basename(sDir)) );
					if( fileit != pParent->files.end() )
						fileit->dirp = nullptr;
				}
			}
		}
		else
		{
			LOG->Warn( "Trying to flush an unknown directory %s.", sDir.c_str() );
		}
#endif
  mutex_.Unlock();
}

const File* FilenameDB::GetFile(const std::string& path) {
  if (mutex_.IsLockedByThisThread() && LOG) {
    LOG->Warn("FilenameDB::GetFile: mutex_ was locked");
  }

  std::string dir, name;
  SplitPath(path, dir, name);
  FileSet* fs = GetFileSet(dir);

  std::set<File>::iterator it;
  it = fs->files_.find(File(name));
  if (it == fs->files_.end()) {
    return nullptr;
  }

  return &*it;
}

void* FilenameDB::GetFilePriv(const std::string& path) {
  ASSERT(!mutex_.IsLockedByThisThread());

  const File* file = GetFile(path);
  void* ret = nullptr;
  if (file != nullptr) {
    ret = file->priv_;
  }

  mutex_.Unlock(); /* locked by GetFileSet */
  return ret;
}

void FilenameDB::GetDirListing(
    const std::string& raw_path, std::vector<std::string>& add_to,
    bool only_dirs, bool return_path_too) {
  std::string path = raw_path;
  //	LOG->Trace( "GetDirListing( %s )", sPath.c_str() );

  ASSERT(!path.empty());

  /* Strip off the last path element and use it as a mask. */
  size_t pos = path.find_last_of('/');
  std::string fn;
  if (pos == path.npos) {
    fn = path;
    path = "";
  } else {
    fn = path.substr(pos + 1);
    path = path.substr(0, pos + 1);
  }

  /* If the last element was empty, use "*". */
  if (fn.size() == 0) {
    fn = "*";
  }

  unsigned start = add_to.size();
  GetFilesSimpleMatch(path, fn, add_to, only_dirs);

  if (return_path_too && start < add_to.size()) {
    while (start < add_to.size()) {
      add_to[start].insert(0, path);
      start++;
    }
  }
}

/* Get a complete copy of a FileSet.  This isn't very efficient, since it's a
 * deep copy, but allows retrieving a copy from elsewhere without having to
 * worry about our locking semantics. */
void FilenameDB::GetFileSetCopy(const std::string& dir, FileSet& out) {
  FileSet* file_set = GetFileSet(dir);
  out = *file_set;
  mutex_.Unlock(); /* locked by GetFileSet */
}

void FilenameDB::CacheFile(const std::string& path) {
  LOG->Warn("Slow cache due to: %s", path.c_str());
  FlushDirCache(Dirname(path));
}

/*
 * Copyright (c) 2003-2004 Glenn Maynard
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
