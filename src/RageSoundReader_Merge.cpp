#include "RageSoundReader_Merge.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "RageLog.h"
#include "RageSoundMixBuffer.h"
#include "RageSoundReader.h"
#include "RageSoundReader_Pan.h"
#include "RageSoundReader_Resample_Good.h"
#include "RageUtil.h"
#include "global.h"

RageSoundReader_Merge::RageSoundReader_Merge() {
  m_iSampleRate = -1;
  m_iChannels = 0;
  m_iNextSourceFrame = 0;
  m_fCurrentStreamToSourceRatio = 1.0f;
}

RageSoundReader_Merge::~RageSoundReader_Merge() {
  for (RageSoundReader* it : m_aSounds) {
    delete it;
  }
}

RageSoundReader_Merge::RageSoundReader_Merge(const RageSoundReader_Merge& cpy)
    : RageSoundReader(cpy) {
  m_iSampleRate = cpy.m_iSampleRate;
  m_iChannels = cpy.m_iChannels;
  m_iNextSourceFrame = cpy.m_iNextSourceFrame;
  m_fCurrentStreamToSourceRatio = cpy.m_fCurrentStreamToSourceRatio;

  for (const RageSoundReader* it : cpy.m_aSounds) {
    m_aSounds.push_back(it->Copy());
  }
}

void RageSoundReader_Merge::AddSound(RageSoundReader* pSound) {
  m_aSounds.push_back(pSound);
}

/* If every sound has the same sample rate, return it.  Otherwise, return -1. */
int RageSoundReader_Merge::GetSampleRateInternal() const {
  // TODO: Convert to a set and compare values?
  int iRate = -1;
  for (const RageSoundReader* it : m_aSounds) {
    if (iRate == -1) {
      iRate = it->GetSampleRate();
    } else if (iRate != it->GetSampleRate()) {
      return -1;
    }
  }
  return iRate;
}

void RageSoundReader_Merge::Finish(int iPreferredSampleRate) {
  /* Figure out how many channels we have.  All sounds must either have 1 or 2
   * channels, which will be converted as needed, or have the same number of
   * channels. */
  m_iChannels = 1;
  for (RageSoundReader* it : m_aSounds) {
    m_iChannels = std::max(m_iChannels, it->GetNumChannels());
  }

  /*
   * We might get different sample rates from our sources.  If they're all the
   * same sample rate, just leave it alone, so the whole sound can be resampled
   * as a group. If not, resample eveything to the preferred rate.  (Using the
   * preferred rate should avoid redundant resampling later.)
   */
  m_iSampleRate = GetSampleRateInternal();
  if (m_iSampleRate == -1) {
    for (RageSoundReader*& it : m_aSounds) {
      RageSoundReader_Resample_Good* pResample =
          new RageSoundReader_Resample_Good(it, iPreferredSampleRate);
      it = pResample;
    }

    m_iSampleRate = iPreferredSampleRate;
  }

  /* If we have two channels, and any sounds have only one, convert them by
   * adding a Pan filter. */
  for (RageSoundReader*& it : m_aSounds) {
    if (it->GetNumChannels() != this->GetNumChannels()) {
      it = new RageSoundReader_Pan(it);
    }
  }

  /* If we have more than two channels, then all sounds must have the same
   * number of channels. */
  if (m_iChannels > 2) {
    std::vector<RageSoundReader*> aSounds;
    for (RageSoundReader* it : m_aSounds) {
      if (it->GetNumChannels() != m_iChannels) {
        LOG->Warn(
            "Discarded sound with %i channels, not %i", it->GetNumChannels(),
            m_iChannels);
        delete it;
        it = nullptr;
      } else {
        aSounds.push_back(it);
      }
    }
    m_aSounds = aSounds;
  }
}

int RageSoundReader_Merge::SetPosition(int iFrame) {
  m_iNextSourceFrame = iFrame;

  int iRet = 0;
  for (int i = 0; i < (int)m_aSounds.size(); ++i) {
    RageSoundReader* pSound = m_aSounds[i];
    int iThisRet = pSound->SetPosition(iFrame);
    if (iThisRet == -1) {
      return -1;
    }
    if (iThisRet > 0) {
      iRet = 1;
    }
  }

  return iRet;
}

bool RageSoundReader_Merge::SetProperty(
    const std::string& sProperty, float fValue) {
  bool bRet = false;
  for (unsigned i = 0; i < m_aSounds.size(); ++i) {
    if (m_aSounds[i]->SetProperty(sProperty, fValue)) {
      bRet = true;
    }
  }

  return bRet;
}

/* As we iterate through the sound tree, we'll find that we need data from
 * different sounds; a sound may be needed by more than one other sound. */
int RageSoundReader_Merge::Read(float* pBuffer, int iFrames) {
  if (m_aSounds.empty()) {
    return END_OF_FILE;
  }

  /*
   * The sounds start together and are read for the same number of frames, so
   * they stay aligned; the first one sets the position and rate we report.
   * Don't compare the others against it: a speed change that keeps the pitch
   * picks each sound's slices by its own content, so their positions and
   * ratios differ from block to block (by up to a quarter of the stretcher's
   * window), without drifting apart.
   */
  int iNextSourceFrame = m_aSounds[0]->GetNextSourceFrame();
  float fRatio = m_aSounds[0]->GetStreamToSourceRatio();

  /* Normally, m_iNextSourceFrame should already be aligned with the
   * GetNextSourceFrame of the first sound.  If it's not, adjust it and return.
   */
  if (m_iNextSourceFrame != iNextSourceFrame ||
      m_fCurrentStreamToSourceRatio != fRatio) {
    m_iNextSourceFrame = iNextSourceFrame;
    m_fCurrentStreamToSourceRatio = fRatio;
    return 0;
  }

  // Read directly if there's only one sound
  if (m_aSounds.size() == 1) {
    RageSoundReader* pSound = m_aSounds.front();
    iFrames = pSound->Read(pBuffer, iFrames);
    if (iFrames > 0) {
      m_iNextSourceFrame +=
          static_cast<int>((iFrames * m_fCurrentStreamToSourceRatio) + 0.5);
    }
    return iFrames;
  }

  RageSoundMixBuffer mix;
  float Buffer[2048];
  iFrames = std::min(iFrames, static_cast<int>(ARRAYLEN(Buffer) / m_iChannels));

  /* Read the first sound once, like the single sound above, so the position
   * and rate we report hold for everything we return.  It returns no data
   * when its rate is about to change. */
  ASSERT(m_aSounds[0]->GetNumChannels() == m_iChannels);
  iFrames = m_aSounds[0]->Read(Buffer, iFrames);
  if (iFrames <= 0) {
    return iFrames;
  }
  mix.write(Buffer, iFrames * m_iChannels);

  /* Read as many frames from each of the others, retrying when it returns no
   * data at its own block or rate changes.  If one ends or fails, it's silent
   * for the rest of this read. */
  for (size_t i = 1; i < m_aSounds.size(); ++i) {
    RageSoundReader* pSound = m_aSounds[i];
    ASSERT(pSound->GetNumChannels() == m_iChannels);

    int iFramesRead = 0;
    while (iFramesRead < iFrames) {
      int iGotFrames = pSound->RetriedRead(Buffer, iFrames - iFramesRead);
      if (iGotFrames < 0) {
        break;
      }

      mix.SetWriteOffset(iFramesRead * m_iChannels);
      mix.write(Buffer, iGotFrames * m_iChannels);
      iFramesRead += iGotFrames;
    }
  }

  /* Read mixed frames into the output buffer. */
  mix.read(pBuffer);

  m_iNextSourceFrame +=
      static_cast<int>((iFrames * m_fCurrentStreamToSourceRatio) + 0.5);

  return iFrames;
}

int RageSoundReader_Merge::GetLength() const {
  int iLength = 0;
  for (unsigned i = 0; i < m_aSounds.size(); ++i) {
    iLength = std::max(iLength, m_aSounds[i]->GetLength());
  }
  return iLength;
}

int RageSoundReader_Merge::GetLength_Fast() const {
  int iLength = 0;
  for (unsigned i = 0; i < m_aSounds.size(); ++i) {
    iLength = std::max(iLength, m_aSounds[i]->GetLength_Fast());
  }
  return iLength;
}

/*
 * Copyright (c) 2004-2006 Glenn Maynard
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
