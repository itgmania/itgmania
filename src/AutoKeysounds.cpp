/*
 * This class handles two things: auto-play preload, and runtime auto-play
 * sounds.
 *
 * On song start, all autoplay sounds and the main BGM track (if any) are
 * combined into a single chain, which is used as the song background.  Sounds
 * added this way are removed from the NoteData.
 *
 * Any sounds not added to the sound chain and any autoplay sounds added to the
 * NoteData during play (usually due to battle mode mods) are played
 * dynamically, via Update().
 *
 * Note that autoplay sounds which are played before the BGM starts will never
 * be placed in the sound chain, since the sound chain becomes the BGM; the BGM
 * can't play sound before it starts.  These sounds will be left in the
 * NoteData, and played as dynamic sounds; this means that they don't get robust
 * sync.  This isn't a problem for imported BMS files, which don't have an
 * offset value, but it's annoying.
 */

#include "AutoKeysounds.h"

#include <algorithm>
#include <climits>
#include <string>

#include "GameState.h"
#include "NoteData.h"
#include "NoteTypes.h"
#include "PlayerNumber.h"
#include "RageLog.h"
#include "RageSoundManager.h"
#include "RageSoundReader_Chain.h"
#include "RageSoundReader_Extend.h"
#include "RageSoundReader_FileReader.h"
#include "RageSoundReader_Pan.h"
#include "RageSoundReader_PitchChange.h"
#include "RageSoundReader_PostBuffering.h"
#include "RageSoundReader_ThreadedBuffer.h"
#include "RageUtil.h"
#include "Song.h"
#include "global.h"

void AutoKeysounds::Load(PlayerNumber pn, const NoteData& auto_keysounds_only) {
  auto_keysounds_only_[pn] = auto_keysounds_only;
}

void AutoKeysounds::LoadAutoplaySoundsInto(RageSoundReader_Chain* chain) {
  //
  // Load sounds.
  //
  Song* song = GAMESTATE->cur_song_;
  std::string song_dir = song->GetSongDir();

  /*
   * Add all current autoplay sounds in both players to the chain.
   */
  int num_tracks =
      auto_keysounds_only_[GAMESTATE->GetMasterPlayerNumber()].GetNumTracks();
  for (int t = 0; t < num_tracks; t++) {
    int row = -1;
    for (;;) {
      /* Find the next row that either player has a note on. */
      int next_row = INT_MAX;
      FOREACH_EnabledPlayer(pn) {
        // XXX Hack. Enabled players need not have their own note data.
        if (t >= auto_keysounds_only_[pn].GetNumTracks()) {
          continue;
        }
        int next_row_for_player = row;
        /* XXX: If a BMS file only has one tap note per track,
         * this will prevent any keysounds from loading.
         * This leads to failure later on.
         * We need a better way to prevent this. */
        if (auto_keysounds_only_[pn].GetNextTapNoteRowForTrack(
                t, next_row_for_player)) {
          next_row = std::min(next_row, next_row_for_player);
        }
      }

      if (next_row == INT_MAX) {
        break;
      }
      row = next_row;

      TapNote tn[kNumPlayers];
      FOREACH_EnabledPlayer(pn) tn[pn] =
          auto_keysounds_only_[pn].GetTapNote(t, row);

      FOREACH_EnabledPlayer(pn) {
        if (tn[pn] == tap_empty) {
          continue;
        }

        ASSERT(tn[pn].type_ == TapNoteType_AutoKeysound);
        if (tn[pn].keysound_index_ >= 0) {
          std::string keysound_file_path =
              song_dir + song->keysound_file_[tn[pn].keysound_index_];
          float seconds =
              GAMESTATE->cur_steps_[pn]
                  ->GetTimingData()
                  ->GetElapsedTimeFromBeatNoOffset(NoteRowToBeat(row)) +
              SOUNDMAN->GetPlayLatency();

          float pan = 0;
          // If two players are playing, pan the keysounds to each player's
          // respective side
          if (GAMESTATE->GetNumPlayersEnabled() == 2) {
            pan = (pn == PLAYER_1) ? -1.0f : +1.0f;
          }
          int index = chain->LoadSound(keysound_file_path);
          chain->AddSound(index, seconds, pan);
        }
      }
    }
  }
}

void AutoKeysounds::LoadTracks(RageSoundReader*& shared) {
  shared = nullptr;

  const std::string music_path =
      GAMESTATE->cur_steps_[GAMESTATE->GetMasterPlayerNumber()]->GetMusicPath();
  if (music_path.empty()) {
    return;
  }

  std::string error;
  RageSoundReader* song_reader =
      RageSoundReader_FileReader::OpenFile(music_path, error);
  if (song_reader == nullptr) {
    LOG->Warn(
        "Couldn't open music file %s: %s", music_path.c_str(), error.c_str());
    return;
  }

  // Load the buffering filter before the effects filters, so effects aren't
  // delayed.
  song_reader = new RageSoundReader_Extend(song_reader);
  song_reader = new RageSoundReader_ThreadedBuffer(song_reader);
  shared = song_reader;
}

void AutoKeysounds::FinishLoading() {
  sound_.Unload();

  Song* song = GAMESTATE->cur_song_;

  LoadTracks(shared_sound_);

  // Load autoplay sounds, if any.
  {
    RageSoundReader_Chain* chain = new RageSoundReader_Chain;
    chain->SetPreferredSampleRate(SOUNDMAN->GetDriverSampleRate());
    LoadAutoplaySoundsInto(chain);

    if (chain->GetNumSounds() > 0 || !shared_sound_) {
      if (shared_sound_) {
        int index = chain->LoadSound(shared_sound_);
        chain->AddSound(index, 0.0f, 0);
      }
      chain->Finish();
      shared_sound_ = new RageSoundReader_Extend(chain);
    } else {
      delete chain;
    }
  }
  ASSERT_M(
      shared_sound_ != nullptr, ssprintf(
                                    "No keysounds were loaded for the song %s!",
                                    song->main_title_.c_str()));

  shared_sound_ = new RageSoundReader_PitchChange(shared_sound_);
  shared_sound_ = new RageSoundReader_PostBuffering(shared_sound_);
  shared_sound_ = new RageSoundReader_Pan(shared_sound_);

  sound_.LoadSoundReader(shared_sound_);
}

/*
 * (c) 2004 Chris Danford, Glenn Maynard
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
