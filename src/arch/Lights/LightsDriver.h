#ifndef LightsDriver_H
#define LightsDriver_H

#include <string>
#include <vector>

#include "LightsManager.h"
#include "arch/RageDriver.h"

struct LightsState;
/** @brief Controls the lights. */
class LightsDriver : public RageDriver {
 public:
  // A startup-only predicate indicating whether an optional lights driver
  // should be loaded automatically. The predicate may use any inexpensive,
  // side-effect-free check appropriate for the driver.
  using AutoDetectFunction = bool (*)();

  static void Create(
      const std::string& drivers, std::vector<LightsDriver*>& add,
      bool allow_auto_detect);
  static void RegisterAutoDetectDriver(
      const std::string& name, AutoDetectFunction should_load);
  static DriverList driver_list_;

  LightsDriver() {}
  virtual ~LightsDriver() {}

  virtual void Set(const LightsState* ls) = 0;

  // Called from the main thread once per frame. Drivers that need to publish
  // state to main-thread-only systems can hand it off here from Set().
  virtual void Update() {}

  // Reset all lights to off
  void Reset();
};

class RegisterAutoDetectLightsDriver {
 public:
  RegisterAutoDetectLightsDriver(
      const std::string& name, LightsDriver::AutoDetectFunction should_load) {
    LightsDriver::RegisterAutoDetectDriver(name, should_load);
  }
};

#define REGISTER_LIGHTS_DRIVER_CLASS2(name, x) \
  static RegisterRageDriver register_##x(      \
      &LightsDriver::driver_list_, #name,      \
      CreateClass<LightsDriver_##x, RageDriver>)
#define REGISTER_LIGHTS_DRIVER_CLASS(name) \
  REGISTER_LIGHTS_DRIVER_CLASS2(name, name)
// Opts a lights driver into startup auto-detection. LightsDriver_<class_name>
// must provide a static ShouldAutoLoad() predicate.
#define REGISTER_AUTODETECT_LIGHTS_DRIVER_CLASS2(name, class_name)  \
  static RegisterAutoDetectLightsDriver register_autodetect_##name( \
      #name, &LightsDriver_##class_name::ShouldAutoLoad)
#define REGISTER_AUTODETECT_LIGHTS_DRIVER_CLASS(name) \
  REGISTER_AUTODETECT_LIGHTS_DRIVER_CLASS2(name, name)

// allows you to make a new class name, but register as an older alias name.
// ex: REGISTER_LIGHTS_DRIVER_ALIAS(LinuxMinimaid, MinimaidHID) will allow older
// preference files to still use the older implementation.
#define REGISTER_LIGHTS_DRIVER_ALIAS(alias_name, actual_class) \
  static RegisterRageDriver register_##alias_name(             \
      &LightsDriver::driver_list_, #alias_name,                \
      CreateClass<LightsDriver_##actual_class, RageDriver>)

#endif

/**
 * @file
 * @author Chris Danford (c) 2003-2004
 * @section LICENSE
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
