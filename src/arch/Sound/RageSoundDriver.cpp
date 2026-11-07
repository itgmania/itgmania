#include "RageSoundDriver.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "RageLog.h"
#include "RageSoundManager.h"
#include "RageUtil.h"
#include "arch/RageDriver.h"
#include "arch/arch_default.h"
#include "global.h"

namespace {
void WarnUserAboutBadSoundDriverEntry() {
  std::vector<std::string> list = GetDefaultSoundDriverList();
  std::string list_string = join(",", list);
  std::string trace =
      std::string(" - Valid sound drivers for your OS are: ") + list_string;
  LOG->Trace("%s", trace.c_str());
  LOG->Trace(
      " - Make sure the driver entry is spelled correctly, and is supported on "
      "your OS.");
}
}  // namespace

DriverList RageSoundDriver::driver_list_;

RageSoundDriver* RageSoundDriver::Create(const std::string& drivers) {
  std::vector<std::string> drivers_to_try;
  if (drivers.empty()) {
    drivers_to_try = GetDefaultSoundDriverList();
  } else {
    size_t start = 0;
    size_t end = drivers.find(',');

    while (end != std::string::npos) {
      drivers_to_try.emplace_back(drivers.substr(start, end - start));
      start = end + 1;
      end = drivers.find(',', start);
    }

    drivers_to_try.emplace_back(drivers.substr(start));

    size_t to_try = 0;

    while (to_try < drivers_to_try.size()) {
      if (std::find(
              GetDefaultSoundDriverList().begin(),
              GetDefaultSoundDriverList().end(),
              drivers_to_try[to_try]) == GetDefaultSoundDriverList().end()) {
        LOG->Warn(
            "Removed unusable sound driver %s", drivers_to_try[to_try].c_str());
        WarnUserAboutBadSoundDriverEntry();
        drivers_to_try.erase(drivers_to_try.begin() + to_try);
      } else {
        ++to_try;
      }
    }
    if (drivers_to_try.empty()) {
      drivers_to_try = GetDefaultSoundDriverList();
    }
  }

  for (const std::string& driver : drivers_to_try) {
    RageDriver* p_driver = driver_list_.Create(driver);
    const char* driver_string = driver.c_str();
    if (p_driver == nullptr) {
      LOG->Trace("Unknown sound driver: %s", driver_string);
      continue;
    }

    RageSoundDriver* ret = dynamic_cast<RageSoundDriver*>(p_driver);
    ASSERT(ret != nullptr);

    const std::string error = ret->Init();
    if (error.empty()) {
      LOG->Info("Sound driver: %s", driver_string);
      return ret;
    }
    LOG->Info("Couldn't load driver %s: %s", driver_string, error.c_str());
    RageUtil::SafeDelete(ret);
  }
  return nullptr;
}

std::vector<std::string> RageSoundDriver::GetSoundDriverList() {
  return GetDefaultSoundDriverList();
}

/*
 * (c) 2002-2006 Glenn Maynard, Steve Checkoway
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
