#ifndef LIFEMETERBATTERY_H
#define LIFEMETERBATTERY_H

#include "AutoActor.h"
#include "BitmapText.h"
#include "LifeMeter.h"
#include "PercentageDisplay.h"
#include "RageSound.h"
#include "Sprite.h"
#include "ThemeMetric.h"

// Battery life meter used in Oni mode.
class LifeMeterBattery : public LifeMeter {
 public:
  LifeMeterBattery();

  virtual void Load(
      const PlayerState* player_state, PlayerStageStats* player_stage_stats);

  virtual void Update(float delat);

  virtual void OnSongEnded();
  virtual void ChangeLife(TapNoteScore tns);
  virtual void ChangeLife(HoldNoteScore hns, TapNoteScore tns);
  virtual void ChangeLife(float delta_life_percent);
  virtual void SetLife(float value);
  virtual void HandleTapScoreNone();
  virtual bool IsInDanger() const;
  virtual bool IsHot() const;
  virtual bool IsFailing() const;
  virtual float GetLife() const;
  virtual int GetRemainingLives() const;

  virtual void BroadcastLifeChanged(bool lost_life);

  void Refresh();
  int GetLivesLeft() { return lives_left_; }
  int GetTotalLives();
  void ChangeLives(int life_diff);

  // Lua
  virtual void PushSelf(lua_State* L);

 private:
  void SubtractLives(int lives);
  void AddLives(int lives);

	// Dead when 0.
  int lives_left_;
	// Lags lives_left_
  int trailing_lives_left;

  ThemeMetric<float> BATTERY_BLINK_TIME;
  ThemeMetric<TapNoteScore> MIN_SCORE_TO_KEEP_LIFE;
  ThemeMetric<int> DANGER_THRESHOLD;
  ThemeMetric<int> MAX_LIVES;
  ThemeMetric<int> SUBTRACT_LIVES;
  ThemeMetric<int> MINES_SUBTRACT_LIVES;
  ThemeMetric<int> HELD_ADD_LIVES;
  ThemeMetric<int> LET_GO_SUBTRACT_LIVES;
  ThemeMetric<LuaReference> COURSE_SONG_REWARD_LIVES;
  ThemeMetric<RString> LIVES_FORMAT;

  AutoActor frame_;
  AutoActor battery_;
  BitmapText text_num_lives_;

  PercentageDisplay percent_;

  // The sound played when a Player loses a life.
  RageSound sound_lose_life_;
  // The sound played when a Player gains a life.
  RageSound sound_gain_life_;
};

#endif  // LIFEMETERBATTERY_H

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
