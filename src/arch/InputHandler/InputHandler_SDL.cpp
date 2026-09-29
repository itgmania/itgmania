#include "InputHandler_SDL.h"

#include <SDL2/SDL.h>

#include "RageLog.h"
#include "arch/ArchHooks/ArchHooks.h"
#include "global.h"

REGISTER_INPUT_HANDLER_CLASS(SDL);

static void EnsureSdlEventsInitialized() {
  if (SDL_WasInit(SDL_INIT_EVENTS) == 0) {
    const int ret = SDL_InitSubSystem(SDL_INIT_EVENTS);
    if (ret != 0) {
      LOG->Warn("SDL error: %s", SDL_GetError());
    }
  }
}

InputHandler_SDL::InputHandler_SDL() { EnsureSdlEventsInitialized(); }

InputHandler_SDL::~InputHandler_SDL() = default;

void InputHandler_SDL::GetDevicesAndDescriptions(
    std::vector<InputDeviceInfo>& vDevicesOut) {
  vDevicesOut.push_back(InputDeviceInfo(DEVICE_KEYBOARD, "SDL"));
}

DeviceButton InputHandler_SDL::SDLKeyToDeviceButton(const SDL_Keysym& keysym) {
  switch (keysym.sym) {
    case SDLK_BACKSPACE:
      return KEY_BACK;
    case SDLK_TAB:
      return KEY_TAB;
    case SDLK_RETURN:
      return KEY_ENTER;
    case SDLK_ESCAPE:
      return KEY_ESC;
    case SDLK_SPACE:
      return KEY_SPACE;
    case SDLK_DELETE:
      return KEY_DEL;

    case SDLK_F1:
      return KEY_F1;
    case SDLK_F2:
      return KEY_F2;
    case SDLK_F3:
      return KEY_F3;
    case SDLK_F4:
      return KEY_F4;
    case SDLK_F5:
      return KEY_F5;
    case SDLK_F6:
      return KEY_F6;
    case SDLK_F7:
      return KEY_F7;
    case SDLK_F8:
      return KEY_F8;
    case SDLK_F9:
      return KEY_F9;
    case SDLK_F10:
      return KEY_F10;
    case SDLK_F11:
      return KEY_F11;
    case SDLK_F12:
      return KEY_F12;
    case SDLK_F13:
      return KEY_F13;
    case SDLK_F14:
      return KEY_F14;
    case SDLK_F15:
      return KEY_F15;
    case SDLK_F16:
      return KEY_F16;

    case SDLK_LCTRL:
      return KEY_LCTRL;
    case SDLK_RCTRL:
      return KEY_RCTRL;
    case SDLK_LSHIFT:
      return KEY_LSHIFT;
    case SDLK_RSHIFT:
      return KEY_RSHIFT;
    case SDLK_LALT:
      return KEY_LALT;
    case SDLK_RALT:
      return KEY_RALT;
    case SDLK_LGUI:
      return KEY_LSUPER;
    case SDLK_RGUI:
      return KEY_RSUPER;
    case SDLK_MENU:
      return KEY_MENU;
    case SDLK_APPLICATION:
      return KEY_MENU;

    case SDLK_NUMLOCKCLEAR:
      return KEY_NUMLOCK;
    case SDLK_SCROLLLOCK:
      return KEY_SCRLLOCK;
    case SDLK_CAPSLOCK:
      return KEY_CAPSLOCK;
    case SDLK_PRINTSCREEN:
      return KEY_PRTSC;
    case SDLK_PAUSE:
      return KEY_PAUSE;

    case SDLK_UP:
      return KEY_UP;
    case SDLK_DOWN:
      return KEY_DOWN;
    case SDLK_LEFT:
      return KEY_LEFT;
    case SDLK_RIGHT:
      return KEY_RIGHT;

    case SDLK_INSERT:
      return KEY_INSERT;
    case SDLK_HOME:
      return KEY_HOME;
    case SDLK_END:
      return KEY_END;
    case SDLK_PAGEUP:
      return KEY_PGUP;
    case SDLK_PAGEDOWN:
      return KEY_PGDN;

    case SDLK_KP_0:
      return KEY_KP_C0;
    case SDLK_KP_1:
      return KEY_KP_C1;
    case SDLK_KP_2:
      return KEY_KP_C2;
    case SDLK_KP_3:
      return KEY_KP_C3;
    case SDLK_KP_4:
      return KEY_KP_C4;
    case SDLK_KP_5:
      return KEY_KP_C5;
    case SDLK_KP_6:
      return KEY_KP_C6;
    case SDLK_KP_7:
      return KEY_KP_C7;
    case SDLK_KP_8:
      return KEY_KP_C8;
    case SDLK_KP_9:
      return KEY_KP_C9;
    case SDLK_KP_DIVIDE:
      return KEY_KP_SLASH;
    case SDLK_KP_MULTIPLY:
      return KEY_KP_ASTERISK;
    case SDLK_KP_MINUS:
      return KEY_KP_HYPHEN;
    case SDLK_KP_PLUS:
      return KEY_KP_PLUS;
    case SDLK_KP_PERIOD:
      return KEY_KP_PERIOD;
    case SDLK_KP_EQUALS:
      return KEY_KP_EQUAL;
    case SDLK_KP_ENTER:
      return KEY_KP_ENTER;

    default:
      break;
  }

  // SDL keycodes for ASCII keys generally match their character codes.
  if (keysym.sym >= 32 && keysym.sym <= 127) {
    return static_cast<DeviceButton>(keysym.sym);
  }

  // Fall back to scancode-based mapping into KEY_OTHER_0...KEY_LAST_OTHER.
  const int scancode = static_cast<int>(keysym.scancode);
  const int mapped = static_cast<int>(KEY_OTHER_0) + scancode;
  if (mapped <= static_cast<int>(KEY_LAST_OTHER)) {
    return static_cast<DeviceButton>(mapped);
  }

  return DeviceButton_Invalid;
}

void InputHandler_SDL::Update() {
  EnsureSdlEventsInitialized();

  SDL_Event ev;
  while (SDL_PollEvent(&ev)) {
    switch (ev.type) {
      case SDL_QUIT:
        ArchHooks::SetUserQuit();
        break;
      case SDL_WINDOWEVENT:
        if (HOOKS != nullptr) {
          switch (ev.window.event) {
            case SDL_WINDOWEVENT_FOCUS_GAINED:
              HOOKS->SetHasFocus(true);
              break;
            case SDL_WINDOWEVENT_FOCUS_LOST:
              HOOKS->SetHasFocus(false);
              break;
            case SDL_WINDOWEVENT_CLOSE:
              ArchHooks::SetUserQuit();
              break;
            default:
              break;
          }
        }
        break;
      case SDL_KEYDOWN:
      case SDL_KEYUP: {
        if (ev.key.repeat != 0) {
          break;
        }

        const DeviceButton button = SDLKeyToDeviceButton(ev.key.keysym);
        if (button == DeviceButton_Invalid) {
          break;
        }

        const float level = (ev.type == SDL_KEYDOWN) ? 1.0f : 0.0f;
        DeviceInput di(DEVICE_KEYBOARD, button, level);
        di.ts.SetZero();
        ButtonPressed(di);
        break;
      }
      default:
        break;
    }
  }

  InputHandler::UpdateTimer();
}