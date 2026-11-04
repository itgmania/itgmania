/* SongOptions - Options that apply to an entire song (not per-player). */

#ifndef SONG_OPTIONS_H
#define SONG_OPTIONS_H

#include <string>
#include <vector>

#include "EnumHelper.h"

enum AutosyncType {
  AutosyncType_Off,
  AutosyncType_Song,
  AutosyncType_Machine,
  AutosyncType_Tempo,
  NUM_AutosyncType,
  AutosyncType_Invalid
};
const std::string& AutosyncTypeToString(AutosyncType cat);
const std::string& AutosyncTypeToLocalizedString(AutosyncType cat);
LuaDeclareType(AutosyncType);

enum SoundEffectType {
  SoundEffectType_Off,
  SoundEffectType_Speed,
  SoundEffectType_Pitch,
  NUM_SoundEffectType,
  SoundEffectType_Invalid
};
const std::string& SoundEffectTypeToString(SoundEffectType cat);
const std::string& SoundEffectTypeToLocalizedString(SoundEffectType cat);
LuaDeclareType(SoundEffectType);

class SongOptions {
 public:
  bool assist_clap_;
  bool assist_metronome_;
  float music_rate_, speed_music_rate_;
  float haste_, speed_haste_;
  AutosyncType autosync_type_;
  SoundEffectType sound_effect_type_;
  bool static_background_;
  bool random_bg_only_;
  bool save_score_;
  bool save_replay_;

  /**
   * @brief Set up the SongOptions with reasonable defaults.
   */
  SongOptions()
      : assist_clap_(false),
        assist_metronome_(false),
        music_rate_(1.0f),
        speed_music_rate_(1.0f),
        haste_(0.0f),
        speed_haste_(1.0f),
        autosync_type_(AutosyncType_Off),
        sound_effect_type_(SoundEffectType_Off),
        static_background_(false),
        random_bg_only_(false),
        save_score_(true),
        save_replay_(false) {};
  void Init() { *this = {}; }
  void Approach(const SongOptions& other, float fDeltaSeconds);
  void GetMods(std::vector<std::string>& AddTo) const;
  void GetLocalizedMods(std::vector<std::string>& AddTo) const;
  std::string GetString() const;
  std::string GetLocalizedString() const;
  void FromString(const std::string& sOptions);
  bool FromOneModString(
      const std::string& sOneMod,
      std::string& sErrorDetailOut);  // On error, return false and optionally
                                      // set sErrorDetailOut

  bool operator==(const SongOptions& other) const;
  bool operator!=(const SongOptions& other) const { return !operator==(other); }

  // Lua
  void PushSelf(lua_State* L);
};

#endif

/*
 * (c) 2001-2004 Chris Danford, Glenn Maynard
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
