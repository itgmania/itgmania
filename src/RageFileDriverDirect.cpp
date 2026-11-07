#include "RageFileDriverDirect.h"

#include <fcntl.h>

#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>

#include "RageException.h"
#include "RageFile.h"
#include "RageFileDriver.h"
#include "RageFileDriverDirectHelpers.h"
#include "RageFileManager.h"
#include "RageUtil.h"
#include "RageUtil_FileDB.h"
#include "global.h"

#if defined(_WIN32)
// clang-format off
#include <windows.h>
#include <io.h>
// clang-format on

#include "archutils/Win32/ErrorStrings.h"
#else
#endif

/* Direct filesystem access: */
static struct FileDriverEntry_DIR : public FileDriverEntry {
  FileDriverEntry_DIR() : FileDriverEntry("DIR") {}
  RageFileDriver* Create(const std::string& root) const {
    return new RageFileDriverDirect(root);
  }
} const g_RegisterDriver;

/* Direct read-only filesystem access: */
static struct FileDriverEntry_DIRRO : public FileDriverEntry {
  FileDriverEntry_DIRRO() : FileDriverEntry("DIRRO") {}
  RageFileDriver* Create(const std::string& root) const {
    return new RageFileDriverDirectReadOnly(root);
  }
} const g_RegisterDriver2;

RageFileDriverDirect::RageFileDriverDirect(const std::string& root)
    : RageFileDriver(new DirectFilenameDB(root)) {
  Remount(root);
}

static std::string MakeTempFilename(const std::string& path) {
  /* "Foo/bar/baz" -> "Foo/bar/new.baz.new".  Both prepend and append: we don't
   * want a wildcard search for the filename to match (foo.txt.new matches
   * foo.txt*), and we don't want to have the same extension (so "new.foo.sm"
   * doesn't show up in *.sm). */
  return Dirname(path) + "new." + Basename(path) + ".new";
}

static RageFileObjDirect* MakeFileObjDirect(
    std::string path, int mode, int& error) {
  int fd;
  if (mode & RageFile::READ) {
    fd = DoOpen(path.c_str(), O_BINARY | O_RDONLY, 0666);

    /* XXX: Windows returns EACCES if we try to open a file on a CDROM that
     * isn't ready, instead of something like ENODEV.  We want to return that
     * case as ENOENT, but we can't distinguish it from file permission errors.
     */
  } else {
    std::string out;
    if (mode & RageFile::STREAMED) {
      out = path;
    } else {
      out = MakeTempFilename(path);
    }

    /* Open a temporary file for writing. */
    fd = DoOpen(out.c_str(), O_BINARY | O_WRONLY | O_CREAT | O_TRUNC, 0666);
  }

  if (fd == -1) {
    error = errno;
    return nullptr;
  }

#if defined(UNIX)
  struct stat st;
  if (fstat(fd, &st) != -1 && (st.st_mode & S_IFDIR)) {
    error = EISDIR;
    close(fd);
    return nullptr;
  }
#endif

  return new RageFileObjDirect(path, fd, mode);
}

RageFileBasic* RageFileDriverDirect::Open(
    const std::string& raw_path, int mode, int& error) {
  if (root_ == "(empty)") {
    error = (mode & RageFile::WRITE) ? EROFS : ENOENT;
    return nullptr;
  }

  std::string path = raw_path;
  ASSERT(path.size() && path[0] == '/');

  /* This partially resolves.  For example, if "abc/def" exists, and we're
   * opening "ABC/DEF/GHI/jkl/mno", this will resolve it to
   * "abc/def/GHI/jkl/mno"; we'll create the missing parts below. */
  fdb_->ResolvePath(path);

  if (mode & RageFile::WRITE) {
    const std::string dir = Dirname(path);
    if (this->GetFileType(dir) != RageFileManager::TYPE_DIR) {
      if (CreateDirectories(root_ + dir)) {
        fdb_->AddFile(dir, 0, 0);
      }
    }
  }

  return MakeFileObjDirect(root_ + path, mode, error);
}

bool RageFileDriverDirect::Move(
    const std::string& raw_old_path, const std::string& raw_new_path) {
  if (root_ == "(empty)") {
    return false;
  }

  std::string old_path = raw_old_path;
  std::string new_path = raw_new_path;
  fdb_->ResolvePath(old_path);
  fdb_->ResolvePath(new_path);

  if (this->GetFileType(old_path) == RageFileManager::TYPE_NONE) {
    return false;
  }

  {
    const std::string dir = Dirname(new_path);
    CreateDirectories(root_ + dir);
  }
  int size = fdb_->GetFileSize(old_path);
  int hash = fdb_->GetFileHash(old_path);
  TRACE(ssprintf(
      "rename \"%s\" -> \"%s\"", (root_ + old_path).c_str(),
      (root_ + new_path).c_str()));
  if (DoRename((root_ + old_path).c_str(), (root_ + new_path).c_str()) == -1) {
    WARN(ssprintf(
        "rename(%s,%s) failed: %s", (root_ + old_path).c_str(),
        (root_ + new_path).c_str(), strerror(errno)));
    return false;
  }

  fdb_->DelFile(old_path);
  fdb_->AddFile(new_path, size, hash, nullptr);
  return true;
}

bool RageFileDriverDirect::Remove(const std::string& raw_path) {
  if (root_ == "(empty)") {
    return false;
  }

  std::string path = raw_path;
  fdb_->ResolvePath(path);

  RageFileManager::FileType type = this->GetFileType(path);
  switch (type) {
    case RageFileManager::TYPE_FILE:
      if (DoRemove((root_ + path).c_str()) == -1) {
        WARN("remove failed: " + path);
        return false;
      }
      fdb_->DelFile(path);
      return true;

    case RageFileManager::TYPE_DIR:
      if (DoRmdir((root_ + path).c_str()) == -1) {
        WARN("rmdir failed: " + path);
        return false;
      }
      fdb_->DelFile(path);
      return true;

    case RageFileManager::TYPE_NONE:
      return false;

    default:
      FAIL_M(ssprintf("Invalid FileType: %i", type));
      return false;
  }
}

RageFileObjDirect* RageFileObjDirect::Copy() const {
  int err;
  RageFileObjDirect* ret = MakeFileObjDirect(path_, mode_, err);

  if (ret == nullptr) {
    RageException::Throw(
        "Couldn't reopen \"%s\": %s", path_.c_str(), strerror(err));
  }

  ret->Seek((int)DoLseek(fd_, 0, SEEK_CUR));

  return ret;
}

bool RageFileDriverDirect::Remount(const std::string& path) {
  root_ = path;
  ((DirectFilenameDB*)fdb_)->SetRoot(path);

  /* If the root path doesn't exist, create it. */
  if (root_ != "(empty)") {
    CreateDirectories(root_);
  }

  return true;
}

/* The DIRRO driver is just like DIR, except writes are disallowed. */
RageFileDriverDirectReadOnly::RageFileDriverDirectReadOnly(
    const std::string& root)
    : RageFileDriverDirect(root) {}
RageFileBasic* RageFileDriverDirectReadOnly::Open(
    const std::string& path, int mode, int& error) {
  if (mode & RageFile::WRITE) {
    error = ERROR_WRITING_NOT_SUPPORTED;
    return nullptr;
  }

  return RageFileDriverDirect::Open(path, mode, error);
}
bool RageFileDriverDirectReadOnly::Move(
    const std::string& /* sOldPath */, const std::string& /* sNewPath */) {
  return false;
}
bool RageFileDriverDirectReadOnly::Remove(const std::string& /* sPath */) {
  return false;
}

static const unsigned int BUFSIZE = 1024 * 64;
RageFileObjDirect::RageFileObjDirect(
    const std::string& path, int fd, int mode) {
  path_ = path;
  fd_ = fd;
  write_failed_ = false;
  mode_ = mode;
  ASSERT(fd_ != -1);

  if (mode_ & RageFile::WRITE) {
    this->EnableWriteBuffering(BUFSIZE);
  }
}

namespace {
#if !defined(_WIN32)
bool FlushDir(std::string path, std::string& error) {
  /* Wait for the directory to be flushed. */
  int dirfd = open(path.c_str(), O_RDONLY);
  if (dirfd == -1) {
    error = strerror(errno);
    return false;
  }

  if (fsync(dirfd) == -1) {
    error = strerror(errno);
    close(dirfd);
    return false;
  }

  close(dirfd);
  return true;
}
#else
bool FlushDir(std::string /* sPath */, std::string& /* sError */) {
  return true;
}
#endif
}  // namespace

bool RageFileObjDirect::FinalFlush() {
  if (!(mode_ & RageFile::WRITE)) {
    return true;
  }

  /* Flush the output buffer. */
  if (Flush() == -1) {
    return false;
  }

  /* Only do the rest of the flushes if SLOW_FLUSH is enabled. */
  if (!(mode_ & RageFile::SLOW_FLUSH)) {
    return true;
  }

  /* Force a kernel buffer flush. */
  if (fsync(fd_) == -1) {
    WARN(ssprintf(
        "Error synchronizing %s: %s", this->path_.c_str(), strerror(errno)));
    SetError(strerror(errno));
    return false;
  }

  std::string error;
  if (!FlushDir(Dirname(path_), error)) {
    WARN(ssprintf(
        "Error synchronizing fsync(%s dir): %s", this->path_.c_str(),
        error.c_str()));
    SetError(error);
    return false;
  }

  return true;
}

RageFileObjDirect::~RageFileObjDirect() {
  bool failed = !FinalFlush();

  if (fd_ != -1) {
    if (DoClose(fd_) == -1) {
      WARN(ssprintf(
          "Error closing %s: %s", this->path_.c_str(), strerror(errno)));
      SetError(strerror(errno));
      failed = true;
    }
  }

  if (!(mode_ & RageFile::WRITE) || (mode_ & RageFile::STREAMED)) {
    return;
  }

  /* We now have path written to MakeTempFilename(m_sPath).
   * Rename the temporary file over the real path. */

  do {
    if (failed || WriteFailed()) {
      break;
    }

    /* We now have path written to MakeTempFilename(m_sPath). Rename the
     * temporary file over the real path. This should be an atomic operation
     * with a journalling filesystem. That is, there should be no
     * intermediate state a JFS might restore the file we're writing (in the
     * case of a crash/powerdown) to an empty or partial file. */

    std::string old_path = MakeTempFilename(path_);
    std::string new_path = path_;

#if defined(_WIN32)
    if (WinMoveFile(DoPathReplace(old_path), DoPathReplace(new_path))) {
      return;
    }

    /* We failed. */
    int err = GetLastError();
    const std::string error = werr_ssprintf(
        err, "Error renaming \"%s\" to \"%s\"", old_path.c_str(),
        new_path.c_str());
    WARN(ssprintf("%s", error.c_str()));
    SetError(error);
    break;
#else
    if (rename(old_path.c_str(), new_path.c_str()) == -1) {
      WARN(ssprintf(
          "Error renaming \"%s\" to \"%s\": %s", old_path.c_str(),
          new_path.c_str(), strerror(errno)));
      SetError(strerror(errno));
      break;
    }

    if (mode_ & RageFile::SLOW_FLUSH) {
      std::string error;
      if (!FlushDir(Dirname(path_), error)) {
        WARN(ssprintf(
            "Error synchronizing fsync(%s dir): %s", this->path_.c_str(),
            error.c_str()));
        SetError(error);
      }
    }

    // Success.
    return;
#endif
  } while (0);

  // The write or the rename failed. Delete the incomplete temporary file.
  DoRemove(MakeTempFilename(path_).c_str());
}

int RageFileObjDirect::ReadInternal(void* buf, size_t bytes) {
  int ret = DoRead(fd_, buf, bytes);
  if (ret == -1) {
    SetError(strerror(errno));
    return -1;
  }

  return ret;
}

// write(), but retry a couple times on EINTR.
static int RetriedWrite(int fd, const void* buf, size_t count) {
  int tries = 3, ret;
  do {
    ret = DoWrite(fd, buf, count);
  } while (ret == -1 && errno == EINTR && tries--);

  return ret;
}

int RageFileObjDirect::FlushInternal() {
  if (WriteFailed()) {
    SetError("previous write failed");
    return -1;
  }

  return 0;
}

int RageFileObjDirect::WriteInternal(const void* buf, size_t bytes) {
  if (WriteFailed()) {
    SetError("previous write failed");
    return -1;
  }

  /* The buffer is cleared. If we still don't have space, it's bigger than
   * the buffer size, so just write it directly. */
  int ret = RetriedWrite(fd_, buf, bytes);
  if (ret == -1) {
    SetError(strerror(errno));
    write_failed_ = true;
    return -1;
  }
  return bytes;
}

int RageFileObjDirect::SeekInternal(int offset) {
  return (int)DoLseek(fd_, offset, SEEK_SET);
}

int RageFileObjDirect::GetFileSize() const {
  const int old_pos = (int)DoLseek(fd_, 0, SEEK_CUR);
  ASSERT_M(
      old_pos != -1, ssprintf("\"%s\": %s", path_.c_str(), strerror(errno)));
  const int ret = (int)DoLseek(fd_, 0, SEEK_END);
  ASSERT_M(ret != -1, ssprintf("\"%s\": %s", path_.c_str(), strerror(errno)));
  DoLseek(fd_, old_pos, SEEK_SET);
  return ret;
}

int RageFileObjDirect::GetFD() { return fd_; }

/*
 * Copyright (c) 2003-2004 Glenn Maynard, Chris Danford
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
