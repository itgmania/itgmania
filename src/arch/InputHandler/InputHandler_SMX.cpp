// do NOT let clang reposition these includes, because it will mess up the
// compilation

// clang-format off
#include "SMX.h"
#include "InputHandler_SMX.h"
#include "InputFilter.h"
#include "RageLog.h"
// clang-format on

REGISTER_INPUT_HANDLER_CLASS(SMX);

static void CustomSMXLogCallback (const char *log)
{
  LOG->Info("SMX SDK Log: %s", log);
}

void OnSMXStateChange(const int pad, const SMXUpdateCallbackReason reason, void *pUser) {
  auto *inputHandler = static_cast<InputHandler_SMX*>(pUser);

  if(SMX_REASON_IS(reason, SMXUpdateCallback_Disconnected))
  {
    LOG->Info("SMX Pad %i: disconnected\n", pad);
    return;
  }

  if(SMX_REASON_IS(reason, SMXUpdateCallback_Connected))
  {
    SMXInfo info;
    SMX_GetInfo(pad, &info);
    LOG->Info("Pad %i connected (jumper: P%i, serial: %s, fw: %i)\n",
        pad,
        info.m_bIsPlayer2 ? 2 : 1,
        info.m_bHasSerialNumber ? info.m_Serial : "(none)",
        info.m_iFirmwareVersion);

    if(!info.m_bHasSerialNumber)
      LOG->Info("Warning: Pad %i has no serial number. Call SMX_SetSerialNumbers() to assign one.\n", pad);

    return;
  }

  if(SMX_REASON_IS(reason, SMXUpdateCallback_InputState))
  {
    const uint16_t state = SMX_GetInputState(pad);
    inputHandler->ProcessInputEvent(pad, state);
  }
}

InputHandler_SMX::InputHandler_SMX() {
  // Reset our internal pad input states
  std::fill(
    std::begin(m_padInputStates),
    std::end(m_padInputStates),
    0
  );

  SMX_SetLogCallback(CustomSMXLogCallback);
}

InputHandler_SMX::~InputHandler_SMX() {
  SMX_Stop();
}

void InputHandler_SMX::GetDevicesAndDescriptions(std::vector<InputDeviceInfo>& vDevicesOut)
{
  vDevicesOut.emplace_back(DEVICE_SMX, "SMX");

  // Start the SMX SDK if needed when a pad is detected.
  // It's ok to call this repeatedly.
  SMX_Start(OnSMXStateChange, this);

  // Set the SMX thread polling values.
  // The Main Thread should probably be 50ms or faster, as some issues arise with a longer sleep.
  // The USB Thread should be set somewhere between 500us and 1000us (1ms) so we achieve full 1000hz speed.
  // TODO: This could maybe be something to expose to the options depending on users CPU power
  SMX_SetPollingRate(50, 900);
}

std::string InputHandler_SMX::GetDeviceSpecificInputString(const DeviceInput& di) {
  const int zeroIndexedButton = di.button - JOY_BUTTON_1;
  const int pad = (zeroIndexedButton / SMX_PANEL_COUNT) + 1;
  const int padRemovedButton = zeroIndexedButton % SMX_PANEL_COUNT;

  static const char* buttonStrings[SMX_PANEL_COUNT] = {
    "UpLeft", "Up", "UpRight", "Left", "Center",
    "Right", "DownLeft", "Down", "DownRight"
  };

  const char* buttonString = (padRemovedButton >= 0 && padRemovedButton < SMX_PANEL_COUNT) ? buttonStrings[padRemovedButton] : "unknown";

  char buffer[32];
  snprintf(buffer, sizeof(buffer), "SMX P%d %s", pad, buttonString);
  return {buffer};
}

void InputHandler_SMX::ProcessInputEvent(const int pad, const uint16_t state) {
  // Fast-fail if somehow the pad is outside of range
  if (pad < 0 || pad >= SMX_PAD_COUNT) {
    return;
  }

  // Compare new input state with the old
  // This function only gets called when the state changes,
  // so we are guaranteed to have a changed state here.
  const uint16_t changedInputs = state ^ m_padInputStates[pad];

  // SMX StateChange Events come in for multiple pads, but get translated into
  // one device from SM's POV.
  // Ensure P2's buttons begin from the correct `JOY_BUTTON`.
  const DeviceButton beginButton = enum_add2(JOY_BUTTON_1, pad * SMX_PANEL_COUNT);

  // Process Inputs
  for (int i = 0; i < SMX_PANEL_COUNT; i++) {
    if ((changedInputs & 1 << i) != 0) {
      const bool pressed = state & 1 << i;

      // Make input event with pressed state for this button
      DeviceInput di(DEVICE_SMX, enum_add2(beginButton, i), pressed);

      // Touch the timestamp timer to indicate the input happened **now**
      di.ts.Touch();

      // Report the input event
      ButtonPressed(di);
    }
  }

  // Save the new input state for later, and report the end of a poll.
  m_padInputStates[pad] = state;
  UpdateTimer();
}


