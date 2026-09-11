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

// this is the bit position of the response, mapped to the panel number it
// matches to.
static constexpr SnekBitMapping sensorBitMappingDance[] = {
    {14, PLAYER_2, PadPanel_Up},    {15, PLAYER_1, PadPanel_Up},
    {16, PLAYER_1, PadPanel_Left},  {17, PLAYER_2, PadPanel_Left},
    {18, PLAYER_1, PadPanel_Down},  {19, PLAYER_2, PadPanel_Down},
    {20, PLAYER_2, PadPanel_Right}, {21, PLAYER_1, PadPanel_Right},
};

InputHandler_SnekConfig::InputHandler_SnekConfig() {
  // do not start the thread when initialized.
  m_bShutdown = true;

  if (IsConnected()) {
    std::array<uint8_t, SNEK_CONFIG_PACKETSIZE> res;

    // get firmware information and check connection
    if (SendCommand(SNEK_CONFIG_OPCODE_GETINFO, 0, res)) {
      LOG->Info(
          "Found snek version: %s", reinterpret_cast<const char*>(&res[2]));
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
    std::vector<InputDeviceInfo>& vDevicesOut) {
  // input data is not used in this class, it is routed through the system.
}

int InputHandler_SnekConfig::InputThread_Start(void* p) {
  ((InputHandler_SnekConfig*)p)->InputThreadMain();
  return 0;
}

void InputHandler_SnekConfig::BroadcastFullSensorStateHelper(
    SnekBitMapping mapping, uint8_t sensor_index, bool isPressed) {
  PadSensor currSensor = PadSensor_TopCenter;

  switch (sensor_index) {
    case 0:
      currSensor = PadSensor_TopCenter;
      break;
    case 1:
      currSensor = PadSensor_BottomCenter;
      break;
    case 2:
      currSensor = PadSensor_LeftCenter;
      break;
    case 3:
      currSensor = PadSensor_RightCenter;
      break;
    default:
      LOG->Warn("Invalid sensor position %d", sensor_index);
      break;
  }

  INPUTFILTER->setFullSensorStateBinary(
      mapping.pn, mapping.panel, currSensor, isPressed);
}

void InputHandler_SnekConfig::StartSensorDebugging() {
  if (!IsConnected() || DebugThread != nullptr) {
    return;
  }

  m_bShutdown = false;

  DebugThread = new RageThread();
  DebugThread->SetName("SnekConfig debug thread");
  DebugThread->Create(InputThread_Start, this);
}

void InputHandler_SnekConfig::StopSensorDebugging() {
  if (DebugThread != nullptr && !m_bShutdown) {
    m_bShutdown = true;
    DebugThread->Wait();

    delete DebugThread;
    DebugThread = nullptr;
  }
}

void InputHandler_SnekConfig::InputThreadMain() {
  std::array<uint8_t, SNEK_CONFIG_PACKETSIZE> res;
  uint8_t sensorNumber = 0;

  while (!m_bShutdown) {
    // extio sensor cmds are 1,2,3,4, so add one to zero index.
    if (!SendCommand(
            SNEK_CONFIG_OPCODE_SET_EXTIO_SENSOR, (sensorNumber + 1), res)) {
      // bad response, try again
      continue;
    }

    uint32_t newState = 0;
    std::memcpy(&newState, &res[2], sizeof(newState));

    if (newState != sensorState[sensorNumber]) {
      // use xor to fire events only on the bits that have changed.
      uint32_t changed = newState ^ sensorState[sensorNumber];

      // walk through only the bits are are interested in (via the sensor
      // mapping)
      for (const auto& mapping : sensorBitMappingDance) {
        const uint32_t mask = 1u << mapping.bitPosition;

        if ((changed & mask) == 0) {
          // This sensor didn't change.
          continue;
        }

        const bool pressed = (newState & mask) != 0;

        // Only send an event for this changed sensor.
        BroadcastFullSensorStateHelper(mapping, sensorNumber, pressed);
      }

      // inform listeners of a new state, as during this debug state the snek
      // will not fire events over the gamepad endpoint
      // as the response is sent via the config end point.
      MESSAGEMAN->Broadcast("TestSensorRedrawEvent");

      sensorState[sensorNumber] = newState;
    }

    sensorNumber++;
    if (sensorNumber >= SNEK_CONFIG_NUM_SENSORS) {
      sensorNumber = 0;
    }
  }

  SendSafeDisconnect();
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
  if (dev.Write(cmd.data(), cmd.size()) != HidResults::Success) {
    LOG->Warn("snek could not send");
    return false;
  }

  int rtnSize = dev.Read(response.data(), response.size());

  // Read response, always a full payload size.
  if (rtnSize != SNEK_CONFIG_PACKETSIZE) {
    LOG->Warn("could not read snek command back");
    return false;
  }

  if (response[0] != SNEK_CONFIG_HID_INPUT) {
    LOG->Warn("snek invalid hid report: %02x", response[0]);
    return false;
  }

  uint8_t opencodeRtn = response[1];

  if (opencodeRtn == SNEK_CONFIG_ERROR) {
    LOG->Warn("snek invalid opcode: %02x", opcode);
    return false;
  } else if (opencodeRtn != opcode) {
    LOG->Warn(
        "snek recv mismatching opcode: %02x != %02x", opencodeRtn, opcode);
    return false;
  }

  return true;
}
