#include "RageFileBasic.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "RageUtil.h"
#include "RageUtil_AutoPtr.h"
#include "global.h"

REGISTER_CLASS_TRAITS(RageFileBasic, pCopy->Copy());

RageFileObj::RageFileObj() {
  read_buffer_ = nullptr;
  write_buffer_ = nullptr;

  ResetReadBuf();

  read_buf_avail_ = 0;
  write_buffer_pos_ = 0;
  write_buffer_used_ = 0;
  eof_ = false;
  file_pos_ = 0;
  crc32_enabled_ = false;
  crc32_ = 0;
}

RageFileObj::RageFileObj(const RageFileObj& cpy) : RageFileBasic(cpy) {
  /* If the original file has a buffer, copy it. */
  if (cpy.read_buffer_ != nullptr) {
    read_buffer_ = new char[BSIZE];
    memcpy(read_buffer_, cpy.read_buffer_, BSIZE);

    int iOffsetIntoBuffer = cpy.read_buf_ - cpy.read_buffer_;
    read_buf_ = read_buffer_ + iOffsetIntoBuffer;
  } else {
    read_buffer_ = nullptr;
  }

  if (cpy.write_buffer_ != nullptr) {
    write_buffer_ = new char[cpy.write_buffer_size_];
    memcpy(write_buffer_, cpy.write_buffer_, write_buffer_used_);
  } else {
    write_buffer_ = nullptr;
  }

  read_buf_avail_ = cpy.read_buf_avail_;
  eof_ = cpy.eof_;
  file_pos_ = cpy.file_pos_;
  write_buffer_pos_ = cpy.write_buffer_pos_;
  write_buffer_size_ = cpy.write_buffer_size_;
  write_buffer_used_ = cpy.write_buffer_used_;
  crc32_enabled_ = cpy.crc32_enabled_;
  crc32_ = cpy.crc32_;
}

RageFileObj::~RageFileObj() {
  delete[] read_buffer_;
  delete[] write_buffer_;
}

int RageFileObj::Seek(int iOffset) {
  /* If we're already at the requested position, short circuit and don't flush
   * our buffer. */
  if (iOffset == file_pos_) {
    return file_pos_;
  }

  eof_ = false;

  /* If we're calculating a CRC32, disable it. */
  crc32_enabled_ = false;

  /* Note that seeks do not flush the write buffer.  Instead, we flush lazily,
   * on the next actual Write (or Flush).  Seek is not allowed to fail, and
   * users should not need to flush before seeking to do proper error checking.
   */
  ResetReadBuf();

  int iPos = SeekInternal(iOffset);
  if (iPos != -1) {
    file_pos_ = iPos;
  }
  return iPos;
}

int RageFileObj::Seek(int offset, int whence) {
  switch (whence) {
    case SEEK_CUR:
      return Seek(Tell() + offset);
    case SEEK_END:
      offset += GetFileSize();
  }
  return Seek((int)offset);
}

int RageFileObj::Read(void* pBuffer, size_t iBytes) {
  int iRet = 0;

  while (!eof_ && iBytes > 0) {
    if (read_buffer_ != nullptr && read_buf_avail_) {
      /* Copy data out of the buffer first. */
      int iFromBuffer = std::min((int)iBytes, read_buf_avail_);
      memcpy(pBuffer, read_buf_, iFromBuffer);
      if (crc32_enabled_) {
        CRC32(crc32_, pBuffer, iFromBuffer);
      }

      iRet += iFromBuffer;
      file_pos_ += iFromBuffer;
      iBytes -= iFromBuffer;
      read_buf_avail_ -= iFromBuffer;
      read_buf_ += iFromBuffer;

      pBuffer = (char*)pBuffer + iFromBuffer;
    }

    if (!iBytes) {
      break;
    }

    ASSERT(read_buf_avail_ == 0);

    /* If buffering is disabled, or the block is bigger than the buffer,
     * read the remainder of the data directly into the desteination buffer. */
    if (read_buffer_ == nullptr || iBytes >= BSIZE) {
      /* We have a lot more to read, so don't waste time copying it into the
       * buffer. */
      int iFromFile = this->ReadInternal(pBuffer, iBytes);
      if (iFromFile == -1) {
        return -1;
      }

      if (iFromFile == 0) {
        eof_ = true;
      }

      if (crc32_enabled_) {
        CRC32(crc32_, pBuffer, iFromFile);
      }
      iRet += iFromFile;
      file_pos_ += iFromFile;
      return iRet;
    }

    /* If buffering is enabled, and we need more data, fill the buffer. */
    read_buf_ = read_buffer_;
    int iGot = FillReadBuf();
    if (iGot == -1) {
      return iGot;
    }
    if (iGot == 0) {
      eof_ = true;
    }
  }

  return iRet;
}

int RageFileObj::Read(std::string& sBuffer, int iBytes) {
  sBuffer.reserve(iBytes != -1 ? iBytes : this->GetFileSize());

  int iRet = 0;
  char buf[4096];
  while (iBytes == -1 || iRet < iBytes) {
    int ToRead = sizeof(buf);
    if (iBytes != -1) {
      ToRead = std::min(ToRead, iBytes - iRet);
    }

    const int iGot = Read(buf, ToRead);
    if (iGot == 0) {
      break;
    }
    if (iGot == -1) {
      return -1;
    }

    sBuffer.append(buf, iGot);
    iRet += iGot;
  }

  sBuffer.erase(sBuffer.begin() + iRet, sBuffer.end());

  return iRet;
}

int RageFileObj::Read(void* pBuffer, size_t iBytes, int iNmemb) {
  const int iRet = Read(pBuffer, iBytes * iNmemb);
  if (iRet == -1) {
    return -1;
  }

  /* If we're reading 10-byte blocks, and we got 27 bytes, we have 7 extra
   * bytes. Seek back.  XXX: seeking is very slow for eg. deflated ZIPs.  If the
   * block is small enough, we may be able to stuff the extra data into the
   * buffer. */
  const int iExtra = iRet % iBytes;
  Seek(Tell() - iExtra);

  return iRet / iBytes;
}

/* Empty the write buffer to disk.  Return -1 on error, 0 on success. */
int RageFileObj::EmptyWriteBuf() {
  if (write_buffer_ == nullptr) {
    return 0;
  }

  if (write_buffer_used_) {
    /* The write buffer may not align with the actual file, if we've seeked.
     * Only seek if needed. */
    bool bSeeked = (write_buffer_pos_ + write_buffer_used_ != file_pos_);
    if (bSeeked) {
      SeekInternal(write_buffer_pos_);
    }

    int iRet = WriteInternal(write_buffer_, write_buffer_used_);

    if (bSeeked) {
      SeekInternal(file_pos_);
    }
    if (iRet == -1) {
      return iRet;
    }
  }

  write_buffer_pos_ = file_pos_;
  write_buffer_used_ = 0;
  return 0;
}

int RageFileObj::Write(const void* pBuffer, size_t iBytes) {
  if (write_buffer_ != nullptr) {
    /* If the file position has moved away from the write buffer, or the
     * incoming data won't fit in the buffer, flush. */
    if (write_buffer_pos_ + write_buffer_used_ != file_pos_ ||
        write_buffer_used_ + (int)iBytes > write_buffer_size_) {
      int iRet = EmptyWriteBuf();
      if (iRet == -1) {
        return iRet;
      }
    }

    if (write_buffer_used_ + (int)iBytes <= write_buffer_size_) {
      memcpy(write_buffer_ + write_buffer_used_, pBuffer, iBytes);
      write_buffer_used_ += iBytes;
      file_pos_ += iBytes;
      if (crc32_enabled_) {
        CRC32(crc32_, pBuffer, iBytes);
      }
      return iBytes;
    }

    /* We're writing a lot of data, and it won't fit in the buffer.  We already
     * flushed above, so m_iWriteBufferUsed; fall through and write the block
     * normally. */
    ASSERT_M(write_buffer_used_ == 0, ssprintf("%i", write_buffer_used_));
  }

  int iRet = WriteInternal(pBuffer, iBytes);
  if (iRet != -1) {
    file_pos_ += iRet;
    if (crc32_enabled_) {
      CRC32(crc32_, pBuffer, iBytes);
    }
  }
  return iRet;
}

int RageFileObj::Write(const void* pBuffer, size_t iBytes, int iNmemb) {
  /* Simple write.  We never return partial writes. */
  int iRet = Write(pBuffer, iBytes * iNmemb) / iBytes;
  if (iRet == -1) {
    return -1;
  }
  return iRet / iBytes;
}

int RageFileObj::Flush() {
  int iRet = EmptyWriteBuf();
  if (iRet == -1) {
    return iRet;
  }
  return FlushInternal();
}

void RageFileObj::EnableReadBuffering() {
  if (read_buffer_ == nullptr) {
    read_buffer_ = new char[BSIZE];
  }
}

void RageFileObj::EnableWriteBuffering(int iBytes) {
  if (write_buffer_ == nullptr) {
    write_buffer_ = new char[iBytes];
    write_buffer_pos_ = file_pos_;
    write_buffer_size_ = iBytes;
  }
}

void RageFileObj::EnableCRC32(bool bOn) {
  if (!bOn) {
    crc32_enabled_ = false;
    return;
  }

  crc32_enabled_ = true;
  crc32_ = 0;
}

bool RageFileObj::GetCRC32(uint32_t* iRet) {
  if (!crc32_enabled_) {
    return false;
  }

  *iRet = crc32_;
  return true;
}

/* Read up to the next \n, and return it in out.  Strip the \n.  If the \n is
 * preceded by a \r (DOS newline), strip that, too. */
int RageFileObj::GetLine(std::string& sOut) {
  sOut = "";

  if (eof_) {
    return 0;
  }
  EnableReadBuffering();

  bool bGotData = false;
  for (;;) {
    bool bDone = false;

    /* Find the end of the block we'll move to out. */
    char* p = (char*)memchr(read_buf_, '\n', read_buf_avail_);
    bool bReAddCR = false;
    if (p == nullptr) {
      /* Hack: If the last character of the buffer is \r, then it's likely that
       * an
       * \r\n has been split across buffers.  Move everything else, then move
       * the
       * \r to the beginning of the buffer and handle it the next time around
       * the loop. */
      if (read_buf_avail_ && read_buf_[read_buf_avail_ - 1] == '\r') {
        bReAddCR = true;
        --read_buf_avail_;
      }

      p = read_buf_ + read_buf_avail_; /* everything */
    } else {
      bDone = true;
    }

    if (p >= read_buf_) {
      char* RealEnd = p;
      if (bDone && p > read_buf_ && p[-1] == '\r') {
        --RealEnd; /* not including \r */
      }
      sOut.append(read_buf_, RealEnd);

      if (bDone) {
        ++p; /* skip \n */
      }
      const int iUsed = p - read_buf_;
      if (iUsed) {
        read_buf_avail_ -= iUsed;
        file_pos_ += iUsed;
        bGotData = true;
        read_buf_ = p;
      }
    }

    if (bReAddCR) {
      ASSERT(read_buf_avail_ == 0);
      read_buf_ = read_buffer_;
      read_buffer_[read_buf_avail_] = '\r';
      ++read_buf_avail_;
    }

    if (bDone) {
      break;
    }

    /* We need more data. */
    read_buf_ = read_buffer_;

    const int iSize = FillReadBuf();

    /* If we've read data already, then don't mark EOF yet.  Wait until the
     * next time we're called. */
    if (iSize == 0 && !bGotData) {
      eof_ = true;
      return 0;
    }
    if (iSize == -1) {
      return -1;  // error
    }
    if (iSize == 0) {
      break;  // EOF or error
    }
  }
  return bGotData ? 1 : 0;
}

// Always use "\r\n".  Even though the program may be running on Unix, the
// files written to a memory card are likely to be edited using Windows.
// #if defined(WIN32)
#define NEWLINE "\r\n"
// #else
// #define NEWLINE "\n"
// #endif

int RageFileObj::PutLine(const std::string& sStr) {
  if (Write(sStr) == -1) {
    return -1;
  }
  return Write(std::string(NEWLINE));
}

/* Fill the internal buffer.  This never marks EOF, since this is an internal,
 * hidden read; EOF should only be set as a result of a real read.  (That is,
 * disabling buffering shouldn't cause the results of AtEOF to change.) */
int RageFileObj::FillReadBuf() {
  /* Don't call this unless buffering is enabled. */
  ASSERT(read_buffer_ != nullptr);

  /* The buffer starts at m_Buffer; any data in it starts at m_pReadBuf; space
   * between the two is old data that we've read.  (Don't mangle that data; we
   * can use it for seeking backwards.) */
  const int iBufAvail = BSIZE - (read_buf_ - read_buffer_) - read_buf_avail_;
  ASSERT_M(
      iBufAvail >= 0, ssprintf(
                          "%p, %p, %i", static_cast<void*>(read_buf_),
                          static_cast<void*>(read_buffer_), BSIZE));
  const int iSize = this->ReadInternal(read_buf_ + read_buf_avail_, iBufAvail);

  if (iSize > 0) {
    read_buf_avail_ += iSize;
  }

  return iSize;
}

void RageFileObj::ResetReadBuf() {
  read_buf_avail_ = 0;
  read_buf_ = read_buffer_;
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
