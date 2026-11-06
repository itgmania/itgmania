#include "ScoreDisplayOni.h"

#include "ActorUtil.h"
#include "GameState.h"
#include "PlayerNumber.h"
#include "PlayerStageStats.h"
#include "PlayerState.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "ScoreDisplay.h"
#include "StatsManager.h"
#include "ThemeManager.h"

ScoreDisplayOni::ScoreDisplayOni() {
  LOG->Trace("ScoreDisplayOni::ScoreDisplayOni()");

  spr_frame_.Load(THEME->GetPathG("ScoreDisplayOni", "frame"));
  this->AddChild(&spr_frame_);

  // init the text
  text_.LoadFromFont(THEME->GetPathF("ScoreDisplayOni", "numbers"));
  text_.SetName("ScoreDisplayOni Numbers");
  text_.SetShadowLength(0);
  LOAD_ALL_COMMANDS(text_);
  this->AddChild(&text_);
}

void ScoreDisplayOni::Init(
    const PlayerState* pPlayerState,
    const PlayerStageStats* pPlayerStageStats) {
  ScoreDisplay::Init(pPlayerState, pPlayerStageStats);
}

void ScoreDisplayOni::Update(float fDelta) {
  ScoreDisplay::Update(fDelta);

  // TODO: Remove use of PlayerNumber.
  PlayerNumber pn = player_state_->player_number_;

  float fSecsIntoPlay = 0;
  if (GAMESTATE->IsPlayerEnabled(pn)) {
    fSecsIntoPlay = STATSMAN->cur_stage_stats_.player_[pn].alive_seconds_;
  }

  text_.SetText(SecondsToMMSSMsMs(fSecsIntoPlay));
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
