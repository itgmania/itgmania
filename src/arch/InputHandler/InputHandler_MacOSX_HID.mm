#include "InputHandler_MacOSX_HID.h"
#include "InputFilter.h"
#include "PrefsManager.h"
#include "RageLog.h"
#include "archutils/Darwin/DarwinThreadHelpers.h"
#include "archutils/Darwin/JoystickDevice.h"
#include "archutils/Darwin/KeyboardDevice.h"
#include "archutils/Darwin/MouseDevice.h"
#include "archutils/Darwin/PumpDevice.h"
#include "global.h"

#include <mach/mach_time.h>
#include <vector>

#include <Carbon/Carbon.h>
#include <IOKit/IOMessage.h>

static double GetTimebaseFactor() {
  mach_timebase_info_data_t time_base;
  mach_timebase_info(&time_base);
  return time_base.numer / (1000.0 * time_base.denom);
}
static const double kTimebaseFactor = GetTimebaseFactor();

REGISTER_INPUT_HANDLER_CLASS2(HID, MacOSX_HID);

void InputHandler_MacOSX_HID::QueueCallback(void* target, int result, void* refcon, void* sender) {
  // The result seems useless as you can't actually return anything...

  InputHandler_MacOSX_HID* that = (InputHandler_MacOSX_HID*)target;
  IOHIDQueueInterface** queue = (IOHIDQueueInterface**)sender;
  IOHIDEventStruct event;
  AbsoluteTime zero_time = {0, 0};
  HIDDevice* dev = static_cast<HIDDevice*>(refcon);
  std::vector<DeviceInput> presses;

  while ((result = CALL(queue, getNextEvent, &event, zero_time, 0)) == kIOReturnSuccess) {
    if (event.longValueSize != 0 && event.longValue != nullptr) {
      free(event.longValue);
      continue;
    }
    uint64_t absolute_time_val = ((uint64_t)event.timestamp.hi << 32) | event.timestamp.lo;
    uint64_t usecs = uint64_t(absolute_time_val * kTimebaseFactor);
    RageTimer event_time(usecs / 1000000ULL, usecs % 1000000ULL);
    dev->GetButtonPresses(presses, event.elementCookie, event.value, event_time);
  }
  for (DeviceInput& i : presses) {
    INPUTFILTER->ButtonPressed(i);
  }
}

static void RunLoopStarted(CFRunLoopObserverRef o, CFRunLoopActivity a, void* sem) {
  CFRunLoopObserverInvalidate(o);
  CFRelease(o);  // we don't need this any longer
  ((RageSemaphore*)sem)->Post();
}

int InputHandler_MacOSX_HID::Run(void* data) {
  InputHandler_MacOSX_HID* that = (InputHandler_MacOSX_HID*)data;

  that->loop_ref_ = CFRunLoopGetCurrent();
  CFRetain(that->loop_ref_);

  that->StartDevices();
  {
    const std::string error = SetThreadPrecedence(1.0f);
    if (!error.empty()) {
      LOG->Warn("Could not set precedence of the input thread: %s", error.c_str());
    }
  }
  // Add an observer for the start of the run loop
  {
    /* The function copies the information out of the structure, so the memory
     * pointed to by context does not need to persist beyond the function call. */
    CFRunLoopObserverContext context = {0, &that->sem_, nullptr, nullptr, nullptr};
    CFRunLoopObserverRef o = CFRunLoopObserverCreate(
        kCFAllocatorDefault, kCFRunLoopEntry, false, 0, RunLoopStarted, &context);
    CFRunLoopAddObserver(that->loop_ref_, o, kCFRunLoopDefaultMode);
  }

  /* Add a source for ending the run loop. This serves two purposes:
   * 1. it provides a way to terminate the run loop when IH_MacOSX_HID exists,
   * and 2. it ensures that CFRunLoopRun() doesn't return immediately if
   * there are no other sources. */
  {
    /* Being a little tricky here, the perform callback takes a void* and
     * returns nothing. CFRunLoopStop takes a CFRunLoopRef (a pointer) so
     * cast the function and pass the loop ref. */
    void* info = that->loop_ref_;
    void (*perform)(void*) = (void (*)(void*))CFRunLoopStop;
    // { version, info, retain, release, copyDescription, equal, hash, schedule, cancel, perform }
    CFRunLoopSourceContext context = {0,       info,    nullptr, nullptr, nullptr,
                                      nullptr, nullptr, nullptr, nullptr, perform};

    // Pass 1 so that it is called after all inputs have been handled (they will have order = 0)
    that->source_ref_ = CFRunLoopSourceCreate(kCFAllocatorDefault, 1, &context);

    CFRunLoopAddSource(that->loop_ref_, that->source_ref_, kCFRunLoopDefaultMode);
  }
  CFRunLoopRun();
  LOG->Trace("Shutting down input handler thread...");
  return 0;
}

void InputHandler_MacOSX_HID::DeviceAdded(void* ref_con, io_iterator_t) {
  InputHandler_MacOSX_HID* that = (InputHandler_MacOSX_HID*)ref_con;

  LockMut(that->change_lock_);
  that->changed_ = true;
}

void InputHandler_MacOSX_HID::DeviceChanged(
    void* ref_con, io_service_t service, natural_t message_type, void* arg) {
  if (message_type == kIOMessageServiceIsTerminated) {
    InputHandler_MacOSX_HID* that = (InputHandler_MacOSX_HID*)ref_con;

    LockMut(that->change_lock_);
    that->changed_ = true;
  }
}

// loop_ref_ needs to be set before this is called
void InputHandler_MacOSX_HID::StartDevices() {
  ASSERT(loop_ref_);
  for (HIDDevice* i : devices_) {
    i->StartQueue(loop_ref_, InputHandler_MacOSX_HID::QueueCallback, this);
  }

  CFRunLoopSourceRef run_loop_source = IONotificationPortGetRunLoopSource(notify_port_);

  CFRunLoopAddSource(loop_ref_, run_loop_source, kCFRunLoopDefaultMode);
}

InputHandler_MacOSX_HID::~InputHandler_MacOSX_HID() {
  for (HIDDevice* i : devices_) {
    delete i;
  }
  if (PREFSMAN->threaded_input_) {
    CFRunLoopSourceSignal(source_ref_);
    CFRunLoopWakeUp(loop_ref_);
    input_thread_.Wait();
    CFRelease(source_ref_);
    CFRelease(loop_ref_);
    LOG->Trace("Input handler thread shut down.");
  }

  for (io_iterator_t& i : iters_) {
    IOObjectRelease(i);
  }
  IONotificationPortDestroy(notify_port_);
}

static CFDictionaryRef GetMatchingDictionary(int usage_page, int usage) {
  // Build the matching dictionary.
  CFMutableDictionaryRef dict;

  if ((dict = IOServiceMatching(kIOHIDDeviceKey)) == nullptr) {
    FAIL_M("Couldn't create a matching dictionary.");
  }
  // Refine the search by only looking for joysticks
  CFNumberRef usage_page_ref = CFInt(usage_page);
  CFNumberRef usage_ref = CFInt(usage);

  CFDictionarySetValue(dict, CFSTR(kIOHIDPrimaryUsagePageKey), usage_page_ref);
  CFDictionarySetValue(dict, CFSTR(kIOHIDPrimaryUsageKey), usage_ref);

  // Cleanup after ourselves
  CFRelease(usage_page_ref);
  CFRelease(usage_ref);

  return dict;
}

// Factor this out because nothing else in IH_C::AddDevices() needs to know the type of the device.
static HIDDevice* MakeDevice(InputDevice id) {
  // Keyboard input is now handled by NSEvent handler to avoid Input Monitoring permission
  // requirements if( id == DEVICE_KEYBOARD )
  //	return new KeyboardDevice;
  /*
  if( id == DEVICE_MOUSE )
          return new MouseDevice;
  */
  if (IsJoystick(id)) {
    return new JoystickDevice;
  }
  if (IsPump(id)) {
    return new PumpDevice;
  }
  return nullptr;
}

void InputHandler_MacOSX_HID::AddDevices(int usage_page, int usage, InputDevice& id) {
  io_iterator_t iter;
  CFDictionaryRef dict = GetMatchingDictionary(usage_page, usage);
  kern_return_t ret = IOServiceAddMatchingNotification(
      notify_port_, kIOFirstMatchNotification, dict, InputHandler_MacOSX_HID::DeviceAdded, this,
      &iter);
  io_object_t device;

  if (ret != KERN_SUCCESS) {
    return;
  }

  iters_.push_back(iter);

  // Iterate over the devices and add them
  while ((device = IOIteratorNext(iter))) {
    LOG->Trace("\tFound device %d", id);
    HIDDevice* dev = MakeDevice(id);
    int num;

    if (!dev) {
      LOG->Trace("\t\tInvalid id, deleting device");
      IOObjectRelease(device);
      continue;
    }

    if (!dev->Open(device) || (num = dev->AssignIDs(id)) == -1) {
      LOG->Trace("\tFailed top open or assign id, deleting device");
      delete dev;
      IOObjectRelease(device);
      continue;
    }
    io_iterator_t i;

    enum_add(id, num);
    devices_.push_back(dev);

    ret = IOServiceAddInterestNotification(
        notify_port_, device, kIOGeneralInterest, InputHandler_MacOSX_HID::DeviceChanged, this, &i);

    if (ret == KERN_SUCCESS) {
      iters_.push_back(i);
    } else {
      LOG->Trace("\t\tFailed to add device changed notification, deleting device");
    }
    IOObjectRelease(device);
  }
}

InputHandler_MacOSX_HID::InputHandler_MacOSX_HID()
    : sem_("Input thread started"), change_lock_("Input handler change lock") {
  InputDevice id = DEVICE_KEYBOARD;

  // Set up the notify ports.
  notify_port_ = IONotificationPortCreate(kIOMasterPortDefault);

  // Add devices.
#if !OSX_KEYBOARD_USE_NSEVENT
  LOG->Trace("Finding keyboards");
  AddDevices(kHIDPage_GenericDesktop, kHIDUsage_GD_Keyboard, id);

  LOG->Trace("Finding mice");
  AddDevices(kHIDPage_GenericDesktop, kHIDUsage_GD_Mouse, id);
#endif  // !OSX_KEYBOARD_USE_NSEVENT

  LOG->Trace("Finding joysticks");
  id = DEVICE_JOY1;
  AddDevices(kHIDPage_GenericDesktop, kHIDUsage_GD_Joystick, id);
  AddDevices(kHIDPage_GenericDesktop, kHIDUsage_GD_GamePad, id);
  LOG->Trace("Finding pump");
  id = DEVICE_PUMP1;
  AddDevices(
      kHIDPage_VendorDefinedStart, 0x0001,
      id);  // Pump pads use the first vendor specific usage page.
  changed_ = false;

  if (PREFSMAN->threaded_input_) {
    input_thread_.SetName("Input thread");
    input_thread_.Create(InputHandler_MacOSX_HID::Run, this);
    // Wait for the run loop to start before returning.
    sem_.Wait();
  } else {
    loop_ref_ = CFRunLoopRef(GetCFRunLoopFromEventLoop(GetMainEventLoop()));
    CFRetain(loop_ref_);
    StartDevices();
  }
}

void InputHandler_MacOSX_HID::GetDevicesAndDescriptions(std::vector<InputDeviceInfo>& devices) {
  for (HIDDevice* i : devices_) {
    i->GetDevicesAndDescriptions(devices);
  }
}

std::string InputHandler_MacOSX_HID::GetDeviceSpecificInputString(const DeviceInput& di) {
  if (di.device_ == DEVICE_KEYBOARD) {
#define OTHER(n) (Enum::to_integral(KEY_OTHER_0) + (n))
    switch (Enum::to_integral(di.button_)) {
      case KEY_DEL:
        return "del";
      case KEY_BACK:
        return "delete";
      case KEY_ENTER:
        return "return";
      case KEY_LALT:
        return "left option";
      case KEY_RALT:
        return "right option";
      case KEY_LMETA:
        return "left cmd";
      case KEY_RMETA:
        return "right cmd";
      case KEY_INSERT:
        return "help";
      case OTHER(0):
        return "F17";
      case OTHER(1):
        return "F18";
      case OTHER(2):
        return "F19";
      case OTHER(3):
        return "F20";
      case OTHER(4):
        return "F21";
      case OTHER(5):
        return "F22";
      case OTHER(6):
        return "F23";
      case OTHER(7):
        return "F25";
      case OTHER(8):
        return "execute";
      case OTHER(9):
        return "select";
      case OTHER(10):
        return "stop";
      case OTHER(11):
        return "again";
      case OTHER(12):
        return "undo";
      case OTHER(13):
        return "cut";
      case OTHER(14):
        return "copy";
      case OTHER(15):
        return "paste";
      case OTHER(16):
        return "find";
      case OTHER(17):
        return "mute";
      case OTHER(18):
        return "volume up";
      case OTHER(19):
        return "volume down";
      case OTHER(20):
        return "AS/400 equal";
      case OTHER(21):
        return "international 1";
      case OTHER(22):
        return "international 2";
      case OTHER(23):
        return "international 3";
      case OTHER(24):
        return "international 4";
      case OTHER(25):
        return "international 5";
      case OTHER(26):
        return "international 6";
      case OTHER(27):
        return "international 7";
      case OTHER(28):
        return "international 8";
      case OTHER(29):
        return "international 9";
      case OTHER(30):
        return "lang 1";
      case OTHER(31):
        return "lang 2";
      case OTHER(32):
        return "lang 3";
      case OTHER(33):
        return "lang 4";
      case OTHER(34):
        return "lang 5";
      case OTHER(35):
        return "lang 6";
      case OTHER(36):
        return "lang 7";
      case OTHER(37):
        return "lang 8";
      case OTHER(38):
        return "lang 9";
      case OTHER(39):
        return "alt erase";
      case OTHER(40):
        return "sys req";
      case OTHER(41):
        return "cancel";
      case OTHER(42):
        return "separator";
      case OTHER(43):
        return "out";
      case OTHER(44):
        return "oper";
      case OTHER(45):
        return "clear/again";  // XXX huh?
      case OTHER(46):
        return "cr sel/props";  // XXX
      case OTHER(47):
        return "ex sel";
      case OTHER(48):
        return "non US backslash";
      case OTHER(49):
        return "application";
      case OTHER(50):
        return "prior";
      default:
        break;
    }
#undef OTHER
  }
  if (di.device_ == DEVICE_PUMP1 || di.device_ == DEVICE_PUMP2) {
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
      default:
        break;
    }
  }

  return InputHandler::GetDeviceSpecificInputString(di);
}

// Modified from NESControllerInterface of Macifom project,
// used under MIT license from
// http://macifom.googlecode.com/svn-history/r89/Macifom/trunk/NESControllerInterface.m Used under
// MIT license from http://inquisitivecocoa.com/2009/04/05/key-code-translator/
static wchar_t KeyCodeToChar(CGKeyCode key_code, unsigned int modifier_flags) {
  TISInputSourceRef current_keyboard = TISCopyCurrentKeyboardInputSource();
  CFDataRef uchr =
      (CFDataRef)TISGetInputSourceProperty(current_keyboard, kTISPropertyUnicodeKeyLayoutData);
  const UCKeyboardLayout* keyboard_layout =
      uchr ? (const UCKeyboardLayout*)CFDataGetBytePtr(uchr) : nullptr;

  if (keyboard_layout) {
    UInt32 dead_key_state = 0;
    constexpr UniCharCount kMaxStringLength = 255;
    UniCharCount actual_string_length = 0;
    UniChar unicode_string[kMaxStringLength];

    OSStatus status = UCKeyTranslate(
        keyboard_layout, key_code, kUCKeyActionDown, modifier_flags, LMGetKbdType(), 0,
        &dead_key_state, kMaxStringLength, &actual_string_length, unicode_string);

    if (status != noErr) {
      NSError* error = [NSError errorWithDomain:NSOSStatusErrorDomain code:status userInfo:nil];
      const char* error_description = [error.localizedDescription UTF8String];
      fprintf(
          stderr,
          "There was an error translating from the '%d' key code to a human readable string: %s\n",
          key_code, error_description);
    } else if (actual_string_length == 0) {
      fprintf(stderr, "Couldn't find a translation for the '%d' key code\n", key_code);
    } else {
      return unicode_string[0];
    }
  } else {
    fprintf(stderr, "Couldn't find a translation for the '%d' key code\n", key_code);
  }
  return 0;
}

wchar_t InputHandler_MacOSX_HID::DeviceButtonToChar(
    DeviceButton button, bool use_current_key_modifiers) {
  // KeyTranslate maps these keys to a character.  They shouldn't be mapped to any character.
  switch (button) {
    default:
      if ((button >= KEY_F1 && button <= KEY_F16)) {
        return L'\0';
      }
      break;
    case KEY_UP:
    case KEY_DOWN:
    case KEY_LEFT:
    case KEY_RIGHT:
    case KEY_ESC:
    case KEY_TAB:
    case KEY_ENTER:
    case KEY_PRTSC:
    case KEY_SCRLLOCK:
    case KEY_PAUSE:
    case KEY_DEL:
    case KEY_HOME:
    case KEY_END:
    case KEY_PGUP:
    case KEY_PGDN:
    case KEY_NUMLOCK:
    case KEY_KP_ENTER:
      return L'\0';
  }

  // Use Text Input Source services to convert the key code to character code.
  UInt8 mac_virtual_key;
  if (KeyboardDevice::DeviceButtonToMacVirtualKey(button, mac_virtual_key)) {
    CGEventRef event = CGEventCreate(nullptr);
    CGEventFlags mods = CGEventGetFlags(event);
    CFRelease(event);
    UInt32 modifiers = use_current_key_modifiers ? (UInt32)mods : 0;
    wchar_t char_code = KeyCodeToChar(mac_virtual_key, modifiers);
    if (char_code != 0) {
      return char_code;
    }
  }

  return InputHandler::DeviceButtonToChar(button, use_current_key_modifiers);
}

/*
 * (c) 2005, 2006 Steve Checkoway
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
