#include "ScreenOptions.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "Actor.h"
#include "ActorUtil.h"
#include "BitmapText.h"
#include "EnumHelper.h"
#include "GameCommand.h"
#include "GameInput.h"
#include "GameState.h"
#include "InputEventPlus.h"
#include "InputFilter.h"
#include "InputMapper.h"
#include "LuaBinding.h"
#include "LuaManager.h"
#include "MessageManager.h"
#include "OptionRowHandler.h"
#include "OptionsCursor.h"
#include "PlayerNumber.h"
#include "PrefsManager.h"
#include "RageInputDevice.h"
#include "RageLog.h"
#include "RageThreads.h"
#include "RageUtil.h"
#include "ScreenManager.h"
#include "ScreenMessage.h"
#include "ScreenWithMenuElements.h"
#include "ThemeManager.h"
#include "global.h"

/*
 * These navigation types are provided:
 *
 * All modes:
 *  left, right -> change option
 *  up, down -> change row
 *   (in toggle modes, focus on nearest item to old focus)
 *
 * NAV_THREE_KEY:
 *  start -> move to next row
 *  left+right+start -> move to prev row
 *  (next screen via "exit" entry)
 * This is the minimal navigation, for using menus with only three buttons.
 *
 * NAV_THREE_KEY_ALT:
 *  left, right -> next, prev row
 *  start -> change option
 *
 * NAV_FIVE_KEY:
 *  start -> next screen
 * This is a much more convenient navigation, requiring five keys.
 *
 * NAV_TOGGLE_THREE_KEY:
 *  start -> on first choice, move to next row; otherewise toggle option and
 * move to first choice left+right+start -> move to prev row
 *
 * NAV_TOGGLE_FIVE_KEY:
 *  start -> toggle option
 *
 * Regular modes and toggle modes must be enabled by the theme.  We could simply
 * automatically switch to toggle mode for multiselect rows, but that would be
 * strange for non-multiselect rows (eg. scroll speed).
 *
 * THREE_KEY modes are navigatable with only MenuLeft, MenuRight and MenuStart,
 * and are used when PREFSMAN->m_iArcadeOptionsNavigation is enabled.  However,
 * they can still use MenuUp and MenuDown for nonessential behavior.
 *
 * NAV_THREE_KEY_MENU:
 *  left, right -> change row
 *  up, down -> change row
 *  start -> next screen
 * This is a specialized navigation for ScreenOptionsService.  It must be
 * enabled to allow screens that use rows to select other screens to work with
 * only three buttons.  (It's also used when in five-key mode.)
 *
 * We don't want to simply allow left/right to move up and down on single-entry
 * rows when in NAV_THREE_KEY, becasue left and right shouldn't exit the "exit"
 * row in player options menus, but it should in the options menu.
 */

static std::string OPTION_EXPLANATION(std::string s) {
  return THEME->GetString("OptionExplanations", s);
}

static const char* InputModeNames[] = {"Individual", "Together"};
StringToX(InputMode);

// REGISTER_SCREEN_CLASS( ScreenOptions );	// can't be instantiated
ScreenOptions::ScreenOptions() {
  // These can be overridden in a derived Init().
  switch (PREFSMAN->arcade_options_navigation_) {
    case 0:
      SetNavigation(NAV_FIVE_KEY);
      break;
    case 1:
      SetNavigation(NAV_THREE_KEY);
      break;
    case 2:
      SetNavigation(NAV_THREE_KEY_ALT);
      break;
  }
  input_mode_ = INPUTMODE_SHARE_CURSOR;
}

void ScreenOptions::Init() {
  NUM_ROWS_SHOWN.Load(name_, "NumRowsShown");
  ROW_INIT_COMMAND.Load(name_, "RowInitCommand");
  ROW_ON_COMMAND.Load(name_, "RowOnCommand");
  ROW_OFF_COMMAND.Load(name_, "RowOffCommand");
  SHOW_SCROLL_BAR.Load(name_, "ShowScrollBar");
  SCROLL_BAR_HEIGHT.Load(name_, "ScrollBarHeight");
  SCROLL_BAR_TIME.Load(name_, "ScrollBarTime");
  LINE_HIGHLIGHT_X.Load(name_, "LineHighlightX");
  SHOW_EXIT_ROW.Load(name_, "ShowExitRow");
  SEPARATE_EXIT_ROW.Load(name_, "SeparateExitRow");
  SEPARATE_EXIT_ROW_Y.Load(name_, "SeparateExitRowY");
  SHOW_EXPLANATIONS.Load(name_, "ShowExplanations");
  ALLOW_REPEATING_CHANGE_VALUE_INPUT.Load(
      name_, "AllowRepeatingChangeValueInput");
  CURSOR_TWEEN_SECONDS.Load(name_, "CursorTweenSeconds");
  WRAP_VALUE_IN_ROW.Load(name_, "WrapValueInRow");
  OPTION_ROW_NORMAL_METRICS_GROUP.Load(name_, "OptionRowNormalMetricsGroup");
  OPTION_ROW_EXIT_METRICS_GROUP.Load(name_, "OptionRowExitMetricsGroup");

  expr_row_position_transform_function_.SetFromReference(
      THEME->GetMetricR(name_, "RowPositionTransformFunction"));

  ScreenWithMenuElements::Init();

  sound_change_col_.Load(THEME->GetPathS(name_, "change"), true);
  sound_next_row_.Load(THEME->GetPathS(name_, "next"), true);
  sound_prev_row_.Load(THEME->GetPathS(name_, "prev"), true);
  sound_toggle_on_.Load(THEME->GetPathS(name_, "toggle on"), true);
  sound_toggle_off_.Load(THEME->GetPathS(name_, "toggle off"), true);
  sound_start_.Load(THEME->GetPathS(name_, "start"), true);

  // add everything to m_frameContainer so we can animate everything at once
  frame_container_.SetName("Container");
  LOAD_ALL_COMMANDS(frame_container_);
  this->AddChild(&frame_container_);

  spr_page_.Load(THEME->GetPathG(name_, "page"));
  spr_page_->SetName("Page");
  LOAD_ALL_COMMANDS_AND_SET_XY(spr_page_);
  frame_container_.AddChild(spr_page_);

  // init line highlights
  FOREACH_PlayerNumber(p) {
    spr_line_highlight_[p].Load(
        THEME->GetPathG(name_, ssprintf("LineHighlight P%d", p + 1)));
    spr_line_highlight_[p]->SetName(ssprintf("LineHighlightP%d", p + 1));
    spr_line_highlight_[p]->SetX(LINE_HIGHLIGHT_X);
    LOAD_ALL_COMMANDS(spr_line_highlight_[p]);
    frame_container_.AddChild(spr_line_highlight_[p]);
  }

  // init cursors
  FOREACH_PlayerNumber(p) {
    cursor_[p].Load("OptionsCursor" + PlayerNumberToString(p), true);
    cursor_[p].SetName("Cursor");
    LOAD_ALL_COMMANDS(cursor_[p]);
    frame_container_.AddChild(&cursor_[p]);
  }

  switch (input_mode_) {
    case INPUTMODE_INDIVIDUAL:
      FOREACH_PlayerNumber(p) {
        text_explanation_[p].LoadFromFont(
            THEME->GetPathF(name_, "explanation"));
        text_explanation_[p].SetDrawOrder(2);
        text_explanation_[p].SetName("Explanation" + PlayerNumberToString(p));
        LOAD_ALL_COMMANDS_AND_SET_XY(text_explanation_[p]);
        frame_container_.AddChild(&text_explanation_[p]);
      }
      break;
    case INPUTMODE_SHARE_CURSOR:
      text_explanation_together_.LoadFromFont(
          THEME->GetPathF(name_, "explanation"));
      text_explanation_together_.SetDrawOrder(2);
      text_explanation_together_.SetName("ExplanationTogether");
      LOAD_ALL_COMMANDS_AND_SET_XY(text_explanation_together_);
      frame_container_.AddChild(&text_explanation_together_);
      break;
    default:
      FAIL_M(ssprintf("Invalid InputMode: %i", input_mode_));
  }

  if (SHOW_SCROLL_BAR) {
    scroll_bar_.SetName("ScrollBar");
    scroll_bar_.SetBarHeight(SCROLL_BAR_HEIGHT);
    scroll_bar_.SetBarTime(SCROLL_BAR_TIME);
    scroll_bar_.Load("DualScrollBar");
    FOREACH_PlayerNumber(p)
        scroll_bar_.EnablePlayer(p, GAMESTATE->IsHumanPlayer(p));
    LOAD_ALL_COMMANDS_AND_SET_XY(scroll_bar_);
    scroll_bar_.SetDrawOrder(2);
    frame_container_.AddChild(&scroll_bar_);
  }

  spr_more_.Load(THEME->GetPathG(name_, "more"));
  spr_more_->SetName("More");
  LOAD_ALL_COMMANDS_AND_SET_XY(spr_more_);
  spr_more_->SetDrawOrder(2);
  spr_more_->PlayCommand("LoseFocus");
  frame_container_.AddChild(spr_more_);

  option_row_type_normal_.Load(OPTION_ROW_NORMAL_METRICS_GROUP, this);
  option_row_type_exit_.Load(OPTION_ROW_EXIT_METRICS_GROUP, this);
}

void ScreenOptions::InitMenu(const std::vector<OptionRowHandler*>& hands) {
  LOG->Trace("ScreenOptions::InitMenu()");

  for (unsigned i = 0; i < rows_.size(); i++) {
    frame_container_.RemoveChild(rows_[i]);
    RageUtil::SafeDelete(rows_[i]);
  }
  rows_.clear();

  for (unsigned r = 0; r < hands.size(); r++)  // foreach row
  {
    rows_.push_back(new OptionRow(&option_row_type_normal_));
    OptionRow& row = *rows_.back();
    row.SetDrawOrder(1);
    frame_container_.AddChild(&row);

    bool first_row_goes_down = options_navigation_ == NAV_TOGGLE_THREE_KEY;

    row.LoadNormal(hands[r], first_row_goes_down);
  }

  if (SHOW_EXIT_ROW) {
    rows_.push_back(new OptionRow(&option_row_type_exit_));
    OptionRow& row = *rows_.back();
    row.LoadExit();
    row.SetDrawOrder(1);
    frame_container_.AddChild(&row);
  }

  frame_container_.SortByDrawOrder();

  int index = 0;
  for (OptionRow* p : rows_) {
    Lua* L = LUA->Get();
    LuaHelpers::Push(L, index++);
    p->lua_instance_->Set(L, "iIndex");
    LUA->Release(L);

    p->RunCommands(ROW_INIT_COMMAND);
  }

  // poke once at all the explanation metrics so that we catch missing ones
  // early
  for (int r = 0; r < (int)rows_.size(); r++)  // foreach row
  {
    GetExplanationText(r);
  }
}

/* Call when option rows have been re-initialized. */
void ScreenOptions::RestartOptions() {
  expr_row_position_transform_function_.ClearCache();
  std::vector<PlayerNumber> vpns;
  FOREACH_HumanPlayer(p) vpns.push_back(p);

  for (unsigned r = 0; r < rows_.size(); r++)  // foreach row
  {
    OptionRow* p_row = rows_[r];
    p_row->Reload();

    this->ImportOptions(r, vpns);

    // HACK: Process disabled players, too, to hide inactive underlines.
    FOREACH_PlayerNumber(p) p_row->AfterImportOptions(p);
  }

  FOREACH_PlayerNumber(p) {
    current_row_[p] = -1;
    focus_x_[p] = -1;
    was_on_exit_[p] = false;

    // put focus on the first enabled row
    for (unsigned r = 0; r < rows_.size(); r++) {
      const OptionRow& row = *rows_[r];
      if (row.GetRowDef().IsEnabledForPlayer(p)) {
        current_row_[p] = r;
        break;
      }
    }

    // Hide the highlight if no rows are enabled.
    spr_line_highlight_[p]->SetVisible(
        current_row_[p] != -1 && GAMESTATE->IsHumanPlayer(p));
  }

  CHECKPOINT_M("About to get the rows positioned right.");

  PositionRows(false);
  FOREACH_HumanPlayer(pn) {
    for (unsigned r = 0; r < rows_.size(); ++r) {
      this->RefreshIcons(r, pn);
    }
    PositionCursor(pn);
  }

  FOREACH_PlayerNumber(p) { AfterChangeRow(p); }
  CHECKPOINT_M("Rows positioned.");
}

void ScreenOptions::BeginScreen() {
  ScreenWithMenuElements::BeginScreen();

  RestartOptions();

  FOREACH_PlayerNumber(p) got_at_least_one_start_pressed_[p] = false;

  ON_COMMAND(frame_container_);

  FOREACH_PlayerNumber(p) {
    cursor_[p].SetVisible(GAMESTATE->IsHumanPlayer(p));
    ON_COMMAND(cursor_[p]);
  }

  this->SortByDrawOrder();
}

void ScreenOptions::TweenOnScreen() {
  ScreenWithMenuElements::TweenOnScreen();

  for (OptionRow* p : rows_) {
    p->RunCommands(ROW_ON_COMMAND);
  }

  frame_container_.SortByDrawOrder();
}

void ScreenOptions::TweenOffScreen() {
  ScreenWithMenuElements::TweenOffScreen();

  for (OptionRow* p : rows_) {
    p->RunCommands(ROW_OFF_COMMAND);
  }
}

ScreenOptions::~ScreenOptions() {
  LOG->Trace("ScreenOptions::~ScreenOptions()");
  for (unsigned i = 0; i < rows_.size(); i++) {
    RageUtil::SafeDelete(rows_[i]);
  }
}

std::string ScreenOptions::GetExplanationText(int i_row) const {
  const OptionRow& row = *rows_[i_row];

  bool allow_explanation = row.GetRowDef().allow_explanation_;
  bool show_explanations = allow_explanation && SHOW_EXPLANATIONS.GetValue();
  if (!show_explanations) {
    return std::string();
  }

  std::string explanation_name = row.GetRowDef().explanation_name_;
  if (explanation_name.empty()) {
    explanation_name = row.GetRowDef().name_;
  }
  ASSERT(!explanation_name.empty());

  return OPTION_EXPLANATION(explanation_name);
}

void ScreenOptions::GetWidthXY(
    PlayerNumber pn, int i_row, int choice_on_row, int& width_out, int& x_out,
    int& y_out) const {
  ASSERT_M(
      i_row < (int)rows_.size(), ssprintf("%i < %i", i_row, (int)rows_.size()));
  const OptionRow& row = *rows_[i_row];
  row.GetWidthXY(pn, choice_on_row, width_out, x_out, y_out);
}

void ScreenOptions::RefreshIcons(int i_row, PlayerNumber pn) {
  OptionRow& row = *rows_[i_row];

  const OptionRowDefinition& def = row.GetRowDef();

  // find first selection and whether multiple are selected
  int first_selection = row.GetOneSelection(pn, true);

  // set icon name and bullet
  std::string icon;
  GameCommand gc;

  if (first_selection == -1) {
    icon = "Multi";
  } else if (first_selection != -1) {
    const OptionRowHandler* hand = row.GetHandler();
    if (hand) {
      int selection = first_selection +
                      (options_navigation_ == NAV_TOGGLE_THREE_KEY ? -1 : 0);
      hand->GetIconTextAndGameCommand(selection, icon, gc);
    }
  }

  // XXX: hack to not display text in the song options menu
  if (def.one_choice_for_all_players_) {
    icon = "";
  }

  rows_[i_row]->SetModIcon(pn, icon, gc);
}

void ScreenOptions::PositionCursor(PlayerNumber pn) {
  // Set the position of the cursor showing the current option the user is
  // changing.
  const int i_row = current_row_[pn];
  if (i_row == -1) {
    return;
  }

  ASSERT_M(
      i_row >= 0 && i_row < (int)rows_.size(),
      ssprintf("%i < %i", i_row, (int)rows_.size()));
  const OptionRow& row = *rows_[i_row];

  const int choice_with_focus = row.GetChoiceInRowWithFocus(pn);
  if (choice_with_focus == -1) {
    return;
  }

  int width, x, y;
  GetWidthXY(pn, i_row, choice_with_focus, width, x, y);

  OptionsCursor& cursor = cursor_[pn];
  cursor.SetBarWidth(width);
  cursor.SetXY((float)x, (float)y);
  bool can_go_left = choice_with_focus > 0;
  bool can_go_right =
      choice_with_focus >= 0 &&
      choice_with_focus < (int)row.GetRowDef().choices_.size() - 1;
  cursor.SetCanGo(can_go_left, can_go_right);
}

void ScreenOptions::TweenCursor(PlayerNumber pn) {
  // Set the position of the cursor showing the current option the user is
  // changing.
  const int i_row = current_row_[pn];
  ASSERT_M(
      i_row >= 0 && i_row < (int)rows_.size(),
      ssprintf("%i < %i", i_row, (int)rows_.size()));

  const OptionRow& row = *rows_[i_row];
  const int choice_with_focus = row.GetChoiceInRowWithFocus(pn);

  int width, x, y;
  GetWidthXY(pn, i_row, choice_with_focus, width, x, y);

  OptionsCursor& cursor = cursor_[pn];
  if (cursor.GetDestX() != (float)x || cursor.GetDestY() != (float)y ||
      cursor.GetBarWidth() != width) {
    cursor.StopTweening();
    cursor.BeginTweening(CURSOR_TWEEN_SECONDS);
    cursor.SetXY((float)x, (float)y);
    cursor.SetBarWidth(width);
  }

  bool can_go_left = false;
  bool can_go_right = false;
  switch (row.GetRowDef().layout_type_) {
    case LAYOUT_SHOW_ONE_IN_ROW:
      can_go_left = choice_with_focus > 0;
      can_go_right =
          choice_with_focus >= 0 &&
          choice_with_focus < (int)row.GetRowDef().choices_.size() - 1;
      break;
    case LAYOUT_SHOW_ALL_IN_ROW:
      break;
    default:
      break;
  }
  cursor.SetCanGo(can_go_left, can_go_right);

  if (GAMESTATE->IsHumanPlayer(pn)) {
    COMMAND(spr_line_highlight_[pn], "Change");
    if (row.GetRowType() == OptionRow::RowType_Exit) {
      COMMAND(spr_line_highlight_[pn], "ChangeToExit");
    }

    spr_line_highlight_[pn]->SetY((float)y);
  }
}

void ScreenOptions::Update(float delta_time) {
  // LOG->Trace( "ScreenOptions::Update(%f)", fDeltaTime );

  ScreenWithMenuElements::Update(delta_time);
}

bool ScreenOptions::Input(const InputEventPlus& input) {
  // HACK: This screen eats mouse inputs if we don't check for them first.
  bool mouse_evt = false;
  for (int i = MOUSE_LEFT; i <= MOUSE_WHEELDOWN; i++) {
    if (input.device_i_ == DeviceInput(DEVICE_MOUSE, (DeviceButton)i)) {
      mouse_evt = true;
    }
  }
  if (mouse_evt) {
    return ScreenWithMenuElements::Input(input);
  }

  /* Allow input when transitioning in (m_In.IsTransitioning()), but ignore it
   * when we're transitioning out. */
  if (cancel_.IsTransitioning() || out_.IsTransitioning() ||
      lock_input_secs_ > 0) {
    return false;
  }

  if (!GAMESTATE->IsHumanPlayer(input.pn_)) {
    return false;
  }

  if (input.type_ == IET_RELEASE) {
    switch (input.menu_i_) {
      case GAME_BUTTON_START:
      case GAME_BUTTON_SELECT:
      case GAME_BUTTON_MENURIGHT:
      case GAME_BUTTON_MENULEFT:
        INPUTMAPPER->ResetKeyRepeat(GAME_BUTTON_START, input.pn_);
        INPUTMAPPER->ResetKeyRepeat(GAME_BUTTON_RIGHT, input.pn_);
        INPUTMAPPER->ResetKeyRepeat(GAME_BUTTON_LEFT, input.pn_);
      default:
        break;
    }
  }

  // default input handler
  return Screen::Input(input);
}

void ScreenOptions::HandleScreenMessage(const ScreenMessage sm) {
  if (sm == SM_MenuTimer) {
    this->BeginFadingOut();
  } else if (sm == SM_BeginFadingOut) {
    if (IsTransitioning()) {
      return;  // already transitioning
    }

    // If the selected option sets a screen, honor it.
    std::string this_screen =
        GetNextScreenForFocusedItem(GAMESTATE->GetMasterPlayerNumber());
    if (this_screen != "") {
      next_screen_ = this_screen;
    }

    // If options set a NextScreen or one is specified in metrics, then fade out
    if (GetNextScreenName() == "") {
      LuaHelpers::ReportScriptErrorFmt(
          "%s::HandleScreenMessage: Tried to fade out, but we have no next "
          "screen",
          name_.c_str());
      return;
    }

    StartTransitioningScreen(SM_ExportOptions);
  } else if (sm == SM_ExportOptions) {
    std::vector<PlayerNumber> vpns;
    FOREACH_HumanPlayer(p) vpns.push_back(p);
    for (unsigned r = 0; r < rows_.size(); r++)  // foreach row
    {
      if (rows_[r]->GetRowType() == OptionRow::RowType_Exit) {
        continue;
      }
      this->ExportOptions(r, vpns);
    }

    this->HandleScreenMessage(SM_GoToNextScreen);
  }

  ScreenWithMenuElements::HandleScreenMessage(sm);
}

void ScreenOptions::PositionRows(bool tween) {
  const int total = NUM_ROWS_SHOWN;
  const int halfsize = total / 2;

  int first_start, first_end, second_start, second_end;

  // Choices for each player. If only one player is active, it's the same for
  // both.
  int p1_choice = GAMESTATE->IsHumanPlayer(PLAYER_1) ? current_row_[PLAYER_1]
                                                     : current_row_[PLAYER_2];
  int p1_visible_choice = 0;
  int p2_choice = GAMESTATE->IsHumanPlayer(PLAYER_2) ? current_row_[PLAYER_2]
                                                     : current_row_[PLAYER_1];
  int p2_visible_choice = 0;
  std::vector<OptionRow*> rows(rows_);
  OptionRow* separate_exit_row = nullptr;

  if ((bool)SEPARATE_EXIT_ROW && !rows.empty() &&
      rows.back()->GetRowType() == OptionRow::RowType_Exit) {
    separate_exit_row = rows.back();

    // Remove the exit row for purposes of positioning everything else.
    if (p1_choice == (int)rows.size() - 1) {
      --p1_choice;
    }
    if (p2_choice == (int)rows.size() - 1) {
      --p2_choice;
    }

    rows.pop_back();
  }

  std::vector<int> visible_rows;

  for (int i = 0; i < (int)rows.size(); i++) {
    const OptionRowDefinition& def = rows[i]->GetRowDef();
    // We only hide rows if HideOnDisable is true, and the row is not enabled
    // for any player. If HideOnDisable is false, we show all rows, even if
    // they are not enabled for any player. Disabled rows show up faded out if
    // they are not enabled
    if (!def.enabled_for_players_.empty() ||
        !rows[i]->GetRowDef().hide_on_disable_) {
      visible_rows.push_back(i);
    }
  }

  const bool treat_all_rows_visible = visible_rows.empty();
  if (treat_all_rows_visible) {
    // If no rows have enabled players, treat all rows as
    // visible for layout. The EditMode help screen relies on this behavior
    // to render a view-only menu.
    // TODO: Provide a better way to specify view-only menus, and remove this
    // fallback.
    for (int i = 0; i < (int)rows.size(); i++) {
      visible_rows.push_back(i);
    }
  }

  bool found_p1 = false;
  bool found_p2 = false;
  for (int vi = 0; vi < (int)visible_rows.size(); vi++) {
    if (visible_rows[vi] == p1_choice) {
      p1_visible_choice = vi;
      found_p1 = true;
    }
    if (visible_rows[vi] == p2_choice) {
      p2_visible_choice = vi;
      found_p2 = true;
    }
  }

  if (!visible_rows.empty()) {
    if (!found_p1) {
      if (p1_choice <= visible_rows.front()) {
        p1_visible_choice = 0;
      } else {
        p1_visible_choice = (int)visible_rows.size() - 1;
      }
    }
    if (!found_p2) {
      if (p2_choice <= visible_rows.front()) {
        p2_visible_choice = 0;
      } else {
        p2_visible_choice = (int)visible_rows.size() - 1;
      }
    }
  }

  const int visible_count = (int)visible_rows.size();

  const bool both_players_activated =
      GAMESTATE->IsHumanPlayer(PLAYER_1) && GAMESTATE->IsHumanPlayer(PLAYER_2);
  if (input_mode_ == INPUTMODE_SHARE_CURSOR || !both_players_activated) {
    // Simply center the cursor.
    first_start = std::max(p1_visible_choice - halfsize, 0);
    first_end = first_start + total;
    second_start = second_end = first_end;
  } else {
    // First half:
    const int earliest = std::min(p1_visible_choice, p2_visible_choice);
    first_start = std::max(earliest - halfsize / 2, 0);
    first_end = first_start + halfsize;

    // Second half:
    const int latest = std::max(p1_visible_choice, p2_visible_choice);

    second_start = std::max(latest - halfsize / 2, 0);

    // Don't overlap.
    second_start = std::max(second_start, first_end);

    second_end = second_start + halfsize;
  }

  first_end = std::min(first_end, visible_count);
  second_end = std::min(second_end, visible_count);

  /* If less than total (and Rows.size()) are displayed, fill in the empty
   * space intelligently. */
  for (;;) {
    const int sum = (first_end - first_start) + (second_end - second_start);
    if (sum >= visible_count || sum >= total) {
      break;  // nothing more to display, or no room
    }
    /* First priority: expand the top of the second half until it meets
     * the first half. */
    if (second_start > first_end) {
      second_start--;
    }
    // Otherwise, expand either end.
    else if (first_start > 0) {
      first_start--;
    } else if (second_end < visible_count) {
      second_end++;
    } else {
      FAIL_M("Do we have room to grow or don't we?");
    }
  }

  int pos = 0;
  int visible_index = 0;
  for (int i = 0; i < (int)rows.size(); i++)  // foreach row
  {
    OptionRow& row = *rows[i];
    const OptionRowDefinition& def = row.GetRowDef();

    const bool row_enabled = !def.enabled_for_players_.empty();
    const bool hide_disabled = def.hide_on_disable_;
    const bool row_visible =
        row_enabled || !hide_disabled || treat_all_rows_visible;
    int this_visible_index = -1;
    if (row_visible) {
      this_visible_index = visible_index;
      visible_index++;
    }

    float f_pos = (float)pos;

    if (row_visible && this_visible_index < first_start) {
      f_pos = -0.5f;
    } else if (
        row_visible && this_visible_index >= first_end &&
        this_visible_index < second_start) {
      f_pos = ((int)NUM_ROWS_SHOWN) / 2 - 0.5f;
    } else if (row_visible && this_visible_index >= second_end) {
      f_pos = ((int)NUM_ROWS_SHOWN) - 0.5f;
    }

    Actor::TweenState destination =
        expr_row_position_transform_function_.GetTransformCached(
            f_pos, i, std::min((int)rows.size(), (int)NUM_ROWS_SHOWN));

    bool hidden = !row_visible;
    if (row_visible) {
      hidden = this_visible_index < first_start ||
               (this_visible_index >= first_end &&
                this_visible_index < second_start) ||
               this_visible_index >= second_end;
    }
    if (!row_enabled && hide_disabled) {
      hidden = true;
      row.SetVisible(false);
    } else {
      row.SetVisible(true);
    }
    for (int j = 0; j < NUM_DIFFUSE_COLORS; j++) {
      destination.diffuse_[j].a_ = hidden ? 0.0f : 1.0f;
    }
    if (!hidden) {
      pos++;
    }
    row.SetDestination(destination, tween);
  }

  if (separate_exit_row) {
    Actor::TweenState destination;
    destination.Init();
    destination.pos_.y_ = SEPARATE_EXIT_ROW_Y;

    for (int j = 0; j < NUM_DIFFUSE_COLORS; j++) {
      destination.diffuse_[j].a_ = 1.0f;
    }
    separate_exit_row->SetDestination(destination, tween);
  }
}

void ScreenOptions::AfterChangeValueOrRow(PlayerNumber pn) {
  if (!GAMESTATE->IsHumanPlayer(pn)) {
    return;
  }

  const int cur_row = current_row_[pn];

  if (cur_row == -1) {
    return;
  }

  // Update m_fY and m_bHidden[].
  PositionRows(true);

  // Do positioning.
  RefreshIcons(cur_row, pn);
  for (unsigned r = 0; r < rows_.size(); r++) {
    /* After changing a value, position underlines. Do this for both players,
     * since underlines for both players will change with
     * m_bOneChoiceForAllPlayers. */
    FOREACH_HumanPlayer(p) rows_[r]->PositionUnderlines(p);
    rows_[r]->PositionIcons(pn);
    rows_[r]->SetRowHasFocus(
        pn, GAMESTATE->IsHumanPlayer(pn) && cur_row == (int)r);
    rows_[r]->UpdateEnabledDisabled();
  }

  if (SHOW_SCROLL_BAR) {
    float percent = 0;
    if (rows_.size() > 1) {
      percent = cur_row / float(rows_.size() - 1);
    }
    scroll_bar_.SetPercentage(pn, percent);
  }

  // Update all players, since changing one player can move both cursors.
  FOREACH_HumanPlayer(p) TweenCursor(p);

  FOREACH_PlayerNumber(p) {
    OptionRow& row = *rows_[cur_row];
    const bool exit_selected = row.GetRowType() == OptionRow::RowType_Exit;
    if (GAMESTATE->GetNumHumanPlayers() != 1 && p != pn) {
      continue;
    }
    if (was_on_exit_[p] != exit_selected) {
      was_on_exit_[p] = exit_selected;
      COMMAND(
          spr_more_,
          ssprintf(
              "Exit%sP%i", exit_selected ? "Selected" : "Unselected", p + 1));
      spr_more_->PlayCommand(exit_selected ? "GainFocus" : "LoseFocus");
    }
  }

  const std::string text = GetExplanationText(cur_row);
  BitmapText* p_text = nullptr;
  switch (input_mode_) {
    case INPUTMODE_INDIVIDUAL:
      p_text = &text_explanation_[pn];
      break;
    case INPUTMODE_SHARE_CURSOR:
      p_text = &text_explanation_together_;
      break;
    default:
      break;
  }
  if (p_text->GetText() != text) {
    p_text->FinishTweening();
    ON_COMMAND(p_text);
    p_text->SetText(text);
  }
}

bool ScreenOptions::MenuBack(const InputEventPlus&) {
  Cancel(SM_GoToPrevScreen);
  return true;
}

bool ScreenOptions::AllAreOnLastRow() const {
  FOREACH_HumanPlayer(p) {
    if (current_row_[p] != (int)(rows_.size() - 1)) {
      return false;
    }
  }
  return true;
}

bool ScreenOptions::MenuStart(const InputEventPlus& input) {
  PlayerNumber pn = input.pn_;
  switch (input.type_) {
    case IET_FIRST_PRESS:
      got_at_least_one_start_pressed_[pn] = true;
      break;
    case IET_RELEASE:
      return false;  // ignore
    default:         // repeat type
      if (!got_at_least_one_start_pressed_[pn]) {
        return false;  // don't allow repeat
      }
      break;
  }

  /* If we are in a three-button mode, check to see if GAME_BUTTON_LEFT and
   * GAME_BUTTON_RIGHT are being held. */
  switch (options_navigation_) {
    case NAV_THREE_KEY:
    case NAV_TOGGLE_THREE_KEY: {
      bool holding_left_and_right =
          INPUTMAPPER->IsBeingPressed(GAME_BUTTON_RIGHT, pn) &&
          INPUTMAPPER->IsBeingPressed(GAME_BUTTON_LEFT, pn);
      if (holding_left_and_right) {
        if (MoveRowRelative(pn, -1, input.type_ != IET_FIRST_PRESS)) {
          sound_prev_row_.Play(true);
        }
        return true;
      }
    }
    default:
      break;
  }

  this->ProcessMenuStart(input);
  return true;
}

void ScreenOptions::ProcessMenuStart(const InputEventPlus& input) {
  PlayerNumber pn = input.pn_;

  int cur_row = current_row_[pn];

  if (cur_row < 0) {
    // this shouldn't be happening, but it is, so we need to bail out. -aj
    sound_start_.PlayCopy(true);
    this->BeginFadingOut();
    return;
  }

  OptionRow& row = *rows_[cur_row];

  if (options_navigation_ == NAV_THREE_KEY_MENU &&
      row.GetRowType() != OptionRow::RowType_Exit) {
    /* In NAV_THREE_KEY_MENU mode, if a row doesn't set a screen, it does
     * something.  Apply it now, and don't go to the next screen. */
    if (!FocusedItemEndsScreen(input.pn_)) {
      std::vector<PlayerNumber> vpns;
      vpns.push_back(input.pn_);
      ExportOptions(cur_row, vpns);
      return;
    }
  }

  // Check whether Start ends this screen.
  {
    bool end_this_screen = false;

    // If we didn't apply and return above in NAV_THREE_KEY_MENU, then the
    // selection sets a screen.
    if (options_navigation_ == NAV_THREE_KEY_MENU) {
      end_this_screen = true;
    }

    // If there's no exit row, then pressing Start on any row ends the screen.
    if (!SHOW_EXIT_ROW) {
      end_this_screen = true;
    }

    // If all players are on "Exit"
    if (AllAreOnLastRow()) {
      end_this_screen = true;
    }

    // Don't accept START to go to the next screen if we're still transitioning
    // in.
    if (end_this_screen &&
        (input.type_ != IET_FIRST_PRESS || IsTransitioning())) {
      return;
    }

    if (end_this_screen) {
      sound_start_.PlayCopy(true);
      this->BeginFadingOut();
      return;
    }
  }

  if (row.GetFirstItemGoesDown()) {
    int choice_in_row = row.GetChoiceInRowWithFocus(pn);
    if (choice_in_row == 0) {
      MenuDown(input);
      return;
    }
  }

  if (row.GetRowDef().select_type_ == SELECT_MULTIPLE) {
    int choice_in_row = row.GetChoiceInRowWithFocus(pn);
    bool selected = !row.GetSelected(pn, choice_in_row);
    bool changed = row.SetSelected(pn, choice_in_row, selected);
    if (changed) {
      AfterChangeValueOrRow(pn);
    }

    if (selected) {
      sound_toggle_on_.Play(true);
    } else {
      sound_toggle_off_.Play(true);
    }

    rows_[cur_row]->PositionUnderlines(pn);
    RefreshIcons(cur_row, pn);

    Message msg("SelectMultiple");
    msg.SetParam("PlayerNumber", pn);
    msg.SetParam("RowIndex", cur_row);
    msg.SetParam("ChoiceInRow", choice_in_row);
    msg.SetParam("Selected", selected);
    MESSAGEMAN->Broadcast(msg);

    if (row.GetFirstItemGoesDown() && row.GoToFirstOnStart()) {
      // move to the first choice in the row
      ChangeValueInRowAbsolute(
          current_row_[pn], pn, 0, input.type_ != IET_FIRST_PRESS);
    }
  } else  // data.selectType != SELECT_MULTIPLE
  {
    switch (options_navigation_) {
      case NAV_THREE_KEY:
        // don't wrap
        if (cur_row == (int)rows_.size() - 1) {
          return;
        }
        MenuDown(input);
        break;
      case NAV_THREE_KEY_ALT:
        ChangeValueInRowRelative(
            current_row_[input.pn_], input.pn_, +1,
            input.type_ != IET_FIRST_PRESS);
        break;

      case NAV_TOGGLE_THREE_KEY:
      case NAV_TOGGLE_FIVE_KEY: {
        int choice_in_row = row.GetChoiceInRowWithFocus(pn);
        if (row.GetRowDef().one_choice_for_all_players_) {
          row.SetOneSharedSelection(choice_in_row);
        } else {
          row.SetOneSelection(pn, choice_in_row);
        }

        if (row.GetFirstItemGoesDown()) {
          ChangeValueInRowAbsolute(
              current_row_[pn], pn, 0, input.type_ != IET_FIRST_PRESS);
        } else {
          ChangeValueInRowRelative(
              current_row_[pn], pn, 0, input.type_ != IET_FIRST_PRESS);
        }
        break;
      }
      case NAV_THREE_KEY_MENU:
        FAIL_M("NAV_THREE_KEY_MENU should be unreachable");
      case NAV_FIVE_KEY:
        /* Jump to the exit row.  (If everyone's already on the exit row, then
         * we'll have already gone to the next screen above.) */
        if (MoveRowAbsolute(pn, rows_.size() - 1)) {
          sound_next_row_.Play(true);
        }

        break;
    }
  }
}

void ScreenOptions::StoreFocus(PlayerNumber pn) {
  // Long rows always put us in the center, so don't update the focus.
  int current_row = current_row_[pn];
  const OptionRow& row = *rows_[current_row];
  if (row.GetRowDef().layout_type_ == LAYOUT_SHOW_ONE_IN_ROW) {
    return;
  }

  int width, y;
  GetWidthXY(
      pn, current_row_[pn], row.GetChoiceInRowWithFocus(pn), width,
      focus_x_[pn], y);
  LOG->Trace(
      "cur selection %ix%i @ %i", current_row_[pn],
      row.GetChoiceInRowWithFocus(pn), focus_x_[pn]);
}

bool ScreenOptions::FocusedItemEndsScreen(PlayerNumber pn) const {
  std::string screen = GetNextScreenForFocusedItem(pn);
  return !screen.empty();
}

std::string ScreenOptions::GetNextScreenForFocusedItem(PlayerNumber pn) const {
  int cur_row = this->GetCurrentRow(pn);

  if (cur_row == -1) {
    return std::string();
  }

  ASSERT(cur_row >= 0 && cur_row < (int)rows_.size());
  const OptionRow* row = rows_[cur_row];

  int choice = row->GetChoiceInRowWithFocus(pn);
  if (row->GetFirstItemGoesDown()) {
    choice--;
  }

  // not the "goes down" item
  if (choice == -1) {
    return std::string();
  }

  const OptionRowHandler* hand = row->GetHandler();
  if (hand == nullptr) {
    return std::string();
  }
  return hand->GetScreen(choice);
}

void ScreenOptions::BeginFadingOut() {
  this->PostScreenMessage(SM_BeginFadingOut, 0);
}

// Left/right
void ScreenOptions::ChangeValueInRowAbsolute(
    int i_row, PlayerNumber pn, int choice_index, bool repeat) {
  if (i_row == -1) {  // no row selected
    return;           // don't allow a move
  }

  OptionRow& row = *rows_[i_row];

  const int num_choices = row.GetRowDef().choices_.size();
  ASSERT(num_choices >= 0 && choice_index < num_choices);

  if (repeat && !ALLOW_REPEATING_CHANGE_VALUE_INPUT) {
    return;
  }

  int current_choice_with_focus = row.GetChoiceInRowWithFocus(pn);
  bool one_changed = (current_choice_with_focus != choice_index);

  Message msg("ChangeValue");
  msg.SetParam("PlayerNumber", pn);
  msg.SetParam("RowIndex", i_row);
  MESSAGEMAN->Broadcast(msg);

  row.SetChoiceInRowWithFocus(pn, choice_index);
  StoreFocus(pn);

  if (row.GetRowDef().one_choice_for_all_players_) {
    /* If this row is bOneChoiceForAllPlayers, then lock the cursors together
     * for this row. Don't do this in toggle modes, since the current selection
     * and the current focus are detached. */
    bool force_focused_choice_together = false;
    if (options_navigation_ != NAV_TOGGLE_THREE_KEY &&
        options_navigation_ != NAV_TOGGLE_FIVE_KEY &&
        row.GetRowDef().one_choice_for_all_players_) {
      force_focused_choice_together = true;
    }

    // Also lock focus if the screen is explicitly set to share cursors.
    if (input_mode_ == INPUTMODE_SHARE_CURSOR) {
      force_focused_choice_together = true;
    }

    if (force_focused_choice_together) {
      // lock focus together
      FOREACH_HumanPlayer(p) {
        row.SetChoiceInRowWithFocus(p, choice_index);
        StoreFocus(p);
      }
    }
  }

  FOREACH_PlayerNumber(p) {
    if (!row.GetRowDef().one_choice_for_all_players_ && p != pn) {
      continue;
    }

    // Don't auto-select the ▼ on value-arrow rows.
    if (choice_index == 0 && row.IsShowOneValueWithGoDown()) {
      continue;
    }

    // Value-arrow rows auto-select on Left/Right even in toggle mode.
    bool auto_select = options_navigation_ != NAV_TOGGLE_THREE_KEY ||
                       row.IsShowOneValueWithGoDown();

    if (row.GetRowDef().select_type_ == SELECT_ONE && auto_select) {
      row.SetOneSelection(p, choice_index);
    }
  }

  if (one_changed) {
    sound_change_col_.Play(true);
  }

  if (row.GetRowDef().export_on_change_) {
    std::vector<PlayerNumber> vpns;
    FOREACH_HumanPlayer(p) vpns.push_back(p);
    ExportOptions(i_row, vpns);
  }

  this->AfterChangeValueInRow(i_row, pn);
}

void ScreenOptions::ChangeValueInRowRelative(
    int i_row, PlayerNumber pn, int delta, bool repeat) {
  if (i_row == -1) {  // no row selected
    return;           // don't allow a move
  }

  OptionRow& row = *rows_[i_row];

  const int num_choices = row.GetRowDef().choices_.size();

  if (options_navigation_ == NAV_THREE_KEY_MENU && num_choices <= 1)  // 1 or 0
  {
    /* There are no other options on the row; move up or down instead of
     * left and right. This allows navigating the options menu with
     * left/right/start.
     * XXX: Only allow repeats if the opposite key isn't pressed; otherwise,
     * holding both directions will repeat in place continuously, which is
     * weird. */
    if (MoveRowRelative(pn, delta, repeat)) {
      if (delta < 0) {
        sound_prev_row_.Play(true);
      } else {
        sound_next_row_.Play(true);
      }
    }
    return;
  }

  if (num_choices <= 1) {  // nowhere to move
    return;
  }

  int current_choice_with_focus = row.GetChoiceInRowWithFocus(pn);

  // Value-arrow rows confine Left/Right to value choices [1, N-1]; the ▼
  // (choice 0) is reached only via the Start key.
  bool confine_to_values = row.IsShowOneValueWithGoDown();

  int new_choice_with_focus;
  if (confine_to_values && current_choice_with_focus == 0) {
    // Leaving the ▼; hover the current value without changing it.
    int selected = row.GetOneSelection(pn, true);
    new_choice_with_focus = (selected >= 1) ? selected : 1;
  } else {
    int lo = confine_to_values ? 1 : 0;
    int hi = num_choices - 1;
    new_choice_with_focus = current_choice_with_focus + delta;
    if (!repeat && WRAP_VALUE_IN_ROW.GetValue()) {
      if (confine_to_values) {
        if (new_choice_with_focus < lo) {
          new_choice_with_focus = hi;
        } else if (new_choice_with_focus > hi) {
          new_choice_with_focus = lo;
        }
      } else {
        wrap(new_choice_with_focus, num_choices);
      }
    } else {
      rage_clamp(new_choice_with_focus, lo, hi);
    }
  }

  ChangeValueInRowAbsolute(i_row, pn, new_choice_with_focus, repeat);
}

void ScreenOptions::AfterChangeValueInRow(int row, PlayerNumber pn) {
  AfterChangeValueOrRow(pn);
}

// Move up/down. Returns true if we actually moved.
bool ScreenOptions::MoveRowRelative(PlayerNumber pn, int dir, bool repeat) {
  // LOG->Trace( "MoveRowRelative(pn %i, dir %i, rep %i)", pn, iDir, bRepeat );

  int dest = -1;
  ASSERT(rows_.size() != 0);
  for (int r = 1; r < (int)rows_.size(); r++) {
    int delta = r * dir;
    dest = current_row_[pn] + delta;
    wrap(dest, rows_.size());

    OptionRow& row = *rows_[dest];
    if (row.GetRowDef().IsEnabledForPlayer(pn)) {
      break;
    }

    dest = -1;
  }

  if (dest == -1) {
    return false;
  }
  if (repeat) {
    // Don't wrap on repeating inputs.
    if (dir > 0 && dest < current_row_[pn]) {
      return false;
    }
    if (dir < 0 && dest > current_row_[pn]) {
      return false;
    }
  }

  return MoveRowAbsolute(pn, dest);
}

void ScreenOptions::AfterChangeRow(PlayerNumber pn) {
  const int i_row = current_row_[pn];
  if (i_row != -1) {
    OptionRow& row = *rows_[i_row];
    if (options_navigation_ == NAV_TOGGLE_FIVE_KEY) {
      if (row.GetRowDef().select_type_ == SELECT_ONE) {
        // For SELECT_ONE, keep focus on selected choice.
        int selection = row.GetOneSelection(pn, true);
        if (selection != -1) {
          row.SetChoiceInRowWithFocus(pn, selection);
        }
      } else if (row.GetRowDef().layout_type_ != LAYOUT_SHOW_ONE_IN_ROW) {
        // Else keep the selection in the row near the focus.
        int selection_dist = -1;
        for (unsigned i = 0; i < row.GetTextItemsSize(); ++i) {
          int width, x, y;
          GetWidthXY(pn, current_row_[pn], i, width, x, y);
          const int dist = std::abs(x - focus_x_[pn]);
          if (selection_dist == -1 || dist < selection_dist) {
            selection_dist = dist;
            row.SetChoiceInRowWithFocus(pn, i);
          }
        }
      }
    }

    if (row.GetFirstItemGoesDown()) {
      // If moving to a bFirstChoiceGoesDown row, put focus back on
      // the first choice.
      row.SetChoiceInRowWithFocus(pn, 0);
    }
  }

  AfterChangeValueOrRow(pn);
}

bool ScreenOptions::MoveRowAbsolute(PlayerNumber pn, int i_row) {
  bool changed = false;
  FOREACH_PlayerNumber(p) {
    if (input_mode_ == INPUTMODE_INDIVIDUAL && p != pn) {
      continue;  // skip
    }

    if (current_row_[p] == i_row) {
      continue;
    }

    current_row_[p] = i_row;

    AfterChangeRow(p);
    changed = true;

    const OptionRow& row = *rows_[i_row];
    Message msg("ChangeRow");
    msg.SetParam("PlayerNumber", p);
    msg.SetParam("RowIndex", GetCurrentRow(p));
    msg.SetParam("ChangedToExit", row.GetRowType() == OptionRow::RowType_Exit);
    MESSAGEMAN->Broadcast(msg);
  }

  return changed;
}

bool ScreenOptions::MenuLeft(const InputEventPlus& input) {
  if (options_navigation_ == NAV_THREE_KEY_ALT) {
    MenuUpDown(input, -1);
  } else {
    ChangeValueInRowRelative(
        current_row_[input.pn_], input.pn_, -1, input.type_ != IET_FIRST_PRESS);
  }

  PlayerNumber pn = input.pn_;
  MESSAGEMAN->Broadcast(
      (MessageID)(Message_MenuLeftP1 + Enum::to_integral(pn)));
  return true;
}

bool ScreenOptions::MenuRight(const InputEventPlus& input) {
  if (options_navigation_ == NAV_THREE_KEY_ALT) {
    MenuUpDown(input, +1);
  } else {
    ChangeValueInRowRelative(
        current_row_[input.pn_], input.pn_, +1, input.type_ != IET_FIRST_PRESS);
  }

  PlayerNumber pn = input.pn_;
  MESSAGEMAN->Broadcast(
      (MessageID)(Message_MenuRightP1 + Enum::to_integral(pn)));
  return true;
}

bool ScreenOptions::MenuUp(const InputEventPlus& input) {
  MenuUpDown(input, -1);
  PlayerNumber pn = input.pn_;
  MESSAGEMAN->Broadcast((MessageID)(Message_MenuUpP1 + Enum::to_integral(pn)));
  return true;
}

bool ScreenOptions::MenuDown(const InputEventPlus& input) {
  MenuUpDown(input, +1);
  PlayerNumber pn = input.pn_;
  MESSAGEMAN->Broadcast(
      (MessageID)(Message_MenuDownP1 + Enum::to_integral(pn)));
  return true;
}

bool ScreenOptions::MenuSelect(const InputEventPlus& input) {
  MenuUpDown(input, -1);
  return true;
}

void ScreenOptions::MenuUpDown(const InputEventPlus& input, int dir) {
  ASSERT(dir == -1 || dir == +1);
  PlayerNumber pn = input.pn_;

  if (input.type_ == IET_REPEAT) {
    /* If down is pressed, don't allow up to repeat, and vice versa. This
     * prevents holding both up and down from toggling repeatedly in-place. */
    if (dir == +1) {
      if (INPUTMAPPER->IsBeingPressed(GAME_BUTTON_MENUUP, pn) ||
          INPUTMAPPER->IsBeingPressed(GAME_BUTTON_SELECT, pn)) {
        return;
      }
    } else {
      if (INPUTMAPPER->IsBeingPressed(GAME_BUTTON_MENUDOWN, pn) ||
          INPUTMAPPER->IsBeingPressed(GAME_BUTTON_START, pn)) {
        return;
      }
    }
  }

  if (MoveRowRelative(pn, dir, input.type_ != IET_FIRST_PRESS)) {
    if (dir < 0) {
      sound_prev_row_.Play(true);
    } else {
      sound_next_row_.Play(true);
    }
  }
}
void ScreenOptions::RedrawOptions() {
  // Save current row indices before redraw
  int saved_row[NUM_PLAYERS];
  FOREACH_PlayerNumber(pn) saved_row[pn] = current_row_[pn];

  // RestartOptions();

  // Restore current row indices if they're still valid
  FOREACH_PlayerNumber(pn) {
    if (saved_row[pn] >= 0 && saved_row[pn] < GetNumRows()) {
      const OptionRow& row = *GetRow(saved_row[pn]);
      if (row.GetRowDef().IsEnabledForPlayer(pn)) {
        current_row_[pn] = saved_row[pn];
      }
    }
  }

  // Update positioning and cursors for restored indices
  PositionRows(true);
  FOREACH_HumanPlayer(pn) { PositionCursor(pn); }
  FOREACH_PlayerNumber(pn) { AfterChangeRow(pn); }

  Message msg("OptionsRedrawn");
  MESSAGEMAN->Broadcast(msg);
}

void ScreenOptions::SetOptionRowVisible(
    int row_index, bool visible, bool redraw) {
  if (row_index < 0 || row_index >= (int)rows_.size()) {
    LuaHelpers::ReportScriptErrorFmt(
        "SetOptionRowVisible: Row index %d is out of bounds", row_index);
    return;
  }

  OptionRow* opt_row = rows_[row_index];
  if (opt_row == nullptr) {
    LuaHelpers::ReportScriptErrorFmt(
        "SetOptionRowVisible: Row index %d is invalid", row_index);
    return;
  }

  opt_row->SetVisible(visible);

  OptionRowDefinition& def = opt_row->GetRowDef();
  if (visible) {
    def.hide_on_disable_ = false;
    if (def.enabled_for_players_.empty()) {
      FOREACH_PlayerNumber(pn) def.enabled_for_players_.insert(pn);
    }
  } else {
    def.hide_on_disable_ = true;
    def.enabled_for_players_.clear();
  }

  if (redraw) {
    RedrawOptions();
  }

  Message msg("OptionRowVisibilityChanged");
  msg.SetParam("RowIndex", row_index);
  msg.SetParam("Visible", visible);
  MESSAGEMAN->Broadcast(msg);
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to ScreenOptions. */
class LunaScreenOptions : public Luna<ScreenOptions> {
 public:
  static int AllAreOnLastRow(T* p, lua_State* L) {
    lua_pushboolean(L, p->AllAreOnLastRow());
    return 1;
  }
  static int FocusedItemEndsScreen(T* p, lua_State* L) {
    lua_pushboolean(
        L, p->FocusedItemEndsScreen(Enum::Check<PlayerNumber>(L, 1)));
    return 1;
  }
  static int GetCurrentRowIndex(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetCurrentRow(Enum::Check<PlayerNumber>(L, 1)));
    return 1;
  }
  static int GetOptionRow(T* p, lua_State* L) {
    int row_index = IArg(1);
    if (row_index < 0 || row_index >= p->GetNumRows()) {
      luaL_error(L, "Row index %d is invalid.", row_index);
    }
    OptionRow* opt_row = p->GetRow(row_index);
    if (opt_row) {
      opt_row->PushSelf(L);
    } else {
      lua_pushnil(L);
    }
    return 1;
  }
  static int SetOptionRowVisible(T* p, lua_State* L) {
    int row_index = IArg(1);
    bool visible = BArg(2);
    bool redraw = lua_isnone(L, 3) ? true : BArg(3);

    p->SetOptionRowVisible(row_index, visible, redraw);
    COMMON_RETURN_SELF;
  }
  static int RedrawOptions(T* p, lua_State* L) {
    p->RedrawOptions();
    COMMON_RETURN_SELF;
  }

  DEFINE_METHOD(GetNumRows, GetNumRows());

  static int SetOptionRowIndex(T* p, lua_State* L) {
    PlayerNumber pn = Enum::Check<PlayerNumber>(L, 1);
    int row_index = IArg(2);
    if (row_index < 0 || row_index >= p->GetNumRows()) {
      luaL_error(L, "Row index %d is invalid.", row_index);
    }
    p->MoveRowAbsolute(pn, row_index);
    COMMON_RETURN_SELF;
  }

  LunaScreenOptions() {
    ADD_METHOD(AllAreOnLastRow);
    ADD_METHOD(FocusedItemEndsScreen);
    ADD_METHOD(GetCurrentRowIndex);
    ADD_METHOD(GetOptionRow);
    ADD_METHOD(GetNumRows);
    ADD_METHOD(SetOptionRowVisible);
    ADD_METHOD(RedrawOptions);
    ADD_METHOD(SetOptionRowIndex);
  }
};

LUA_REGISTER_DERIVED_CLASS(ScreenOptions, ScreenWithMenuElements)
// lua end

/*
 * (c) 2001-2004 Chris Danford, Glenn Maynard
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
