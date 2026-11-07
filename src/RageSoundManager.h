/* RageSoundManager - A global singleton to interface RageSound and
 * RageSoundDriver. */

#ifndef RAGE_SOUND_MANAGER_H
#define RAGE_SOUND_MANAGER_H

#include <cstdint>
#include <map>
#include <string>

class RageSound;
class RageSoundBase;
class RageSoundDriver;
struct RageSoundParams;
class RageSoundReader;
class RageSoundReader_Preload;
class RageTimer;

class RageSoundManager {
 public:
  RageSoundManager();
  ~RageSoundManager();

  /* This may be called when shutting down, in order to stop all sounds.  This
   * should be called before shutting down threads that may have running sounds,
   * in order to prevent DirectSound delays and glitches.  Further attempts to
   * start sounds will do nothing, and threads may be shut down. */
  void Shutdown();

  void Init();

  void SetMixVolume();
  float GetVolumeOfNonCriticalSounds() const {
    return volume_of_non_critical_sounds_;
  }
  void SetVolumeOfNonCriticalSounds(float volume_of_non_critical_sounds);

  void Update();
  void StartMixing(RageSoundBase* snd);       /* used by RageSound */
  void StopMixing(RageSoundBase* snd);        /* used by RageSound */
  bool Pause(RageSoundBase* snd, bool pause); /* used by RageSound */
  bool GetPlayingPosition(
      const RageSoundBase* snd, int& source_frame,
      RageTimer* timer = nullptr); /* used by RageSound */
  float GetPlayLatency() const;
  int GetDriverSampleRate() const;

  RageSoundReader* GetLoadedSound(const std::string& path);
  void AddLoadedSound(const std::string& path, RageSoundReader_Preload* sound);

 private:
  std::map<std::string, RageSoundReader_Preload*> map_preloaded_sounds_;

  RageSoundDriver* driver_;

  /* Prefs: */
  float volume_of_non_critical_sounds_;
  // Swallow up warnings. If they must be used, define them.
  RageSoundManager& operator=(const RageSoundManager& rhs);
  RageSoundManager(const RageSoundManager& rhs);
};

extern RageSoundManager* SOUNDMAN;

#endif

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
