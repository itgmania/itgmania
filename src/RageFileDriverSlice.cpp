#include "RageFileDriverSlice.h"

#include <algorithm>
#include <cstddef>

#include "RageFileBasic.h"
#include "global.h"

RageFileDriverSlice::RageFileDriverSlice(
    RageFileBasic* pFile, int iOffset, int iFileSize) {
  file_ = pFile;
  offset_ = iOffset;
  file_size_ = iFileSize;
  file_pos_ = 0;
  file_owned_ = false;
}

RageFileDriverSlice::RageFileDriverSlice(const RageFileDriverSlice& cpy)
    : RageFileObj(cpy) {
  file_ = cpy.file_->Copy();
  offset_ = cpy.offset_;
  file_size_ = cpy.file_size_;
  file_pos_ = cpy.file_pos_;
  file_owned_ = true;
}

RageFileDriverSlice::~RageFileDriverSlice() {
  if (file_owned_) {
    delete file_;
  }
}

RageFileDriverSlice* RageFileDriverSlice::Copy() const {
  RageFileDriverSlice* pRet = new RageFileDriverSlice(*this);
  return pRet;
}

int RageFileDriverSlice::ReadInternal(void* buf, size_t bytes) {
  /* Make sure we're reading from the right place.  We might have been
   * constructed with a file not pointing to iOffset. */
  file_->Seek(file_pos_ + offset_);

  const int bytes_left = file_size_ - this->file_pos_;
  const int got = file_->Read(buf, std::min((int)bytes, bytes_left));
  if (got == -1) {
    SetError(file_->GetError());
    return -1;
  }

  file_pos_ += got;

  return got;
}

int RageFileDriverSlice::SeekInternal(int offset) {
  ASSERT(offset >= 0);
  offset = std::min(offset, file_size_);

  int ret = file_->Seek(offset_ + offset);
  if (ret == -1) {
    SetError(file_->GetError());
    return -1;
  }
  ret -= offset_;
  ASSERT(ret >= 0);
  file_pos_ = ret;

  return ret;
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
