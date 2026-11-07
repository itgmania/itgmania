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

    int offset_into_buffer = cpy.read_buf_ - cpy.read_buffer_;
    read_buf_ = read_buffer_ + offset_into_buffer;
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

int RageFileObj::Seek(int offset) {
  /* If we're already at the requested position, short circuit and don't flush
   * our buffer. */
  if (offset == file_pos_) {
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

  int pos = SeekInternal(offset);
  if (pos != -1) {
    file_pos_ = pos;
  }
  return pos;
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

int RageFileObj::Read(void* buffer, size_t bytes) {
  int ret = 0;

  while (!eof_ && bytes > 0) {
    if (read_buffer_ != nullptr && read_buf_avail_) {
      /* Copy data out of the buffer first. */
      int from_buffer = std::min((int)bytes, read_buf_avail_);
      memcpy(buffer, read_buf_, from_buffer);
      if (crc32_enabled_) {
        CRC32(crc32_, buffer, from_buffer);
      }

      ret += from_buffer;
      file_pos_ += from_buffer;
      bytes -= from_buffer;
      read_buf_avail_ -= from_buffer;
      read_buf_ += from_buffer;

      buffer = (char*)buffer + from_buffer;
    }

    if (!bytes) {
      break;
    }

    ASSERT(read_buf_avail_ == 0);

    /* If buffering is disabled, or the block is bigger than the buffer,
     * read the remainder of the data directly into the desteination buffer. */
    if (read_buffer_ == nullptr || bytes >= BSIZE) {
      /* We have a lot more to read, so don't waste time copying it into the
       * buffer. */
      int from_file = this->ReadInternal(buffer, bytes);
      if (from_file == -1) {
        return -1;
      }

      if (from_file == 0) {
        eof_ = true;
      }

      if (crc32_enabled_) {
        CRC32(crc32_, buffer, from_file);
      }
      ret += from_file;
      file_pos_ += from_file;
      return ret;
    }

    /* If buffering is enabled, and we need more data, fill the buffer. */
    read_buf_ = read_buffer_;
    int got = FillReadBuf();
    if (got == -1) {
      return got;
    }
    if (got == 0) {
      eof_ = true;
    }
  }

  return ret;
}

int RageFileObj::Read(std::string& buffer, int bytes) {
  buffer.reserve(bytes != -1 ? bytes : this->GetFileSize());

  int ret = 0;
  char buf[4096];
  while (bytes == -1 || ret < bytes) {
    int to_read = sizeof(buf);
    if (bytes != -1) {
      to_read = std::min(to_read, bytes - ret);
    }

    const int got = Read(buf, to_read);
    if (got == 0) {
      break;
    }
    if (got == -1) {
      return -1;
    }

    buffer.append(buf, got);
    ret += got;
  }

  buffer.erase(buffer.begin() + ret, buffer.end());

  return ret;
}

int RageFileObj::Read(void* buffer, size_t bytes, int nmemb) {
  const int ret = Read(buffer, bytes * nmemb);
  if (ret == -1) {
    return -1;
  }

  /* If we're reading 10-byte blocks, and we got 27 bytes, we have 7 extra
   * bytes. Seek back.  XXX: seeking is very slow for eg. deflated ZIPs.  If the
   * block is small enough, we may be able to stuff the extra data into the
   * buffer. */
  const int extra = ret % bytes;
  Seek(Tell() - extra);

  return ret / bytes;
}

/* Empty the write buffer to disk.  Return -1 on error, 0 on success. */
int RageFileObj::EmptyWriteBuf() {
  if (write_buffer_ == nullptr) {
    return 0;
  }

  if (write_buffer_used_) {
    /* The write buffer may not align with the actual file, if we've seeked.
     * Only seek if needed. */
    bool seeked = (write_buffer_pos_ + write_buffer_used_ != file_pos_);
    if (seeked) {
      SeekInternal(write_buffer_pos_);
    }

    int ret = WriteInternal(write_buffer_, write_buffer_used_);

    if (seeked) {
      SeekInternal(file_pos_);
    }
    if (ret == -1) {
      return ret;
    }
  }

  write_buffer_pos_ = file_pos_;
  write_buffer_used_ = 0;
  return 0;
}

int RageFileObj::Write(const void* buffer, size_t bytes) {
  if (write_buffer_ != nullptr) {
    /* If the file position has moved away from the write buffer, or the
     * incoming data won't fit in the buffer, flush. */
    if (write_buffer_pos_ + write_buffer_used_ != file_pos_ ||
        write_buffer_used_ + (int)bytes > write_buffer_size_) {
      int ret = EmptyWriteBuf();
      if (ret == -1) {
        return ret;
      }
    }

    if (write_buffer_used_ + (int)bytes <= write_buffer_size_) {
      memcpy(write_buffer_ + write_buffer_used_, buffer, bytes);
      write_buffer_used_ += bytes;
      file_pos_ += bytes;
      if (crc32_enabled_) {
        CRC32(crc32_, buffer, bytes);
      }
      return bytes;
    }

    /* We're writing a lot of data, and it won't fit in the buffer.  We already
     * flushed above, so m_iWriteBufferUsed; fall through and write the block
     * normally. */
    ASSERT_M(write_buffer_used_ == 0, ssprintf("%i", write_buffer_used_));
  }

  int ret = WriteInternal(buffer, bytes);
  if (ret != -1) {
    file_pos_ += ret;
    if (crc32_enabled_) {
      CRC32(crc32_, buffer, bytes);
    }
  }
  return ret;
}

int RageFileObj::Write(const void* buffer, size_t bytes, int nmemb) {
  /* Simple write.  We never return partial writes. */
  int ret = Write(buffer, bytes * nmemb) / bytes;
  if (ret == -1) {
    return -1;
  }
  return ret / bytes;
}

int RageFileObj::Flush() {
  int ret = EmptyWriteBuf();
  if (ret == -1) {
    return ret;
  }
  return FlushInternal();
}

void RageFileObj::EnableReadBuffering() {
  if (read_buffer_ == nullptr) {
    read_buffer_ = new char[BSIZE];
  }
}

void RageFileObj::EnableWriteBuffering(int bytes) {
  if (write_buffer_ == nullptr) {
    write_buffer_ = new char[bytes];
    write_buffer_pos_ = file_pos_;
    write_buffer_size_ = bytes;
  }
}

void RageFileObj::EnableCRC32(bool on) {
  if (!on) {
    crc32_enabled_ = false;
    return;
  }

  crc32_enabled_ = true;
  crc32_ = 0;
}

bool RageFileObj::GetCRC32(uint32_t* ret) {
  if (!crc32_enabled_) {
    return false;
  }

  *ret = crc32_;
  return true;
}

/* Read up to the next \n, and return it in out.  Strip the \n.  If the \n is
 * preceded by a \r (DOS newline), strip that, too. */
int RageFileObj::GetLine(std::string& out) {
  out = "";

  if (eof_) {
    return 0;
  }
  EnableReadBuffering();

  bool got_data = false;
  for (;;) {
    bool done = false;

    /* Find the end of the block we'll move to out. */
    char* p = (char*)memchr(read_buf_, '\n', read_buf_avail_);
    bool re_add_cr = false;
    if (p == nullptr) {
      /* Hack: If the last character of the buffer is \r, then it's likely that
       * an
       * \r\n has been split across buffers.  Move everything else, then move
       * the
       * \r to the beginning of the buffer and handle it the next time around
       * the loop. */
      if (read_buf_avail_ && read_buf_[read_buf_avail_ - 1] == '\r') {
        re_add_cr = true;
        --read_buf_avail_;
      }

      p = read_buf_ + read_buf_avail_; /* everything */
    } else {
      done = true;
    }

    if (p >= read_buf_) {
      char* real_end = p;
      if (done && p > read_buf_ && p[-1] == '\r') {
        --real_end; /* not including \r */
      }
      out.append(read_buf_, real_end);

      if (done) {
        ++p; /* skip \n */
      }
      const int used = p - read_buf_;
      if (used) {
        read_buf_avail_ -= used;
        file_pos_ += used;
        got_data = true;
        read_buf_ = p;
      }
    }

    if (re_add_cr) {
      ASSERT(read_buf_avail_ == 0);
      read_buf_ = read_buffer_;
      read_buffer_[read_buf_avail_] = '\r';
      ++read_buf_avail_;
    }

    if (done) {
      break;
    }

    /* We need more data. */
    read_buf_ = read_buffer_;

    const int size = FillReadBuf();

    /* If we've read data already, then don't mark EOF yet.  Wait until the
     * next time we're called. */
    if (size == 0 && !got_data) {
      eof_ = true;
      return 0;
    }
    if (size == -1) {
      return -1;  // error
    }
    if (size == 0) {
      break;  // EOF or error
    }
  }
  return got_data ? 1 : 0;
}

// Always use "\r\n".  Even though the program may be running on Unix, the
// files written to a memory card are likely to be edited using Windows.
// #if defined(WIN32)
#define NEWLINE "\r\n"
// #else
// #define NEWLINE "\n"
// #endif

int RageFileObj::PutLine(const std::string& str) {
  if (Write(str) == -1) {
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
  const int buf_avail = BSIZE - (read_buf_ - read_buffer_) - read_buf_avail_;
  ASSERT_M(
      buf_avail >= 0, ssprintf(
                          "%p, %p, %i", static_cast<void*>(read_buf_),
                          static_cast<void*>(read_buffer_), BSIZE));
  const int size = this->ReadInternal(read_buf_ + read_buf_avail_, buf_avail);

  if (size > 0) {
    read_buf_avail_ += size;
  }

  return size;
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
