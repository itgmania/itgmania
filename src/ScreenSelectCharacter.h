/* ScreenSelectCharacter - Deprecated. Replaced by ScreenSelectMaster. */

#ifndef SCREEN_SELECT_CHARACTER_H
#define SCREEN_SELECT_CHARACTER_H

#include "Banner.h"
#include "GameConstantsAndTypes.h"
#include "InputQueue.h"
#include "ModIcon.h"
#include "PlayerNumber.h"
#include "RageSound.h"
#include "ScreenMessage.h"
#include "ScreenWithMenuElements.h"
#include "Sprite.h"

#define MAX_CHAR_ICONS_TO_SHOW 11

class ScreenSelectCharacter : public ScreenWithMenuElements {
 public:
  virtual void Init();
  virtual ~ScreenSelectCharacter();

  virtual bool Input(const InputEventPlus& input);
  virtual void HandleScreenMessage(const ScreenMessage sm);

  bool MenuLeft(const InputEventPlus& input);
  bool MenuRight(const InputEventPlus& input);
  bool MenuUp(const InputEventPlus& input);
  bool MenuDown(const InputEventPlus& input);
  bool MenuStart(const InputEventPlus& input);
  bool MenuBack(const InputEventPlus& input);

  void TweenOffScreen();

 private:
  // These functions take the PlayerNumber of the player making the selections,
  // which is not necessarily the same as the PlayerNumber that the options are
  // being chosen for.  If only one player is joined, they will pick their
  // choices, then the CPU's choices.
  void BeforeRowChange(PlayerNumber pn);
  void AfterRowChange(PlayerNumber pn);
  void AfterValueChange(PlayerNumber pn);
  void Move(PlayerNumber pn, int delta_value);
  bool AllAreFinishedChoosing() const;
  void MakeSelection(PlayerNumber pn);

  int selected_character_[NUM_PLAYERS];
  enum {
    CHOOSING_HUMAN_CHARACTER,
    CHOOSING_CPU_CHARACTER,
    FINISHED_CHOOSING
  } selection_row_[NUM_PLAYERS];
  PlayerNumber GetAffectedPlayerNumber(
      PlayerNumber
          pn);  // returns the number of the player that pn is selecting for

  Sprite spr_title_[NUM_PLAYERS];

  Banner spr_icons_[NUM_PLAYERS][MAX_CHAR_ICONS_TO_SHOW];

  Sprite spr_card_[NUM_PLAYERS];
  Sprite spr_card_arrows_[NUM_PLAYERS];

  Sprite spr_attack_frame_[NUM_PLAYERS];
  ModIcon attack_icons_[NUM_PLAYERS][NUM_ATTACK_LEVELS][NUM_ATTACKS_PER_LEVEL];

  Sprite spr_explanation_;

  RageSound sound_change_;
};

#endif

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
