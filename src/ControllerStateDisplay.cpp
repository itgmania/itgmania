#include "global.h"

#include "ControllerStateDisplay.h"

#include "EnumHelper.h"
#include "InputFilter.h"
#include "InputMapper.h"
#include "LuaBinding.h"
#include "RageInputDevice.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "ThemeManager.h"

static const char* ControllerStateButtonNames[] = {
    "UpLeft", "UpRight", "Center", "DownLeft", "DownRight",
};
XToString(ControllerStateButton);

// TODO: Generalize for all game types
static const GameButton ControllerStateButtonToGameButton[] = {
    PUMP_BUTTON_UPLEFT,   PUMP_BUTTON_UPRIGHT,   PUMP_BUTTON_CENTER,
    PUMP_BUTTON_DOWNLEFT, PUMP_BUTTON_DOWNRIGHT,
};

REGISTER_ACTOR_CLASS(ControllerStateDisplay);

ControllerStateDisplay::ControllerStateDisplay() {
  is_loaded_ = false;
  multiplayer_ = MultiPlayer_Invalid;
  last_input_device_state_ = InputDeviceState_Invalid;
}

void ControllerStateDisplay::LoadMultiPlayer(
    RString type, MultiPlayer multiplayer) {
  LoadInternal(type, multiplayer, GameController_1);
}

void ControllerStateDisplay::LoadGameController(
    RString type, GameController game_controller) {
  LoadInternal(type, MultiPlayer_Invalid, game_controller);
}

void ControllerStateDisplay::LoadInternal(
    RString type, MultiPlayer multiplayer, GameController game_controller) {
  ASSERT(!is_loaded_);
  is_loaded_ = true;
  multiplayer_ = multiplayer;

  LuaThreadVariable varElement(
      "MultiPlayer", LuaReference::Create(multiplayer_));
  smart_pointer_frame_.Load(THEME->GetPathG(type, "frame"));
  this->AddChild(smart_pointer_frame_);

  FOREACH_ENUM(ControllerStateButton, b) {
    Button& button = buttons_[b];

    RString path = THEME->GetPathG(type, ControllerStateButtonToString(b));
    button.smart_pointer_.Load(path);
    this->AddChild(buttons_[b].smart_pointer_);

    button.game_input_ =
        GameInput(game_controller, ControllerStateButtonToGameButton[b]);
  }
}

void ControllerStateDisplay::Update(float fDelta) {
  ActorFrame::Update(fDelta);

  if (multiplayer_ != MultiPlayer_Invalid) {
    InputDevice input_device =
				InputMapper::MultiPlayerToInputDevice(multiplayer_);
    InputDeviceState input_device_state =
				INPUTMAN->GetInputDeviceState(input_device);
    if (input_device_state != last_input_device_state_) {
      PlayCommand(InputDeviceStateToString(input_device_state));
    }
    last_input_device_state_ = input_device_state;
  }

  FOREACH_ENUM(ControllerStateButton, b) {
    Button& button = buttons_[b];
    if (!button.smart_pointer_.IsLoaded()) {
      continue;
    }

    bool visible =
        INPUTMAPPER->IsBeingPressed(button.game_input_, multiplayer_);

    button.smart_pointer_->SetVisible(visible);
  }
}

// Allow Lua to have access to the ControllerStateDisplay.
class LunaControllerStateDisplay : public Luna<ControllerStateDisplay> {
 public:
  static int LoadGameController(T* p, lua_State* L) {
    p->LoadGameController(SArg(1), Enum::Check<GameController>(L, 2));
    COMMON_RETURN_SELF;
  }
  static int LoadMultiPlayer(T* p, lua_State* L) {
    p->LoadMultiPlayer(SArg(1), Enum::Check<MultiPlayer>(L, 2));
    COMMON_RETURN_SELF;
  }

  LunaControllerStateDisplay() {
    ADD_METHOD(LoadGameController);
    ADD_METHOD(LoadMultiPlayer);
  }
};

LUA_REGISTER_DERIVED_CLASS(ControllerStateDisplay, ActorFrame)

/*
 * (c) 2001-2004 Chris Danford
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, provided that the above
 * copyright notice(s) and this permission notice appear in all copies of
 * the Software and that both the above copyright notice(s) and this
 * permission notice appear in supporting documentation.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
 * INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
