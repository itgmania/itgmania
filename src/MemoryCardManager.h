#ifndef MemoryCardManager_H
#define MemoryCardManager_H

#include <string>
#include <vector>

#include "GameConstantsAndTypes.h"  // for MemoryCardState
#include "PlayerNumber.h"
#include "Preference.h"
#include "RageSound.h"
#include "RageTimer.h"
#include "arch/MemoryCard/MemoryCardDriver.h"

extern const std::string kMemCardMountPoint[kNumPlayers];

class MemoryCardManager {
 public:
  MemoryCardManager();
  ~MemoryCardManager();

  void Update();

  MemoryCardState GetCardState(PlayerNumber pn) const { return state_[pn]; }
  float GetCardStateAge(PlayerNumber pn) const {
    return state_changed_at_[pn].Ago();
  }
  std::string GetCardError(PlayerNumber pn) const { return error_[pn]; }

  void WaitForCheckingToComplete();
  bool CardInserted(PlayerNumber pn);
  void LockCard(
      PlayerNumber pn);  // prevent removing or changing of memory card
  void UnlockCard(PlayerNumber pn);
  bool MountCard(PlayerNumber pn, int timeout = 15);
  bool MountCard(PlayerNumber pn, const UsbStorageDevice& d, int timeout = 15);
  void UnmountCard(PlayerNumber pn, int timeout = 10);

  void SetCardAccessTimeLimit(PlayerNumber pn, float seconds);

  bool IsMounted(PlayerNumber pn) const { return mounted_[pn]; }

  // When paused, no changes in memory card state will be noticed until
  // unpaused.
  void PauseMountingThread();
  void UnPauseMountingThread();

  bool GetCardLocked(PlayerNumber pn) const { return card_locked_[pn]; }

  bool IsNameAvailable(PlayerNumber pn) const;
  std::string GetName(PlayerNumber pn) const;

  const std::vector<UsbStorageDevice>& GetStorageDevices() {
    return storage_devices_;
  }

  static Preference1D<std::string> memory_card_os_mount_point_;
  static Preference1D<int> memory_card_usb_bus_;
  static Preference1D<int> memory_card_usb_port_;
  static Preference1D<int> memory_card_usb_level_;

  static Preference<std::string> editor_memory_card_os_mount_point_;

  // Lua
  void PushSelf(lua_State* L);

 protected:
  void UpdateAssignments();
  void CheckStateChanges();

  std::vector<UsbStorageDevice> storage_devices_;  // all currently connected

  bool card_locked_[kNumPlayers];
  bool mounted_[kNumPlayers];  // card is currently mounted

  UsbStorageDevice
      device_[kNumPlayers];  // device in the memory card slot, blank if none
  UsbStorageDevice
      final_device_[kNumPlayers];  // device in the memory card slot when we
                                   // finalized, blank if none

  MemoryCardState state_[kNumPlayers];
  RageTimer state_changed_at_[kNumPlayers];
  std::string error_[kNumPlayers];  // if MemoryCardState_Error

  RageSound sound_ready_;
  RageSound sound_error_;
  RageSound sound_too_late_;
  RageSound sound_disconnect_;
};

extern MemoryCardManager*
    MEMCARDMAN;  // global and accessible from anywhere in our program

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
