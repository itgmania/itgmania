#include "global.h"

#include "InputQueue.h"

#include <vector>

#include "InputEventPlus.h"
#include "InputMapper.h"
#include "RageLog.h"
#include "RageTimer.h"

// Global and accessible from anywhere in our program.
InputQueue* INPUTQUEUE = nullptr;

const unsigned MAX_INPUT_QUEUE_LENGTH = 32;

InputQueue::InputQueue() {
  FOREACH_ENUM(GameController, game_controller)
  queue_[game_controller].resize(MAX_INPUT_QUEUE_LENGTH);
}

void InputQueue::RememberInput(const InputEventPlus& input_event_plus) {
  if (!input_event_plus.game_input_.IsValid()) {
    return;
  }

  int game_controller = input_event_plus.game_input_.controller;
  if (queue_[game_controller].size() >= MAX_INPUT_QUEUE_LENGTH) {  // full
    queue_[game_controller].erase(
        queue_[game_controller].begin(),
        queue_[game_controller].begin() +
            (queue_[game_controller].size() - MAX_INPUT_QUEUE_LENGTH + 1));
  }
  queue_[game_controller].push_back(input_event_plus);
}

bool InputQueue::WasPressedRecently(
    GameController game_controller, const GameButton game_button,
    const RageTimer& oldest_time_allowed, InputEventPlus* input_event_plus) {
	// Iterate newest to oldest.
  for (int queue_index = queue_[game_controller].size() - 1; queue_index >= 0;
       --queue_index) {
    const InputEventPlus& iep = queue_[game_controller][queue_index];
		// Buttons are too old. Stop searching because we're not going to find a
		// match.
    if (iep.device_input_.ts < oldest_time_allowed) {
      return false;
    }

    if (iep.game_input_.button != game_button) {
      continue;
    }

    if (input_event_plus != nullptr) {
      *input_event_plus = iep;
    }

    return true;
  }

	// Didn't find the button.
  return false;
}

void InputQueue::ClearQueue(GameController game_controller) {
	queue_[game_controller].clear();
}

static const float g_fSimultaneousThreshold = 0.05f;

bool InputQueueCode::EnteredCode(GameController game_controller) const {
  if (game_controller == GameController_Invalid) {
    return false;
  }
  if (presses_.size() == 0) {
    return false;
  }

  RageTimer oldest_time_allowed;
  if (max_seconds_back_ == -1) {
    oldest_time_allowed.SetZero();
  } else {
    oldest_time_allowed += -max_seconds_back_;
  }

  // Iterate newest to oldest.
  int sequence_index = presses_.size() - 1;  // count down
  const std::vector<InputEventPlus>& queue =
			INPUTQUEUE->GetQueue(game_controller);
  int queue_index = queue.size() - 1;
  while (queue_index >= 0) {
    // If the buttons are too old, stop searching because we're not going to
    // find a match.
    if (!oldest_time_allowed.IsZero() &&
        queue[queue_index].device_input_.ts < oldest_time_allowed) {
      return false;
    }

    // If the last press is an input type we're not interested in, skip it and
		// look again.
    const ButtonPress& press = presses_[sequence_index];
    if (!press.input_types_[queue[queue_index].type_]) {
      --queue_index;
      continue;
    }

    // Search backwards for all of press.buttons_to_press_ pressed within
    // g_fTapThreshold seconds with m_aButtonsToHold pressed. Start looking at
    // queue_index.
    RageTimer oldest_time_allowed_for_tap(queue[queue_index].device_input_.ts);
    oldest_time_allowed_for_tap += -g_fSimultaneousThreshold;

    bool matched = false;
    int min_search_index_used = queue_index;
    for (int b = 0; b < (int)press.buttons_to_press_.size(); ++b) {
      const InputEventPlus* input_event_plus = nullptr;
			// Iterate newest to oldest.
			int queue_search_index = queue_index; 
      for (; queue_search_index >= 0; --queue_search_index) {
        const InputEventPlus& iep = queue[queue_search_index];
				// Buttons are too old. Stop searching because we're not going to find a
				// match.
        if (iep.device_input_.ts < oldest_time_allowed_for_tap) {
          break;
        }

        if (!press.input_types_[iep.type_]) {
          continue;
        }

        if (iep.game_input_.button == press.buttons_to_press_[b]) {
          input_event_plus = &iep;
          break;
        }
      }
      if (input_event_plus == nullptr) {
        break;  // didn't find the button
      }

      // Check that buttons_to_hold_ were being held when the buttons were
      // pressed.
      bool all_held_buttons_okay = true;
      for (unsigned i = 0; i < press.buttons_to_hold_.size(); ++i) {
        GameInput game_input(game_controller, press.buttons_to_hold_[i]);
        if (!INPUTMAPPER->IsBeingPressed(
                game_input, MultiPlayer_Invalid, &input_event_plus->input_list_)) {
          all_held_buttons_okay = false;
        }
      }
      for (unsigned i = 0; i < press.buttons_to_not_hold_.size(); ++i) {
        GameInput game_input(game_controller, press.buttons_to_not_hold_[i]);
        if (INPUTMAPPER->IsBeingPressed(
                game_input, MultiPlayer_Invalid, &input_event_plus->input_list_)) {
          all_held_buttons_okay = false;
        }
      }
      if (!all_held_buttons_okay) {
        continue;
      }
      min_search_index_used = std::min(min_search_index_used, queue_search_index);
      if (b == (int)press.buttons_to_press_.size() - 1) {
        matched = true;
      }
    }

    if (!matched) {
      // The press wasn't matched. If allow_intermediate_presses_ is true, skip
			// the last press, and look again.
      if (!press.allow_intermediate_presses_) {
        return false;
      }
      --queue_index;
      continue;
    }

    if (sequence_index == 0) {
      // We matched the whole pattern. Empty the queue so we don't match on it
      // again.
      INPUTQUEUE->ClearQueue(game_controller);
      return true;
    }

    // The press was matched.
    queue_index = min_search_index_used - 1;
    --sequence_index;
  }

  return false;
}

bool InputQueueCode::Load(RString button_names_str) {
  presses_.clear();

  std::vector<RString> presses;
  split(button_names_str, ",", presses, false);
  for (RString& press : presses) {
    std::vector<RString> button_names;

    split(press, "-", button_names, false);

    if (button_names.size() < 1) {
      if (button_names_str != "") {
        LOG->Trace("Ignoring empty code \"%s\".", button_names_str.c_str());
      }
      return false;
    }

    presses_.push_back(ButtonPress());
    for (RString button_name : button_names) {
      bool hold = false;
      bool not_hold = false;
      while (true) {
        if (button_name.Left(1) == "+") {
          presses_.back().input_types_[IET_REPEAT] = true;
          button_name.erase(0, 1);
        } else if (button_name.Left(1) == "~") {
          presses_.back().input_types_[IET_FIRST_PRESS] = false;
          presses_.back().input_types_[IET_RELEASE] = true;
          button_name.erase(0, 1);
        } else if (button_name.Left(1) == "@") {
          button_name.erase(0, 1);
          hold = true;
        } else if (button_name.Left(1) == "!") {
          button_name.erase(0, 1);
          not_hold = true;
        } else {
          break;
        }
      }

      // Search for the corresponding GameButton
      const GameButton game_button =
          INPUTMAPPER->GetInputScheme()->ButtonNameToIndex(button_name);
      if (game_button == GameButton_Invalid) {
        LOG->Trace(
            "The code \"%s\" contains an unrecognized button \"%s\".",
            button_names_str.c_str(), button_name.c_str());
        presses_.clear();
        return false;
      }

      if (hold) {
        presses_.back().buttons_to_hold_.push_back(game_button);
      } else if (not_hold) {
        presses_.back().buttons_to_not_hold_.push_back(game_button);
      } else {
        presses_.back().buttons_to_press_.push_back(game_button);
      }
    }
  }

  if (presses_.size() == 1) {
    max_seconds_back_ = 0.55f;
  } else {
    max_seconds_back_ = (presses_.size() - 1) * 0.6f;
  }

  // If we make it here, we found all the buttons in the code.
  return true;
}

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
