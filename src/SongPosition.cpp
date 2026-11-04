#include "SongPosition.h"

#include "LightsManager.h"
#include "Preference.h"
#include "RageTimer.h"
#include "RageUtil.h"
#include "TimingData.h"
#include "global.h"

static Preference<float> g_fVisualDelaySeconds("VisualDelaySeconds", 0.0f);

void SongPosition::UpdateSongPosition(
    float fPositionSeconds, const TimingData& timing,
    const RageTimer& timestamp, float fAdditionalVisualDelay) {
  if (!timestamp.IsZero()) {
    last_beat_update_ = timestamp;
  } else {
    last_beat_update_.Touch();
  }

  TimingData::GetBeatArgs beat_info;
  beat_info.elapsed_time_ = fPositionSeconds;
  timing.GetBeatAndBPSFromElapsedTime(beat_info);
  song_beat_ = beat_info.beat_;
  cur_bps_ = beat_info.bps_out_;
  freeze_ = beat_info.freeze_out_;
  delay_ = beat_info.delay_out_;
  warp_begin_row_ = beat_info.warp_begin_out_;
  warp_destination_ = beat_info.warp_dest_out_;

  // "Crash reason : -243478.890625 -48695.773438"
  // The question is why is -2000 used as the limit? -aj
  ASSERT_M(
      song_beat_ > -2000, ssprintf(
                              "Song beat %f at %f seconds is less than -2000!",
                              song_beat_, fPositionSeconds));

  music_seconds_ = fPositionSeconds;

  light_song_beat_ =
      timing.GetBeatFromElapsedTime(fPositionSeconds + g_fLightsAheadSeconds);

  song_beat_no_offset_ =
      timing.GetBeatFromElapsedTimeNoOffset(fPositionSeconds);

  music_seconds_visible_ =
      fPositionSeconds - g_fVisualDelaySeconds.Get() - fAdditionalVisualDelay;
  beat_info.elapsed_time_ = music_seconds_visible_;
  timing.GetBeatAndBPSFromElapsedTime(beat_info);
  song_beat_visible_ = beat_info.beat_;
}

// lua start
#include "LuaBinding.h"
class LunaSongPosition : public Luna<SongPosition> {
 public:
  DEFINE_METHOD(GetMusicSecondsVisible, music_seconds_visible_);
  DEFINE_METHOD(GetSongBeatVisible, song_beat_visible_);
  DEFINE_METHOD(GetMusicSeconds, music_seconds_);
  DEFINE_METHOD(GetSongBeat, song_beat_);
  DEFINE_METHOD(GetSongBeatNoOffset, song_beat_no_offset_);
  DEFINE_METHOD(GetCurBPS, cur_bps_);
  DEFINE_METHOD(GetFreeze, freeze_);
  DEFINE_METHOD(GetDelay, delay_);
  DEFINE_METHOD(GetWarpBeginRow, warp_begin_row_);
  DEFINE_METHOD(GetWarpDestination, warp_destination_);

  LunaSongPosition() {
    ADD_METHOD(GetMusicSecondsVisible);
    ADD_METHOD(GetSongBeatVisible);
    ADD_METHOD(GetMusicSeconds);
    ADD_METHOD(GetSongBeat);
    ADD_METHOD(GetSongBeatNoOffset);
    ADD_METHOD(GetCurBPS);
    ADD_METHOD(GetFreeze);
    ADD_METHOD(GetDelay);
    ADD_METHOD(GetWarpBeginRow);
    ADD_METHOD(GetWarpDestination);
  }
};

LUA_REGISTER_CLASS(SongPosition);
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
