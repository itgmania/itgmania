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

static const std::string kResetRow = "ResetOptions";

void OptionListRow::Load(OptionsList* options, const std::string& type) {
  options_ = options;
  ITEMS_SPACING_Y.Load(type, "ItemsSpacingY");

  text_.resize(1);
  text_[0].SetName("Text");
  text_[0].LoadFromFont(THEME->GetPathF(type, "normal"));
  ActorUtil::LoadAllCommands(text_[0], type);

  underlines_.resize(1);
  underlines_[0].Load(THEME->GetPathG(type, "underline"));
  underlines_[0]->SetName("Underline");
  ActorUtil::LoadAllCommands(*underlines_[0], type);

  text_[0].PlayCommand("On");
  underlines_[0]->PlayCommand("On");
}

// GetTitleForHandler
// can always use the title
// can have special speed row titles, note skin, or any SELECT_ONE

void OptionListRow::SetFromHandler(const OptionRowHandler* handler) {
  this->FinishTweening();
  this->RemoveAllChildren();

  if (handler == nullptr) {
    return;
  }

  unsigned int num = std::max(handler->def_.choices_.size(), text_.size()) + 1;
  text_.resize(num, text_[0]);
  underlines_.resize(num, underlines_[0]);

  for (unsigned i = 0; i < handler->def_.choices_.size(); ++i) {
    // init underlines
    this->AddChild(underlines_[i]);

    // init text
    this->AddChild(&text_[i]);
  }

  SetTextFromHandler(handler);

  const unsigned cnt = handler->def_.choices_.size();
  items_in_two_rows_ = (int)cnt > MAX_ITEMS_BEFORE_SPLIT;
  const float width = ITEMS_SPLIT_WIDTH;
  float y = 0;
  for (unsigned i = 0; i < cnt; ++i) {
    float x = 0;
    if (items_in_two_rows_) {
      if ((i % 2) == 0) {
        x = -width / 2;
      } else {
        x = +width / 2;
      }
    }

    // set the Y position of each item in the line
    text_[i].SetXY(x, y);
    underlines_[i]->SetXY(x, y);

    if (items_in_two_rows_) {
      underlines_[i]->PlayCommand("SetTwoRows");
    } else {
      underlines_[i]->PlayCommand("SetOneRow");
    }

    if (!items_in_two_rows_ || (i % 2) == 1 || i + 1 == cnt) {
      y += ITEMS_SPACING_Y;
    }
  }

  int exit = handler->def_.choices_.size();
  text_[exit].SetText("Exit");  // XXX localize
  text_[exit].SetXY(0, y);
  this->AddChild(&text_[exit]);
}

void OptionListRow::SetTextFromHandler(const OptionRowHandler* handler) {
  ASSERT(handler != nullptr);
  for (unsigned i = 0; i < handler->def_.choices_.size(); ++i) {
    // init text
    std::string text = handler->GetThemedItemText(i);

    std::string dest = handler->GetScreen(i);
    if (options_->set_direct_rows_.find(dest) !=
            options_->set_direct_rows_.end() &&
        dest.size()) {
      const OptionRowHandler* target = options_->rows_[dest];
      if (target->def_.select_type_ == SELECT_ONE) {
        int selection = options_->GetOneSelection(dest);
        text += ": " + target->GetThemedItemText(selection);
      }
    }

    text_[i].SetText(text);
  }
}

void OptionListRow::SetUnderlines(
    const std::vector<bool>& selections, const OptionRowHandler* p_handler) {
  for (unsigned i = 0; i < selections.size(); ++i) {
    Actor* actor = underlines_[i];

    bool selected = selections[i];
    std::string dest = p_handler->GetScreen(i);
    if (dest.size()) {
      /* This is a submenu.  Underline the row if its options have been changed
       * from the default. */
      const OptionRowHandler* target = options_->rows_[dest];
      if (target->def_.select_type_ == SELECT_ONE) {
        int selection = options_->GetOneSelection(dest);
        const OptionRowHandler* l_handler = options_->rows_.find(dest)->second;
        int i_default = l_handler->GetDefaultOption();
        if (i_default != -1 && selection != i_default) {
          selected |= true;
        }
      } else if (target->def_.select_type_ == SELECT_MULTIPLE) {
        const std::vector<bool>& target_selections =
            options_->selections_.find(dest)->second;
        for (unsigned j = 0; j < target_selections.size(); j++) {
          if (target_selections[j]) {
            selected = true;
          }
        }
      }
    }

    actor->PlayCommand(selected ? "Show" : "Hide");
  }
}

void OptionListRow::PositionCursor(Actor* cursor, int selection) {
  float x = text_[selection].GetDestX();
  float y = text_[selection].GetDestY();
  if (items_in_two_rows_) {
    cursor->PlayCommand("PositionTwoRows");
  } else {
    cursor->PlayCommand("PositionOneRow");
  }
  cursor->SetXY(x, y);
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
void OptionsList::Load(std::string type, PlayerNumber pn) {
  TOP_MENU.Load(type, "TopMenu");

  pn_ = pn;
  start_is_down_ = false;
  game_button_previous_item_ = INPUTMAPPER->GetInputScheme()->ButtonNameToIndex(
      THEME->GetMetric(name_, "PrevItemButton"));
  game_button_next_item_ = INPUTMAPPER->GetInputScheme()->ButtonNameToIndex(
      THEME->GetMetric(name_, "NextItemButton"));

  codes_.Load(type);

  cursor_.Load(THEME->GetPathG(type, "cursor"));
  cursor_->SetName("Cursor");
  ActorUtil::LoadAllCommands(*cursor_, type);
  this->AddChild(cursor_);

  std::vector<std::string> direct_lines;
  split(DIRECT_LINES, ",", direct_lines, true);
  for (std::string& s : direct_lines) {
    set_direct_rows_.insert(s);
  }

  std::vector<std::string> set_to_load;
  split(TOP_MENUS, ",", set_to_load);
  set_top_menus_.insert(set_to_load.begin(), set_to_load.end());

  while (!set_to_load.empty()) {
    std::string line_name = *set_to_load.begin();
    set_to_load.erase(set_to_load.begin());

    if (rows_.find(line_name) != rows_.end()) {
      continue;
    }

    std::string row_commands = LINE(line_name);
    Commands cmds;
    ParseCommands(row_commands, cmds, false);

    OptionRowHandler* hand = OptionRowHandlerUtil::Make(cmds);
    if (hand == nullptr) {
      LuaHelpers::ReportScriptErrorFmt(
          "Invalid OptionRowHandler '%s' in %s::Line%s",
          cmds.GetOriginalCommandString().c_str(), name_.c_str(),
          line_name.c_str());
      continue;
    }

    rows_[line_name] = hand;

    for (size_t i = 0; i < hand->def_.choices_.size(); ++i) {
      std::string screen = hand->GetScreen(i);
      if (!screen.empty()) {
        set_to_load.push_back(screen);
      }
    }
  }

  for (int i = 0; i < 2; ++i) {
    row_[i].SetName("OptionsList");
    row_[i].Load(this, "OptionsList");
    ActorUtil::LoadAllCommands(row_[i], type);
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
  std::string current_row = GetCurrentRow();
  return rows_[current_row];
}

int OptionsList::GetOneSelection(std::string row, bool allow_fail) const {
  std::map<std::string, std::vector<bool> >::const_iterator it =
      selections_.find(row);
  ASSERT_M(it != selections_.end(), row);
  const std::vector<bool>& selections = it->second;
  for (unsigned i = 0; i < selections.size(); i++) {
    if (selections[i]) {
      return i;
    }
  }

  ASSERT(
      allow_fail);  // shouldn't call this if not expecting one to be selected
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
void OptionsList::SwitchMenu(int dir) {
  /* Consider the menu as a list, where the main menu is the first item and the
   * submenus follow.  This allows consistent navigation; moving right from the
   * main menu walks through the menus, moving left goes back as far as the main
   * menu.  Don't loop, so it's harder to lose track of menus. */
  std::string top_row = menu_stack_.front();
  const OptionRowHandler* handler = rows_[top_row];
  int current_row = 0;
  if (menu_stack_.size() == 1) {
    current_row = -1;
  } else {
    current_row = FindScreenInHandler(handler, menu_stack_.back());
  }

  current_row += dir;
  if (current_row >= 0) {
    if (current_row >= (int)handler->def_.choices_.size()) {
      return;
    }
    std::string dest = handler->GetScreen(current_row);
    if (dest.empty()) {
      return;
    }

    if (menu_stack_.size() == 1) {
      menu_stack_.push_back(dest);
    } else {
      menu_stack_.back() = dest;
    }
  } else {
    if (menu_stack_.size() == 1) {
      return;
    }

    menu_stack_.pop_back();
  }

  SetDefaultCurrentRow();
  SwitchToCurrentRow();
  TweenOnCurrentRow(dir > 0);
}

bool OptionsList::Input(const InputEventPlus& input) {
  Message msg("");
  if (codes_.InputMessage(input, msg)) {
    this->HandleMessage(msg);
  }

  const OptionRowHandler* handler = GetCurrentHandler();

  PlayerNumber pn = input.pn_;
  if (start_is_down_) {
    if (input.menu_i_ == GAME_BUTTON_LEFT ||
        input.menu_i_ == GAME_BUTTON_RIGHT) {
      if (input.type_ != IET_FIRST_PRESS) {
        return false;
      }

      accept_start_release_ = false;

      const std::string& current_row = menu_stack_.back();
      std::vector<bool>& selections = selections_[current_row];
      if (menu_stack_selection_ == (int)selections.size()) {
        return false;
      }

      std::string dest = handler->GetScreen(menu_stack_selection_);
      if (set_direct_rows_.find(dest) != set_direct_rows_.end() &&
          dest.size()) {
        const OptionRowHandler* target = rows_[dest];
        std::vector<bool>& target_selections = selections_[dest];

        if (target->def_.select_type_ == SELECT_ONE) {
          int selection = GetOneSelection(dest);
          int dir = (input.menu_i_ == GAME_BUTTON_RIGHT ? +1 : -1);
          selection += dir;
          wrap(selection, target_selections.size());
          SelectItem(dest, selection);

          Message l_msg("OptionsListQuickChange");
          l_msg.SetParam("Player", pn);
          l_msg.SetParam("Direction", dir);
          l_msg.SetParam("Selection", selection);
          MESSAGEMAN->Broadcast(l_msg);
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
        handler->def_.choices_.size() + 1);  // +1 for exit row
    PositionCursor();

    Message l_msg("OptionsListLeft");
    l_msg.SetParam("Player", input.pn_);
    l_msg.SetParam("Selection", menu_stack_selection_);
    MESSAGEMAN->Broadcast(l_msg);
    return true;
  } else if (input.menu_i_ == game_button_next_item_) {
    if (input.type_ == IET_RELEASE) {
      return false;
    }

    ++menu_stack_selection_;
    wrap(
        menu_stack_selection_,
        handler->def_.choices_.size() + 1);  // +1 for exit row
    PositionCursor();

    Message l_msg("OptionsListRight");
    l_msg.SetParam("Player", input.pn_);
    l_msg.SetParam("Selection", menu_stack_selection_);
    MESSAGEMAN->Broadcast(l_msg);
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
void OptionsList::TweenOnCurrentRow(bool forward) {
  OptionListRow& old_row = row_[!current_row_];
  OptionListRow& new_row = row_[current_row_];

  /* Tween out the old row. */
  if (forward) {
    old_row.PlayCommand("TweenOutForward");
  } else {
    old_row.PlayCommand("TweenOutBackward");
  }

  /* Tween in the old row. */
  if (forward) {
    new_row.PlayCommand("TweenInForward");
  } else {
    new_row.PlayCommand("TweenInBackward");
  }
}

void OptionsList::ImportRow(std::string row) {
  std::vector<bool> selections[kNumPlayers];
  std::vector<PlayerNumber> vpns;
  vpns.push_back(pn_);
  OptionRowHandler* handler = rows_[row];
  selections[pn_].resize(handler->def_.choices_.size());
  handler->ImportOption(nullptr, vpns, selections);
  selections_[row] = selections[pn_];

  if (set_top_menus_.find(row) != set_top_menus_.end()) {
    fill(selections_[row].begin(), selections_[row].end(), false);
  }
}

void OptionsList::ExportRow(std::string row) {
  if (set_top_menus_.find(row) != set_top_menus_.end()) {
    return;
  }

  std::vector<bool> selections[kNumPlayers];
  selections[pn_] = selections_[row];

  std::vector<PlayerNumber> vpns;
  vpns.push_back(pn_);

  rows_[row]->ExportOption(vpns, selections);
}

void OptionsList::SetDefaultCurrentRow() {
  /* If all items on the row just point to other menus, default to 0. */
  menu_stack_selection_ = 0;

  const std::string& current_row = menu_stack_.back();
  const OptionRowHandler* handler = rows_.find(current_row)->second;
  if (handler->def_.select_type_ == SELECT_ONE) {
    /* One item is selected, so position the cursor on it. */
    menu_stack_selection_ = GetOneSelection(current_row, true);
    if (menu_stack_selection_ == -1) {
      menu_stack_selection_ = 0;
    }
  }
}

int OptionsList::FindScreenInHandler(
    const OptionRowHandler* handler, std::string screen) {
  for (size_t i = 0; i < handler->def_.choices_.size(); ++i) {
    if (handler->GetScreen(i) == screen) {
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

  std::string last_menu = menu_stack_.back();

  menu_stack_.pop_back();

  /* Choose the default option. */
  SetDefaultCurrentRow();

  /* If the old menu exists as a target from the new menu, switch to it. */
  const OptionRowHandler* handler = GetCurrentHandler();
  int index = FindScreenInHandler(handler, last_menu);
  if (index != -1) {
    menu_stack_selection_ = index;
  }

  SwitchToCurrentRow();
  TweenOnCurrentRow(false);
}

void OptionsList::Push(std::string dest) {
  menu_stack_.push_back(dest);
  SetDefaultCurrentRow();
  SwitchToCurrentRow();
}

void OptionsList::SelectItem(const std::string& row_name, int menu_item) {
  const OptionRowHandler* handler = rows_[row_name];
  std::vector<bool>& selections = selections_[row_name];

  if (handler->def_.select_type_ == SELECT_MULTIPLE) {
    bool selected = !selections[menu_item];
    selections[menu_item] = selected;

    //		if( bSelected )
    //			m_SoundToggleOn.Play();
    //		else
    //			m_SoundToggleOff.Play();
  } else  // data.selectType != SELECT_MULTIPLE
  {
    fill(selections.begin(), selections.end(), false);
    selections[menu_item] = true;
  }

  SelectionsChanged(row_name);
  UpdateMenuFromSelections();
}

void OptionsList::SelectionsChanged(const std::string& row_name) {
  const OptionRowHandler* handler = rows_[row_name];
  std::vector<bool>& selections = selections_[row_name];

  if (handler->def_.one_choice_for_all_players_ && linked_ != nullptr) {
    std::vector<bool>& linked_selections = linked_->selections_[row_name];
    linked_selections = selections;

    if (linked_->IsOpened()) {
      linked_->UpdateMenuFromSelections();
    }

    linked_->ExportRow(row_name);
  }

  ExportRow(row_name);
}

void OptionsList::UpdateMenuFromSelections() {
  const std::vector<bool>& current_selections =
      selections_.find(GetCurrentRow())->second;
  row_[current_row_].SetUnderlines(current_selections, GetCurrentHandler());
  row_[current_row_].SetTextFromHandler(GetCurrentHandler());
}

bool OptionsList::Start() {
  OptionRowHandler* handler = GetCurrentHandler();
  const std::string& current_row = menu_stack_.back();
  std::vector<bool>& selections = selections_[current_row];
  if (menu_stack_selection_ == (int)selections.size()) {
    Pop();

    Message msg("OptionsListPop");
    msg.SetParam("Player", pn_);
    MESSAGEMAN->Broadcast(msg);

    return menu_stack_.empty();
  }

  {
    std::string icon_text;
    GameCommand gc;
    handler->GetIconTextAndGameCommand(menu_stack_selection_, icon_text, gc);
    if (gc.name_ == kResetRow) {
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

  std::string dest = handler->GetScreen(menu_stack_selection_);
  if (dest.size()) {
    Push(dest);
    TweenOnCurrentRow(true);

    Message msg("OptionsListPush");
    msg.SetParam("Player", pn_);
    MESSAGEMAN->Broadcast(msg);

    return false;
  }

  SelectItem(GetCurrentRow(), menu_stack_selection_);

  /* Move to the exit row, but only if it's SelectOne. */
  if (handler->def_.select_type_ == SELECT_ONE) {
    menu_stack_selection_ = (int)selections.size();
    PositionCursor();
  }
  if (handler->NotifyOfSelection(pn_, menu_stack_selection_)) {
    /* The current selections are irrelevant when we're getting them
     * from NotifyOfSelection. Re import them from the OptionRow. */
    ImportRow(current_row);
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
