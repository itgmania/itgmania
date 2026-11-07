#include "ScoreDisplayCalories.h"

#include "ActorUtil.h"
#include "BitmapText.h"
#include "LuaManager.h"
#include "MessageManager.h"
#include "PlayerNumber.h"
#include "RollingNumbers.h"
#include "StageStats.h"
#include "StatsManager.h"
#include "XmlFile.h"
#include "global.h"

REGISTER_ACTOR_CLASS(ScoreDisplayCalories);

ScoreDisplayCalories::ScoreDisplayCalories() {}

ScoreDisplayCalories::~ScoreDisplayCalories() {
  if (message_on_step_ != "") {
    MESSAGEMAN->Unsubscribe(this, message_on_step_);
  }
}

void ScoreDisplayCalories::LoadFromNode(const XNode* node) {
  RollingNumbers::LoadFromNode(node);

  {
    Lua* L = LUA->Get();
    bool b = node->PushAttrValue(L, "PlayerNumber");
    ASSERT(b);
    LuaHelpers::Pop(L, player_number_);
    LUA->Release(L);
  }

  MESSAGEMAN->Subscribe(this, "Step");
}

void ScoreDisplayCalories::Update(float delta) {
  // We have to set the initial text after
  // StatsManager::CalcAccumPlayedStageStats is called.
  if (IsFirstUpdate()) {
    UpdateNumber();
  }

  RollingNumbers::Update(delta);
}

void ScoreDisplayCalories::HandleMessage(const Message& msg) {
  if (msg.GetName() == "Step") {
    PlayerNumber pn;
    msg.GetParam("PlayerNumber", pn);
    if (pn == player_number_) {
      UpdateNumber();
    }
  }

  RollingNumbers::HandleMessage(msg);
}

void ScoreDisplayCalories::UpdateNumber() {
  float cals =
      STATSMAN->GetAccumPlayedStageStats()
          .player_[player_number_]
          .calories_burned_ +
      STATSMAN->cur_stage_stats_.player_[player_number_].calories_burned_;

  SetTargetNumber(cals);
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the ScoreDisplayCalories. */
class LunaScoreDisplayCalories : public Luna<ScoreDisplayCalories> {
 public:
  LunaScoreDisplayCalories() {}
};

LUA_REGISTER_DERIVED_CLASS(ScoreDisplayCalories, BitmapText)

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
