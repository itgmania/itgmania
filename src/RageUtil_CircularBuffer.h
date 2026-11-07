/* CircBuf - A fast, thread-safe, lockless circular buffer. */

#ifndef RAGE_UTIL_CIRCULAR_BUFFER
#define RAGE_UTIL_CIRCULAR_BUFFER

#include <algorithm>
#include <cstring>

/* Lock-free circular buffer.  This should be threadsafe if one thread is
 * reading and another is writing. */
template <class T>
class CircBuf {
  T* buf_;
  /* read_pos is the position data is read from; write_pos is the position
   * data is written to.  If read_pos == write_pos, the buffer is empty.
   *
   * There will always be at least one position empty, as a completely full
   * buffer (read_pos == write_pos) is indistinguishable from an empty buffer.
   *
   * Invariants: read_pos < size, write_pos < size. */
  unsigned size_;
  unsigned block_size_;

  /* These are volatile to prevent reads and writes to them from being
   * optimized. */
  volatile unsigned read_pos_, write_pos_;

 public:
  CircBuf() {
    buf_ = nullptr;
    clear();
  }

  ~CircBuf() { delete[] buf_; }

  void swap(CircBuf& rhs) {
    std::swap(size_, rhs.size_);
    std::swap(block_size_, rhs.block_size_);
    std::swap(read_pos_, rhs.read_pos_);
    std::swap(write_pos_, rhs.write_pos_);
    std::swap(buf_, rhs.buf_);
  }

  CircBuf& operator=(const CircBuf& rhs) {
    CircBuf c(rhs);
    this->swap(c);
    return *this;
  }

  CircBuf(const CircBuf& cpy) {
    size_ = cpy.size_;
    read_pos_ = cpy.read_pos_;
    write_pos_ = cpy.write_pos_;
    block_size_ = cpy.block_size_;
    if (size_) {
      buf_ = new T[size_];
      memcpy(buf_, cpy.buf_, size_ * sizeof(T));
    } else {
      buf_ = nullptr;
    }
  }

  /* Return the number of elements available to read. */
  unsigned num_readable() const {
    const int rpos = read_pos_;
    const int wpos = write_pos_;
    if (rpos < wpos) {
      /* The buffer looks like "eeeeDDDDeeee" (e = empty, D = data). */
      return wpos - rpos;
    } else if (rpos > wpos) {
      /* The buffer looks like "DDeeeeeeeeDD" (e = empty, D = data). */
      return size_ - (rpos - wpos);
    } else {  // if( rpos == wpos )
      /* The buffer looks like "eeeeeeeeeeee" (e = empty, D = data). */
      return 0;
    }
  }

  /* Return the number of writable elements. */
  unsigned num_writable() const {
    const int rpos = read_pos_;
    const int wpos = write_pos_;

    int ret;
    if (rpos < wpos) {
      /* The buffer looks like "eeeeDDDDeeee" (e = empty, D = data). */
      ret = size_ - (wpos - rpos);
    } else if (rpos > wpos) {
      /* The buffer looks like "DDeeeeeeeeDD" (e = empty, D = data). */
      ret = rpos - wpos;
    } else {  // if( rpos == wpos )
      /* The buffer looks like "eeeeeeeeeeee" (e = empty, D = data). */
      ret = size_;
    }

    /* Subtract the blocksize, to account for the element that we never fill
     * while keeping the entries aligned to m_iBlockSize. */
    return ret - block_size_;
  }

  unsigned capacity() const { return size_; }

  void reserve(unsigned n, int block_size = 1) {
    block_size_ = block_size;

    clear();
    delete[] buf_;
    buf_ = nullptr;

    /* Reserve an extra byte.  We'll never fill more than n bytes; the extra
     * byte is to guarantee that read_pos != write_pos when the buffer is full,
     * since that would be ambiguous with an empty buffer. */
    if (n != 0) {
      size_ = n + 1;
      size_ = ((size_ + block_size - 1) / block_size) * block_size;  // round up

      buf_ = new T[size_];
    } else {
      size_ = 0;
    }
  }

  void clear() {
    read_pos_ = 0;
    write_pos_ = 0;
  }

  /* Indicate that n elements have been written. */
  void advance_write_pointer(int n) { write_pos_ = (write_pos_ + n) % size_; }

  /* Indicate that n elements have been read. */
  void advance_read_pointer(int n) { read_pos_ = (read_pos_ + n) % size_; }

  void get_write_pointers(T* pointers[2], unsigned sizes[2]) {
    const int rpos = read_pos_;
    const int wpos = write_pos_;

    if (rpos <= wpos) {
      /* The buffer looks like "eeeeDDDDeeee" or "eeeeeeeeeeee" (e = empty, D =
       * data). */
      pointers[0] = buf_ + wpos;
      pointers[1] = buf_;

      sizes[0] = size_ - wpos;
      sizes[1] = rpos;
    } else if (rpos > wpos) {
      /* The buffer looks like "DDeeeeeeeeDD" (e = empty, D = data). */
      pointers[0] = buf_ + wpos;
      pointers[1] = nullptr;

      sizes[0] = rpos - wpos;
      sizes[1] = 0;
    }

    /* Subtract the blocksize, to account for the element that we never fill
     * while keeping the entries aligned to m_iBlockSize. */
    if (sizes[1]) {
      sizes[1] -= block_size_;
    } else {
      sizes[0] -= block_size_;
    }
  }

  /* Like get_write_pointers, but only return the first range available. */
  T* get_write_pointer(unsigned* sizes) {
    T* both_pointers[2];
    unsigned both_sizes[2];
    get_write_pointers(both_pointers, both_sizes);
    *sizes = both_sizes[0];
    return both_pointers[0];
  }

  void get_read_pointers(T* pointers[2], unsigned sizes[2]) {
    const int rpos = read_pos_;
    const int wpos = write_pos_;

    if (rpos < wpos) {
      /* The buffer looks like "eeeeDDDDeeee" (e = empty, D = data). */
      pointers[0] = buf_ + rpos;
      pointers[1] = nullptr;

      sizes[0] = wpos - rpos;
      sizes[1] = 0;
    } else if (rpos > wpos) {
      /* The buffer looks like "DDeeeeeeeeDD" (e = empty, D = data). */
      pointers[0] = buf_ + rpos;
      pointers[1] = buf_;

      sizes[0] = size_ - rpos;
      sizes[1] = wpos;
    } else {
      /* The buffer looks like "eeeeeeeeeeee" (e = empty, D = data). */
      pointers[0] = nullptr;
      pointers[1] = nullptr;

      sizes[0] = 0;
      sizes[1] = 0;
    }
  }

  /* Write buffer_size elements from buffer, and advance the write pointer.  If
   * the data will not fit entirely, the write pointer will be unchanged
   * and false will be returned. */
  bool write(const T* buffer, unsigned buffer_size) {
    T* p[2];
    unsigned sizes[2];
    get_write_pointers(p, sizes);

    if (buffer_size > sizes[0] + sizes[1]) {
      return false;
    }

    const int from_first = std::min(buffer_size, sizes[0]);
    memcpy(p[0], buffer, from_first * sizeof(T));
    if (buffer_size > sizes[0]) {
      memcpy(p[1], buffer + from_first, (buffer_size - sizes[0]) * sizeof(T));
    }

    advance_write_pointer(buffer_size);

    return true;
  }

  /* Read buffer_size elements from buffer, and advance the read pointer.  If
   * the buffer can not be filled completely, the read pointer will be unchanged
   * and false will be returned. */
  bool read(T* buffer, unsigned buffer_size) {
    T* p[2];
    unsigned sizes[2];
    get_read_pointers(p, sizes);

    if (buffer_size > sizes[0] + sizes[1]) {
      return false;
    }

    const int from_first = std::min(buffer_size, sizes[0]);
    memcpy(buffer, p[0], from_first * sizeof(T));
    if (buffer_size > sizes[0]) {
      memcpy(buffer + from_first, p[1], (buffer_size - sizes[0]) * sizeof(T));
    }

    /* Set the data that we just read to 0xFF.  This way, if we're passing
     * pointesr through, we can tell if we accidentally get a stale pointer. */
    memset(p[0], 0xFF, from_first * sizeof(T));
    if (buffer_size > sizes[0]) {
      memset(p[1], 0xFF, (buffer_size - sizes[0]) * sizeof(T));
    }

    advance_read_pointer(buffer_size);
    return true;
  }
};

#endif

/*
 * Copyright (c) 2004 Glenn Maynard
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
