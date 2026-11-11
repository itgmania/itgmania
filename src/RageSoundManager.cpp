/*
 * This manager has several distinct purposes:
 *
 * Load the sound driver, and handle most communication between it and
 * RageSound. Factory and reference count RageSoundReader objects for RageSound.
 * User-level:
 *  - global volume management
 *  - sound detaching ("play and delete when done playing")
 */

#include "RageSoundManager.h"

#include <cstdint>
#include <map>
#include <string>
#include <utility>

#include "LocalizedString.h"
#include "Preference.h"
#include "RageException.h"
#include "RageLog.h"
#include "RageSound.h"
#include "RageSoundConstants.h"
#include "RageSoundReader_PostBuffering.h"
#include "RageSoundReader_Preload.h"
#include "RageThreads.h"
#include "RageTimer.h"
#include "RageUtil.h"
#include "StdString.h"
#include "arch/Sound/RageSoundDriver.h"
#include "global.h"

/*
 * The lock ordering requirements are:
 * RageSound::Lock before g_SoundManMutex
 * RageSound::Lock must not be locked when calling driver calls (since the
 * driver may lock a mutex and then make RageSound calls back)
 *
 * Do not make RageSound calls that might lock while holding g_SoundManMutex.
 */

static RageMutex sound_man_mutex("SoundMan");
static Preference<std::string> sound_drivers(
    "SoundDrivers", "");  // "" == DEFAULT_SOUND_DRIVER_LIST

RageSoundManager* SOUNDMAN = nullptr;

RageSoundManager::RageSoundManager()
    : driver_(nullptr), volume_of_non_critical_sounds_(1.0f) {}

static LocalizedString COULDNT_FIND_SOUND_DRIVER(
    "RageSoundManager", "Couldn't find a sound driver that works");
void RageSoundManager::Init() {
  driver_ = RageSoundDriver::Create(sound_drivers);
  if (driver_ == nullptr) {
    RageException::Throw("%s", COULDNT_FIND_SOUND_DRIVER.GetValue().c_str());
  }
}

RageSoundManager::~RageSoundManager() {
  /* Don't lock while deleting the driver (the decoder thread might deadlock).
   */
  delete driver_;
  for (std::pair<const std::string&, RageSoundReader_Preload*> s :
       map_preloaded_sounds_) {
    delete s.second;
  }
  map_preloaded_sounds_.clear();
}

/*
 * Previously, we went to some lengths to shut down sounds before exiting
 * threads. The only other thread that actually starts sounds is SOUND.  Doing
 * this was ugly; instead, let's shut down the driver early, stopping all
 * sounds.  We don't want to delete SOUNDMAN early, since those threads are
 * still using it; just shut down the driver.
 */
void RageSoundManager::Shutdown() { RageUtil::SafeDelete(driver_); }

void RageSoundManager::StartMixing(RageSoundBase* sound) {
  if (driver_ != nullptr) {
    driver_->StartMixing(sound);
  }
}

void RageSoundManager::StopMixing(RageSoundBase* sound) {
  if (driver_ != nullptr) {
    driver_->StopMixing(sound);
  }
}

bool RageSoundManager::Pause(RageSoundBase* sound, bool pause) {
  if (driver_ == nullptr) {
    return false;
  } else {
    return driver_->PauseMixing(sound, pause);
  }
}

bool RageSoundManager::GetPlayingPosition(
    const RageSoundBase* sound, int& source_frame, RageTimer* timer) {
  if (driver_ == nullptr) {
    return false;
  }
  return driver_->GetPlayingPosition(sound, source_frame, timer);
}

void RageSoundManager::Update() {
  /* Scan m_mapPreloadedSounds for sounds that are no longer loaded, and delete
   * them. */
  sound_man_mutex
      .Lock(); /* lock for access to m_mapPreloadedSounds, owned_sounds */
  {
    for (auto it = map_preloaded_sounds_.begin();
         it != map_preloaded_sounds_.end();) {
      if (it->second->GetReferenceCount() == 1) {
        LOG->Trace("Deleted old sound \"%s\"", it->first.c_str());
        delete it->second;
        it = map_preloaded_sounds_.erase(it);
      } else {
        ++it;
      }
    }
  }

  sound_man_mutex.Unlock(); /* finished with m_mapPreloadedSounds */

  if (driver_ != nullptr) {
    driver_->Update();
  }
}

float RageSoundManager::GetPlayLatency() const {
  if (driver_ == nullptr) {
    return 0;
  }

  return driver_->GetPlayLatency();
}

int RageSoundManager::GetDriverSampleRate() const {
  if (driver_ == nullptr) {
    return FALLBACK_SAMPLE_RATE;
  }

  // Returns the *actual* operating rate of the loaded driver
  return driver_->GetSampleRate();
}

/* If the given path is loaded, return a copy; otherwise return nullptr.
 * It's the caller's responsibility to delete the result. */
RageSoundReader* RageSoundManager::GetLoadedSound(const std::string& raw_path) {
  LockMut(sound_man_mutex); /* lock for access to m_mapPreloadedSounds */

  std::string path(raw_path);
  MakeLower(path);
  std::map<std::string, RageSoundReader_Preload*>::const_iterator it;
  it = map_preloaded_sounds_.find(path);
  if (it == map_preloaded_sounds_.end()) {
    return nullptr;
  }

  return it->second->Copy();
}

/* Add the sound to the set of loaded sounds that can be copied for reuse.
 * The sound will be kept in memory as long as there are any other references
 * to it; once we hold the last one, we'll release it. */
void RageSoundManager::AddLoadedSound(
    const std::string& raw_path, RageSoundReader_Preload* sound) {
  LockMut(sound_man_mutex); /* lock for access to m_mapPreloadedSounds */

  /* Don't AddLoadedSound a sound that's already registered.  It should have
   * been used in GetLoadedSound. */
  std::string path(raw_path);
  MakeLower(path);
  std::map<std::string, RageSoundReader_Preload*>::const_iterator it;
  it = map_preloaded_sounds_.find(path);
  ASSERT_M(it == map_preloaded_sounds_.end(), path);

  map_preloaded_sounds_[path] = sound->Copy();
}

static Preference<float> sound_volume("SoundVolume", 1.0f);

void RageSoundManager::SetMixVolume() {
  RageSoundReader_PostBuffering::SetMasterVolume(sound_volume.Get());
}

void RageSoundManager::SetVolumeOfNonCriticalSounds(
    float volume_of_non_critical_sounds) {
  sound_man_mutex.Lock(); /* lock for access to m_fVolumeOfNonCriticalSounds */
  volume_of_non_critical_sounds_ = volume_of_non_critical_sounds;
  sound_man_mutex.Unlock(); /* finished with m_fVolumeOfNonCriticalSounds */
}

/*
 * Copyright (c) 2002-2004 Glenn Maynard
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
