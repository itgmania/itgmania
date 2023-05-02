#ifndef INVENTORY_H
#define INVENTORY_H

#include <vector>

#include "Actor.h"
#include "PlayerNumber.h"
#include "PlayerState.h"
#include "RageSound.h"
#include "ScreenMessage.h"

AutoScreenMessage(SM_BattleDamageLevel1);
AutoScreenMessage(SM_BattleDamageLevel2);
AutoScreenMessage(SM_BattleDamageLevel3);

// Inventory management for PLAY_MODE_BATTLE.
class Inventory : public Actor {
 public:
  Inventory();
  ~Inventory();
  void Load(PlayerState* player_state);

  virtual void Update(float delta);
  virtual void DrawPrimitives() {};

  void UseItem(int slot);

 protected:
  void AwardItem(int item_index);

  PlayerState* player_state_;
  unsigned int last_seen_combo_;

  // A sound played when an item has been acquired.
  RageSound sound_acquire_item_;
  std::vector<RageSound*> sound_use_item_;
  RageSound sound_item_ending_;
};

#endif  // INVENTORY_H

/**
 * @file
 * @author Chris Danford (c) 2003
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
