#ifndef ATTACK_H
#define ATTACK_H

#define ATTACK_STARTS_NOW (-10000.f)

#include <string>
#include <vector>

#include "GameConstantsAndTypes.h"

class Song;
class PlayerState;
/** @brief An action made against a Player to make things more difficult. */
struct Attack {
  AttackLevel level_;
  /**
   * @brief the starting point of this attack.
   *
   * If this is -1, then the attack starts now. */
  float start_second_;
  /** @brief How long does this attack last? */
  float secs_remaining_;
  /** @brief The modifiers used for this attack. */
  std::string modifiers_;
  bool on_;      // set and used by GAMESTATE
  bool global_;  // true for song-wide course mods
  bool show_in_attack_list_;

  void MakeBlank() {
    level_ = ATTACK_LEVEL_1;
    start_second_ = ATTACK_STARTS_NOW;
    secs_remaining_ = 0;
    modifiers_ = std::string();
    on_ = false;
    global_ = false;
    show_in_attack_list_ = true;
  }
  Attack()
      : level_(ATTACK_LEVEL_1),
        start_second_(ATTACK_STARTS_NOW),
        secs_remaining_(0),
        modifiers_(std::string()),
        on_(false),
        global_(false),
        show_in_attack_list_(true) {
  }  // MakeBlank() is effectively called here.
  Attack(
      AttackLevel level, float start_second, float secs_remaining,
      std::string modifiers, bool on, bool global,
      bool show_in_attack_list = true)
      : level_(level),
        start_second_(start_second),
        secs_remaining_(secs_remaining),
        modifiers_(modifiers),
        on_(on),
        global_(global),
        show_in_attack_list_(show_in_attack_list) {}

  void GetAttackBeats(
      const Song* song, float& start_beat, float& end_beat) const;
  void GetRealtimeAttackBeats(
      const Song* song, const PlayerState* player_state, float& start_beat,
      float& end_beat) const;
  /**
   * @brief Determine if this attack has no modifiers, and is thus blank or
   * empty.
   * @return true if it is blank/empty, or false otherwise. */
  bool IsBlank() const { return modifiers_.empty(); }
  bool ContainsTransformOrTurn() const;
  static Attack FromGlobalCourseModifier(const std::string& modifiers);
  std::string GetTextDescription() const;

  int GetNumAttacks() const;
};

struct AttackArray : public std::vector<Attack> {
  /**
   * @brief Determine if the list of attacks contains a transform or turn mod.
   * @return true if it does, or false otherwise. */
  bool ContainsTransformOrTurn() const;

  /**
   * @brief Return a string representation used for simfiles.
   * @return the string representation. */
  std::vector<std::string> ToVectorString() const;

  /**
   * @brief Adjust the starting time of all attacks.
   * @param delta the amount to change. */
  void UpdateStartTimes(float delta);
};

#endif

/**
 * @file
 * @author Chris Danford (c) 2003-2004
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
