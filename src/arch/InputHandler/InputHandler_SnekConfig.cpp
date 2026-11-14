#include "InputHandler_SnekConfig.h"

#include <fcntl.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "EnumHelper.h"
#include "Game.h"
#include "GameInput.h"
#include "GameState.h"
#include "InputFilter.h"
#include "InputMapper.h"
#include "LightsManager.h"
#include "PrefsManager.h"
#include "RageInputDevice.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "StdString.h"
#include "arch/ArchHooks/ArchHooks.h"
#include "arch/InputHandler/InputHandler.h"
#include "arch/Lights/LightsDriver_Export.h"
#include "archutils/Common/HidDevice.h"

REGISTER_INPUT_HANDLER_CLASS(SnekConfig);
REGISTER_AUTODETECT_INPUT_HANDLER_CLASS(SnekConfig);

namespace {
Preference<bool> g_bSnekSensorDebugging("SnekSensorDebugging", true);

// arrow buttons are disabled on the firmware side during debugging in this
// version onward (v0.4) otherwise during debugging UP and RIGHT may blink (as
// they are responses from the foot pcb)
constexpr char SNEK_FIXED_OUTPUTS_BUILD_DATE[] = "20260302";

// extract the std::string from a response packet
std::string GetSnekVersion(
    const std::array<uint8_t, SNEK_CONFIG_PACKETSIZE>& response) {
  const char* version = reinterpret_cast<const char*>(&response[2]);
  size_t length = 0;
  const size_t max_length = response.size() - 2;
  while (length < max_length && version[length] != '\0') {
    ++length;
  }
  return std::string(version, length);
}

// Attempt to extract a datecode from the version string and require a build
// with the fixed output-button behavior before enabling sensor debugging.
bool SupportsSensorDebugging(const std::string& version) {
  constexpr size_t date_length = 8;
  const auto is_digit = [](char value) { return value >= '0' && value <= '9'; };

  for (size_t start = 0; start + date_length <= version.size(); ++start) {
    if ((start > 0 && is_digit(version[start - 1])) ||
        (start + date_length < version.size() &&
         is_digit(version[start + date_length]))) {
      continue;
    }

    bool valid_date = true;
    for (size_t offset = 0; valid_date && offset < date_length; ++offset) {
      const char digit = version[start + offset];
      valid_date = is_digit(digit);
    }

    if (valid_date) {
      return version.compare(
                 start, date_length, SNEK_FIXED_OUTPUTS_BUILD_DATE) >= 0;
    }
  }

  return false;
}
}  // namespace

// this is the bit position of the response, mapped to the panel number it
// matches to.
static constexpr SnekBitMapping sensorBitMappingDance[] = {
    {14, PLAYER_2, PadPanel_Up},    {15, PLAYER_1, PadPanel_Up},
    {16, PLAYER_1, PadPanel_Left},  {17, PLAYER_2, PadPanel_Left},
    {18, PLAYER_1, PadPanel_Down},  {19, PLAYER_2, PadPanel_Down},
    {20, PLAYER_2, PadPanel_Right}, {21, PLAYER_1, PadPanel_Right},
};

// auto load the device if the device is plugged in.
bool InputHandler_SnekConfig::ShouldAutoLoad() {
  return HidDevice::IsDevicePresent(
      SNEK_CONFIG_VID, SNEK_CONFIG_PID, SNEK_CONFIG_INTERFACE_NUM);
}

InputHandler_SnekConfig::InputHandler_SnekConfig() {
  // do not start the thread when initialized.
  shutdown_ = true;

  if (IsConnected()) {
    std::array<uint8_t, SNEK_CONFIG_PACKETSIZE> res;

    // get firmware information and check connection
    if (SendCommand(SNEK_CONFIG_OPCODE_GETINFO, 0, res)) {
      const std::string version = GetSnekVersion(res);
      LOG->Info("Found snek version: %s", version.c_str());
      sensor_debugging_supported_ = SupportsSensorDebugging(version);
      LOG->Info(
          "Snek sensor debugging firmware support: %s",
          sensor_debugging_supported_ ? "supported" : "NOT SUPPORTED");
      if (!sensor_debugging_supported_) {
        LOG->Warn(
            "Sensor debugging during gameplay is disabled for snek firmware "
            "[%s]. "
            "Please update the firmware on the device.",
            version.c_str());
      }
    } else {
      LOG->Warn("Failed to open snek config end point");
      return;
    }
  } else {
    LOG->Warn("SnekConfig enabled, but could not connect to snek.");
  }
}

InputHandler_SnekConfig::~InputHandler_SnekConfig() {
  // disconnect cleanup etc
  StopSensorDebugging();
  SendSafeDisconnect();
}

void InputHandler_SnekConfig::SendSafeDisconnect() {
  if (IsConnected()) {
    std::array<uint8_t, SNEK_CONFIG_PACKETSIZE> res;
    SendCommand(SNEK_CONFIG_OPCODE_SET_EXTIO_SENSOR, 0, res);
    SendCommand(SNEK_CONFIG_OPCODE_DISCONNECT, 0, res);
  }
}

std::string InputHandler_SnekConfig::GetDeviceSpecificInputString(
    const DeviceInput& di) {
  return InputHandler::GetDeviceSpecificInputString(di);
}

void InputHandler_SnekConfig::GetDevicesAndDescriptions(
    std::vector<InputDeviceInfo>& devices_out) {
  // input data is not used in this class, it is routed through the system.
}

int InputHandler_SnekConfig::InputThread_Start(void* p) {
  ((InputHandler_SnekConfig*)p)->InputThreadMain();
  return 0;
}

void InputHandler_SnekConfig::BroadcastFullSensorStateHelper(
    SnekBitMapping mapping, uint8_t sensor_index, bool is_pressed) {
  PadSensor curr_sensor = PadSensor_TopCenter;

  switch (sensor_index) {
    case 0:
      curr_sensor = PadSensor_TopCenter;
      break;
    case 1:
      curr_sensor = PadSensor_BottomCenter;
      break;
    case 2:
      curr_sensor = PadSensor_LeftCenter;
      break;
    case 3:
      curr_sensor = PadSensor_RightCenter;
      break;
    default:
      LOG->Warn("Invalid sensor position %d", sensor_index);
      break;
  }

  INPUTFILTER->setFullSensorStateBinary(
      mapping.pn_, mapping.panel_, curr_sensor, is_pressed);
}

void InputHandler_SnekConfig::StartSensorDebugging() {
  if (!g_bSnekSensorDebugging || !sensor_debugging_supported_ ||
      !IsConnected() || debug_thread_ != nullptr) {
    return;
  }

  shutdown_ = false;

  debug_thread_ = new RageThread();
  debug_thread_->SetName("SnekConfig debug thread");
  debug_thread_->Create(InputThread_Start, this);
}

void InputHandler_SnekConfig::StopSensorDebugging() {
  if (debug_thread_ != nullptr && !shutdown_) {
    shutdown_ = true;
    debug_thread_->Wait();

    delete debug_thread_;
    debug_thread_ = nullptr;
  }
}

void InputHandler_SnekConfig::InputThreadMain() {
  std::array<uint8_t, SNEK_CONFIG_PACKETSIZE> res;
  uint8_t sensor_number = 0;

  while (!shutdown_) {
    // extio sensor cmds are 1,2,3,4, so add one to zero index.
    if (!SendCommand(
            SNEK_CONFIG_OPCODE_SET_EXTIO_SENSOR, (sensor_number + 1), res)) {
      // bad response, try again
      continue;
    }

    uint32_t new_state = 0;
    std::memcpy(&new_state, &res[2], sizeof(new_state));

    if (new_state != sensor_state_[sensor_number]) {
      // use xor to fire events only on the bits that have changed.
      uint32_t changed = new_state ^ sensor_state_[sensor_number];

      // walk through only the bits are are interested in (via the sensor
      // mapping)
      for (const auto& mapping : sensorBitMappingDance) {
        const uint32_t mask = 1u << mapping.bit_position_;

        if ((changed & mask) == 0) {
          // This sensor didn't change.
          continue;
        }

        const bool pressed = (new_state & mask) != 0;

        // Only send an event for this changed sensor.
        BroadcastFullSensorStateHelper(mapping, sensor_number, pressed);
      }

      sensor_state_[sensor_number] = new_state;
    }

    sensor_number++;
    if (sensor_number >= SNEK_CONFIG_NUM_SENSORS) {
      sensor_number = 0;
    }
  }

  // after manually polling, set sensor 0 to go back into game mode
  // on disconnect we will send the proper disconnect command.
  SendCommand(SNEK_CONFIG_OPCODE_SET_EXTIO_SENSOR, 0, res);
}

bool InputHandler_SnekConfig::SendCommand(
    uint8_t opcode, uint8_t payload,
    std::array<uint8_t, SNEK_CONFIG_PACKETSIZE>& response) {
  std::array<uint8_t, SNEK_CONFIG_PACKETSIZE> cmd{};

  cmd[0] = SNEK_CONFIG_HID_OUTPUT;
  cmd[1] = opcode;
  cmd[2] = payload;

  // Compute CRC over bytes 0-62
  uint8_t crc = 0;
  for (size_t i = 0; i < SNEK_CONFIG_PACKETSIZE - 1; ++i) {
    crc = static_cast<uint8_t>(crc + cmd[i]);
  }

  // last byte is the crc.
  cmd[(SNEK_CONFIG_PACKETSIZE - 1)] = crc;

  // Send command
  if (dev_.Write(cmd.data(), cmd.size()) != HidResults::Success) {
    LOG->Warn("snek could not send");
    return false;
  }

  int rtn_size = dev_.Read(response.data(), response.size());

  // Read response, always a full payload size.
  if (rtn_size != SNEK_CONFIG_PACKETSIZE) {
    LOG->Warn("could not read snek command back");
    return false;
  }

  if (response[0] != SNEK_CONFIG_HID_INPUT) {
    LOG->Warn("snek invalid hid report: %02x", response[0]);
    return false;
  }

  uint8_t opencode_rtn = response[1];

  if (opencode_rtn == SNEK_CONFIG_ERROR) {
    LOG->Warn("snek invalid opcode: %02x", opcode);
    return false;
  } else if (opencode_rtn != opcode) {
    LOG->Warn(
        "snek recv mismatching opcode: %02x != %02x", opencode_rtn, opcode);
    return false;
  }

  return true;
}
