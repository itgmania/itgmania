#include "global.h"
#include "InputHandler_SDL.h"
#include "RageUtil.h"
#include "RageLog.h"
#include "RageDisplay.h"
#include "InputFilter.h"

#include <algorithm>
#include <vector>
#include <SDL3/SDL.h>

#include "arch/ArchHooks/ArchHooks_Unix.h"

REGISTER_INPUT_HANDLER_CLASS(SDL);

static DeviceButton SDLToDeviceButton(int key, bool shift)
{
#define KEY_INV DeviceButton_Invalid
    switch (key)
    {
    case SDLK_A: return shift ? KEY_CA : KEY_Ca;
    case SDLK_B: return shift ? KEY_CB : KEY_Cb;
    case SDLK_C: return shift ? KEY_CC : KEY_Cc;
    case SDLK_D: return shift ? KEY_CD : KEY_Cd;
    case SDLK_E: return shift ? KEY_CE : KEY_Ce;
    case SDLK_F: return shift ? KEY_CF : KEY_Cf;
    case SDLK_G: return shift ? KEY_CG : KEY_Cg;
    case SDLK_H: return shift ? KEY_CH : KEY_Ch;
    case SDLK_I: return shift ? KEY_CI : KEY_Ci;
    case SDLK_J: return shift ? KEY_CJ : KEY_Cj;
    case SDLK_K: return shift ? KEY_CK : KEY_Ck;
    case SDLK_L: return shift ? KEY_CL : KEY_Cl;
    case SDLK_M: return shift ? KEY_CM : KEY_Cm;
    case SDLK_N: return shift ? KEY_CN : KEY_Cn;
    case SDLK_O: return shift ? KEY_CO : KEY_Co;
    case SDLK_P: return shift ? KEY_CP : KEY_Cp;
    case SDLK_Q: return shift ? KEY_CQ : KEY_Cq;
    case SDLK_R: return shift ? KEY_CR : KEY_Cr;
    case SDLK_S: return shift ? KEY_CS : KEY_Cs;
    case SDLK_T: return shift ? KEY_CT : KEY_Ct;
    case SDLK_U: return shift ? KEY_CU : KEY_Cu;
    case SDLK_V: return shift ? KEY_CV : KEY_Cv;
    case SDLK_X: return shift ? KEY_CX : KEY_Cx;
    case SDLK_Y: return shift ? KEY_CY : KEY_Cy;
    case SDLK_Z: return shift ? KEY_CZ : KEY_Cz;

    case SDLK_1: return KEY_C1;
    case SDLK_2: return KEY_C2;
    case SDLK_3: return KEY_C3;
    case SDLK_4: return KEY_C4;
    case SDLK_5: return KEY_C5;
    case SDLK_6: return KEY_C6;
    case SDLK_7: return KEY_C7;
    case SDLK_8: return KEY_C8;
    case SDLK_9: return KEY_C9;
    case SDLK_0: return KEY_C0;

    case SDLK_SPACE: return KEY_SPACE;
    case SDLK_EXCLAIM: return KEY_EXCL;
    case SDLK_DBLAPOSTROPHE: return KEY_QUOTE;
    case SDLK_HASH: return KEY_HASH;
    case SDLK_DOLLAR: return KEY_DOLLAR;
    case SDLK_PERCENT: return KEY_PERCENT;
    case SDLK_AMPERSAND: return KEY_AMPER;
    case SDLK_APOSTROPHE: return KEY_SQUOTE;
    case SDLK_LEFTPAREN: return KEY_LPAREN;
    case SDLK_RIGHTPAREN: return KEY_RPAREN;
    case SDLK_ASTERISK: return KEY_ASTERISK;
    case SDLK_PLUS: return KEY_PLUS;
    case SDLK_COMMA: return KEY_COMMA;
    case SDLK_MINUS: return KEY_HYPHEN;
    case SDLK_PERIOD: return KEY_PERIOD;
    case SDLK_SLASH: return KEY_SLASH;
    case SDLK_COLON: return KEY_COLON;
    case SDLK_SEMICOLON: return KEY_SEMICOLON;
    case SDLK_LESS: return KEY_LANGLE;
    case SDLK_EQUALS: return KEY_EQUAL;
    case SDLK_GREATER: return KEY_RANGLE;
    case SDLK_QUESTION: return KEY_QUOTE;
    case SDLK_AT: return KEY_AT;
    case SDLK_LEFTBRACKET: return KEY_LBRACKET;
    case SDLK_BACKSLASH: return KEY_BACKSLASH;
    case SDLK_RIGHTBRACKET: return KEY_RBRACKET;
    case SDLK_CARET: return KEY_CARAT;
    case SDLK_UNDERSCORE: return KEY_UNDERSCORE;
    case SDLK_GRAVE: return KEY_ACCENT;
    case SDLK_LEFTBRACE: return KEY_LBRACE;
    case SDLK_PIPE: return KEY_PIPE;
    case SDLK_RIGHTBRACE: return KEY_RBRACE;
    case SDLK_DELETE: return KEY_DEL;
    case SDLK_BACKSPACE: return KEY_BACK;
    case SDLK_TAB: return KEY_TAB;
    case SDLK_RETURN: return KEY_ENTER;
    case SDLK_PAUSE: return KEY_PAUSE;
    case SDLK_ESCAPE: return KEY_ESC;
    case SDLK_F1: return KEY_F1;
    case SDLK_F2: return KEY_F2;
    case SDLK_F3: return KEY_F3;
    case SDLK_F4: return KEY_F4;
    case SDLK_F5: return KEY_F5;
    case SDLK_F6: return KEY_F6;
    case SDLK_F7: return KEY_F7;
    case SDLK_F8: return KEY_F8;
    case SDLK_F9: return KEY_F9;
    case SDLK_F10: return KEY_F10;
    case SDLK_F11: return KEY_F11;
    case SDLK_F12: return KEY_F12;
    case SDLK_F13: return KEY_F13;
    case SDLK_F14: return KEY_F14;
    case SDLK_F15: return KEY_F15;
    case SDLK_F16: return KEY_F16;

    case SDLK_LCTRL: return KEY_LCTRL;
    case SDLK_RCTRL: return KEY_RCTRL;
    case SDLK_LSHIFT: return KEY_LSHIFT;
    case SDLK_RSHIFT: return KEY_RSHIFT;
    case SDLK_LALT: return KEY_LALT;
    case SDLK_RALT: return KEY_RALT;
    case SDLK_LGUI: return KEY_LMETA;
    case SDLK_RGUI: return KEY_RMETA;
    // Why are meta and super keys defined separately?
    case SDLK_MENU: return KEY_MENU;
    case SDLK_MODE: return KEY_FN;
    case SDLK_NUMLOCKCLEAR: return KEY_NUMLOCK;
    case SDLK_SCROLLLOCK: return KEY_SCRLLOCK;
    case SDLK_CAPSLOCK: return KEY_CAPSLOCK;
    case SDLK_PRINTSCREEN: return KEY_PRTSC;

    case SDLK_UP: return KEY_UP;
    case SDLK_DOWN: return KEY_DOWN;
    case SDLK_LEFT: return KEY_LEFT;
    case SDLK_RIGHT: return KEY_RIGHT;

    case SDLK_INSERT: return KEY_INSERT;
    case SDLK_HOME: return KEY_HOME;
    case SDLK_END: return KEY_END;
    case SDLK_PAGEUP: return KEY_PGUP;
    case SDLK_PAGEDOWN: return KEY_PGDN;

    case SDLK_KP_0: return KEY_KP_C0;
    case SDLK_KP_1: return KEY_KP_C1;
    case SDLK_KP_2: return KEY_KP_C2;
    case SDLK_KP_3: return KEY_KP_C3;
    case SDLK_KP_4: return KEY_KP_C4;
    case SDLK_KP_5: return KEY_KP_C5;
    case SDLK_KP_6: return KEY_KP_C6;
    case SDLK_KP_7: return KEY_KP_C7;
    case SDLK_KP_8: return KEY_KP_C8;
    case SDLK_KP_9: return KEY_KP_C9;
    case SDLK_KP_DIVIDE: return KEY_KP_SLASH;
    case SDLK_KP_MULTIPLY: return KEY_KP_ASTERISK;
    case SDLK_KP_MINUS: return KEY_KP_HYPHEN;
    case SDLK_KP_PLUS: return KEY_KP_PLUS;
    case SDLK_KP_PERIOD: return KEY_KP_PERIOD;
    case SDLK_KP_EQUALS: return KEY_KP_EQUAL;
    case SDLK_KP_ENTER: return KEY_KP_ENTER;
    default: return DeviceButton_Invalid;
    }
}

InputHandler_SDL::InputHandler_SDL()
{
    LOG->Info("[InputHandler_SDL] Using InputHandler_SDL");

    m_pWindow = SDL_GL_GetCurrentWindow();
    if (!m_pWindow)
    {
        LOG->Info("[InputHandler_SDL] Could not get window!");
        return;
    }

    m_devicesChanged = false;
    QueryJoysticks();
}

InputHandler_SDL::~InputHandler_SDL()
{
    for (auto pGamepad : m_pGamepads)
    {
        SDL_CloseGamepad(pGamepad);
    }
    for (auto pJoystick : m_pJoysticks)
    {
        SDL_CloseJoystick(pJoystick);
    }
    m_pGamepads.clear();
    m_pJoysticks.clear();
}

void InputHandler_SDL::Update()
{
    if (!m_pWindow)
    {
        InputHandler::UpdateTimer();
        return;
    }

    // Reset any wheel motion (we do not get notified of inaction)
    ButtonPressed(DeviceInput(DEVICE_MOUSE, MOUSE_WHEELUP, 0));
    ButtonPressed(DeviceInput(DEVICE_MOUSE, MOUSE_WHEELDOWN, 0));
    INPUTFILTER->UpdateMouseWheel(0);

    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        // For Joystick/Gamepad cross-over
        unsigned int eventDeviceId = -1;
        int axisIndex = SDL_GAMEPAD_AXIS_INVALID;
        float axisValue = 0.0f;

        switch (event.type)
        {
        case SDL_EVENT_QUIT:
            ArchHooks::SetUserQuit();
            break;
        case SDL_EVENT_KEY_UP:
        case SDL_EVENT_KEY_DOWN:
            {
                auto released = event.type == SDL_EVENT_KEY_UP;
                auto btn = SDLToDeviceButton(static_cast<int>(event.key.key), event.key.mod & SDL_KMOD_SHIFT);
                if (btn == DeviceButton_Invalid)
                {
                    break;
                }
                RegisterKeyEvent(event.key.timestamp, !released, btn);
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            INPUTFILTER->UpdateCursorLocation(event.motion.x, event.motion.y);
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            {
                auto released = event.type == SDL_EVENT_MOUSE_BUTTON_UP;
                auto btn = DeviceButton_Invalid;

                switch (event.button.button)
                {
                case SDL_BUTTON_LEFT: btn = MOUSE_LEFT;
                    break;
                case SDL_BUTTON_RIGHT: btn = MOUSE_RIGHT;
                    break;
                case SDL_BUTTON_MIDDLE: btn = MOUSE_MIDDLE;
                    break;
                default: break;
                }

                if (btn == DeviceButton_Invalid)
                {
                    break;
                }

                ButtonPressed(DeviceInput(DEVICE_MOUSE, btn, released ? 0 : 1));
            }
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            {
                auto scrollUp = event.wheel.y > 0;
                ButtonPressed(DeviceInput(DEVICE_MOUSE, MOUSE_WHEELUP, scrollUp ? 1 : 0));
                ButtonPressed(DeviceInput(DEVICE_MOUSE, MOUSE_WHEELDOWN, scrollUp ? 0 : 1));
                INPUTFILTER->UpdateMouseWheel(event.wheel.y);
            }
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            {
                auto released = event.type == SDL_EVENT_GAMEPAD_BUTTON_UP;
                auto btn = DeviceButton_Invalid;

                switch (event.gbutton.button)
                {
                case SDL_GAMEPAD_BUTTON_SOUTH: btn = JOY_BUTTON_1;
                    break;
                case SDL_GAMEPAD_BUTTON_EAST: btn = JOY_BUTTON_2;
                    break;
                case SDL_GAMEPAD_BUTTON_WEST: btn = JOY_BUTTON_3;
                    break;
                case SDL_GAMEPAD_BUTTON_NORTH: btn = JOY_BUTTON_4;
                    break;
                case SDL_GAMEPAD_BUTTON_START: btn = JOY_BUTTON_5;
                    break;
                case SDL_GAMEPAD_BUTTON_BACK: btn = JOY_BUTTON_6;
                    break;
                case SDL_GAMEPAD_BUTTON_LEFT_STICK: btn = JOY_BUTTON_7;
                    break;
                case SDL_GAMEPAD_BUTTON_RIGHT_STICK: btn = JOY_BUTTON_8;
                    break;
                case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: btn = JOY_BUTTON_9;
                    break;
                case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: btn = JOY_BUTTON_10;
                    break;
                case SDL_GAMEPAD_BUTTON_GUIDE: btn = JOY_BUTTON_13;
                    break;
                case SDL_GAMEPAD_BUTTON_MISC1: btn = JOY_BUTTON_14;
                    break;
                case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1: btn = JOY_BUTTON_15;
                    break;
                case SDL_GAMEPAD_BUTTON_LEFT_PADDLE1: btn = JOY_BUTTON_16;
                    break;
                case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2: btn = JOY_BUTTON_17;
                    break;
                case SDL_GAMEPAD_BUTTON_LEFT_PADDLE2: btn = JOY_BUTTON_18;
                    break;
                case SDL_GAMEPAD_BUTTON_MISC2: btn = JOY_BUTTON_19;
                    break;
                case SDL_GAMEPAD_BUTTON_MISC3: btn = JOY_BUTTON_20;
                    break;
                case SDL_GAMEPAD_BUTTON_MISC4: btn = JOY_BUTTON_21;
                    break;
                case SDL_GAMEPAD_BUTTON_MISC5: btn = JOY_BUTTON_22;
                    break;
                case SDL_GAMEPAD_BUTTON_MISC6: btn = JOY_BUTTON_23;
                    break;
                case SDL_GAMEPAD_BUTTON_TOUCHPAD: btn = JOY_BUTTON_24;
                    break;
                case SDL_GAMEPAD_BUTTON_DPAD_UP: btn = JOY_HAT_UP;
                    break;
                case SDL_GAMEPAD_BUTTON_DPAD_DOWN: btn = JOY_HAT_DOWN;
                    break;
                case SDL_GAMEPAD_BUTTON_DPAD_LEFT: btn = JOY_HAT_LEFT;
                    break;
                case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: btn = JOY_HAT_RIGHT;
                    break;
                default: break;
                }

                auto deviceId = m_connectedDevices.at(event.gbutton.which);
                ButtonPressed(DeviceInput(static_cast<InputDevice>(deviceId), btn, released ? 0 : 1));
            }
            break;
        // Joystick axis seems to align with Gamepads
        case SDL_EVENT_JOYSTICK_AXIS_MOTION:
            if (SDL_IsGamepad(event.jaxis.which))
            {
                break;
            }

            axisValue = event.jaxis.value;
            axisIndex = event.jaxis.axis;
            eventDeviceId = event.jaxis.which;
        // Fall-through
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            {
                if (axisIndex == SDL_GAMEPAD_AXIS_INVALID)
                {
                    axisIndex = event.gaxis.axis;
                    axisValue = event.gaxis.value;
                    eventDeviceId = event.gaxis.which;
                }

                auto deadzoneAdjust = axisValue < 0 ? -1.0 : 1.0;
                auto btn = DeviceButton_Invalid;

                switch (axisIndex)
                {
                case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
                    btn = JOY_BUTTON_11;
                    break;
                case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
                    btn = JOY_BUTTON_12;
                    break;
                case SDL_GAMEPAD_AXIS_LEFTX:
                    btn = event.gaxis.value < 0 ? JOY_LEFT : JOY_RIGHT;
                    break;
                case SDL_GAMEPAD_AXIS_LEFTY:
                    btn = event.gaxis.value < 0 ? JOY_UP : JOY_DOWN;
                    break;
                case SDL_GAMEPAD_AXIS_RIGHTX:
                    btn = event.gaxis.value < 0 ? JOY_LEFT_2 : JOY_RIGHT_2;
                    break;
                case SDL_GAMEPAD_AXIS_RIGHTY:
                    btn = event.gaxis.value < 0 ? JOY_UP_2 : JOY_DOWN_2;
                    break;
                default: break;
                }

                if (btn == DeviceButton_Invalid)
                {
                    break;
                }

                // This isn't actually correct for joysticks, as it'll produce a square deadzone radius lol.
                // TODO: Implement DirectInput's deadzone calculations
                auto released = axisValue * deadzoneAdjust < (SDL_JOYSTICK_AXIS_MAX / 2);
                auto deviceId = m_connectedDevices.at(eventDeviceId);
                ButtonPressed(DeviceInput(static_cast<InputDevice>(deviceId), btn, released ? 0 : 1));
            }
            break;
        // -- Joysticks
        // These cover gamepad added/removed too!
        case SDL_EVENT_JOYSTICK_ADDED:
            ConnectJoystick(event.jdevice.which);
            break;
        case SDL_EVENT_JOYSTICK_REMOVED:
            DisconnectJoystick(event.jdevice.which);
            break;
        case SDL_EVENT_JOYSTICK_BUTTON_UP:
        case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
            {
                // We'll get double inputs because gamepads are joysticks behind the scenes, so ignore gamepads.
                if (SDL_IsGamepad(event.jbutton.which))
                {
                    break;
                }

                auto released = event.type == SDL_EVENT_JOYSTICK_BUTTON_UP;
                auto btn = JOY_BUTTON_1 + event.jbutton.button;

                // Prevent any buttons higher than the max supported buttons
                if (btn > JOY_BUTTON_32)
                {
                    break;
                }

                auto deviceId = m_connectedDevices.at(event.jbutton.which);
                ButtonPressed(DeviceInput(static_cast<InputDevice>(deviceId), static_cast<DeviceButton>(btn), released ? 0 : 1));
            }
            break;
        case SDL_EVENT_JOYSTICK_HAT_MOTION:
            {
                if (SDL_IsGamepad(event.jhat.which))
                {
                    break;
                }

                auto btn = DeviceButton_Invalid;
                switch (event.jhat.hat)
                {
                case SDL_HAT_UP: btn = JOY_HAT_UP;
                    break;
                case SDL_HAT_DOWN: btn = JOY_HAT_DOWN;
                    break;
                case SDL_HAT_LEFT: btn = JOY_HAT_LEFT;
                    break;
                case SDL_HAT_RIGHT: btn = JOY_HAT_RIGHT;
                    break;
                default: break;
                }

                if (btn == DeviceButton_Invalid)
                {
                    break;
                }

                auto deviceId = m_connectedDevices.at(event.jbutton.which);
                ButtonPressed(DeviceInput(static_cast<InputDevice>(deviceId), btn, 1));
            }
            break;
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            {
                auto p = DISPLAY->GetActualVideoModeParams();
                bool ignore = false;
                DISPLAY->SetVideoMode(static_cast<VideoModeParams>(p), ignore);
            }
            break;
        default:
            break;
        }
    }

    InputHandler::UpdateTimer();
}


void InputHandler_SDL::GetDevicesAndDescriptions(std::vector<InputDeviceInfo>& vDevicesOut)
{
    if (!m_pWindow)
    {
        return;
    }

    vDevicesOut.emplace_back(DEVICE_KEYBOARD, "Keyboard");
    vDevicesOut.emplace_back(DEVICE_MOUSE, "Mouse");

    for (auto pGamepad : m_pGamepads)
    {
        auto joystickId = SDL_GetGamepadID(pGamepad);
        auto deviceIndex = m_connectedDevices.at(joystickId);
        vDevicesOut.emplace_back(static_cast<InputDevice>(deviceIndex), SDL_GetGamepadNameForID(joystickId));
    }
    for (auto pJoystick : m_pJoysticks)
    {
        auto joystickId = SDL_GetJoystickID(pJoystick);
        auto deviceIndex = m_connectedDevices.at(joystickId);
        vDevicesOut.emplace_back(static_cast<InputDevice>(deviceIndex), SDL_GetJoystickNameForID(joystickId));
    }
}

/**
 * An exhaustive Device -> Button Name function
 * @param di
 * @return
 */
RString InputHandler_SDL::GetDeviceSpecificInputString(const DeviceInput& di)
{
    auto deviceIndex = static_cast<int>(di.device);
    auto deviceString = ssprintf("(Joy%d)", deviceIndex).c_str();
    auto result = std::find_if(
        m_connectedDevices.begin(),
        m_connectedDevices.end(),
        [deviceIndex](const auto& connectedDevice) { return connectedDevice.second == deviceIndex; });

    if (result == m_connectedDevices.end())
    {
        return InputHandler::GetDeviceSpecificInputString(di);
    }

    auto joystickId = result->first;

    if (SDL_IsGamepad(joystickId))
    {
        auto pGamepad = SDL_GetGamepadFromID(joystickId);
        auto gamepadType = SDL_GetGamepadTypeForID(joystickId);
        auto btn = SDL_GAMEPAD_BUTTON_INVALID;
        auto axisBtn = SDL_GAMEPAD_AXIS_INVALID;
        bool axisPositive = true;

        // TODO: Confirm if this is actually useful - or should this be localized in the theme?
        const bool isNewerMicrosoft = gamepadType == SDL_GAMEPAD_TYPE_XBOXONE;
        const bool isOlderSony = gamepadType == SDL_GAMEPAD_TYPE_PS3;
        const bool isNewerSony = gamepadType == SDL_GAMEPAD_TYPE_PS4 || gamepadType == SDL_GAMEPAD_TYPE_PS5;
        const bool isNintendo = gamepadType >= SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO && gamepadType <= SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT;

        switch (di.button)
        {
        case JOY_BUTTON_1: btn = SDL_GAMEPAD_BUTTON_SOUTH;
            break;
        case JOY_BUTTON_2: btn = SDL_GAMEPAD_BUTTON_EAST;
            break;
        case JOY_BUTTON_3: btn = SDL_GAMEPAD_BUTTON_WEST;
            break;
        case JOY_BUTTON_4: btn = SDL_GAMEPAD_BUTTON_NORTH;
            break;
        case JOY_BUTTON_5: btn = SDL_GAMEPAD_BUTTON_START;
            break;
        case JOY_BUTTON_6: btn = SDL_GAMEPAD_BUTTON_BACK;
            break;
        case JOY_BUTTON_7: btn = SDL_GAMEPAD_BUTTON_LEFT_STICK;
            break;
        case JOY_BUTTON_8: btn = SDL_GAMEPAD_BUTTON_RIGHT_STICK;
            break;
        case JOY_BUTTON_9: btn = SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
            break;
        case JOY_BUTTON_10: btn = SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
            break;
        case JOY_BUTTON_11: axisBtn = SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
            break;
        case JOY_BUTTON_12: axisBtn = SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
            break;
        case JOY_BUTTON_13: btn = SDL_GAMEPAD_BUTTON_GUIDE;
            break;
        case JOY_BUTTON_14: btn = SDL_GAMEPAD_BUTTON_MISC1;
            break;
        case JOY_BUTTON_15: btn = SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1;
            break;
        case JOY_BUTTON_16: btn = SDL_GAMEPAD_BUTTON_LEFT_PADDLE1;
            break;
        case JOY_BUTTON_17: btn = SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2;
            break;
        case JOY_BUTTON_18: btn = SDL_GAMEPAD_BUTTON_LEFT_PADDLE2;
            break;
        case JOY_BUTTON_19: btn = SDL_GAMEPAD_BUTTON_MISC2;
            break;
        case JOY_BUTTON_20: btn = SDL_GAMEPAD_BUTTON_MISC3;
            break;
        case JOY_BUTTON_21: btn = SDL_GAMEPAD_BUTTON_MISC4;
            break;
        case JOY_BUTTON_22: btn = SDL_GAMEPAD_BUTTON_MISC5;
            break;
        case JOY_BUTTON_23: btn = SDL_GAMEPAD_BUTTON_MISC6;
            break;
        case JOY_BUTTON_24: btn = SDL_GAMEPAD_BUTTON_TOUCHPAD;
            break;
        case JOY_HAT_UP: btn = SDL_GAMEPAD_BUTTON_DPAD_UP;
            break;
        case JOY_HAT_DOWN: btn = SDL_GAMEPAD_BUTTON_DPAD_DOWN;
            break;
        case JOY_HAT_LEFT: btn = SDL_GAMEPAD_BUTTON_DPAD_LEFT;
            break;
        case JOY_HAT_RIGHT: btn = SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
            break;
        case JOY_LEFT:
            axisBtn = SDL_GAMEPAD_AXIS_LEFTX;
            axisPositive = false;
            break;
        case JOY_RIGHT: axisBtn = SDL_GAMEPAD_AXIS_LEFTX;
            break;
        case JOY_UP:
            axisBtn = SDL_GAMEPAD_AXIS_LEFTY;
            axisPositive = false;
            break;
        case JOY_DOWN: axisBtn = SDL_GAMEPAD_AXIS_LEFTY;
            break;
        case JOY_LEFT_2:
            axisBtn = SDL_GAMEPAD_AXIS_RIGHTX;
            axisPositive = false;
            break;
        case JOY_RIGHT_2: axisBtn = SDL_GAMEPAD_AXIS_RIGHTX;
            break;
        case JOY_UP_2:
            axisBtn = SDL_GAMEPAD_AXIS_RIGHTY;
            axisPositive = false;
            break;
        case JOY_DOWN_2: axisBtn = SDL_GAMEPAD_AXIS_RIGHTY;
            break;
        default: break;
        }

        // Handle face buttons
        switch (SDL_GetGamepadButtonLabel(pGamepad, btn))
        {
        case SDL_GAMEPAD_BUTTON_LABEL_A:
            return ssprintf("A %s", deviceString);
        case SDL_GAMEPAD_BUTTON_LABEL_B:
            return ssprintf("B %s", deviceString);
        case SDL_GAMEPAD_BUTTON_LABEL_X:
            return ssprintf("X %s", deviceString);
        case SDL_GAMEPAD_BUTTON_LABEL_Y:
            return ssprintf("Y %s", deviceString);
        case SDL_GAMEPAD_BUTTON_LABEL_CROSS:
            return ssprintf("Cross %s", deviceString);
        case SDL_GAMEPAD_BUTTON_LABEL_SQUARE:
            return ssprintf("Square %s", deviceString);
        case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE:
            return ssprintf("Circle %s", deviceString);
        case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE:
            return ssprintf("Triangle %s", deviceString);
        default: break;
        }

        // Handle buttons (if any) - don't capture face buttons here!
        switch (btn)
        {
        case SDL_GAMEPAD_BUTTON_START:
            if (isNewerMicrosoft)
            {
                return ssprintf("Menu %s", deviceString);
            }
            if (isNewerSony)
            {
                return ssprintf("Options %s", deviceString);
            }
            if (isNintendo)
            {
                return ssprintf("+ %s", deviceString);
            }
            return ssprintf("Start %s", deviceString);
        case SDL_GAMEPAD_BUTTON_BACK:
            if (isNewerMicrosoft)
            {
                return ssprintf("View %s", deviceString);
            }
            if (isOlderSony)
            {
                return ssprintf("Select %s", deviceString);
            }
            if (isNewerSony)
            {
                return ssprintf("Share %s", deviceString);
            }
            if (isNintendo)
            {
                return ssprintf("- %s", deviceString);;
            }
            return ssprintf("Back %s", deviceString);
        case SDL_GAMEPAD_BUTTON_GUIDE:
            if (isOlderSony || isNewerSony)
            {
                return ssprintf("PS button %s", deviceString);
            }
            if (isNintendo)
            {
                return ssprintf("Home %s", deviceString);
            }
            if (isNewerMicrosoft)
            {
                return ssprintf("Xbox button %s", deviceString);
            }
            return ssprintf("Guide %s", deviceString);
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
            if (isOlderSony || isNewerSony)
            {
                return ssprintf("L1 %s", deviceString);
            }
            if (isNintendo)
            {
                return ssprintf("L %s", deviceString);
            }
            return ssprintf("Left Bumper %s", deviceString);
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
            if (isOlderSony || isNewerSony)
            {
                return ssprintf("R1 %s", deviceString);
            }
            if (isNintendo)
            {
                return ssprintf("R %s", deviceString);
            }
            return ssprintf("Right Bumper %s", deviceString);
        case SDL_GAMEPAD_BUTTON_LEFT_STICK:
            if (isOlderSony || isNewerSony)
            {
                return ssprintf("L3 %s", deviceString);
            }
            return ssprintf("Left Stick %s", deviceString);
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
            if (isOlderSony || isNewerSony)
            {
                return ssprintf("R3 %s", deviceString);
            }
            return ssprintf("Right Stick %s", deviceString);
        case SDL_GAMEPAD_BUTTON_LEFT_PADDLE1:
            if (isNewerMicrosoft)
            {
                return ssprintf("P3 %s", deviceString);
            }
            return ssprintf("Left Paddle 1 %s", deviceString);
        case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1:
            if (isNewerMicrosoft)
            {
                return ssprintf("P1 %s", deviceString);
            }
            return ssprintf("Right Paddle 1 %s", deviceString);
        case SDL_GAMEPAD_BUTTON_LEFT_PADDLE2:
            if (isNewerMicrosoft)
            {
                return ssprintf("P4 %s", deviceString);
            }
            return ssprintf("Left Paddle 2 %s", deviceString);
        case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2:
            if (isNewerMicrosoft)
            {
                return ssprintf("P2 %s", deviceString);
            }
            return ssprintf("Right Paddle 2 %s", deviceString);
        case SDL_GAMEPAD_BUTTON_MISC1:
            if (isOlderSony || isNewerSony)
            {
                return ssprintf("Microphone %s", deviceString);
            }
            if (isNintendo)
            {
                return ssprintf("Capture %s", deviceString);
            }
            if (isNewerMicrosoft)
            {
                return ssprintf("Share %s", deviceString);
            }
            return ssprintf("Misc %s", deviceString);
        case SDL_GAMEPAD_BUTTON_MISC2:
            return ssprintf("Misc 2 %s", deviceString);
        case SDL_GAMEPAD_BUTTON_MISC3:
            return ssprintf("Misc 3 %s", deviceString);
        case SDL_GAMEPAD_BUTTON_MISC4:
            return ssprintf("Misc 4 %s", deviceString);
        case SDL_GAMEPAD_BUTTON_MISC5:
            return ssprintf("Misc 5 %s", deviceString);
        case SDL_GAMEPAD_BUTTON_MISC6:
            return ssprintf("Misc 6 %s", deviceString);
        case SDL_GAMEPAD_BUTTON_TOUCHPAD:
            return ssprintf("Touchpad %s", deviceString);
        case SDL_GAMEPAD_BUTTON_DPAD_UP:
            return ssprintf("D-Pad Up %s", deviceString);
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
            return ssprintf("D-Pad Down %s", deviceString);
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
            return ssprintf("D-Pad Left %s", deviceString);
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
            return ssprintf("D-Pad Right %s", deviceString);
        default: break;
        }

        // Handle axis buttons (if any)
        switch (axisBtn)
        {
        case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
            if (isOlderSony || isNewerSony)
            {
                return ssprintf("L2 %s", deviceString);
            }
            if (isNintendo)
            {
                return ssprintf("ZL %s", deviceString);
            }
            return ssprintf("Left Trigger %s", deviceString);
        case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
            if (isOlderSony || isNewerSony)
            {
                return ssprintf("R2 %s", deviceString);
            }
            if (isNintendo)
            {
                return ssprintf("ZR %s", deviceString);
            }
            return ssprintf("Right Trigger %s", deviceString);
        case SDL_GAMEPAD_AXIS_LEFTX:
            if (axisPositive)
            {
                return ssprintf("Left Stick Right %s", deviceString);
            }
            return ssprintf("Left Stick Left %s", deviceString);
        case SDL_GAMEPAD_AXIS_LEFTY:
            if (axisPositive)
            {
                return ssprintf("Left Stick Down %s", deviceString);
            }
            return ssprintf("Left Stick Up %s", deviceString);
        case SDL_GAMEPAD_AXIS_RIGHTX:
            if (axisPositive)
            {
                return ssprintf("Right Stick Right %s", deviceString);
            }
            return ssprintf("Right Stick Down %s", deviceString);
        case SDL_GAMEPAD_AXIS_RIGHTY:
            if (axisPositive)
            {
                return ssprintf("Right Stick Down %s", deviceString);
            }
            return ssprintf("Right Stick Up %s", deviceString);
        default: break;
        }

        // Otherwise return the string
        return ssprintf("%s", SDL_GetGamepadStringForButton(btn));
    }
    return InputHandler::GetDeviceSpecificInputString(di);
}

/**
 * If the devices have changed notify the system.
 * Note: The game will destroy and reinitialize this driver if this ever returns true.
 * @return bool
 */
bool InputHandler_SDL::DevicesChanged()
{
    return m_devicesChanged;
}

/**
 * Initial query for joysticks/gamepads
 */
void InputHandler_SDL::QueryJoysticks()
{
    auto count = 0;
    const auto pJoystickIds = SDL_GetJoysticks(&count);
    for (auto i = 0; i < count; i++)
    {
        if (!ConnectJoystick(pJoystickIds[i]))
        {
            break;
        }
    }
    // Since this function is called at construction, we don't need to notify the game that devices have changed
    m_devicesChanged = false;
}

/**
 * Connect a joystick or gamepad.
 * @param joystickId
 * @return
 */
bool InputHandler_SDL::ConnectJoystick(SDL_JoystickID joystickId)
{
    // Is the device already connected?
    if (m_connectedDevices.count(joystickId) != 0)
    {
        return true;
    }

    // Can the joystick be promoted to a gamepad (easier to handle)
    if (SDL_IsGamepad(joystickId))
    {
        auto pGamepad = SDL_OpenGamepad(joystickId);
        m_pGamepads.push_back(pGamepad);
        LOG->Info("[InputHandler_SDL::ConnectJoystick] Opened Gamepad: %d - %s", joystickId,
                  SDL_GetGamepadNameForID(joystickId));
    }
    else
    {
        auto pJoystick = SDL_OpenJoystick(joystickId);
        m_pJoysticks.push_back(pJoystick);
        LOG->Info("[InputHandler_SDL::ConnectJoystick] Opened Joystick: %d - %s", joystickId,
                  SDL_GetJoystickNameForID(joystickId));
    }

    // Find the first available device
    const auto invalidDeviceIndex = 999;
    int lowestDeviceIndex = invalidDeviceIndex;
    for (auto i = static_cast<int>(DEVICE_JOY1); i < static_cast<int>(DEVICE_JOY32); i++)
    {
        auto result = std::find_if(
            m_connectedDevices.begin(),
            m_connectedDevices.end(),
            [i](const auto& connectedDevice) { return connectedDevice.second == i; });

        if (result == m_connectedDevices.end())
        {
            lowestDeviceIndex = i;
            break;
        }
    }

    if (lowestDeviceIndex != invalidDeviceIndex)
    {
        m_connectedDevices.insert({joystickId, lowestDeviceIndex});
    }
    else
    {
        LOG->Info("[InputHandler_SDL::QueryJoysticks] Cannot connect more than %d devices.",
                  static_cast<int>(m_connectedDevices.size()));
        return false;
    }

    m_devicesChanged = true;
    return true;
}

/**
 * Disconnect a joystick or gamepad
 * @param joystickId
 * @return
 */
bool InputHandler_SDL::DisconnectJoystick(SDL_JoystickID joystickId)
{
    // Is the joystick a gamepad? If so remove it from their list
    if (SDL_IsGamepad(joystickId))
    {
        auto pGamepad = SDL_GetGamepadFromID(joystickId);
        auto gamepadIndex = std::find(m_pGamepads.begin(), m_pGamepads.end(), pGamepad);
        if (gamepadIndex != m_pGamepads.end())
        {
            m_pGamepads.erase(gamepadIndex);

            LOG->Info("[InputHandler_SDL::DisconnectJoystick] Closed Gamepad: %d - %s", joystickId,
                      SDL_GetGamepadNameForID(joystickId));
            SDL_CloseGamepad(pGamepad);
        }
        else
        {
            return false;
        }
    }
    else
    {
        auto pJoystick = SDL_GetJoystickFromID(joystickId);
        auto joystickIndex = std::find(m_pJoysticks.begin(), m_pJoysticks.end(), pJoystick);
        if (joystickIndex != m_pJoysticks.end())
        {
            m_pJoysticks.erase(joystickIndex);

            LOG->Info("[InputHandler_SDL::DisconnectJoystick] Closed Joystick: %d - %s", joystickId,
                      SDL_GetJoystickNameForID(joystickId));
            SDL_CloseJoystick(pJoystick);
        }
        else
        {
            return false;
        }
    }

    m_devicesChanged = true;
    return true;
}

void InputHandler_SDL::RegisterKeyEvent(uint64_t timestamp, bool keyDown, DeviceButton button)
{
    DeviceInput di(
        DEVICE_KEYBOARD,
        button,
        keyDown ? 1.0f : 0.0f
    );

    ButtonPressed(di);
}
