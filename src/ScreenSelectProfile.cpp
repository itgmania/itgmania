#include "ScreenSelectProfile.h"

#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "GameInput.h"
#include "GameState.h"
#include "InputEventPlus.h"
#include "InputFilter.h"
#include "LuaManager.h"
#include "MemoryCardManager.h"
#include "MessageManager.h"
#include "PlayerNumber.h"
#include "ProfileManager.h"
#include "Screen.h"
#include "ScreenManager.h"
#include "ScreenMessage.h"
#include "ScreenWithMenuElements.h"

REGISTER_SCREEN_CLASS(ScreenSelectProfile);

void ScreenSelectProfile::Init() {
  FOREACH_PlayerNumber(p) {
    // no selection initially
    selected_profiles_[p] = -1;
  }
  tracking_repeating_input_ = GameButton_Invalid;
  ScreenWithMenuElements::Init();
}

bool ScreenSelectProfile::Input(const InputEventPlus& input) {
  if (IsTransitioning()) {
    return false;
  }

  return ScreenWithMenuElements::Input(input);
}

bool ScreenSelectProfile::MenuLeft(const InputEventPlus& input) {
  PlayerNumber pn = input.pn_;
  if (lock_input_secs_ > 0) {
    return false;
  }
  if (input.type_ == IET_RELEASE) {
    return false;
  }
  if (input.type_ != IET_FIRST_PRESS) {
    /*
    if( !ALLOW_REPEATING_INPUT )
            return false;
    */
    if (tracking_repeating_input_ != input.menu_i_) {
      return false;
    }
  }
  tracking_repeating_input_ = input.menu_i_;
  MESSAGEMAN->Broadcast(
      (MessageID)(Message_MenuLeftP1 + Enum::to_integral(pn)));
  return true;
}

bool ScreenSelectProfile::MenuRight(const InputEventPlus& input) {
  PlayerNumber pn = input.pn_;
  if (lock_input_secs_ > 0) {
    return false;
  }
  if (input.type_ == IET_RELEASE) {
    return false;
  }
  if (input.type_ != IET_FIRST_PRESS) {
    /*
    if( !ALLOW_REPEATING_INPUT )
            return false;
    */
    if (tracking_repeating_input_ != input.menu_i_) {
      return false;
    }
  }
  tracking_repeating_input_ = input.menu_i_;
  MESSAGEMAN->Broadcast(
      (MessageID)(Message_MenuRightP1 + Enum::to_integral(pn)));
  return true;
}

bool ScreenSelectProfile::MenuUp(const InputEventPlus& input) {
  PlayerNumber pn = input.pn_;
  if (lock_input_secs_ > 0) {
    return false;
  }
  if (input.type_ == IET_RELEASE) {
    return false;
  }
  if (input.type_ != IET_FIRST_PRESS) {
    /*
    if( !ALLOW_REPEATING_INPUT )
            return false;
    */
    if (tracking_repeating_input_ != input.menu_i_) {
      return false;
    }
  }
  tracking_repeating_input_ = input.menu_i_;
  MESSAGEMAN->Broadcast((MessageID)(Message_MenuUpP1 + Enum::to_integral(pn)));
  return true;
}

bool ScreenSelectProfile::MenuDown(const InputEventPlus& input) {
  PlayerNumber pn = input.pn_;
  if (lock_input_secs_ > 0) {
    return false;
  }
  if (input.type_ == IET_RELEASE) {
    return false;
  }
  if (input.type_ != IET_FIRST_PRESS) {
    /*
    if( !ALLOW_REPEATING_INPUT )
            return false;
    */
    if (tracking_repeating_input_ != input.menu_i_) {
      return false;
    }
  }
  tracking_repeating_input_ = input.menu_i_;
  MESSAGEMAN->Broadcast(
      (MessageID)(Message_MenuDownP1 + Enum::to_integral(pn)));
  return true;
}

bool ScreenSelectProfile::SetProfileIndex(PlayerNumber pn, int profile_index) {
  if (!GAMESTATE->IsHumanPlayer(pn)) {
    if (profile_index == -1) {
      GAMESTATE->JoinPlayer(pn);
      SCREENMAN->PlayStartSound();
      return true;
    }
    return false;
  }

  if (profile_index > PROFILEMAN->GetNumLocalProfiles()) {
    return false;
  }

  // wrong selection
  if (profile_index < -3) {
    return false;
  }

  // unload player
  if (profile_index == -2) {
    // GAMESTATE->UnjoinPlayer takes care of unloading the profile.
    GAMESTATE->UnjoinPlayer(pn);
    MEMCARDMAN->UnlockCard(pn);
    MEMCARDMAN->UnmountCard(pn);
    selected_profiles_[pn] = -1;
    return true;
  }

  selected_profiles_[pn] = profile_index;

  return true;
}

bool ScreenSelectProfile::Finish() {
  if (GAMESTATE->GetNumPlayersEnabled() == 0) {
    return false;
  }

  // if profile indexes are the same for both players
  if (GAMESTATE->GetNumPlayersEnabled() == 2 &&
      selected_profiles_[0] == selected_profiles_[1] &&
      selected_profiles_[0] > 0) {
    return false;
  }

  int used_local_profiles = 0;
  int unselected_profiles = 0;

  FOREACH_PlayerNumber(p) {
    // not all players have made their choices
    if (GAMESTATE->IsHumanPlayer(p) && (selected_profiles_[p] == -1)) {
      unselected_profiles++;
    }

    // card not ready
    if (selected_profiles_[p] == 0 &&
        MEMCARDMAN->GetCardState(p) != MemoryCardState_Ready) {
      return false;
    }

    // profile index too big
    if (selected_profiles_[p] > PROFILEMAN->GetNumLocalProfiles()) {
      return false;
    }

    // inc used profile count
    if (selected_profiles_[p] > 0) {
      used_local_profiles++;
    }
  }

  // this allows to continue if there is less local profiles than number of
  // human players
  if (unselected_profiles &&
      used_local_profiles < PROFILEMAN->GetNumLocalProfiles()) {
    return false;
  }

  // unload all profiles, so we don't end up in a situation where two
  // players might have the same profile assigned
  FOREACH_PlayerNumber(p) {
    MEMCARDMAN->UnlockCard(p);
    MEMCARDMAN->UnmountCard(p);
    PROFILEMAN->UnloadProfile(p);
  }

  // all ok - load profiles and go to next screen
  FOREACH_PlayerNumber(p) {
    if (selected_profiles_[p] > 0) {
      PROFILEMAN->default_local_profile_id_[p].Set(
          PROFILEMAN->GetLocalProfileIDFromIndex(selected_profiles_[p] - 1));
      PROFILEMAN->LoadLocalProfileFromMachine(p);
      GAMESTATE->LoadCurrentSettingsFromProfile(p);
    }
    if (selected_profiles_[p] == 0) {
      MEMCARDMAN->WaitForCheckingToComplete();

      bool success = false;
      if (MEMCARDMAN->MountCard(p)) {
        success = PROFILEMAN->LoadProfileFromMemoryCard(
            p, true);  // load full profile
        MEMCARDMAN->UnmountCard(p);
      }

      // Lock the card on successful load, so we won't allow it to be changed.
      if (success) {
        GAMESTATE->LoadCurrentSettingsFromProfile(p);
        MEMCARDMAN->LockCard(p);
      }
    }

    // If the player picked a profile (>= 0) or chose to play as a guest (-3),
    // broadcast a message
    if (selected_profiles_[p] >= 0 || selected_profiles_[p] == -3) {
      Message msg(MessageIDToString(Message_PlayerProfileSet));
      msg.SetParam("Player", p);
      MESSAGEMAN->Broadcast(msg);
    }
  }

  StartTransitioningScreen(SM_GoToNextScreen);
  return true;
}

void ScreenSelectProfile::HandleScreenMessage(const ScreenMessage sm) {
  if (sm == SM_MenuTimer) {
    bool finished = Finish();
    if (!finished) {
      // TODO: we need to decide how to handle unfinished business.
    }
  }

  ScreenWithMenuElements::HandleScreenMessage(sm);
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the ScreenSelectProfile. */
class LunaScreenSelectProfile : public Luna<ScreenSelectProfile> {
 public:
  static int SetProfileIndex(T* p, lua_State* L) {
    PlayerNumber pn = Enum::Check<PlayerNumber>(L, 1);
    int profile_index = IArg(2);
    bool ret = p->SetProfileIndex(pn, profile_index);
    LuaHelpers::Push(L, ret);
    return 1;
  }

  static int GetProfileIndex(T* p, lua_State* L) {
    PlayerNumber pn = Enum::Check<PlayerNumber>(L, 1);
    LuaHelpers::Push(L, p->GetProfileIndex(pn));
    return 1;
  }

  static int Finish(T* p, lua_State* L) {
    bool ret = p->Finish();
    LuaHelpers::Push(L, ret);
    return 1;
  }

  static int Cancel(T* p, lua_State* L) {
    p->Cancel(SM_GoToPrevScreen);
    return 1;
  }

  LunaScreenSelectProfile() {
    ADD_METHOD(SetProfileIndex);
    ADD_METHOD(GetProfileIndex);
    ADD_METHOD(Finish);
    ADD_METHOD(Cancel);
  }
};

LUA_REGISTER_DERIVED_CLASS(ScreenSelectProfile, ScreenWithMenuElements)

/*
 * Copyright (c) 2007 vdl
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
