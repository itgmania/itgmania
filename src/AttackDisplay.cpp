#include "global.h"

#include "AttackDisplay.h"

#include <set>

#include "ActorUtil.h"
#include "Character.h"
#include "GameState.h"
#include "PlayerState.h"
#include "RageLog.h"
#include "ThemeManager.h"

RString GetAttackPieceName(const RString& attack) {
  RString ret = ssprintf("attack %s", attack.c_str());

  // 1.5x -> 1_5x.  If we pass a period to THEME->GetPathTo, it'll think
  // we're looking for a specific file and not search.
  ret.Replace(".", "_");

  return ret;
}

AttackDisplay::AttackDisplay() {
  if (GAMESTATE->play_mode_ != PLAY_MODE_BATTLE &&
      GAMESTATE->play_mode_ != PLAY_MODE_RAVE) {
    return;
	}

	// Invisible
  sprite_.SetDiffuseAlpha(0);
  this->AddChild(&sprite_);
}

void AttackDisplay::Init(const PlayerState* player_state) {
  player_state_ = player_state;

  // TODO: Remove use of PlayerNumber.
  PlayerNumber pn = player_state_->m_PlayerNumber;
  sprite_.SetName(ssprintf("TextP%d", pn + 1));

  if (GAMESTATE->play_mode_ != PLAY_MODE_BATTLE &&
      GAMESTATE->play_mode_ != PLAY_MODE_RAVE) {
    return;
	}

  std::set<RString> attacks;
  for (int al = 0; al < NUM_ATTACK_LEVELS; ++al) {
    const Character* ch = GAMESTATE->cur_characters_[pn];
    ASSERT(ch != nullptr);
    const RString* asAttacks = ch->attacks_[al];
    for (int att = 0; att < NUM_ATTACKS_PER_LEVEL; ++att) {
      attacks.insert(asAttacks[att]);
		}
  }

  for (auto it = attacks.begin(); it != attacks.end(); ++it) {
    const RString path =
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

  if (!player_state_->m_bAttackBeganThisUpdate) {
		return;
	}
  // don't handle this again

  for (unsigned s = 0; s < player_state_->m_ActiveAttacks.size(); ++s) {
    const Attack& attack = player_state_->m_ActiveAttacks[s];

		// Hasn't started yet.
    if (attack.fStartSecond >= 0) {
			continue;
		}

		// Ended already
    if (attack.fSecsRemaining <= 0) {
			continue;
		}

    if (attack.IsBlank()) {
			continue;
		}

    SetAttack(attack.sModifiers);
    break;
  }
}

void AttackDisplay::SetAttack(const RString& text) {
  const RString path =
      THEME->GetPathG("AttackDisplay", GetAttackPieceName(text), true);
  if (path == "") {
		return;
	}

  sprite_.SetDiffuseAlpha(1);
  sprite_.Load(path);

  // TODO: Remove use of PlayerNumber.
  PlayerNumber pn = player_state_->m_PlayerNumber;

  const RString sName = ssprintf("%sP%i", text.c_str(), pn + 1);
  sprite_.RunCommands(
      THEME->GetMetricA("AttackDisplay", sName + "OnCommand"));
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
