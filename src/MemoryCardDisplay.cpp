#include "MemoryCardDisplay.h"

#include <string>

#include "ActorFrame.h"
#include "ActorUtil.h"
#include "GameConstantsAndTypes.h"
#include "LuaManager.h"
#include "MemoryCardManager.h"
#include "PlayerNumber.h"
#include "RageUtil.h"
#include "ThemeManager.h"
#include "XmlFile.h"

REGISTER_ACTOR_CLASS(MemoryCardDisplay);

MemoryCardDisplay::MemoryCardDisplay() {
  player_number_ = kPlayerInvalid;
  last_seen_state_ = MemoryCardState_Invalid;
}

void MemoryCardDisplay::Load(PlayerNumber pn) {
  player_number_ = pn;

  for (int i = 0; i < NUM_MemoryCardState; i++) {
    MemoryCardState mcs = (MemoryCardState)i;
    std::string state = MemoryCardStateToString(mcs);
    spr_[i].Load(THEME->GetPathG(
        "MemoryCardDisplay", ssprintf("%s p%d", state.c_str(), pn + 1)));
    spr_[i].SetVisible(false);
    this->AddChild(&spr_[i]);
  }
}

void MemoryCardDisplay::LoadFromNode(const XNode* node) {
  Lua* L = LUA->Get();
  node->PushAttrValue(L, "PlayerNumber");
  PlayerNumber pn;
  LuaHelpers::Pop(L, pn);
  LUA->Release(L);

  Load(pn);

  ActorFrame::LoadFromNode(node);
}

void MemoryCardDisplay::Update(float delta) {
  MemoryCardState new_mcs = MEMCARDMAN->GetCardState(player_number_);
  if (last_seen_state_ != new_mcs) {
    if (last_seen_state_ != MemoryCardState_Invalid) {
      spr_[last_seen_state_].SetVisible(false);
    }
    last_seen_state_ = new_mcs;
    spr_[last_seen_state_].SetVisible(true);
  }

  ActorFrame::Update(delta);
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
