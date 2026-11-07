#include "GhostArrowRow.h"

#include <cstddef>
#include <string>
#include <vector>

#include "ArrowEffects.h"
#include "Game.h"
#include "GameConstantsAndTypes.h"
#include "GameInput.h"
#include "GameState.h"
#include "MessageManager.h"
#include "NoteDisplay.h"
#include "NoteSkinManager.h"
#include "NoteTypes.h"
#include "PlayerNumber.h"
#include "PlayerState.h"
#include "RageUtil.h"
#include "Style.h"
#include "global.h"

void GhostArrowRow::Load(
    const PlayerState* player_state, float y_reverse_offset) {
  player_state_ = player_state;
  y_reverse_offset_pixels_ = y_reverse_offset;

  const PlayerNumber pn = player_state_->player_number_;
  const Style* style = GAMESTATE->GetCurrentStyle(pn);
  NOTESKIN->SetPlayerNumber(pn);

  // init arrows
  for (int c = 0; c < style->cols_per_player_; c++) {
    const std::string& button =
        GAMESTATE->GetCurrentStyle(pn)->ColToButtonName(c);

    std::vector<GameInput> game_i;
    GAMESTATE->GetCurrentStyle(pn)->StyleInputToGameInput(c, pn, game_i);
    NOTESKIN->SetGameController(game_i[0].controller_);

    hold_showing_.push_back(TapNoteSubType_Invalid);
    last_hold_showing_.push_back(TapNoteSubType_Invalid);

    ghost_.push_back(NOTESKIN->LoadActor(button, "Explosion", this));
    ghost_[c]->SetName("GhostArrow");
  }
}

void GhostArrowRow::SetColumnRenderers(
    std::vector<NoteColumnRenderer>& renderers) {
  ASSERT_M(
      renderers.size() == ghost_.size(),
      "Notefield has different number of columns than ghost row.");
  for (size_t c = 0; c < ghost_.size(); ++c) {
    ghost_[c]->SetFakeParent(&(renderers[c]));
  }
  renderers_ = &renderers;
}

GhostArrowRow::~GhostArrowRow() {
  for (unsigned i = 0; i < ghost_.size(); ++i) {
    delete ghost_[i];
  }
}

void GhostArrowRow::Update(float delta_time) {
  for (unsigned c = 0; c < ghost_.size(); c++) {
    ghost_[c]->Update(delta_time);
    (*renderers_)[c].UpdateReceptorGhostStuff(ghost_[c]);
  }

  for (unsigned i = 0; i < hold_showing_.size(); ++i) {
    if (last_hold_showing_[i] != hold_showing_[i]) {
      if (last_hold_showing_[i] == TapNoteSubType_Hold) {
        ghost_[i]->PlayCommand("HoldingOff");
      } else if (last_hold_showing_[i] == TapNoteSubType_Roll) {
        ghost_[i]->PlayCommand("RollOff");
      }
      /*
      else if( m_bLastHoldShowing[i] == TapNoteSubType_Mine )
              m_Ghost[i]->PlayCommand( "MinefieldOff" );
      */

      if (hold_showing_[i] == TapNoteSubType_Hold) {
        ghost_[i]->PlayCommand("HoldingOn");
      } else if (hold_showing_[i] == TapNoteSubType_Roll) {
        ghost_[i]->PlayCommand("RollOn");
      }
      /*
      else if( m_bHoldShowing[i] == TapNoteSubType_Mine )
              m_Ghost[i]->PlayCommand( "MinefieldOn" );
      */
      last_hold_showing_[i] = hold_showing_[i];
    }
    hold_showing_[i] = TapNoteSubType_Invalid;
  }
}

void GhostArrowRow::DrawPrimitives() {
  const Style* style =
      GAMESTATE->GetCurrentStyle(player_state_->player_number_);
  for (unsigned i = 0; i < ghost_.size(); i++) {
    const int c = style->column_draw_order_[i];
    ghost_[c]->Draw();
  }
}

void GhostArrowRow::DidTapNote(int col, TapNoteScore tns, bool bright) {
  ASSERT_M(
      col >= 0 && col < (int)ghost_.size(),
      ssprintf(
          "assert(iCol %i >= 0  && iCol %i < (int)ghost_.size() %i) failed",
          col, col, (int)ghost_.size()));

  Message msg("ColumnJudgment");
  msg.SetParam("TapNoteScore", tns);
  // This may be useful for popn styled judgment :) -DaisuMaster
  msg.SetParam("Column", col);
  if (bright) {
    msg.SetParam("Bright", true);
  }
  ghost_[col]->HandleMessage(msg);

  ghost_[col]->PlayCommand("Judgment");
  if (bright) {
    ghost_[col]->PlayCommand("Bright");
  } else {
    ghost_[col]->PlayCommand("Dim");
  }
  std::string judge = TapNoteScoreToString(tns);
  ghost_[col]->PlayCommand(Capitalize(judge));
}

void GhostArrowRow::DidHoldNote(int col, HoldNoteScore hns, bool bright) {
  ASSERT(col >= 0 && col < (int)ghost_.size());
  Message msg("ColumnJudgment");
  msg.SetParam("HoldNoteScore", hns);
  msg.SetParam("Column", col);
  if (bright) {
    msg.SetParam("Bright", true);
  }
  ghost_[col]->HandleMessage(msg);

  ghost_[col]->PlayCommand("Judgment");
  if (bright) {
    ghost_[col]->PlayCommand("Bright");
  } else {
    ghost_[col]->PlayCommand("Dim");
  }
  std::string judge = HoldNoteScoreToString(hns);
  ghost_[col]->PlayCommand(Capitalize(judge));
}

void GhostArrowRow::SetHoldShowing(int col, const TapNote& tn) {
  ASSERT(col >= 0 && col < (int)ghost_.size());
  hold_showing_[col] = tn.sub_type_;
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
