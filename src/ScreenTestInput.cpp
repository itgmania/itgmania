#include "ScreenTestInput.h"

#include <string>
#include <vector>

#include "ActorUtil.h"
#include "BitmapText.h"
#include "GameInput.h"
#include "InputEventPlus.h"
#include "InputFilter.h"
#include "InputMapper.h"
#include "LocalizedString.h"
#include "MessageManager.h"
#include "PrefsManager.h"
#include "RageInput.h"
#include "RageInputDevice.h"
#include "RageUtil.h"
#include "Screen.h"
#include "ScreenManager.h"
#include "ScreenMessage.h"

class DeviceList : public BitmapText {
 public:
  void Update(float delta_time) {
    // Update devices text
    this->SetText(INPUTMAN->GetDisplayDevicesString());

    BitmapText::Update(delta_time);
  }

  virtual DeviceList* Copy() const;
};

REGISTER_ACTOR_CLASS(DeviceList);

static LocalizedString CONTROLLER("ScreenTestInput", "Controller");
static LocalizedString SECONDARY("ScreenTestInput", "secondary");
static LocalizedString NOT_MAPPED("ScreenTestInput", "not mapped");
class InputList : public BitmapText {
  virtual InputList* Copy() const;

  void Update(float delta_time) {
    // Update input texts
    std::vector<std::string> inputs;

    std::vector<DeviceInput> device_inputs;
    INPUTFILTER->GetPressedButtons(device_inputs);
    for (const DeviceInput& di : device_inputs) {
      if (!di.down_ && di.level_ == 0.0f) {
        continue;
      }

      std::string temp;
      temp += INPUTMAN->GetDeviceSpecificInputString(di);
      if (di.level_ == 1.0f) {
        temp += ssprintf(" - 1 ");
      } else {
        temp += ssprintf(" - %.3f ", di.level_);
      }

      GameInput gi;
      if (INPUTMAPPER->DeviceToGame(di, gi)) {
        std::string name = GameButtonToLocalizedString(
            INPUTMAPPER->GetInputScheme(), gi.button_);
        temp += ssprintf(
            " - %s %d %s", CONTROLLER.GetValue().c_str(), gi.controller_ + 1,
            name.c_str());

        if (!PREFSMAN->only_dedicated_menu_buttons_) {
          GameButton mb =
              INPUTMAPPER->GetInputScheme()->GameButtonToMenuButton(gi.button_);
          if (mb != GameButton_Invalid && mb != gi.button_) {
            std::string game_button_string =
                GameButtonToLocalizedString(INPUTMAPPER->GetInputScheme(), mb);
            temp += ssprintf(
                " - (%s %s)", game_button_string.c_str(),
                SECONDARY.GetValue().c_str());
          }
        }
      } else {
        temp += " - " + NOT_MAPPED.GetValue();
      }

      std::string comment = INPUTFILTER->GetButtonComment(di);
      if (comment != "") {
        temp += " - " + comment;
      }

      inputs.push_back(temp);
    }

    this->SetText(join("\n", inputs));

    BitmapText::Update(delta_time);
  }
};

REGISTER_ACTOR_CLASS(InputList);

REGISTER_SCREEN_CLASS(ScreenTestInput);

bool ScreenTestInput::Input(const InputEventPlus& input) {
  std::string message = input.device_i_.ToString();
  bool handled = false;
  switch (input.type_) {
    case IET_FIRST_PRESS:
    case IET_RELEASE: {
      switch (input.type_) {
        case IET_FIRST_PRESS:
          message += "Pressed";
          break;
        case IET_RELEASE:
          message += "Released";
          break;
        default:
          break;
      }
      MESSAGEMAN->Broadcast(message);
      handled = true;
      break;
    }
    default:
      break;
  }

  return Screen::Input(input) || handled;  // default handler
}

void ScreenTestInput::BeginScreen() {
  INPUTMAN->StartSensorTest();
  ScreenWithMenuElements::BeginScreen();
}

void ScreenTestInput::EndScreen() {
  INPUTMAN->StopSensorTest();
  ScreenWithMenuElements::EndScreen();
}

bool ScreenTestInput::MenuStart(const InputEventPlus& input) {
  return MenuBack(input);
}

bool ScreenTestInput::MenuBack(const InputEventPlus& input) {
  if (input.type_ != IET_REPEAT) {
    return false;  // ignore
  }

  if (IsTransitioning()) {
    return false;
  }
  SCREENMAN->PlayStartSound();
  StartTransitioningScreen(SM_GoToPrevScreen);
  return true;
}

/*
 * (c) 2003-2004 Chris Danford
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
