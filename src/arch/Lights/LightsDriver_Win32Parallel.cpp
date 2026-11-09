#include "LightsDriver_Win32Parallel.h"

#include "RageUtil.h"
#include "global.h"
#include "windows.h"

REGISTER_LIGHTS_DRIVER_CLASS(Win32Parallel);

HINSTANCE g_dll = nullptr;

typedef void(WINAPI PORTOUT)(short int port, char data);
PORTOUT* port_out = nullptr;

const int kLightsPerParallelPort = 8;
// xxx: don't hardcode the port addresses. -aj
const int kMaxParallelPorts = 3;
short lpt_address[kMaxParallelPorts] = {
    0x378,  // LPT1
    0x278,  // LPT2
    0x3bc,  // LPT3
};

int CabinetLightToIndex(CabinetLight cl) { return cl; }

int GameControllerAndGameButtonToIndex(GameController gc, GameButton gb) {
  rage_clamp((int&)gb, 0, 4);
  return NUM_CabinetLight + gc * 4 + gb;
}

void IndexToLptAndPin(int index, int& lpt_out, int& pin_out) {
  lpt_out = index / kLightsPerParallelPort;
  ASSERT(lpt_out >= 0 && lpt_out < kMaxParallelPorts);
  pin_out = index % kLightsPerParallelPort;
}

LightsDriver_Win32Parallel::LightsDriver_Win32Parallel() {
  // init io.dll
  g_dll = LoadLibrary("parallel_lights_io.dll");
  if (g_dll == nullptr) {
    MessageBox(
        nullptr, "Could not LoadLibrary( parallel_lights_io.dll ).", "ERROR",
        MB_OK);
    return;
  }

  // Get the function pointers
  port_out = (PORTOUT*)GetProcAddress(g_dll, "PortOut");
}

LightsDriver_Win32Parallel::~LightsDriver_Win32Parallel() {
  FreeLibrary(g_dll);
}

void LightsDriver_Win32Parallel::Set(const LightsState* ls) {
  BYTE data[kMaxParallelPorts] = {0x00, 0x00, 0x00};

  {
    FOREACH_CabinetLight(cl) {
      bool on = ls->cabinet_lights_[cl];
      int index = CabinetLightToIndex(cl);
      int lpt;
      int pin;
      IndexToLptAndPin(index, lpt, pin);
      BYTE mask = (BYTE)(0x01 << pin);
      if (on) {
        data[lpt] |= mask;
      } else {
        data[lpt] &= ~mask;
      }
    }
  }

  FOREACH_ENUM(GameController, gc) {
    FOREACH_ENUM(GameButton, gb) {
      bool on = ls->game_button_lights_[gc][gb];
      int index = GameControllerAndGameButtonToIndex(gc, gb);
      int lpt;
      int pin;
      IndexToLptAndPin(index, lpt, pin);
      BYTE mask = (BYTE)(0x01 << pin);
      if (on) {
        data[lpt] |= mask;
      } else {
        data[lpt] &= ~mask;
      }
    }
  }

  {
    for (int i = 0; i < kMaxParallelPorts; i++) {
      short address = lpt_address[i];
      port_out(address, data[i]);
    }
  }
}

/*
 * (c) 2003-2004 Chris Danford
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
