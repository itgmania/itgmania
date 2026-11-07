#ifndef INPUTHANDLER_DIRECTINPUT_HELPER_H
#define INPUTHANDLER_DIRECTINPUT_HELPER_H

#include <string>
#include <vector>

#include "InputFilter.h"

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
extern LPDIRECTINPUT8 g_dinput;

#define INPUT_QSIZE 32

struct input_t {
  // DirectInput offset for this input type:
  DWORD ofs_;

  // Button, axis or hat:
  enum Type { KEY, BUTTON, AXIS, HAT } type_;

  int num_;

  // Comparitor for finding the input_t with the matching ofs member in std
  // containers.
  class Compare {
   public:
    Compare(DWORD ofs) : ofs_(ofs) {}

    bool operator()(const input_t& input) const { return input.ofs_ == ofs_; }

   private:
    DWORD ofs_;
  };
};

struct DIDevice {
  DIDEVICEINSTANCE joystick_inst_;
  LPDIRECTINPUTDEVICE8 device_;
  std::string name_;

  enum { KEYBOARD, JOYSTICK, MOUSE } type_;

  bool buffered_;
  int buttons_, axes_, hats_;
  std::vector<input_t> inputs_;
  InputDevice dev_;

  DIDevice();

  bool Open();
  void Close();
};

struct XIDevice {
  std::string name_;
  DWORD x_input_slot_;
  InputDevice dev_;

  XIDevice();
};

#endif

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
