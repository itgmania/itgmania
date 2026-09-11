#ifndef INPUT_HANDLER_SNEK_CONFIG
#define INPUT_HANDLER_SNEK_CONFIG

/*
 * -------------------------- NOTE --------------------------
 *
 * This driver needs user read/write access the device.
 * This can be achieved by using a udev rule like this:
 *
 * SUBSYSTEMS=="usb", ATTRS{idVendor}=="2e8a", ATTRS{idProduct}=="10a8",
 * OWNER="dance", GROUP="dance", MODE="0660"
 *
 * or
 *
 * KERNEL=="hidraw*", ATTRS{idVendor}=="2e8a", ATTRS{idProduct}=="10a8",
 * OWNER="dance", GROUP="dance", MODE="0660"
 *
 * Refer to your distribution's documentation on how to properly apply a udev
 * rule.
 *
 * -------------------------- NOTE --------------------------
 */

#include <cstdint>
#include <string>
#include <vector>

#include "InputFilter.h"
#include "InputHandler.h"
#include "LightsManager.h"
#include "PlayerNumber.h"
#include "RageInputDevice.h"
#include "RageThreads.h"
#include "archutils/Common/HidDevice.h"

#define SNEK_CONFIG_PACKETSIZE 64

#define SNEK_CONFIG_VID 0x2E8A
#define SNEK_CONFIG_PID 0x10a8

// the config interface is number 1
#define SNEK_CONFIG_INTERFACE_NUM 1

// hid report number is this
#define SNEK_CONFIG_HID_OUTPUT 0x77

// hid report response starts with this.
#define SNEK_CONFIG_HID_INPUT 0x78

// these are all opcodes.
#define SNEK_CONFIG_OPCODE_GETINFO 0x01
#define SNEK_CONFIG_OPCODE_SET_EXTIO_SENSOR 0x08
#define SNEK_CONFIG_OPCODE_DISCONNECT 0x0A
#define SNEK_CONFIG_ERROR 0xFF

#define SNEK_CONFIG_NUM_SENSORS 4

struct SnekBitMapping {
  uint32_t bitPosition;
  PlayerNumber pn;
  PadPanel panel;
};

class InputHandler_SnekConfig : public InputHandler {
 public:
  InputHandler_SnekConfig();
  ~InputHandler_SnekConfig();

  std::string GetDeviceSpecificInputString(const DeviceInput& di);
  void GetDevicesAndDescriptions(std::vector<InputDeviceInfo>& vDevicesOut);

  bool IsConnected() { return dev.IsConnected(); }

  void StartSensorDebugging();
  void StopSensorDebugging();

 private:
  // ensure auto reconnect and blocking reads (we need to ask it a question then
  // get an answer)
  HidDevice dev = HidDevice(
      SNEK_CONFIG_VID, SNEK_CONFIG_PID, SNEK_CONFIG_INTERFACE_NUM, true, false);

  std::atomic<bool> m_bShutdown;
  RageThread* DebugThread = nullptr;

  uint32_t sensorState[SNEK_CONFIG_NUM_SENSORS] = {};

  static int InputThread_Start(void* p);
  void InputThreadMain();

  void BroadcastFullSensorStateHelper(
      SnekBitMapping mapping, uint8_t sensor_index, bool isPressed);
  void SendSafeDisconnect();

  bool SendCommand(
      uint8_t opcode, uint8_t payload,
      std::array<uint8_t, SNEK_CONFIG_PACKETSIZE>& response);
};

#endif

/*
 * (c) 2026 din
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
