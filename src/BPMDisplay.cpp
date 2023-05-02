#include "global.h"

#include "BPMDisplay.h"

#include <cmath>
#include <climits>
#include <vector>

#include "ActorUtil.h"
#include "CommonMetrics.h"
#include "Course.h"
#include "GameConstantsAndTypes.h"
#include "GameState.h"
#include "LocalizedString.h"
#include "RageUtil.h"
#include "Song.h"
#include "Steps.h"
#include "Style.h"

REGISTER_ACTOR_CLASS(BPMDisplay);

BPMDisplay::BPMDisplay() {
  bpm_from_ = 0;
  bpm_to_ = 0;
  current_bpm_ = 0;
  bpms_.push_back(0);
  percent_in_state_ = 0;
  cycle_time_ = 1.0f;
}

void BPMDisplay::Load() {
  SET_NO_BPM_COMMAND.Load(m_sName, "SetNoBpmCommand");
  SET_NORMAL_COMMAND.Load(m_sName, "SetNormalCommand");
  SET_CHANGING_COMMAND.Load(m_sName, "SetChangeCommand");
  SET_RANDOM_COMMAND.Load(m_sName, "SetRandomCommand");
  SET_EXTRA_COMMAND.Load(m_sName, "SetExtraCommand");
  CYCLE.Load(m_sName, "Cycle");
  RANDOM_CYCLE_SPEED.Load(m_sName, "RandomCycleSpeed");
  COURSE_CYCLE_SPEED.Load(m_sName, "CourseCycleSpeed");
  SEPARATOR.Load(m_sName, "Separator");
  SHOW_QMARKS.Load(m_sName, "ShowQMarksInRandomCycle");
  NO_BPM_TEXT.Load(m_sName, "NoBpmText");
  QUESTIONMARKS_TEXT.Load(m_sName, "QuestionMarksText");
  RANDOM_TEXT.Load(m_sName, "RandomText");
  VARIOUS_TEXT.Load(m_sName, "VariousText");
  BPM_FORMAT_STRING.Load(m_sName, "FormatString");

  RunCommands(SET_NORMAL_COMMAND);
}

void BPMDisplay::LoadFromNode(const XNode* node) {
  BitmapText::LoadFromNode(node);
  Load();
}

float BPMDisplay::GetActiveBPM() const {
  return bpm_to_ + (bpm_from_ - bpm_to_) * percent_in_state_;
}

void BPMDisplay::Update(float delta) {
  BitmapText::Update(delta);

  if (!(bool)CYCLE) {
    return;
  }
  if (bpms_.size() == 0) {
    return;  // no bpm
  }

  percent_in_state_ -= delta / cycle_time_;
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
                ? (RString)QUESTIONMARKS_TEXT
                : ssprintf((RString)BPM_FORMAT_STRING, RandomFloat(0, 999)));
      } else {
        SetText(ssprintf((RString)BPM_FORMAT_STRING, RandomFloat(0, 999)));
      }
    } else if (bpm_from_ == -1) {
      bpm_from_ = bpm_to_;
    }
  }

  if (bpm_to_ != -1) {
    const float fActualBPM = GetActiveBPM();
    SetText(ssprintf((RString)BPM_FORMAT_STRING, fActualBPM));
  }
}

void BPMDisplay::SetBPMRange(const DisplayBpms& display_bpms) {
  ASSERT(!display_bpms.bpms.empty());

  bpms_.clear();

  const std::vector<float>& bpms = display_bpms.bpms;

  bool all_identical = true;
  for (unsigned i = 0; i < bpms.size(); ++i) {
    if (i > 0 && bpms[i] != bpms[i - 1]) {
      all_identical = false;
    }
  }

  if (!(bool)CYCLE) {
    int min_bpm = INT_MAX;
    int max_bpm = INT_MIN;
    for (unsigned i = 0; i < bpms.size(); ++i) {
      min_bpm = std::min(min_bpm, (int)lrintf(bpms[i]));
      max_bpm = std::max(max_bpm, (int)lrintf(bpms[i]));
    }
    if (min_bpm == max_bpm) {
      if (min_bpm == -1) {
        // NOTE(aj): random (was "...")
        SetText(RANDOM_TEXT);
      } else {
        SetText(ssprintf("%i", min_bpm));
      }
    } else {
      SetText(
          ssprintf("%i%s%i", min_bpm, SEPARATOR.GetValue().c_str(), max_bpm));
    }
  } else {
    for (unsigned i = 0; i < bpms.size(); ++i) {
      bpms_.push_back(bpms[i]);
      if (bpms[i] != -1) {
        // Hold
        bpms_.push_back(bpms[i]);
      }
    }

    // Start on the first hold.
    current_bpm_ = std::min(1, static_cast<int>(bpms_.size()));
    bpm_from_ = bpms[0];
    bpm_to_ = bpms[0];
    percent_in_state_ = 1;
  }

  if (GAMESTATE->IsAnExtraStageAndSelectionLocked()) {
    RunCommands(SET_EXTRA_COMMAND);
  } else if (!all_identical) {
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

void BPMDisplay::SetBpmFromSong(const Song* song) {
  ASSERT(song != nullptr);
  switch (song->m_DisplayBPMType) {
    case DISPLAY_BPM_ACTUAL:
    case DISPLAY_BPM_SPECIFIED: {
      DisplayBpms bpms;
      song->GetDisplayBpms(bpms);
      SetBPMRange(bpms);
      cycle_time_ = 1.0f;
    } break;
    case DISPLAY_BPM_RANDOM:
      CycleRandomly();
      break;
    default:
      FAIL_M(ssprintf("Invalid display BPM type: %i", song->m_DisplayBPMType));
  }
}

void BPMDisplay::SetBpmFromSteps(const Steps* steps) {
  ASSERT(steps != nullptr);
  DisplayBpms bpms;
  float min_bpm, max_bpm;
  steps->GetTimingData()->GetActualBPM(min_bpm, max_bpm);
  bpms.Add(min_bpm);
  bpms.Add(max_bpm);
  cycle_time_ = 1.0f;
}

void BPMDisplay::SetBpmFromCourse(const Course* course) {
  ASSERT(course != nullptr);
  ASSERT(GAMESTATE->GetCurrentStyle(PLAYER_INVALID) != nullptr);

  StepsType steps_type =
      GAMESTATE->GetCurrentStyle(PLAYER_INVALID)->m_StepsType;
  Trail* trail = course->GetTrail(steps_type);
  // NOTE(f): GetTranslitFullTitle because "Crashinfo.txt is garbled because of
  // the ANSI output as usual."
  ASSERT_M(trail != nullptr,
           ssprintf(
               "Course '%s' has no trail for StepsType '%s'",
                course->GetTranslitFullTitle().c_str(),
                StringConversion::ToString(steps_type).c_str()));

  cycle_time_ = (float)COURSE_CYCLE_SPEED;

  if ((int)trail->m_vEntries.size() >
      CommonMetrics::MAX_COURSE_ENTRIES_BEFORE_VARIOUS) {
    SetVarious();
    return;
  }

  DisplayBpms bpms;
  trail->GetDisplayBpms(bpms);
  SetBPMRange(bpms);
}

void BPMDisplay::SetConstantBpm(float bpm) {
  DisplayBpms bpms;
  bpms.Add(bpm);
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
    if (GAMESTATE->GetCurrentStyle(PLAYER_INVALID) == nullptr) {
      ;  // This is true when backing out from ScreenSelectCourse to
         // ScreenTitleMenu.  So, don't call SetBpmFromCourse where an assert
         // will fire.
    } else {
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
  virtual void Update(float delta);

 private:
  float m_fLastGameStateBPM;
};

SongBPMDisplay::SongBPMDisplay() { m_fLastGameStateBPM = 0; }

void SongBPMDisplay::Update(float delta) {
  float fGameStateBPM = GAMESTATE->position_.m_fCurBPS * 60.0f;
  if (m_fLastGameStateBPM != fGameStateBPM) {
    m_fLastGameStateBPM = fGameStateBPM;
    SetConstantBpm(fGameStateBPM);
  }

  BPMDisplay::Update(delta);
}

REGISTER_ACTOR_CLASS(SongBPMDisplay);

#include "LuaBinding.h"
// Allow Lua to have access to the BPMDisplay.
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
      const Song* song = Luna<Song>::check(L, 1, true);
      p->SetBpmFromSong(song);
    }
    COMMON_RETURN_SELF;
  }

  static int SetFromSteps(T* p, lua_State* L) {
    if (lua_isnil(L, 1)) {
      p->NoBPM();
    } else {
      const Steps* steps = Luna<Steps>::check(L, 1, true);
      p->SetBpmFromSteps(steps);
    }
    COMMON_RETURN_SELF;
  }

  static int SetFromCourse(T* p, lua_State* L) {
    if (lua_isnil(L, 1)) {
      p->NoBPM();
    } else {
      const Course* course = Luna<Course>::check(L, 1, true);
      p->SetBpmFromCourse(course);
    }
    COMMON_RETURN_SELF;
  }

  static int GetText(T* p, lua_State* L) {
    lua_pushstring(L, p->GetText());
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
