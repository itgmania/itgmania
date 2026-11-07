#include "DialogDriver_MacOSX.h"

#include <CoreFoundation/CoreFoundation.h>

#include <string>
#include <vector>

#include "InputFilter.h"
#include "ProductInfo.h"
#include "RageUtil.h"
#include "global.h"

REGISTER_DIALOG_DRIVER_CLASS(MacOSX);

static CFOptionFlags ShowAlert(
    CFOptionFlags flags, const std::string& message, CFStringRef ok,
    CFStringRef alt = nullptr, CFStringRef other = nullptr) {
  CFOptionFlags result;
  CFStringRef text = CFStringCreateWithCString(
      nullptr, message.c_str(), kCFStringEncodingUTF8);

  if (text == nullptr) {
    std::string error = ssprintf(
        "CFString for dialog string \"%s\" could not be created.",
        message.c_str());
    WARN(error);
    DEBUG_ASSERT_M(false, error);
    return kCFUserNotificationDefaultResponse;  // Is this better than
                                                // displaying an "unknown error"
                                                // message?
  }
  CFUserNotificationDisplayAlert(
      0.0, flags, nullptr, nullptr, nullptr, CFSTR(PRODUCT_FAMILY), text, ok,
      alt, other, &result);
  CFRelease(text);

  // Flush all input that's accumulated while the dialog box was up.
  if (INPUTFILTER) {
    std::vector<InputEvent> dummy;
    INPUTFILTER->Reset();
    INPUTFILTER->GetInputEvents(dummy);
  }

  return result;
}

#define LSTRING(b, x) \
  CFBundleCopyLocalizedString((b), CFSTR(x), nullptr, CFSTR("Localizable"))

void DialogDriver_MacOSX::OK(std::string message, std::string id) {
  CFBundleRef bundle = CFBundleGetMainBundle();
  CFStringRef dsa = LSTRING(bundle, "Don't show again");
  CFOptionFlags result = ShowAlert(
      kCFUserNotificationNoteAlertLevel, message.c_str(), CFSTR("OK"), dsa);

  CFRelease(dsa);
  if (result == kCFUserNotificationAlternateResponse) {
    Dialog::IgnoreMessage(id);
  }
}

void DialogDriver_MacOSX::Error(std::string error, std::string id) {
  ShowAlert(kCFUserNotificationStopAlertLevel, error.c_str(), CFSTR("OK"));
}

Dialog::Result DialogDriver_MacOSX::AbortRetryIgnore(
    std::string message, std::string id) {
  CFBundleRef bundle = CFBundleGetMainBundle();
  CFStringRef ignore = LSTRING(bundle, "Ignore");
  CFStringRef retry = LSTRING(bundle, "Retry");
  CFStringRef abort = LSTRING(bundle, "Abort");
  CFOptionFlags result = ShowAlert(
      kCFUserNotificationNoteAlertLevel, message.c_str(), ignore, retry, abort);

  CFRelease(ignore);
  CFRelease(retry);
  CFRelease(abort);
  switch (result) {
    case kCFUserNotificationDefaultResponse:
      Dialog::IgnoreMessage(id);
      return Dialog::ignore;
    case kCFUserNotificationAlternateResponse:
      return Dialog::retry;
    case kCFUserNotificationOtherResponse:
    case kCFUserNotificationCancelResponse:
      return Dialog::abort;
    default:
      FAIL_M(ssprintf("Invalid response: %d.", int(result)));
  }
}

Dialog::Result DialogDriver_MacOSX::AbortRetry(
    std::string message, std::string id) {
  CFBundleRef bundle = CFBundleGetMainBundle();
  CFStringRef retry = LSTRING(bundle, "Retry");
  CFStringRef abort = LSTRING(bundle, "Abort");
  CFOptionFlags result = ShowAlert(
      kCFUserNotificationNoteAlertLevel, message.c_str(), retry, abort);

  CFRelease(retry);
  CFRelease(abort);
  switch (result) {
    case kCFUserNotificationDefaultResponse:
    case kCFUserNotificationCancelResponse:
      return Dialog::abort;
    case kCFUserNotificationAlternateResponse:
      return Dialog::retry;
    default:
      FAIL_M(ssprintf("Invalid response: %d.", int(result)));
  }
}

/*
 * (c) 2003-2006 Steve Checkoway
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
