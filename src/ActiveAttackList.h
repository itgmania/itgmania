#ifndef ACTIVE_ATTACK_LIST_H
#define ACTIVE_ATTACK_LIST_H

#include "BitmapText.h"
#include "PlayerState.h"

// Shows currently active Player modifiers during gameplay.
class ActiveAttackList : public BitmapText {
 public:
  // The constructor that does nothing.
  ActiveAttackList();

  // Set up the PlayerState.
  // player_state is the PlayerState involved with the attacks.
  void Init(const PlayerState* player_state);

  // Look into updating the list.
  // fDelta is the present time.
  virtual void Update(float delta);

  // Refresh the list of attacks.
  void Refresh();

 protected:
  // The PlayerState of the Player who is dealing with the attack list.
  const PlayerState* player_state_;
};

#endif  // ACTIVE_ATTACK_LIST_H

/**
 * @file
 * @author Chris Danford (c) 2004
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
