#include "ScreenOptionsMemoryCard.h"

#include <cstddef>
#include <string>
#include <vector>

#include "ActorUtil.h"
#include "GameState.h"
#include "LocalizedString.h"
#include "MemoryCardManager.h"
#include "MessageManager.h"
#include "OptionRowHandler.h"
#include "PlayerNumber.h"
#include "RageUtil.h"
#include "Screen.h"
#include "ScreenManager.h"
#include "ScreenMessage.h"
#include "ScreenOptions.h"
#include "ScreenPrompt.h"
#include "ThemeManager.h"
#include "arch/MemoryCard/MemoryCardDriver.h"

REGISTER_SCREEN_CLASS(ScreenOptionsMemoryCard);

void ScreenOptionsMemoryCard::Init() {
  ScreenOptions::Init();

  text_os_mount_dir_.SetName("Mount");
  text_os_mount_dir_.LoadFromFont(THEME->GetPathF(name_, "mount"));
  ActorUtil::LoadAllCommands(text_os_mount_dir_, name_);
  this->AddChild(&text_os_mount_dir_);

  this->SubscribeToMessage(Message_StorageDevicesChanged);
}

bool ScreenOptionsMemoryCard::UpdateCurrentUsbStorageDevices() {
  std::vector<UsbStorageDevice> old_devices = current_usb_storage_devices_;

  const std::vector<UsbStorageDevice>& new_devices =
      MEMCARDMAN->GetStorageDevices();

  current_usb_storage_devices_.clear();
  for (size_t i = 0; i < new_devices.size(); ++i) {
    const UsbStorageDevice& dev = new_devices[i];
    if (dev.state_ == UsbStorageDevice::STATE_CHECKING) {
      continue;
    }
    current_usb_storage_devices_.push_back(dev);
  }

  return old_devices != current_usb_storage_devices_;
}

static LocalizedString NO_LABEL("ScreenOptionsMemoryCard", "(no label)");
static LocalizedString SIZE_UNKNOWN("ScreenOptionsMemoryCard", "size ???");
static LocalizedString VOLUME_SIZE("ScreenOptionsMemoryCard", "%dMB");
void ScreenOptionsMemoryCard::CreateMenu() {
  std::vector<OptionRowHandler*> hands;

  for (const UsbStorageDevice& iter : current_usb_storage_devices_) {
    std::vector<std::string> vs;
    if (iter.volume_label_.empty()) {
      vs.push_back(NO_LABEL);
    } else {
      vs.push_back(iter.volume_label_);
    }
    if (iter.volume_size_mb_ == 0) {
      vs.push_back(SIZE_UNKNOWN);
    } else {
      vs.push_back(
          ssprintf(std::string(VOLUME_SIZE).c_str(), iter.volume_size_mb_));
    }

    hands.push_back(OptionRowHandlerUtil::MakeNull());

    OptionRowDefinition& def = hands.back()->def_;
    std::string description = join(", ", vs);
    def.name_ = description;
    def.choices_.push_back("");
    def.explanation_name_ = "Memory Card";
    def.allow_theme_title_ = false;
    def.one_choice_for_all_players_ = true;
  }

  if (current_usb_storage_devices_.empty()) {
    hands.push_back(OptionRowHandlerUtil::MakeNull());
    OptionRowDefinition& def = hands.back()->def_;
    def.name_ = "No USB memory cards found";
    def.explanation_name_ = "No Memory Card";
    def.choices_.push_back("");
    def.allow_theme_title_ = true;
    def.one_choice_for_all_players_ = true;
  }

  InitMenu(hands);
}

void ScreenOptionsMemoryCard::BeginScreen() {
  UpdateCurrentUsbStorageDevices();
  CreateMenu();

  ScreenOptions::BeginScreen();

  // select the last chosen memory card (if present)
  SelectRowWithMemoryCard(MEMCARDMAN->editor_memory_card_os_mount_point_.Get());
}

void ScreenOptionsMemoryCard::HandleScreenMessage(const ScreenMessage sm) {
  ScreenOptions::HandleScreenMessage(sm);
}

void ScreenOptionsMemoryCard::AfterChangeRow(PlayerNumber pn) {
  ScreenOptions::AfterChangeRow(pn);

  if (current_usb_storage_devices_.empty()) {
    text_os_mount_dir_.SetText("");
  } else {
    int row = current_row_[GAMESTATE->GetMasterPlayerNumber()];
    text_os_mount_dir_.SetText(current_usb_storage_devices_[row].os_mount_dir_);
  }
}

void ScreenOptionsMemoryCard::HandleMessage(const Message& msg) {
  if (msg == Message_StorageDevicesChanged) {
    if (!out_.IsTransitioning()) {
      /* Remember the old mountpoint. */
      const std::vector<UsbStorageDevice>& v = current_usb_storage_devices_;
      int row = current_row_[GAMESTATE->GetMasterPlayerNumber()];
      std::string old_mount_point;
      if (row < int(v.size())) {
        const UsbStorageDevice& dev = v[row];
        old_mount_point = dev.os_mount_dir_;
      }

      /* If the devices that we'd actually show haven't changed, don't recreate
       * the menu. */
      if (!UpdateCurrentUsbStorageDevices()) {
        return;
      }

      CreateMenu();
      RestartOptions();

      /* If the memory card that was selected previously is still there, select
       * it. */
      SelectRowWithMemoryCard(old_mount_point);
    }
  }

  ScreenOptions::HandleMessage(msg);
}

void ScreenOptionsMemoryCard::ImportOptions(
    int row, const std::vector<PlayerNumber>& vpns) {}

void ScreenOptionsMemoryCard::ExportOptions(
    int i_row, const std::vector<PlayerNumber>& vpns) {
  OptionRow& row = *rows_[i_row];
  if (row.GetRowType() == OptionRow::RowType_Exit) {
    return;
  }

  PlayerNumber pn = GAMESTATE->GetMasterPlayerNumber();
  if (current_row_[pn] == i_row) {
    const std::vector<UsbStorageDevice>& v = current_usb_storage_devices_;
    if (i_row < int(v.size())) {
      const UsbStorageDevice& dev = v[i_row];
      MEMCARDMAN->editor_memory_card_os_mount_point_.Set(dev.os_mount_dir_);
    }
  }
}

void ScreenOptionsMemoryCard::SelectRowWithMemoryCard(
    const std::string& os_mount_point) {
  if (os_mount_point.empty()) {
    return;
  }

  const std::vector<UsbStorageDevice>& v = current_usb_storage_devices_;
  for (unsigned i = 0; i < v.size(); i++) {
    if (v[i].os_mount_dir_ == os_mount_point) {
      this->MoveRowAbsolute(PLAYER_1, i);
      return;
    }
  }
}

static LocalizedString ERROR_MOUNTING_CARD(
    "ScreenOptionsMemoryCard", "Error mounting card: %s");
void ScreenOptionsMemoryCard::ProcessMenuStart(const InputEventPlus&) {
  if (IsTransitioning()) {
    return;
  }

  int cur_row = current_row_[GAMESTATE->GetMasterPlayerNumber()];

  const std::vector<UsbStorageDevice>& v = current_usb_storage_devices_;
  if (cur_row < int(v.size()))  // a card
  {
    // Why is this statement in twice? Doubt it would change right after the if.
    // -Wolfman2000
    const std::vector<UsbStorageDevice>& usb = current_usb_storage_devices_;
    const UsbStorageDevice& dev = usb[cur_row];
    MEMCARDMAN->editor_memory_card_os_mount_point_.Set(dev.os_mount_dir_);

    /* The destination screen must UnmountCard.  XXX: brittle */
    bool success = MEMCARDMAN->MountCard(PLAYER_1, dev);
    if (success) {
      SCREENMAN->PlayStartSound();
      this->BeginFadingOut();
    } else {
      std::string s = ssprintf(
          ERROR_MOUNTING_CARD.GetValue().c_str(),
          MEMCARDMAN->GetCardError(PLAYER_1).c_str());
      ScreenPrompt::Prompt(SM_None, s);
    }
  } else {
    SCREENMAN->PlayInvalidSound();
  }
}

/*
 * (c) 2005 Chris Danford
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
