#include "global.h"

#include "GameplayAssist.h"

#include "CommonMetrics.h"
#include "GameState.h"
#include "NoteData.h"
#include "RageSoundManager.h"
#include "Song.h"
#include "ThemeManager.h"

void GameplayAssist::Init() {
  sound_assist_clap_.Load(THEME->GetPathS("GameplayAssist", "clap"), true);
  sound_assist_metronome_measure_.Load(
      THEME->GetPathS("GameplayAssist", "metronome measure"), true);
  sound_assist_metronome_beat_.Load(
      THEME->GetPathS("GameplayAssist", "metronome beat"), true);
}

void GameplayAssist::PlayTicks(
    const NoteData& note_data, const PlayerState* player_state) {
  bool clap = GAMESTATE->song_options_.GetCurrent().m_bAssistClap;
  bool metronome = GAMESTATE->song_options_.GetCurrent().m_bAssistMetronome;
  if (!clap && !metronome) {
    return;
  }

  // don't play sounds for dead players
  if (player_state->m_HealthState == HealthState_Dead) {
    return;
  }

  // Sound cards have a latency between when a sample is Play()ed and when the
  // sound will start coming out the speaker. Compensate for this by boosting
  // position_seconds ahead. This is just to make sure that we request the sound
  // early enough for it to come out on time; the actual precise timing is
  // handled by SetStartTime.
  SongPosition& position =
      GAMESTATE->player_state_[player_state->m_PlayerNumber]->m_Position;
  float position_seconds = position.m_fMusicSeconds;
  position_seconds += SOUNDMAN->GetPlayLatency() +
                      (float)CommonMetrics::TICK_EARLY_SECONDS + 0.250f;
  const TimingData& timing =
      *GAMESTATE->cur_steps_[player_state->m_PlayerNumber]->GetTimingData();
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
        note_data, row, row_last_crossed + 1, song_row + 1)
    if (note_data.IsThereATapOrHoldHeadAtRow(row)) {
      clap_row = row;
    }

    if (clap_row != -1 && timing.IsJudgableAtRow(clap_row)) {
      const float tick_beat = NoteRowToBeat(clap_row);
      const float tick_second =
          timing.GetElapsedTimeFromBeatNoOffset(tick_beat);
      float fSecondsUntil = tick_second - position.m_fMusicSeconds;
			// 2x music rate means the time until the tick is halved.
      fSecondsUntil /= GAMESTATE->song_options_.GetCurrent().m_fMusicRate;

      RageSoundParams rage_sound_params;
      rage_sound_params.m_StartTime =
          position.m_LastBeatUpdate +
          (fSecondsUntil - (float)CommonMetrics::TICK_EARLY_SECONDS);
      sound_assist_clap_.Play(false, &rage_sound_params);
    }
  }

  if (metronome && row_last_crossed != -1) {
    // row_last_crossed+1, song_row+1

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
      float fSecondsUntil = tick_second - position.m_fMusicSeconds;
      // 2x music rate means the time until the tick is halved.
      fSecondsUntil /= GAMESTATE->song_options_.GetCurrent().m_fMusicRate;

      RageSoundParams rage_sound_params;
      rage_sound_params.m_StartTime =
          position.m_LastBeatUpdate +
          (fSecondsUntil - (float)CommonMetrics::TICK_EARLY_SECONDS);
      if (is_measure) {
        sound_assist_metronome_measure_.Play(false, &rage_sound_params);
      } else {
        sound_assist_metronome_beat_.Play(false, &rage_sound_params);
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
