#include "RageFileDriverMemory.h"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

#include "RageFile.h"
#include "RageFileBasic.h"
#include "RageFileDriver.h"
#include "RageThreads.h"
#include "RageUtil.h"
#include "RageUtil_FileDB.h"
#include "global.h"

struct RageFileObjMemFile {
  RageFileObjMemFile() : refs_(0), mutex_("RageFileObjMemFile") {}
  std::string buf_;
  int refs_;
  RageMutex mutex_;

  static void AddReference(RageFileObjMemFile* pFile) {
    pFile->mutex_.Lock();
    ++pFile->refs_;
    pFile->mutex_.Unlock();
  }

  static void ReleaseReference(RageFileObjMemFile* pFile) {
    pFile->mutex_.Lock();
    const int iRefs = --pFile->refs_;
    const bool bShouldDelete = (pFile->refs_ == 0);
    pFile->mutex_.Unlock();
    ASSERT(iRefs >= 0);

    if (bShouldDelete) {
      delete pFile;
    }
  }
};

RageFileObjMem::RageFileObjMem(RageFileObjMemFile* pFile) {
  if (pFile == nullptr) {
    pFile = new RageFileObjMemFile;
  }

  file_ = pFile;
  file_pos_ = 0;
  RageFileObjMemFile::AddReference(file_);
}

RageFileObjMem::~RageFileObjMem() {
  RageFileObjMemFile::ReleaseReference(file_);
}

int RageFileObjMem::ReadInternal(void* buffer, size_t bytes) {
  LockMut(file_->mutex_);

  file_pos_ = std::min(file_pos_, GetFileSize());
  bytes = std::min(bytes, (size_t)GetFileSize() - file_pos_);
  if (bytes == 0) {
    return 0;
  }
  memcpy(buffer, &file_->buf_[file_pos_], bytes);
  file_pos_ += bytes;

  return bytes;
}

int RageFileObjMem::WriteInternal(const void* buffer, size_t bytes) {
  file_->mutex_.Lock();
  file_->buf_.replace(file_pos_, bytes, (const char*)buffer, bytes);
  file_->mutex_.Unlock();

  file_pos_ += bytes;
  return bytes;
}

int RageFileObjMem::SeekInternal(int offset) {
  file_pos_ = std::clamp(offset, 0, GetFileSize());
  return file_pos_;
}

int RageFileObjMem::GetFileSize() const {
  LockMut(file_->mutex_);
  return file_->buf_.size();
}

RageFileObjMem::RageFileObjMem(const RageFileObjMem& cpy) : RageFileObj(cpy) {
  file_ = cpy.file_;
  file_pos_ = cpy.file_pos_;
  RageFileObjMemFile::AddReference(file_);
}

RageFileObjMem* RageFileObjMem::Copy() const {
  RageFileObjMem* pRet = new RageFileObjMem(*this);
  return pRet;
}

const std::string& RageFileObjMem::GetString() const { return file_->buf_; }

void RageFileObjMem::PutString(const std::string& sBuf) {
  file_->mutex_.Lock();
  file_->buf_ = sBuf;
  file_->mutex_.Unlock();
}

RageFileDriverMem::RageFileDriverMem()
    : RageFileDriver(new NullFilenameDB), mutex_("RageFileDriverMem") {}

RageFileDriverMem::~RageFileDriverMem() {
  for (unsigned i = 0; i < files_.size(); ++i) {
    RageFileObjMemFile* pFile = files_[i];
    RageFileObjMemFile::ReleaseReference(pFile);
  }
}

RageFileBasic* RageFileDriverMem::Open(
    const std::string& sPath, int mode, int& err) {
  LockMut(mutex_);

  if (mode == RageFile::WRITE) {
    /* If the file exists, delete it. */
    Remove(sPath);

    RageFileObjMemFile* pFile = new RageFileObjMemFile;

    /* Add one reference, representing the file in the filesystem. */
    RageFileObjMemFile::AddReference(pFile);

    files_.push_back(pFile);
    fdb_->AddFile(sPath, 0, 0, pFile);

    return new RageFileObjMem(pFile);
  }

  RageFileObjMemFile* pFile = (RageFileObjMemFile*)fdb_->GetFilePriv(sPath);
  if (pFile == nullptr) {
    err = ENOENT;
    return nullptr;
  }

  return new RageFileObjMem(pFile);
}

bool RageFileDriverMem::Remove(const std::string& sPath) {
  LockMut(mutex_);

  RageFileObjMemFile* pFile = (RageFileObjMemFile*)fdb_->GetFilePriv(sPath);
  if (pFile == nullptr) {
    return false;
  }

  /* Unregister the file. */
  fdb_->DelFile(sPath);
  std::vector<RageFileObjMemFile*>::iterator it =
      find(files_.begin(), files_.end(), pFile);
  ASSERT(it != files_.end());
  files_.erase(it);

  RageFileObjMemFile::ReleaseReference(pFile);

  return true;
}

static struct FileDriverEntry_MEM : public FileDriverEntry {
  FileDriverEntry_MEM() : FileDriverEntry("MEM") {}
  RageFileDriver* Create(const std::string& sRoot) const {
    return new RageFileDriverMem();
  }
} const g_RegisterDriver;

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
