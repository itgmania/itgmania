#include "LifeMeterTime.h"

#include <algorithm>
#include <cstddef>
#include <string>

#include "Actor.h"
#include "Course.h"
#include "GameConstantsAndTypes.h"
#include "GameState.h"
#include "LifeMeter.h"
#include "MessageManager.h"
#include "PlayerStageStats.h"
#include "PlayerState.h"
#include "Preference.h"
#include "RadarValues.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "Song.h"
#include "StatsManager.h"
#include "Steps.h"
#include "StreamDisplay.h"
#include "ThemeManager.h"
#include "ThemeMetric.h"
#include "global.h"

const float FULL_LIFE_SECONDS = 1.5f * 60;

static ThemeMetric<float> METER_WIDTH("LifeMeterTime", "MeterWidth");
static ThemeMetric<float> METER_HEIGHT("LifeMeterTime", "MeterHeight");
static ThemeMetric<float> DANGER_THRESHOLD("LifeMeterTime", "DangerThreshold");
static ThemeMetric<float> INITIAL_VALUE("LifeMeterTime", "InitialValue");
static ThemeMetric<float> MIN_LIFE_TIME("LifeMeterTime", "MinLifeTime");

static const float g_fTimeMeterSecondsChangeInit[] = {
    +0.0f,  // SE_CheckpointHit
    +0.2f,  // SE_W1
    +0.0f,  // SE_W2
    -0.5f,  // SE_W3
    -1.0f,  // SE_W4
    -2.0f,  // SE_W5
    -4.0f,  // SE_Miss
    -2.0f,  // SE_HitMine
    -0.0f,  // SE_CheckpointMiss
    -0.0f,  // SE_Held
    -4.0f,  // SE_LetGo
    -0.0f,  // SE_Missed
};
static_assert(ARRAYLEN(g_fTimeMeterSecondsChangeInit) == NUM_ScoreEvent);

static void TimeMeterSecondsChangeInit(
    size_t /*ScoreEvent*/ i, std::string& sNameOut, float& defaultValueOut) {
  sNameOut = "TimeMeterSecondsChange" + ScoreEventToString((ScoreEvent)i);
  defaultValueOut = g_fTimeMeterSecondsChangeInit[i];
}

static Preference1D<float> g_fTimeMeterSecondsChange(
    TimeMeterSecondsChangeInit, NUM_ScoreEvent);

LifeMeterTime::LifeMeterTime() {
  life_total_gained_seconds_ = 0;
  life_total_lost_seconds_ = 0;
  stream_ = nullptr;
}

LifeMeterTime::~LifeMeterTime() { delete stream_; }

void LifeMeterTime::Load(
    const PlayerState* pPlayerState, PlayerStageStats* pPlayerStageStats) {
  LifeMeter::Load(pPlayerState, pPlayerStageStats);

  const std::string sType = "LifeMeterTime";

  spr_background_.Load(THEME->GetPathG(sType, "background"));
  spr_background_->SetName("Background");
  spr_background_->ZoomToWidth(METER_WIDTH);
  spr_background_->ZoomToHeight(METER_HEIGHT);
  this->AddChild(spr_background_);

  quad_danger_glow_.ZoomToWidth(METER_WIDTH);
  quad_danger_glow_.ZoomToHeight(METER_HEIGHT);
  // hardcoded effects...
  quad_danger_glow_.SetEffectDiffuseShift(
      1.0f, RageColor(1, 0, 0, 0.8f), RageColor(1, 0, 0, 0));
  quad_danger_glow_.SetEffectClock(Actor::CLOCK_BGM_BEAT);
  this->AddChild(&quad_danger_glow_);

  stream_ = new StreamDisplay;
  bool bExtra = GAMESTATE->IsAnExtraStage();
  stream_->Load(bExtra ? "StreamDisplayExtra" : "StreamDisplay");
  this->AddChild(stream_);

  std::string sExtra = bExtra ? "extra " : "";
  spr_frame_.Load(THEME->GetPathG(sType, sExtra + "frame"));
  spr_frame_->SetName("Frame");
  this->AddChild(spr_frame_);

  sound_gain_life_.Load(THEME->GetPathS(sType, "GainLife"));
}

void LifeMeterTime::OnLoadSong() {
  if (GetLifeSeconds() <= 0 && GAMESTATE->GetCourseSongIndex() > 0) {
    return;
  }

  float fOldLife = life_total_lost_seconds_;
  float fGainSeconds = 0;
  if (GAMESTATE->IsCourseMode()) {
    Course* pCourse = GAMESTATE->cur_course_;
    ASSERT(pCourse != nullptr);
    fGainSeconds =
        pCourse->entries_[GAMESTATE->GetCourseSongIndex()].gain_seconds_;
  } else {
    // Placeholderish, at least this way it won't crash when someone tries it
    // out in non-course mode. -Kyz
    Song* song = GAMESTATE->cur_song_;
    ASSERT(song != nullptr);
    float song_len = song->music_length_seconds_;
    Steps* steps = GAMESTATE->cur_steps_[player_state_->player_number_];
    ASSERT(steps != nullptr);
    RadarValues radars = steps->GetRadarValues(player_state_->player_number_);
    float scorable_things =
        radars[RadarCategory_TapsAndHolds] + radars[RadarCategory_Lifts];
    if (g_fTimeMeterSecondsChange[SE_Held] > 0.0f) {
      scorable_things +=
          radars[RadarCategory_Holds] + radars[RadarCategory_Rolls];
    }
    // Calculate the amount of time to give for the player to need 80% W1.
    float gainable_score_time =
        scorable_things * g_fTimeMeterSecondsChange[SE_W1];
    fGainSeconds = song_len - (gainable_score_time * INITIAL_VALUE);
  }

  if (MIN_LIFE_TIME > fGainSeconds) {
    fGainSeconds = MIN_LIFE_TIME;
  }
  life_total_gained_seconds_ += fGainSeconds;
  sound_gain_life_.Play(false);
  SendLifeChangedMessage(fOldLife, TapNoteScore_Invalid, HoldNoteScore_Invalid);
}

void LifeMeterTime::ChangeLife(TapNoteScore tns) {
  if (GetLifeSeconds() <= 0) {
    return;
  }

  float fMeterChange = 0;
  switch (tns) {
    default:
      FAIL_M(ssprintf("Invalid TapNoteScore: %i", tns));
    case TNS_W1:
      fMeterChange = g_fTimeMeterSecondsChange[SE_W1];
      break;
    case TNS_W2:
      fMeterChange = g_fTimeMeterSecondsChange[SE_W2];
      break;
    case TNS_W3:
      fMeterChange = g_fTimeMeterSecondsChange[SE_W3];
      break;
    case TNS_W4:
      fMeterChange = g_fTimeMeterSecondsChange[SE_W4];
      break;
    case TNS_W5:
      fMeterChange = g_fTimeMeterSecondsChange[SE_W5];
      break;
    case TNS_Miss:
      fMeterChange = g_fTimeMeterSecondsChange[SE_Miss];
      break;
    case TNS_HitMine:
      fMeterChange = g_fTimeMeterSecondsChange[SE_HitMine];
      break;
    case TNS_CheckpointHit:
      fMeterChange = g_fTimeMeterSecondsChange[SE_CheckpointHit];
      break;
    case TNS_CheckpointMiss:
      fMeterChange = g_fTimeMeterSecondsChange[SE_CheckpointMiss];
      break;
  }

  float fOldLife = life_total_lost_seconds_;
  life_total_lost_seconds_ -= fMeterChange;
  SendLifeChangedMessage(fOldLife, tns, HoldNoteScore_Invalid);
}

void LifeMeterTime::ChangeLife(HoldNoteScore hns, TapNoteScore tns) {
  if (GetLifeSeconds() <= 0) {
    return;
  }

  float fMeterChange = 0;
  switch (hns) {
    default:
      FAIL_M(ssprintf("Invalid HoldNoteScore: %i", hns));
    case HNS_Held:
      fMeterChange = g_fTimeMeterSecondsChange[SE_Held];
      break;
    case HNS_LetGo:
      fMeterChange = g_fTimeMeterSecondsChange[SE_LetGo];
      break;
    case HNS_Missed:
      fMeterChange = g_fTimeMeterSecondsChange[SE_Missed];
      break;
  }

  float fOldLife = life_total_lost_seconds_;
  life_total_lost_seconds_ -= fMeterChange;
  SendLifeChangedMessage(fOldLife, tns, hns);
}

void LifeMeterTime::ChangeLife(float delta) {
  float old_life = life_total_lost_seconds_;
  life_total_lost_seconds_ -= delta;
  SendLifeChangedMessage(old_life, TapNoteScore_Invalid, HoldNoteScore_Invalid);
}

void LifeMeterTime::SetLife(float value) {
  float old_life = life_total_lost_seconds_;
  life_total_lost_seconds_ = value;
  SendLifeChangedMessage(old_life, TapNoteScore_Invalid, HoldNoteScore_Invalid);
}

void LifeMeterTime::HandleTapScoreNone() {
  // do nothing.
}

void LifeMeterTime::SendLifeChangedMessage(
    float fOldLife, TapNoteScore tns, HoldNoteScore hns) {
  Message msg("LifeChanged");
  msg.SetParam("Player", player_state_->player_number_);
  msg.SetParam("TapNoteScore", LuaReference::Create(tns));
  msg.SetParam("HoldNoteScore", LuaReference::Create(hns));
  msg.SetParam("OldLife", fOldLife);
  msg.SetParam("Difference", fOldLife - life_total_lost_seconds_);
  msg.SetParam("LifeMeter", LuaReference::CreateFromPush(*this));
  MESSAGEMAN->Broadcast(msg);
}

bool LifeMeterTime::IsInDanger() const {
  return stream_->GetPercent() < DANGER_THRESHOLD;
}

bool LifeMeterTime::IsHot() const { return false; }

bool LifeMeterTime::IsFailing() const { return GetLifeSeconds() <= 0; }

void LifeMeterTime::Update(float fDeltaTime) {
  // update current stage stats so ScoreDisplayLifeTime can show the right thing
  float fSecs = GetLifeSeconds();
  fSecs = std::max(0.0f, fSecs);
  player_stage_stats_->life_remaining_seconds_ = fSecs;

  LifeMeter::Update(fDeltaTime);

  stream_->SetPercent(GetLife());
  stream_->SetPassingAlpha(0);
  stream_->SetHotAlpha(0);

  if (player_state_->health_state_ == HealthState_Danger) {
    quad_danger_glow_.SetDiffuseAlpha(1);
  } else {
    quad_danger_glow_.SetDiffuseAlpha(0);
  }
}

float LifeMeterTime::GetLife() const {
  float fPercent = GetLifeSeconds() / FULL_LIFE_SECONDS;
  rage_clamp(fPercent, 0, 1);
  return fPercent;
}

float LifeMeterTime::GetLifeSeconds() const {
  return life_total_gained_seconds_ -
         (life_total_lost_seconds_ + STATSMAN->cur_stage_stats_.steps_seconds_);
}

/*
 * (c) 2001-2004 Chris Danford
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
