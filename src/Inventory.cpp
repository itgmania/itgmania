#include "global.h"

#include "Inventory.h"

#include <vector>

#include "GameState.h"
#include "PlayerState.h"
#include "PrefsManager.h"
#include "RageTimer.h"
#include "RageUtil.h"
#include "ScreenManager.h"
#include "Song.h"
#include "StatsManager.h"
#include "ThemeManager.h"
#include "ThemeMetric.h"

void ReloadItems();

#define NUM_ITEM_TYPES \
	THEME->GetMetricF("Inventory", "NumItemTypes")
#define ITEM_DURATION_SECONDS \
  THEME->GetMetricF("Inventory", "ItemDurationSeconds")
#define ITEM_COMBO(i) \
  THEME->GetMetricI("Inventory", ssprintf("Item%dCombo", i + 1))
#define ITEM_EFFECT(i) \
  THEME->GetMetric("Inventory", ssprintf("Item%dEffect", i + 1))
#define ITEM_LEVEL(i) \
  THEME->GetMetricI("Inventory", ssprintf("Item%dLevel", i + 1))
ThemeMetric<float> ITEM_USE_RATE_SECONDS("Inventory", "ItemUseRateSeconds");

#define ITEM_USE_PROBABILITY (1.f / ITEM_USE_RATE_SECONDS)

struct Item {
  AttackLevel level;
  unsigned int combo;
  RString modifier;
};
static std::vector<Item> g_Items;

void ReloadItems() {
  g_Items.clear();
  for (int i = 0; i < NUM_ITEM_TYPES; i++) {
    Item item;
    item.level = (AttackLevel)(ITEM_LEVEL(i) - 1);
    item.combo = ITEM_COMBO(i);
    item.modifier = ITEM_EFFECT(i);
    g_Items.push_back(item);
  }
}

Inventory::Inventory() {
  PlayMode mode = GAMESTATE->play_mode_;
  switch (mode) {
    case PLAY_MODE_BATTLE:
      break;
    default:
      FAIL_M(ssprintf("Inventory not valid for PlayMode %i", mode));
  }
}

Inventory::~Inventory() {
  for (unsigned i = 0; i < sound_use_item_.size(); ++i) {
    delete sound_use_item_[i];
  }
  sound_use_item_.clear();
}

void Inventory::Load(PlayerState* player_state) {
  ReloadItems();

  player_state_ = player_state;
  last_seen_combo_ = 0;

  // Don't load battle sounds if they're not going to be used.
  switch (GAMESTATE->play_mode_) {
    case PLAY_MODE_BATTLE: {
      sound_acquire_item_.Load(THEME->GetPathS("Inventory", "aquire item"));
      for (unsigned i = 0; i < g_Items.size(); ++i) {
        RageSound* sound = new RageSound;
        sound->Load(
            THEME->GetPathS("Inventory", ssprintf("use item %u", i + 1)));
        sound_use_item_.push_back(sound);
      }
      sound_item_ending_.Load(THEME->GetPathS("Inventory", "item ending"));
      break;
    }
    default:
      break;
  }
}

void Inventory::Update(float delta) {
  if (player_state_->m_bAttackEndedThisUpdate) {
    sound_item_ending_.Play(false);
  }

  // TODO: Remove use of PlayerNumber.
  PlayerNumber pn = player_state_->m_PlayerNumber;

  // Check to see if they deserve a new item.
  if (STATSMAN->m_CurStageStats.m_player[pn].m_iCurCombo != last_seen_combo_) {
    unsigned int old_combo = last_seen_combo_;
    last_seen_combo_ = STATSMAN->m_CurStageStats.m_player[pn].m_iCurCombo;
    unsigned int new_combo = last_seen_combo_;

#define CROSSED(i) (old_combo < i) && (new_combo >= i)
#define BROKE_ABOVE(i) (new_combo < old_combo) && (old_combo >= i)

    for (unsigned i = 0; i < g_Items.size(); ++i) {
      bool earned_this_item = false;
      if (PREFSMAN->m_bBreakComboToGetItem) {
        earned_this_item = BROKE_ABOVE(g_Items[i].combo);
      } else {
        earned_this_item = CROSSED(g_Items[i].combo);
      }

      if (earned_this_item) {
        AwardItem(i);
        break;
      }
    }
  }

  Song& song = *GAMESTATE->cur_song_;
  // use items if this player is CPU-controlled
  if (player_state_->m_PlayerController != PC_HUMAN &&
      GAMESTATE->position_.m_fSongBeat < song.GetLastBeat()) {
    // every 1 seconds, try to use an item
    int last_second = (int)(RageTimer::GetTimeSinceStartFast() - delta);
    int this_second = (int)RageTimer::GetTimeSinceStartFast();
    if (last_second != this_second) {
      for (int s = 0; s < NUM_INVENTORY_SLOTS; ++s) {
        if (!player_state_->m_Inventory[s].IsBlank()) {
          if (randomf(0, 1) < ITEM_USE_PROBABILITY) {
            UseItem(s);
          }
        }
      }
    }
  }
}

void Inventory::AwardItem(int item_index) {
  // CPU players and replay data are vanity only. They should not effect
	// gameplay by acquiring/launching attacks.
  if (player_state_->m_PlayerController == PC_CPU) {
    return;
  }

  // Search for the first open slot.
  int open_slot = -1;

  Attack* inventory = player_state_->m_Inventory;  //[NUM_INVENTORY_SLOTS]

  if (inventory[NUM_INVENTORY_SLOTS / 2].IsBlank()) {
    open_slot = NUM_INVENTORY_SLOTS / 2;
  } else {
    for (int s = 0; s < NUM_INVENTORY_SLOTS; ++s) {
      if (inventory[s].IsBlank()) {
        open_slot = s;
        break;
      }
    }
  }

  if (open_slot != -1) {
    Attack attack;
    attack.sModifiers = g_Items[item_index].modifier;
    attack.fSecsRemaining = ITEM_DURATION_SECONDS;
    attack.level = g_Items[item_index].level;
    inventory[open_slot] = attack;
    sound_acquire_item_.Play(false);
  }
  // lse not enough room to insert item
}

void Inventory::UseItem(int slot) {
  Attack* inventory = player_state_->m_Inventory;  //[NUM_INVENTORY_SLOTS]

  if (inventory[slot].IsBlank()) {
    return;
  }

  PlayerNumber pn = player_state_->m_PlayerNumber;
  Attack attack = inventory[slot];

  // remove the item
  inventory[slot].MakeBlank();
  sound_use_item_[attack.level]->Play(false);

  PlayerNumber pn_to_attack = OPPOSITE_PLAYER[pn];
  PlayerState* player_state_to_attack = GAMESTATE->player_state_[pn_to_attack];
  player_state_to_attack->LaunchAttack(attack);

  float percent_health_to_drain = (attack.level + 1) / 10.f;
  ASSERT(percent_health_to_drain > 0);
  GAMESTATE->opponent_health_percent_ -= percent_health_to_drain;
  CLAMP(GAMESTATE->opponent_health_percent_, 0.f, 1.f);

  // Play announcer sound.
  SCREENMAN->SendMessageToTopScreen(
      ssprintf("SM_BattleDamageLevel%d", attack.level + 1));
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
