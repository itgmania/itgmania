// This class handles two things: auto-play preload, and runtime auto-play
// sounds.
//
// On song start, all autoplay sounds and the main BGM track (if any) are
// combined into a single chain, which is used as the song background. Sounds
// added this way are removed from the NoteData.
//
// Any sounds not added to the sound chain and any autoplay sounds added to the
// NoteData during play (usually due to battle mode mods) are played
// dynamically, via Update().
//
// Note that autoplay sounds which are played before the BGM starts will never
// be placed in the sound chain, since the sound chain becomes the BGM; the BGM
// can't play sound before it starts. These sounds will be left in the
// NoteData, and played as dynamic sounds; this means that they don't get robust
// sync. This isn't a problem for imported BMS files, which don't have an
// offset value, but it's annoying.
#include "global.h"

#include "AutoKeysounds.h"

#include <vector>

#include "GameState.h"
#include "RageLog.h"
#include "RageSoundManager.h"
#include "RageSoundReader_Chain.h"
#include "RageSoundReader_ChannelSplit.h"
#include "RageSoundReader_Extend.h"
#include "RageSoundReader_FileReader.h"
#include "RageSoundReader_Merge.h"
#include "RageSoundReader_Pan.h"
#include "RageSoundReader_PitchChange.h"
#include "RageSoundReader_PostBuffering.h"
#include "RageSoundReader_ThreadedBuffer.h"
#include "Song.h"

void AutoKeysounds::Load(
    PlayerNumber pn, const NoteData& auto_keysound_note_data) {
  auto_keysound_note_datas_[pn] = auto_keysound_note_data;
}

void AutoKeysounds::LoadAutoplaySoundsInto(RageSoundReader_Chain* chain) {
  // Load sounds.
  Song* song = GAMESTATE->cur_song_;
  RString song_dir = song->GetSongDir();

  // Add all current autoplay sounds in both players to the chain.
  int num_tracks = auto_keysound_note_datas_[GAMESTATE->GetMasterPlayerNumber()]
                       .GetNumTracks();
  for (int track = 0; track < num_tracks; ++track) {
    int row = -1;
    while (true) {
      // Find the next row that either player has a note on.
      int next_row = INT_MAX;
      FOREACH_EnabledPlayer(pn) {
        // XXX Hack. Enabled players need not have their own note data.
        if (track >= auto_keysound_note_datas_[pn].GetNumTracks()) {
          continue;
        }
        int next_row_for_player = row;
        // If a BMS file only has one tap note per track, this will prevent any
        // keysounds from loading. This leads to failure later on.
        // TODO: We need a better way to prevent this.
        if (auto_keysound_note_datas_[pn].GetNextTapNoteRowForTrack(
                track, next_row_for_player)) {
          next_row = std::min(next_row, next_row_for_player);
        }
      }

      if (next_row == INT_MAX) {
        break;
      }
      row = next_row;

      TapNote tap_notes[NUM_PLAYERS];
      FOREACH_EnabledPlayer(pn) {
        tap_notes[pn] = auto_keysound_note_datas_[pn].GetTapNote(track, row);
      }

      FOREACH_EnabledPlayer(pn) {
        if (tap_notes[pn] == TAP_EMPTY) {
          continue;
        }

        ASSERT(tap_notes[pn].type == TapNoteType_AutoKeysound);
        if (tap_notes[pn].iKeysoundIndex >= 0) {
          RString keysound_file_path =
              song_dir + song->m_vsKeysoundFile[tap_notes[pn].iKeysoundIndex];
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

void AutoKeysounds::LoadTracks(
    const Song* song, RageSoundReader*& shared, RageSoundReader*& player1,
    RageSoundReader*& player2) {
  shared = nullptr;
  player1 = nullptr;
  // NOTE(teejusb): player2 is seemingly unused and just gets set to nullptr.
  player2 = nullptr;

  std::vector<RString> music_file;
  const RString music_path =
      GAMESTATE->cur_steps_[GAMESTATE->GetMasterPlayerNumber()]
          ->GetMusicPath();

  if (!music_path.empty()) {
    music_file.push_back(music_path);
  }

  FOREACH_ENUM(InstrumentTrack, it) {
    if (it == InstrumentTrack_Guitar) {
      continue;
    }
    if (song->HasInstrumentTrack(it)) {
      music_file.push_back(song->GetInstrumentTrackPath(it));
    }
  }

  std::vector<RageSoundReader*> sounds;
  for (const RString& s : music_file) {
    RString error;
    RageSoundReader* sound_reader =
        RageSoundReader_FileReader::OpenFile(s, error);
    sounds.push_back(sound_reader);
  }

  if (sounds.size() == 1) {
    RageSoundReader* sound_reader = sounds[0];

    // Load the buffering filter before the effects filters, so effects aren't
    // delayed.
    sound_reader = new RageSoundReader_Extend(sound_reader);
    sound_reader = new RageSoundReader_ThreadedBuffer(sound_reader);
    shared = sound_reader;
  } else if (!sounds.empty()) {
    RageSoundReader_Merge* merged = new RageSoundReader_Merge;

    for (RageSoundReader* so : sounds) {
      merged->AddSound(so);
    }
    merged->Finish(SOUNDMAN->GetDriverSampleRate());

    RageSoundReader* sound_reader = merged;

    // Load the buffering filter before the effects filters, so effects aren't
    // delayed.
    sound_reader = new RageSoundReader_Extend(sound_reader);
    sound_reader = new RageSoundReader_ThreadedBuffer(sound_reader);
    shared = sound_reader;
  }

  if (song->HasInstrumentTrack(InstrumentTrack_Guitar)) {
    RString error;
    RageSoundReader* guitar_track_reader = RageSoundReader_FileReader::OpenFile(
        song->GetInstrumentTrackPath(InstrumentTrack_Guitar), error);
    // Load the buffering filter before the effects filters, so effects aren't
    // delayed.
    guitar_track_reader = new RageSoundReader_Extend(guitar_track_reader);
    guitar_track_reader =
        new RageSoundReader_ThreadedBuffer(guitar_track_reader);
    player1 = guitar_track_reader;
  }

  return;
}

void AutoKeysounds::FinishLoading() {
  sound_.Unload();

  Song* song = GAMESTATE->cur_song_;

  std::vector<RageSoundReader*> sounds;
  LoadTracks(song, shared_sound_, player_sounds_[0], player_sounds_[1]);

  // Load autoplay sounds, if any.
  {
    RageSoundReader_Chain* chain = new RageSoundReader_Chain;
    chain->SetPreferredSampleRate(SOUNDMAN->GetDriverSampleRate());
    LoadAutoplaySoundsInto(chain);

    if (chain->GetNumSounds() > 0 || !shared_sound_) {
      if (shared_sound_) {
        int iIndex = chain->LoadSound(shared_sound_);
        chain->AddSound(iIndex, 0.0f, 0);
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
                                    song->m_sMainTitle.c_str()));

  shared_sound_ = new RageSoundReader_PitchChange(shared_sound_);
  shared_sound_ = new RageSoundReader_PostBuffering(shared_sound_);
  shared_sound_ = new RageSoundReader_Pan(shared_sound_);
  sounds.push_back(shared_sound_);

  if (player_sounds_[0] != nullptr) {
    player_sounds_[0] = new RageSoundReader_PitchChange(player_sounds_[0]);
    player_sounds_[0] = new RageSoundReader_PostBuffering(player_sounds_[0]);
    player_sounds_[0] = new RageSoundReader_Pan(player_sounds_[0]);
    sounds.push_back(player_sounds_[0]);
  }

  if (player_sounds_[1] != nullptr) {
    player_sounds_[1] = new RageSoundReader_PitchChange(player_sounds_[1]);
    player_sounds_[1] = new RageSoundReader_PostBuffering(player_sounds_[1]);
    player_sounds_[1] = new RageSoundReader_Pan(player_sounds_[1]);
    sounds.push_back(player_sounds_[1]);
  }

  if (GAMESTATE->GetNumPlayersEnabled() == 1 &&
      GAMESTATE->GetMasterPlayerNumber() == PLAYER_2)
    std::swap(player_sounds_[PLAYER_1], player_sounds_[PLAYER_2]);

  if (sounds.size() > 1) {
    RageSoundReader_Merge* merged = new RageSoundReader_Merge;

    for (RageSoundReader* ps : sounds) {
      merged->AddSound(ps);
    }

    merged->Finish(SOUNDMAN->GetDriverSampleRate());

    chain_ = merged;
  } else {
    ASSERT(!sounds.empty());
    chain_ = sounds[0];
  }

  sound_.LoadSoundReader(chain_);
}

void AutoKeysounds::Update(float delta) {
  // NOTE(teejusb): There used to be commented out code to address keysounds for
  // crossed rows.
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
