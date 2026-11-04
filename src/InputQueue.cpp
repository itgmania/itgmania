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

  float fMaxSecondsBack;
  if (presses.size() == 1) {
    fMaxSecondsBack = 0.55f;
  } else {
    fMaxSecondsBack = (presses.size() - 1) * 0.6f;
  }
  RageTimer OldestTimeAllowed;
  OldestTimeAllowed += -fMaxSecondsBack;

  // iterate newest to oldest
  int iSequenceIndex = presses.size() - 1;  // count down
  const std::vector<InputEventPlus>& aQueue = INPUTQUEUE->GetQueue(controller);
  int iQueueIndex = aQueue.size() - 1;
  while (iQueueIndex >= 0) {
    /* If the buttons are too old, stop searching because we're not going to
     * find a match. */
    if (!OldestTimeAllowed.IsZero() &&
        aQueue[iQueueIndex].device_i_.ts_ < OldestTimeAllowed) {
      return false;
    }

    /* If the last press is an input type we're not interested in, skip it
     * and look again. */
    const ButtonPress& Press = presses[iSequenceIndex];
    if (!Press.input_types_[aQueue[iQueueIndex].type_]) {
      --iQueueIndex;
      continue;
    }

    /* Search backwards for all of Press.m_aButtonsToPress pressed within
     * g_fTapThreshold seconds with m_aButtonsToHold pressed.  Start looking at
     * iQueueIndex. */
    RageTimer OldestTimeAllowedForTap(aQueue[iQueueIndex].device_i_.ts_);
    OldestTimeAllowedForTap += -g_fSimultaneousThreshold;

    bool bMatched = false;
    int iMinSearchIndexUsed = iQueueIndex;
    for (int b = 0; b < (int)Press.buttons_to_press_.size(); ++b) {
      const InputEventPlus* pIEP = nullptr;
      int iQueueSearchIndex = iQueueIndex;
      for (; iQueueSearchIndex >= 0;
           --iQueueSearchIndex)  // iterate newest to oldest
      {
        const InputEventPlus& iep = aQueue[iQueueSearchIndex];
        if (iep.device_i_.ts_ <
            OldestTimeAllowedForTap) {  // buttons are too old.  Stop searching
                                        // because we're not going to find a
                                        // match
          break;
        }

        if (!Press.input_types_[iep.type_]) {
          continue;
        }

        if (iep.game_i_.button_ == Press.buttons_to_press_[b]) {
          pIEP = &iep;
          break;
        }
      }
      if (pIEP == nullptr) {
        break;  // didn't find the button
      }

      // Check that m_aButtonsToHold were being held when the buttons were
      // pressed.
      bool bAllHeldButtonsOK = true;
      for (unsigned i = 0; i < Press.buttons_to_hold_.size(); i++) {
        GameInput gi(controller, Press.buttons_to_hold_[i]);
        if (!INPUTMAPPER->IsBeingPressed(gi, &pIEP->input_list_)) {
          bAllHeldButtonsOK = false;
        }
      }
      for (unsigned i = 0; i < Press.buttons_to_not_hold_.size(); i++) {
        GameInput gi(controller, Press.buttons_to_not_hold_[i]);
        if (INPUTMAPPER->IsBeingPressed(gi, &pIEP->input_list_)) {
          bAllHeldButtonsOK = false;
        }
      }
      if (!bAllHeldButtonsOK) {
        continue;
      }
      iMinSearchIndexUsed = std::min(iMinSearchIndexUsed, iQueueSearchIndex);
      if (b == (int)Press.buttons_to_press_.size() - 1) {
        bMatched = true;
      }
    }

    if (!bMatched) {
      /* The press wasn't matched.  If m_bAllowIntermediatePresses is true,
       * skip the last press, and look again. */
      if (!Press.allow_intermediate_presses_) {
        return false;
      }
      --iQueueIndex;
      continue;
    }

    if (iSequenceIndex == 0) {
      return true;
    }

    /* The press was matched. */
    iQueueIndex = iMinSearchIndexUsed - 1;
    --iSequenceIndex;
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
    const std::string& sAlt, std::vector<ButtonPress>& presses) {
  std::vector<std::string> asPresses;
  split(sAlt, ",", asPresses, false);
  for (std::string& sPress : asPresses) {
    std::vector<std::string> asButtonNames;
    split(sPress, "-", asButtonNames, false);

    if (asButtonNames.size() < 1) {
      if (sAlt != "") {
        LOG->Trace("Ignoring empty code \"%s\".", sAlt.c_str());
      }
      return false;
    }

    presses.push_back(ButtonPress());
    for (std::string sButtonName : asButtonNames) {
      bool bHold = false;
      bool bNotHold = false;
      for (;;) {
        if (Left(sButtonName, 1) == "+") {
          presses.back().input_types_[IET_REPEAT] = true;
          sButtonName.erase(0, 1);
        } else if (Left(sButtonName, 1) == "~") {
          presses.back().input_types_[IET_FIRST_PRESS] = false;
          presses.back().input_types_[IET_RELEASE] = true;
          sButtonName.erase(0, 1);
        } else if (Left(sButtonName, 1) == "@") {
          sButtonName.erase(0, 1);
          bHold = true;
        } else if (Left(sButtonName, 1) == "!") {
          sButtonName.erase(0, 1);
          bNotHold = true;
        } else {
          break;
        }
      }

      const GameButton gb =
          INPUTMAPPER->GetInputScheme()->ButtonNameToIndex(sButtonName);
      if (gb == GameButton_Invalid) {
        LOG->Trace(
            "The code \"%s\" contains an unrecognized button \"%s\".",
            sAlt.c_str(), sButtonName.c_str());
        presses.clear();
        return false;
      }

      if (bHold) {
        presses.back().buttons_to_hold_.push_back(gb);
      } else if (bNotHold) {
        presses.back().buttons_to_not_hold_.push_back(gb);
      } else {
        presses.back().buttons_to_press_.push_back(gb);
      }
    }
  }
  return true;
}

bool InputQueueCode::Load(std::string sButtonsNames) {
  patterns_.clear();

  std::vector<std::string> asAlternatives;
  split(sButtonsNames, "|", asAlternatives, true);
  if (asAlternatives.empty()) {
    return true;
  }

  for (const auto& sAlt : asAlternatives) {
    std::vector<ButtonPress> presses;
    if (ParseCodeString(sAlt, presses) && !presses.empty()) {
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
