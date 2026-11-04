#include "ScreenMiniMenu.h"

#include <string>
#include <vector>

#include "GameState.h"
#include "OptionRowHandler.h"
#include "PlayerNumber.h"
#include "PrefsManager.h"
#include "Screen.h"
#include "ScreenManager.h"
#include "ScreenMessage.h"
#include "ScreenOptions.h"
#include "global.h"

bool ScreenMiniMenu::cancelled_ = false;
int ScreenMiniMenu::last_row_code_ = -1;
std::vector<int> ScreenMiniMenu::last_answers_;

// Settings:
namespace {
const MenuDef* g_pMenuDef = nullptr;
ScreenMessage g_SendOnOK;
ScreenMessage g_SendOnCancel;
};  // namespace

void ScreenMiniMenu::MiniMenu(
    const MenuDef* pDef, ScreenMessage SM_SendOnOK,
    ScreenMessage SM_SendOnCancel, float fX, float fY) {
  g_pMenuDef = pDef;
  g_SendOnOK = SM_SendOnOK;
  g_SendOnCancel = SM_SendOnCancel;

  SCREENMAN->AddNewScreenToTop(pDef->class_name_);
  Screen* pNewScreen = SCREENMAN->GetTopScreen();
  pNewScreen->SetXY(fX, fY);
}

REGISTER_SCREEN_CLASS(ScreenMiniMenu);

void ScreenMiniMenu::Init() {
  if (PREFSMAN->arcade_options_navigation_) {
    SetNavigation(NAV_THREE_KEY_MENU);
  }

  ScreenOptions::Init();
}

void ScreenMiniMenu::BeginScreen() {
  ASSERT(g_pMenuDef != nullptr);

  LoadMenu(g_pMenuDef);
  send_on_ok_ = g_SendOnOK;
  send_on_cancel_ = g_SendOnCancel;
  g_pMenuDef = nullptr;

  ScreenOptions::BeginScreen();

  /* HACK: An OptionRow exits if a screen is set. ScreenMiniMenu is always
   * pushed, so we don't set screens to load. Set a dummy screen, so
   * ScreenOptions::GetNextScreenForSelection will know to move on. */
  next_screen_ = "xxx";
}

void ScreenMiniMenu::LoadMenu(const MenuDef* pDef) {
  menu_rows_ = pDef->rows_;

  last_answers_.resize(menu_rows_.size());
  // Convert from m_vMenuRows to std::vector<OptionRowDefinition>
  std::vector<OptionRowHandler*> vHands;
  for (unsigned r = 0; r < menu_rows_.size(); r++) {
    const MenuRowDef& mr = menu_rows_[r];
    OptionRowHandler* pHand = OptionRowHandlerUtil::MakeSimple(mr);
    vHands.push_back(pHand);
  }

  ScreenOptions::InitMenu(vHands);
}

void ScreenMiniMenu::AfterChangeValueOrRow(PlayerNumber pn) {
  ScreenOptions::AfterChangeValueOrRow(pn);

  std::vector<PlayerNumber> vpns;
  FOREACH_PlayerNumber(p) vpns.push_back(p);
  for (unsigned i = 0; i < rows_.size(); i++) {
    ExportOptions(i, vpns);
  }

  // Changing one option can affect whether other options are available.
  for (unsigned i = 0; i < rows_.size(); i++) {
    const MenuRowDef& mr = menu_rows_[i];
    if (mr.pfn_enabled_) {
      OptionRow& optrow = *rows_[i];
      optrow.GetRowDef().enabled_for_players_.clear();
      if (mr.pfn_enabled_()) {
        optrow.GetRowDef().enabled_for_players_.insert(
            GAMESTATE->GetMasterPlayerNumber());
      }
    }
    rows_[i]->UpdateEnabledDisabled();
  }
}

void ScreenMiniMenu::ImportOptions(
    int r, const std::vector<PlayerNumber>& vpns) {
  OptionRow& optrow = *rows_[r];
  const MenuRowDef& mr = menu_rows_[r];
  if (!mr.choices_.empty()) {
    optrow.SetOneSharedSelection(mr.default_choice_);
  }
}

void ScreenMiniMenu::ExportOptions(
    int r, const std::vector<PlayerNumber>& vpns) {
  PlayerNumber main_player = vpns.empty() ? PLAYER_1 : vpns.front();
  if (r == GetCurrentRow(main_player)) {
    last_row_code_ = menu_rows_[r].row_code_;
  }
  last_answers_.resize(menu_rows_.size());
  last_answers_[r] = rows_[r]->GetOneSharedSelection(true);
}

void ScreenMiniMenu::HandleScreenMessage(const ScreenMessage SM) {
  if (SM == SM_GoToNextScreen) {
    cancelled_ = false;
    SCREENMAN->PopTopScreen(send_on_ok_);
    return;
  } else if (SM == SM_GoToPrevScreen) {
    cancelled_ = true;
    SCREENMAN->PopTopScreen(send_on_cancel_);
    return;
  }

  ScreenOptions::HandleScreenMessage(SM);
}

bool ScreenMiniMenu::FocusedItemEndsScreen(PlayerNumber pn) const {
  return true;
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
