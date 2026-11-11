#include "RageSoundReader_PostBuffering.h"

#include <algorithm>
#include <string>

#include "RageSoundReader.h"
#include "RageSoundReader_Filter.h"
#include "RageSoundUtil.h"
#include "RageThreads.h"
#include "global.h"

/*
 * This filter is normally inserted after extended buffering, implementing
 * properties that do not seek the sound, allowing these properties to be
 * changed with low latency.
 */
RageMutex mutex("PostBuffering");
static float master_volume = 1.0f;

RageSoundReader_PostBuffering::RageSoundReader_PostBuffering(
    RageSoundReader* source)
    : RageSoundReader_Filter(source) {
  volume_ = 1.0f;
}

void RageSoundReader_PostBuffering::SetMasterVolume(float volume) {
  LockMut(mutex);
  master_volume = volume;
}

int RageSoundReader_PostBuffering::Read(float* buf, int frames) {
  frames = source_->Read(buf, frames);
  if (frames < 0) {
    return frames;
  }

  // Combine the sound's volume with master volume.
  mutex.Lock();

  // Square the master so lower volumes are more sensitive.
  // This lines up better with perceived volume.
  float volume = volume_ * master_volume * master_volume;
  volume = std::clamp(volume, 0.0f, 1.0f);
  mutex.Unlock();

  if (volume != 1.0f) {
    RageSoundUtil::Attenuate(buf, frames * this->GetNumChannels(), volume);
  }

  return frames;
}

bool RageSoundReader_PostBuffering::SetProperty(
    const std::string& property, float value) {
  if (property == "Volume") {
    volume_ = value;
    return true;
  }

  return RageSoundReader_Filter::SetProperty(property, value);
}

/*
 * Copyright (c) 2007 Glenn Maynard
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
