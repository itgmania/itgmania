/* This reader simply precaches all of the data from another reader. This
 * reduces CPU usage for sounds that are played several times at once. */

#include "RageSoundReader_Preload.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

#include "Preference.h"
#include "RageSoundReader.h"
#include "RageSoundUtil.h"
#include "RageUtil.h"
#include "global.h"

/* If true, preloaded sounds are stored in 16-bit instead of floats.  Most
 * processing happens after preloading, and it's usually a waste to store high-
 * resolution data for sound effects. */
Preference<bool> g_bSoundPreload16bit("SoundPreload16bit", true);

/* If a sound is smaller than this, we'll load it entirely into memory. */
Preference<int> g_iSoundPreloadMaxSamples(
    "SoundPreloadMaxSamples", 1024 * 1024);

#define samplesize (buffer_is16_bit_ ? sizeof(int16_t) : sizeof(float))
#define framesize (samplesize * channels_)

bool RageSoundReader_Preload::PreloadSound(RageSoundReader*& sound) {
  RageSoundReader_Preload* preload = new RageSoundReader_Preload;
  if (!preload->Open(sound)) {
    /* Preload failed.  It read some data, so we need to rewind the reader. */
    sound->SetPosition(0);
    delete preload;
    return false;
  }

  sound = preload;
  return true;
}

RageSoundReader_Preload::RageSoundReader_Preload()
    : buffer_(new std::string),
      buffer_is16_bit_(false),
      position_(0),
      sample_rate_(0),
      channels_(0),
      rate_(0.0f) {
  buffer_is16_bit_ = g_bSoundPreload16bit.Get();
}

int RageSoundReader_Preload::GetTotalFrames() const {
  return buffer_->size() / framesize;
}

bool RageSoundReader_Preload::Open(RageSoundReader* source) {
  ASSERT(source != nullptr);
  sample_rate_ = source->GetSampleRate();
  channels_ = source->GetNumChannels();
  rate_ = source->GetStreamToSourceRatio();

  int max_samples = g_iSoundPreloadMaxSamples.Get();

  /* Check the length, and see if we think it'll fit in the buffer. */
  int len = source->GetLength_Fast();
  if (len != -1) {
    float secs = len / 1000.f;

    int frames =
        static_cast<int>((secs * sample_rate_) + 0.5); /* seconds -> frames */
    int samples = unsigned(frames * channels_);        /* frames -> samples */
    if (samples > max_samples) {
      return false; /* Don't bother trying to preload it. */
    }

    int bytes = unsigned(samples * samplesize); /* samples -> bytes */
    buffer_.Get()->reserve(bytes);
  }

  for (;;) {
    /* If the rate changes, we won't preload it. */
    if (source->GetStreamToSourceRatio() != rate_) {
      return false; /* Don't bother trying to preload it. */
    }
    float buffer[1024];
    int cnt = source->Read(buffer, ARRAYLEN(buffer) / channels_);

    if (cnt == END_OF_FILE) {
      break;
    }
    if (cnt < 0) {
      return false;
    }
    /* Add the buffer. */
    if (buffer_is16_bit_) {
      int16_t buffer16[1024];
      RageSoundUtil::ConvertFloatToNativeInt16(
          buffer, buffer16, cnt * channels_);
      buffer_.Get()->append(
          (char*)buffer16, (char*)(buffer16 + cnt * channels_));
    } else {
      buffer_.Get()->append((char*)buffer, (char*)(buffer + cnt * channels_));
    }

    if (buffer_.Get()->size() > max_samples * samplesize) {
      return false; /* too big */
    }
  }

  position_ = 0;
  delete source;
  return true;
}

int RageSoundReader_Preload::GetLength() const {
  return int(float(GetTotalFrames()) * 1000.f / sample_rate_);
}

int RageSoundReader_Preload::GetLength_Fast() const { return GetLength(); }

int RageSoundReader_Preload::SetPosition(int frame) {
  position_ = frame;
  position_ = static_cast<int>((position_ / rate_) + 0.5);

  if (position_ >= int(buffer_->size() / framesize)) {
    position_ = buffer_->size() / framesize;
    return 0;
  }

  return 1;
}

int RageSoundReader_Preload::GetNextSourceFrame() const {
  return static_cast<int>((position_ * rate_) + 0.5);
}

int RageSoundReader_Preload::Read(float* buffer, int frames) {
  const int size_frames = buffer_->size() / framesize;
  const int frames_avail = size_frames - position_;

  frames = std::min(frames, frames_avail);
  if (frames == 0) {
    return END_OF_FILE;
  }
  if (buffer_is16_bit_) {
    const int16_t* in =
        (const int16_t*)(buffer_->data() + (position_ * framesize));
    RageSoundUtil::ConvertNativeInt16ToFloat(in, buffer, frames * channels_);
  } else {
    memcpy(
        buffer, buffer_->data() + (position_ * framesize), frames * framesize);
  }
  position_ += frames;

  return frames;
}

RageSoundReader_Preload* RageSoundReader_Preload::Copy() const {
  return new RageSoundReader_Preload(*this);
}

int RageSoundReader_Preload::GetReferenceCount() const {
  return buffer_.GetReferenceCount();
}

/*
 * Copyright (c) 2003 Glenn Maynard
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
