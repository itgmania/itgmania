#include "RageDriver.h"

#include <map>
#include <string>

#include "RageUtil.h"
#include "global.h"

void DriverList::Add(const istring& name, CreateRageDriverFn pfn) {
  if (registrees_ == nullptr) {
    registrees_ = new std::map<istring, CreateRageDriverFn>;
  }

  ASSERT(registrees_->find(name) == registrees_->end());
  (*registrees_)[name] = pfn;
}

RageDriver* DriverList::Create(const std::string& driver_name) {
  if (registrees_ == nullptr) {
    return nullptr;
  }

  std::map<istring, CreateRageDriverFn>::const_iterator iter =
      registrees_->find(istring(driver_name.c_str()));
  if (iter == registrees_->end()) {
    return nullptr;
  }
  return (iter->second)();
}

RegisterRageDriver::RegisterRageDriver(
    DriverList* driver_list, const istring& name, CreateRageDriverFn pfn) {
  driver_list->Add(name, pfn);
}

/*
 * (c) 2006 Glenn Maynard
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
