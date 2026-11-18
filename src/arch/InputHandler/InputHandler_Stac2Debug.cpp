#include "InputHandler_Stac2Debug.h"

#include <cstring>

#include "Game.h"
#include "GameState.h"
#include "InputFilter.h"
#include "PrefsManager.h"
#include "RageLog.h"
#include "RageUtil.h"

REGISTER_INPUT_HANDLER_CLASS(Stac2Debug);
REGISTER_AUTODETECT_INPUT_HANDLER_CLASS(Stac2Debug);

namespace {
constexpr uint16_t kVendorId = 0x2E8A;
constexpr uint16_t kProductIds[] = {0x10D9, 0x10E9};
constexpr uint8_t kConfigInterface = 1;
constexpr uint8_t kOutputReport = 0x11;
constexpr uint8_t kInputReport = 0x12;

Preference<bool> g_bStac2SensorDebugging("Stac2SensorDebugging", true);

struct SensorBitMapping {
  PadPanel panel;
  PadSensor sensor;
};

// CONFIG_GET_INPUTSTATE uses the same raw bitmap layout as the event queue:
// four sensor bits for each panel, ordered top, bottom, left, right.
constexpr SensorBitMapping kDanceMapping[] = {
    {PadPanel_Up, PadSensor_TopCenter},
    {PadPanel_Up, PadSensor_BottomCenter},
    {PadPanel_Up, PadSensor_LeftCenter},
    {PadPanel_Up, PadSensor_RightCenter},
    {PadPanel_Down, PadSensor_TopCenter},
    {PadPanel_Down, PadSensor_BottomCenter},
    {PadPanel_Down, PadSensor_LeftCenter},
    {PadPanel_Down, PadSensor_RightCenter},
    {PadPanel_Left, PadSensor_TopCenter},
    {PadPanel_Left, PadSensor_BottomCenter},
    {PadPanel_Left, PadSensor_LeftCenter},
    {PadPanel_Left, PadSensor_RightCenter},
    {PadPanel_Right, PadSensor_TopCenter},
    {PadPanel_Right, PadSensor_BottomCenter},
    {PadPanel_Right, PadSensor_LeftCenter},
    {PadPanel_Right, PadSensor_RightCenter},
};

constexpr SensorBitMapping kPumpMapping[] = {
    {PadPanel_UpLeft, PadSensor_TopCenter},
    {PadPanel_UpLeft, PadSensor_BottomCenter},
    {PadPanel_UpLeft, PadSensor_LeftCenter},
    {PadPanel_UpLeft, PadSensor_RightCenter},
    {PadPanel_UpRight, PadSensor_TopCenter},
    {PadPanel_UpRight, PadSensor_BottomCenter},
    {PadPanel_UpRight, PadSensor_LeftCenter},
    {PadPanel_UpRight, PadSensor_RightCenter},
    {PadPanel_Center, PadSensor_TopCenter},
    {PadPanel_Center, PadSensor_BottomCenter},
    {PadPanel_Center, PadSensor_LeftCenter},
    {PadPanel_Center, PadSensor_RightCenter},
    {PadPanel_DownLeft, PadSensor_TopCenter},
    {PadPanel_DownLeft, PadSensor_BottomCenter},
    {PadPanel_DownLeft, PadSensor_LeftCenter},
    {PadPanel_DownLeft, PadSensor_RightCenter},
    {PadPanel_DownRight, PadSensor_TopCenter},
    {PadPanel_DownRight, PadSensor_BottomCenter},
    {PadPanel_DownRight, PadSensor_LeftCenter},
    {PadPanel_DownRight, PadSensor_RightCenter},
};
}  // namespace

bool InputHandler_Stac2Debug::ShouldAutoLoad() {
  return HidDevice::IsDevicePresent(
      kVendorId, {kProductIds[0], kProductIds[1]}, kConfigInterface);
}

InputHandler_Stac2Debug::InputHandler_Stac2Debug()
    : devices_{
          {kVendorId, kProductIds[0], kConfigInterface, true, false},
          {kVendorId, kProductIds[1], kConfigInterface, true, false}} {
  for (size_t pad = 0; pad < kNumPads; ++pad) {
    if (!devices_[pad].IsConnected()) {
      continue;
    }

    std::array<uint8_t, kPacketSize> response{};
    if (SendCommand(devices_[pad], Stac2Opcode::GetInfo, response)) {
      LOG->Info(
          "Found stac2 p%d version: %s", static_cast<int>(pad + 1),
          reinterpret_cast<const char*>(&response[2]));
    } else {
      LOG->Warn(
          "Failed to open stac2 p%d config endpoint",
          static_cast<int>(pad + 1));
    }
  }
}

InputHandler_Stac2Debug::~InputHandler_Stac2Debug() {
  StopSensorDebugging();

  for (HidDevice& device : devices_) {
    if (device.IsConnected()) {
      std::array<uint8_t, kPacketSize> response{};
      SendCommand(device, Stac2Opcode::Disconnect, response);
    }
  }
}

void InputHandler_Stac2Debug::GetDevicesAndDescriptions(
    std::vector<InputDeviceInfo>& devices_out) {
  // This handler only supplies full sensor state to InputFilter.
}

void InputHandler_Stac2Debug::StartSensorDebugging() {
  if (!g_bStac2SensorDebugging || debug_thread_ != nullptr) {
    return;
  }

  bool connected = false;
  for (HidDevice& device : devices_) {
    connected = connected || device.IsConnected();
  }
  if (!connected) {
    return;
  }

  shutdown_ = false;
  debug_thread_ = new RageThread();
  debug_thread_->SetName("Stac2Debug thread");
  debug_thread_->Create(InputThreadStart, this);
}

void InputHandler_Stac2Debug::StopSensorDebugging() {
  if (debug_thread_ == nullptr) {
    return;
  }

  shutdown_ = true;
  if (debug_thread_->IsCreated()) {
    debug_thread_->Wait();
  }
  delete debug_thread_;
  debug_thread_ = nullptr;
}

int InputHandler_Stac2Debug::InputThreadStart(void* context) {
  static_cast<InputHandler_Stac2Debug*>(context)->InputThreadMain();
  return 0;
}

void InputHandler_Stac2Debug::InputThreadMain() {
  while (!shutdown_) {
    for (size_t pad = 0; pad < kNumPads && !shutdown_; ++pad) {
      if (!devices_[pad].IsConnected()) {
        continue;
      }

      std::array<uint8_t, kPacketSize> response{};
      if (!SendCommand(devices_[pad], Stac2Opcode::GetInputState, response)) {
        continue;
      }

      uint32_t new_state = 0;
      std::memcpy(&new_state, &response[2], sizeof(new_state));
      if (new_state != sensor_states_[pad]) {
        BroadcastChangedSensorState(
            static_cast<PlayerNumber>(pad), sensor_states_[pad], new_state);
        sensor_states_[pad] = new_state;
      }
    }
  }
}

void InputHandler_Stac2Debug::BroadcastChangedSensorState(
    PlayerNumber player, uint32_t old_state, uint32_t new_state) {
  const InputScheme& input = GAMESTATE->GetCurrentGame()->input_scheme_;
  const SensorBitMapping* mapping = nullptr;
  size_t mapping_size = 0;

  if (EqualsNoCase(input.name_, "dance")) {
    mapping = kDanceMapping;
    mapping_size = std::size(kDanceMapping);
  } else if (EqualsNoCase(input.name_, "pump")) {
    mapping = kPumpMapping;
    mapping_size = std::size(kPumpMapping);
  } else {
    return;
  }

  const uint32_t changed = old_state ^ new_state;
  for (size_t bit = 0; bit < mapping_size; ++bit) {
    const uint32_t mask = 1U << bit;
    if ((changed & mask) != 0) {
      INPUTFILTER->setFullSensorStateBinary(
          player, mapping[bit].panel, mapping[bit].sensor,
          (new_state & mask) != 0);
    }
  }
}

bool InputHandler_Stac2Debug::SendCommand(
    HidDevice& device, Stac2Opcode opcode,
    std::array<uint8_t, kPacketSize>& response) {
  const uint8_t opcode_value = static_cast<uint8_t>(opcode);
  std::array<uint8_t, kPacketSize> command{};
  command[0] = kOutputReport;
  command[1] = opcode_value;

  uint8_t checksum = 0;
  for (size_t index = 0; index < command.size() - 1; ++index) {
    checksum = static_cast<uint8_t>(checksum + command[index]);
  }
  command.back() = checksum;

  if (device.Write(command.data(), command.size()) != HidResults::Success) {
    LOG->Warn("stac2 could not send opcode %02x", opcode_value);
    return false;
  }

  if (device.Read(response.data(), response.size()) != kPacketSize) {
    LOG->Warn("could not read stac2 command response");
    return false;
  }
  if (response[0] != kInputReport) {
    LOG->Warn("stac2 invalid HID report: %02x", response[0]);
    return false;
  }
  if (response[1] == static_cast<uint8_t>(Stac2Opcode::Error)) {
    LOG->Warn("stac2 invalid opcode: %02x", opcode_value);
    return false;
  }
  if (response[1] != opcode_value) {
    LOG->Warn(
        "stac2 received mismatching opcode: %02x != %02x", response[1],
        opcode_value);
    return false;
  }
  return true;
}
