#include "RageSoundMixBuffer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

RageSoundMixBuffer::RageSoundMixBuffer() : offset_(0) {}
RageSoundMixBuffer::~RageSoundMixBuffer() {}

/* write() will start mixing iOffset samples into the buffer.  Be careful; this
 * is measured in samples, not frames, so if the data is stereo, multiply by
 * two. */
void RageSoundMixBuffer::SetWriteOffset(int offset) {
  offset_ = static_cast<int>(offset);
}

void RageSoundMixBuffer::Extend(unsigned samples) {
  const unsigned realsize = samples + offset_;
  if (mixbuf_.size() < realsize) {
    mixbuf_.resize(realsize, 0.0f);
  }
}

void RageSoundMixBuffer::write(
    const float* buf, unsigned i_size, int source_stride, int dest_stride) {
  if (i_size == 0) {
    return;
  }

  // iSize = 3, iDestStride = 2 uses 4 frames.  Don't allocate the stride of the
  // last sample.
  Extend(i_size * dest_stride - (dest_stride - 1));

  // Load the audio at m_iOffset into the buffer.
  float* dest_buf = &mixbuf_[offset_];

  while (i_size) {
    *dest_buf += *buf;
    buf += source_stride;
    dest_buf += dest_stride;
    --i_size;
  }
}

void RageSoundMixBuffer::read(int16_t* buf) {
  for (unsigned pos = 0; pos < mixbuf_.size(); ++pos) {
    // do the read
    float out = mixbuf_[pos];
    // ensure volume is within expected levels to prevent clipping
    out = std::clamp(out, -1.0f, +1.0f);
    // round rather than truncate to minimize distortion
    buf[pos] = static_cast<int16_t>(std::round(out * INT16_MAX));
  }
  mixbuf_.clear();
}

void RageSoundMixBuffer::read(float* buf) {
  // ensure volume is within expected levels to prevent clipping
  std::transform(mixbuf_.begin(), mixbuf_.end(), buf, [](float s) {
    return std::clamp(s, -1.0f, +1.0f);
  });
  mixbuf_.clear();
}

void RageSoundMixBuffer::read_deinterlace(float** bufs, int channels) {
  for (unsigned i = 0; i < mixbuf_.size() / channels; ++i) {
    for (int ch = 0; ch < channels; ++ch) {
      bufs[ch][i] = mixbuf_[channels * i + ch];
    }
  }
  mixbuf_.clear();
}

/*
 * Copyright (c) 2002-2004 Glenn Maynard
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
