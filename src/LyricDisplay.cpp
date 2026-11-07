#include "LyricDisplay.h"

#include <algorithm>

#include "ActorFrame.h"
#include "ActorUtil.h"
#include "GameState.h"
#include "LuaManager.h"
#include "LuaReference.h"
#include "Song.h"
#include "ThemeManager.h"
#include "ThemeMetric.h"

static ThemeMetric<float> IN_LENGTH("LyricDisplay", "InLength");
static ThemeMetric<float> OUT_LENGTH("LyricDisplay", "OutLength");

LyricDisplay::LyricDisplay() {
  text_lyrics_[0].SetName("LyricBack");
  ActorUtil::LoadAllCommands(text_lyrics_[0], "LyricDisplay");
  text_lyrics_[0].LoadFromFont(THEME->GetPathF("LyricDisplay", "text"));
  this->AddChild(&text_lyrics_[0]);

  text_lyrics_[1].SetName("LyricFront");
  ActorUtil::LoadAllCommands(text_lyrics_[1], "LyricDisplay");
  text_lyrics_[1].LoadFromFont(THEME->GetPathF("LyricDisplay", "text"));
  this->AddChild(&text_lyrics_[1]);

  Init();
}

void LyricDisplay::Init() {
  for (int i = 0; i < 2; i++) {
    text_lyrics_[i].SetText("");
  }
  cur_lyric_number_ = 0;

  last_second_ = -500;
  stopped_ = false;
}

void LyricDisplay::Stop() { stopped_ = true; }

void LyricDisplay::Update(float delta_time) {
  if (stopped_) {
    return;
  }

  ActorFrame::Update(delta_time);

  if (GAMESTATE->cur_song_ == nullptr) {
    return;
  }

  // If the song has changed (in a course), reset.
  if (GAMESTATE->position_.music_seconds_ < last_second_) {
    Init();
  }
  last_second_ = GAMESTATE->position_.music_seconds_;

  if (cur_lyric_number_ >= GAMESTATE->cur_song_->lyric_segments_.size()) {
    return;
  }

  const Song* song = GAMESTATE->cur_song_;
  const float start_time =
      (song->lyric_segments_[cur_lyric_number_].start_time_) -
      IN_LENGTH.GetValue();

  if (GAMESTATE->position_.music_seconds_ < start_time) {
    return;
  }

  // Clamp this lyric to the beginning of the next or the end of the music.
  float end_time;
  if (cur_lyric_number_ + 1 < GAMESTATE->cur_song_->lyric_segments_.size()) {
    end_time = song->lyric_segments_[cur_lyric_number_ + 1].start_time_;
  } else {
    end_time = song->GetLastSecond();
  }

  const float distance =
      end_time - song->lyric_segments_[cur_lyric_number_].start_time_;
  const float tween_buffer_time = IN_LENGTH.GetValue() + OUT_LENGTH.GetValue();

  /* If it's negative, two lyrics are so close together that there's no time
   * to tween properly. Lyrics should never be this brief, anyway, so just
   * skip it. */
  float show_length = std::max(distance - tween_buffer_time, 0.0f);

  // Make lyrics show faster for faster song rates.
  show_length /= GAMESTATE->song_options_.GetCurrent().music_rate_;

  const LyricSegment& seg =
      GAMESTATE->cur_song_->lyric_segments_[cur_lyric_number_];

  LuaThreadVariable var1("LyricText", seg.lyric_);
  LuaThreadVariable var2("LyricDuration", LuaReference::Create(show_length));
  LuaThreadVariable var3("LyricColor", LuaReference::Create(seg.color_));

  PlayCommand("Changed");

  cur_lyric_number_++;
}

/*
 * (c) 2003-2004 Kevin Slaughter, Glenn Maynard
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
