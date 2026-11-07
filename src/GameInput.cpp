#include "GameInput.h"

#include <cstdio>
#include <string>

#include "EnumHelper.h"
#include "InputMapper.h"
#include "ThemeManager.h"

static const char* GameControllerNames[] = {
    "1",
    "2",
};
XToString(GameController);
StringToX(GameController);
LuaXType(GameController);

std::string GameButtonToString(const InputScheme* inputs, GameButton i) {
  return inputs->GetGameButtonName(i);
}

std::string GameButtonToLocalizedString(
    const InputScheme* inputs, GameButton i) {
  return THEME->GetString("GameButton", GameButtonToString(inputs, i));
}

GameButton StringToGameButton(const InputScheme* inputs, const std::string& s) {
  FOREACH_GameButtonInScheme(inputs, i) {
    if (s == GameButtonToString(inputs, i)) {
      return i;
    }
  }
  return GameButton_Invalid;
}

std::string GameInput::ToString(const InputScheme* inputs) const {
  return GameControllerToString(controller_) + std::string("_") +
         GameButtonToString(inputs, button_);
}

bool GameInput::FromString(const InputScheme* inputs, const std::string& s) {
  char controller[32] = "";
  char button[32] = "";

  if (2 != sscanf(s.c_str(), "%31[^_]_%31[^_]", controller, button)) {
    controller_ = GameController_Invalid;
    return false;
  }

  controller_ = StringToGameController(controller);
  button_ = StringToGameButton(inputs, button);
  return true;
};

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
