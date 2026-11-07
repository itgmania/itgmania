#include "USB.h"

#include "RageLog.h"
#include "RageUtil.h"
#include "archutils/Win32/ErrorStrings.h"
#include "global.h"

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")

extern "C" {
#include "setupapi.h"
/* Quiet header warning: */
#include "hidsdi.h"
}

#include <string>
#include <vector>

static std::string GetUSBDevicePath(int num) {
  GUID guid;
  HidD_GetHidGuid(&guid);

  HDEVINFO device_info = SetupDiGetClassDevs(
      &guid, nullptr, nullptr, (DIGCF_PRESENT | DIGCF_DEVICEINTERFACE));

  SP_DEVICE_INTERFACE_DATA device_interface;
  device_interface.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

  if (!SetupDiEnumDeviceInterfaces(
          device_info, nullptr, &guid, num, &device_interface)) {
    SetupDiDestroyDeviceInfoList(device_info);
    return std::string();
  }

  unsigned long size;
  SetupDiGetDeviceInterfaceDetail(
      device_info, &device_interface, nullptr, 0, &size, 0);

  PSP_INTERFACE_DEVICE_DETAIL_DATA device_detail =
      (PSP_INTERFACE_DEVICE_DETAIL_DATA)malloc(size);
  device_detail->cbSize = sizeof(SP_INTERFACE_DEVICE_DETAIL_DATA);

  std::string ret;
  if (SetupDiGetDeviceInterfaceDetail(
          device_info, &device_interface, device_detail, size, &size,
          nullptr)) {
    ret = device_detail->DevicePath;
  }
  free(device_detail);

  SetupDiDestroyDeviceInfoList(device_info);
  return ret;
}

bool USBDevice::Open(
    int vid, int pid, int block_size, int num, void (*pfn_init)(HANDLE)) {
  DWORD index = 0;

  std::string path;
  while ((path = GetUSBDevicePath(index++)) != "") {
    HANDLE h = CreateFile(
        path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
        OPEN_EXISTING, 0, nullptr);

    if (h == INVALID_HANDLE_VALUE) {
      continue;
    }

    HIDD_ATTRIBUTES attr;
    if (!HidD_GetAttributes(h, &attr)) {
      CloseHandle(h);
      continue;
    }

    if ((vid != -1 && attr.VendorID != vid) ||
        (pid != -1 && attr.ProductID != pid)) {
      CloseHandle(h);
      continue; /* This isn't it. */
    }

    /* The VID and PID match. */
    if (num-- > 0) {
      CloseHandle(h);
      continue;
    }

    if (pfn_init) {
      pfn_init(h);
    }
    CloseHandle(h);

    io_.Open(path, block_size);
    return true;
  }

  return false;
}

bool USBDevice::IsOpen() const { return io_.IsOpen(); }

int USBDevice::GetPadEvent() {
  if (!IsOpen()) {
    return -1;
  }

  long buf;
  if (io_.read(&buf) <= 0) {
    return -1;
  }

  return buf;
}

WindowsFileIO::WindowsFileIO() {
  ZeroMemory(&overlapped_, sizeof(overlapped_));
  handle_ = INVALID_HANDLE_VALUE;
  buffer_ = nullptr;
}

WindowsFileIO::~WindowsFileIO() {
  if (handle_ != INVALID_HANDLE_VALUE) {
    CloseHandle(handle_);
  }
  delete[] buffer_;
}

bool WindowsFileIO::Open(std::string path, int block_size) {
  LOG->Trace("WindowsFileIO::open(%s)", path.c_str());
  block_size_ = block_size;

  if (buffer_) {
    delete[] buffer_;
  }
  buffer_ = new char[block_size_];

  if (handle_ != INVALID_HANDLE_VALUE) {
    CloseHandle(handle_);
  }

  handle_ = CreateFile(
      path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
      OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);

  if (handle_ == INVALID_HANDLE_VALUE) {
    return false;
  }

  queue_read();

  return true;
}

void WindowsFileIO::queue_read() {
  /* Request feedback from the device. */
  unsigned long read;
  ReadFile(handle_, buffer_, block_size_, &read, &overlapped_);
}

int WindowsFileIO::finish_read(void* p) {
  LOG->Trace("this %p, %p", this, p);
  /* We do; get the result.  It'll go into the original buffer_
   * we supplied on the original call; that's why buffer_ is a
   * member instead of a local. */
  unsigned long cnt;
  int ret = GetOverlappedResult(handle_, &overlapped_, &cnt, FALSE);

  if (ret == 0 && (GetLastError() == ERROR_IO_PENDING ||
                   GetLastError() == ERROR_IO_INCOMPLETE)) {
    return -1;
  }

  queue_read();

  if (ret == 0) {
    LOG->Warn(
        werr_ssprintf(GetLastError(), "Error reading USB device").c_str());
    return -1;
  }

  memcpy(p, buffer_, cnt);
  return cnt;
}

int WindowsFileIO::read(void* p) {
  LOG->Trace("WindowsFileIO::read()");

  /* See if we have a response for our request (which we may
   * have made on a previous call): */
  if (WaitForSingleObjectEx(handle_, 0, TRUE) == WAIT_TIMEOUT) {
    return -1;
  }

  return finish_read(p);
}

int WindowsFileIO::read_several(
    const std::vector<WindowsFileIO*>& sources, void* p, int& actual,
    float timeout) {
  HANDLE* handles = new HANDLE[sources.size()];
  for (unsigned i = 0; i < sources.size(); ++i) {
    handles[i] = sources[i]->handle_;
  }

  int ret = WaitForMultipleObjectsEx(
      sources.size(), handles, false, int(timeout * 1000), true);
  delete[] handles;

  if (ret == -1) {
    LOG->Trace(werr_ssprintf(GetLastError(), "WaitForMultipleObjectsEx failed")
                   .c_str());
    return -1;
  }

  if (ret >= int(WAIT_OBJECT_0) && ret < int(WAIT_OBJECT_0 + sources.size())) {
    actual = ret - WAIT_OBJECT_0;
    return sources[actual]->finish_read(p);
  }

  return 0;
}

bool WindowsFileIO::IsOpen() const { return handle_ != INVALID_HANDLE_VALUE; }

/*
 * (c) 2002-2005 Glenn Maynard
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
