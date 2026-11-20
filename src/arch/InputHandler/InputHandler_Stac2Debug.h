#ifndef INPUT_HANDLER_STAC2_DEBUG
#define INPUT_HANDLER_STAC2_DEBUG

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

#include "InputHandler.h"
#include "PlayerNumber.h"
#include "RageThreads.h"
#include "archutils/Common/HidDevice.h"

class InputHandler_Stac2Debug : public InputHandler {
 public:
  InputHandler_Stac2Debug();
  ~InputHandler_Stac2Debug() override;

  static bool ShouldAutoLoad();
  void GetDevicesAndDescriptions(
      std::vector<InputDeviceInfo>& devices_out) override;

  void StartSensorDebugging() override;
  void StopSensorDebugging() override;

 private:
  static constexpr size_t kNumPads = 2;
  static constexpr size_t kPacketSize = 64;

  enum class Stac2Opcode : uint8_t {
    GetInfo = 0x01,
    GetInputState = 0x05,
    Disconnect = 0x0A,
    Error = 0xFF,
  };

  static int InputThreadStart(void* context);
  void InputThreadMain();
  void BroadcastChangedSensorState(
      PlayerNumber player, uint32_t old_state, uint32_t new_state);
  bool SendCommand(
      HidDevice& device, Stac2Opcode opcode,
      std::array<uint8_t, kPacketSize>& response);

  HidDevice devices_[kNumPads];
  uint32_t sensor_states_[kNumPads] = {};
  std::atomic<bool> shutdown_{true};
  RageThread* debug_thread_ = nullptr;
};

#endif
