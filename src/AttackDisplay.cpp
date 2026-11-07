#include "AttackDisplay.h"

#include <set>
#include <string>

#include "ActorFrame.h"
#include "Attack.h"
#include "Character.h"
#include "GameConstantsAndTypes.h"
#include "GameState.h"
#include "PlayerNumber.h"
#include "PlayerState.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "StdString.h"
#include "ThemeManager.h"
#include "global.h"

std::string GetAttackPieceName(const std::string& attack) {
  std::string ret = ssprintf("attack %s", attack.c_str());

  /* 1.5x -> 1_5x.  If we pass a period to THEME->GetPathTo, it'll think
   * we're looking for a specific file and not search. */
  Replace(ret, ".", "_");

  return ret;
}

AttackDisplay::AttackDisplay() {
  if (GAMESTATE->play_mode_ != PLAY_MODE_BATTLE &&
      GAMESTATE->play_mode_ != PLAY_MODE_RAVE) {
    return;
  }

  spr_attack_.SetDiffuseAlpha(0);  // invisible
  this->AddChild(&spr_attack_);
}

void AttackDisplay::Init(const PlayerState* player_state) {
  player_state_ = player_state;

  // TODO: Remove use of PlayerNumber.
  PlayerNumber pn = player_state_->player_number_;
  spr_attack_.SetName(ssprintf("TextP%d", pn + 1));

  if (GAMESTATE->play_mode_ != PLAY_MODE_BATTLE &&
      GAMESTATE->play_mode_ != PLAY_MODE_RAVE) {
    return;
  }

  std::set<std::string> attacks;
  for (int al = 0; al < NUM_ATTACK_LEVELS; al++) {
    const Character* ch = GAMESTATE->cur_characters_[pn];
    ASSERT(ch != nullptr);
    const std::string* as_attacks = ch->attacks_[al];
    for (int att = 0; att < NUM_ATTACKS_PER_LEVEL; ++att) {
      attacks.insert(as_attacks[att]);
    }
  }

  for (std::set<std::string>::const_iterator it = attacks.begin();
       it != attacks.end(); ++it) {
    const std::string path =
        THEME->GetPathG("AttackDisplay", GetAttackPieceName(*it), true);
    if (path == "") {
      LOG->Trace("Couldn't find \"%s\"", GetAttackPieceName(*it).c_str());
      continue;
    }

    texture_preload_.Load(path);
  }
}

void AttackDisplay::Update(float delta) {
  ActorFrame::Update(delta);

  if (GAMESTATE->play_mode_ != PLAY_MODE_BATTLE &&
      GAMESTATE->play_mode_ != PLAY_MODE_RAVE) {
    return;
  }

  if (!player_state_->attack_began_this_update_) {
    return;
  }
  // don't handle this again

  for (unsigned s = 0; s < player_state_->active_attacks_.size(); s++) {
    const Attack& attack = player_state_->active_attacks_[s];

    if (attack.start_second_ >= 0) {
      continue; /* hasn't started yet */
    }

    if (attack.secs_remaining_ <= 0) {
      continue; /* ended already */
    }

    if (attack.IsBlank()) {
      continue;
    }

    SetAttack(attack.modifiers_);
    break;
  }
}

void AttackDisplay::SetAttack(const std::string& text) {
  const std::string path =
      THEME->GetPathG("AttackDisplay", GetAttackPieceName(text), true);
  if (path == "") {
    return;
  }

  spr_attack_.SetDiffuseAlpha(1);
  spr_attack_.Load(path);

  // TODO: Remove use of PlayerNumber.
  PlayerNumber pn = player_state_->player_number_;

  const std::string name = ssprintf("%sP%i", text.c_str(), pn + 1);
  spr_attack_.RunCommands(
      THEME->GetMetricA("AttackDisplay", name + "OnCommand"));
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
