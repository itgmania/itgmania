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
    RageFileBasic* file, int uncompressed_size) {
  file_owned_ = false;
  file_ = file;
  decomp_buf_avail_ = 0;
  inflate_ = new z_stream;
  memset(inflate_, 0, sizeof(z_stream));

  uncompressed_size_ = uncompressed_size;

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

int RageFileObjInflate::SeekInternal(int pos) {
  /* Optimization: if offset is the end of the file, it's a lseek(0,SEEK_END).
   * Don't decode anything. */
  if (pos >= uncompressed_size_) {
    file_pos_ = uncompressed_size_;
    file_->Seek(file_->GetFileSize());
    decomp_buf_ptr_ = decomp_buf_;
    decomp_buf_avail_ = 0;
    inflateReset(inflate_);
    return uncompressed_size_;
  }

  if (pos < file_pos_) {
    inflateReset(inflate_);
    decomp_buf_ptr_ = decomp_buf_;
    decomp_buf_avail_ = 0;

    file_->Seek(0);
    file_pos_ = 0;
  }

  int offset = pos - file_pos_;

  /* Can this be optimized? */
  char buf[1024 * 4];
  while (offset) {
    int got = ReadInternal(buf, std::min((int)sizeof(buf), offset));
    if (got == -1) {
      return -1;
    }

    if (got == 0) {
      break;
    }
    offset -= got;
  }

  return file_pos_;
}

RageFileObjDeflate::RageFileObjDeflate(RageFileBasic* file) {
  file_ = file;
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

int RageFileObjDeflate::WriteInternal(const void* buffer, size_t i_bytes) {
  if (i_bytes == 0) {
    return 0;
  }
  deflate_->next_in = (Bytef*)buffer;
  deflate_->avail_in = i_bytes;

  for (;;) {
    char buf[1024 * 4];
    deflate_->next_out = (Bytef*)buf;
    deflate_->avail_out = sizeof(buf);

    int err = deflate(deflate_, Z_NO_FLUSH);

    if (err != Z_OK) {
      FAIL_M(ssprintf("deflate: err %i", err));
    }
    if (deflate_->avail_out < sizeof(buf)) {
      int l_bytes = sizeof(buf) - deflate_->avail_out;
      int ret = file_->Write(buf, l_bytes);
      if (ret == -1) {
        SetError(file_->GetError());
        return -1;
      }
      if (ret < l_bytes) {
        SetError("Partial write");
        return -1;
      }
    }

    if (deflate_->avail_in == 0 && deflate_->avail_out != 0) {
      break;
    }
  }
  return i_bytes;
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
      int bytes = sizeof(buf) - deflate_->avail_out;
      int ret = file_->Write(buf, bytes);
      if (ret == -1) {
        SetError(file_->GetError());
        return -1;
      }
      if (ret < bytes) {
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
    RageFileBasic* raw_file, std::string& error, uint32_t* crc32) {
  std::unique_ptr<RageFileBasic> file(raw_file);

  error = "";

  file->Seek(0);
  file->EnableCRC32(true);

  {
    char magic[2];
    FileReading::ReadBytes(*file, magic, 2, error);
    if (error != "") {
      return nullptr;
    }

    if (magic[0] != '\x1f' || magic[1] != '\x8b') {
      error = "Not a gzipped file";
      return nullptr;
    }
  }

  uint8_t compression_method = FileReading::read_8(*file, error);
  uint8_t flags = FileReading::read_8(*file, error);
  FileReading::read_32_le(*file, error); /* time */
  FileReading::read_8(*file, error);     /* xfl */
  FileReading::read_8(*file, error);     /* os */
  if (error != "") {
    return nullptr;
  }

#define FTEXT 1 << 0
#define FHCRC 1 << 1
#define FEXTRA 1 << 2
#define FNAME 1 << 3
#define FCOMMENT 1 << 4
#define UNSUPPORTED_MASK ~((1 << 5) - 1)
  if (compression_method != 8) {
    error = ssprintf("Unsupported compression: %i", compression_method);
    return nullptr;
  }

  /* Warning: flags other than FNAME are untested, since gzip doesn't
   * actually output them. */
  if (flags & UNSUPPORTED_MASK) {
    error = ssprintf("Unsupported flags: %x", flags);
    return nullptr;
  }

  if (flags & FEXTRA) {
    int16_t size = FileReading::read_16_le(*file, error);
    FileReading::SkipBytes(*file, size, error);
  }

  if (flags & FNAME) {
    while (error == "" && FileReading::read_8(*file, error) != 0);
  }
  if (flags & FCOMMENT) {
    while (error == "" && FileReading::read_8(*file, error) != 0);
  }

  if (flags & FHCRC) {
    /* Get the CRC of the data read so far.  Be sure to do this before
     * reading iExpectedCRC16. */
    uint32_t actual_crc32;
    bool ok = file->GetCRC32(&actual_crc32);
    ASSERT(ok);

    uint16_t expected_crc16 = FileReading::read_u16_le(*file, error);
    uint16_t actual_crc16 = int16_t(actual_crc32 & 0xFFFF);
    if (error != "") {
      return nullptr;
    }

    if (actual_crc16 != expected_crc16) {
      error = "Header CRC error";
      return nullptr;
    }
  }

  /* We only need CRC checking on the raw data for the header, so disable
   * it. */
  file->EnableCRC32(false);

  if (error != "") {
    return nullptr;
  }

  int data_pos = file->Tell();

  /* Seek to the end, and grab the uncompressed flie size and CRC. */
  int footer_pos = file->GetFileSize() - 8;

  FileReading::Seek(*file, footer_pos, error);

  uint32_t expected_crc32 = FileReading::read_u32_le(*file, error);
  uint32_t uncompressed_size = FileReading::read_u32_le(*file, error);
  if (crc32 != nullptr) {
    *crc32 = expected_crc32;
  }

  FileReading::Seek(*file, data_pos, error);

  if (error != "") {
    return nullptr;
  }

  RageFileDriverSlice* slice_file =
      new RageFileDriverSlice(file.release(), data_pos, footer_pos - data_pos);
  slice_file->DeleteFileWhenFinished();
  RageFileObjInflate* inflate_file =
      new RageFileObjInflate(slice_file, uncompressed_size);
  inflate_file->DeleteFileWhenFinished();

  /* Enable CRC calculation only if the caller is interested. */
  if (crc32 != nullptr) {
    inflate_file->EnableCRC32();
  }

  return inflate_file;
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
RageFileObjGzip::RageFileObjGzip(RageFileBasic* file)
    : RageFileObjDeflate(file) {
  data_start_offset_ = -1;
}

/* Write the gzip header. */
int RageFileObjGzip::Start() {
  /* We should be at the start of the file. */
  ASSERT(this->Tell() == 0);

  static const char kHeader[] = {
      '\x1f', '\x8b',        // magic
      8,                     // method: deflate
      0,                     // no flags
      0,      0,      0, 0,  // no time
      0,                     // no extra flags
      '\xFF'                 // unknown os
  };

  if (file_->Write(kHeader, sizeof(kHeader)) == -1) {
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
  uint32_t crc;
  bool ok = this->GetCRC32(&crc);
  ASSERT(ok);

  /* Figure out the size of the data. */
  uint32_t size = Tell() - data_start_offset_;

  /* Write the CRC and size directly to the file, so they don't get compressed.
   */
  crc = Swap32LE(crc);
  if (file_->Write(&crc, sizeof(crc)) == -1) {
    SetError(file_->GetError());
    return -1;
  }

  /* Write the size. */
  size = Swap32LE(size);
  if (file_->Write(&size, sizeof(size)) == -1) {
    SetError(file_->GetError());
    return -1;
  }

  /* Flush the CRC and wize that we just wrote directly to the file. */
  return file_->Flush();
}

#include "RageFileDriverMemory.h"

bool GunzipString(const std::string& in, std::string& out, std::string& error) {
  RageFileObjMem* mem = new RageFileObjMem;
  mem->PutString(in);

  uint32_t crc32;
  RageFileBasic* file = GunzipFile(mem, error, &crc32);
  if (file == nullptr) {
    return false;
  }

  file->Read(out);

  /* Check the CRC. */
  unsigned ret;
  ASSERT(file->GetCRC32(&ret));
  RageUtil::SafeDelete(file);

  if (ret != crc32) {
    error = "CRC error";
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
