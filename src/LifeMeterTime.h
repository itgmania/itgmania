#ifndef LIFEMETERTIME_H
#define LIFEMETERTIME_H

#include "AutoActor.h"
#include "BitmapText.h"
#include "LifeMeter.h"
#include "MeterDisplay.h"
#include "PercentageDisplay.h"
#include "Quad.h"
#include "RageSound.h"
#include "Sprite.h"
#include "StreamDisplay.h"

// Battery life meter used in Survival.
class LifeMeterTime : public LifeMeter {
 public:
  LifeMeterTime();

  virtual ~LifeMeterTime();

  virtual void Load(
      const PlayerState* player_state, PlayerStageStats* player_stage_stats);

  virtual void Update(float delta);

  virtual void OnLoadSong();
  virtual void ChangeLife(TapNoteScore tns);
  virtual void ChangeLife(HoldNoteScore hns, TapNoteScore tns);
  virtual void ChangeLife(float delta);
  virtual void SetLife(float value);
  virtual void HandleTapScoreNone();
  virtual bool IsInDanger() const;
  virtual bool IsHot() const;
  virtual bool IsFailing() const;
  virtual float GetLife() const;

 private:
  float GetLifeSeconds() const;
  void SendLifeChangedMessage(
      float old_life, TapNoteScore tns, HoldNoteScore hns);

  AutoActor background_;
  Quad danger_glow_;
  StreamDisplay* stream_;
  AutoActor frame_;

  float life_total_gained_seconds_;
  float life_total_lost_seconds_;
  // The sound played when time is gained at the start of each Song.
  RageSound sound_gain_life_;
};

#endif  // LIFEMETERTIME_H

/**
 * @file
 * @author Chris Danford (c) 2001-2004
 * @section LICENSE
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
