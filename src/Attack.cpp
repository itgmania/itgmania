#include "Attack.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "GameConstantsAndTypes.h"
#include "GameState.h"
#include "PlayerOptions.h"
#include "PlayerState.h"
#include "RageUtil.h"
#include "Song.h"
#include "global.h"

void Attack::GetAttackBeats(
    const Song* song, float& start_beat, float& end_beat) const {
  ASSERT(song != nullptr);
  ASSERT_M(start_second_ >= 0, ssprintf("StartSecond: %f", start_second_));

  const TimingData& timing = song->song_timing_;
  start_beat = timing.GetBeatFromElapsedTime(start_second_);
  end_beat = timing.GetBeatFromElapsedTime(start_second_ + secs_remaining_);
}

/* Get the range for an attack that's being applied in realtime, eg. during
 * battle mode.  We need a PlayerState for this, so we can push the region
 * off-screen to prevent popping when the attack has note modifers. */
void Attack::GetRealtimeAttackBeats(
    const Song* song, const PlayerState* player_state, float& start_beat,
    float& end_beat) const {
  ASSERT(song != nullptr);

  if (start_second_ >= 0) {
    GetAttackBeats(song, start_beat, end_beat);
    return;
  }

  ASSERT(player_state != nullptr);

  /* If reasonable, push the attack forward 8 beats so that notes on screen
   * don't change suddenly. */
  start_beat = std::min(
      GAMESTATE->position_.song_beat_ + 8, player_state->last_drawn_beat_);
  start_beat = std::trunc(start_beat) + 1;

  const TimingData& timing = song->song_timing_;
  const float start_second = timing.GetElapsedTimeFromBeat(start_beat);
  const float end_second = start_second + secs_remaining_;
  end_beat = timing.GetBeatFromElapsedTime(end_second);
  end_beat = std::trunc(end_beat) + 1;

  // loading the course should have caught this.
  ASSERT_M(
      end_beat >= start_beat,
      ssprintf("EndBeat %f >= StartBeat %f", end_beat, start_beat));
}

bool Attack::ContainsTransformOrTurn() const {
  PlayerOptions po;
  po.FromString(modifiers_);
  return po.ContainsTransformOrTurn();
}

Attack Attack::FromGlobalCourseModifier(const std::string& modifiers) {
  Attack a;
  a.start_second_ = 0;
  a.secs_remaining_ = 10000; /* whole song */
  a.level_ = ATTACK_LEVEL_1;
  a.modifiers_ = modifiers;
  a.global_ = true;
  return a;
}

std::string Attack::GetTextDescription() const {
  std::string s =
      modifiers_ + " " + ssprintf("(%.2f seconds)", secs_remaining_);
  return s;
}

int Attack::GetNumAttacks() const {
  std::vector<std::string> tmp;
  split(this->modifiers_, ",", tmp);
  return tmp.size();
}

bool AttackArray::ContainsTransformOrTurn() const {
  return std::any_of((*this).begin(), (*this).end(), [](const Attack& a) {
    return a.ContainsTransformOrTurn();
  });
}

std::vector<std::string> AttackArray::ToVectorString() const {
  std::vector<std::string> ret;
  for (const Attack& a : *this) {
    ret.push_back(ssprintf(
        "TIME=%f:LEN=%f:MODS=%s", a.start_second_, a.secs_remaining_,
        a.modifiers_.c_str()));
  }
  return ret;
}

void AttackArray::UpdateStartTimes(float delta) {
  for (Attack& a : *this) {
    a.start_second_ += delta;
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
