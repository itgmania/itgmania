#ifndef SCORE_DISPLAY_RAVE_H
#define SCORE_DISPLAY_RAVE_H

#include "AutoActor.h"
#include "BitmapText.h"
#include "GameConstantsAndTypes.h"
#include "ScoreDisplay.h"
#include "Sprite.h"

/** @brief ScoreDisplay implementation for PLAY_MODE_RAVE. */
class ScoreDisplayRave : public ScoreDisplay {
 public:
  ScoreDisplayRave();
  virtual void Init(
      const PlayerState* player_state,
      const PlayerStageStats* player_stage_stats);

  virtual void Update(float delta);

 protected:
  AutoActor spr_frame_base_;
  Sprite spr_meter_[NUM_ATTACK_LEVELS];
  AutoActor spr_frame_overlay_;
  BitmapText text_level_;

  AttackLevel last_level_seen_;
};

#endif

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
