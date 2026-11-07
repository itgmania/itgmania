#ifndef SOUND_EFFECT_CONTROL_H
#define SOUND_EFFECT_CONTROL_H

#include <string>

#include "ThemeMetric.h"

class RageSoundReader;
class PlayerState;
class NoteData;
/** @brief Control a sound property through user input. */
class SoundEffectControl {
 public:
  SoundEffectControl();
  void Load(
      const std::string& type, PlayerState* player_state,
      const NoteData* note_data);

  void SetSoundReader(RageSoundReader* player);
  void ReleaseSound() { SetSoundReader(nullptr); }

  void Update(float delta_time);

 private:
  void HoldsBeingHeld(int row, int& holds_held, int& holds_let_go) const;

  ThemeMetric<std::string> SOUND_PROPERTY;
  ThemeMetric<bool> LOCK_TO_HOLD;
  ThemeMetric<float> PROPERTY_MIN;
  ThemeMetric<float> PROPERTY_CENTER;
  ThemeMetric<float> PROPERTY_MAX;

  PlayerState* player_state_;

  bool locked_;

  float sample_;
  float last_level_;

  const NoteData* note_data_;
  RageSoundReader* sound_reader_;
};

#endif

/**
 * @file
 * @author Glenn Maynard (c) 2006-2007
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
