#include "GameplayAssist.h"

#include <algorithm>

#include "CommonMetrics.h"
#include "GameState.h"
#include "NoteData.h"
#include "NoteTypes.h"
#include "RageSoundManager.h"
#include "SongPosition.h"
#include "Steps.h"
#include "ThemeManager.h"

void GameplayAssist::Init() {
  sound_assist_clap_.Load(THEME->GetPathS("GameplayAssist", "clap"), true);
  sound_assist_metronome_measure_.Load(
      THEME->GetPathS("GameplayAssist", "metronome measure"), true);
  sound_assist_metronome_beat_.Load(
      THEME->GetPathS("GameplayAssist", "metronome beat"), true);
}

void GameplayAssist::PlayTicks(const NoteData& nd, const PlayerState* ps) {
  bool clap = GAMESTATE->song_options_.GetCurrent().assist_clap_;
  bool metronome = GAMESTATE->song_options_.GetCurrent().assist_metronome_;
  if (!clap && !metronome) {
    return;
  }

  /* Sound cards have a latency between when a sample is Play()ed and when the
   * sound will start coming out the speaker.  Compensate for this by boosting
   * fPositionSeconds ahead.  This is just to make sure that we request the
   * sound early enough for it to come out on time; the actual precise timing is
   * handled by SetStartTime. */
  SongPosition& position =
      GAMESTATE->player_state_[ps->player_number_]->position_;
  float position_seconds = position.music_seconds_;

  // float fPositionSeconds = GAMESTATE->m_Position.m_fMusicSeconds;
  position_seconds += SOUNDMAN->GetPlayLatency() +
                      (float)CommonMetrics::TICK_EARLY_SECONDS + 0.250f;
  const TimingData& timing =
      *GAMESTATE->cur_steps_[ps->player_number_]->GetTimingData();
  const float song_beat =
      timing.GetBeatFromElapsedTimeNoOffset(position_seconds);

  const int song_row = std::max(0, BeatToNoteRowNotRounded(song_beat));
  static int row_last_crossed = -1;
  if (song_row < row_last_crossed) {
    row_last_crossed = song_row;
  }

  if (clap) {
    int clap_row = -1;
    // for each index we crossed since the last update:
    FOREACH_NONEMPTY_ROW_ALL_TRACKS_RANGE(
        nd, r, row_last_crossed + 1, song_row + 1) {
      if (nd.IsThereATapOrHoldHeadAtRow(r)) {
        clap_row = r;
        break;
      }
    }

    if (clap_row != -1 && timing.IsJudgableAtRow(clap_row)) {
      const float tick_beat = NoteRowToBeat(clap_row);
      const float tick_second =
          timing.GetElapsedTimeFromBeatNoOffset(tick_beat);
      float seconds_until = tick_second - position.music_seconds_;
      seconds_until /= GAMESTATE->song_options_.GetCurrent()
                           .music_rate_; /* 2x music rate means the time until
                                             the tick is halved */

      RageSoundParams p;
      p.start_time_ =
          position.last_beat_update_ +
          (seconds_until - (float)CommonMetrics::TICK_EARLY_SECONDS);
      sound_assist_clap_.Play(false, &p);
    }
  }

  if (metronome && row_last_crossed != -1) {
    // iRowLastCrossed+1, iSongRow+1

    int last_crossed_measure_index;
    int last_crossed_beat_index;
    int last_crossed_rows_remainder;
    timing.NoteRowToMeasureAndBeat(
        row_last_crossed, last_crossed_measure_index, last_crossed_beat_index,
        last_crossed_rows_remainder);

    int current_measure_index;
    int current_beat_index;
    int current_rows_remainder;
    timing.NoteRowToMeasureAndBeat(
        song_row, current_measure_index, current_beat_index,
        current_rows_remainder);

    int metronome_row = -1;
    bool is_measure = false;

    if (last_crossed_measure_index != current_measure_index ||
        last_crossed_beat_index != current_beat_index) {
      metronome_row = song_row - current_rows_remainder;
      is_measure = current_beat_index == 0 && current_rows_remainder == 0;
    }

    if (metronome_row != -1) {
      const float tick_beat = NoteRowToBeat(metronome_row);
      const float tick_second =
          timing.GetElapsedTimeFromBeatNoOffset(tick_beat);
      float seconds_until = tick_second - position.music_seconds_;
      seconds_until /= GAMESTATE->song_options_.GetCurrent()
                           .music_rate_; /* 2x music rate means the time until
                                             the tick is halved */

      RageSoundParams p;
      p.start_time_ =
          position.last_beat_update_ +
          (seconds_until - (float)CommonMetrics::TICK_EARLY_SECONDS);
      if (is_measure) {
        sound_assist_metronome_measure_.Play(false, &p);
      } else {
        sound_assist_metronome_beat_.Play(false, &p);
      }
    }
  }

  row_last_crossed = song_row;
}

void GameplayAssist::StopPlaying() {
  sound_assist_clap_.StopPlaying();
  sound_assist_metronome_measure_.StopPlaying();
  sound_assist_metronome_beat_.StopPlaying();
}

/*
 * (c) 2003-2006 Chris Danford
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
