#ifndef INPUT_HANDLER_WIN32_RTIO_H
#define INPUT_HANDLER_WIN32_RTIO_H

#include <windows.h>

#include <string>
#include <vector>

#include "InputHandler.h"
#include "RageThreads.h"

typedef struct {
  bool p1_pad_up_;
  bool p1_pad_down_;
  bool p1_pad_left_;
  bool p1_pad_right_;
  bool p2_pad_up_;
  bool p2_pad_down_;
  bool p2_pad_left_;
  bool p2_pad_right_;
  bool p1_menu_up_;
  bool p1_menu_down_;
  bool p1_menu_left_;
  bool p1_menu_right_;
  bool p2_menu_up_;
  bool p2_menu_down_;
  bool p2_menu_left_;
  bool p2_menu_right_;
  bool p1_menu_start_;
  bool p2_menu_start_;
} GAME_INPUT;

typedef struct {
  int p1_insert_coin_;
  int p2_insert_coin_;
  int volume_up_;
  int volume_down_;
  int test_switch_;
  int select_switch_;
} OPERATOR_INPUT;

enum COUNTER_STATE {
  COUNTER_STATE_SEND_1 =
      0,  // Ready to send "H10" (first coin counter increment command)
  COUNTER_STATE_RECV_1,  // Ready to recv "h10" (ack for first coin counter
                         // increment command)
  COUNTER_STATE_SEND_2,  // Ready to send "H00" (second coin counter increment
                         // command)
  COUNTER_STATE_RECV_2,  // Ready to recv "h00" (ack for second coin counter
                         // increment command)
};

class SerialDevice {
 public:
  SerialDevice(int read_buffer_size = 0x1000, int write_buffer_size = 0x1000)
      : read_buffer_size_(read_buffer_size),
        write_buffer_size_(write_buffer_size) {}
  ~SerialDevice();
  bool Connect(int com_number);
  void Disconnect();
  int Read(char* buffer, int buffer_size);
  int Write(const char* buffer, int buffer_size);

 private:
  bool Setup();

  HANDLE com_handle_ = INVALID_HANDLE_VALUE;
  int read_buffer_size_;
  int write_buffer_size_;
  OVERLAPPED read_overlapped_;
  OVERLAPPED write_overlapped_;
};

class RtioDevice {
 public:
  ~RtioDevice();
  bool Connect();
  void Disconnect();
  bool ReadMsgs(std::vector<std::string>* msgs);
  bool WriteMsg(const std::string& msg);

 private:
  int ParseMsg(char* buffer, int buffer_size);

  SerialDevice serial_;
  char read_buffer_[0x2000];
  int read_offset_ = 0;
};

class InputHandler_Win32_RTIO : public InputHandler {
 public:
  InputHandler_Win32_RTIO();
  ~InputHandler_Win32_RTIO();
  void GetDevicesAndDescriptions(std::vector<InputDeviceInfo>& vDevicesOut);
  std::string GetDeviceSpecificInputString(const DeviceInput& di);
  static int InputThread_Start(void* this_ptr);

 private:
  bool Initialize();
  void InputThread();
  void HandleGameInput(const std::string& msg, const RageTimer& now);
  void HandleOperatorInput(const std::string& msg, const RageTimer& now);
  void HandleCounterAck(const std::string& msg);

  RtioDevice rtio_;
  RageThread input_thread_;
  bool initialized_ = false;
  bool shutdown_ = false;
  GAME_INPUT last_game_input_ = {};
  OPERATOR_INPUT last_operator_input_ = {};
  int counter_cycles_pending_ = 0;
  COUNTER_STATE counter_state_ = COUNTER_STATE_SEND_1;
  RageTimer last_counter_send_;
  RageTimer last_counter_recv_;
};

#endif

/*
 * Contributed by x0rbl (2019). Stepmania copyright/license:
 *
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
