//
//  InputHandler_NSEvent.mm
//  StepMania
//
//  Created by heshuimu on 12/22/19.
//

#include "InputHandler_NSEvent.hpp"
#include "archutils/Darwin/CocoaEventDispatcher.h"
#include "global.h"

#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>

#if OSX_KEYBOARD_USE_NSEVENT
REGISTER_INPUT_HANDLER_CLASS2(NSEvent, NSEvent);
#endif

static float DownOrUp(bool down) { return down ? 1 : 0; }

InputHandler_NSEvent::InputHandler_NSEvent() {
  InitKeyCodeMap();

  auto& dispatcher = CocoaEventDispatcher::sharedDispatcher;
  m_ResponderID = dispatcher.AddResponder(
      std::bind(&InputHandler_NSEvent::HandleEvent, this, std::placeholders::_1));
}

InputHandler_NSEvent::~InputHandler_NSEvent() {
  auto& dispatcher = CocoaEventDispatcher::sharedDispatcher;
  dispatcher.RemoveResponder(m_ResponderID);
}

void InputHandler_NSEvent::InitKeyCodeMap() {
  m_NSKeyCodeMap[kVK_ANSI_A] = KEY_Ca;
  m_NSKeyCodeMap[kVK_ANSI_B] = KEY_Cb;
  m_NSKeyCodeMap[kVK_ANSI_C] = KEY_Cc;
  m_NSKeyCodeMap[kVK_ANSI_D] = KEY_Cd;
  m_NSKeyCodeMap[kVK_ANSI_E] = KEY_Ce;
  m_NSKeyCodeMap[kVK_ANSI_F] = KEY_Cf;
  m_NSKeyCodeMap[kVK_ANSI_G] = KEY_Cg;
  m_NSKeyCodeMap[kVK_ANSI_H] = KEY_Ch;
  m_NSKeyCodeMap[kVK_ANSI_I] = KEY_Ci;
  m_NSKeyCodeMap[kVK_ANSI_J] = KEY_Cj;
  m_NSKeyCodeMap[kVK_ANSI_K] = KEY_Ck;
  m_NSKeyCodeMap[kVK_ANSI_L] = KEY_Cl;
  m_NSKeyCodeMap[kVK_ANSI_M] = KEY_Cm;
  m_NSKeyCodeMap[kVK_ANSI_N] = KEY_Cn;
  m_NSKeyCodeMap[kVK_ANSI_O] = KEY_Co;
  m_NSKeyCodeMap[kVK_ANSI_P] = KEY_Cp;
  m_NSKeyCodeMap[kVK_ANSI_Q] = KEY_Cq;
  m_NSKeyCodeMap[kVK_ANSI_R] = KEY_Cr;
  m_NSKeyCodeMap[kVK_ANSI_S] = KEY_Cs;
  m_NSKeyCodeMap[kVK_ANSI_T] = KEY_Ct;
  m_NSKeyCodeMap[kVK_ANSI_U] = KEY_Cu;
  m_NSKeyCodeMap[kVK_ANSI_V] = KEY_Cv;
  m_NSKeyCodeMap[kVK_ANSI_W] = KEY_Cw;
  m_NSKeyCodeMap[kVK_ANSI_X] = KEY_Cx;
  m_NSKeyCodeMap[kVK_ANSI_Y] = KEY_Cy;
  m_NSKeyCodeMap[kVK_ANSI_Z] = KEY_Cz;

  m_NSKeyCodeMap[kVK_ANSI_0] = KEY_C0;
  m_NSKeyCodeMap[kVK_ANSI_1] = KEY_C1;
  m_NSKeyCodeMap[kVK_ANSI_2] = KEY_C2;
  m_NSKeyCodeMap[kVK_ANSI_3] = KEY_C3;
  m_NSKeyCodeMap[kVK_ANSI_4] = KEY_C4;
  m_NSKeyCodeMap[kVK_ANSI_5] = KEY_C5;
  m_NSKeyCodeMap[kVK_ANSI_6] = KEY_C6;
  m_NSKeyCodeMap[kVK_ANSI_7] = KEY_C7;
  m_NSKeyCodeMap[kVK_ANSI_8] = KEY_C8;
  m_NSKeyCodeMap[kVK_ANSI_9] = KEY_C9;

  m_NSKeyCodeMap[kVK_ANSI_Keypad0] = KEY_KP_C0;
  m_NSKeyCodeMap[kVK_ANSI_Keypad1] = KEY_KP_C1;
  m_NSKeyCodeMap[kVK_ANSI_Keypad2] = KEY_KP_C2;
  m_NSKeyCodeMap[kVK_ANSI_Keypad3] = KEY_KP_C3;
  m_NSKeyCodeMap[kVK_ANSI_Keypad4] = KEY_KP_C4;
  m_NSKeyCodeMap[kVK_ANSI_Keypad5] = KEY_KP_C5;
  m_NSKeyCodeMap[kVK_ANSI_Keypad6] = KEY_KP_C6;
  m_NSKeyCodeMap[kVK_ANSI_Keypad7] = KEY_KP_C7;
  m_NSKeyCodeMap[kVK_ANSI_Keypad8] = KEY_KP_C8;
  m_NSKeyCodeMap[kVK_ANSI_Keypad9] = KEY_KP_C9;

  m_NSKeyCodeMap[kVK_ANSI_KeypadDivide] = KEY_KP_SLASH;
  m_NSKeyCodeMap[kVK_ANSI_KeypadMultiply] = KEY_KP_ASTERISK;
  m_NSKeyCodeMap[kVK_ANSI_KeypadEnter] = KEY_KP_ENTER;
  m_NSKeyCodeMap[kVK_ANSI_KeypadEquals] = KEY_KP_EQUAL;
  m_NSKeyCodeMap[kVK_ANSI_KeypadMinus] = KEY_KP_HYPHEN;
  m_NSKeyCodeMap[kVK_ANSI_KeypadPlus] = KEY_KP_PLUS;
  m_NSKeyCodeMap[kVK_ANSI_KeypadDecimal] = KEY_KP_PERIOD;

  m_NSKeyCodeMap[kVK_Home] = KEY_HOME;
  m_NSKeyCodeMap[kVK_End] = KEY_END;
  m_NSKeyCodeMap[kVK_PageDown] = KEY_PGDN;
  m_NSKeyCodeMap[kVK_PageUp] = KEY_PGUP;

  m_NSKeyCodeMap[kVK_F1] = KEY_F1;
  m_NSKeyCodeMap[kVK_F2] = KEY_F2;
  m_NSKeyCodeMap[kVK_F3] = KEY_F3;
  m_NSKeyCodeMap[kVK_F4] = KEY_F4;
  m_NSKeyCodeMap[kVK_F5] = KEY_F5;
  m_NSKeyCodeMap[kVK_F6] = KEY_F6;
  m_NSKeyCodeMap[kVK_F7] = KEY_F7;
  m_NSKeyCodeMap[kVK_F8] = KEY_F8;
  m_NSKeyCodeMap[kVK_F9] = KEY_F9;
  m_NSKeyCodeMap[kVK_F10] = KEY_F10;
  m_NSKeyCodeMap[kVK_F11] = KEY_F11;
  m_NSKeyCodeMap[kVK_F12] = KEY_F12;
  m_NSKeyCodeMap[kVK_F13] = KEY_F13;
  m_NSKeyCodeMap[kVK_F14] = KEY_F14;
  m_NSKeyCodeMap[kVK_F15] = KEY_F15;
  m_NSKeyCodeMap[kVK_F16] = KEY_F16;

  m_NSKeyCodeMap[kVK_ANSI_Quote] = KEY_QUOTE;
  m_NSKeyCodeMap[kVK_ANSI_Backslash] = KEY_BACKSLASH;
  m_NSKeyCodeMap[kVK_CapsLock] = KEY_CAPSLOCK;
  m_NSKeyCodeMap[kVK_ANSI_Comma] = KEY_COMMA;
  m_NSKeyCodeMap[kVK_ForwardDelete] = KEY_DEL;
  m_NSKeyCodeMap[kVK_Delete] = KEY_BACK;
  m_NSKeyCodeMap[kVK_ANSI_Equal] = KEY_EQUAL;

  m_NSKeyCodeMap[kVK_Escape] = KEY_ESC;
  m_NSKeyCodeMap[kVK_ANSI_LeftBracket] = KEY_LBRACKET;
  m_NSKeyCodeMap[kVK_ANSI_Minus] = KEY_HYPHEN;
  m_NSKeyCodeMap[kVK_ANSI_Period] = KEY_PERIOD;
  m_NSKeyCodeMap[kVK_Return] = KEY_ENTER;
  m_NSKeyCodeMap[kVK_ANSI_RightBracket] = KEY_RBRACKET;
  m_NSKeyCodeMap[kVK_ANSI_Semicolon] = KEY_SEMICOLON;
  m_NSKeyCodeMap[kVK_Space] = KEY_SPACE;
  m_NSKeyCodeMap[kVK_Tab] = KEY_TAB;
  m_NSKeyCodeMap[kVK_ANSI_Grave] = KEY_ACCENT;
  m_NSKeyCodeMap[kVK_ANSI_Slash] = KEY_SLASH;

  m_NSKeyCodeMap[kVK_Command] = KEY_LMETA;
  m_NSKeyCodeMap[kVK_RightCommand] = KEY_RMETA;
  m_NSKeyCodeMap[kVK_Control] = KEY_LCTRL;
  m_NSKeyCodeMap[kVK_RightControl] = KEY_RCTRL;
  m_NSKeyCodeMap[kVK_Function] = KEY_FN;
  m_NSKeyCodeMap[kVK_Option] = KEY_LALT;
  m_NSKeyCodeMap[kVK_RightOption] = KEY_RALT;
  m_NSKeyCodeMap[kVK_Shift] = KEY_LSHIFT;
  m_NSKeyCodeMap[kVK_RightShift] = KEY_RSHIFT;

  m_NSKeyCodeMap[kVK_DownArrow] = KEY_DOWN;
  m_NSKeyCodeMap[kVK_LeftArrow] = KEY_LEFT;
  m_NSKeyCodeMap[kVK_RightArrow] = KEY_RIGHT;
  m_NSKeyCodeMap[kVK_UpArrow] = KEY_UP;
}

void InputHandler_NSEvent::HandleEvent(NSEvent* e) {
  NSEventType type = [e type];
  if (type != NSEventTypeKeyDown && type != NSEventTypeKeyUp && type != NSEventTypeFlagsChanged) {
    return;
  }

  if (type != NSEventTypeFlagsChanged && [e isARepeat]) {
    return;
  }

  uint64_t usecs = uint64_t([e timestamp] * 1000000.0);
  RageTimer eventTime(usecs / 1000000ULL, usecs % 1000000ULL);
  unsigned short keyCode = [e keyCode];
  float zval = (type == NSEventTypeKeyDown ? 1.0 : 0.0);

  switch (type) {
    case NSEventTypeKeyUp:
    case NSEventTypeKeyDown:
      ButtonPressed(DeviceInput(DEVICE_KEYBOARD, m_NSKeyCodeMap[keyCode], zval, eventTime));
      break;

    case NSEventTypeFlagsChanged: {
      NSEventModifierFlags flags = [e modifierFlags];
      switch (keyCode) {
        case kVK_CapsLock:
          ButtonPressed(DeviceInput(
              DEVICE_KEYBOARD, m_NSKeyCodeMap[keyCode],
              DownOrUp(flags & NSEventModifierFlagCapsLock), eventTime));
          break;
        case kVK_Shift:
        case kVK_RightShift:
          ButtonPressed(DeviceInput(
              DEVICE_KEYBOARD, m_NSKeyCodeMap[keyCode], DownOrUp(flags & NSEventModifierFlagShift),
              eventTime));
          break;
        case kVK_Control:
        case kVK_RightControl:
          ButtonPressed(DeviceInput(
              DEVICE_KEYBOARD, m_NSKeyCodeMap[keyCode],
              DownOrUp(flags & NSEventModifierFlagControl), eventTime));
          break;
        case kVK_Option:
        case kVK_RightOption:
          ButtonPressed(DeviceInput(
              DEVICE_KEYBOARD, m_NSKeyCodeMap[keyCode], DownOrUp(flags & NSEventModifierFlagOption),
              eventTime));
          break;
        case kVK_Command:
        case kVK_RightCommand:
          ButtonPressed(DeviceInput(
              DEVICE_KEYBOARD, m_NSKeyCodeMap[keyCode],
              DownOrUp(flags & NSEventModifierFlagCommand), eventTime));
          break;
        case kVK_Function:
          ButtonPressed(DeviceInput(
              DEVICE_KEYBOARD, m_NSKeyCodeMap[keyCode],
              DownOrUp(flags & NSEventModifierFlagFunction), eventTime));
          break;
      }
    } break;

    default:
      break;
  }
}

void InputHandler_NSEvent::GetDevicesAndDescriptions(std::vector<InputDeviceInfo>& vDevicesOut) {
  vDevicesOut.push_back(InputDeviceInfo(DEVICE_KEYBOARD, "NSEventKeyboard"));
}
