#include "SongOptions.h"

#include <cmath>
#include <string>
#include <vector>

#include "CommonMetrics.h"
#include "EnumHelper.h"
#include "RageUtil.h"
#include "RageUtil/Regex.h"
#include "StdString.h"
#include "global.h"

static const char* AutosyncTypeNames[] = {
    "Off",
    "Song",
    "Machine",
    "Tempo",
};
XToString(AutosyncType);
XToLocalizedString(AutosyncType);
LuaXType(AutosyncType);

static const char* SoundEffectTypeNames[] = {
    "Off",
    "Speed",
    "Pitch",
};
XToString(SoundEffectType);
XToLocalizedString(SoundEffectType);
LuaXType(SoundEffectType);

void SongOptions::Approach(const SongOptions& other, float fDeltaSeconds) {
#define APPROACH(opt) \
  fapproach(opt, other.opt, fDeltaSeconds* other.speed_##opt);
#define DO_COPY(x) x = other.x;

  APPROACH(music_rate_);
  APPROACH(haste_);
  DO_COPY(assist_clap_);
  DO_COPY(assist_metronome_);
  DO_COPY(autosync_type_);
  DO_COPY(sound_effect_type_);
  DO_COPY(static_background_);
  DO_COPY(random_bg_only_);
  DO_COPY(save_score_);
  DO_COPY(save_replay_);
#undef APPROACH
#undef DO_COPY
}

static void AddPart(
    std::vector<std::string>& add_to, float level, std::string name) {
  if (level == 0) {
    return;
  }

  const std::string level_str =
      (level == 1) ? std::string("")
                   : ssprintf("%ld%% ", std::lrint(level * 100));

  add_to.push_back(level_str + name);
}

void SongOptions::GetMods(std::vector<std::string>& add_to) const {
  if (music_rate_ != 1) {
    std::string s = ssprintf("%2.2f", music_rate_);
    if (s[s.size() - 1] == '0') {
      s.erase(s.size() - 1);
    }
    add_to.push_back(s + "xMusic");
  }

  AddPart(add_to, haste_, "Haste");

  switch (autosync_type_) {
    case AutosyncType_Off:
      break;
    case AutosyncType_Song:
      add_to.push_back("AutosyncSong");
      break;
    case AutosyncType_Machine:
      add_to.push_back("AutosyncMachine");
      break;
    case AutosyncType_Tempo:
      add_to.push_back("AutosyncTempo");
      break;
    default:
      FAIL_M(ssprintf("Invalid autosync type: %i", autosync_type_));
  }

  switch (sound_effect_type_) {
    case SoundEffectType_Off:
      break;
    case SoundEffectType_Speed:
      add_to.push_back("EffectSpeed");
      break;
    case SoundEffectType_Pitch:
      add_to.push_back("EffectPitch");
      break;
    default:
      FAIL_M(ssprintf("Invalid sound effect type: %i", sound_effect_type_));
  }

  if (assist_clap_) {
    add_to.push_back("Clap");
  }
  if (assist_metronome_) {
    add_to.push_back("Metronome");
  }

  if (static_background_) {
    add_to.push_back("StaticBG");
  }
  if (random_bg_only_) {
    add_to.push_back("RandomBG");
  }
}

void SongOptions::GetLocalizedMods(std::vector<std::string>& v) const {
  GetMods(v);
  for (std::string& s : v) {
    s = CommonMetrics::LocalizeOptionItem(s, true);
  }
}

std::string SongOptions::GetString() const {
  std::vector<std::string> v;
  GetMods(v);
  return join(", ", v);
}

std::string SongOptions::GetLocalizedString() const {
  std::vector<std::string> v;
  GetLocalizedMods(v);
  return join(", ", v);
}

/* Options are added to the current settings; call Init() beforehand if
 * you don't want this. */
void SongOptions::FromString(const std::string& multiple_mods) {
  std::string temp = multiple_mods;
  std::vector<std::string> vs;
  split(temp, ",", vs, true);
  std::string throw_away;
  for (std::string& s : vs) {
    FromOneModString(s, throw_away);
  }
}

bool SongOptions::FromOneModString(
    const std::string& one_mod, std::string& error_out) {
  std::string bit = one_mod;
  MakeLower(bit);
  Trim(bit);

  Regex mult("^([0-9]+(\\.[0-9]+)?)xmusic$");
  std::vector<std::string> matches;
  if (mult.Compare(bit, matches)) {
    music_rate_ = StringToFloat(matches[0]);
    return true;
  }

  matches.clear();

  std::vector<std::string> parts;
  split(bit, " ", parts, true);
  bool on = true;
  if (parts.size() > 1) {
    bit = parts[1];
    if (parts[0] == "no") {
      on = false;
    }
  }

  if (bit == "clap") {
    assist_clap_ = on;
  } else if (bit == "metronome") {
    assist_metronome_ = on;
  } else if (bit == "autosync" || bit == "autosyncsong") {
    autosync_type_ = on ? AutosyncType_Song : AutosyncType_Off;
  } else if (bit == "autosyncmachine") {
    autosync_type_ = on ? AutosyncType_Machine : AutosyncType_Off;
  } else if (bit == "autosynctempo") {
    autosync_type_ = on ? AutosyncType_Tempo : AutosyncType_Off;
  } else if (bit == "effect" && !on) {
    sound_effect_type_ = SoundEffectType_Off;
  } else if (bit == "effectspeed") {
    sound_effect_type_ = on ? SoundEffectType_Speed : SoundEffectType_Off;
  } else if (bit == "effectpitch") {
    sound_effect_type_ = on ? SoundEffectType_Pitch : SoundEffectType_Off;
  } else if (bit == "staticbg") {
    static_background_ = on;
  } else if (bit == "randombg") {
    random_bg_only_ = on;
  } else if (bit == "savescore") {
    save_score_ = on;
  } else if (bit == "savereplay") {
    save_replay_ = on;
  } else if (bit == "haste") {
    haste_ = on ? 1.0f : 0.0f;
  } else {
    return false;
  }

  return true;
}

bool SongOptions::operator==(const SongOptions& other) const {
  if (music_rate_ != other.music_rate_) {
    return false;
  }
  if (haste_ != other.haste_) {
    return false;
  }
  if (assist_clap_ != other.assist_clap_) {
    return false;
  }
  if (assist_metronome_ != other.assist_metronome_) {
    return false;
  }
  if (autosync_type_ != other.autosync_type_) {
    return false;
  }
  if (sound_effect_type_ != other.sound_effect_type_) {
    return false;
  }
  if (static_background_ != other.static_background_) {
    return false;
  }
  if (random_bg_only_ != other.random_bg_only_) {
    return false;
  }
  if (save_score_ != other.save_score_) {
    return false;
  }
  if (save_replay_ != other.save_replay_) {
    return false;
  }
  return true;
}

// lua start
#include "LuaBinding.h"
#include "OptionsBinding.h"

/** @brief Allow Lua to have access to SongOptions. */
class LunaSongOptions : public Luna<SongOptions> {
 public:
  ENUM_INTERFACE(AutosyncSetting, autosync_type_, AutosyncType);
  // ENUM_INTERFACE(SoundEffectSetting, SoundEffectType, SoundEffectType);
  //  Broken, SoundEffectType_Speed disables rate mod, other settings have no
  //  effect. -Kyz
  BOOL_INTERFACE(AssistClap, assist_clap_);
  BOOL_INTERFACE(AssistMetronome, assist_metronome_);
  BOOL_INTERFACE(StaticBackground, static_background_);
  BOOL_INTERFACE(RandomBGOnly, random_bg_only_);
  BOOL_INTERFACE(SaveScore, save_score_);
  BOOL_INTERFACE(SaveReplay, save_replay_);
  FLOAT_INTERFACE(
      MusicRate, music_rate_,
      (v > 0.0f && v <= 3.0f));  // Greater than 3 seems to crash frequently,
                                 // haven't investigated why. -Kyz
  FLOAT_INTERFACE(Haste, haste_, (v >= -1.0f && v <= 1.0f));

  LunaSongOptions() {
    ADD_METHOD(AutosyncSetting);
    // ADD_METHOD(SoundEffectSetting);
    ADD_METHOD(AssistClap);
    ADD_METHOD(AssistMetronome);
    ADD_METHOD(StaticBackground);
    ADD_METHOD(RandomBGOnly);
    ADD_METHOD(SaveScore);
    ADD_METHOD(SaveReplay);
    ADD_METHOD(MusicRate);
    ADD_METHOD(Haste);
  }
};

LUA_REGISTER_CLASS(SongOptions)
// lua end

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
