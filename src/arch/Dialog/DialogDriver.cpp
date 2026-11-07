#include "DialogDriver.h"

#include <map>
#include <string>
#include <vector>

#include "RageLog.h"
#include "RageUtil.h"
#include "global.h"

std::map<istring, CreateDialogDriverFn>* RegisterDialogDriver::registrees_;
RegisterDialogDriver::RegisterDialogDriver(
    const istring& name, CreateDialogDriverFn pfn) {
  if (registrees_ == nullptr) {
    registrees_ = new std::map<istring, CreateDialogDriverFn>;
  }

  ASSERT(registrees_->find(name) == registrees_->end());
  (*registrees_)[name] = pfn;
}

REGISTER_DIALOG_DRIVER_CLASS(Null);

DialogDriver* DialogDriver::Create() {
  std::string drivers = "win32,macosx,null";
  std::vector<std::string> drivers_to_try;
  split(drivers, ",", drivers_to_try, true);

  ASSERT(drivers_to_try.size() != 0);

  for (const std::string& driver : drivers_to_try) {
    std::map<istring, CreateDialogDriverFn>::const_iterator iter =
        RegisterDialogDriver::registrees_->find(istring(driver.c_str()));

    if (iter == RegisterDialogDriver::registrees_->end()) {
      continue;
    }

    DialogDriver* ret = (iter->second)();
    DEBUG_ASSERT(ret);
    const std::string error = ret->Init();

    if (error.empty()) {
      return ret;
    }
    if (LOG) {
      LOG->Info("Couldn't load driver %s: %s", driver.c_str(), error.c_str());
    }
    RageUtil::SafeDelete(ret);
  }
  return nullptr;
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
