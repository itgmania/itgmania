#ifndef GameplayAssist_H
#define GameplayAssist_H

#include "NoteData.h"
#include "PlayerState.h"
#include "RageSound.h"

// The handclaps and metronomes ready to assist the player.
class GameplayAssist {
 public:
  // Load the sounds.
  void Init();
  // Play the sounds in question for the particular chart.
  void PlayTicks(const NoteData& note_data, const PlayerState* player_state);
  // Stop playing the sounds.
  void StopPlaying();

 private:
  // The sound made when a note is to be hit.
  RageSound sound_assist_clap_;
  // The sound made when crossing a new measure.
  RageSound sound_assist_metronome_measure_;
  // The sound made when crossing a new beat.
  RageSound sound_assist_metronome_beat_;
};

#endif  // GameplayAssist_H

/**
 * @file
 * @author Chris Danford (c) 2003-2006
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
