#include "ScreenUnlockBrowse.h"

#include "ActorUtil.h"
#include "GameCommand.h"
#include "InputQueue.h"
#include "MessageManager.h"
#include "PlayerNumber.h"
#include "RageUtil.h"
#include "Screen.h"
#include "ScreenMessage.h"
#include "ScreenSelectMaster.h"
#include "UnlockManager.h"
#include "global.h"

REGISTER_SCREEN_CLASS(ScreenUnlockBrowse);

void ScreenUnlockBrowse::Init() {
  int index = 0;
  // fill m_aGameCommands before calling Init()
  for (const UnlockEntry& ue : UNLOCKMAN->unlock_entries_) {
    GameCommand gc;
    UnlockEntryStatus st = ue.GetUnlockEntryStatus();
    switch (st) {
      default:
        FAIL_M(ssprintf("Invalid UnlockEntryStatus: %i", st));
      case UnlockEntryStatus_RequirementsMet:
      case UnlockEntryStatus_Unlocked:
        gc.invalid_ = false;
        break;
      case UnlockEntryStatus_RequrementsNotMet:
        break;
    }
    gc.index_ = index++;
    // gc.m_sUnlockEntryID = ue->m_sEntryID;
    gc.name_ = ssprintf("%d", gc.index_);

    game_commands_.push_back(gc);
  }

  ScreenSelectMaster::Init();

  banner_.SetName("Banner");
  LOAD_ALL_COMMANDS_AND_SET_XY(banner_);
  this->AddChild(&banner_);

  this->SubscribeToMessage(Message_MenuSelectionChanged);
}

void ScreenUnlockBrowse::BeginScreen() {
  ScreenSelectMaster::BeginScreen();

  HandleMessage(Message(MessageIDToString(Message_MenuSelectionChanged)));
}

bool ScreenUnlockBrowse::MenuStart(const InputEventPlus& input) {
  sound_start_.Play(true);
  this->PostScreenMessage(SM_BeginFadingOut, 0);
  return true;
}

void ScreenUnlockBrowse::HandleMessage(const Message& msg) {
  if (msg == Message_MenuSelectionChanged) {
    int selection = this->GetSelectionIndex(PLAYER_1);
    const UnlockEntry& ue = UNLOCKMAN->unlock_entries_[selection];
    if (ue.IsLocked()) {
      banner_.LoadFallback();
    } else {
      banner_.LoadBannerFromUnlockEntry(&ue);
    }
  }

  ScreenSelectMaster::HandleMessage(msg);
}

/*
 * (c) 2006 Chris Danford
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
