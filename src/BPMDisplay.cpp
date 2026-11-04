#include "BPMDisplay.h"

#include <algorithm>
#include <climits>
#include <string>
#include <vector>

#include "ActorUtil.h"
#include "BitmapText.h"
#include "CommonMetrics.h"
#include "Course.h"
#include "GameConstantsAndTypes.h"
#include "GameState.h"
#include "PlayerNumber.h"
#include "RageUtil.h"
#include "RageUtil/RandomNumbers.h"
#include "Song.h"
#include "Steps.h"
#include "Style.h"
#include "global.h"

REGISTER_ACTOR_CLASS(BPMDisplay);

BPMDisplay::BPMDisplay() {
  bpm_from_ = bpm_to_ = 0;
  current_bpm_ = 0;
  bpms_.push_back(0);
  percent_in_state_ = 0;
  cycle_time_ = 1.0f;
}

void BPMDisplay::Load() {
  SET_NO_BPM_COMMAND.Load(name_, "SetNoBpmCommand");
  SET_NORMAL_COMMAND.Load(name_, "SetNormalCommand");
  SET_CHANGING_COMMAND.Load(name_, "SetChangeCommand");
  SET_RANDOM_COMMAND.Load(name_, "SetRandomCommand");
  SET_EXTRA_COMMAND.Load(name_, "SetExtraCommand");
  CYCLE.Load(name_, "Cycle");
  RANDOM_CYCLE_SPEED.Load(name_, "RandomCycleSpeed");
  COURSE_CYCLE_SPEED.Load(name_, "CourseCycleSpeed");
  SEPARATOR.Load(name_, "Separator");
  SHOW_QMARKS.Load(name_, "ShowQMarksInRandomCycle");
  NO_BPM_TEXT.Load(name_, "NoBpmText");
  QUESTIONMARKS_TEXT.Load(name_, "QuestionMarksText");
  RANDOM_TEXT.Load(name_, "RandomText");
  VARIOUS_TEXT.Load(name_, "VariousText");
  BPM_FORMAT_STRING.Load(name_, "FormatString");

  RunCommands(SET_NORMAL_COMMAND);
}

void BPMDisplay::LoadFromNode(const XNode* pNode) {
  BitmapText::LoadFromNode(pNode);
  Load();
}

float BPMDisplay::GetActiveBPM() const {
  return bpm_to_ + (bpm_from_ - bpm_to_) * percent_in_state_;
}

void BPMDisplay::Update(float fDeltaTime) {
  BitmapText::Update(fDeltaTime);

  if (!(bool)CYCLE) {
    return;
  }
  if (bpms_.size() == 0) {
    return;  // no bpm
  }

  percent_in_state_ -= fDeltaTime / cycle_time_;
  if (percent_in_state_ < 0) {
    // go to next state
    percent_in_state_ = 1;  // reset timer

    current_bpm_ = (current_bpm_ + 1) % bpms_.size();
    bpm_from_ = bpm_to_;
    bpm_to_ = bpms_[current_bpm_];

    if (bpm_to_ == -1) {
      bpm_from_ = -1;
      if ((bool)SHOW_QMARKS) {
        SetText(
            (RandomFloat(0, 1) > 0.90f)
                ? (std::string)QUESTIONMARKS_TEXT
                : ssprintf(
                      ((std::string)BPM_FORMAT_STRING).c_str(),
                      RandomFloat(0, 999)));
      } else {
        SetText(ssprintf(
            ((std::string)BPM_FORMAT_STRING).c_str(), RandomFloat(0, 999)));
      }
    } else if (bpm_from_ == -1) {
      bpm_from_ = bpm_to_;
    }
  }

  if (bpm_to_ != -1) {
    const float fActualBPM = GetActiveBPM();
    SetText(ssprintf(((std::string)BPM_FORMAT_STRING).c_str(), fActualBPM));
  }
}

void BPMDisplay::SetBPMRange(const DisplayBpms& bpms) {
  ASSERT(!bpms.bpms_.empty());

  bpms_.clear();

  const std::vector<float>& BPMS = bpms.bpms_;

  bool AllIdentical = true;
  for (unsigned i = 0; i < BPMS.size(); ++i) {
    if (i > 0 && BPMS[i] != BPMS[i - 1]) {
      AllIdentical = false;
    }
  }

  if (!(bool)CYCLE) {
    int MinBPM = INT_MAX;
    int MaxBPM = INT_MIN;
    for (unsigned i = 0; i < BPMS.size(); ++i) {
      MinBPM = std::min(MinBPM, static_cast<int>(BPMS[i] + 0.5));
      MaxBPM = std::max(MaxBPM, static_cast<int>(BPMS[i] + 0.5));
    }
    if (MinBPM == MaxBPM) {
      if (MinBPM == -1) {
        SetText(RANDOM_TEXT);  // random (was "...") -aj
      } else {
        SetText(ssprintf("%i", MinBPM));
      }
    } else {
      SetText(ssprintf("%i%s%i", MinBPM, SEPARATOR.GetValue().c_str(), MaxBPM));
    }
  } else {
    for (unsigned i = 0; i < BPMS.size(); ++i) {
      bpms_.push_back(BPMS[i]);
      if (BPMS[i] != -1) {
        bpms_.push_back(BPMS[i]);  // hold
      }
    }

    current_bpm_ =
        std::min(1, static_cast<int>(bpms_.size()));  // start on the first hold
    bpm_from_ = BPMS[0];
    bpm_to_ = BPMS[0];
    percent_in_state_ = 1;
  }

  if (GAMESTATE->IsAnExtraStageAndSelectionLocked()) {
    RunCommands(SET_EXTRA_COMMAND);
  } else if (!AllIdentical) {
    RunCommands(SET_CHANGING_COMMAND);
  } else {
    RunCommands(SET_NORMAL_COMMAND);
  }
}

void BPMDisplay::CycleRandomly() {
  DisplayBpms bpms;
  bpms.Add(-1);
  SetBPMRange(bpms);

  RunCommands(SET_RANDOM_COMMAND);

  cycle_time_ = (float)RANDOM_CYCLE_SPEED;

  // Go to default value in event of a negative value in the metrics
  if (cycle_time_ < 0) {
    cycle_time_ = 0.2f;
  }
}

void BPMDisplay::NoBPM() {
  bpms_.clear();
  SetText(NO_BPM_TEXT);
  RunCommands(SET_NO_BPM_COMMAND);
}

void BPMDisplay::SetBpmFromSong(const Song* pSong) {
  ASSERT(pSong != nullptr);
  switch (pSong->display_bpm_type_) {
    case DISPLAY_BPM_ACTUAL:
    case DISPLAY_BPM_SPECIFIED: {
      DisplayBpms bpms;
      pSong->GetDisplayBpms(bpms);
      SetBPMRange(bpms);
      cycle_time_ = 1.0f;
    } break;
    case DISPLAY_BPM_RANDOM:
      CycleRandomly();
      break;
    default:
      FAIL_M(
          ssprintf("Invalid display BPM type: %i", pSong->display_bpm_type_));
  }
}

void BPMDisplay::SetBpmFromSteps(const Steps* pSteps) {
  ASSERT(pSteps != nullptr);
  DisplayBpms bpms;
  float fMinBPM, fMaxBPM;
  pSteps->GetTimingData()->GetActualBPM(fMinBPM, fMaxBPM);
  bpms.Add(fMinBPM);
  bpms.Add(fMaxBPM);
  cycle_time_ = 1.0f;
}

void BPMDisplay::SetBpmFromCourse(const Course* pCourse) {
  ASSERT(pCourse != nullptr);
  ASSERT(GAMESTATE->GetCurrentStyle(PLAYER_INVALID) != nullptr);

  StepsType st = GAMESTATE->GetCurrentStyle(PLAYER_INVALID)->steps_type_;
  Trail* pTrail = pCourse->GetTrail(st);
  // GetTranslitFullTitle because "Crashinfo.txt is garbled because of the ANSI
  // output as usual." -f
  ASSERT_M(
      pTrail != nullptr, ssprintf(
                             "Course '%s' has no trail for StepsType '%s'",
                             pCourse->GetTranslitFullTitle().c_str(),
                             StringConversion::ToString(st).c_str()));

  cycle_time_ = (float)COURSE_CYCLE_SPEED;

  if ((int)pTrail->entries_.size() >
      CommonMetrics::MAX_COURSE_ENTRIES_BEFORE_VARIOUS) {
    SetVarious();
    return;
  }

  DisplayBpms bpms;
  pTrail->GetDisplayBpms(bpms);
  SetBPMRange(bpms);
}

void BPMDisplay::SetConstantBpm(float fBPM) {
  DisplayBpms bpms;
  bpms.Add(fBPM);
  SetBPMRange(bpms);
}

void BPMDisplay::SetVarious() {
  bpms_.clear();
  bpms_.push_back(-1);
  SetText(VARIOUS_TEXT);
}

void BPMDisplay::SetFromGameState() {
  if (GAMESTATE->cur_song_.Get()) {
    if (GAMESTATE->IsAnExtraStageAndSelectionLocked()) {
      CycleRandomly();
    } else {
      SetBpmFromSong(GAMESTATE->cur_song_);
    }
    return;
  }
  if (GAMESTATE->cur_course_.Get()) {
    if (GAMESTATE->GetCurrentStyle(PLAYER_INVALID) == nullptr)
      ;  // This is true when backing out from ScreenSelectCourse to
         // ScreenTitleMenu.  So, don't call SetBpmFromCourse where an assert
         // will fire.
    else {
      SetBpmFromCourse(GAMESTATE->cur_course_);
    }

    return;
  }

  NoBPM();
}

// SongBPMDisplay (in-game BPM display)
class SongBPMDisplay : public BPMDisplay {
 public:
  SongBPMDisplay();
  virtual SongBPMDisplay* Copy() const;
  virtual void Update(float fDeltaTime);

 private:
  float last_game_state_bpm_;
};

SongBPMDisplay::SongBPMDisplay() { last_game_state_bpm_ = 0; }

void SongBPMDisplay::Update(float fDeltaTime) {
  float fGameStateBPM = GAMESTATE->position_.cur_bps_ * 60.0f;
  if (last_game_state_bpm_ != fGameStateBPM) {
    last_game_state_bpm_ = fGameStateBPM;
    SetConstantBpm(fGameStateBPM);
  }

  BPMDisplay::Update(fDeltaTime);
}

REGISTER_ACTOR_CLASS(SongBPMDisplay);

#include "LuaBinding.h"
/** @brief Allow Lua to have access to the BPMDisplay. */
class LunaBPMDisplay : public Luna<BPMDisplay> {
 public:
  static int SetFromGameState(T* p, lua_State* L) {
    p->SetFromGameState();
    COMMON_RETURN_SELF;
  }
  static int SetFromSong(T* p, lua_State* L) {
    if (lua_isnil(L, 1)) {
      p->NoBPM();
    } else {
      const Song* pSong = Luna<Song>::check(L, 1, true);
      p->SetBpmFromSong(pSong);
    }
    COMMON_RETURN_SELF;
  }
  static int SetFromSteps(T* p, lua_State* L) {
    if (lua_isnil(L, 1)) {
      p->NoBPM();
    } else {
      const Steps* pSteps = Luna<Steps>::check(L, 1, true);
      p->SetBpmFromSteps(pSteps);
    }
    COMMON_RETURN_SELF;
  }
  static int SetFromCourse(T* p, lua_State* L) {
    if (lua_isnil(L, 1)) {
      p->NoBPM();
    } else {
      const Course* pCourse = Luna<Course>::check(L, 1, true);
      p->SetBpmFromCourse(pCourse);
    }
    COMMON_RETURN_SELF;
  }
  static int GetText(T* p, lua_State* L) {
    lua_pushstring(L, p->GetText().c_str());
    return 1;
  }

  LunaBPMDisplay() {
    ADD_METHOD(SetFromGameState);
    ADD_METHOD(SetFromSong);
    ADD_METHOD(SetFromSteps);
    ADD_METHOD(SetFromCourse);
    ADD_METHOD(GetText);
  }
};

LUA_REGISTER_DERIVED_CLASS(BPMDisplay, BitmapText)

/*
 * (c) 2001-2002 Chris Danford
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
