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

  auto& dispatcher = CocoaEventDispatcher::shared_dispatcher_;
  responder_id_ = dispatcher.AddResponder(
      std::bind(&InputHandler_NSEvent::HandleEvent, this, std::placeholders::_1));
}

InputHandler_NSEvent::~InputHandler_NSEvent() {
  auto& dispatcher = CocoaEventDispatcher::shared_dispatcher_;
  dispatcher.RemoveResponder(responder_id_);
}

void InputHandler_NSEvent::InitKeyCodeMap() {
  ns_key_code_map_[kVK_ANSI_A] = KEY_Ca;
  ns_key_code_map_[kVK_ANSI_B] = KEY_Cb;
  ns_key_code_map_[kVK_ANSI_C] = KEY_Cc;
  ns_key_code_map_[kVK_ANSI_D] = KEY_Cd;
  ns_key_code_map_[kVK_ANSI_E] = KEY_Ce;
  ns_key_code_map_[kVK_ANSI_F] = KEY_Cf;
  ns_key_code_map_[kVK_ANSI_G] = KEY_Cg;
  ns_key_code_map_[kVK_ANSI_H] = KEY_Ch;
  ns_key_code_map_[kVK_ANSI_I] = KEY_Ci;
  ns_key_code_map_[kVK_ANSI_J] = KEY_Cj;
  ns_key_code_map_[kVK_ANSI_K] = KEY_Ck;
  ns_key_code_map_[kVK_ANSI_L] = KEY_Cl;
  ns_key_code_map_[kVK_ANSI_M] = KEY_Cm;
  ns_key_code_map_[kVK_ANSI_N] = KEY_Cn;
  ns_key_code_map_[kVK_ANSI_O] = KEY_Co;
  ns_key_code_map_[kVK_ANSI_P] = KEY_Cp;
  ns_key_code_map_[kVK_ANSI_Q] = KEY_Cq;
  ns_key_code_map_[kVK_ANSI_R] = KEY_Cr;
  ns_key_code_map_[kVK_ANSI_S] = KEY_Cs;
  ns_key_code_map_[kVK_ANSI_T] = KEY_Ct;
  ns_key_code_map_[kVK_ANSI_U] = KEY_Cu;
  ns_key_code_map_[kVK_ANSI_V] = KEY_Cv;
  ns_key_code_map_[kVK_ANSI_W] = KEY_Cw;
  ns_key_code_map_[kVK_ANSI_X] = KEY_Cx;
  ns_key_code_map_[kVK_ANSI_Y] = KEY_Cy;
  ns_key_code_map_[kVK_ANSI_Z] = KEY_Cz;

  ns_key_code_map_[kVK_ANSI_0] = KEY_C0;
  ns_key_code_map_[kVK_ANSI_1] = KEY_C1;
  ns_key_code_map_[kVK_ANSI_2] = KEY_C2;
  ns_key_code_map_[kVK_ANSI_3] = KEY_C3;
  ns_key_code_map_[kVK_ANSI_4] = KEY_C4;
  ns_key_code_map_[kVK_ANSI_5] = KEY_C5;
  ns_key_code_map_[kVK_ANSI_6] = KEY_C6;
  ns_key_code_map_[kVK_ANSI_7] = KEY_C7;
  ns_key_code_map_[kVK_ANSI_8] = KEY_C8;
  ns_key_code_map_[kVK_ANSI_9] = KEY_C9;

  ns_key_code_map_[kVK_ANSI_Keypad0] = KEY_KP_C0;
  ns_key_code_map_[kVK_ANSI_Keypad1] = KEY_KP_C1;
  ns_key_code_map_[kVK_ANSI_Keypad2] = KEY_KP_C2;
  ns_key_code_map_[kVK_ANSI_Keypad3] = KEY_KP_C3;
  ns_key_code_map_[kVK_ANSI_Keypad4] = KEY_KP_C4;
  ns_key_code_map_[kVK_ANSI_Keypad5] = KEY_KP_C5;
  ns_key_code_map_[kVK_ANSI_Keypad6] = KEY_KP_C6;
  ns_key_code_map_[kVK_ANSI_Keypad7] = KEY_KP_C7;
  ns_key_code_map_[kVK_ANSI_Keypad8] = KEY_KP_C8;
  ns_key_code_map_[kVK_ANSI_Keypad9] = KEY_KP_C9;

  ns_key_code_map_[kVK_ANSI_KeypadDivide] = KEY_KP_SLASH;
  ns_key_code_map_[kVK_ANSI_KeypadMultiply] = KEY_KP_ASTERISK;
  ns_key_code_map_[kVK_ANSI_KeypadEnter] = KEY_KP_ENTER;
  ns_key_code_map_[kVK_ANSI_KeypadEquals] = KEY_KP_EQUAL;
  ns_key_code_map_[kVK_ANSI_KeypadMinus] = KEY_KP_HYPHEN;
  ns_key_code_map_[kVK_ANSI_KeypadPlus] = KEY_KP_PLUS;
  ns_key_code_map_[kVK_ANSI_KeypadDecimal] = KEY_KP_PERIOD;

  ns_key_code_map_[kVK_Home] = KEY_HOME;
  ns_key_code_map_[kVK_End] = KEY_END;
  ns_key_code_map_[kVK_PageDown] = KEY_PGDN;
  ns_key_code_map_[kVK_PageUp] = KEY_PGUP;

  ns_key_code_map_[kVK_F1] = KEY_F1;
  ns_key_code_map_[kVK_F2] = KEY_F2;
  ns_key_code_map_[kVK_F3] = KEY_F3;
  ns_key_code_map_[kVK_F4] = KEY_F4;
  ns_key_code_map_[kVK_F5] = KEY_F5;
  ns_key_code_map_[kVK_F6] = KEY_F6;
  ns_key_code_map_[kVK_F7] = KEY_F7;
  ns_key_code_map_[kVK_F8] = KEY_F8;
  ns_key_code_map_[kVK_F9] = KEY_F9;
  ns_key_code_map_[kVK_F10] = KEY_F10;
  ns_key_code_map_[kVK_F11] = KEY_F11;
  ns_key_code_map_[kVK_F12] = KEY_F12;
  ns_key_code_map_[kVK_F13] = KEY_F13;
  ns_key_code_map_[kVK_F14] = KEY_F14;
  ns_key_code_map_[kVK_F15] = KEY_F15;
  ns_key_code_map_[kVK_F16] = KEY_F16;

  ns_key_code_map_[kVK_ANSI_Quote] = KEY_SQUOTE;
  ns_key_code_map_[kVK_ANSI_Backslash] = KEY_BACKSLASH;
  ns_key_code_map_[kVK_CapsLock] = KEY_CAPSLOCK;
  ns_key_code_map_[kVK_ANSI_Comma] = KEY_COMMA;
  ns_key_code_map_[kVK_ForwardDelete] = KEY_DEL;
  ns_key_code_map_[kVK_Delete] = KEY_BACK;
  ns_key_code_map_[kVK_ANSI_Equal] = KEY_EQUAL;

  ns_key_code_map_[kVK_Escape] = KEY_ESC;
  ns_key_code_map_[kVK_ANSI_LeftBracket] = KEY_LBRACKET;
  ns_key_code_map_[kVK_ANSI_Minus] = KEY_HYPHEN;
  ns_key_code_map_[kVK_ANSI_Period] = KEY_PERIOD;
  ns_key_code_map_[kVK_Return] = KEY_ENTER;
  ns_key_code_map_[kVK_ANSI_RightBracket] = KEY_RBRACKET;
  ns_key_code_map_[kVK_ANSI_Semicolon] = KEY_SEMICOLON;
  ns_key_code_map_[kVK_Space] = KEY_SPACE;
  ns_key_code_map_[kVK_Tab] = KEY_TAB;
  ns_key_code_map_[kVK_ANSI_Grave] = KEY_ACCENT;
  ns_key_code_map_[kVK_ANSI_Slash] = KEY_SLASH;

  ns_key_code_map_[kVK_Command] = KEY_LMETA;
  ns_key_code_map_[kVK_RightCommand] = KEY_RMETA;
  ns_key_code_map_[kVK_Control] = KEY_LCTRL;
  ns_key_code_map_[kVK_RightControl] = KEY_RCTRL;
  ns_key_code_map_[kVK_Function] = KEY_FN;
  ns_key_code_map_[kVK_Option] = KEY_LALT;
  ns_key_code_map_[kVK_RightOption] = KEY_RALT;
  ns_key_code_map_[kVK_Shift] = KEY_LSHIFT;
  ns_key_code_map_[kVK_RightShift] = KEY_RSHIFT;

  ns_key_code_map_[kVK_DownArrow] = KEY_DOWN;
  ns_key_code_map_[kVK_LeftArrow] = KEY_LEFT;
  ns_key_code_map_[kVK_RightArrow] = KEY_RIGHT;
  ns_key_code_map_[kVK_UpArrow] = KEY_UP;
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
  RageTimer event_time(usecs / 1000000ULL, usecs % 1000000ULL);
  unsigned short key_code = [e keyCode];
  float zval = (type == NSEventTypeKeyDown ? 1.0 : 0.0);

  switch (type) {
    case NSEventTypeKeyUp:
    case NSEventTypeKeyDown:
      ButtonPressed(DeviceInput(DEVICE_KEYBOARD, ns_key_code_map_[key_code], zval, event_time));
      break;

    case NSEventTypeFlagsChanged: {
      NSEventModifierFlags flags = [e modifierFlags];
      switch (key_code) {
        case kVK_CapsLock:
          ButtonPressed(DeviceInput(
              DEVICE_KEYBOARD, ns_key_code_map_[key_code],
              DownOrUp(flags & NSEventModifierFlagCapsLock), event_time));
          break;
        case kVK_Shift:
        case kVK_RightShift:
          ButtonPressed(DeviceInput(
              DEVICE_KEYBOARD, ns_key_code_map_[key_code],
              DownOrUp(flags & NSEventModifierFlagShift), event_time));
          break;
        case kVK_Control:
        case kVK_RightControl:
          ButtonPressed(DeviceInput(
              DEVICE_KEYBOARD, ns_key_code_map_[key_code],
              DownOrUp(flags & NSEventModifierFlagControl), event_time));
          break;
        case kVK_Option:
        case kVK_RightOption:
          ButtonPressed(DeviceInput(
              DEVICE_KEYBOARD, ns_key_code_map_[key_code],
              DownOrUp(flags & NSEventModifierFlagOption), event_time));
          break;
        case kVK_Command:
        case kVK_RightCommand:
          ButtonPressed(DeviceInput(
              DEVICE_KEYBOARD, ns_key_code_map_[key_code],
              DownOrUp(flags & NSEventModifierFlagCommand), event_time));
          break;
        case kVK_Function:
          ButtonPressed(DeviceInput(
              DEVICE_KEYBOARD, ns_key_code_map_[key_code],
              DownOrUp(flags & NSEventModifierFlagFunction), event_time));
          break;
      }
    } break;

    default:
      break;
  }
}

void InputHandler_NSEvent::GetDevicesAndDescriptions(std::vector<InputDeviceInfo>& devices_out) {
  devices_out.push_back(InputDeviceInfo(DEVICE_KEYBOARD, "NSEventKeyboard"));
}
