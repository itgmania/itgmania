#include "LightsDriver.h"

#include "RageLog.h"
#include "RageUtil.h"
#include "arch/RageDriver.h"
#include "global.h"

// We explicitly load these drivers as they should always be available.
#include <algorithm>
#include <string>
#include <vector>

#include "arch/Lights/LightsDriver_Export.h"
#include "arch/Lights/LightsDriver_SystemMessage.h"

DriverList LightsDriver::driver_list_;

namespace {
struct AutoDetectLightsDriver {
  std::string name_;
  LightsDriver::AutoDetectFunction should_load_;
};

std::vector<AutoDetectLightsDriver>& GetAutoDetectLightsDrivers() {
  static std::vector<AutoDetectLightsDriver> drivers;
  return drivers;
}
}  // namespace

void LightsDriver::RegisterAutoDetectDriver(
    const std::string& name, AutoDetectFunction should_load) {
  GetAutoDetectLightsDrivers().push_back({name, should_load});
}

void LightsDriver::Create(
    const std::string& drivers, std::vector<LightsDriver*>& add,
    bool allow_auto_detect) {
  LOG->Trace("Initializing lights drivers: %s", drivers.c_str());

  std::vector<std::string> drivers_to_try;
  split(drivers, ",", drivers_to_try, true);

  if (allow_auto_detect) {
    for (const AutoDetectLightsDriver& auto_driver :
         GetAutoDetectLightsDrivers()) {
      const bool already_default =
          std::find(
              drivers_to_try.begin(), drivers_to_try.end(),
              auto_driver.name_) != drivers_to_try.end();
      if (already_default || !auto_driver.should_load_()) {
        continue;
      }

      LOG->Info(
          "Auto-detected %s; loading lights driver", auto_driver.name_.c_str());
      RageDriver* driver = driver_list_.Create(auto_driver.name_);
      if (driver == nullptr) {
        LOG->Warn(
            "Auto-detected lights driver %s is not registered",
            auto_driver.name_.c_str());
        continue;
      }

      LightsDriver* lights_driver = dynamic_cast<LightsDriver*>(driver);
      ASSERT(lights_driver != nullptr);
      add.push_back(lights_driver);
    }
  } else {
    LOG->Info(
        "LightsDriver is explicitly configured as \"%s\"; automatic lights "
        "driver detection is disabled",
        drivers.c_str());
  }

  for (const std::string& driver : drivers_to_try) {
    RageDriver* ret = driver_list_.Create(driver);
    if (ret == nullptr) {
      LOG->Trace("Unknown lights driver: %s", driver.c_str());
      continue;
    }

    LightsDriver* p_driver = dynamic_cast<LightsDriver*>(ret);
    ASSERT(p_driver != nullptr);

    LOG->Info("Lights driver: %s", driver.c_str());
    add.push_back(p_driver);
  }

  // ensure these are always available to use for debugging
  // or if InputHandlers that want lighting state.
  add.push_back(new LightsDriver_SystemMessage);
  add.push_back(new LightsDriver_Export);
}

void LightsDriver::Reset() {
  LightsState state;
  ZERO(state.cabinet_lights_);
  ZERO(state.game_button_lights_);
  ZERO(state.coin_counter_);
  Set(&state);
}

/*
 * (c) 2002-2005 Glenn Maynard
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
