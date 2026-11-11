#include "LightsDriver_Win32Serial.h"

#include <cstdint>
#include <string>

#include "RageUtil.h"
#include "global.h"
#include "windows.h"

REGISTER_LIGHTS_DRIVER_CLASS(Win32Serial);

static Preference<std::string> lights_com_port("LightsComPort", "COM54");

HANDLE serial_port;

LightsDriver_Win32Serial::LightsDriver_Win32Serial() {
  // Ensure a non-match the first time
  last_output_[0] = 0;

  std::string com_port = lights_com_port.Get();

  serial_port = CreateFile(
      std::string("\\\\.\\").append(com_port).c_str(), GENERIC_WRITE, 0, NULL,
      OPEN_EXISTING, 0, NULL);

  if (serial_port == INVALID_HANDLE_VALUE) {
    MessageBox(
        nullptr, "Could not find a device on the configured COM port.", "ERROR",
        MB_OK);
    return;
  }

  DCB dcb_serial_params = {0};
  dcb_serial_params.DCBlength = sizeof(dcb_serial_params);

  if (GetCommState(serial_port, &dcb_serial_params) == 0) {
    MessageBox(
        nullptr, "Could not connect to a device on the configured COM port.",
        "ERROR", MB_OK);
    return;
  }

  dcb_serial_params.BaudRate = CBR_115200;  // Setting BaudRate = 115200
  dcb_serial_params.ByteSize = 8;           // Setting ByteSize = 8
  dcb_serial_params.StopBits = ONESTOPBIT;  // Setting StopBits = 1
  dcb_serial_params.Parity = NOPARITY;      // Setting Parity = None

  if (SetCommState(serial_port, &dcb_serial_params) == 0) {
    MessageBox(
        nullptr,
        "Could not setup the device parameters on the configured COM port.",
        "ERROR", MB_OK);
    return;
  }

  COMMTIMEOUTS timeouts = {0};
  timeouts.ReadIntervalTimeout = 50;          // in milliseconds
  timeouts.ReadTotalTimeoutConstant = 50;     // in milliseconds
  timeouts.ReadTotalTimeoutMultiplier = 10;   // in milliseconds
  timeouts.WriteTotalTimeoutConstant = 50;    // in milliseconds
  timeouts.WriteTotalTimeoutMultiplier = 10;  // in milliseconds

  if (SetCommTimeouts(serial_port, &timeouts) == 0) {
    MessageBox(
        nullptr,
        "Could not set the device timeouts on the configured COM port.",
        "ERROR", MB_OK);
    return;
  }
}

LightsDriver_Win32Serial::~LightsDriver_Win32Serial() {
  CloseHandle(serial_port);
}

void LightsDriver_Win32Serial::Set(const LightsState* ls) {
  if (serial_port != INVALID_HANDLE_VALUE) {
    uint8_t buffer[kFullSextetCount];

    packLine(buffer, ls);

    // Only write if the message has changed since the last write.
    if (memcmp(buffer, last_output_, kFullSextetCount) != 0) {
      DWORD bytes_written = 0;
      WriteFile(serial_port, buffer, kFullSextetCount, &bytes_written, NULL);

      // Remember last message
      memcpy(last_output_, buffer, kFullSextetCount);
    }
  }
}

/*
 * (c) 2020 Skogaby <skogabyskogaby@gmail.com>
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
