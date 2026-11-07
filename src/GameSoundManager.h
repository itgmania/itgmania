#ifndef RAGE_SOUNDS_H
#define RAGE_SOUNDS_H

#include <string>

#include "PlayerNumber.h"

class TimingData;
class RageSound;
struct lua_State;

int MusicThread_start(void* p);

/** @brief High-level sound utilities. */
class GameSoundManager {
 public:
  GameSoundManager();
  ~GameSoundManager();
  void Update(float delta_time);

  struct PlayMusicParams {
    PlayMusicParams() {
      timing_ = nullptr;
      force_loop_ = false;
      start_second_ = 0;
      length_seconds_ = -1;
      fade_in_length_seconds_ = 0;
      fade_out_length_seconds_ = 0;
      align_beat_ = true;
      apply_music_rate_ = false;
    }

    std::string file_;
    const TimingData* timing_;
    bool force_loop_;
    float start_second_;
    float length_seconds_;
    float fade_in_length_seconds_;
    float fade_out_length_seconds_;
    bool align_beat_;
    bool apply_music_rate_;
  };
  void PlayMusic(
      PlayMusicParams params,
      PlayMusicParams fallback_music_params = PlayMusicParams());
  void PlayMusic(
      std::string file, const TimingData* timing = nullptr,
      bool force_loop = false, float start_sec = 0, float length_sec = -1,
      float fade_in_length_seconds = 0, float fade_len = 0,
      bool align_beat = true, bool apply_music_rate = false);
  void StopMusic() { PlayMusic(""); }
  void DimMusic(float volume, float duration_seconds);
  std::string GetMusicPath() const;
  void Flush();

  void PlayOnce(std::string path);
  void PlayOnceFromDir(std::string dir);
  void PlayOnceFromAnnouncer(std::string folder_name);

  void HandleSongTimer(bool on = true);

  static float GetPlayerBalance(PlayerNumber pn);

  // Lua
  void PushSelf(lua_State* L);
};

extern GameSoundManager* SOUND;
#endif

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
