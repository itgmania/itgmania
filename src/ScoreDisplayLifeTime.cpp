#include "ScoreDisplayLifeTime.h"

#include <string>

#include "ActorUtil.h"
#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "PlayerStageStats.h"
#include "PlayerState.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "ScoreDisplay.h"
#include "ThemeManager.h"

ScoreDisplayLifeTime::ScoreDisplayLifeTime() {
  LOG->Trace("ScoreDisplayLifeTime::ScoreDisplayLifeTime()");
}

void ScoreDisplayLifeTime::Init(
    const PlayerState* player_state,
    const PlayerStageStats* player_stage_stats) {
  ScoreDisplay::Init(player_state, player_stage_stats);

  const std::string type = "ScoreDisplayLifeTime";

  spr_frame_.Load(THEME->GetPathG(type, "frame"));
  spr_frame_->SetName("Frame");
  this->AddChild(spr_frame_);
  ActorUtil::LoadAllCommandsAndOnCommand(spr_frame_, type);

  text_time_remaining_.LoadFromFont(THEME->GetPathF(type, "TimeRemaining"));
  text_time_remaining_.SetName("TimeRemaining");
  this->AddChild(&text_time_remaining_);
  ActorUtil::LoadAllCommandsAndOnCommand(text_time_remaining_, type);

  text_delta_seconds_.LoadFromFont(THEME->GetPathF(type, "DeltaSeconds"));
  text_delta_seconds_.SetName("DeltaSeconds");
  this->AddChild(&text_delta_seconds_);
  ActorUtil::LoadAllCommandsAndOnCommand(text_delta_seconds_, type);

  FOREACH_ENUM(TapNoteScore, tns) {
    const std::string& command = TapNoteScoreToString(tns);
    if (!text_delta_seconds_.HasCommand(command)) {
      ActorUtil::LoadCommand(text_delta_seconds_, type, command);
    }
  }
  FOREACH_ENUM(HoldNoteScore, hns) {
    const std::string& command = HoldNoteScoreToString(hns);
    if (!text_delta_seconds_.HasCommand(command)) {
      ActorUtil::LoadCommand(text_delta_seconds_, type, command);
    }
  }
}

void ScoreDisplayLifeTime::Update(float delta) {
  ScoreDisplay::Update(delta);

  float secs = player_stage_stats_->life_remaining_seconds_;

  std::string s = SecondsToMSSMsMs(secs);
  text_time_remaining_.SetText(s);
}

void ScoreDisplayLifeTime::OnLoadSong() {}

void ScoreDisplayLifeTime::OnJudgment(TapNoteScore tns) {}

void ScoreDisplayLifeTime::OnJudgment(HoldNoteScore hns, TapNoteScore tns) {}

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
