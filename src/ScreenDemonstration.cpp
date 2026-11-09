#include "ScreenDemonstration.h"

#include <algorithm>
#include <string>
#include <vector>

#include "GameConstantsAndTypes.h"
#include "GameManager.h"
#include "GameState.h"
#include "PlayerNumber.h"
#include "RageUtil.h"
#include "RageUtil/RandomNumbers.h"
#include "Screen.h"
#include "ScreenAttract.h"
#include "ScreenGameplay.h"
#include "ScreenJukebox.h"
#include "ScreenMessage.h"
#include "SongManager.h"
#include "Style.h"
#include "ThemeManager.h"
#include "global.h"

#define SECONDS_TO_SHOW THEME->GetMetricF(name_, "SecondsToShow")
#define ALLOW_STYLE_TYPES THEME->GetMetric(name_, "AllowStyleTypes")

REGISTER_SCREEN_CLASS(ScreenDemonstration);
ScreenDemonstration::ScreenDemonstration() { demonstration_ = true; }

void ScreenDemonstration::Init() {
  GAMESTATE->Reset();
  GAMESTATE->VisitAttractScreen(name_);
  ScreenAttract::SetAttractVolume(true);

  // Choose a Style
  {
    std::vector<std::string> v;
    split(ALLOW_STYLE_TYPES, ",", v);
    std::vector<StyleType> style_type_allow;
    for (const std::string& s : v) {
      StyleType st = StringToStyleType(s);
      ASSERT(st != StyleType_Invalid);
      style_type_allow.push_back(st);
    }

    std::vector<const Style*> style_possible;
    GAMEMAN->GetDemonstrationStylesForGame(
        GAMESTATE->cur_game_, style_possible);
    for (int i = (int)(style_possible.size()) - 1; i >= 0; i--) {
      bool allow_this =
          find(
              style_type_allow.begin(), style_type_allow.end(),
              style_possible[i]->style_type_) != style_type_allow.end();
      if (!allow_this) {
        style_possible.erase(style_possible.begin() + i);
      }
    }

    ASSERT(style_possible.size() > 0);
    const Style* style = style_possible[RandomInt(style_possible.size())];
    GAMESTATE->SetCurrentStyle(style, kPlayerInvalid);
  }

  GAMESTATE->play_mode_.Set(PLAY_MODE_REGULAR);

  ScreenJukebox::Init();

  if (GAMESTATE->cur_song_ == nullptr)  // we didn't find a song.
  {
    PostScreenMessage(SM_GoToNextScreen, 0);  // Abort demonstration.
    return;
  }

  ClearMessageQueue();  // remove all of the messages set in ScreenGameplay that
                        // drive "ready", "go", etc.

  GAMESTATE->gameplay_lead_in_.Set(false);

  dancing_state_ = STATE_DANCING;
  this->PostScreenMessage(
      SM_BeginFadingOut, SECONDS_TO_SHOW);  // TODO: Use MenuTimer instead?
}

void ScreenDemonstration::HandleScreenMessage(const ScreenMessage sm) {
  if (sm == SM_NotesEnded || sm == SM_BeginFadingOut) {
    if (!out_.IsTransitioning()) {
      out_.StartTransitioning(SM_GoToNextScreen);
    }
    return;
  } else if (sm == SM_LoseFocus) {
    ScreenAttract::SetAttractVolume(false);
  }
  ScreenJukebox::HandleScreenMessage(sm);
}

void ScreenDemonstration::Cancel(ScreenMessage send_when_done) {
  ScreenAttract::SetAttractVolume(false);  // unmute attract sounds
  ScreenJukebox::Cancel(send_when_done);
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
