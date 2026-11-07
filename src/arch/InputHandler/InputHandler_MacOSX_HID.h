#ifndef INPUT_HANDLER_MACOSX_HID_H
#define INPUT_HANDLER_MACOSX_HID_H

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>

#include <string>
#include <vector>

#include "InputHandler.h"
#include "RageInputDevice.h"
#include "RageThreads.h"

class HIDDevice;

class InputHandler_MacOSX_HID : public InputHandler {
 private:
  std::vector<HIDDevice*> devices_;
  RageThread input_thread_;
  RageSemaphore sem_;
  CFRunLoopRef loop_ref_;
  CFRunLoopSourceRef source_ref_;
  std::vector<io_iterator_t> iters_;  // We don't really care about these but
                                      // they need to stick around
  IONotificationPortRef notify_port_;
  RageMutex change_lock_;
  bool changed_;

  static int Run(void* data);
  static void DeviceAdded(void* ref_con, io_iterator_t iter);
  static void DeviceChanged(
      void* ref_con, io_service_t service, natural_t message_type, void* arg);
  void StartDevices();
  void AddDevices(int usage_page, int usage, InputDevice& id);

 public:
  InputHandler_MacOSX_HID();
  ~InputHandler_MacOSX_HID();

  bool DevicesChanged() {
    LockMut(change_lock_);
    return changed_;
  }
  void GetDevicesAndDescriptions(std::vector<InputDeviceInfo>& devices_out);
  std::string GetDeviceSpecificInputString(const DeviceInput& di);
  wchar_t DeviceButtonToChar(
      DeviceButton button, bool use_current_key_modifiers);
  static void QueueCallback(
      void* target, int result, void* refcon, void* sender);
};

#endif

/*
 * (c) 2005-2006 Steve Checkoway
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
