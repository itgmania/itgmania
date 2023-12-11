#include "global.h"

#include "CombinedLifeMeterTug.h"

#include <cstddef>

#include "ActorUtil.h"
#include "GameState.h"
#include "PrefsManager.h"
#include "ThemeManager.h"
#include "ThemeMetric.h"

ThemeMetric<float> METER_WIDTH("CombinedLifeMeterTug", "MeterWidth");

static void TugMeterPercentChangeInit(
    size_t score_event, RString& name_out, float& default_value_out) {
  name_out =
      "TugMeterPercentChange" + ScoreEventToString((ScoreEvent)score_event);
  switch (score_event) {
    default:
      FAIL_M(ssprintf("Invalid ScoreEvent: %i", static_cast<int>(score_event)));
    case SE_W1:
      default_value_out = +0.010f;
      break;
    case SE_W2:
      default_value_out = +0.008f;
      break;
    case SE_W3:
      default_value_out = +0.004f;
      break;
    case SE_W4:
      default_value_out = +0.000f;
      break;
    case SE_W5:
      default_value_out = -0.010f;
      break;
    case SE_Miss:
      default_value_out = -0.020f;
      break;
    case SE_HitMine:
      default_value_out = -0.040f;
      break;
    case SE_CheckpointHit:
      default_value_out = +0.002f;
      break;
    case SE_CheckpointMiss:
      default_value_out = -0.002f;
      break;
    case SE_Held:
      default_value_out = +0.008f;
      break;
    case SE_LetGo:
      default_value_out = -0.020f;
      break;
    case SE_Missed:
      default_value_out = +0.000f;
      break;
  }
}

static Preference1D<float> g_fTugMeterPercentChange(
    TugMeterPercentChangeInit, NUM_ScoreEvent);

CombinedLifeMeterTug::CombinedLifeMeterTug() {
  FOREACH_PlayerNumber(p) {
    RString stream_path =
        THEME->GetPathG("CombinedLifeMeterTug", ssprintf("stream p%d", p + 1));
    RString tip_path =
        THEME->GetPathG("CombinedLifeMeterTug", ssprintf("tip p%d", p + 1));
    stream_[p].Load(stream_path, METER_WIDTH, tip_path);
    this->AddChild(&stream_[p]);
  }
  stream_[PLAYER_2].SetZoomX(-1);

  separator_.Load(THEME->GetPathG("CombinedLifeMeterTug", "separator"));
  separator_->SetName("Separator");
  LOAD_ALL_COMMANDS(separator_);
  this->AddChild(separator_);

  frame_.Load(THEME->GetPathG("CombinedLifeMeterTug", "frame"));
  frame_->SetName("Frame");
  LOAD_ALL_COMMANDS(frame_);
  this->AddChild(frame_);
}

void CombinedLifeMeterTug::Update(float fDelta) {
  float percent_to_show = GAMESTATE->tug_life_percent_p1_;
  CLAMP(percent_to_show, 0.f, 1.f);

  stream_[PLAYER_1].SetPercent(percent_to_show);
  stream_[PLAYER_2].SetPercent(1 - percent_to_show);

  float fSeparatorX =
      SCALE(percent_to_show, 0.f, 1.f, -METER_WIDTH / 2.f, +METER_WIDTH / 2.f);

  separator_->SetX(fSeparatorX);

  ActorFrame::Update(fDelta);
}

void CombinedLifeMeterTug::ChangeLife(PlayerNumber pn, TapNoteScore score) {
  float percent_to_move = 0;
  switch (score) {
    case TNS_W1:
      percent_to_move = g_fTugMeterPercentChange[SE_W1];
      break;
    case TNS_W2:
      percent_to_move = g_fTugMeterPercentChange[SE_W2];
      break;
    case TNS_W3:
      percent_to_move = g_fTugMeterPercentChange[SE_W3];
      break;
    case TNS_W4:
      percent_to_move = g_fTugMeterPercentChange[SE_W4];
      break;
    case TNS_W5:
      percent_to_move = g_fTugMeterPercentChange[SE_W5];
      break;
    case TNS_Miss:
      percent_to_move = g_fTugMeterPercentChange[SE_Miss];
      break;
    case TNS_HitMine:
      percent_to_move = g_fTugMeterPercentChange[SE_HitMine];
      break;
    case TNS_CheckpointHit:
      percent_to_move = g_fTugMeterPercentChange[SE_CheckpointHit];
      break;
    case TNS_CheckpointMiss:
      percent_to_move = g_fTugMeterPercentChange[SE_CheckpointMiss];
      break;
    default:
      FAIL_M(ssprintf("Invalid TapNotScore: %i", score));
  }

  ChangeLife(pn, percent_to_move);
}

void CombinedLifeMeterTug::HandleTapScoreNone(PlayerNumber pn) {}

void CombinedLifeMeterTug::ChangeLife(
    PlayerNumber pn, HoldNoteScore score, TapNoteScore tscore) {
  float percent_to_move = 0;
  switch (score) {
    case HNS_Held:
      percent_to_move = g_fTugMeterPercentChange[SE_Held];
      break;
    case HNS_LetGo:
      percent_to_move = g_fTugMeterPercentChange[SE_LetGo];
      break;
    case HNS_Missed:
      percent_to_move = g_fTugMeterPercentChange[SE_Missed];
      break;
    default:
      FAIL_M(ssprintf("Invalid HoldNoteScore: %i", score));
  }

  ChangeLife(pn, percent_to_move);
}

void CombinedLifeMeterTug::ChangeLife(PlayerNumber pn, float percent_to_move) {
  if (PREFSMAN->m_bMercifulDrain && percent_to_move < 0) {
    float life_percentage = 0;
    switch (pn) {
      case PLAYER_1:
        life_percentage = GAMESTATE->tug_life_percent_p1_;
        break;
      case PLAYER_2:
        life_percentage = 1 - GAMESTATE->tug_life_percent_p1_;
        break;
      default:
        FAIL_M(ssprintf("Invalid player number: %i", pn));
    }

    // Clamp the life meter only for calculating the multiplier.
    life_percentage = clamp(life_percentage, 0.0f, 1.0f);
    percent_to_move *= SCALE(life_percentage, 0.f, 1.f, 0.2f, 1.f);
  }

  switch (pn) {
    case PLAYER_1:
      GAMESTATE->tug_life_percent_p1_ += percent_to_move;
      break;
    case PLAYER_2:
      GAMESTATE->tug_life_percent_p1_ -= percent_to_move;
      break;
    default:
      FAIL_M(ssprintf("Invalid player number: %i", pn));
  }
}

void CombinedLifeMeterTug::SetLife(PlayerNumber pn, float value) {
  switch (pn) {
    case PLAYER_1:
      GAMESTATE->tug_life_percent_p1_ = value;
      break;
    case PLAYER_2:
      GAMESTATE->tug_life_percent_p1_ = 1 - value;
      break;
    default:
      FAIL_M(ssprintf("Invalid player number: %i", pn));
  }
}

/*
 * (c) 2003-2004 Chris Danford
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
