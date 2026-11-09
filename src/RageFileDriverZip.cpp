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
  RageFileDriver* Create(const std::string& root) const {
    return new RageFileDriverZip(root);
  }
} const kRegisterDriver;

RageFileDriverZip::RageFileDriverZip(const std::string& path)
    : RageFileDriver(new NullFilenameDB), mutex_("RageFileDriverZip") {
  file_owned_ = false;
  zip_ = nullptr;
  Load(path);
}

bool RageFileDriverZip::Load(const std::string& path) {
  ASSERT(zip_ == nullptr); /* don't load twice */

  file_owned_ = true;
  path_ = path;
  mutex_.SetName(ssprintf("RageFileDriverZip(%s)", path.c_str()));

  RageFile* file = new RageFile;

  if (!file->Open(path)) {
    WARN(ssprintf(
        "Couldn't open %s: %s", path.c_str(), file->GetError().c_str()));
    delete file;
    return false;
  }

  zip_ = file;

  return ParseZipfile();
}

bool RageFileDriverZip::ReadEndCentralRecord(
    int& total_entries, int& central_directory_offset) {
  std::string error;
  std::string sig = FileReading::ReadString(*zip_, 4, error);
  FileReading::read_16_le(*zip_, error); /* skip number of this disk */
  FileReading::read_16_le(*zip_, error); /* skip disk with central directory */
  FileReading::read_16_le(
      *zip_, error); /* skip number of entries on this disk */
  total_entries = FileReading::read_16_le(*zip_, error);
  FileReading::read_32_le(
      *zip_, error); /* skip size of the central directory */
  central_directory_offset = FileReading::read_32_le(*zip_, error);
  int comment_length = FileReading::read_16_le(*zip_, error);
  comment_ = FileReading::ReadString(*zip_, comment_length, error);

  if (error != "") {
    WARN(ssprintf("%s: %s", path_.c_str(), error.c_str()));
    return false;
  }

  return true;
}

/* Find the end of central directory record, and seek to it. */
bool RageFileDriverZip::SeekToEndCentralRecord() {
  const int search_to = std::max(zip_->GetFileSize() - 1024 * 32, 0);
  int real_pos = zip_->GetFileSize();

  while (real_pos > 0 && real_pos >= search_to) {
    /* Move back in the file; leave some overlap between checks, to handle
     * the case where the signature crosses the block boundary. */
    char buf[1024 * 4];
    real_pos -= sizeof(buf) - 4;
    real_pos = std::max(0, real_pos);
    zip_->Seek(real_pos);

    int got = zip_->Read(buf, sizeof(buf));
    if (got == -1) {
      WARN(ssprintf("%s: %s", path_.c_str(), zip_->GetError().c_str()));
      return false;
    }

    for (int pos = got - 4; pos >= 0; --pos) {
      if (memcmp(buf + pos, "\x50\x4B\x05\x06", 4)) {
        continue;
      }

      zip_->Seek(real_pos + pos);
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
  int total_entries, central_directory_offset;
  if (!ReadEndCentralRecord(total_entries, central_directory_offset)) {
    return false; /* warned already */
  }

  /* Seek to the start of the central file directory. */
  zip_->Seek(central_directory_offset);

  /* Loop through files in central directory. */
  for (int i = 0; i < total_entries; ++i) {
    FileInfo info;
    info.data_offset_ = -1;
    int got = ProcessCdirFileHdr(info);
    if (got == -1) { /* error */
      break;
    }
    if (got == 0) { /* skip */
      continue;
    }

    FileInfo* p_info = new FileInfo(info);
    files_.push_back(p_info);
    fdb_->AddFile(
        "/" + p_info->name_, p_info->uncompressed_size_, p_info->crc32_,
        p_info);
  }

  if (files_.size() == 0) {
    WARN(ssprintf("%s: no files found in central file header", path_.c_str()));
  }

  return true;
}

int RageFileDriverZip::ProcessCdirFileHdr(FileInfo& info) {
  std::string error;
  std::string sig = FileReading::ReadString(*zip_, 4, error);
  if (sig != "\x50\x4B\x01\x02") {
    WARN(ssprintf(
        "%s: central directory record signature not found", path_.c_str()));
    return -1;
  }

  FileReading::read_8(*zip_, error);     /* skip version made by */
  FileReading::read_8(*zip_, error);     /* skip OS made by */
  FileReading::read_16_le(*zip_, error); /* skip version needed to extract */
  int general_purpose = FileReading::read_16_le(*zip_, error);
  info.compression_method_ =
      (ZipCompressionMethod)FileReading::read_16_le(*zip_, error);
  FileReading::read_16_le(*zip_, error); /* skip last mod file time */
  FileReading::read_16_le(*zip_, error); /* skip last mod file date */
  info.crc32_ = FileReading::read_32_le(*zip_, error);
  info.compressed_size_ = FileReading::read_32_le(*zip_, error);
  info.uncompressed_size_ = FileReading::read_32_le(*zip_, error);
  int filename_length = FileReading::read_16_le(*zip_, error);
  int extra_field_length = FileReading::read_16_le(*zip_, error);
  int file_comment_length = FileReading::read_16_le(*zip_, error);
  FileReading::read_16_le(*zip_, error); /* relative offset of local header */
  FileReading::read_16_le(*zip_, error); /* skip internal file attributes */
  unsigned external_file_attributes = FileReading::read_32_le(*zip_, error);
  info.offset_ = FileReading::read_32_le(*zip_, error);

  /* Check for errors before reading variable-length fields. */
  if (error != "") {
    WARN(ssprintf("%s: %s", path_.c_str(), error.c_str()));
    return -1;
  }

  info.name_ = FileReading::ReadString(*zip_, filename_length, error);
  FileReading::SkipBytes(
      *zip_, extra_field_length, error); /* skip extra field */
  FileReading::SkipBytes(
      *zip_, file_comment_length, error); /* skip file comment */

  if (error != "") {
    WARN(ssprintf("%s: %s", path_.c_str(), error.c_str()));
    return -1;
  }

  /* Check usability last, so we always read past the whole entry and don't
   * leave the file pointer in the middle of a record. */
  if (general_purpose & 1) {
    WARN(ssprintf(
        "Skipped encrypted \"%s\" in \"%s\"", info.name_.c_str(),
        path_.c_str()));
    return 0;
  }

  /* Skip directories. */
  if (external_file_attributes & (1 << 4)) {
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

  std::string error;
  std::string sig = FileReading::ReadString(*zip_, 4, error);

  if (error != "") {
    WARN(ssprintf(
        "%s: error opening \"%s\": %s", path_.c_str(), info.name_.c_str(),
        error.c_str()));
    return false;
  }

  if (sig != "\x50\x4B\x03\x04") {
    WARN(ssprintf(
        "%s: local file header not found for \"%s\"", path_.c_str(),
        info.name_.c_str()));
    return false;
  }

  FileReading::SkipBytes(
      *zip_, 22, error); /* skip most of the local file header */

  const int filename_length = FileReading::read_16_le(*zip_, error);
  const int extra_field_length = FileReading::read_16_le(*zip_, error);
  info.data_offset_ = zip_->Tell() + filename_length + extra_field_length;

  if (error != "") {
    WARN(ssprintf("%s: %s", path_.c_str(), error.c_str()));
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
    const std::string& path, int mode, int& err) {
  if (mode & RageFile::WRITE) {
    err = ERROR_WRITING_NOT_SUPPORTED;
    return nullptr;
  }

  FileInfo* info = (FileInfo*)fdb_->GetFilePriv(path);
  if (info == nullptr) {
    err = ENOENT;
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

  RageFileDriverSlice* slice = new RageFileDriverSlice(
      zip_->Copy(), info->data_offset_, info->compressed_size_);
  slice->DeleteFileWhenFinished();

  switch (info->compression_method_) {
    case STORED:
      return slice;
    case DEFLATED: {
      RageFileObjInflate* inflate =
          new RageFileObjInflate(slice, info->uncompressed_size_);
      inflate->DeleteFileWhenFinished();
      return inflate;
    }
    default:
      /* unknown compression method */
      err = ENOSYS;
      return nullptr;
  }
}

/* NOP for now.  This could check to see if the ZIP's mtime has changed, and
 * reload. */
void RageFileDriverZip::FlushDirCache(const std::string& path) {}

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
