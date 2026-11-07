#include "ScoreDisplayRave.h"

#include "ActorUtil.h"
#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "PlayerNumber.h"
#include "PlayerState.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "ScoreDisplay.h"
#include "ThemeManager.h"

ScoreDisplayRave::ScoreDisplayRave() {
  LOG->Trace("ScoreDisplayRave::ScoreDisplayRave()");

  last_level_seen_ = ATTACK_LEVEL_1;

  for (int i = 0; i < NUM_ATTACK_LEVELS; i++) {
    spr_meter_[i].Load(
        THEME->GetPathG("ScoreDisplayRave", ssprintf("stream level%d", i + 1)));
    spr_meter_[i].SetCropRight(1.f);
  }

  text_level_.LoadFromFont(THEME->GetPathF("ScoreDisplayRave", "level"));
  text_level_.SetText("1");
}

void ScoreDisplayRave::Init(
    const PlayerState* player_state,
    const PlayerStageStats* player_stage_stats) {
  ScoreDisplay::Init(player_state, player_stage_stats);

  PlayerNumber pn = player_state->player_number_;

  spr_frame_base_.Load(
      THEME->GetPathG("ScoreDisplayRave", ssprintf("frame base p%d", pn + 1)));
  spr_frame_base_->SetName(ssprintf("FrameBaseP%d", pn + 1));
  LOAD_ALL_COMMANDS_AND_SET_XY(spr_frame_base_);
  this->AddChild(spr_frame_base_);

  for (int i = 0; i < NUM_ATTACK_LEVELS; i++) {
    spr_meter_[i].SetName(ssprintf("MeterP%d", pn + 1));
    LOAD_ALL_COMMANDS_AND_SET_XY(spr_meter_[i]);
    this->AddChild(&spr_meter_[i]);
  }

  text_level_.SetName(ssprintf("LevelP%d", pn + 1));
  LOAD_ALL_COMMANDS_AND_SET_XY(text_level_);
  this->AddChild(&text_level_);

  spr_frame_overlay_.Load(THEME->GetPathG(
      "ScoreDisplayRave", ssprintf("frame overlay p%d", pn + 1)));
  spr_frame_overlay_->SetName(ssprintf("FrameOverP%d", pn + 1));
  LOAD_ALL_COMMANDS_AND_SET_XY(spr_frame_overlay_);
  this->AddChild(spr_frame_overlay_);
}

void ScoreDisplayRave::Update(float delta) {
  ScoreDisplay::Update(delta);

  float f_level = player_state_->super_meter_;
  AttackLevel level = (AttackLevel)(int)f_level;

  if (level != last_level_seen_) {
    spr_meter_[last_level_seen_].SetCropRight(1.f);
    last_level_seen_ = level;
    text_level_.SetText(ssprintf("%d", level + 1));
  }

  float percent = f_level - Enum::to_integral(level);
  spr_meter_[level].SetCropRight(1 - percent);
}

/*
 * (c) 2003 Chris Danford
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
