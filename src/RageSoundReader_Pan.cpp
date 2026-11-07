#include "RageSoundReader_Pan.h"

#include <algorithm>
#include <string>

#include "RageSoundReader.h"
#include "RageSoundReader_Filter.h"
#include "RageSoundUtil.h"

RageSoundReader_Pan::RageSoundReader_Pan(RageSoundReader* source)
    : RageSoundReader_Filter(source) {
  pan_ = 0;
}

int RageSoundReader_Pan::Read(float* buf, int frames) {
  frames = source_->Read(buf, frames);
  if (frames < 0) {
    return frames;
  }

  int samples = frames * source_->GetNumChannels();

  if (source_->GetNumChannels() == 1) {
    RageSoundUtil::ConvertMonoToStereoInPlace(buf, samples);
    samples *= 2;
  }

  /* This block goes from iStreamFrame to iStreamFrame+iGotFrames. */
  if (GetNumChannels() == 2 && pan_ != 0.0) {
    RageSoundUtil::Pan(buf, frames, pan_);
  }

  return frames;
}

unsigned RageSoundReader_Pan::GetNumChannels() const {
  return std::max(2u, RageSoundReader_Filter::GetNumChannels());
}

bool RageSoundReader_Pan::SetProperty(
    const std::string& property, float value) {
  if (property == "Pan") {
    pan_ = value;
    return true;
  }

  return RageSoundReader_Filter::SetProperty(property, value);
}

/*
 * Copyright (c) 2006 Glenn Maynard
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
