#include "Dialog.h"

#include <string>
#include <vector>

#include "DialogDriver.h"
#include "Preference.h"
#include "PrefsManager.h"
#include "RageLog.h"
#include "RageThreads.h"
#include "RageUtil.h"
#include "StdString.h"
#include "global.h"

static Preference<std::string> ignored_dialogs("IgnoredDialogs", "");

static DialogDriver* impl = nullptr;
static DialogDriver_Null null_driver;
static bool g_windowed =
    true;  // Start out true so that we'll show errors before DISPLAY is init'd.

static bool DialogsEnabled() { return g_windowed; }

void Dialog::Init() {
  if (impl != nullptr) {
    return;
  }

  impl = DialogDriver::Create();

  // DialogDriver_Null should have worked, at least.
  ASSERT(impl != nullptr);
}

void Dialog::Shutdown() {
  delete impl;
  impl = nullptr;
}

static bool MessageIsIgnored(std::string id) {
  std::vector<std::string> list;
  split(ignored_dialogs, ",", list);
  for (unsigned i = 0; i < list.size(); ++i) {
    if (!CompareNoCase(id, list[i])) {
      return true;
    }
  }
  return false;
}

void Dialog::IgnoreMessage(std::string id) {
  // We can't ignore messages before PREFSMAN is around.
  if (PREFSMAN == nullptr) {
    if (id != "" && LOG) {
      LOG->Warn(
          "Dialog: message \"%s\" set ID too early for ignorable messages",
          id.c_str());
    }
    return;
  }

  if (id == "") {
    return;
  }

  if (MessageIsIgnored(id)) {
    return;
  }

  std::vector<std::string> list;
  split(ignored_dialogs, ",", list);
  list.push_back(id);
  ignored_dialogs.Set(join(",", list));
  PREFSMAN->SavePrefsToDisk();
}

void Dialog::Error(std::string message, std::string id) {
  Dialog::Init();

  if (LOG) {
    LOG->Trace("Dialog: \"%s\" [%s]", message.c_str(), id.c_str());
  }

  if (id != "" && MessageIsIgnored(id)) {
    return;
  }

  RageThread::SetIsShowingDialog(true);

  impl->Error(message, id);

  RageThread::SetIsShowingDialog(false);
}

void Dialog::SetWindowed(bool windowed) { g_windowed = windowed; }

void Dialog::OK(std::string message, std::string id) {
  Dialog::Init();

  if (LOG) {
    LOG->Trace("Dialog: \"%s\" [%s]", message.c_str(), id.c_str());
  }

  if (id != "" && MessageIsIgnored(id)) {
    return;
  }

  RageThread::SetIsShowingDialog(true);

  // only show Dialog if windowed
  if (DialogsEnabled()) {
    impl->OK(message, id);  // call derived version
  } else {
    null_driver.OK(message, id);
  }

  RageThread::SetIsShowingDialog(false);
}

Dialog::Result Dialog::AbortRetryIgnore(std::string message, std::string id) {
  Dialog::Init();

  if (LOG) {
    LOG->Trace("Dialog: \"%s\" [%s]", message.c_str(), id.c_str());
  }

  if (id != "" && MessageIsIgnored(id)) {
    return null_driver.AbortRetryIgnore(message, id);
  }

  RageThread::SetIsShowingDialog(true);

  // only show Dialog if windowed
  Dialog::Result ret;
  if (DialogsEnabled()) {
    ret = impl->AbortRetryIgnore(message, id);  // call derived version
  } else {
    ret = null_driver.AbortRetryIgnore(message, id);
  }

  RageThread::SetIsShowingDialog(false);

  return ret;
}

Dialog::Result Dialog::AbortRetry(std::string message, std::string id) {
  Dialog::Init();

  if (LOG) {
    LOG->Trace("Dialog: \"%s\" [%s]", message.c_str(), id.c_str());
  }

  if (id != "" && MessageIsIgnored(id)) {
    return null_driver.AbortRetry(message, id);
  }

  RageThread::SetIsShowingDialog(true);

  // only show Dialog if windowed
  Dialog::Result ret;
  if (DialogsEnabled()) {
    ret = impl->AbortRetry(message, id);  // call derived version
  } else {
    ret = null_driver.AbortRetry(message, id);
  }

  RageThread::SetIsShowingDialog(false);

  return ret;
}

/*
 * (c) 2003-2004 Glenn Maynard, Chris Danford
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
