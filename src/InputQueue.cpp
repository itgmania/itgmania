#include "InputQueue.h"

#include <algorithm>
#include <string>
#include <vector>

#include "EnumHelper.h"
#include "GameInput.h"
#include "InputEventPlus.h"
#include "InputFilter.h"
#include "InputMapper.h"
#include "PlayerNumber.h"
#include "RageLog.h"
#include "RageTimer.h"
#include "RageUtil.h"
#include "StdString.h"

InputQueue* INPUTQUEUE =
    nullptr;  // global and accessible from anywhere in our program

const unsigned MAX_INPUT_QUEUE_LENGTH = 32;

InputQueue::InputQueue() {
  FOREACH_ENUM(GameController, gc)
  queue_[gc].resize(MAX_INPUT_QUEUE_LENGTH);
}

void InputQueue::RememberInput(const InputEventPlus& iep) {
  if (!iep.game_i_.IsValid()) {
    return;
  }

  int c = iep.game_i_.controller_;
  if (queue_[c].size() >= MAX_INPUT_QUEUE_LENGTH) {  // full
    queue_[c].erase(
        queue_[c].begin(),
        queue_[c].begin() + (queue_[c].size() - MAX_INPUT_QUEUE_LENGTH + 1));
  }
  queue_[c].push_back(iep);
}

void InputQueue::ClearQueue(GameController c) { queue_[c].clear(); }

static const float g_fSimultaneousThreshold = 0.05f;

bool InputQueueCode::CheckPattern(
    const std::vector<ButtonPress>& presses, GameController controller) const {
  if (presses.size() == 0) {
    return false;
  }

  float max_seconds_back;
  if (presses.size() == 1) {
    max_seconds_back = 0.55f;
  } else {
    max_seconds_back = (presses.size() - 1) * 0.6f;
  }
  RageTimer oldest_time_allowed;
  oldest_time_allowed += -max_seconds_back;

  // iterate newest to oldest
  int sequence_index = presses.size() - 1;  // count down
  const std::vector<InputEventPlus>& queue = INPUTQUEUE->GetQueue(controller);
  int queue_index = queue.size() - 1;
  while (queue_index >= 0) {
    /* If the buttons are too old, stop searching because we're not going to
     * find a match. */
    if (!oldest_time_allowed.IsZero() &&
        queue[queue_index].device_i_.ts_ < oldest_time_allowed) {
      return false;
    }

    /* If the last press is an input type we're not interested in, skip it
     * and look again. */
    const ButtonPress& press = presses[sequence_index];
    if (!press.input_types_[queue[queue_index].type_]) {
      --queue_index;
      continue;
    }

    /* Search backwards for all of Press.m_aButtonsToPress pressed within
     * g_fTapThreshold seconds with m_aButtonsToHold pressed.  Start looking at
     * iQueueIndex. */
    RageTimer oldest_time_allowed_for_tap(queue[queue_index].device_i_.ts_);
    oldest_time_allowed_for_tap += -g_fSimultaneousThreshold;

    bool matched = false;
    int min_search_index_used = queue_index;
    for (int b = 0; b < (int)press.buttons_to_press_.size(); ++b) {
      const InputEventPlus* p_iep = nullptr;
      int queue_search_index = queue_index;
      for (; queue_search_index >= 0;
           --queue_search_index)  // iterate newest to oldest
      {
        const InputEventPlus& iep = queue[queue_search_index];
        if (iep.device_i_.ts_ <
            oldest_time_allowed_for_tap) {  // buttons are too old.  Stop
                                            // searching
                                            // because we're not going to find a
                                            // match
          break;
        }

        if (!press.input_types_[iep.type_]) {
          continue;
        }

        if (iep.game_i_.button_ == press.buttons_to_press_[b]) {
          p_iep = &iep;
          break;
        }
      }
      if (p_iep == nullptr) {
        break;  // didn't find the button
      }

      // Check that m_aButtonsToHold were being held when the buttons were
      // pressed.
      bool all_held_buttons_ok = true;
      for (unsigned i = 0; i < press.buttons_to_hold_.size(); i++) {
        GameInput gi(controller, press.buttons_to_hold_[i]);
        if (!INPUTMAPPER->IsBeingPressed(gi, &p_iep->input_list_)) {
          all_held_buttons_ok = false;
        }
      }
      for (unsigned i = 0; i < press.buttons_to_not_hold_.size(); i++) {
        GameInput gi(controller, press.buttons_to_not_hold_[i]);
        if (INPUTMAPPER->IsBeingPressed(gi, &p_iep->input_list_)) {
          all_held_buttons_ok = false;
        }
      }
      if (!all_held_buttons_ok) {
        continue;
      }
      min_search_index_used =
          std::min(min_search_index_used, queue_search_index);
      if (b == (int)press.buttons_to_press_.size() - 1) {
        matched = true;
      }
    }

    if (!matched) {
      /* The press wasn't matched.  If m_bAllowIntermediatePresses is true,
       * skip the last press, and look again. */
      if (!press.allow_intermediate_presses_) {
        return false;
      }
      --queue_index;
      continue;
    }

    if (sequence_index == 0) {
      return true;
    }

    /* The press was matched. */
    queue_index = min_search_index_used - 1;
    --sequence_index;
  }

  return false;
}

bool InputQueueCode::EnteredCode(GameController controller) const {
  if (controller == GameController_Invalid) {
    return false;
  }

  if (patterns_.empty()) {
    return false;
  }

  for (size_t i = 0; i < patterns_.size(); ++i) {
    if (CheckPattern(patterns_[i], controller)) {
      INPUTQUEUE->ClearQueue(controller);
      return true;
    }
  }
  return false;
}

bool InputQueueCode::ParseCodeString(
    const std::string& alt, std::vector<ButtonPress>& presses) {
  std::vector<std::string> as_presses;
  split(alt, ",", as_presses, false);
  for (std::string& press : as_presses) {
    std::vector<std::string> button_names;
    split(press, "-", button_names, false);

    if (button_names.size() < 1) {
      if (alt != "") {
        LOG->Trace("Ignoring empty code \"%s\".", alt.c_str());
      }
      return false;
    }

    presses.push_back(ButtonPress());
    for (std::string button_name : button_names) {
      bool hold = false;
      bool not_hold = false;
      for (;;) {
        if (Left(button_name, 1) == "+") {
          presses.back().input_types_[IET_REPEAT] = true;
          button_name.erase(0, 1);
        } else if (Left(button_name, 1) == "~") {
          presses.back().input_types_[IET_FIRST_PRESS] = false;
          presses.back().input_types_[IET_RELEASE] = true;
          button_name.erase(0, 1);
        } else if (Left(button_name, 1) == "@") {
          button_name.erase(0, 1);
          hold = true;
        } else if (Left(button_name, 1) == "!") {
          button_name.erase(0, 1);
          not_hold = true;
        } else {
          break;
        }
      }

      const GameButton gb =
          INPUTMAPPER->GetInputScheme()->ButtonNameToIndex(button_name);
      if (gb == GameButton_Invalid) {
        LOG->Trace(
            "The code \"%s\" contains an unrecognized button \"%s\".",
            alt.c_str(), button_name.c_str());
        presses.clear();
        return false;
      }

      if (hold) {
        presses.back().buttons_to_hold_.push_back(gb);
      } else if (not_hold) {
        presses.back().buttons_to_not_hold_.push_back(gb);
      } else {
        presses.back().buttons_to_press_.push_back(gb);
      }
    }
  }
  return true;
}

bool InputQueueCode::Load(std::string buttons_names) {
  patterns_.clear();

  std::vector<std::string> alternatives;
  split(buttons_names, "|", alternatives, true);
  if (alternatives.empty()) {
    return true;
  }

  for (const auto& alt : alternatives) {
    std::vector<ButtonPress> presses;
    if (ParseCodeString(alt, presses) && !presses.empty()) {
      patterns_.push_back(std::move(presses));
    }
  }

  if (patterns_.empty()) {
    return false;
  }

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
