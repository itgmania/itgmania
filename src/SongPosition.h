#ifndef SONGPOSITION_H
#define SONGPOSITION_H

#include "RageTimer.h"
#include "TimingData.h"
struct lua_State;

class SongPosition {
 public:
  SongPosition() { SongPosition::Reset(); }

  // Represents the time into the current song, not scaled by the music rate.
  float music_seconds_;
  // Represents the current beat of the song.
  float song_beat_;
  // Same as above, but without any offset.
  float song_beat_no_offset_;
  // Represents the current beats per second (BPS) of the song.
  float cur_bps_;
  // The sum of the song beat (for lighting effects) and
  // g_fLightsFalloffSeconds.
  float light_song_beat_;
  // A flag to determine if the song is in the middle of a freeze or stop.
  bool freeze_;
  // A flag to determine if the song is in the middle of a delay (Pump style
  // stop).
  bool delay_;
  // The row used to start a warp.
  int warp_begin_row_;
  // The beat to warp to after a warp begins.
  float warp_destination_;
  // The time of the last update to m_fSongBeat and related variables.
  RageTimer last_beat_update_;
  // Represents the visible time into the current song.
  float music_seconds_visible_;
  // Represents the visible beat of the song.
  float song_beat_visible_;
  //// A flag to determine if the song is in the middle of a stop (freeze or
  /// delay). / bool m_bStop;

  void Reset() {
    music_seconds_ = 0;  // MUSIC_SECONDS_INVALID;
    song_beat_ = 0;
    song_beat_no_offset_ = 0;
    cur_bps_ = 10;
    light_song_beat_ = 0;  // Assuming initial value
    freeze_ = false;
    delay_ = false;
    // m_bStop = false;
    warp_begin_row_ =
        -1;  // Set to -1 because some song may want to warp to row 0. -aj
    warp_destination_ =
        -1;  // Set when a warp is encountered. also see above. -aj
    music_seconds_visible_ = 0;
    song_beat_visible_ = 0;
  }

  void UpdateSongPosition(
      float fPositionSeconds, const TimingData& timing,
      const RageTimer& timestamp = RageZeroTimer,
      float fAdditionalVisualDelay = 0.0f);

  // Lua
  void PushSelf(lua_State* L);
};

#endif  // SONGPOSITION_H

/**
 * @file
 * @author Thai Pangsakulyanont (c) 2011
 * @section LICENSE
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
