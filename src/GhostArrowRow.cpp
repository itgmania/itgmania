#include "global.h"

#include "GhostArrowRow.h"

#include <cstddef>
#include <vector>

#include "ActorUtil.h"
#include "ArrowEffects.h"
#include "Game.h"
#include "GameConstantsAndTypes.h"
#include "GameState.h"
#include "NoteSkinManager.h"
#include "PlayerState.h"
#include "RageUtil.h"
#include "Style.h"

void GhostArrowRow::Load(
    const PlayerState* player_state, float y_reverse_offset) {
  player_state_ = player_state;
  y_reverse_offset_pixels_ = y_reverse_offset;

  const PlayerNumber pn = player_state_->m_PlayerNumber;
  const Style* style = GAMESTATE->GetCurrentStyle(pn);
  NOTESKIN->SetPlayerNumber(pn);

  // init arrows
  for (int c = 0; c < style->m_iColsPerPlayer; ++c) {
    const RString& button = GAMESTATE->GetCurrentStyle(pn)->ColToButtonName(c);

    std::vector<GameInput> GameI;
    GAMESTATE->GetCurrentStyle(pn)->StyleInputToGameInput(c, pn, GameI);
    NOTESKIN->SetGameController(GameI[0].controller);

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
  for (std::size_t c = 0; c < ghost_.size(); ++c) {
    ghost_[c]->SetFakeParent(&(renderers[c]));
  }
  renderers_ = &renderers;
}

GhostArrowRow::~GhostArrowRow() {
  for (unsigned i = 0; i < ghost_.size(); ++i) {
    delete ghost_[i];
  }
}

void GhostArrowRow::Update(float delta) {
  for (unsigned c = 0; c < ghost_.size(); c++) {
    ghost_[c]->Update(delta);
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
      else if( last_hold_showing_[i] == TapNoteSubType_Mine )
              ghost_[i]->PlayCommand( "MinefieldOff" );
      */

      if (hold_showing_[i] == TapNoteSubType_Hold) {
        ghost_[i]->PlayCommand("HoldingOn");
      } else if (hold_showing_[i] == TapNoteSubType_Roll) {
        ghost_[i]->PlayCommand("RollOn");
      }
      /*
      else if( hold_showing_[i] == TapNoteSubType_Mine )
              ghost_[i]->PlayCommand( "MinefieldOn" );
      */
      last_hold_showing_[i] = hold_showing_[i];
    }
    hold_showing_[i] = TapNoteSubType_Invalid;
  }
}

void GhostArrowRow::DrawPrimitives() {
  const Style* style =
      GAMESTATE->GetCurrentStyle(player_state_->m_PlayerNumber);
  for (unsigned i = 0; i < ghost_.size(); i++) {
    const int c = style->m_iColumnDrawOrder[i];
    ghost_[c]->Draw();
  }
}

void GhostArrowRow::DidTapNote(int col, TapNoteScore tns, bool bright) {
  ASSERT_M(
      col >= 0 && col < (int)ghost_.size(),
      ssprintf(
          "assert(col %i >= 0  && col %i < (int)ghost_.size() %i) failed", col,
          col, (int)ghost_.size()));

  Message msg("ColumnJudgment");
  msg.SetParam("TapNoteScore", tns);
  // NOTE(DaisuMaster): This may be useful for popn styled judgment.
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
  RString judge = TapNoteScoreToString(tns);
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
  RString sJudge = HoldNoteScoreToString(hns);
  ghost_[col]->PlayCommand(Capitalize(sJudge));
}

void GhostArrowRow::SetHoldShowing(int col, const TapNote& tn) {
  ASSERT(col >= 0 && col < (int)ghost_.size());
  hold_showing_[col] = tn.subType;
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
