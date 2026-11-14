#include "HidDevice.h"

#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "RageLog.h"
#include "hidapi.h"

std::vector<int> make_pids(int base_pid, int size) {
  std::vector<int> vec(size);

  for (int i = 0; i < size; i++) {
    vec[i] = base_pid + i;
  }

  return vec;
}

HidDevice::HidDevice(
    int vid, const std::vector<int> pids, int interface_num,
    bool auto_reconnection, bool non_blocking_read)
    : vid_{vid},
      pids_{pids},
      interface_num_{interface_num},
      auto_reconnection_{auto_reconnection},
      non_blocking_read_{non_blocking_read} {
  bool result = TryConnect();

  if (!result) {
    LOG->Warn(
        "HidDevice with vendor_id 0x%04x and pids 0x%s %d could not connect.",
        vid, GetPidsString(pids).c_str(), interface_num);
  } else {
    found_once_ = true;
  }
}

HidDevice::HidDevice(
    int vid, int pid, int interface_num, bool auto_reconnection,
    bool non_blocking_read)
    : HidDevice(
          vid, make_pids(pid, 1), interface_num, auto_reconnection,
          non_blocking_read) {}

HidDevice::~HidDevice() {
  Close();
  hid_exit();
}

void HidDevice::Close() {
  if (handle_ != nullptr) {
    hid_close(handle_);
  }
  handle_ = nullptr;

  found_device_info_.vid_ = 0;
  found_device_info_.pid_ = 0;
  found_device_info_.interface_num_ = 0;
  found_device_info_.path_.clear();
}

bool HidDevice::Open(const char* path) {
  handle_ = hid_open_path(path);

  if (handle_ != nullptr && non_blocking_read_) {
    hid_set_nonblocking(handle_, 1);
  }

  if (handle_) {
    LOG->Info(
        "HidDevice 0x%04x:0x%04x %d opened by path %s", found_device_info_.vid_,
        found_device_info_.pid_, found_device_info_.interface_num_,
        found_device_info_.path_.c_str());
  } else {
    LOG->Warn(
        "HidDevice 0x%04x:0x%04x %d could not be opened by path %s. Are "
        "permissions correct?",
        found_device_info_.vid_, found_device_info_.pid_,
        found_device_info_.interface_num_, found_device_info_.path_.c_str());
  }

  return handle_ != nullptr;
}

bool HidDevice::TryConnect() {
  if (!GetDeviceInfo(vid_, pids_, interface_num_, &found_device_info_)) {
    return false;
  }

  return Open(found_device_info_.path_.c_str());
}

bool HidDevice::CheckConnection() {
  if (IsConnected()) {
    return true;
  }

  if (!auto_reconnection_ || !found_once_) {
    return false;
  }

  return TryConnect();
}

const wchar_t* HidDevice::GetError() { return hid_read_error(handle_); }

bool HidDevice::IsConnected() { return handle_ != nullptr; }

bool HidDevice::FoundOnce() { return found_once_; }

const std::string HidDevice::GetPidsString(const std::vector<int> pids) {
  std::string pids_string;
  char pid[5] = {0};
  size_t size = pids.size();

  for (size_t i = 0; i < size; ++i) {
    sprintf(pid, "%04X", pids[i]);
    pids_string += pid;

    if (i != size - 1) {
      pids_string += ",";
    }
  }

  return pids_string;
}

bool HidDevice::GetDeviceInfo(
    int vid, const std::vector<int> pids, int interface_number,
    HidDeviceInfo* device_info) {
  bool found{false};
  struct hid_device_info *devs, *cur_dev;
  size_t size = pids.size();

  devs = hid_enumerate(vid, 0);
  cur_dev = devs;

  if (devs && cur_dev) {
    // Look for the desired devices by iterating connected ones
    while (cur_dev) {
      for (size_t i = 0; i < size; i++) {
        if (cur_dev->vendor_id == vid && cur_dev->product_id == pids[i]) {
          if (interface_number == -1) {
            found = true;
            break;
          } else {
            if (cur_dev->interface_number == interface_number) {
              found = true;
              break;
            }
          }
        }
      }

      if (found) {
        break;
      }

      cur_dev = cur_dev->next;
    }
  }

  if (found && cur_dev != nullptr) {
    device_info->vid_ = cur_dev->vendor_id;
    device_info->pid_ = cur_dev->product_id;
    device_info->interface_num_ = cur_dev->interface_number;
    device_info->path_ = cur_dev->path;
  }

  hid_free_enumeration(devs);
  return found;
}

bool HidDevice::IsDevicePresent(
    int vid, const std::vector<int> pids, int interface_number) {
  HidDeviceInfo device_info;
  return GetDeviceInfo(vid, pids, interface_number, &device_info);
}

bool HidDevice::IsDevicePresent(int vid, int pid, int interface_number) {
  return IsDevicePresent(vid, make_pids(pid, 1), interface_number);
}

int HidDevice::Read(unsigned char* data, size_t length) {
  if (!CheckConnection()) {
    return NotConnected;
  }

  int result = hid_read(handle_, data, length);

  if (result == -1) {
    LOG->Warn(
        "HidDevice 0x%04x:0x%04x %d read failed. Fail reason %ls",
        found_device_info_.vid_, found_device_info_.pid_,
        found_device_info_.interface_num_, GetError());
    Close();
  }

  return result;
}

HidResults HidDevice::Write(const unsigned char* data, size_t length) {
  if (!CheckConnection()) {
    return NotConnected;
  }

  size_t result = static_cast<size_t>(hid_write(handle_, data, length));

  if (result != length) {
    LOG->Warn(
        "HidDevice 0x%04x:0x%04x %d write failed. Fail reason %ls",
        found_device_info_.vid_, found_device_info_.pid_,
        found_device_info_.interface_num_, GetError());
    Close();
    return OperationFailed;
  }

  return Success;
}
