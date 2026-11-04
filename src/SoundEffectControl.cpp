#include "SoundEffectControl.h"

#include <string>

#include "GameConstantsAndTypes.h"
#include "GameInput.h"
#include "GameState.h"
#include "InputMapper.h"
#include "NoteData.h"
#include "NoteTypes.h"
#include "PlayerState.h"
#include "RageSoundReader.h"
#include "RageUtil.h"

SoundEffectControl::SoundEffectControl() {
  locked_ = false;
  sample_ = 0.0f;
  last_level_ = 0.0f;
  player_state_ = nullptr;
  note_data_ = nullptr;
}

void SoundEffectControl::Load(
    const std::string& sType, PlayerState* pPlayerState,
    const NoteData* pNoteData) {
  SOUND_PROPERTY.Load(sType, "SoundProperty");
  LOCK_TO_HOLD.Load(sType, "LockToHold");
  PROPERTY_MIN.Load(sType, "PropertyMin");
  PROPERTY_CENTER.Load(sType, "PropertyCenter");
  PROPERTY_MAX.Load(sType, "PropertyMax");

  player_state_ = pPlayerState;
  note_data_ = pNoteData;
}

void SoundEffectControl::SetSoundReader(RageSoundReader* pPlayer) {
  sound_reader_ = pPlayer;
}

void SoundEffectControl::Update(float fDeltaTime) {
  if (SOUND_PROPERTY == "") {
    return;
  }

  float fLevel = INPUTMAPPER->GetLevel(
      GAME_BUTTON_EFFECT_UP, player_state_->player_number_);
  fLevel -= INPUTMAPPER->GetLevel(
      GAME_BUTTON_EFFECT_DOWN, player_state_->player_number_);
  rage_clamp(fLevel, -1.0f, +1.0f);

  if (LOCK_TO_HOLD) {
    int iRow = BeatToNoteRow(GAMESTATE->position_.song_beat_);
    int iHoldsHeld, iHoldsLetGo;
    HoldsBeingHeld(iRow, iHoldsHeld, iHoldsLetGo);

    /* If no holds are being held, or any have been missed, lock the effect off
     * until the button has been released. */
    if (iHoldsLetGo > 0 || iHoldsHeld == 0) {
      locked_ = true;
    }

    /* If the button is released, unlock it when the level crosses or reaches 0.
     */
    if (locked_) {
      if ((fLevel <= 0.0f && last_level_ >= 0.0f) ||
          (fLevel >= 0.0f && last_level_ <= 0.0f)) {
        locked_ = false;
      }
    }
  }

  last_level_ = fLevel;

  if (locked_) {
    fLevel = 0.0f;
  }

  sample_ = fLevel;
  player_state_->effect_history_.AddSample(sample_, fDeltaTime);

  float fPropertyMin = PROPERTY_MIN;
  float fPropertyCenter = PROPERTY_CENTER;
  float fPropertyMax = PROPERTY_MAX;

  float fCurrent;
  if (sample_ < 0) {
    fCurrent = SCALE(sample_, 0.0f, -1.0f, fPropertyCenter, fPropertyMin);
  } else {
    fCurrent = SCALE(sample_, 0.0f, +1.0f, fPropertyCenter, fPropertyMax);
  }

  if (sound_reader_) {
    sound_reader_->SetProperty(SOUND_PROPERTY, fCurrent);
  }
}

/* Return false if any holds have been LetGo.  Otherwise, return true if at
 * least one hold is active. */
void SoundEffectControl::HoldsBeingHeld(
    int iRow, int& iHoldsHeld, int& iHoldsLetGo) const {
  iHoldsHeld = iHoldsLetGo = 0;
  for (int c = 0; c < note_data_->GetNumTracks(); ++c) {
    NoteData::TrackMap::const_iterator begin, end;
    note_data_->GetTapNoteRangeInclusive(c, iRow, iRow + 1, begin, end);
    if (begin == end) {
      continue;
    }

    const TapNote& tn = begin->second;
    if (tn.type_ != TapNoteType_HoldHead) {
      continue;
    }
    if (tn.hold_result_.active_) {
      ++iHoldsHeld;
    } else if (tn.hold_result_.hns_ == HNS_LetGo) {
      ++iHoldsLetGo;
    }
  }
}

/*
 * (c) 2006-2007 Glenn Maynard
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
