#include "HIDDevice.h"

#include "RageLog.h"
#include "RageUtil.h"

HIDDevice::HIDDevice()
    : interface_(nullptr), queue_(nullptr), running_(false) {}

HIDDevice::~HIDDevice() {
  if (queue_) {
    CFRunLoopSourceRef run_loop_source;

    if (running_) {
      CALL(queue_, stop);
      run_loop_source = CALL(queue_, getAsyncEventSource);
      CFRunLoopSourceInvalidate(run_loop_source);
      CFRelease(run_loop_source);
    }

    CALL(queue_, dispose);
    CALL(queue_, Release);
  }
  if (interface_) {
    CALL(interface_, close);
    CALL(interface_, Release);
  }
}

bool HIDDevice::Open(io_object_t device) {
  IOReturn ret;
  CFMutableDictionaryRef properties;
  kern_return_t result;
  CFTypeRef object;

  result = IORegistryEntryCreateCFProperties(
      device, &properties, kCFAllocatorDefault, kNilOptions);
  if (result != KERN_SUCCESS || !properties) {
    LOG->Warn("Couldn't get properties.");
    return false;
  }

  object = CFDictionaryGetValue(properties, CFSTR(kIOHIDProductKey));

  CFTypeRef vid_ref =
      CFDictionaryGetValue(properties, CFSTR(kIOHIDVendorIDKey));
  CFTypeRef pid_ref =
      CFDictionaryGetValue(properties, CFSTR(kIOHIDProductIDKey));
  int vid, pid;

  if (!IntValue(vid_ref, vid)) {
    vid = 0;
  }
  if (!IntValue(pid_ref, pid)) {
    pid = 0;
  }
  if (!InitDevice(vid, pid)) {
    LOG->Warn("Couldn't initialize device.");
    CFRelease(properties);
    return false;
  }

  if (object && CFGetTypeID(object) == CFStringGetTypeID()) {
    const char* str =
        CFStringGetCStringPtr(CFStringRef(object), CFStringGetSystemEncoding());
    description_ = str ? str : "";
  }
  if (description_ == "") {
    description_ = ssprintf("%04x:%04x", vid, pid);
  }
  LOG->Trace("\t\tDevice description: %s", description_.c_str());

  object = CFDictionaryGetValue(properties, CFSTR(kIOHIDElementKey));
  if (!object || CFGetTypeID(object) != CFArrayGetTypeID()) {
    LOG->Warn("Couldn't get HID elements.");
    CFRelease(properties);
    return false;
  }

  CFArrayRef logical_devices = CFArrayRef(object);
  CFRange r = {0, CFArrayGetCount(logical_devices)};

  CFArrayApplyFunction(logical_devices, r, HIDDevice::AddLogicalDevice, this);

  CFRelease(properties);

  // Create the interface
  IOCFPlugInInterface** plug_in_interface;
  HRESULT hresult;
  SInt32 score;

  ret = IOCreatePlugInInterfaceForService(
      device, kIOHIDDeviceUserClientTypeID, kIOCFPlugInInterfaceID,
      &plug_in_interface, &score);
  if (ret != kIOReturnSuccess) {
    PrintIOErr(ret, "Failed to create plugin interface.");
    return false;
  }

  // Call a method of the plugin to create the device interface
  CFUUIDBytes bytes = CFUUIDGetUUIDBytes(kIOHIDDeviceInterfaceID);

  hresult = CALL(plug_in_interface, QueryInterface, bytes, (void**)&interface_);

  CALL(plug_in_interface, Release);

  if (hresult != S_OK) {
    LOG->Warn("Couldn't get device interface from plugin interface.");
    interface_ = nullptr;
    return false;
  }

  // open the interface
  if ((ret = CALL(interface_, open, 0)) != kIOReturnSuccess) {
    PrintIOErr(ret, "Failed to open the interface.");
    CALL(interface_, Release);
    interface_ = nullptr;
    return false;
  }

  // alloc/create queue
  queue_ = CALL(interface_, allocQueue);
  if (!queue_) {
    LOG->Warn("Couldn't allocate a queue.");
    return false;
  }

  if ((ret = CALL(queue_, create, 0, 32)) != kIOReturnSuccess) {
    PrintIOErr(ret, "Failed to create the queue.");
    CALL(queue_, Release);
    queue_ = nullptr;
    CALL(interface_, Release);
    interface_ = nullptr;
    return false;
  }

  Open();
  LOG->Trace("\t\tDevice open");
  return true;
}

void HIDDevice::StartQueue(
    CFRunLoopRef loop_ref, IOHIDCallbackFunction callback, void* target) {
  CFRunLoopSourceRef run_loop_source;
  // This creates a run loop source. It is released in the dtor.
  IOReturn ret = CALL(queue_, createAsyncEventSource, &run_loop_source);

  if (ret != kIOReturnSuccess) {
    PrintIOErr(ret, "Failed to create async event source.");
    return;
  }

  if (!CFRunLoopContainsSource(
          loop_ref, run_loop_source, kCFRunLoopDefaultMode)) {
    CFRunLoopAddSource(loop_ref, run_loop_source, kCFRunLoopDefaultMode);
  }

  ret = CALL(queue_, setEventCallout, callback, target, this);

  if (ret != kIOReturnSuccess) {
    PrintIOErr(ret, "Failed to set the call back.");
    return;
  }

  // start the queue
  ret = CALL(queue_, start);

  if (ret != kIOReturnSuccess) {
    CFRunLoopSourceInvalidate(run_loop_source);
    CFRelease(run_loop_source);

    PrintIOErr(ret, "Failed to start the queue.");
    return;
  }
  running_ = true;
}

void HIDDevice::AddLogicalDevice(const void* value, void* context) {
  if (CFGetTypeID(CFTypeRef(value)) != CFDictionaryGetTypeID()) {
    return;
  }

  CFDictionaryRef properties = CFDictionaryRef(value);
  HIDDevice* self = (HIDDevice*)context;
  CFTypeRef object;
  int usage, usage_page;

  // Get usage page
  object = CFDictionaryGetValue(properties, CFSTR(kIOHIDElementUsagePageKey));
  if (!IntValue(object, usage_page)) {
    return;
  }

  // Get usage
  object = CFDictionaryGetValue(properties, CFSTR(kIOHIDElementUsageKey));
  if (!IntValue(object, usage)) {
    return;
  }

  object = CFDictionaryGetValue(properties, CFSTR(kIOHIDElementKey));
  if (!object || CFGetTypeID(object) != CFArrayGetTypeID()) {
    return;
  }

  if (!self->AddLogicalDevice(usage_page, usage)) {
    return;
  }

  CFArrayRef elements = CFArrayRef(object);
  CFRange r = {0, CFArrayGetCount(elements)};

  CFArrayApplyFunction(elements, r, HIDDevice::AddElement, self);
}

void HIDDevice::AddElement(const void* value, void* context) {
  if (CFGetTypeID(CFTypeRef(value)) != CFDictionaryGetTypeID()) {
    return;
  }

  CFDictionaryRef properties = CFDictionaryRef(value);
  HIDDevice* self = (HIDDevice*)context;
  CFTypeRef object;
  int usage, usage_page;
  long cookie;

  // Recursively add elements
  object = CFDictionaryGetValue(properties, CFSTR(kIOHIDElementKey));
  if (object && CFGetTypeID(object) == CFArrayGetTypeID()) {
    CFArrayRef elements = CFArrayRef(object);
    CFRange r = {0, CFArrayGetCount(elements)};

    CFArrayApplyFunction(elements, r, AddElement, context);
  }

  // Get usage page
  object = CFDictionaryGetValue(properties, CFSTR(kIOHIDElementUsagePageKey));
  if (!IntValue(object, usage_page)) {
    return;
  }

  // Get usage
  object = CFDictionaryGetValue(properties, CFSTR(kIOHIDElementUsageKey));
  if (!IntValue(object, usage)) {
    return;
  }

  // Get cookie
  object = CFDictionaryGetValue(properties, CFSTR(kIOHIDElementCookieKey));
  if (!LongValue(object, cookie)) {
    return;
  }
  self->AddElement(usage_page, usage, IOHIDElementCookie(cookie), properties);
}

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
