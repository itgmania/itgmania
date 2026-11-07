/* RageSoundReader_Chain - Chain sounds together */

#ifndef RAGE_SOUND_READER_CHAIN
#define RAGE_SOUND_READER_CHAIN

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "RageSoundReader.h"

class RageSoundReader_Chain : public RageSoundReader {
 public:
  RageSoundReader_Chain();
  ~RageSoundReader_Chain();
  RageSoundReader_Chain* Copy() const;

  /* Set the preferred sample rate.  This will only be used if the source sounds
   * use different sample rates. */
  void SetPreferredSampleRate(int sample_rate) {
    preferred_sample_rate_ = sample_rate;
  }

  int LoadSound(std::string path);
  int LoadSound(RageSoundReader* sound);

  /* Add the given sound to play after fOffsetSecs seconds.  Takes ownership
   * of pSound. */
  void AddSound(int index, float offset_secs, float pan);

  /* Finish adding sounds. */
  void Finish();

  /* Return the number of added sounds. */
  int GetNumSounds() const { return sounds_.size(); }

  int GetLength() const;
  int GetLength_Fast() const;
  int SetPosition(int frame);
  int Read(float* buf, int frames);
  int GetSampleRate() const;
  unsigned GetNumChannels() const { return channels_; }
  bool SetProperty(const std::string& property, float value);
  int GetNextSourceFrame() const;
  float GetStreamToSourceRatio() const;
  std::string GetError() const { return ""; }

 private:
  int GetSampleRateInternal() const;

  int preferred_sample_rate_;
  int actual_sample_rate_;
  unsigned channels_;

  std::map<std::string, RageSoundReader*> named_sounds_;
  std::vector<RageSoundReader*> loaded_sounds_;

  struct Sound {
    int index_;  // into m_apLoadedSounds
    int offset_ms_;
    float pan_;
    RageSoundReader* sound_;  // nullptr if not activated

    int GetOffsetFrame(int sample_rate) const {
      return int(int64_t(offset_ms_) * sample_rate / 1000);
    }
    bool operator<(const Sound& rhs) const {
      return offset_ms_ < rhs.offset_ms_;
    }
  };
  std::vector<Sound> sounds_;

  /* Read state: */
  int current_frame_;
  unsigned next_sound_;
  std::vector<Sound*> active_sounds_;

  void ActivateSound(Sound* s);
  void ReleaseSound(Sound* s);
};

#endif

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
