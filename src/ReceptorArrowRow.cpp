#include "ReceptorArrowRow.h"

#include <cstddef>
#include <vector>

#include "ActorFrame.h"
#include "ArrowEffects.h"
#include "GameConstantsAndTypes.h"
#include "GameState.h"
#include "NoteDisplay.h"
#include "PlayerState.h"
#include "RageUtil.h"
#include "ReceptorArrow.h"
#include "Style.h"
#include "global.h"

ReceptorArrowRow::ReceptorArrowRow() {
  player_state_ = nullptr;
  y_reverse_offset_pixels_ = 0;
  fade_to_fail_percent_ = 0;
  renderers_ = nullptr;
}

void ReceptorArrowRow::Load(
    const PlayerState* pPlayerState, float fYReverseOffset) {
  player_state_ = pPlayerState;
  y_reverse_offset_pixels_ = fYReverseOffset;

  const Style* pStyle =
      GAMESTATE->GetCurrentStyle(pPlayerState->player_number_);

  for (int c = 0; c < pStyle->cols_per_player_; c++) {
    receptor_arrow_.push_back(new ReceptorArrow);
    receptor_arrow_[c]->SetName("ReceptorArrow");
    receptor_arrow_[c]->Load(player_state_, c);
    this->AddChild(receptor_arrow_[c]);
  }
}

void ReceptorArrowRow::SetColumnRenderers(
    std::vector<NoteColumnRenderer>& renderers) {
  ASSERT_M(
      renderers.size() == receptor_arrow_.size(),
      "Notefield has different number of columns than receptor row.");
  for (size_t c = 0; c < receptor_arrow_.size(); ++c) {
    receptor_arrow_[c]->SetFakeParent(&(renderers[c]));
  }
  renderers_ = &renderers;
}

ReceptorArrowRow::~ReceptorArrowRow() {
  for (unsigned i = 0; i < receptor_arrow_.size(); ++i) {
    delete receptor_arrow_[i];
  }
}

void ReceptorArrowRow::Update(float fDeltaTime) {
  ActorFrame::Update(fDeltaTime);
  // If we're on gameplay, then the notefield will take care of updating
  // ArrowEffects.  But if we're on ScreenNameEntry, there is no notefield,
  // Checking whether m_renderers is null is a proxy for checking whether
  // there is a notefield. -Kyz
  if (renderers_ == nullptr) {
    ArrowEffects::Update();
  }

  for (unsigned c = 0; c < receptor_arrow_.size(); c++) {
    // m_fDark==1 or m_fFadeToFailPercent==1 should make fBaseAlpha==0
    float fBaseAlpha =
        (1 - player_state_->player_options_.GetCurrent().dark_ -
         player_state_->player_options_.GetCurrent().darks_[c]);
    if (fade_to_fail_percent_ != -1) {
      fBaseAlpha *= (1 - fade_to_fail_percent_);
    }
    rage_clamp(fBaseAlpha, 0.0f, 1.0f);
    receptor_arrow_[c]->SetBaseAlpha(fBaseAlpha);

    if (renderers_ != nullptr) {
      // set arrow XYZ
      (*renderers_)[c].UpdateReceptorGhostStuff(receptor_arrow_[c]);
    } else {
      // ScreenNameEntry uses ReceptorArrowRow but doesn't have or need
      // column renderers.  Just do the lazy thing and offset x. -Kyz
      const Style* style =
          GAMESTATE->GetCurrentStyle(player_state_->player_number_);
      receptor_arrow_[c]->SetX(
          style->column_info_[player_state_->player_number_][c].x_offset_);
    }
  }
}

void ReceptorArrowRow::DrawPrimitives() {
  const Style* pStyle =
      GAMESTATE->GetCurrentStyle(player_state_->player_number_);
  for (unsigned i = 0; i < receptor_arrow_.size(); i++) {
    const int c = pStyle->column_draw_order_[i];
    receptor_arrow_[c]->Draw();
  }
}

void ReceptorArrowRow::Step(int iCol, TapNoteScore score) {
  ASSERT(iCol >= 0 && iCol < (int)receptor_arrow_.size());
  receptor_arrow_[iCol]->Step(score);
}

void ReceptorArrowRow::SetPressed(int iCol) {
  ASSERT(iCol >= 0 && iCol < (int)receptor_arrow_.size());
  receptor_arrow_[iCol]->SetPressed();
}

void ReceptorArrowRow::SetNoteUpcoming(int iCol, bool b) {
  ASSERT(iCol >= 0 && iCol < (int)receptor_arrow_.size());
  receptor_arrow_[iCol]->SetNoteUpcoming(b);
}

/*
 * (c) 2001-2003 Chris Danford
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
