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

bool RageSoundReader_Preload::PreloadSound(RageSoundReader*& pSound) {
  RageSoundReader_Preload* pPreload = new RageSoundReader_Preload;
  if (!pPreload->Open(pSound)) {
    /* Preload failed.  It read some data, so we need to rewind the reader. */
    pSound->SetPosition(0);
    delete pPreload;
    return false;
  }

  pSound = pPreload;
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

bool RageSoundReader_Preload::Open(RageSoundReader* pSource) {
  ASSERT(pSource != nullptr);
  sample_rate_ = pSource->GetSampleRate();
  channels_ = pSource->GetNumChannels();
  rate_ = pSource->GetStreamToSourceRatio();

  int iMaxSamples = g_iSoundPreloadMaxSamples.Get();

  /* Check the length, and see if we think it'll fit in the buffer. */
  int iLen = pSource->GetLength_Fast();
  if (iLen != -1) {
    float fSecs = iLen / 1000.f;

    int iFrames =
        static_cast<int>((fSecs * sample_rate_) + 0.5); /* seconds -> frames */
    int iSamples = unsigned(iFrames * channels_);       /* frames -> samples */
    if (iSamples > iMaxSamples) {
      return false; /* Don't bother trying to preload it. */
    }

    int iBytes = unsigned(iSamples * samplesize); /* samples -> bytes */
    buffer_.Get()->reserve(iBytes);
  }

  for (;;) {
    /* If the rate changes, we won't preload it. */
    if (pSource->GetStreamToSourceRatio() != rate_) {
      return false; /* Don't bother trying to preload it. */
    }
    float buffer[1024];
    int iCnt = pSource->Read(buffer, ARRAYLEN(buffer) / channels_);

    if (iCnt == END_OF_FILE) {
      break;
    }
    if (iCnt < 0) {
      return false;
    }
    /* Add the buffer. */
    if (buffer_is16_bit_) {
      int16_t buffer16[1024];
      RageSoundUtil::ConvertFloatToNativeInt16(
          buffer, buffer16, iCnt * channels_);
      buffer_.Get()->append(
          (char*)buffer16, (char*)(buffer16 + iCnt * channels_));
    } else {
      buffer_.Get()->append((char*)buffer, (char*)(buffer + iCnt * channels_));
    }

    if (buffer_.Get()->size() > iMaxSamples * samplesize) {
      return false; /* too big */
    }
  }

  position_ = 0;
  delete pSource;
  return true;
}

int RageSoundReader_Preload::GetLength() const {
  return int(float(GetTotalFrames()) * 1000.f / sample_rate_);
}

int RageSoundReader_Preload::GetLength_Fast() const { return GetLength(); }

int RageSoundReader_Preload::SetPosition(int iFrame) {
  position_ = iFrame;
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

int RageSoundReader_Preload::Read(float* pBuffer, int iFrames) {
  const int iSizeFrames = buffer_->size() / framesize;
  const int iFramesAvail = iSizeFrames - position_;

  iFrames = std::min(iFrames, iFramesAvail);
  if (iFrames == 0) {
    return END_OF_FILE;
  }
  if (buffer_is16_bit_) {
    const int16_t* pIn =
        (const int16_t*)(buffer_->data() + (position_ * framesize));
    RageSoundUtil::ConvertNativeInt16ToFloat(pIn, pBuffer, iFrames * channels_);
  } else {
    memcpy(
        pBuffer, buffer_->data() + (position_ * framesize),
        iFrames * framesize);
  }
  position_ += iFrames;

  return iFrames;
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
