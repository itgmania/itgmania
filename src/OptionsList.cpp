#include "OptionsList.h"

#include <algorithm>
#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "Actor.h"
#include "ActorUtil.h"
#include "CodeDetector.h"
#include "Command.h"
#include "GameCommand.h"
#include "GameInput.h"
#include "GameState.h"
#include "InputEventPlus.h"
#include "InputFilter.h"
#include "InputMapper.h"
#include "LuaManager.h"
#include "MessageManager.h"
#include "ModsGroup.h"
#include "OptionRowHandler.h"
#include "PlayerNumber.h"
#include "PlayerState.h"
#include "RageUtil.h"
#include "ThemeManager.h"
#include "global.h"

#define LINE(sLineName) \
  THEME->GetMetric(name_, ssprintf("Line%s", sLineName.c_str()))
#define MAX_ITEMS_BEFORE_SPLIT THEME->GetMetricI(name_, "MaxItemsBeforeSplit")
#define ITEMS_SPLIT_WIDTH THEME->GetMetricF(name_, "ItemsSplitWidth")
#define DIRECT_LINES THEME->GetMetric(name_, "DirectLines")
#define TOP_MENUS THEME->GetMetric(name_, "TopMenus")

static const std::string RESET_ROW = "ResetOptions";

void OptionListRow::Load(OptionsList* pOptions, const std::string& sType) {
  options_ = pOptions;
  ITEMS_SPACING_Y.Load(sType, "ItemsSpacingY");

  text_.resize(1);
  text_[0].SetName("Text");
  text_[0].LoadFromFont(THEME->GetPathF(sType, "normal"));
  ActorUtil::LoadAllCommands(text_[0], sType);

  underlines_.resize(1);
  underlines_[0].Load(THEME->GetPathG(sType, "underline"));
  underlines_[0]->SetName("Underline");
  ActorUtil::LoadAllCommands(*underlines_[0], sType);

  text_[0].PlayCommand("On");
  underlines_[0]->PlayCommand("On");
}

// GetTitleForHandler
// can always use the title
// can have special speed row titles, note skin, or any SELECT_ONE

void OptionListRow::SetFromHandler(const OptionRowHandler* pHandler) {
  this->FinishTweening();
  this->RemoveAllChildren();

  if (pHandler == nullptr) {
    return;
  }

  unsigned int iNum =
      std::max(pHandler->def_.choices_.size(), text_.size()) + 1;
  text_.resize(iNum, text_[0]);
  underlines_.resize(iNum, underlines_[0]);

  for (unsigned i = 0; i < pHandler->def_.choices_.size(); ++i) {
    // init underlines
    this->AddChild(underlines_[i]);

    // init text
    this->AddChild(&text_[i]);
  }

  SetTextFromHandler(pHandler);

  const unsigned iCnt = pHandler->def_.choices_.size();
  items_in_two_rows_ = (int)iCnt > MAX_ITEMS_BEFORE_SPLIT;
  const float fWidth = ITEMS_SPLIT_WIDTH;
  float fY = 0;
  for (unsigned i = 0; i < iCnt; ++i) {
    float fX = 0;
    if (items_in_two_rows_) {
      if ((i % 2) == 0) {
        fX = -fWidth / 2;
      } else {
        fX = +fWidth / 2;
      }
    }

    // set the Y position of each item in the line
    text_[i].SetXY(fX, fY);
    underlines_[i]->SetXY(fX, fY);

    if (items_in_two_rows_) {
      underlines_[i]->PlayCommand("SetTwoRows");
    } else {
      underlines_[i]->PlayCommand("SetOneRow");
    }

    if (!items_in_two_rows_ || (i % 2) == 1 || i + 1 == iCnt) {
      fY += ITEMS_SPACING_Y;
    }
  }

  int iExit = pHandler->def_.choices_.size();
  text_[iExit].SetText("Exit");  // XXX localize
  text_[iExit].SetXY(0, fY);
  this->AddChild(&text_[iExit]);
}

void OptionListRow::SetTextFromHandler(const OptionRowHandler* pHandler) {
  ASSERT(pHandler != nullptr);
  for (unsigned i = 0; i < pHandler->def_.choices_.size(); ++i) {
    // init text
    std::string sText = pHandler->GetThemedItemText(i);

    std::string sDest = pHandler->GetScreen(i);
    if (options_->set_direct_rows_.find(sDest) !=
            options_->set_direct_rows_.end() &&
        sDest.size()) {
      const OptionRowHandler* pTarget = options_->rows_[sDest];
      if (pTarget->def_.select_type_ == SELECT_ONE) {
        int iSelection = options_->GetOneSelection(sDest);
        sText += ": " + pTarget->GetThemedItemText(iSelection);
      }
    }

    text_[i].SetText(sText);
  }
}

void OptionListRow::SetUnderlines(
    const std::vector<bool>& aSelections, const OptionRowHandler* pHandler) {
  for (unsigned i = 0; i < aSelections.size(); ++i) {
    Actor* pActor = underlines_[i];

    bool bSelected = aSelections[i];
    std::string sDest = pHandler->GetScreen(i);
    if (sDest.size()) {
      /* This is a submenu.  Underline the row if its options have been changed
       * from the default. */
      const OptionRowHandler* pTarget = options_->rows_[sDest];
      if (pTarget->def_.select_type_ == SELECT_ONE) {
        int iSelection = options_->GetOneSelection(sDest);
        const OptionRowHandler* lHandler = options_->rows_.find(sDest)->second;
        int iDefault = lHandler->GetDefaultOption();
        if (iDefault != -1 && iSelection != iDefault) {
          bSelected |= true;
        }
      } else if (pTarget->def_.select_type_ == SELECT_MULTIPLE) {
        const std::vector<bool>& bTargetSelections =
            options_->selections_.find(sDest)->second;
        for (unsigned j = 0; j < bTargetSelections.size(); j++) {
          if (bTargetSelections[j]) {
            bSelected = true;
          }
        }
      }
    }

    pActor->PlayCommand(bSelected ? "Show" : "Hide");
  }
}

void OptionListRow::PositionCursor(Actor* pCursor, int iSelection) {
  float fX = text_[iSelection].GetDestX();
  float fY = text_[iSelection].GetDestY();
  if (items_in_two_rows_) {
    pCursor->PlayCommand("PositionTwoRows");
  } else {
    pCursor->PlayCommand("PositionOneRow");
  }
  pCursor->SetXY(fX, fY);
}

OptionsList::OptionsList() {
  current_row_ = 0;
  linked_ = nullptr;
}

OptionsList::~OptionsList() {
  for (std::pair<const std::string&, OptionRowHandler*> hand : rows_) {
    delete hand.second;
  }
}

// This is the initialization function.
void OptionsList::Load(std::string sType, PlayerNumber pn) {
  TOP_MENU.Load(sType, "TopMenu");

  pn_ = pn;
  start_is_down_ = false;
  game_button_previous_item_ = INPUTMAPPER->GetInputScheme()->ButtonNameToIndex(
      THEME->GetMetric(name_, "PrevItemButton"));
  game_button_next_item_ = INPUTMAPPER->GetInputScheme()->ButtonNameToIndex(
      THEME->GetMetric(name_, "NextItemButton"));

  codes_.Load(sType);

  cursor_.Load(THEME->GetPathG(sType, "cursor"));
  cursor_->SetName("Cursor");
  ActorUtil::LoadAllCommands(*cursor_, sType);
  this->AddChild(cursor_);

  std::vector<std::string> asDirectLines;
  split(DIRECT_LINES, ",", asDirectLines, true);
  for (std::string& s : asDirectLines) {
    set_direct_rows_.insert(s);
  }

  std::vector<std::string> setToLoad;
  split(TOP_MENUS, ",", setToLoad);
  set_top_menus_.insert(setToLoad.begin(), setToLoad.end());

  while (!setToLoad.empty()) {
    std::string sLineName = *setToLoad.begin();
    setToLoad.erase(setToLoad.begin());

    if (rows_.find(sLineName) != rows_.end()) {
      continue;
    }

    std::string sRowCommands = LINE(sLineName);
    Commands cmds;
    ParseCommands(sRowCommands, cmds, false);

    OptionRowHandler* pHand = OptionRowHandlerUtil::Make(cmds);
    if (pHand == nullptr) {
      LuaHelpers::ReportScriptErrorFmt(
          "Invalid OptionRowHandler '%s' in %s::Line%s",
          cmds.GetOriginalCommandString().c_str(), name_.c_str(),
          sLineName.c_str());
      continue;
    }

    rows_[sLineName] = pHand;

    for (size_t i = 0; i < pHand->def_.choices_.size(); ++i) {
      std::string sScreen = pHand->GetScreen(i);
      if (!sScreen.empty()) {
        setToLoad.push_back(sScreen);
      }
    }
  }

  for (int i = 0; i < 2; ++i) {
    row_[i].SetName("OptionsList");
    row_[i].Load(this, "OptionsList");
    ActorUtil::LoadAllCommands(row_[i], sType);
    this->AddChild(&row_[i]);
  }

  this->PlayCommand("TweenOff");
  this->FinishTweening();
}

void OptionsList::Reset() {
  /* Import options. */
  for (std::pair<const std::string&, OptionRowHandler*> hand : rows_) {
    ImportRow(hand.first);
  }
}

void OptionsList::Open() {
  this->PlayCommand("Reset");

  Message msg("OptionsListOpened");
  msg.SetParam("Player", pn_);
  MESSAGEMAN->Broadcast(msg);

  /* Push the initial menu. */
  ASSERT(menu_stack_.size() == 0);
  Push(TOP_MENU);

  this->FinishTweening();
  row_[!current_row_].SetFromHandler(nullptr);
  this->PlayCommand("TweenOn");
}

void OptionsList::Close() {
  Message msg("OptionsListClosed");
  msg.SetParam("Player", pn_);
  MESSAGEMAN->Broadcast(msg);

  start_is_down_ = false;
  menu_stack_.clear();
  this->PlayCommand("TweenOff");
}

std::string OptionsList::GetCurrentRow() const {
  ASSERT(!menu_stack_.empty());  // called while the menu was closed
  return menu_stack_.back();
}

// This can't be const because OptionRowHandler->NotifyOfSelection() will modify
// the OptionRow.
OptionRowHandler* OptionsList::GetCurrentHandler() {
  std::string sCurrentRow = GetCurrentRow();
  return rows_[sCurrentRow];
}

int OptionsList::GetOneSelection(std::string sRow, bool bAllowFail) const {
  std::map<std::string, std::vector<bool> >::const_iterator it =
      selections_.find(sRow);
  ASSERT_M(it != selections_.end(), sRow);
  const std::vector<bool>& bSelections = it->second;
  for (unsigned i = 0; i < bSelections.size(); i++) {
    if (bSelections[i]) {
      return i;
    }
  }

  ASSERT(
      bAllowFail);  // shouldn't call this if not expecting one to be selected
  return -1;
}

void OptionsList::PositionCursor() {
  row_[current_row_].PositionCursor(cursor_, menu_stack_selection_);
}

/* Toggle to the next menu.  This is used to switch quickly through option
 * submenus, to choose many options or to find the one you're looking for.  For
 * that goal, it's not helpful to switch only through the options listed in the
 * current parent menu; always toggle through the whole set.  Skip menus that
 * only contain other menus. */
void OptionsList::SwitchMenu(int iDir) {
  /* Consider the menu as a list, where the main menu is the first item and the
   * submenus follow.  This allows consistent navigation; moving right from the
   * main menu walks through the menus, moving left goes back as far as the main
   * menu.  Don't loop, so it's harder to lose track of menus. */
  std::string sTopRow = menu_stack_.front();
  const OptionRowHandler* pHandler = rows_[sTopRow];
  int iCurrentRow = 0;
  if (menu_stack_.size() == 1) {
    iCurrentRow = -1;
  } else {
    iCurrentRow = FindScreenInHandler(pHandler, menu_stack_.back());
  }

  iCurrentRow += iDir;
  if (iCurrentRow >= 0) {
    if (iCurrentRow >= (int)pHandler->def_.choices_.size()) {
      return;
    }
    std::string sDest = pHandler->GetScreen(iCurrentRow);
    if (sDest.empty()) {
      return;
    }

    if (menu_stack_.size() == 1) {
      menu_stack_.push_back(sDest);
    } else {
      menu_stack_.back() = sDest;
    }
  } else {
    if (menu_stack_.size() == 1) {
      return;
    }

    menu_stack_.pop_back();
  }

  SetDefaultCurrentRow();
  SwitchToCurrentRow();
  TweenOnCurrentRow(iDir > 0);
}

bool OptionsList::Input(const InputEventPlus& input) {
  Message msg("");
  if (codes_.InputMessage(input, msg)) {
    this->HandleMessage(msg);
  }

  const OptionRowHandler* pHandler = GetCurrentHandler();

  PlayerNumber pn = input.pn_;
  if (start_is_down_) {
    if (input.menu_i_ == GAME_BUTTON_LEFT ||
        input.menu_i_ == GAME_BUTTON_RIGHT) {
      if (input.type_ != IET_FIRST_PRESS) {
        return false;
      }

      accept_start_release_ = false;

      const std::string& sCurrentRow = menu_stack_.back();
      std::vector<bool>& bSelections = selections_[sCurrentRow];
      if (menu_stack_selection_ == (int)bSelections.size()) {
        return false;
      }

      std::string sDest = pHandler->GetScreen(menu_stack_selection_);
      if (set_direct_rows_.find(sDest) != set_direct_rows_.end() &&
          sDest.size()) {
        const OptionRowHandler* pTarget = rows_[sDest];
        std::vector<bool>& bTargetSelections = selections_[sDest];

        if (pTarget->def_.select_type_ == SELECT_ONE) {
          int iSelection = GetOneSelection(sDest);
          int iDir = (input.menu_i_ == GAME_BUTTON_RIGHT ? +1 : -1);
          iSelection += iDir;
          wrap(iSelection, bTargetSelections.size());
          SelectItem(sDest, iSelection);

          Message lMsg("OptionsListQuickChange");
          lMsg.SetParam("Player", pn);
          lMsg.SetParam("Direction", iDir);
          lMsg.SetParam("Selection", iSelection);
          MESSAGEMAN->Broadcast(lMsg);
        }
      }
      return true;
    }
  }

  if (input.menu_i_ == game_button_previous_item_) {
    if (input.type_ == IET_RELEASE) {
      return false;
    }

    --menu_stack_selection_;
    wrap(
        menu_stack_selection_,
        pHandler->def_.choices_.size() + 1);  // +1 for exit row
    PositionCursor();

    Message lMsg("OptionsListLeft");
    lMsg.SetParam("Player", input.pn_);
    lMsg.SetParam("Selection", menu_stack_selection_);
    MESSAGEMAN->Broadcast(lMsg);
    return true;
  } else if (input.menu_i_ == game_button_next_item_) {
    if (input.type_ == IET_RELEASE) {
      return false;
    }

    ++menu_stack_selection_;
    wrap(
        menu_stack_selection_,
        pHandler->def_.choices_.size() + 1);  // +1 for exit row
    PositionCursor();

    Message lMsg("OptionsListRight");
    lMsg.SetParam("Player", input.pn_);
    lMsg.SetParam("Selection", menu_stack_selection_);
    MESSAGEMAN->Broadcast(lMsg);
    return true;
  } else if (CodeDetector::EnteredPrevOpList(input.game_i_.controller_)) {
    if (input.type_ == IET_FIRST_PRESS) {
      SwitchMenu(-1);
    }
    return true;
  } else if (CodeDetector::EnteredNextOpList(input.game_i_.controller_)) {
    if (input.type_ == IET_FIRST_PRESS) {
      SwitchMenu(+1);
    }
    return true;
  } else if (input.menu_i_ == GAME_BUTTON_START) {
    if (input.type_ == IET_FIRST_PRESS) {
      start_is_down_ = true;
      accept_start_release_ = true;
      return true;
    }
    if (input.type_ == IET_RELEASE) {
      if (accept_start_release_) {
        Start();
      }
      start_is_down_ = false;
    }

    return true;
  } else if (input.menu_i_ == GAME_BUTTON_SELECT) {
    if (input.type_ != IET_FIRST_PRESS) {
      return false;
    }
    //		if( input.type == IET_RELEASE )
    {
      Close();
      return true;
    }
    return true;
  }
  return false;
}

void OptionsList::SwitchToCurrentRow() {
  current_row_ = !current_row_;

  /* Set up the new row. */
  row_[current_row_].SetFromHandler(GetCurrentHandler());
  row_[current_row_].SetUnderlines(
      selections_[menu_stack_.back()], GetCurrentHandler());
  PositionCursor();

  Message msg("OptionsMenuChanged");
  msg.SetParam("Player", pn_);
  msg.SetParam("Menu", menu_stack_.back());
  MESSAGEMAN->Broadcast(msg);
}

/* After setting up a new row, tween it on. */
void OptionsList::TweenOnCurrentRow(bool bForward) {
  OptionListRow& OldRow = row_[!current_row_];
  OptionListRow& NewRow = row_[current_row_];

  /* Tween out the old row. */
  if (bForward) {
    OldRow.PlayCommand("TweenOutForward");
  } else {
    OldRow.PlayCommand("TweenOutBackward");
  }

  /* Tween in the old row. */
  if (bForward) {
    NewRow.PlayCommand("TweenInForward");
  } else {
    NewRow.PlayCommand("TweenInBackward");
  }
}

void OptionsList::ImportRow(std::string sRow) {
  std::vector<bool> aSelections[NUM_PLAYERS];
  std::vector<PlayerNumber> vpns;
  vpns.push_back(pn_);
  OptionRowHandler* pHandler = rows_[sRow];
  aSelections[pn_].resize(pHandler->def_.choices_.size());
  pHandler->ImportOption(nullptr, vpns, aSelections);
  selections_[sRow] = aSelections[pn_];

  if (set_top_menus_.find(sRow) != set_top_menus_.end()) {
    fill(selections_[sRow].begin(), selections_[sRow].end(), false);
  }
}

void OptionsList::ExportRow(std::string sRow) {
  if (set_top_menus_.find(sRow) != set_top_menus_.end()) {
    return;
  }

  std::vector<bool> aSelections[NUM_PLAYERS];
  aSelections[pn_] = selections_[sRow];

  std::vector<PlayerNumber> vpns;
  vpns.push_back(pn_);

  rows_[sRow]->ExportOption(vpns, aSelections);
}

void OptionsList::SetDefaultCurrentRow() {
  /* If all items on the row just point to other menus, default to 0. */
  menu_stack_selection_ = 0;

  const std::string& sCurrentRow = menu_stack_.back();
  const OptionRowHandler* pHandler = rows_.find(sCurrentRow)->second;
  if (pHandler->def_.select_type_ == SELECT_ONE) {
    /* One item is selected, so position the cursor on it. */
    menu_stack_selection_ = GetOneSelection(sCurrentRow, true);
    if (menu_stack_selection_ == -1) {
      menu_stack_selection_ = 0;
    }
  }
}

int OptionsList::FindScreenInHandler(
    const OptionRowHandler* pHandler, std::string sScreen) {
  for (size_t i = 0; i < pHandler->def_.choices_.size(); ++i) {
    if (pHandler->GetScreen(i) == sScreen) {
      return i;
    }
  }
  return -1;
}

void OptionsList::Pop() {
  if (menu_stack_.size() == 1) {
    Close();
    return;
  }

  std::string sLastMenu = menu_stack_.back();

  menu_stack_.pop_back();

  /* Choose the default option. */
  SetDefaultCurrentRow();

  /* If the old menu exists as a target from the new menu, switch to it. */
  const OptionRowHandler* pHandler = GetCurrentHandler();
  int iIndex = FindScreenInHandler(pHandler, sLastMenu);
  if (iIndex != -1) {
    menu_stack_selection_ = iIndex;
  }

  SwitchToCurrentRow();
  TweenOnCurrentRow(false);
}

void OptionsList::Push(std::string sDest) {
  menu_stack_.push_back(sDest);
  SetDefaultCurrentRow();
  SwitchToCurrentRow();
}

void OptionsList::SelectItem(const std::string& sRowName, int iMenuItem) {
  const OptionRowHandler* pHandler = rows_[sRowName];
  std::vector<bool>& bSelections = selections_[sRowName];

  if (pHandler->def_.select_type_ == SELECT_MULTIPLE) {
    bool bSelected = !bSelections[iMenuItem];
    bSelections[iMenuItem] = bSelected;

    //		if( bSelected )
    //			m_SoundToggleOn.Play();
    //		else
    //			m_SoundToggleOff.Play();
  } else  // data.selectType != SELECT_MULTIPLE
  {
    fill(bSelections.begin(), bSelections.end(), false);
    bSelections[iMenuItem] = true;
  }

  SelectionsChanged(sRowName);
  UpdateMenuFromSelections();
}

void OptionsList::SelectionsChanged(const std::string& sRowName) {
  const OptionRowHandler* pHandler = rows_[sRowName];
  std::vector<bool>& bSelections = selections_[sRowName];

  if (pHandler->def_.one_choice_for_all_players_ && linked_ != nullptr) {
    std::vector<bool>& bLinkedSelections = linked_->selections_[sRowName];
    bLinkedSelections = bSelections;

    if (linked_->IsOpened()) {
      linked_->UpdateMenuFromSelections();
    }

    linked_->ExportRow(sRowName);
  }

  ExportRow(sRowName);
}

void OptionsList::UpdateMenuFromSelections() {
  const std::vector<bool>& bCurrentSelections =
      selections_.find(GetCurrentRow())->second;
  row_[current_row_].SetUnderlines(bCurrentSelections, GetCurrentHandler());
  row_[current_row_].SetTextFromHandler(GetCurrentHandler());
}

bool OptionsList::Start() {
  OptionRowHandler* pHandler = GetCurrentHandler();
  const std::string& sCurrentRow = menu_stack_.back();
  std::vector<bool>& bSelections = selections_[sCurrentRow];
  if (menu_stack_selection_ == (int)bSelections.size()) {
    Pop();

    Message msg("OptionsListPop");
    msg.SetParam("Player", pn_);
    MESSAGEMAN->Broadcast(msg);

    return menu_stack_.empty();
  }

  {
    std::string sIconText;
    GameCommand gc;
    pHandler->GetIconTextAndGameCommand(menu_stack_selection_, sIconText, gc);
    if (gc.name_ == RESET_ROW) {
      GAMESTATE->player_state_[pn_]->ResetToDefaultPlayerOptions(
          ModsLevel_Preferred);
      GAMESTATE->ResetToDefaultSongOptions(ModsLevel_Preferred);

      /* Import options. */
      for (std::pair<const std::string&, OptionRowHandler*> hand : rows_) {
        ImportRow(hand.first);
        SelectionsChanged(hand.first);
      }

      UpdateMenuFromSelections();

      Message msg("OptionsListReset");
      msg.SetParam("Player", pn_);
      MESSAGEMAN->Broadcast(msg);

      return false;
    }
  }

  std::string sDest = pHandler->GetScreen(menu_stack_selection_);
  if (sDest.size()) {
    Push(sDest);
    TweenOnCurrentRow(true);

    Message msg("OptionsListPush");
    msg.SetParam("Player", pn_);
    MESSAGEMAN->Broadcast(msg);

    return false;
  }

  SelectItem(GetCurrentRow(), menu_stack_selection_);

  /* Move to the exit row, but only if it's SelectOne. */
  if (pHandler->def_.select_type_ == SELECT_ONE) {
    menu_stack_selection_ = (int)bSelections.size();
    PositionCursor();
  }
  if (pHandler->NotifyOfSelection(pn_, menu_stack_selection_)) {
    /* The current selections are irrelevant when we're getting them
     * from NotifyOfSelection. Re import them from the OptionRow. */
    ImportRow(sCurrentRow);
    UpdateMenuFromSelections();
  }

  /* Better to include Selection as a parameter since
   * the index may or may not change depending on if SelectType
   * is SELECT_ONE or SELECT_MULTIPLE. */
  Message msg("OptionsListStart");
  msg.SetParam("Player", pn_);
  msg.SetParam("Selection", menu_stack_selection_);
  MESSAGEMAN->Broadcast(msg);

  return false;
}

/*
 * Copyright (c) 2006 Glenn Maynard
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
