#include "RageFileDriverDeflate.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

#include "RageException.h"
#include "RageFile.h"
#include "RageFileBasic.h"
#include "RageFileDriverSlice.h"
#include "RageUtil.h"
#include "RageUtil/Endian.h"
#include "global.h"
#include "zconf.h"

#if defined(_WIN32)
#include "zlib.h"
#elif defined(MACOSX)
#include "zlib.h"
#else
#include <zlib.h>
#endif

RageFileObjInflate::RageFileObjInflate(
    RageFileBasic* pFile, int iUncompressedSize) {
  file_owned_ = false;
  file_ = pFile;
  decomp_buf_avail_ = 0;
  inflate_ = new z_stream;
  memset(inflate_, 0, sizeof(z_stream));

  uncompressed_size_ = iUncompressedSize;

  int err = inflateInit2(inflate_, -MAX_WBITS);
  if (err == Z_MEM_ERROR) {
    RageException::Throw("inflateInit2( %i ): out of memory.", -MAX_WBITS);
  }
  if (err != Z_OK) {
    WARN(ssprintf("Huh? inflateInit2() err = %i", err));
  }

  decomp_buf_ptr_ = decomp_buf_;
  file_pos_ = 0;
}

RageFileObjInflate::RageFileObjInflate(const RageFileObjInflate& cpy)
    : RageFileObj(cpy) {
  /* XXX completely untested */
  /* Copy the entire decode state. */
  file_ = cpy.file_->Copy();
  file_owned_ = true;
  inflate_ = new z_stream;
  uncompressed_size_ = cpy.uncompressed_size_;
  file_pos_ = cpy.file_pos_;
  inflateCopy(inflate_, const_cast<z_stream*>(cpy.inflate_));

  decomp_buf_ptr_ = decomp_buf_ + (cpy.decomp_buf_ptr_ - cpy.decomp_buf_);
  decomp_buf_avail_ = cpy.decomp_buf_avail_;
  memcpy(decomp_buf_, cpy.decomp_buf_, decomp_buf_avail_);
}

RageFileObjInflate* RageFileObjInflate::Copy() const {
  return new RageFileObjInflate(*this);
}

RageFileObjInflate::~RageFileObjInflate() {
  if (file_owned_) {
    delete file_;
  }

  int err = inflateEnd(inflate_);
  if (err != Z_OK) {
    WARN(ssprintf("Huh? inflateEnd() err = %i", err));
  }

  delete inflate_;
}

int RageFileObjInflate::ReadInternal(void* buf, size_t bytes) {
  /* Don't read more than m_iUncompressedSize of data.  If we don't do this,
   * it's possible for a .gz to contain a header claiming 500k of data, but to
   * actually contain much more deflated data. */
  ASSERT_M(
      file_pos_ <= uncompressed_size_,
      ssprintf("%i, %i", file_pos_, uncompressed_size_));
  bytes = std::min(bytes, size_t(uncompressed_size_ - file_pos_));

  bool done = false;
  int ret = 0;
  while (bytes && !done) {
    if (!decomp_buf_avail_) {
      decomp_buf_ptr_ = decomp_buf_;
      decomp_buf_avail_ = 0;
      int got = file_->Read(decomp_buf_, sizeof(decomp_buf_));
      if (got == -1) {
        SetError(file_->GetError());
        return -1;
      }

      decomp_buf_avail_ = got;
    }

    inflate_->next_in = (Bytef*)decomp_buf_ptr_;
    inflate_->avail_in = decomp_buf_avail_;
    inflate_->next_out = (Bytef*)buf;
    inflate_->avail_out = bytes;

    int err = inflate(inflate_, Z_SYNC_FLUSH);
    switch (err) {
      case Z_DATA_ERROR:
        SetError("Data error");
        return -1;
      case Z_MEM_ERROR:
        SetError("out of memory");
        return -1;
      case Z_BUF_ERROR:
        SetError("file truncated");
        return -1;
      case Z_STREAM_END:
        done = true;
        break;
      case Z_OK:
        break;
      default:
        WARN(ssprintf("Huh? inflate err %i", err));
    }

    const int used = (char*)inflate_->next_in - decomp_buf_ptr_;
    decomp_buf_ptr_ += used;
    decomp_buf_avail_ -= used;

    const int got = (char*)inflate_->next_out - (char*)buf;
    file_pos_ += got;
    ret += got;
    buf = (char*)buf + got;
    bytes -= got;
  }

  return ret;
}

int RageFileObjInflate::SeekInternal(int iPos) {
  /* Optimization: if offset is the end of the file, it's a lseek(0,SEEK_END).
   * Don't decode anything. */
  if (iPos >= uncompressed_size_) {
    file_pos_ = uncompressed_size_;
    file_->Seek(file_->GetFileSize());
    decomp_buf_ptr_ = decomp_buf_;
    decomp_buf_avail_ = 0;
    inflateReset(inflate_);
    return uncompressed_size_;
  }

  if (iPos < file_pos_) {
    inflateReset(inflate_);
    decomp_buf_ptr_ = decomp_buf_;
    decomp_buf_avail_ = 0;

    file_->Seek(0);
    file_pos_ = 0;
  }

  int iOffset = iPos - file_pos_;

  /* Can this be optimized? */
  char buf[1024 * 4];
  while (iOffset) {
    int got = ReadInternal(buf, std::min((int)sizeof(buf), iOffset));
    if (got == -1) {
      return -1;
    }

    if (got == 0) {
      break;
    }
    iOffset -= got;
  }

  return file_pos_;
}

RageFileObjDeflate::RageFileObjDeflate(RageFileBasic* pFile) {
  file_ = pFile;
  file_owned_ = false;

  deflate_ = new z_stream;
  memset(deflate_, 0, sizeof(z_stream));

  int err = deflateInit2(
      deflate_, 3, Z_DEFLATED,
      -15,  // windowBits
      8,    // memLevel
      Z_DEFAULT_STRATEGY);

  if (err == Z_MEM_ERROR) {
    RageException::Throw("inflateInit2( %i ): out of memory.", -MAX_WBITS);
  }
  if (err != Z_OK) {
    WARN(ssprintf("Huh? inflateInit2() err = %i", err));
  }
}

RageFileObjDeflate::~RageFileObjDeflate() {
  FlushInternal();

  if (file_owned_) {
    delete file_;
  }

  int err = deflateEnd(deflate_);
  if (err != Z_OK) {
    WARN(ssprintf("Huh? deflateEnd() err = %i", err));
  }

  delete deflate_;
}

int RageFileObjDeflate::WriteInternal(const void* pBuffer, size_t iBytes) {
  if (iBytes == 0) {
    return 0;
  }
  deflate_->next_in = (Bytef*)pBuffer;
  deflate_->avail_in = iBytes;

  for (;;) {
    char buf[1024 * 4];
    deflate_->next_out = (Bytef*)buf;
    deflate_->avail_out = sizeof(buf);

    int err = deflate(deflate_, Z_NO_FLUSH);

    if (err != Z_OK) {
      FAIL_M(ssprintf("deflate: err %i", err));
    }
    if (deflate_->avail_out < sizeof(buf)) {
      int lBytes = sizeof(buf) - deflate_->avail_out;
      int iRet = file_->Write(buf, lBytes);
      if (iRet == -1) {
        SetError(file_->GetError());
        return -1;
      }
      if (iRet < lBytes) {
        SetError("Partial write");
        return -1;
      }
    }

    if (deflate_->avail_in == 0 && deflate_->avail_out != 0) {
      break;
    }
  }
  return iBytes;
}

/* Note that flushing clears compression state, so (unlike most Flush() calls)
 * calling this *does* change the result of the output if you continue writing
 * data. */
int RageFileObjDeflate::FlushInternal() {
  deflate_->avail_in = 0;

  for (;;) {
    char buf[1024 * 4];
    deflate_->next_out = (Bytef*)buf;
    deflate_->avail_out = sizeof(buf);

    int err = deflate(deflate_, Z_FINISH);
    if (err != Z_OK && err != Z_STREAM_END) {
      FAIL_M(ssprintf("deflate: err %i", err));
    }
    if (deflate_->avail_out < sizeof(buf)) {
      int iBytes = sizeof(buf) - deflate_->avail_out;
      int iRet = file_->Write(buf, iBytes);
      if (iRet == -1) {
        SetError(file_->GetError());
        return -1;
      }
      if (iRet < iBytes) {
        SetError("Partial write");
        return -1;
      }
    }

    if (err == Z_STREAM_END && deflate_->avail_out != 0) {
      return file_->Flush();
    }
  }
}

/*
 * Parse a .gz file, check the header CRC16 if present, and return the data
 * CRC32 and a decompressor.  pFile will be deleted.
 */
RageFileObjInflate* GunzipFile(
    RageFileBasic* pFile_, std::string& sError, uint32_t* iCRC32) {
  std::unique_ptr<RageFileBasic> pFile(pFile_);

  sError = "";

  pFile->Seek(0);
  pFile->EnableCRC32(true);

  {
    char magic[2];
    FileReading::ReadBytes(*pFile, magic, 2, sError);
    if (sError != "") {
      return nullptr;
    }

    if (magic[0] != '\x1f' || magic[1] != '\x8b') {
      sError = "Not a gzipped file";
      return nullptr;
    }
  }

  uint8_t iCompressionMethod = FileReading::read_8(*pFile, sError);
  uint8_t iFlags = FileReading::read_8(*pFile, sError);
  FileReading::read_32_le(*pFile, sError); /* time */
  FileReading::read_8(*pFile, sError);     /* xfl */
  FileReading::read_8(*pFile, sError);     /* os */
  if (sError != "") {
    return nullptr;
  }

#define FTEXT 1 << 0
#define FHCRC 1 << 1
#define FEXTRA 1 << 2
#define FNAME 1 << 3
#define FCOMMENT 1 << 4
#define UNSUPPORTED_MASK ~((1 << 5) - 1)
  if (iCompressionMethod != 8) {
    sError = ssprintf("Unsupported compression: %i", iCompressionMethod);
    return nullptr;
  }

  /* Warning: flags other than FNAME are untested, since gzip doesn't
   * actually output them. */
  if (iFlags & UNSUPPORTED_MASK) {
    sError = ssprintf("Unsupported flags: %x", iFlags);
    return nullptr;
  }

  if (iFlags & FEXTRA) {
    int16_t iSize = FileReading::read_16_le(*pFile, sError);
    FileReading::SkipBytes(*pFile, iSize, sError);
  }

  if (iFlags & FNAME) {
    while (sError == "" && FileReading::read_8(*pFile, sError) != 0);
  }
  if (iFlags & FCOMMENT) {
    while (sError == "" && FileReading::read_8(*pFile, sError) != 0);
  }

  if (iFlags & FHCRC) {
    /* Get the CRC of the data read so far.  Be sure to do this before
     * reading iExpectedCRC16. */
    uint32_t iActualCRC32;
    bool bOK = pFile->GetCRC32(&iActualCRC32);
    ASSERT(bOK);

    uint16_t iExpectedCRC16 = FileReading::read_u16_le(*pFile, sError);
    uint16_t iActualCRC16 = int16_t(iActualCRC32 & 0xFFFF);
    if (sError != "") {
      return nullptr;
    }

    if (iActualCRC16 != iExpectedCRC16) {
      sError = "Header CRC error";
      return nullptr;
    }
  }

  /* We only need CRC checking on the raw data for the header, so disable
   * it. */
  pFile->EnableCRC32(false);

  if (sError != "") {
    return nullptr;
  }

  int iDataPos = pFile->Tell();

  /* Seek to the end, and grab the uncompressed flie size and CRC. */
  int iFooterPos = pFile->GetFileSize() - 8;

  FileReading::Seek(*pFile, iFooterPos, sError);

  uint32_t iExpectedCRC32 = FileReading::read_u32_le(*pFile, sError);
  uint32_t iUncompressedSize = FileReading::read_u32_le(*pFile, sError);
  if (iCRC32 != nullptr) {
    *iCRC32 = iExpectedCRC32;
  }

  FileReading::Seek(*pFile, iDataPos, sError);

  if (sError != "") {
    return nullptr;
  }

  RageFileDriverSlice* pSliceFile =
      new RageFileDriverSlice(pFile.release(), iDataPos, iFooterPos - iDataPos);
  pSliceFile->DeleteFileWhenFinished();
  RageFileObjInflate* pInflateFile =
      new RageFileObjInflate(pSliceFile, iUncompressedSize);
  pInflateFile->DeleteFileWhenFinished();

  /* Enable CRC calculation only if the caller is interested. */
  if (iCRC32 != nullptr) {
    pInflateFile->EnableCRC32();
  }

  return pInflateFile;
}

/*
 * Usage:
 *
 * RageFile output;
 * output.Open( "hello.gz", RageFile::WRITE );
 *
 * RageFileObjGzip gzip( &output );
 * gzip.Start();
 * gzip.Write( "data" );
 * gzip.Finish();
 */
RageFileObjGzip::RageFileObjGzip(RageFileBasic* pFile)
    : RageFileObjDeflate(pFile) {
  data_start_offset_ = -1;
}

/* Write the gzip header. */
int RageFileObjGzip::Start() {
  /* We should be at the start of the file. */
  ASSERT(this->Tell() == 0);

  static const char header[] = {
      '\x1f', '\x8b',        // magic
      8,                     // method: deflate
      0,                     // no flags
      0,      0,      0, 0,  // no time
      0,                     // no extra flags
      '\xFF'                 // unknown os
  };

  if (file_->Write(header, sizeof(header)) == -1) {
    return -1;
  }

  data_start_offset_ = Tell();

  /* Enable and reset the CRC32 for the uncompressed data about to be
   * written to this file. */
  this->EnableCRC32(true);

  return 0;
}

/* Write the gzip footer. */
int RageFileObjGzip::Finish() {
  /* We're about to write to the underlying file (so the footer isn't
   * compressed).  Flush the compressed data first. */
  if (this->Flush() == -1) {
    return -1;
  }

  /* Read the CRC of the data that's been written. */
  uint32_t iCRC;
  bool bOK = this->GetCRC32(&iCRC);
  ASSERT(bOK);

  /* Figure out the size of the data. */
  uint32_t iSize = Tell() - data_start_offset_;

  /* Write the CRC and size directly to the file, so they don't get compressed.
   */
  iCRC = Swap32LE(iCRC);
  if (file_->Write(&iCRC, sizeof(iCRC)) == -1) {
    SetError(file_->GetError());
    return -1;
  }

  /* Write the size. */
  iSize = Swap32LE(iSize);
  if (file_->Write(&iSize, sizeof(iSize)) == -1) {
    SetError(file_->GetError());
    return -1;
  }

  /* Flush the CRC and wize that we just wrote directly to the file. */
  return file_->Flush();
}

#include "RageFileDriverMemory.h"

bool GunzipString(
    const std::string& sIn, std::string& sOut, std::string& sError) {
  RageFileObjMem* mem = new RageFileObjMem;
  mem->PutString(sIn);

  uint32_t iCRC32;
  RageFileBasic* pFile = GunzipFile(mem, sError, &iCRC32);
  if (pFile == nullptr) {
    return false;
  }

  pFile->Read(sOut);

  /* Check the CRC. */
  unsigned iRet;
  ASSERT(pFile->GetCRC32(&iRet));
  RageUtil::SafeDelete(pFile);

  if (iRet != iCRC32) {
    sError = "CRC error";
    return false;
  }

  return true;
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
