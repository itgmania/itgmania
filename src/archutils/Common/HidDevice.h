#ifndef HidDevice_H
#define HidDevice_H

#include <cstddef>
#include <string>

#include "hidapi.h"
#include "vector"

enum HidResults {
  OperationFailed = -1,
  NotConnected = -2,
  Success = 0,
};

std::vector<int> make_pids(int base_pid, int size);

struct HidDeviceInfo {
  char* path_{nullptr};
  int pid_;
  int vid_;
  int interface_num_;
};

class HidDevice {
 private:
  HidDeviceInfo found_device_info_{};
  hid_device* handle_{nullptr};

  static const std::string GetPidsString(const std::vector<int> pids);

  // Info necessary to search the connected device once a connection attempt is
  // made
  int vid_;
  const std::vector<int> pids_;
  int interface_num_ = -1;

  // Behaviour configuration
  bool auto_reconnection_ = true;
  bool non_blocking_read_ = false;

  bool found_once_ = false;
  void Close();
  bool Open(const char* path);
  bool TryConnect();
  bool CheckConnection();
  const wchar_t* GetError();

 public:
  static void GetDeviceInfo(
      int vid, const std::vector<int> pids, int interfaceNumber,
      HidDeviceInfo* device_info);

  HidDevice(
      int vid, const std::vector<int> pids, int interfaceNum = -1,
      bool autoReconnection = true, bool nonBlockingRead = false);
  HidDevice(
      int vid, int pid, int interfaceNum = -1, bool autoReconnection = true,
      bool nonBlockingRead = false);

  virtual ~HidDevice();

  bool IsConnected();
  bool FoundOnce();

  int Read(unsigned char* data, size_t length);
  HidResults Write(const unsigned char* data, size_t length);
};

#endif
