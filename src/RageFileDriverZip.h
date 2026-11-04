#ifndef RAGE_FILE_DRIVER_ZIP_H
#define RAGE_FILE_DRIVER_ZIP_H

#include <string>
#include <vector>

#include "RageFileDriver.h"
#include "RageThreads.h"

/** @brief A read-only file driver for ZIPs. */
class RageFileDriverZip : public RageFileDriver {
 public:
  RageFileDriverZip(const std::string& sPath);
  bool Load(const std::string& sPath);

  virtual ~RageFileDriverZip();

  RageFileBasic* Open(const std::string& sPath, int iMode, int& iErr);
  void FlushDirCache(const std::string& sPath);

  void DeleteFileWhenFinished() { file_owned_ = true; }

  /* Lower-level access: */
  enum ZipCompressionMethod { STORED = 0, DEFLATED = 8 };
  struct FileInfo {
    std::string name_;
    int offset_;
    int data_offset_;

    ZipCompressionMethod compression_method_;
    int crc32_;
    int compressed_size_, uncompressed_size_;
  };

 private:
  bool file_owned_;

  RageFileBasic* zip_;
  std::vector<FileInfo*> files_;

  std::string path_;
  std::string comment_;

  /* Open() must be threadsafe.  Mutex access to "zip", since we seek
   * around in it when reading files. */
  RageMutex mutex_;

  bool ParseZipfile();
  bool ReadEndCentralRecord(
      int& total_entries_central_dir, int& offset_start_central_directory);
  int ProcessCdirFileHdr(FileInfo& info);
  bool SeekToEndCentralRecord();
  bool ReadLocalFileHeader(FileInfo& info);
};

#endif

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
