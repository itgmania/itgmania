#include "MemoryCardManager.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "LuaManager.h"
#include "MessageManager.h"
#include "PlayerNumber.h"
#include "Preference.h"
#include "RageFileDriver.h"
#include "RageFileDriverTimeout.h"
#include "RageFileManager.h"
#include "RageLog.h"
#include "RageSound.h"
#include "RageUtil.h"
#include "RageUtil_WorkerThread.h"
#include "ScreenManager.h"
#include "StdString.h"
#include "ThemeManager.h"
#include "arch/MemoryCard/MemoryCardDriver.h"  // for UsbStorageDevice
#include "arch/MemoryCard/MemoryCardDriver_Null.h"
#include "global.h"

MemoryCardManager* MEMCARDMAN =
    nullptr;  // global and accessible from anywhere in our program

static void MemoryCardOsMountPointInit(
    size_t /*PlayerNumber*/ i, std::string& sNameOut,
    std::string& defaultValueOut) {
  sNameOut = ssprintf("MemoryCardOsMountPointP%d", int(i + 1));
  defaultValueOut = "";
}

static void MemoryCardUsbBusInit(
    size_t /*PlayerNumber*/ i, std::string& sNameOut, int& defaultValueOut) {
  sNameOut = ssprintf("MemoryCardUsbBusP%d", int(i + 1));
  defaultValueOut = -1;
}

static void MemoryCardUsbPortInit(
    size_t /*PlayerNumber*/ i, std::string& sNameOut, int& defaultValueOut) {
  sNameOut = ssprintf("MemoryCardUsbPortP%d", int(i + 1));
  defaultValueOut = -1;
}

static void MemoryCardUsbLevelInit(
    size_t /*PlayerNumber*/ i, std::string& sNameOut, int& defaultValueOut) {
  sNameOut = ssprintf("MemoryCardUsbLevelP%d", int(i + 1));
  defaultValueOut = -1;
}

static Preference<bool> g_bMemoryCards("MemoryCards", false);
static Preference<bool> g_bMemoryCardProfiles("MemoryCardProfiles", true);

// if set, always use the device that mounts to this point
Preference1D<std::string> MemoryCardManager::memory_card_os_mount_point_(
    MemoryCardOsMountPointInit, NUM_PLAYERS, PreferenceType::Immutable);

// Look for this level, bus, port when assigning cards. -1 = match any
Preference1D<int> MemoryCardManager::memory_card_usb_bus_(
    MemoryCardUsbBusInit, NUM_PLAYERS, PreferenceType::Immutable);
Preference1D<int> MemoryCardManager::memory_card_usb_port_(
    MemoryCardUsbPortInit, NUM_PLAYERS, PreferenceType::Immutable);
Preference1D<int> MemoryCardManager::memory_card_usb_level_(
    MemoryCardUsbLevelInit, NUM_PLAYERS, PreferenceType::Immutable);

Preference<std::string> MemoryCardManager::editor_memory_card_os_mount_point_(
    "EditorMemoryCardOsMountPoint", "", nullptr, PreferenceType::Immutable);

const std::string MEM_CARD_MOUNT_POINT[NUM_PLAYERS] = {
    // @ is important; see RageFileManager LoadedDriver::GetPath
    "/@mc1/",
    "/@mc2/",
};

static const std::string MEM_CARD_MOUNT_POINT_INTERNAL[NUM_PLAYERS] = {
    // @ is important; see RageFileManager LoadedDriver::GetPath
    "/@mc1int/",
    "/@mc2int/",
};

// Only access the memory card driver in a timeout-safe thread.
class ThreadedMemoryCardWorker : public RageWorkerThread {
 public:
  ThreadedMemoryCardWorker();
  ~ThreadedMemoryCardWorker();

  enum MountThreadState { detect_and_mount, detect_and_dont_mount, paused };
  void SetMountThreadState(MountThreadState mts);

  // These functions may time out.
  bool Mount(const UsbStorageDevice* pDevice, float fTimeout);
  bool Unmount(const UsbStorageDevice* pDevice, float fTimeout);

  // This function will not time out.
  bool StorageDevicesChanged(std::vector<UsbStorageDevice>& aOut);

 protected:
  void HandleRequest(int iRequest);
  void RequestTimedOut();
  void DoHeartbeat();

 private:
  MemoryCardDriver* driver_;
  MountThreadState mount_thread_state_;

  /* We make a copy of the device info we're working with, since the pointer
   * we're given will become invalid if the operation times out and DoRequest
   * returns. */
  UsbStorageDevice request_device_;

  bool result_ = false;

  RageMutex usb_storage_devices_mutex_;
  bool usb_storage_devices_changed_ = false;
  std::vector<UsbStorageDevice> usb_storage_devices_;

  std::vector<UsbStorageDevice> mounted_devices_;

  enum {
    REQ_MOUNT,
    REQ_UNMOUNT,
  };
};

bool ThreadedMemoryCardWorker::StorageDevicesChanged(
    std::vector<UsbStorageDevice>& aOut) {
  usb_storage_devices_mutex_.Lock();
  if (!usb_storage_devices_changed_) {
    usb_storage_devices_mutex_.Unlock();
    return false;
  }

  aOut = usb_storage_devices_;
  usb_storage_devices_.clear();
  usb_storage_devices_changed_ = false;

  usb_storage_devices_mutex_.Unlock();
  return true;
}

ThreadedMemoryCardWorker::ThreadedMemoryCardWorker()
    : RageWorkerThread("MemoryCardWorker"),
      usb_storage_devices_mutex_("UsbStorageDevicesMutex") {
  if (g_bMemoryCards) {
    driver_ = MemoryCardDriver::Create();
  } else {
    driver_ = new MemoryCardDriver_Null;
  }

  mount_thread_state_ = detect_and_mount;
  SetHeartbeat(0.1f);

  StartThread();
}

ThreadedMemoryCardWorker::~ThreadedMemoryCardWorker() {
  StopThread();

  delete driver_;
}

void ThreadedMemoryCardWorker::SetMountThreadState(MountThreadState mts) {
  /* If "pause", stop calling updates in the heartbeat. In principle, we should
   * also not return from this function until the current heartbeat, if running,
   * finishes.  However, since we can't guarantee that it'll exit within the
   * timeout, there's no point: we have to return when we time out, and in that
   * case the heartbeat will still be running. I don't know if the reasons for
   * pausing really need us to wait, so don't. */
  mount_thread_state_ = mts;
}

void ThreadedMemoryCardWorker::HandleRequest(int iRequest) {
  switch (iRequest) {
    case REQ_MOUNT:
      result_ = driver_->Mount(&request_device_);
      mounted_devices_.push_back(request_device_);
      break;

    case REQ_UNMOUNT: {
      driver_->Unmount(&request_device_);
      std::vector<UsbStorageDevice>::iterator it = find(
          mounted_devices_.begin(), mounted_devices_.end(), request_device_);
      if (it == mounted_devices_.end()) {
        LOG->Warn("Unmounted a device that wasn't mounted");
      } else {
        mounted_devices_.erase(it);
      }
      break;
    }
  }
}

void ThreadedMemoryCardWorker::RequestTimedOut() {
  /* We timed out, so the current operation will abort. The unmount request
   * may be skipped, if it's attempted during the timeout, so unmount all
   * mounted devices. */
  for (unsigned i = 0; i < mounted_devices_.size(); ++i) {
    driver_->Unmount(&mounted_devices_[i]);
  }

  mounted_devices_.clear();
}

void ThreadedMemoryCardWorker::DoHeartbeat() {
  if (mount_thread_state_ == paused) {
    return;
  }

  // If true, detect and mount. If false, only detect.
  bool bMount = (mount_thread_state_ == detect_and_mount);

  std::vector<UsbStorageDevice> aStorageDevices;
  // LOG->Trace("update");
  if (!driver_->DoOneUpdate(bMount, aStorageDevices)) {
    return;
  }

  usb_storage_devices_mutex_.Lock();
  usb_storage_devices_ = aStorageDevices;
  usb_storage_devices_changed_ = true;
  usb_storage_devices_mutex_.Unlock();
}

bool ThreadedMemoryCardWorker::Mount(
    const UsbStorageDevice* pDevice, float fTimeout) {
  // If we're currently in a timed-out state, fail.
  if (IsTimedOut()) {
    return false;
  }

  request_device_ = *pDevice;
  SetTimeout(fTimeout);
  const bool bFinished = DoRequest(REQ_MOUNT);
  SetTimeout(-1);

  return bFinished && result_;
}

bool ThreadedMemoryCardWorker::Unmount(
    const UsbStorageDevice* pDevice, float fTimeout) {
  // If we're currently in a timed-out state, fail.
  if (IsTimedOut()) {
    return false;
  }

  request_device_ = *pDevice;
  SetTimeout(fTimeout);
  const bool bFinished = DoRequest(REQ_UNMOUNT);
  SetTimeout(-1);

  return bFinished;
}

static ThreadedMemoryCardWorker* g_pWorker = nullptr;

MemoryCardManager::MemoryCardManager() {
  ASSERT(g_pWorker == nullptr);

  // Register with Lua.
  {
    Lua* L = LUA->Get();
    lua_pushstring(L, "MEMCARDMAN");
    this->PushSelf(L);
    lua_settable(L, LUA_GLOBALSINDEX);
    LUA->Release(L);
  }

  g_pWorker = new ThreadedMemoryCardWorker;

  FOREACH_PlayerNumber(p) {
    card_locked_[p] = false;
    mounted_[p] = false;
    state_[p] = MemoryCardState_NoCard;
  }

  /* These can play at any time.  Preload them, so we don't cause a skip in
   * gameplay. */
  sound_ready_.Load(THEME->GetPathS("MemoryCardManager", "ready"), true);
  sound_error_.Load(THEME->GetPathS("MemoryCardManager", "error"), true);
  sound_too_late_.Load(THEME->GetPathS("MemoryCardManager", "too late"), true);
  sound_disconnect_.Load(
      THEME->GetPathS("MemoryCardManager", "disconnect"), true);

  /* Mount the filesystems that we'll use with Mount().  Use a bogus root for
   * the internal mount for now, since we don't know where we'll mount to yet;
   * nothing reads from it until it's mounted, anyway. */
  FOREACH_PlayerNumber(pn) {
    FILEMAN->Mount("dir", "(empty)", MEM_CARD_MOUNT_POINT_INTERNAL[pn]);
    FILEMAN->Mount(
        "timeout", MEM_CARD_MOUNT_POINT_INTERNAL[pn], MEM_CARD_MOUNT_POINT[pn]);
  }
}

MemoryCardManager::~MemoryCardManager() {
  // Unregister with Lua.
  LUA->UnsetGlobal("MEMCARDMAN");

  ASSERT(g_pWorker != nullptr);
  RageUtil::SafeDelete(g_pWorker);

  FOREACH_PlayerNumber(pn) {
    FILEMAN->Unmount("", "", MEM_CARD_MOUNT_POINT[pn]);
    FILEMAN->Unmount("", "", MEM_CARD_MOUNT_POINT_INTERNAL[pn]);
  }
}

void MemoryCardManager::Update() {
  std::vector<UsbStorageDevice> vOld;

  vOld = storage_devices_;  // copy
  if (!g_pWorker->StorageDevicesChanged(storage_devices_)) {
    return;
  }

  UpdateAssignments();

  MESSAGEMAN->Broadcast(Message_StorageDevicesChanged);
}

// Assign cards from m_vStorageDevices to m_Device.
void MemoryCardManager::UpdateAssignments() {
  if (!g_bMemoryCardProfiles.Get()) {
    return;
  }

  // make a list of unassigned
  std::vector<UsbStorageDevice> vUnassignedDevices = storage_devices_;  // copy

  // remove cards that are already assigned
  FOREACH_PlayerNumber(p) {
    UsbStorageDevice& assigned_device = device_[p];
    if (assigned_device.IsBlank()) {  // no card assigned to this player
      continue;
    }

    for (std::vector<UsbStorageDevice>::iterator d = vUnassignedDevices.begin();
         d != vUnassignedDevices.end(); ++d) {
      if (*d == assigned_device) {
        vUnassignedDevices.erase(d);
        break;
      }
    }
  }

  // Try to assign each device to a player. If a player already has a device
  // assigned, and the device still exists, keep him on the same card.
  FOREACH_PlayerNumber(p) {
    UsbStorageDevice& assigned_device = device_[p];
    if (!assigned_device.IsBlank()) {
      // The player has a card assigned. If it's been removed, clear it.
      std::vector<UsbStorageDevice>::iterator it = find(
          storage_devices_.begin(), storage_devices_.end(), assigned_device);
      if (it != storage_devices_.end()) {
        /* The player has a card, and it's still plugged in. Update any
         * changed state, such as m_State. */
        LOG->Trace(
            "Player %d already has a card: '%s'", p + 1,
            assigned_device.os_mount_dir_.c_str());
        assigned_device = *it;
        continue;
      }

      // The assigned card has been removed; clear it and re-search.
      LOG->Trace(
          "Player %i: disconnected bus %d port %d device %d path %s", p + 1,
          assigned_device.bus_, assigned_device.port_, assigned_device.level_,
          assigned_device.os_mount_dir_.c_str());
      assigned_device.MakeBlank();
    }

    LOG->Trace("Looking for a card for Player %d", p + 1);

    for (std::vector<UsbStorageDevice>::iterator d = vUnassignedDevices.begin();
         d != vUnassignedDevices.end(); ++d) {
      // search for card dir match
      if (!memory_card_os_mount_point_[p].Get().empty() &&
          CompareNoCase(
              d->os_mount_dir_, memory_card_os_mount_point_[p].Get())) {
        continue;  // not a match
      }

      // search for USB bus match
      if (memory_card_usb_bus_[p] != -1 && memory_card_usb_bus_[p] != d->bus_) {
        continue;  // not a match
      }

      if (memory_card_usb_port_[p] != -1 &&
          memory_card_usb_port_[p] != d->port_) {
        continue;  // not a match
      }

      if (memory_card_usb_level_[p] != -1 &&
          memory_card_usb_level_[p] != d->level_) {
        continue;  // not a match
      }

      LOG->Trace("Player %i: matched %s", p + 1, d->device_.c_str());

      assigned_device = *d;  // save a copy
      vUnassignedDevices.erase(
          d);  // remove the device so we don't match it for another player
      break;
    }
  }

  CheckStateChanges();
}

void MemoryCardManager::CheckStateChanges() {
  // Deal with assignment changes.
  FOREACH_PlayerNumber(p) {
    const UsbStorageDevice& new_device = device_[p];

    MemoryCardState state = MemoryCardState_Invalid;
    std::string sError;

    if (card_locked_[p]) {
      if (final_device_[p].state_ == UsbStorageDevice::STATE_NONE) {
        /* We didn't have a card when we finalized, so we won't accept anything.
         * If anything is inserted (even if it's still checking), say TOO LATE.
         */
        if (new_device.state_ == UsbStorageDevice::STATE_NONE) {
          state = MemoryCardState_NoCard;
        } else {
          state = MemoryCardState_TooLate;
        }
      } else {
        // We had a card inserted when we finalized.
        if (new_device.state_ == UsbStorageDevice::STATE_NONE) {
          state = MemoryCardState_Removed;
        }
        if (new_device.state_ == UsbStorageDevice::STATE_READY) {
          if (final_device_[p].serial_ != new_device.serial_) {
            // A different card is inserted than we had when we finalized.
            state = MemoryCardState_Error;
            sError = "Changed";
          }
        }

        // Otherwise, the card is checking or has an error.
        // Use the regular logic.
      }
    }

    if (state == MemoryCardState_Invalid) {
      switch (new_device.state_) {
        case UsbStorageDevice::STATE_NONE:
          state = MemoryCardState_NoCard;
          break;

        case UsbStorageDevice::STATE_CHECKING:
          state = MemoryCardState_Checking;
          break;

        case UsbStorageDevice::STATE_ERROR:
          state = MemoryCardState_Error;
          sError = new_device.error_;
          break;

        case UsbStorageDevice::STATE_READY:
          state = MemoryCardState_Ready;
          break;
        default:
          break;
      }
    }

    MemoryCardState LastState = state_[p];
    if (state_[p] != state) {
      // play sound
      RageSoundParams params;
      params.is_critical_sound_ = true;
      switch (state) {
        case MemoryCardState_NoCard:
        case MemoryCardState_Removed:
          if (LastState == MemoryCardState_Ready) {
            sound_disconnect_.Play(true, &params);
            MESSAGEMAN->Broadcast(
                (MessageID)(Message_CardRemovedP1 + Enum::to_integral(p)));
          }
          break;
        case MemoryCardState_Ready:
          sound_ready_.Play(true, &params);
          break;
        case MemoryCardState_TooLate:
          sound_too_late_.Play(true, &params);
          break;
        case MemoryCardState_Error:
          sound_error_.Play(true, &params);
          break;
        default:
          break;
      }

      state_[p] = state;
      state_changed_at_[p].Touch();
      error_[p] = sError;
    }
  }

  SCREENMAN->RefreshCreditsMessages();
}

void MemoryCardManager::WaitForCheckingToComplete() {
  g_pWorker->SetTimeout(5);

  /* If either player's card is in STATE_CHECKING, we need to give it a chance
   * to finish up before returning. */
  bool bLogged = false;
  while (!g_pWorker->IsTimedOut()) {
    // Check for changes.
    Update();

    bool bEitherPlayerIsChecking = false;
    FOREACH_PlayerNumber(
        p) if (device_[p].state_ == UsbStorageDevice::STATE_CHECKING)
        bEitherPlayerIsChecking = true;
    if (!bEitherPlayerIsChecking) {
      break;
    }

    /* Only if we need to, wait for something to happen. If we time out
     * waiting for a heartbeat, give up. */
    if (!bLogged) {
      bLogged = true;
      LOG->Trace(
          "One or more cards are in STATE_CHECKING; waiting for them ...");
    }

    if (!g_pWorker->WaitForOneHeartbeat()) {
      LOG->Trace("STATE_CHECKING wait timed out");
      break;
    }
  }

  g_pWorker->SetTimeout(-1);
}

bool MemoryCardManager::CardInserted(PlayerNumber pn) {
  return device_[pn].state_ == UsbStorageDevice::STATE_CHECKING ||
         device_[pn].state_ == UsbStorageDevice::STATE_READY;
}

void MemoryCardManager::LockCard(PlayerNumber pn) {
  if (card_locked_[pn]) {
    return;
  }

  // Set the final state.
  CheckStateChanges();

  /* If the card in this player's slot is ready, then use it. If there is
   * no card ready when we finalize, clear m_FinalDevice. */
  if (device_[pn].state_ == UsbStorageDevice::STATE_READY) {
    final_device_[pn] = device_[pn];
  } else {
    final_device_[pn] = UsbStorageDevice();
  }

  // Set this last, since it changes the behavior of CheckStateChanges.
  card_locked_[pn] = true;
}

void MemoryCardManager::UnlockCard(PlayerNumber pn) {
  card_locked_[pn] = false;

  g_pWorker->SetMountThreadState(ThreadedMemoryCardWorker::detect_and_mount);

  // If a memory card was inserted too late last game, allow it now.
  CheckStateChanges();
}

// Called just before reading or writing to the memory card. Should block.
bool MemoryCardManager::MountCard(PlayerNumber pn, int iTimeout) {
  LOG->Trace("MemoryCardManager::MountCard(%i)", pn);
  if (GetCardState(pn) != MemoryCardState_Ready) {
    return false;
  }
  ASSERT(!device_[pn].IsBlank());

  // Pause the mounting thread when we mount the first drive.
  bool bStartingMemoryCardAccess = true;
  FOREACH_PlayerNumber(p) if (mounted_[p]) bStartingMemoryCardAccess =
      false;  // already did
  if (bStartingMemoryCardAccess) {
    // We're starting to do stuff to the memory cards.
    this->PauseMountingThread();
  }

  if (!g_pWorker->Mount(&device_[pn], static_cast<float>(iTimeout))) {
    LOG->Trace("MemoryCardManager::MountCard: mount failed");
    if (bStartingMemoryCardAccess) {
      this->UnPauseMountingThread();
    }

    return false;
  }

  mounted_[pn] = true;

  // Limit the whole access, in case the card responds, but very slowly.
  SetCardAccessTimeLimit(pn, static_cast<float>(iTimeout));

  RageFileDriver* pDriver =
      FILEMAN->GetFileDriver(MEM_CARD_MOUNT_POINT_INTERNAL[pn]);
  if (pDriver == nullptr) {
    LOG->Warn(
        "FILEMAN->GetFileDriver(%s) failed",
        MEM_CARD_MOUNT_POINT_INTERNAL[pn].c_str());
    return true;
  }

  // We don't want to unmount the timeout FS. Instead, just move the target.
  pDriver->Remount(device_[pn].os_mount_dir_);

  // Flush mountpoints pointing to what we've mounted.
  FILEMAN->FlushDirCache(MEM_CARD_MOUNT_POINT[pn]);
  FILEMAN->FlushDirCache(MEM_CARD_MOUNT_POINT_INTERNAL[pn]);

  FILEMAN->ReleaseFileDriver(pDriver);

  return true;
}

/* Temporarily mount a specific card.  On unmount, the device will be reverted.
 * This is used to access cards in the editor. */
bool MemoryCardManager::MountCard(
    PlayerNumber pn, const UsbStorageDevice& d, int iTimeout) {
  device_[pn] = d;
  CheckStateChanges();
  return MountCard(pn, iTimeout);
}

/* Called when finished accessing a memory card.  If writes have been performed,
 * will block until flushed. */
void MemoryCardManager::UnmountCard(PlayerNumber pn, int iTimeout) {
  LOG->Trace(
      "MemoryCardManager::UnmountCard(%i) (mounted: %i)", pn, mounted_[pn]);
  if (device_[pn].IsBlank()) {
    return;
  }

  if (!mounted_[pn]) {
    return;
  }

  SetCardAccessTimeLimit(pn, -1);

  // Leave our own filesystem drivers mounted.  Unmount the kernel mount.
  if (!g_pWorker->Unmount(&device_[pn], static_cast<float>(iTimeout))) {
    LOG->Warn("MemoryCardManager::UnmountCard: unmount failed");
  }

  // Flush mountpoints pointing to what we've unmounted.
  FILEMAN->FlushDirCache(MEM_CARD_MOUNT_POINT[pn]);
  FILEMAN->FlushDirCache(MEM_CARD_MOUNT_POINT_INTERNAL[pn]);

  mounted_[pn] = false;

  // Unpause the mounting thread when we unmount the last drive.
  bool bNeedUnpause = true;
  FOREACH_PlayerNumber(p) if (mounted_[p]) bNeedUnpause = false;
  if (bNeedUnpause) {
    this->UnPauseMountingThread();
  }

  /* If memory card profiles are disabled, then this was assigned by passing to
   * a UsbStorageDevice to MountCard.  Remove the temporary assignment. */
  if (!g_bMemoryCardProfiles.Get()) {
    device_[pn] = UsbStorageDevice();
    CheckStateChanges();
  }
}

void MemoryCardManager::SetCardAccessTimeLimit(
    PlayerNumber pn, float fSeconds) {
  RageFileDriver* pDriver = FILEMAN->GetFileDriver(MEM_CARD_MOUNT_POINT[pn]);
  if (pDriver == nullptr) {
    return;
  }

  RageFileDriverTimeout* pTimeoutDriver =
      dynamic_cast<RageFileDriverTimeout*>(pDriver);
  if (pTimeoutDriver != nullptr) {
    pTimeoutDriver->SetDeadline(fSeconds);
  }

  FILEMAN->ReleaseFileDriver(pDriver);
}

bool MemoryCardManager::IsNameAvailable(PlayerNumber pn) const {
  return device_[pn].is_name_available_;
}

std::string MemoryCardManager::GetName(PlayerNumber pn) const {
  return device_[pn].name_;
}

void MemoryCardManager::PauseMountingThread() {
  LOG->Trace("MemoryCardManager::PauseMountingThread");

  g_pWorker->SetMountThreadState(ThreadedMemoryCardWorker::paused);
}

void MemoryCardManager::UnPauseMountingThread() {
  LOG->Trace("MemoryCardManager::UnPauseMountingThread");

  g_pWorker->SetMountThreadState(ThreadedMemoryCardWorker::detect_and_mount);
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the MemoryCardManager. */
class LunaMemoryCardManager : public Luna<MemoryCardManager> {
 public:
  static int GetCardState(T* p, lua_State* L) {
    PlayerNumber pn = Enum::Check<PlayerNumber>(L, 1);
    LuaHelpers::Push(L, p->GetCardState(pn));
    return 1;
  }
  static int GetName(T* p, lua_State* L) {
    PlayerNumber pn = Enum::Check<PlayerNumber>(L, 1);
    lua_pushstring(L, p->GetName(pn).c_str());
    return 1;
  }

  LunaMemoryCardManager() {
    ADD_METHOD(GetCardState);
    ADD_METHOD(GetName);
  }
};

LUA_REGISTER_CLASS(MemoryCardManager)
// lua end

/*
 * (c) 2003-2005 Chris Danford, Glenn Maynard
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
