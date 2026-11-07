#include "ScoreDisplayAliveTime.h"

#include "ActorUtil.h"
#include "BitmapText.h"
#include "GameState.h"
#include "LuaManager.h"
#include "MessageManager.h"
#include "PlayerNumber.h"
#include "RageUtil.h"
#include "StageStats.h"
#include "StatsManager.h"
#include "XmlFile.h"
#include "global.h"

REGISTER_ACTOR_CLASS(ScoreDisplayAliveTime);

ScoreDisplayAliveTime::ScoreDisplayAliveTime() {
  player_number_ = PLAYER_INVALID;
}

ScoreDisplayAliveTime::~ScoreDisplayAliveTime() {}

void ScoreDisplayAliveTime::LoadFromNode(const XNode* node) {
  BitmapText::LoadFromNode(node);

  {
    Lua* L = LUA->Get();
    bool b = node->PushAttrValue(L, "PlayerNumber");
    LuaHelpers::Pop(L, player_number_);
    ASSERT(b);
    LUA->Release(L);
  }
}

void ScoreDisplayAliveTime::Update(float delta) {
  UpdateNumber();
  BitmapText::Update(delta);
}

void ScoreDisplayAliveTime::HandleMessage(const Message& msg) {
  // TODO: Add handling of GoalComplete message
  BitmapText::HandleMessage(msg);
}

void ScoreDisplayAliveTime::UpdateNumber() {
  float secs_into_play = 0;
  ASSERT(player_number_ != PLAYER_INVALID);
  if (player_number_ != PLAYER_INVALID &&
      GAMESTATE->IsPlayerEnabled(player_number_)) {
    secs_into_play =
        STATSMAN->GetAccumPlayedStageStats()
            .player_[player_number_]
            .alive_seconds_ +
        STATSMAN->cur_stage_stats_.player_[player_number_].alive_seconds_;
  }

  SetText(SecondsToMMSSMsMs(secs_into_play));
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the ScoreDisplayAliveTime. */
class LunaScoreDisplayAliveTime : public Luna<ScoreDisplayAliveTime> {
 public:
  LunaScoreDisplayAliveTime() {}
};

LUA_REGISTER_DERIVED_CLASS(ScoreDisplayAliveTime, BitmapText)

// lua end

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
