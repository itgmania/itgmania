#include "WheelNotifyIcon.h"

#include <algorithm>
#include <cstdint>

#include "RageUtil.h"
#include "Sprite.h"
#include "ThemeManager.h"
#include "ThemeMetric.h"
#include "WheelNotifyIcon.h"
#include "global.h"

/* todo: replace this entire thing with a set of AutoActors and a Scroller.
 * In reality, everything except the Beginner/Training icon can be replicated
 * in Lua (in stock StepMania 4), so I'm not sure if we even need this... -aj
 */

static ThemeMetric<bool> SHOW_TRAINING("WheelNotifyIcon", "ShowTraining");
static ThemeMetric<bool> BLINK_BEST_ICON("WheelNotifyIcon", "BlinkPlayersBest");
static ThemeMetric<int> NUM_ICONS_TO_SHOW("WheelNotifyIcon", "NumIconsToShow");

WheelNotifyIcon::WheelNotifyIcon() {
  // Load( THEME->GetPathG("MusicWheelItem","WheelNotifyIcon") );
  Load(THEME->GetPathG("WheelNotifyIcon", "icons 4x2"));
  StopAnimating();
}

void WheelNotifyIcon::SetFlags(Flags flags) {
  icons_to_show_.clear();

  // push onto vector in highest to lowest priority

  switch (flags.players_best_number_) {
    case 1:
      icons_to_show_.push_back(best1);
      break;
    case 2:
      icons_to_show_.push_back(best2);
      break;
    case 3:
      icons_to_show_.push_back(best3);
      break;
  }

  if (flags.edits_) {
    icons_to_show_.push_back(edits);
  }

  switch (flags.stages_for_song_) {
    case 1:
      break;
    case 2:
      icons_to_show_.push_back(long_ver);
      break;
    case 3:
      icons_to_show_.push_back(marathon);
      break;
    default:
      FAIL_M(ssprintf("flags.stages_for_song_ = %d", flags.stages_for_song_));
  }

  if (flags.has_beginner_or1_meter_ && (bool)SHOW_TRAINING) {
    icons_to_show_.push_back(training);
  }

  // If BLINK_BEST_ICON, make player's best icon blink if it's the only icon.
  if (icons_to_show_.size() == 1 && BLINK_BEST_ICON) {
    if (icons_to_show_[0] >= best1 && icons_to_show_[0] <= best3) {
      icons_to_show_.push_back(empty);
    }
  }

  const unsigned int newSize = std::min<unsigned int>(
      icons_to_show_.size(), static_cast<unsigned int>(NUM_ICONS_TO_SHOW));
  icons_to_show_.resize(newSize);

  // Broadcast Set message so items can react. (futures) -aj
  // Message msg("Set");
  // this->HandleMessage( msg );

  /* Make sure the right icon is selected, since we might be drawn before
   * we get another update. */
  Update(0);
}

bool WheelNotifyIcon::EarlyAbortDraw() const {
  if (icons_to_show_.empty()) {
    return true;
  }
  return Sprite::EarlyAbortDraw();
}

void WheelNotifyIcon::Update(float fDeltaTime) {
  if (icons_to_show_.size() > 0) {
    /* We should probably end up parsing the vector and then dynamically
     * insert flag icons based on "priority". Easy to do, hopefully
            - Midiman */
    static uint_fast32_t updateCounter = 0;
    updateCounter++;
    const int index = updateCounter % icons_to_show_.size();
    Sprite::SetState(icons_to_show_[index]);
  }

  Sprite::Update(fDeltaTime);
}

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
