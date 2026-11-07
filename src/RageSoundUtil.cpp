#include "RageSoundUtil.h"

#include <algorithm>
#include <cstdint>

#include "RageUtil.h"

void RageSoundUtil::Attenuate(float* buf, int samples, float volume) {
  while (samples--) {
    *buf *= volume;
    ++buf;
  }
}

/* Pan buffer left or right; fPos is -1...+1.  Buffer is assumed to be stereo.
 */
void RageSoundUtil::Pan(float* buffer, int frames, float pos) {
  if (pos == 0) {
    return; /* no change */
  }

  bool swap = pos < 0;
  if (swap) {
    pos = -pos;
  }

  float left_factors[2] = {1 - pos, 0};
  float right_factors[2] = {
      SCALE(pos, 0, 1, 0.5f, 0), SCALE(pos, 0, 1, 0.5f, 1)};

  if (swap) {
    std::swap(left_factors[0], right_factors[0]);
    std::swap(left_factors[1], right_factors[1]);
  }

  for (int samp = 0; samp < frames; ++samp) {
    float l = buffer[0] * left_factors[0] + buffer[1] * left_factors[1];
    float r = buffer[0] * right_factors[0] + buffer[1] * right_factors[1];
    buffer[0] = l;
    buffer[1] = r;
    buffer += 2;
  }
}

void RageSoundUtil::Fade(
    float* buffer, int frames, int channels, float start_volume,
    float end_volume) {
  /* If the whole buffer is full volume, skip. */
  if (start_volume > .9999f && end_volume > .9999f) {
    return;
  }

  for (int frame = 0; frame < frames; ++frame) {
    float vol_percent = SCALE(frame, 0, frames, start_volume, end_volume);

    vol_percent = std::clamp(vol_percent, 0.f, 1.f);
    for (int i = 0; i < channels; ++i) {
      *buffer *= vol_percent;
      buffer++;
    }
  }
}

/* Dupe mono to stereo in-place; do it in reverse, to handle overlap. */
void RageSoundUtil::ConvertMonoToStereoInPlace(float* data, int frames) {
  float* input = data;
  float* output = input;
  input += frames;
  output += frames * 2;
  while (frames--) {
    input -= 1;
    output -= 2;
    output[0] = input[0];
    output[1] = input[0];
  }
}

void RageSoundUtil::ConvertNativeInt16ToFloat(
    const int16_t* from, float* to, int samples) {
  for (int i = 0; i < samples; ++i) {
    to[i] = from[i] / 32768.0f;
  }
}

void RageSoundUtil::ConvertFloatToNativeInt16(
    const float* from, int16_t* to, int samples) {
  for (int i = 0; i < samples; ++i) {
    int out = static_cast<int>((from[i] * 32768.0f) + 0.5);
    to[i] = std::clamp(out, -32768, 32767);
  }
}

/*
 * Copyright (c) 2002-2007 Glenn Maynard
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
