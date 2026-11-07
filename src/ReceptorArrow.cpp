#include "ReceptorArrow.h"

#include <string>
#include <vector>

#include "ActorFrame.h"
#include "GameConstantsAndTypes.h"
#include "GameInput.h"
#include "GameState.h"
#include "NoteSkinManager.h"
#include "PlayerNumber.h"
#include "PlayerState.h"
#include "RageUtil.h"
#include "Style.h"

ReceptorArrow::ReceptorArrow() {
  is_pressed_ = false;
  was_pressed_ = false;
  was_reverse_ = false;
}

void ReceptorArrow::Load(const PlayerState* player_state, int col_no) {
  player_state_ = player_state;
  col_no_ = col_no;

  const PlayerNumber pn = player_state_->player_number_;
  std::vector<GameInput> game_i;
  GAMESTATE->GetCurrentStyle(pn)->StyleInputToGameInput(col_no, pn, game_i);
  NOTESKIN->SetPlayerNumber(pn);
  // FIXME?  Does this cause a problem when game inputs on different
  // controllers are mapped to the same column?  Such a thing could be set
  // up in a style that uses two controllers and has a mapping that fits the
  // requirements. -Kyz
  NOTESKIN->SetGameController(game_i[0].controller_);

  std::string button = GAMESTATE->GetCurrentStyle(pn)->ColToButtonName(col_no);
  receptor_.Load(NOTESKIN->LoadActor(button, "Receptor"));
  this->AddChild(receptor_);

  bool reverse =
      player_state_->player_options_.GetCurrent().GetReversePercentForColumn(
          col_no_) > 0.5f;
  receptor_->PlayCommand(reverse ? "ReverseOn" : "ReverseOff");
  was_reverse_ = reverse;
}

void ReceptorArrow::Update(float delta_time) {
  ActorFrame::Update(delta_time);

  bool reverse =
      player_state_->player_options_.GetCurrent().GetReversePercentForColumn(
          col_no_) > 0.5f;
  if (reverse != was_reverse_) {
    receptor_->PlayCommand(reverse ? "ReverseOn" : "ReverseOff");
    was_reverse_ = reverse;
  }
}

void ReceptorArrow::DrawPrimitives() {
  if (was_pressed_ && !is_pressed_) {
    receptor_->PlayCommand("Lift");
  } else if (!was_pressed_ && is_pressed_) {
    receptor_->PlayCommand("Press");
  }

  was_pressed_ = is_pressed_;
  is_pressed_ = false;  // it may get turned back on next update

  ActorFrame::DrawPrimitives();
}

void ReceptorArrow::Step(TapNoteScore score) {
  is_pressed_ = true;

  std::string judge = TapNoteScoreToString(score);
  receptor_->PlayCommand(Capitalize(judge));
}

void ReceptorArrow::SetNoteUpcoming(bool b) {
  receptor_->PlayCommand(b ? "ShowNoteUpcoming" : "HideNoteUpcoming");
}

/*
 * (c) 2001-2004 Ben Nordstrom, Chris Danford
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
