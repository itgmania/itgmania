#include "Trail.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <string>
#include <vector>

#include "Attack.h"
#include "CommonMetrics.h"
#include "GameConstantsAndTypes.h"
#include "GameState.h"
#include "LuaManager.h"
#include "NoteData.h"
#include "NoteDataUtil.h"
#include "PlayerNumber.h"
#include "PlayerOptions.h"
#include "Song.h"
#include "Steps.h"
#include "global.h"

void TrailEntry::GetAttackArray(AttackArray& out) const {
  if (!modifiers_.empty()) {
    out.push_back(Attack::FromGlobalCourseModifier(modifiers_));
  }

  out.insert(out.end(), attacks_.begin(), attacks_.end());
}

bool TrailEntry::ContainsTransformOrTurn() const {
  PlayerOptions po;
  po.FromString(modifiers_);
  if (po.ContainsTransformOrTurn()) {
    return true;
  }
  if (attacks_.ContainsTransformOrTurn()) {
    return true;
  }
  return false;
}

// TrailEntry lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the TrailEntry. */
class LunaTrailEntry : public Luna<TrailEntry> {
 public:
  static int GetSong(T* p, lua_State* L) {
    if (p->song_) {
      p->song_->PushSelf(L);
    } else {
      lua_pushnil(L);
    }
    return 1;
  }
  static int GetSteps(T* p, lua_State* L) {
    if (p->steps_) {
      p->steps_->PushSelf(L);
    } else {
      lua_pushnil(L);
    }
    return 1;
  }
  DEFINE_METHOD(IsSecret, secret_);
  DEFINE_METHOD(GetNormalModifiers, modifiers_);

  LunaTrailEntry() {
    ADD_METHOD(GetSong);
    ADD_METHOD(GetSteps);
    ADD_METHOD(IsSecret);
    ADD_METHOD(GetNormalModifiers);
  }
};

LUA_REGISTER_CLASS(TrailEntry)
// TrailEntry lua end

void Trail::SetRadarValues(const RadarValues& rv) {
  cached_radar_values_ = rv;
  radar_values_cached_ = true;
}

const RadarValues& Trail::GetRadarValues() const {
  if (radar_values_cached_) {
    return cached_radar_values_;
  }
  if (IsSecret()) {
    // Don't calculate RadarValues for a non-fixed Course.  They values are
    // worthless because they'll change every time this Trail is
    // regenerated.
    cached_radar_values_ = RadarValues();
    return cached_radar_values_;
  } else {
    RadarValues rv;
    rv.Zero();

    for (const TrailEntry& e : entries_) {
      const Steps* pSteps = e.steps_;
      ASSERT(pSteps != nullptr);
      // Hack: don't calculate for autogen entries
      if (!pSteps->IsAutogen() && e.ContainsTransformOrTurn()) {
        NoteData nd;
        pSteps->GetNoteData(nd);
        RadarValues rv_orig;
        TimingData* timing = const_cast<TimingData*>(pSteps->GetTimingData());
        NoteDataUtil::CalculateRadarValues(
            nd, e.song_->music_length_seconds_, timing, rv_orig);
        PlayerOptions po;
        po.FromString(e.modifiers_);
        if (po.ContainsTransformOrTurn()) {
          NoteDataUtil::TransformNoteData(
              nd, *(pSteps->GetTimingData()), po, pSteps->steps_type_);
        }
        NoteDataUtil::TransformNoteData(
            nd, *(pSteps->GetTimingData()), e.attacks_, pSteps->steps_type_,
            e.song_);
        RadarValues transformed_rv;
        NoteDataUtil::CalculateRadarValues(
            nd, e.song_->music_length_seconds_, timing, transformed_rv);
        rv += transformed_rv;
      } else {
        rv += pSteps->GetRadarValues(PLAYER_1);
      }
    }

    /* Hack: SetRadarValues is non-const (a const setter doesn't
     * make sense), but it only modifies a mutable value.  Just
     * cast away const. */
    const_cast<Trail*>(this)->SetRadarValues(rv);

    return cached_radar_values_;
  }
}

int Trail::GetMeter() const {
  if (specified_meter_ != -1) {
    return specified_meter_;
  }

  if (entries_.empty()) {
    return 0;
  }

  float fMeter = GetTotalMeter() / (float)entries_.size();

  return std::lrint(fMeter);
}

int Trail::GetTotalMeter() const {
  return std::accumulate(
      entries_.begin(), entries_.end(), 0, [](int total, const TrailEntry& e) {
        return total + e.steps_->GetMeter();
      });
}

float Trail::GetLengthSeconds() const {
  return std::accumulate(
      entries_.begin(), entries_.end(), 0.f,
      [](float total, const TrailEntry& e) {
        return total + e.song_->music_length_seconds_;
      });
}

void Trail::GetDisplayBpms(DisplayBpms& AddTo) const {
  for (const TrailEntry& e : entries_) {
    if (e.secret_) {
      AddTo.Add(-1);
      continue;
    }

    Song* pSong = e.song_;
    ASSERT(pSong != nullptr);
    switch (pSong->display_bpm_type_) {
      case DISPLAY_BPM_ACTUAL:
      case DISPLAY_BPM_SPECIFIED:
        pSong->GetDisplayBpms(AddTo);
        break;
      case DISPLAY_BPM_RANDOM:
        AddTo.Add(-1);
        break;
        DEFAULT_FAIL(pSong->display_bpm_type_);
    }
  }
}

bool Trail::IsSecret() const {
  return std::any_of(entries_.begin(), entries_.end(), [](const TrailEntry& e) {
    return e.secret_;
  });
}

bool Trail::ContainsSong(const Song* pSong) const {
  return std::any_of(
      entries_.begin(), entries_.end(),
      [&](const TrailEntry& e) { return e.song_ == pSong; });
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the Trail. */
class LunaTrail : public Luna<Trail> {
 public:
  static int GetDifficulty(T* p, lua_State* L) {
    LuaHelpers::Push(L, p->course_difficulty_);
    return 1;
  }
  static int GetMeter(T* p, lua_State* L) {
    LuaHelpers::Push(L, p->GetMeter());
    return 1;
  }
  static int GetTotalMeter(T* p, lua_State* L) {
    LuaHelpers::Push(L, p->GetTotalMeter());
    return 1;
  }
  static int GetStepsType(T* p, lua_State* L) {
    LuaHelpers::Push(L, p->steps_type_);
    return 1;
  }
  static int GetRadarValues(T* p, lua_State* L) {
    RadarValues& rv = const_cast<RadarValues&>(p->GetRadarValues());
    rv.PushSelf(L);
    return 1;
  }
  static int GetArtists(T* p, lua_State* L) {
    std::vector<std::string> asArtists, asAltArtists;
    for (const TrailEntry& e : p->entries_) {
      if (e.secret_) {
        asArtists.push_back("???");
        asAltArtists.push_back("???");
      } else {
        asArtists.push_back(e.song_->GetDisplayArtist());
        asAltArtists.push_back(e.song_->GetTranslitArtist());
      }
    }

    if ((int)asArtists.size() >
        CommonMetrics::MAX_COURSE_ENTRIES_BEFORE_VARIOUS) {
      asArtists.clear();
      asAltArtists.clear();
      asArtists.push_back("Various Artists");
      asAltArtists.push_back("Various Artists");
    }

    LuaHelpers::CreateTableFromArray(asArtists, L);
    LuaHelpers::CreateTableFromArray(asAltArtists, L);
    return 2;
  }
  static int GetTrailEntry(T* p, lua_State* L) {
    TrailEntry& te = p->entries_[IArg(1)];
    te.PushSelf(L);
    return 1;
  }
  static int GetTrailEntries(T* p, lua_State* L) {
    std::vector<TrailEntry*> v;
    for (unsigned i = 0; i < p->entries_.size(); ++i) {
      v.push_back(&p->entries_[i]);
    }
    LuaHelpers::CreateTableFromArray<TrailEntry*>(v, L);
    return 1;
  }
  DEFINE_METHOD(GetLengthSeconds, GetLengthSeconds())
  DEFINE_METHOD(IsSecret, IsSecret())
  static int ContainsSong(T* p, lua_State* L) {
    const Song* pS = Luna<Song>::check(L, 1);
    lua_pushboolean(L, p->ContainsSong(pS));
    return 1;
  }

  LunaTrail() {
    ADD_METHOD(GetDifficulty);
    ADD_METHOD(GetMeter);
    ADD_METHOD(GetTotalMeter);
    ADD_METHOD(GetStepsType);
    ADD_METHOD(GetRadarValues);
    ADD_METHOD(GetArtists);
    ADD_METHOD(GetTrailEntry);
    ADD_METHOD(GetTrailEntries);
    ADD_METHOD(GetLengthSeconds);
    ADD_METHOD(IsSecret);
    ADD_METHOD(ContainsSong);
  }
};

LUA_REGISTER_CLASS(Trail)
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
