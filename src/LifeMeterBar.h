#ifndef LIFEMETERBAR_H
#define LIFEMETERBAR_H

#include "AutoActor.h"
#include "LifeMeter.h"
#include "Quad.h"
#include "Sprite.h"
#include "StreamDisplay.h"
#include "ThemeMetric.h"

// The player's life represented as a bar.
class LifeMeterBar : public LifeMeter {
 public:
  LifeMeterBar();
  ~LifeMeterBar();

  virtual void Load(
      const PlayerState* player_state, PlayerStageStats* player_stage_stats);

  virtual void Update(float delta);

  virtual void ChangeLife(TapNoteScore tns);
  virtual void ChangeLife(HoldNoteScore hns, TapNoteScore tns);
  virtual void ChangeLife(float delta_life_percent);
  virtual void SetLife(float value);
  virtual void HandleTapScoreNone();
  virtual void AfterLifeChanged();
  virtual bool IsInDanger() const;
  virtual bool IsHot() const;
  virtual bool IsFailing() const;
  virtual float GetLife() const { return life_percentage_; }

  void UpdateNonstopLifebar();
  void FillForHowToPlay(int num_w2s, int num_misses);
  // this function is solely for HowToPlay

 private:
  ThemeMetric<float> DANGER_THRESHOLD;
  ThemeMetric<float> INITIAL_VALUE;
  ThemeMetric<float> HOT_VALUE;
  ThemeMetric<float> LIFE_MULTIPLIER;
  ThemeMetric<bool> FORCE_LIFE_DIFFICULTY_ON_EXTRA_STAGE;
  ThemeMetric<TapNoteScore> MIN_STAY_ALIVE;
  ThemeMetric<float> EXTRA_STAGE_LIFE_DIFFICULTY;

  ThemeMetric1D<float> life_percent_change_;

  AutoActor under_;
  AutoActor danger_;
  StreamDisplay* stream_;
  AutoActor over_;

  float life_percentage_;

  float passing_alpha_;
  float hot_alpha_;

  bool merciful_beginner_in_effect_;
  float base_life_difficulty_;
	// Essentially same as pref
  float life_difficulty_;

	// cached from prefs
  int progressive_life_bar_;
  // The current number of progressive W5/miss rankings.
  int miss_combo_;
  // The combo needed before the life bar starts to fill up after a Player
	// failed.
  int combo_to_regain_life_;
};

#endif  // LIFEMETERBAR_H

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
