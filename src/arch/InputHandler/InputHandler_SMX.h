#ifndef INPUT_HANDLER_SMX_H
#define INPUT_HANDLER_SMX_H

#include "InputHandler.h"

constexpr int SMX_PAD_COUNT = 2;
constexpr int SMX_PANEL_COUNT = 9;

class InputHandler_SMX : public InputHandler
{
public:
  InputHandler_SMX();
  ~InputHandler_SMX() override;

  void GetDevicesAndDescriptions(std::vector<InputDeviceInfo>& vDevicesOut) override;
  std::string GetDeviceSpecificInputString(const DeviceInput &di) override;

  void ProcessInputEvent(int pad, uint16_t state);

private:
  uint16_t m_padInputStates[SMX_PAD_COUNT]{};
};

#endif  // INPUT_HANDLER_SMX_H
