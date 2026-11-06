/*
 * Ref: http://www.info-zip.org/pub/infozip/doc/appnote-981119-iz.zip
 */

#include "RageFileDriverZip.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <string>

#include "RageFile.h"
#include "RageFileDriver.h"
#include "RageFileDriverDeflate.h"
#include "RageFileDriverSlice.h"
#include "RageUtil.h"
#include "RageUtil_FileDB.h"
#include "global.h"

static struct FileDriverEntry_ZIP : public FileDriverEntry {
  FileDriverEntry_ZIP() : FileDriverEntry("ZIP") {}
  RageFileDriver* Create(const std::string& sRoot) const {
    return new RageFileDriverZip(sRoot);
  }
} const g_RegisterDriver;

RageFileDriverZip::RageFileDriverZip(const std::string& sPath)
    : RageFileDriver(new NullFilenameDB), mutex_("RageFileDriverZip") {
  file_owned_ = false;
  zip_ = nullptr;
  Load(sPath);
}

bool RageFileDriverZip::Load(const std::string& sPath) {
  ASSERT(zip_ == nullptr); /* don't load twice */

  file_owned_ = true;
  path_ = sPath;
  mutex_.SetName(ssprintf("RageFileDriverZip(%s)", sPath.c_str()));

  RageFile* pFile = new RageFile;

  if (!pFile->Open(sPath)) {
    WARN(ssprintf(
        "Couldn't open %s: %s", sPath.c_str(), pFile->GetError().c_str()));
    delete pFile;
    return false;
  }

  zip_ = pFile;

  return ParseZipfile();
}

bool RageFileDriverZip::ReadEndCentralRecord(
    int& iTotalEntries, int& iCentralDirectoryOffset) {
  std::string sError;
  std::string sSig = FileReading::ReadString(*zip_, 4, sError);
  FileReading::read_16_le(*zip_, sError); /* skip number of this disk */
  FileReading::read_16_le(*zip_, sError); /* skip disk with central directory */
  FileReading::read_16_le(
      *zip_, sError); /* skip number of entries on this disk */
  iTotalEntries = FileReading::read_16_le(*zip_, sError);
  FileReading::read_32_le(
      *zip_, sError); /* skip size of the central directory */
  iCentralDirectoryOffset = FileReading::read_32_le(*zip_, sError);
  int iCommentLength = FileReading::read_16_le(*zip_, sError);
  comment_ = FileReading::ReadString(*zip_, iCommentLength, sError);

  if (sError != "") {
    WARN(ssprintf("%s: %s", path_.c_str(), sError.c_str()));
    return false;
  }

  return true;
}

/* Find the end of central directory record, and seek to it. */
bool RageFileDriverZip::SeekToEndCentralRecord() {
  const int iSearchTo = std::max(zip_->GetFileSize() - 1024 * 32, 0);
  int iRealPos = zip_->GetFileSize();

  while (iRealPos > 0 && iRealPos >= iSearchTo) {
    /* Move back in the file; leave some overlap between checks, to handle
     * the case where the signature crosses the block boundary. */
    char buf[1024 * 4];
    iRealPos -= sizeof(buf) - 4;
    iRealPos = std::max(0, iRealPos);
    zip_->Seek(iRealPos);

    int iGot = zip_->Read(buf, sizeof(buf));
    if (iGot == -1) {
      WARN(ssprintf("%s: %s", path_.c_str(), zip_->GetError().c_str()));
      return false;
    }

    for (int iPos = iGot - 4; iPos >= 0; --iPos) {
      if (memcmp(buf + iPos, "\x50\x4B\x05\x06", 4)) {
        continue;
      }

      zip_->Seek(iRealPos + iPos);
      return true;
    }
  }

  return false;
}

bool RageFileDriverZip::ParseZipfile() {
  if (!SeekToEndCentralRecord()) {
    WARN(ssprintf(
        "Couldn't open %s: couldn't find end of central directory record",
        path_.c_str()));
    return false;
  }

  /* Read the end of central directory record. */
  int iTotalEntries, iCentralDirectoryOffset;
  if (!ReadEndCentralRecord(iTotalEntries, iCentralDirectoryOffset)) {
    return false; /* warned already */
  }

  /* Seek to the start of the central file directory. */
  zip_->Seek(iCentralDirectoryOffset);

  /* Loop through files in central directory. */
  for (int i = 0; i < iTotalEntries; ++i) {
    FileInfo info;
    info.data_offset_ = -1;
    int got = ProcessCdirFileHdr(info);
    if (got == -1) { /* error */
      break;
    }
    if (got == 0) { /* skip */
      continue;
    }

    FileInfo* pInfo = new FileInfo(info);
    files_.push_back(pInfo);
    fdb_->AddFile(
        "/" + pInfo->name_, pInfo->uncompressed_size_, pInfo->crc32_, pInfo);
  }

  if (files_.size() == 0) {
    WARN(ssprintf("%s: no files found in central file header", path_.c_str()));
  }

  return true;
}

int RageFileDriverZip::ProcessCdirFileHdr(FileInfo& info) {
  std::string sError;
  std::string sSig = FileReading::ReadString(*zip_, 4, sError);
  if (sSig != "\x50\x4B\x01\x02") {
    WARN(ssprintf(
        "%s: central directory record signature not found", path_.c_str()));
    return -1;
  }

  FileReading::read_8(*zip_, sError);     /* skip version made by */
  FileReading::read_8(*zip_, sError);     /* skip OS made by */
  FileReading::read_16_le(*zip_, sError); /* skip version needed to extract */
  int iGeneralPurpose = FileReading::read_16_le(*zip_, sError);
  info.compression_method_ =
      (ZipCompressionMethod)FileReading::read_16_le(*zip_, sError);
  FileReading::read_16_le(*zip_, sError); /* skip last mod file time */
  FileReading::read_16_le(*zip_, sError); /* skip last mod file date */
  info.crc32_ = FileReading::read_32_le(*zip_, sError);
  info.compressed_size_ = FileReading::read_32_le(*zip_, sError);
  info.uncompressed_size_ = FileReading::read_32_le(*zip_, sError);
  int iFilenameLength = FileReading::read_16_le(*zip_, sError);
  int iExtraFieldLength = FileReading::read_16_le(*zip_, sError);
  int iFileCommentLength = FileReading::read_16_le(*zip_, sError);
  FileReading::read_16_le(*zip_, sError); /* relative offset of local header */
  FileReading::read_16_le(*zip_, sError); /* skip internal file attributes */
  unsigned iExternalFileAttributes = FileReading::read_32_le(*zip_, sError);
  info.offset_ = FileReading::read_32_le(*zip_, sError);

  /* Check for errors before reading variable-length fields. */
  if (sError != "") {
    WARN(ssprintf("%s: %s", path_.c_str(), sError.c_str()));
    return -1;
  }

  info.name_ = FileReading::ReadString(*zip_, iFilenameLength, sError);
  FileReading::SkipBytes(
      *zip_, iExtraFieldLength, sError); /* skip extra field */
  FileReading::SkipBytes(
      *zip_, iFileCommentLength, sError); /* skip file comment */

  if (sError != "") {
    WARN(ssprintf("%s: %s", path_.c_str(), sError.c_str()));
    return -1;
  }

  /* Check usability last, so we always read past the whole entry and don't
   * leave the file pointer in the middle of a record. */
  if (iGeneralPurpose & 1) {
    WARN(ssprintf(
        "Skipped encrypted \"%s\" in \"%s\"", info.name_.c_str(),
        path_.c_str()));
    return 0;
  }

  /* Skip directories. */
  if (iExternalFileAttributes & (1 << 4)) {
    return 0;
  }

  if (info.compression_method_ != STORED &&
      info.compression_method_ != DEFLATED) {
    WARN(ssprintf(
        "File \"%s\" in \"%s\" uses unsupported compression method %i",
        info.name_.c_str(), path_.c_str(), info.compression_method_));

    return 0;
  }

  return 1;
}

bool RageFileDriverZip::ReadLocalFileHeader(FileInfo& info) {
  /* Seek to and read the local file header. */
  zip_->Seek(info.offset_);

  std::string sError;
  std::string sSig = FileReading::ReadString(*zip_, 4, sError);

  if (sError != "") {
    WARN(ssprintf(
        "%s: error opening \"%s\": %s", path_.c_str(), info.name_.c_str(),
        sError.c_str()));
    return false;
  }

  if (sSig != "\x50\x4B\x03\x04") {
    WARN(ssprintf(
        "%s: local file header not found for \"%s\"", path_.c_str(),
        info.name_.c_str()));
    return false;
  }

  FileReading::SkipBytes(
      *zip_, 22, sError); /* skip most of the local file header */

  const int iFilenameLength = FileReading::read_16_le(*zip_, sError);
  const int iExtraFieldLength = FileReading::read_16_le(*zip_, sError);
  info.data_offset_ = zip_->Tell() + iFilenameLength + iExtraFieldLength;

  if (sError != "") {
    WARN(ssprintf("%s: %s", path_.c_str(), sError.c_str()));
    return false;
  }

  return true;
}

RageFileDriverZip::~RageFileDriverZip() {
  for (unsigned i = 0; i < files_.size(); ++i) {
    delete files_[i];
  }

  if (file_owned_) {
    delete zip_;
  }
}

RageFileBasic* RageFileDriverZip::Open(
    const std::string& sPath, int iMode, int& iErr) {
  if (iMode & RageFile::WRITE) {
    iErr = ERROR_WRITING_NOT_SUPPORTED;
    return nullptr;
  }

  FileInfo* info = (FileInfo*)fdb_->GetFilePriv(sPath);
  if (info == nullptr) {
    iErr = ENOENT;
    return nullptr;
  }

  mutex_.Lock();

  /* If we haven't figured out the offset to the real data yet, do so now. */
  if (info->data_offset_ == -1) {
    if (!ReadLocalFileHeader(*info)) {
      mutex_.Unlock();
      return nullptr;
    }
  }

  /* We won't do any further access to zip, except to copy it (which is
   * threadsafe), so we can unlock now. */
  mutex_.Unlock();

  RageFileDriverSlice* pSlice = new RageFileDriverSlice(
      zip_->Copy(), info->data_offset_, info->compressed_size_);
  pSlice->DeleteFileWhenFinished();

  switch (info->compression_method_) {
    case STORED:
      return pSlice;
    case DEFLATED: {
      RageFileObjInflate* pInflate =
          new RageFileObjInflate(pSlice, info->uncompressed_size_);
      pInflate->DeleteFileWhenFinished();
      return pInflate;
    }
    default:
      /* unknown compression method */
      iErr = ENOSYS;
      return nullptr;
  }
}

/* NOP for now.  This could check to see if the ZIP's mtime has changed, and
 * reload. */
void RageFileDriverZip::FlushDirCache(const std::string& sPath) {}

/*
 * Copyright (c) 2003-2005 Glenn Maynard
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
