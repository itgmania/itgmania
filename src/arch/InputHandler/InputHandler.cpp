#include "InputHandler.h"

#include "InputFilter.h"
#include "InputHandler_MonkeyKeyboard.h"
#include "LocalizedString.h"
#include "RageException.h"
#include "RageInputDevice.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "arch/RageDriver.h"
#include "arch/arch_default.h"
#include "global.h"
// #include "InputHandler_NSEvent.hpp"

#include <algorithm>
#include <string>
#include <vector>

void InputHandler::UpdateTimer() {
  last_update_.Touch();
  inputs_since_update_ = 0;
}

void InputHandler::ButtonPressed(DeviceInput di) {
  if (di.ts_.IsZero()) {
    di.ts_ = last_update_.Half();
    ++inputs_since_update_;
  }

  INPUTFILTER->ButtonPressed(di);

  if (inputs_since_update_ >= 1000) {
    /*
     * We haven't received an update in a long time, so warn about it.  We
     * expect to receive input events before the first UpdateTimer call only on
     * the first update.  Leave inputs_since_update_ where it is, so we only
     * warn once.  Only updates that didn't provide a timestamp are counted; if
     * the driver provides its own timestamps, UpdateTimer is optional.
     */
    LOG->Warn(
        "InputHandler::ButtonPressed: Driver sent many updates without calling "
        "UpdateTimer");
    FAIL_M("x");
  }
}

wchar_t InputHandler::DeviceButtonToChar(
    DeviceButton button, bool use_current_key_modifiers) {
  wchar_t c = L'\0';
  switch (button) {
    default:
      if (button < 127) {
        c = (wchar_t)button;
      } else if (button >= KEY_KP_C0 && button <= KEY_KP_C9) {
        c = (wchar_t)(button - KEY_KP_C0) + '0';
      }
      break;
    case KEY_KP_SLASH:
      c = L'/';
      break;
    case KEY_KP_ASTERISK:
      c = L'*';
      break;
    case KEY_KP_HYPHEN:
      c = L'-';
      break;
    case KEY_KP_PLUS:
      c = L'+';
      break;
    case KEY_KP_PERIOD:
      c = L'.';
      break;
    case KEY_KP_EQUAL:
      c = L'=';
      break;
  }

  // Handle some default US keyboard modifiers for derived InputHandlers that
  // don't implement DeviceButtonToChar.
  if (use_current_key_modifiers) {
    bool holding_shift =
        INPUTFILTER->IsBeingPressed(DeviceInput(DEVICE_KEYBOARD, KEY_LSHIFT)) ||
        INPUTFILTER->IsBeingPressed(DeviceInput(DEVICE_KEYBOARD, KEY_RSHIFT));

    bool holding_ctrl =
        INPUTFILTER->IsBeingPressed(DeviceInput(DEVICE_KEYBOARD, KEY_LCTRL)) ||
        INPUTFILTER->IsBeingPressed(DeviceInput(DEVICE_KEYBOARD, KEY_RCTRL));

    // todo: handle Caps Lock -freem

    if (holding_shift && !holding_ctrl) {
      MakeUpper(&c, 1);

      switch (c) {
        case L'`':
          c = L'~';
          break;
        case L'1':
          c = L'!';
          break;
        case L'2':
          c = L'@';
          break;
        case L'3':
          c = L'#';
          break;
        case L'4':
          c = L'$';
          break;
        case L'5':
          c = L'%';
          break;
        case L'6':
          c = L'^';
          break;
        case L'7':
          c = L'&';
          break;
        case L'8':
          c = L'*';
          break;
        case L'9':
          c = L'(';
          break;
        case L'0':
          c = L')';
          break;
        case L'-':
          c = L'_';
          break;
        case L'=':
          c = L'+';
          break;
        case L'[':
          c = L'{';
          break;
        case L']':
          c = L'}';
          break;
        case L'\'':
          c = L'"';
          break;
        case L'\\':
          c = L'|';
          break;
        case L';':
          c = L':';
          break;
        case L',':
          c = L'<';
          break;
        case L'.':
          c = L'>';
          break;
        case L'/':
          c = L'?';
          break;
      }
    }
  }

  return c;
}

static LocalizedString HOME("DeviceButton", "Home");
static LocalizedString END("DeviceButton", "End");
static LocalizedString UP("DeviceButton", "Up");
static LocalizedString DOWN("DeviceButton", "Down");
static LocalizedString SPACE("DeviceButton", "Space");
static LocalizedString SHIFT("DeviceButton", "Shift");
static LocalizedString CTRL("DeviceButton", "Ctrl");
static LocalizedString ALT("DeviceButton", "Alt");
static LocalizedString INSERT("DeviceButton", "Insert");
static LocalizedString DEL("DeviceButton", "Delete");
static LocalizedString PGUP("DeviceButton", "PgUp");
static LocalizedString PGDN("DeviceButton", "PgDn");
static LocalizedString BACKSLASH("DeviceButton", "Backslash");

std::string InputHandler::GetDeviceSpecificInputString(const DeviceInput& di) {
  if (di.device_ == InputDevice_Invalid) {
    return std::string();
  }

  if (di.device_ == DEVICE_KEYBOARD) {
    if (di.button_ >= KEY_KP_C0 && di.button_ <= KEY_KP_ENTER) {
      return DeviceButtonToString(di.button_);
    }

    wchar_t c = DeviceButtonToChar(di.button_, false);
    if (c && c != L' ') {  // Don't show "Key  " for space.
      return InputDeviceToString(di.device_) + " " +
             Capitalize(WStringToRString(std::wstring() + c));
    }
  }

  std::string s = DeviceButtonToString(di.button_);
  if (di.device_ != DEVICE_KEYBOARD) {
    s = InputDeviceToString(di.device_) + " " + s;
  }
  return s;
}

std::string InputHandler::GetLocalizedInputString(const DeviceInput& di) {
  switch (di.button_) {
    case KEY_HOME:
      return HOME.GetValue();
    case KEY_END:
      return END.GetValue();
    case KEY_UP:
      return UP.GetValue();
    case KEY_DOWN:
      return DOWN.GetValue();
    case KEY_SPACE:
      return SPACE.GetValue();
    case KEY_LSHIFT:
    case KEY_RSHIFT:
      return SHIFT.GetValue();
    case KEY_LCTRL:
    case KEY_RCTRL:
      return CTRL.GetValue();
    case KEY_LALT:
    case KEY_RALT:
      return ALT.GetValue();
    case KEY_INSERT:
      return INSERT.GetValue();
    case KEY_DEL:
      return DEL.GetValue();
    case KEY_PGUP:
      return PGUP.GetValue();
    case KEY_PGDN:
      return PGDN.GetValue();
    case KEY_BACKSLASH:
      return BACKSLASH.GetValue();
    default:
      wchar_t c = DeviceButtonToChar(di.button_, false);
      if (c && c != L' ') {  // Don't show "Key  " for space.
        return Capitalize(WStringToRString(std::wstring() + c));
      }

      return DeviceButtonToString(di.button_);
  }
}

DriverList InputHandler::driver_list_;

namespace {
struct AutoDetectInputDriver {
  std::string name_;
  InputHandler::AutoDetectFunction should_load_;
};

std::vector<AutoDetectInputDriver>& GetAutoDetectInputDrivers() {
  static std::vector<AutoDetectInputDriver> drivers;
  return drivers;
}
}  // namespace

void InputHandler::RegisterAutoDetectDriver(
    const std::string& name, AutoDetectFunction is_present) {
  GetAutoDetectInputDrivers().push_back({name, is_present});
}

static LocalizedString INPUT_HANDLERS_EMPTY(
    "Arch", "Input Handlers cannot be empty.");
void InputHandler::Create(
    const std::string& drivers, std::vector<InputHandler*>& add) {
  const std::vector<std::string>& drivers_to_try =
      drivers.empty() ? GetDefaultInputDriverList() : split(drivers, ',', true);

  if (drivers_to_try.empty()) {
    RageException::Throw("%s", INPUT_HANDLERS_EMPTY.GetValue().c_str());
  }

  // Optional drivers may opt into startup detection using whatever predicate
  // is appropriate for them (hardware enumeration, a required DLL, and so on).
  // A non-empty InputDrivers preference is authoritative, so auto-detection is
  // used only alongside the platform's default driver list.
  if (drivers.empty()) {
    for (const AutoDetectInputDriver& auto_driver :
         GetAutoDetectInputDrivers()) {
      const bool already_default =
          std::find(
              drivers_to_try.begin(), drivers_to_try.end(),
              auto_driver.name_) != drivers_to_try.end();
      if (already_default || !auto_driver.should_load_()) {
        continue;
      }

      LOG->Info(
          "Auto-detected %s; loading input handler", auto_driver.name_.c_str());
      RageDriver* driver = InputHandler::driver_list_.Create(auto_driver.name_);
      if (driver == nullptr) {
        LOG->Warn(
            "Auto-detected input handler %s is not registered",
            auto_driver.name_.c_str());
        continue;
      }

      InputHandler* handler = dynamic_cast<InputHandler*>(driver);
      DEBUG_ASSERT(handler != nullptr);
      add.push_back(handler);
    }
  } else {
    LOG->Info(
        "InputDrivers is explicitly configured as \"%s\"; automatic input "
        "driver detection is disabled",
        drivers.c_str());
  }

  for (const std::string& s : drivers_to_try) {
    RageDriver* driver = InputHandler::driver_list_.Create(s);
    if (driver == nullptr) {
      LOG->Trace("Unknown Input Handler name: %s", s.c_str());
      continue;
    }

    InputHandler* ret = dynamic_cast<InputHandler*>(driver);
    DEBUG_ASSERT(ret);
    add.push_back(ret);
  }

  // Always add
  add.push_back(new InputHandler_MonkeyKeyboard);
  //    add.push_back(new InputHandler_NSEvent);
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
