#include "MovieTexture.h"

#include <string>
#include <vector>

#include "LocalizedString.h"
#include "Preference.h"
#include "RageException.h"
#include "RageFile.h"
#include "RageLog.h"
#include "RageTextureID.h"
#include "RageUtil.h"
#include "StdString.h"
#include "arch/RageDriver.h"
#include "arch/arch_default.h"
#include "global.h"

void ForceToAscii(std::string& str) {
  for (unsigned i = 0; i < str.size(); ++i) {
    if (str[i] < 0x20 || str[i] > 0x7E) {
      str[i] = '?';
    }
  }
}

bool RageMovieTexture::GetFourCC(
    std::string fn, std::string& handler, std::string& type) {
  std::string ignore, ext;
  splitpath(fn, ignore, ignore, ext);
  if (!CompareNoCase(ext, ".mpg") || !CompareNoCase(ext, ".mpeg") ||
      !CompareNoCase(ext, ".mpv") || !CompareNoCase(ext, ".mpe")) {
    handler = type = "MPEG";
    return true;
  }
  if (!CompareNoCase(ext, ".ogv")) {
    handler = type = "Ogg";
    return true;
  }

  // Not very pretty but should do all the same error checking without iostream
#define HANDLE_ERROR(x)                               \
  {                                                   \
    LOG->Warn("Error reading %s: %s", fn.c_str(), x); \
    handler = type = "";                              \
    return false;                                     \
  }

  RageFile file;
  if (!file.Open(fn)) {
    HANDLE_ERROR("Could not open file.");
  }
  if (!file.Seek(0x70)) {
    HANDLE_ERROR("Could not seek.");
  }
  type = "    ";
  if (file.Read((char*)type.c_str(), 4) != 4) {
    HANDLE_ERROR("Could not read.");
  }
  ForceToAscii(type);

  if (file.Seek(0xBC) != 0xBC) {
    HANDLE_ERROR("Could not seek.");
  }
  handler = "    ";
  if (file.Read((char*)handler.c_str(), 4) != 4) {
    HANDLE_ERROR("Could not read.");
  }
  ForceToAscii(handler);

  return true;
#undef HANDLE_ERROR
}

DriverList RageMovieTextureDriver::driver_list_;

// Helper for MakeRageMovieTexture()
static void DumpAVIDebugInfo(const std::string& fn) {
  std::string type, handler;
  if (!RageMovieTexture::GetFourCC(fn, handler, type)) {
    return;
  }

  LOG->Trace(
      "Movie %s has handler '%s', type '%s'", fn.c_str(), handler.c_str(),
      type.c_str());
}

static Preference<std::string> g_sMovieDrivers(
    "MovieDrivers", "");  // "" == default
/* Try drivers in order of preference until we find one that works. */
static LocalizedString MOVIE_DRIVERS_EMPTY(
    "Arch", "Movie Drivers cannot be empty.");
static LocalizedString COULDNT_CREATE_MOVIE_DRIVER(
    "Arch", "Couldn't create a movie driver.");
RageMovieTexture* RageMovieTexture::Create(RageTextureID id) {
  DumpAVIDebugInfo(id.filename_);

  std::string drivers = g_sMovieDrivers;
  std::vector<std::string> drivers_to_try;

  if (drivers.empty()) {
    drivers_to_try = GetDefaultMovieDriverList();
  } else {
    drivers_to_try = split(drivers, ',');
  }

  if (drivers_to_try.empty()) {
    RageException::Throw("%s", MOVIE_DRIVERS_EMPTY.GetValue().c_str());
  }

  RageMovieTexture* ret = nullptr;

  for (const std::string& driver : drivers_to_try) {
    const char* driver_string = driver.c_str();
    LOG->Trace("Initializing driver: %s", driver_string);
    RageDriver* driver_base =
        RageMovieTextureDriver::driver_list_.Create(driver);

    if (driver_base == nullptr) {
      LOG->Trace("Unknown movie driver name: %s", driver_string);
      continue;
    }

    RageMovieTextureDriver* p_driver =
        dynamic_cast<RageMovieTextureDriver*>(driver_base);
    ASSERT(p_driver != nullptr);

    std::string error;
    ret = p_driver->Create(id, error);
    delete p_driver;

    if (ret == nullptr) {
      LOG->Trace("Couldn't load driver %s: %s", driver_string, error.c_str());
      RageUtil::SafeDelete(ret);
      continue;
    }
    LOG->Trace(
        "Created movie texture \"%s\" with driver \"%s\"", id.filename_.c_str(),
        driver_string);
    break;
  }
  if (!ret) {
    RageException::Throw("%s", COULDNT_CREATE_MOVIE_DRIVER.GetValue().c_str());
  }

  return ret;
}

/*
 * (c) 2003-2004 Glenn Maynard
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
