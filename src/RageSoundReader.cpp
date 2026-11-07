#include "RageSoundReader.h"

#include "RageLog.h"
#include "RageUtil_AutoPtr.h"

REGISTER_CLASS_TRAITS(RageSoundReader, pCopy->Copy());

/* Read(), handling the STREAM_LOOPED and empty return cases. */
int RageSoundReader::RetriedRead(
    float* buffer, int frames, int* source_frame, float* rate) {
  if (frames == 0) {
    return 0;
  }

  // pReader may return 0, which means "try again immediately". As a failsafe,
  // only try this a finite number of times. Use a high number, because in
  // principle each filter in the stack may cause this.
  unsigned retry_count = 50;
  while (--retry_count) {
    if (rate) {
      *rate = this->GetStreamToSourceRatio();
    }
    if (source_frame) {
      *source_frame = this->GetNextSourceFrame();
    }
    int frames_read = this->Read(buffer, frames);
    if (frames_read == RageSoundReader::STREAM_LOOPED) {
      frames_read = 0;
    }
    if (frames_read != 0) {
      return frames_read;
    }
  }

  LOG->Warn("WARNING: could not read from the sound file. Giving up.");

  // Pretend we got EOF.
  return RageSoundReader::END_OF_FILE;
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
