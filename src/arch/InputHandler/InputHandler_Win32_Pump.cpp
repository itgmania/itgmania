#include "InputHandler_Win32_Pump.h"

#include <string>
#include <vector>

#include "PrefsManager.h"
#include "RageInputDevice.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "archutils/Win32/ErrorStrings.h"
#include "archutils/Win32/USB.h"
#include "global.h"

REGISTER_INPUT_HANDLER_CLASS2(Pump, Win32_Pump);

InputHandler_Win32_Pump::InputHandler_Win32_Pump() {
  shutdown_ = false;
  const int kPumpUsbVid = 0x0d2f;
  const int kPumpUsbPids[2] = {
      0x0001 /* older model */, 0x0003 /* shipped with Exceed */
  };

  device_ = new USBDevice[NUM_PUMPS];

  int num_found = 0;
  // TODO: Use C++1x semantics to loop through the pids.
  // Hardcoding for now to keep this moving.
  for (int p = 0; p < 2; ++p) {
    const int pump_usb_pid = kPumpUsbPids[p];
    for (int i = 0; i < NUM_PUMPS; ++i) {
      if (device_[i].Open(
              kPumpUsbVid, pump_usb_pid, sizeof(long), i, nullptr)) {
        num_found++;
        LOG->Info("Found Pump pad %i", num_found);
      }
    }
  }

  /* Don't start a thread if we have no pads. */
  if (num_found > 0 && PREFSMAN->threaded_input_) {
    input_thread_.SetName("Pump thread");
    input_thread_.Create(InputThread_Start, this);
  }
}

InputHandler_Win32_Pump::~InputHandler_Win32_Pump() {
  if (input_thread_.IsCreated()) {
    shutdown_ = true;
    LOG->Trace("Shutting down Pump thread ...");
    input_thread_.Wait();
    LOG->Trace("Pump thread shut down.");
  }

  delete[] device_;
}

void InputHandler_Win32_Pump::HandleInput(int device, int event) {
  static const int kBits[] = {
      /* P1 */ (1 << 9),   (1 << 12), (1 << 13), (1 << 11), (1 << 10),
      /* ESC */ (1 << 16),
      /* P1 */ (1 << 17),  (1 << 20), (1 << 21), (1 << 19), (1 << 18),
  };

  InputDevice id = InputDevice(DEVICE_PUMP1 + device);

  for (int button = 0; button < ARRAYLEN(kBits); ++button) {
    DeviceInput di(
        id, enum_add2(JOY_BUTTON_1, button), !(event & kBits[button]));

    /* If we're in a thread, our timestamp is accurate. */
    if (input_thread_.IsCreated()) {
      di.ts_.Touch();
    }

    ButtonPressed(di);
  }
}

std::string InputHandler_Win32_Pump::GetDeviceSpecificInputString(
    const DeviceInput& di) {
  switch (di.button_) {
    case JOY_BUTTON_1:
      return "UL";
    case JOY_BUTTON_2:
      return "UR";
    case JOY_BUTTON_3:
      return "MID";
    case JOY_BUTTON_4:
      return "DL";
    case JOY_BUTTON_5:
      return "DR";
    case JOY_BUTTON_6:
      return "Esc";
    case JOY_BUTTON_7:
      return "P2 UL";
    case JOY_BUTTON_8:
      return "P2 UR";
    case JOY_BUTTON_9:
      return "P2 MID";
    case JOY_BUTTON_10:
      return "P2 DL";
    case JOY_BUTTON_11:
      return "P2 DR";
  }

  return InputHandler::GetDeviceSpecificInputString(di);
}

void InputHandler_Win32_Pump::GetDevicesAndDescriptions(
    std::vector<InputDeviceInfo>& devices_out) {
  for (int i = 0; i < NUM_PUMPS; ++i) {
    if (device_[i].IsOpen()) {
      devices_out.push_back(
          InputDeviceInfo(InputDevice(DEVICE_PUMP1 + i), "Pump USB"));
    }
  }
}

int InputHandler_Win32_Pump::InputThread_Start(void* p) {
  ((InputHandler_Win32_Pump*)p)->InputThreadMain();
  return 0;
}

void InputHandler_Win32_Pump::InputThreadMain() {
  if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST)) {
    LOG->Warn(
        werr_ssprintf(GetLastError(), "Failed to set Pump thread priority")
            .c_str());
  }

  /* Enable priority boosting. */
  SetThreadPriorityBoost(GetCurrentThread(), FALSE);

  std::vector<WindowsFileIO*> sources;
  for (int i = 0; i < NUM_PUMPS; ++i) {
    if (device_[i].io_.IsOpen()) {
      sources.push_back(&device_[i].io_);
    }
  }

  while (!shutdown_) {
    CHECKPOINT;
    int actual = 0, val = 0;
    int ret = WindowsFileIO::read_several(sources, &val, actual, 0.100f);

    CHECKPOINT;
    if (ret <= 0) {
      continue; /* no event */
    }

    HandleInput(actual, val);
    InputHandler::UpdateTimer();
  }
  CHECKPOINT;
}

void InputHandler_Win32_Pump::Update() {
  if (!input_thread_.IsCreated()) {
    for (int i = 0; i < NUM_PUMPS; ++i) {
      int ret = device_[i].GetPadEvent();

      if (ret == -1) {
        continue; /* no event */
      }

      HandleInput(i, ret);
    }
    InputHandler::UpdateTimer();
  }
}

/*
 * (c) 2002-2004 Glenn Maynard
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
