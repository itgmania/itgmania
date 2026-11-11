/* Define all of the input devices we know about. This is the public interface
 * for describing input devices. */

#include "RageInputDevice.h"

#include <cstdio>
#include <map>
#include <string>
#include <utility>

#include "EnumHelper.h"
#include "RageUtil.h"

static const char* InputDeviceStateNames[] = {
    "Connected",
    "Unplugged",
    "NeedsMultitap",
    "NoInputHandler",
};
XToString(InputDeviceState);
XToLocalizedString(InputDeviceState);
LuaXType(InputDevice);

static std::map<DeviceButton, std::string> map_names_to_string;
static std::map<std::string, DeviceButton> map_string_to_names;
static void InitNames() {
  if (!map_names_to_string.empty()) {
    return;
  }

  map_names_to_string[KEY_PERIOD] = "period";
  map_names_to_string[KEY_COMMA] = "comma";
  map_names_to_string[KEY_COLON] = "colon";
  map_names_to_string[KEY_SPACE] = "space";
  map_names_to_string[KEY_DEL] = "delete";
  map_names_to_string[KEY_BACKSLASH] = "backslash";

  map_names_to_string[KEY_BACK] = "backspace";
  map_names_to_string[KEY_TAB] = "tab";
  map_names_to_string[KEY_ENTER] = "enter";
  map_names_to_string[KEY_PAUSE] = "pause";
  map_names_to_string[KEY_ESC] = "escape";

  map_names_to_string[KEY_F1] = "F1";
  map_names_to_string[KEY_F2] = "F2";
  map_names_to_string[KEY_F3] = "F3";
  map_names_to_string[KEY_F4] = "F4";
  map_names_to_string[KEY_F5] = "F5";
  map_names_to_string[KEY_F6] = "F6";
  map_names_to_string[KEY_F7] = "F7";
  map_names_to_string[KEY_F8] = "F8";
  map_names_to_string[KEY_F9] = "F9";
  map_names_to_string[KEY_F10] = "F10";
  map_names_to_string[KEY_F11] = "F11";
  map_names_to_string[KEY_F12] = "F12";
  map_names_to_string[KEY_F13] = "F13";
  map_names_to_string[KEY_F14] = "F14";
  map_names_to_string[KEY_F15] = "F15";
  map_names_to_string[KEY_F16] = "F16";

  map_names_to_string[KEY_LCTRL] = "left ctrl";
  map_names_to_string[KEY_RCTRL] = "right ctrl";
  map_names_to_string[KEY_LSHIFT] = "left shift";
  map_names_to_string[KEY_RSHIFT] = "right shift";
  map_names_to_string[KEY_LALT] = "left alt";
  map_names_to_string[KEY_RALT] = "right alt";
  map_names_to_string[KEY_LMETA] = "left meta";
  map_names_to_string[KEY_RMETA] = "right meta";
  // Note: On Windows, the Super key is the Windows key. -aj
  map_names_to_string[KEY_LSUPER] = "left super";
  map_names_to_string[KEY_RSUPER] = "right super";
  map_names_to_string[KEY_MENU] = "menu";

  map_names_to_string[KEY_FN] = "function";

  map_names_to_string[KEY_NUMLOCK] = "num lock";
  map_names_to_string[KEY_SCRLLOCK] = "scroll lock";
  map_names_to_string[KEY_CAPSLOCK] = "caps lock";
  map_names_to_string[KEY_PRTSC] = "prtsc";

  map_names_to_string[KEY_UP] = "up";
  map_names_to_string[KEY_DOWN] = "down";
  map_names_to_string[KEY_LEFT] = "left";
  map_names_to_string[KEY_RIGHT] = "right";

  map_names_to_string[KEY_INSERT] = "insert";
  map_names_to_string[KEY_HOME] = "home";
  map_names_to_string[KEY_END] = "end";
  map_names_to_string[KEY_PGUP] = "pgup";
  map_names_to_string[KEY_PGDN] = "pgdn";

  map_names_to_string[KEY_KP_C0] = "KP 0";
  map_names_to_string[KEY_KP_C1] = "KP 1";
  map_names_to_string[KEY_KP_C2] = "KP 2";
  map_names_to_string[KEY_KP_C3] = "KP 3";
  map_names_to_string[KEY_KP_C4] = "KP 4";
  map_names_to_string[KEY_KP_C5] = "KP 5";
  map_names_to_string[KEY_KP_C6] = "KP 6";
  map_names_to_string[KEY_KP_C7] = "KP 7";
  map_names_to_string[KEY_KP_C8] = "KP 8";
  map_names_to_string[KEY_KP_C9] = "KP 9";
  map_names_to_string[KEY_KP_SLASH] = "KP /";
  map_names_to_string[KEY_KP_ASTERISK] = "KP *";
  map_names_to_string[KEY_KP_HYPHEN] = "KP -";
  map_names_to_string[KEY_KP_PLUS] = "KP +";
  map_names_to_string[KEY_KP_PERIOD] = "KP .";
  map_names_to_string[KEY_KP_EQUAL] = "KP =";
  map_names_to_string[KEY_KP_ENTER] = "KP enter";

  map_names_to_string[JOY_LEFT] = "Left1";
  map_names_to_string[JOY_RIGHT] = "Right1";
  map_names_to_string[JOY_UP] = "Up1";
  map_names_to_string[JOY_DOWN] = "Down1";

  // Secondary sticks:
  map_names_to_string[JOY_LEFT_2] = "Left2";
  map_names_to_string[JOY_RIGHT_2] = "Right2";
  map_names_to_string[JOY_UP_2] = "Up2";
  map_names_to_string[JOY_DOWN_2] = "Down2";

  map_names_to_string[JOY_Z_UP] = "Z-Up";
  map_names_to_string[JOY_Z_DOWN] = "Z-Down";
  map_names_to_string[JOY_ROT_UP] = "R-Up";
  map_names_to_string[JOY_ROT_DOWN] = "R-Down";
  map_names_to_string[JOY_ROT_LEFT] = "R-Left";
  map_names_to_string[JOY_ROT_RIGHT] = "R-Right";
  map_names_to_string[JOY_ROT_Z_UP] = "ZR-Up";
  map_names_to_string[JOY_ROT_Z_DOWN] = "ZR-Down";
  map_names_to_string[JOY_HAT_LEFT] = "H-Left";
  map_names_to_string[JOY_HAT_RIGHT] = "H-Right";
  map_names_to_string[JOY_HAT_UP] = "H-Up";
  map_names_to_string[JOY_HAT_DOWN] = "H-Down";
  map_names_to_string[JOY_AUX_1] = "Aux1";
  map_names_to_string[JOY_AUX_2] = "Aux2";
  map_names_to_string[JOY_AUX_3] = "Aux3";
  map_names_to_string[JOY_AUX_4] = "Aux4";

  map_names_to_string[MOUSE_LEFT] = "left mouse button";
  map_names_to_string[MOUSE_RIGHT] = "right mouse button";
  map_names_to_string[MOUSE_MIDDLE] = "middle mouse button";
  map_names_to_string[MOUSE_WHEELUP] = "mousewheel up";
  map_names_to_string[MOUSE_WHEELDOWN] = "mousewheel down";

  for (std::pair<const DeviceButton&, const std::string&> m :
       map_names_to_string) {
    map_string_to_names[m.second] = m.first;
  }
}

/* Return a reversible representation of a DeviceButton. This is not affected
 * by InputDrivers, localization or the keyboard language. */
std::string DeviceButtonToString(DeviceButton key) {
  InitNames();

  // Check the name map first to allow making names for keys that are inside
  // the ascii range. -Kyz
  std::map<DeviceButton, std::string>::const_iterator it =
      map_names_to_string.find(key);
  if (it != map_names_to_string.end()) {
    return it->second;
  }

  // All printable ASCII except for uppercase alpha characters line up.
  if (key >= 33 && key < 127 && !(key >= 'A' && key <= 'Z')) {
    return ssprintf("%c", key);
  }

  if (key >= KEY_OTHER_0 && key < KEY_LAST_OTHER) {
    return ssprintf("unk %i", key - KEY_OTHER_0);
  }

  if (key >= JOY_BUTTON_1 && key <= JOY_BUTTON_32) {
    return ssprintf("B%i", key - JOY_BUTTON_1 + 1);
  }

  if (key >= MIDI_FIRST && key <= MIDI_LAST) {
    return ssprintf("Midi %d", key - MIDI_FIRST);
  }

  return "unknown";
}

DeviceButton StringToDeviceButton(const std::string& s) {
  InitNames();

  if (s.size() == 1) {
    return (DeviceButton)s[0];
  }

  int i;
  if (sscanf(s.c_str(), "unk %i", &i) == 1) {
    return enum_add2(KEY_OTHER_0, i);
  }

  if (sscanf(s.c_str(), "B%i", &i) == 1) {
    return enum_add2(JOY_BUTTON_1, i - 1);
  }

  if (sscanf(s.c_str(), "Midi %i", &i) == 1) {
    return enum_add2(MIDI_FIRST, i);
  }

  if (sscanf(s.c_str(), "Mouse %i", &i) == 1) {
    return enum_add2(MOUSE_LEFT, i);
  }

  std::map<std::string, DeviceButton>::const_iterator it =
      map_string_to_names.find(s);
  if (it != map_string_to_names.end()) {
    return it->second;
  }

  return DeviceButton_Invalid;
}
LuaXType(DeviceButton);

static const char* InputDeviceNames[] = {
    "Key",   "Joy1",  "Joy2",  "Joy3",  "Joy4",  "Joy5",  "Joy6",    "Joy7",
    "Joy8",  "Joy9",  "Joy10", "Joy11", "Joy12", "Joy13", "Joy14",   "Joy15",
    "Joy16", "Joy17", "Joy18", "Joy19", "Joy20", "Joy21", "Joy22",   "Joy23",
    "Joy24", "Joy25", "Joy26", "Joy27", "Joy28", "Joy29", "Joy30",   "Joy31",
    "Joy32", "Pump1", "Pump2", "Midi",  "Mouse", "PIUIO", "PumpHID",
};
XToString(InputDevice);
StringToX(InputDevice);

/* Return a reversible representation of a DeviceInput. This is not affected by
 * InputDrivers, localization or the keyboard language. */
std::string DeviceInput::ToString() const {
  if (device_ == InputDevice_Invalid) {
    return std::string();
  }

  std::string s =
      InputDeviceToString(device_) + "_" + DeviceButtonToString(button_);
  return s;
}

bool DeviceInput::FromString(const std::string& s) {
  char device[32] = "";
  char button[32] = "";

  if (2 != sscanf(s.c_str(), "%31[^_]_%31[^_]", device, button)) {
    device_ = InputDevice_Invalid;
    return false;
  }

  device_ = StringToInputDevice(device);
  button_ = StringToDeviceButton(button);
  return true;
}

/*
 * Copyright (c) 2001-2002 Chris Danford
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
