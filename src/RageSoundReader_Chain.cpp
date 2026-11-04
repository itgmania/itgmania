#include "RageSoundReader_Chain.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "RageLog.h"
#include "RageSound.h"
#include "RageSoundConstants.h"
#include "RageSoundMixBuffer.h"
#include "RageSoundReader.h"
#include "RageSoundReader_FileReader.h"
#include "RageSoundReader_Pan.h"
#include "RageSoundReader_Preload.h"
#include "RageSoundReader_Resample_Good.h"
#include "RageUtil.h"
#include "StdString.h"
#include "global.h"

/*
 * Keyed sounds should pass this object to SoundReader_Preload, to preprocess
 * it. Streaming more than two or three sounds is too expensive (keyed games can
 * play two dozen), and reading from disk is too latent.
 *
 * This can also be used for chained background music, which should always
 * stream, so we don't do the preloading in here.
 */
RageSoundReader_Chain::RageSoundReader_Chain() {
  // The preferred sample rate for resampling when sounds have different rates.
  preferred_sample_rate_ = FALLBACK_SAMPLE_RATE;

  // The actual sample rate of the audio after resampling.
  // Used for timing calculations, such as sound offsets or frame positioning.
  actual_sample_rate_ = -1;

  channels_ = 0;
  current_frame_ = 0;
  next_sound_ = 0;
}

RageSoundReader_Chain::~RageSoundReader_Chain() {
  /* Clear m_apActiveSounds. */
  while (!active_sounds_.empty()) {
    ReleaseSound(active_sounds_.front());
  }

  for (RageSoundReader* it : loaded_sounds_) {
    delete it;
  }
}

RageSoundReader_Chain* RageSoundReader_Chain::Copy() const {
  RageSoundReader_Chain* copy = new RageSoundReader_Chain();
  copy->preferred_sample_rate_ = this->preferred_sample_rate_;
  copy->actual_sample_rate_ = this->actual_sample_rate_;
  copy->channels_ = this->channels_;
  copy->current_frame_ = this->current_frame_;
  copy->next_sound_ = this->next_sound_;
  copy->active_sounds_ = this->active_sounds_;  // Shallow copy
  copy->loaded_sounds_ = this->loaded_sounds_;  // Shallow copy
  copy->sounds_ = this->sounds_;                // Shallow copy
  return copy;
}

/* The same sound may be used several times, and by several different chains.
 * Avoid loading the same sound multiple times.  We need to make a Copy() if we
 * need to read it more than once at a time. */
void RageSoundReader_Chain::AddSound(
    int iIndex, float fOffsetSecs, float fPan) {
  if (iIndex == -1) {
    return;
  }

  Sound s;
  s.index_ = iIndex;
  s.offset_ms_ = static_cast<int>((fOffsetSecs * 1000) + 0.5);
  s.pan_ = fPan;
  s.sound_ = nullptr;
  sounds_.push_back(s);
}

int RageSoundReader_Chain::LoadSound(std::string sPath) {
  MakeLower(sPath);

  std::map<std::string, RageSoundReader*>::const_iterator it =
      named_sounds_.find(sPath);
  if (it != named_sounds_.end()) {
    const RageSoundReader* pReader = it->second;

    for (int i = 0; i < (int)loaded_sounds_.size(); ++i) {
      if (loaded_sounds_[i] == pReader) {
        return i;
      }
    }
    FAIL_M(sPath);
  }

  std::string sError;
  bool bPrebuffer;
  RageSoundReader* pReader =
      RageSoundReader_FileReader::OpenFile(sPath, sError, &bPrebuffer);
  if (pReader == nullptr) {
    LOG->Warn(
        "RageSoundReader_Chain: error opening sound \"%s\": %s", sPath.c_str(),
        sError.c_str());
    return -1;
  }

  named_sounds_[sPath] = pReader;

  loaded_sounds_.push_back(named_sounds_[sPath]);
  return loaded_sounds_.size() - 1;
}

int RageSoundReader_Chain::LoadSound(RageSoundReader* pSound) {
  loaded_sounds_.push_back(pSound);
  return loaded_sounds_.size() - 1;
}

/* If every sound has the same sample rate, return it.  Otherwise, return -1. */
int RageSoundReader_Chain::GetSampleRateInternal() const {
  if (loaded_sounds_.empty()) {
    return preferred_sample_rate_;
  }

  int iRate = -1;
  for (const RageSoundReader* it : loaded_sounds_) {
    if (iRate == -1) {
      iRate = it->GetSampleRate();
    } else if (iRate != it->GetSampleRate()) {
      return -1;
    }
  }
  return iRate;
}

int RageSoundReader_Chain::GetSampleRate() const {
  if (actual_sample_rate_ == -1) {
    return preferred_sample_rate_;
  }
  return actual_sample_rate_;
}

void RageSoundReader_Chain::Finish() {
  /* Figure out how many channels we have.  All sounds must either have 1 or 2
   * channels, which will be converted as needed, or have the same number of
   * channels. */
  channels_ = 1;
  for (RageSoundReader* it : loaded_sounds_) {
    channels_ = std::max(channels_, it->GetNumChannels());
  }

  if (channels_ > 2) {
    for (RageSoundReader* it : loaded_sounds_) {
      if (it->GetNumChannels() != channels_) {
        LOG->Warn(
            "Discarded sound with %i channels, not %i", it->GetNumChannels(),
            channels_);
        delete it;
        it = nullptr;
      }
    }
  }

  /* Remove any sounds that don't have corresponding RageSoundReaders. */
  for (unsigned i = 0; i < sounds_.size();) {
    Sound& sound = sounds_[i];

    if (loaded_sounds_[sound.index_] == nullptr) {
      sounds_.erase(sounds_.begin() + i);
      continue;
    }

    ++i;
  }

  /*
   * We might get different sample rates from our sources.  If they're all the
   * same sample rate, just leave it alone, so the whole sound can be resampled
   * as a group. If not, resample eveything to the preferred rate.  (Using the
   * preferred rate should avoid redundant resampling later.)
   */
  actual_sample_rate_ = GetSampleRateInternal();
  if (actual_sample_rate_ == -1) {
    for (RageSoundReader*& pSound : loaded_sounds_) {
      RageSoundReader_Resample_Good* pResample =
          new RageSoundReader_Resample_Good(pSound, preferred_sample_rate_);
      pSound = pResample;
    }

    actual_sample_rate_ = preferred_sample_rate_;
  }

  /* Attempt to preload all sounds. */
  for (RageSoundReader*& pSound : loaded_sounds_) {
    RageSoundReader_Preload::PreloadSound(pSound);
  }

  /* Sort the sounds by start time. */
  sort(sounds_.begin(), sounds_.end());
}

int RageSoundReader_Chain::SetPosition(int iFrame) {
  /* Clear m_apActiveSounds. */
  while (!active_sounds_.empty()) {
    ReleaseSound(active_sounds_.front());
  }

  current_frame_ = iFrame;

  /* Run through all sounds in the chain, and activate all sounds which have
   * data at iFrame. */
  for (next_sound_ = 0; next_sound_ < sounds_.size(); ++next_sound_) {
    Sound* pSound = &sounds_[next_sound_];
    int iOffsetFrame = pSound->GetOffsetFrame(GetSampleRate());

    /* If this sound is in the future, skip it. */
    if (iOffsetFrame > iFrame) {
      break;
    }

    /* Find the RageSoundReader. */
    ActivateSound(pSound);
    RageSoundReader* pReader = pSound->sound_;

    int iOffsetFrames = iFrame - iOffsetFrame;
    if (pReader->SetPosition(iOffsetFrames) == 0) {
      /* We're past the end of this sound. */
      ReleaseSound(pSound);
      continue;
    }
  }

  /* If no sounds were started, and we have no sounds ahead of us, we've seeked
   * past EOF. */
  if (active_sounds_.empty() && next_sound_ == sounds_.size()) {
    return 0;
  }

  return 1;
}

void RageSoundReader_Chain::ActivateSound(Sound* s) {
  RageSoundReader* pSound = loaded_sounds_[s->index_];
  s->sound_ = pSound->Copy();

  /* Add a balance filter.  If this source has the same number of channels
   * as this sound, and does not need to be panned, we can omit this. */
  if (s->pan_ != 0.0f ||
      s->sound_->GetNumChannels() != this->GetNumChannels()) {
    s->sound_ = new RageSoundReader_Pan(s->sound_);
    s->sound_->SetProperty("Pan", s->pan_);
  }

  active_sounds_.push_back(s);
}

void RageSoundReader_Chain::ReleaseSound(Sound* s) {
  std::vector<Sound*>::iterator it =
      find(active_sounds_.begin(), active_sounds_.end(), s);
  ASSERT(it != active_sounds_.end());
  RageSoundReader*& pSound = s->sound_;

  delete pSound;
  pSound = nullptr;

  active_sounds_.erase(it);
}

bool RageSoundReader_Chain::SetProperty(
    const std::string& sProperty, float fValue) {
  bool bRet = false;
  for (unsigned i = 0; i < active_sounds_.size(); ++i) {
    if (active_sounds_[i]->sound_->SetProperty(sProperty, fValue)) {
      bRet = true;
    }
  }
  return bRet;
}

int RageSoundReader_Chain::GetNextSourceFrame() const {
  return current_frame_;
  /*	if( m_apActiveSounds.empty() )
                  return m_iCurrentFrame;

          int iPosition = m_apActiveSounds[0]->pSound->GetNextSourceFrame();
          iPosition += m_apActiveSounds[0]->GetOffsetFrame( GetSampleRate() );

          for( unsigned i = 1; i < m_apActiveSounds.size(); ++i )
          {
                  int iThisPosition =
     m_apActiveSounds[i]->pSound->GetNextSourceFrame(); iThisPosition +=
     m_apActiveSounds[i]->GetOffsetFrame( GetSampleRate() ); if( iThisPosition
     != iPosition ) LOG->Warn( "RageSoundReader_Chain: sound positions moving at
     different rates" );
          }

          return iPosition;
  */
}

float RageSoundReader_Chain::GetStreamToSourceRatio() const {
  if (active_sounds_.empty()) {
    return 1.0f;
  }

  float iRate = active_sounds_[0]->sound_->GetStreamToSourceRatio();
  for (unsigned i = 1; i < active_sounds_.size(); ++i) {
    if (active_sounds_[i]->sound_->GetStreamToSourceRatio() != iRate) {
      LOG->Warn("RageSoundReader_Chain: sound rates changing differently");
    }
  }

  return iRate;
}

/* As we iterate through the sound tree, we'll find that we need data from
 * different sounds; a sound may be needed by more than one other sound. */
int RageSoundReader_Chain::Read(float* pBuffer, int iFrames) {
  while (next_sound_ < sounds_.size() &&
         current_frame_ ==
             sounds_[next_sound_].GetOffsetFrame(actual_sample_rate_)) {
    Sound* pSound = &sounds_[next_sound_];
    ActivateSound(pSound);
    ++next_sound_;
  }

  if (next_sound_ == sounds_.size() && active_sounds_.empty()) {
    return END_OF_FILE;
  }

  /* Clamp iFrames to the beginning of the next sound we need to start. */
  if (next_sound_ < sounds_.size()) {
    int iOffsetFrame = sounds_[next_sound_].GetOffsetFrame(actual_sample_rate_);
    ASSERT_M(
        iOffsetFrame >= current_frame_,
        ssprintf("%i %i", iOffsetFrame, current_frame_));
    int iFramesToRead = iOffsetFrame - current_frame_;
    iFrames = std::min(iFramesToRead, iFrames);
  }

  if (active_sounds_.size() == 1 &&
      active_sounds_.front()->sound_->GetNumChannels() == channels_ &&
      active_sounds_.front()->sound_->GetSampleRate() == actual_sample_rate_) {
    /* We have only one source, and it matches our target.  Don't mix; read
     * directly from the source into the destination.  This is to optimize
     * the common case of having one BGM track and no autoplay sounds. */
    iFrames = active_sounds_.front()->sound_->Read(pBuffer, iFrames);
    if (iFrames < 0) {
      ReleaseSound(active_sounds_.front());
    }
    if (iFrames > 0) {
      current_frame_ += iFrames;
    }
    return iFrames;
  }

  // Note: this is only safe because we know pBuffer is not nullptr
  // and iFrames and m_iChannels are reasonable sizes.
  if (active_sounds_.empty()) {
    /* If we have more sounds ahead of us, pretend we read the entire block,
     * since there's silence in between.  Otherwise, we're at EOF. */
    size_t pBufferSize =
        static_cast<size_t>(iFrames) * channels_ * sizeof(float);
    memset(pBuffer, 0, pBufferSize);
    current_frame_ += iFrames;
    return iFrames;
  }

  RageSoundMixBuffer mix;
  /* Read iFrames from each sound. */
  float Buffer[2048];
  iFrames = std::min(iFrames, (int)(ARRAYLEN(Buffer) / channels_));
  for (unsigned i = 0; i < active_sounds_.size();) {
    RageSoundReader* pSound = active_sounds_[i]->sound_;
    ASSERT(
        pSound->GetNumChannels() ==
        channels_);  // guaranteed by ActivateSound and Finish

    /* If we receive less than we were asked for, keep asking for more data.
     * Most filters would simply return what they have in this situation, but we
     * want to deal transparently with separate sounds returning
     * differently-sized partial blocks. */
    int iFramesRead = 0;
    while (iFramesRead < iFrames) {
      int iGotFrames = pSound->RetriedRead(Buffer, iFrames - iFramesRead);
      if (iGotFrames < 0) {
        iFramesRead = iGotFrames;
        break;
      }

      mix.SetWriteOffset(iFramesRead * pSound->GetNumChannels());
      mix.write(Buffer, iGotFrames * pSound->GetNumChannels());
      iFramesRead += iGotFrames;
    }

    if (iFramesRead < 0) {
      ReleaseSound(active_sounds_[i]);
    } else {
      ++i;
    }
  }

  /* Read mixed frames into the output buffer. */
  int iMaxFramesRead = mix.size() / channels_;
  mix.read(pBuffer);
  current_frame_ += iMaxFramesRead;

  return iMaxFramesRead;
}

int RageSoundReader_Chain::GetLength() const {
  int iLength = 0;
  for (unsigned i = 0; i < sounds_.size(); ++i) {
    const Sound& sound = sounds_[i];
    const RageSoundReader* pSound = loaded_sounds_[sound.index_];
    int iThisLength = pSound->GetLength();
    if (iThisLength) {
      iLength = std::max(iLength, iThisLength + sound.offset_ms_);
    }
  }
  return iLength;
}

int RageSoundReader_Chain::GetLength_Fast() const {
  int iLength = 0;
  for (unsigned i = 0; i < sounds_.size(); ++i) {
    const Sound& sound = sounds_[i];
    const RageSoundReader* pSound = loaded_sounds_[sound.index_];
    int iThisLength = pSound->GetLength_Fast();
    if (iThisLength) {
      iLength = std::max(iLength, iThisLength + sound.offset_ms_);
    }
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
