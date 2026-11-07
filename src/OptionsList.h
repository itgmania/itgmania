#ifndef SCREEN_OPTIONS_LIST_H
#define SCREEN_OPTIONS_LIST_H

#include <map>
#include <set>
#include <string>
#include <vector>

#include "Actor.h"
#include "ActorFrame.h"
#include "AutoActor.h"
#include "BitmapText.h"
#include "CodeSet.h"
#include "GameInput.h"
#include "InputQueue.h"
#include "OptionRowHandler.h"
#include "PlayerNumber.h"
#include "ThemeMetric.h"

class OptionsList;
class OptionListRow : public ActorFrame {
 public:
  void Load(OptionsList* options, const std::string& type);
  void SetFromHandler(const OptionRowHandler* handler);
  void SetTextFromHandler(const OptionRowHandler* handler);
  void SetUnderlines(
      const std::vector<bool>& selections, const OptionRowHandler* handler);

  void PositionCursor(Actor* cursor, int selection);

  void Start();

 private:
  OptionsList* options_;

  std::vector<BitmapText> text_;
  // underline for each ("self or child has selection")
  std::vector<AutoActor> underlines_;

  bool items_in_two_rows_;

  ThemeMetric<float> ITEMS_SPACING_Y;
};
/** @brief A popup options list. */
class OptionsList : public ActorFrame {
 public:
  friend class OptionListRow;

  OptionsList();
  ~OptionsList();

  void Load(std::string type, PlayerNumber pn);
  void Reset();

  void Link(OptionsList* link) { linked_ = link; }

  /** @brief Show the top-level menu. */
  void Open();

  /** @brief Close all menus (for menu timer). */
  void Close();

  bool Input(const InputEventPlus& input);
  bool IsOpened() const { return menu_stack_.size() > 0; }

  bool
  Start();  // return true if the last menu was popped in response to this press

 private:
  ThemeMetric<std::string> TOP_MENU;

  void SelectItem(const std::string& row_name, int menu_item);
  void SwitchMenu(int dir);
  void PositionCursor();
  void SelectionsChanged(const std::string& row_name);
  void UpdateMenuFromSelections();
  std::string GetCurrentRow() const;
  OptionRowHandler* GetCurrentHandler();
  int GetOneSelection(std::string row, bool allow_fail = false) const;
  void SwitchToCurrentRow();
  void TweenOnCurrentRow(bool forward);
  void SetDefaultCurrentRow();
  void Push(std::string dest);
  void Pop();
  void ImportRow(std::string row);
  void ExportRow(std::string row);
  static int FindScreenInHandler(
      const OptionRowHandler* handler, std::string screen);

  InputQueueCodeSet codes_;

  OptionsList* linked_;

  bool start_is_down_;
  bool accept_start_release_;

  std::map<std::string, OptionRowHandler*> rows_;
  std::map<std::string, std::vector<bool> > selections_;
  std::set<std::string> set_direct_rows_;
  std::set<std::string>
      set_top_menus_;  // list of top-level menus, pointing to submenus

  PlayerNumber pn_;
  AutoActor cursor_;
  OptionListRow row_[2];
  int current_row_;

  std::vector<std::string> menu_stack_;
  int menu_stack_selection_;

 protected:
  GameButton game_button_previous_item_;
  GameButton game_button_next_item_;
};

#endif

/**
 * @file
 * @author 2006 Glenn Maynard (c)
 * @section LICENSE
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
