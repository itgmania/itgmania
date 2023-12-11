#ifndef RAGE_SOUNDS_H
#define RAGE_SOUNDS_H

#include "PlayerNumber.h"
#include "RageSound.h"
#include "TimingData.h"

struct lua_State;

int MusicThread_start(void* p);

// High-level sound utilities.
class GameSoundManager {
 public:
  GameSoundManager();
  ~GameSoundManager();
  void Update(float fDeltaTime);

  struct PlayMusicParams {
    PlayMusicParams() {
      timing = nullptr;
      force_loop = false;
      start_second = 0;
      length_seconds = -1;
      fade_in_length_seconds = 0;
      fade_out_length_seconds = 0;
      align_beat = true;
      apply_music_rate = false;
    }

    RString file;
    const TimingData* timing;
    bool force_loop;
    float start_second;
    float length_seconds;
    float fade_in_length_seconds;
    float fade_out_length_seconds;
    bool align_beat;
    bool apply_music_rate;
  };
  void PlayMusic(
      PlayMusicParams params,
      PlayMusicParams fallback_music_params = PlayMusicParams());
  void PlayMusic(
      RString file, const TimingData* timing = nullptr, bool force_loop = false,
      float start_seconds = 0, float length_seconds = -1,
      float fade_in_length_seconds = 0, float fade_out_length_seconds = 0,
      bool align_beat = true, bool apply_music_rate = false);
  void StopMusic() { PlayMusic(""); }
  void DimMusic(float volume, float duration_seconds);
  RString GetMusicPath() const;
  void Flush();

  void PlayOnce(RString path);
  void PlayOnceFromDir(RString dir);
  void PlayOnceFromAnnouncer(RString folder_name);

  void HandleSongTimer(bool on = true);
  float GetFrameTimingAdjustment(float delta);

  static float GetPlayerBalance(PlayerNumber pn);

  // Lua
  void PushSelf(lua_State* L);
};

extern GameSoundManager* SOUND;

#endif  // RAGE_SOUNDS_H

/*
 * Copyright (c) 2003-2004 Glenn Maynard
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
