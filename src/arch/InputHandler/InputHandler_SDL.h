#ifndef INPUT_HANDLER_SDL_H
#define INPUT_HANDLER_SDL_H

#include <SDL2/SDL.h>

#include <vector>

#include "InputHandler.h"

class InputHandler_SDL : public InputHandler {
 public:
  InputHandler_SDL();
  ~InputHandler_SDL() override;

  void Update() override;
  void GetDevicesAndDescriptions(
      std::vector<InputDeviceInfo>& vDevicesOut) override;

 private:
  static DeviceButton SDLKeyToDeviceButton(const SDL_Keysym& keysym);
};

#endif