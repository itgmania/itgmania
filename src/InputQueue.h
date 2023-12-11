#ifndef INPUT_QUEUE_H
#define INPUT_QUEUE_H

#include <vector>

#include "GameInput.h"
#include "InputEventPlus.h"
#include "InputFilter.h"
#include "RageTimer.h"

// Stores a list of the most recently pressed MenuInputs for each player.
class InputQueue {
 public:
  InputQueue();

  void RememberInput(const InputEventPlus& game_input);
  bool WasPressedRecently(
      GameController game_controller, const GameButton game_button,
      const RageTimer& oldest_time_allowed,
      InputEventPlus* input_event_plus = nullptr);
  const std::vector<InputEventPlus>& GetQueue(
      GameController game_controller) const {
    return queue_[game_controller];
  }
  void ClearQueue(GameController game_controller);

 protected:
  std::vector<InputEventPlus> queue_[NUM_GameController];
};

struct InputQueueCode {
 public:
  bool Load(RString button_names);
  bool EnteredCode(GameController game_controller) const;

  InputQueueCode() : presses_() {}

 private:
  struct ButtonPress {
    ButtonPress()
        : buttons_to_hold_(),
          buttons_to_not_hold_(),
          buttons_to_press_(),
          allow_intermediate_presses_(false) {
      memset(input_types_, 0, sizeof(input_types_));
      input_types_[IET_FIRST_PRESS] = true;
    }
    std::vector<GameButton> buttons_to_hold_;
    std::vector<GameButton> buttons_to_not_hold_;
    std::vector<GameButton> buttons_to_press_;

    bool input_types_[NUM_InputEventType];
    bool allow_intermediate_presses_;
  };
  std::vector<ButtonPress> presses_;

  float max_seconds_back_;
};

// Global and accessible from anywhere in our program.
extern InputQueue* INPUTQUEUE;

#endif  // INPUT_QUEUE_H

/*
 * (c) 2001-2007 Chris Danford, Glenn Maynard
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
