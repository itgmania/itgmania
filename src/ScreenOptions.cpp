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

void ScreenOptions::InitMenu(const std::vector<OptionRowHandler*>& vHands) {
  LOG->Trace("ScreenOptions::InitMenu()");

  for (unsigned i = 0; i < rows_.size(); i++) {
    frame_container_.RemoveChild(rows_[i]);
    RageUtil::SafeDelete(rows_[i]);
  }
  rows_.clear();

  for (unsigned r = 0; r < vHands.size(); r++)  // foreach row
  {
    rows_.push_back(new OptionRow(&option_row_type_normal_));
    OptionRow& row = *rows_.back();
    row.SetDrawOrder(1);
    frame_container_.AddChild(&row);

    bool bFirstRowGoesDown = options_navigation_ == NAV_TOGGLE_THREE_KEY;

    row.LoadNormal(vHands[r], bFirstRowGoesDown);
  }

  if (SHOW_EXIT_ROW) {
    rows_.push_back(new OptionRow(&option_row_type_exit_));
    OptionRow& row = *rows_.back();
    row.LoadExit();
    row.SetDrawOrder(1);
    frame_container_.AddChild(&row);
  }

  frame_container_.SortByDrawOrder();

  int iIndex = 0;
  for (OptionRow* p : rows_) {
    Lua* L = LUA->Get();
    LuaHelpers::Push(L, iIndex++);
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
    OptionRow* pRow = rows_[r];
    pRow->Reload();

    this->ImportOptions(r, vpns);

    // HACK: Process disabled players, too, to hide inactive underlines.
    FOREACH_PlayerNumber(p) pRow->AfterImportOptions(p);
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

std::string ScreenOptions::GetExplanationText(int iRow) const {
  const OptionRow& row = *rows_[iRow];

  bool bAllowExplanation = row.GetRowDef().allow_explanation_;
  bool bShowExplanations = bAllowExplanation && SHOW_EXPLANATIONS.GetValue();
  if (!bShowExplanations) {
    return std::string();
  }

  std::string sExplanationName = row.GetRowDef().explanation_name_;
  if (sExplanationName.empty()) {
    sExplanationName = row.GetRowDef().name_;
  }
  ASSERT(!sExplanationName.empty());

  return OPTION_EXPLANATION(sExplanationName);
}

void ScreenOptions::GetWidthXY(
    PlayerNumber pn, int iRow, int iChoiceOnRow, int& iWidthOut, int& iXOut,
    int& iYOut) const {
  ASSERT_M(
      iRow < (int)rows_.size(), ssprintf("%i < %i", iRow, (int)rows_.size()));
  const OptionRow& row = *rows_[iRow];
  row.GetWidthXY(pn, iChoiceOnRow, iWidthOut, iXOut, iYOut);
}

void ScreenOptions::RefreshIcons(int iRow, PlayerNumber pn) {
  OptionRow& row = *rows_[iRow];

  const OptionRowDefinition& def = row.GetRowDef();

  // find first selection and whether multiple are selected
  int iFirstSelection = row.GetOneSelection(pn, true);

  // set icon name and bullet
  std::string sIcon;
  GameCommand gc;

  if (iFirstSelection == -1) {
    sIcon = "Multi";
  } else if (iFirstSelection != -1) {
    const OptionRowHandler* pHand = row.GetHandler();
    if (pHand) {
      int iSelection = iFirstSelection +
                       (options_navigation_ == NAV_TOGGLE_THREE_KEY ? -1 : 0);
      pHand->GetIconTextAndGameCommand(iSelection, sIcon, gc);
    }
  }

  // XXX: hack to not display text in the song options menu
  if (def.one_choice_for_all_players_) {
    sIcon = "";
  }

  rows_[iRow]->SetModIcon(pn, sIcon, gc);
}

void ScreenOptions::PositionCursor(PlayerNumber pn) {
  // Set the position of the cursor showing the current option the user is
  // changing.
  const int iRow = current_row_[pn];
  if (iRow == -1) {
    return;
  }

  ASSERT_M(
      iRow >= 0 && iRow < (int)rows_.size(),
      ssprintf("%i < %i", iRow, (int)rows_.size()));
  const OptionRow& row = *rows_[iRow];

  const int iChoiceWithFocus = row.GetChoiceInRowWithFocus(pn);
  if (iChoiceWithFocus == -1) {
    return;
  }

  int iWidth, iX, iY;
  GetWidthXY(pn, iRow, iChoiceWithFocus, iWidth, iX, iY);

  OptionsCursor& cursor = cursor_[pn];
  cursor.SetBarWidth(iWidth);
  cursor.SetXY((float)iX, (float)iY);
  bool bCanGoLeft = iChoiceWithFocus > 0;
  bool bCanGoRight =
      iChoiceWithFocus >= 0 &&
      iChoiceWithFocus < (int)row.GetRowDef().choices_.size() - 1;
  cursor.SetCanGo(bCanGoLeft, bCanGoRight);
}

void ScreenOptions::TweenCursor(PlayerNumber pn) {
  // Set the position of the cursor showing the current option the user is
  // changing.
  const int iRow = current_row_[pn];
  ASSERT_M(
      iRow >= 0 && iRow < (int)rows_.size(),
      ssprintf("%i < %i", iRow, (int)rows_.size()));

  const OptionRow& row = *rows_[iRow];
  const int iChoiceWithFocus = row.GetChoiceInRowWithFocus(pn);

  int iWidth, iX, iY;
  GetWidthXY(pn, iRow, iChoiceWithFocus, iWidth, iX, iY);

  OptionsCursor& cursor = cursor_[pn];
  if (cursor.GetDestX() != (float)iX || cursor.GetDestY() != (float)iY ||
      cursor.GetBarWidth() != iWidth) {
    cursor.StopTweening();
    cursor.BeginTweening(CURSOR_TWEEN_SECONDS);
    cursor.SetXY((float)iX, (float)iY);
    cursor.SetBarWidth(iWidth);
  }

  bool bCanGoLeft = false;
  bool bCanGoRight = false;
  switch (row.GetRowDef().layout_type_) {
    case LAYOUT_SHOW_ONE_IN_ROW:
      bCanGoLeft = iChoiceWithFocus > 0;
      bCanGoRight = iChoiceWithFocus >= 0 &&
                    iChoiceWithFocus < (int)row.GetRowDef().choices_.size() - 1;
      break;
    case LAYOUT_SHOW_ALL_IN_ROW:
      break;
    default:
      break;
  }
  cursor.SetCanGo(bCanGoLeft, bCanGoRight);

  if (GAMESTATE->IsHumanPlayer(pn)) {
    COMMAND(spr_line_highlight_[pn], "Change");
    if (row.GetRowType() == OptionRow::RowType_Exit) {
      COMMAND(spr_line_highlight_[pn], "ChangeToExit");
    }

    spr_line_highlight_[pn]->SetY((float)iY);
  }
}

void ScreenOptions::Update(float fDeltaTime) {
  // LOG->Trace( "ScreenOptions::Update(%f)", fDeltaTime );

  ScreenWithMenuElements::Update(fDeltaTime);
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

void ScreenOptions::HandleScreenMessage(const ScreenMessage SM) {
  if (SM == SM_MenuTimer) {
    this->BeginFadingOut();
  } else if (SM == SM_BeginFadingOut) {
    if (IsTransitioning()) {
      return;  // already transitioning
    }

    // If the selected option sets a screen, honor it.
    std::string sThisScreen =
        GetNextScreenForFocusedItem(GAMESTATE->GetMasterPlayerNumber());
    if (sThisScreen != "") {
      next_screen_ = sThisScreen;
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
  } else if (SM == SM_ExportOptions) {
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

  ScreenWithMenuElements::HandleScreenMessage(SM);
}

void ScreenOptions::PositionRows(bool bTween) {
  const int total = NUM_ROWS_SHOWN;
  const int halfsize = total / 2;

  int first_start, first_end, second_start, second_end;

  // Choices for each player. If only one player is active, it's the same for
  // both.
  int P1Choice = GAMESTATE->IsHumanPlayer(PLAYER_1) ? current_row_[PLAYER_1]
                                                    : current_row_[PLAYER_2];
  int P1VisibleChoice = 0;
  int P2Choice = GAMESTATE->IsHumanPlayer(PLAYER_2) ? current_row_[PLAYER_2]
                                                    : current_row_[PLAYER_1];
  int P2VisibleChoice = 0;
  std::vector<OptionRow*> Rows(rows_);
  OptionRow* pSeparateExitRow = nullptr;

  if ((bool)SEPARATE_EXIT_ROW && !Rows.empty() &&
      Rows.back()->GetRowType() == OptionRow::RowType_Exit) {
    pSeparateExitRow = Rows.back();

    // Remove the exit row for purposes of positioning everything else.
    if (P1Choice == (int)Rows.size() - 1) {
      --P1Choice;
    }
    if (P2Choice == (int)Rows.size() - 1) {
      --P2Choice;
    }

    Rows.pop_back();
  }

  std::vector<int> visibleRows;

  for (int i = 0; i < (int)Rows.size(); i++) {
    const OptionRowDefinition& def = Rows[i]->GetRowDef();
    // We only hide rows if HideOnDisable is true, and the row is not enabled
    // for any player. If HideOnDisable is false, we show all rows, even if
    // they are not enabled for any player. Disabled rows show up faded out if
    // they are not enabled
    if (!def.enabled_for_players_.empty() ||
        !Rows[i]->GetRowDef().hide_on_disable_) {
      visibleRows.push_back(i);
    }
  }

  const bool bTreatAllRowsVisible = visibleRows.empty();
  if (bTreatAllRowsVisible) {
    // If no rows have enabled players, treat all rows as
    // visible for layout. The EditMode help screen relies on this behavior
    // to render a view-only menu.
    // TODO: Provide a better way to specify view-only menus, and remove this
    // fallback.
    for (int i = 0; i < (int)Rows.size(); i++) {
      visibleRows.push_back(i);
    }
  }

  bool bFoundP1 = false;
  bool bFoundP2 = false;
  for (int vi = 0; vi < (int)visibleRows.size(); vi++) {
    if (visibleRows[vi] == P1Choice) {
      P1VisibleChoice = vi;
      bFoundP1 = true;
    }
    if (visibleRows[vi] == P2Choice) {
      P2VisibleChoice = vi;
      bFoundP2 = true;
    }
  }

  if (!visibleRows.empty()) {
    if (!bFoundP1) {
      if (P1Choice <= visibleRows.front()) {
        P1VisibleChoice = 0;
      } else {
        P1VisibleChoice = (int)visibleRows.size() - 1;
      }
    }
    if (!bFoundP2) {
      if (P2Choice <= visibleRows.front()) {
        P2VisibleChoice = 0;
      } else {
        P2VisibleChoice = (int)visibleRows.size() - 1;
      }
    }
  }

  const int visibleCount = (int)visibleRows.size();

  const bool BothPlayersActivated =
      GAMESTATE->IsHumanPlayer(PLAYER_1) && GAMESTATE->IsHumanPlayer(PLAYER_2);
  if (input_mode_ == INPUTMODE_SHARE_CURSOR || !BothPlayersActivated) {
    // Simply center the cursor.
    first_start = std::max(P1VisibleChoice - halfsize, 0);
    first_end = first_start + total;
    second_start = second_end = first_end;
  } else {
    // First half:
    const int earliest = std::min(P1VisibleChoice, P2VisibleChoice);
    first_start = std::max(earliest - halfsize / 2, 0);
    first_end = first_start + halfsize;

    // Second half:
    const int latest = std::max(P1VisibleChoice, P2VisibleChoice);

    second_start = std::max(latest - halfsize / 2, 0);

    // Don't overlap.
    second_start = std::max(second_start, first_end);

    second_end = second_start + halfsize;
  }

  first_end = std::min(first_end, visibleCount);
  second_end = std::min(second_end, visibleCount);

  /* If less than total (and Rows.size()) are displayed, fill in the empty
   * space intelligently. */
  for (;;) {
    const int sum = (first_end - first_start) + (second_end - second_start);
    if (sum >= visibleCount || sum >= total) {
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
    } else if (second_end < visibleCount) {
      second_end++;
    } else {
      FAIL_M("Do we have room to grow or don't we?");
    }
  }

  int pos = 0;
  int visibleIndex = 0;
  for (int i = 0; i < (int)Rows.size(); i++)  // foreach row
  {
    OptionRow& row = *Rows[i];
    const OptionRowDefinition& def = row.GetRowDef();

    const bool bRowEnabled = !def.enabled_for_players_.empty();
    const bool bHideDisabled = def.hide_on_disable_;
    const bool bRowVisible =
        bRowEnabled || !bHideDisabled || bTreatAllRowsVisible;
    int thisVisibleIndex = -1;
    if (bRowVisible) {
      thisVisibleIndex = visibleIndex;
      visibleIndex++;
    }

    float fPos = (float)pos;

    if (bRowVisible && thisVisibleIndex < first_start) {
      fPos = -0.5f;
    } else if (
        bRowVisible && thisVisibleIndex >= first_end &&
        thisVisibleIndex < second_start) {
      fPos = ((int)NUM_ROWS_SHOWN) / 2 - 0.5f;
    } else if (bRowVisible && thisVisibleIndex >= second_end) {
      fPos = ((int)NUM_ROWS_SHOWN) - 0.5f;
    }

    Actor::TweenState tsDestination =
        expr_row_position_transform_function_.GetTransformCached(
            fPos, i, std::min((int)Rows.size(), (int)NUM_ROWS_SHOWN));

    bool bHidden = !bRowVisible;
    if (bRowVisible) {
      bHidden =
          thisVisibleIndex < first_start ||
          (thisVisibleIndex >= first_end && thisVisibleIndex < second_start) ||
          thisVisibleIndex >= second_end;
    }
    if (!bRowEnabled && bHideDisabled) {
      bHidden = true;
      row.SetVisible(false);
    } else {
      row.SetVisible(true);
    }
    for (int j = 0; j < NUM_DIFFUSE_COLORS; j++) {
      tsDestination.diffuse_[j].a_ = bHidden ? 0.0f : 1.0f;
    }
    if (!bHidden) {
      pos++;
    }
    row.SetDestination(tsDestination, bTween);
  }

  if (pSeparateExitRow) {
    Actor::TweenState tsDestination;
    tsDestination.Init();
    tsDestination.pos_.y_ = SEPARATE_EXIT_ROW_Y;

    for (int j = 0; j < NUM_DIFFUSE_COLORS; j++) {
      tsDestination.diffuse_[j].a_ = 1.0f;
    }
    pSeparateExitRow->SetDestination(tsDestination, bTween);
  }
}

void ScreenOptions::AfterChangeValueOrRow(PlayerNumber pn) {
  if (!GAMESTATE->IsHumanPlayer(pn)) {
    return;
  }

  const int iCurRow = current_row_[pn];

  if (iCurRow == -1) {
    return;
  }

  // Update m_fY and m_bHidden[].
  PositionRows(true);

  // Do positioning.
  RefreshIcons(iCurRow, pn);
  for (unsigned r = 0; r < rows_.size(); r++) {
    /* After changing a value, position underlines. Do this for both players,
     * since underlines for both players will change with
     * m_bOneChoiceForAllPlayers. */
    FOREACH_HumanPlayer(p) rows_[r]->PositionUnderlines(p);
    rows_[r]->PositionIcons(pn);
    rows_[r]->SetRowHasFocus(
        pn, GAMESTATE->IsHumanPlayer(pn) && iCurRow == (int)r);
    rows_[r]->UpdateEnabledDisabled();
  }

  if (SHOW_SCROLL_BAR) {
    float fPercent = 0;
    if (rows_.size() > 1) {
      fPercent = iCurRow / float(rows_.size() - 1);
    }
    scroll_bar_.SetPercentage(pn, fPercent);
  }

  // Update all players, since changing one player can move both cursors.
  FOREACH_HumanPlayer(p) TweenCursor(p);

  FOREACH_PlayerNumber(p) {
    OptionRow& row = *rows_[iCurRow];
    const bool bExitSelected = row.GetRowType() == OptionRow::RowType_Exit;
    if (GAMESTATE->GetNumHumanPlayers() != 1 && p != pn) {
      continue;
    }
    if (was_on_exit_[p] != bExitSelected) {
      was_on_exit_[p] = bExitSelected;
      COMMAND(
          spr_more_,
          ssprintf(
              "Exit%sP%i", bExitSelected ? "Selected" : "Unselected", p + 1));
      spr_more_->PlayCommand(bExitSelected ? "GainFocus" : "LoseFocus");
    }
  }

  const std::string text = GetExplanationText(iCurRow);
  BitmapText* pText = nullptr;
  switch (input_mode_) {
    case INPUTMODE_INDIVIDUAL:
      pText = &text_explanation_[pn];
      break;
    case INPUTMODE_SHARE_CURSOR:
      pText = &text_explanation_together_;
      break;
    default:
      break;
  }
  if (pText->GetText() != text) {
    pText->FinishTweening();
    ON_COMMAND(pText);
    pText->SetText(text);
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
      bool bHoldingLeftAndRight =
          INPUTMAPPER->IsBeingPressed(GAME_BUTTON_RIGHT, pn) &&
          INPUTMAPPER->IsBeingPressed(GAME_BUTTON_LEFT, pn);
      if (bHoldingLeftAndRight) {
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

  int iCurRow = current_row_[pn];

  if (iCurRow < 0) {
    // this shouldn't be happening, but it is, so we need to bail out. -aj
    sound_start_.PlayCopy(true);
    this->BeginFadingOut();
    return;
  }

  OptionRow& row = *rows_[iCurRow];

  if (options_navigation_ == NAV_THREE_KEY_MENU &&
      row.GetRowType() != OptionRow::RowType_Exit) {
    /* In NAV_THREE_KEY_MENU mode, if a row doesn't set a screen, it does
     * something.  Apply it now, and don't go to the next screen. */
    if (!FocusedItemEndsScreen(input.pn_)) {
      std::vector<PlayerNumber> vpns;
      vpns.push_back(input.pn_);
      ExportOptions(iCurRow, vpns);
      return;
    }
  }

  // Check whether Start ends this screen.
  {
    bool bEndThisScreen = false;

    // If we didn't apply and return above in NAV_THREE_KEY_MENU, then the
    // selection sets a screen.
    if (options_navigation_ == NAV_THREE_KEY_MENU) {
      bEndThisScreen = true;
    }

    // If there's no exit row, then pressing Start on any row ends the screen.
    if (!SHOW_EXIT_ROW) {
      bEndThisScreen = true;
    }

    // If all players are on "Exit"
    if (AllAreOnLastRow()) {
      bEndThisScreen = true;
    }

    // Don't accept START to go to the next screen if we're still transitioning
    // in.
    if (bEndThisScreen &&
        (input.type_ != IET_FIRST_PRESS || IsTransitioning())) {
      return;
    }

    if (bEndThisScreen) {
      sound_start_.PlayCopy(true);
      this->BeginFadingOut();
      return;
    }
  }

  if (row.GetFirstItemGoesDown()) {
    int iChoiceInRow = row.GetChoiceInRowWithFocus(pn);
    if (iChoiceInRow == 0) {
      MenuDown(input);
      return;
    }
  }

  if (row.GetRowDef().select_type_ == SELECT_MULTIPLE) {
    int iChoiceInRow = row.GetChoiceInRowWithFocus(pn);
    bool bSelected = !row.GetSelected(pn, iChoiceInRow);
    bool changed = row.SetSelected(pn, iChoiceInRow, bSelected);
    if (changed) {
      AfterChangeValueOrRow(pn);
    }

    if (bSelected) {
      sound_toggle_on_.Play(true);
    } else {
      sound_toggle_off_.Play(true);
    }

    rows_[iCurRow]->PositionUnderlines(pn);
    RefreshIcons(iCurRow, pn);

    Message msg("SelectMultiple");
    msg.SetParam("PlayerNumber", pn);
    msg.SetParam("RowIndex", iCurRow);
    msg.SetParam("ChoiceInRow", iChoiceInRow);
    msg.SetParam("Selected", bSelected);
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
        if (iCurRow == (int)rows_.size() - 1) {
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
        int iChoiceInRow = row.GetChoiceInRowWithFocus(pn);
        if (row.GetRowDef().one_choice_for_all_players_) {
          row.SetOneSharedSelection(iChoiceInRow);
        } else {
          row.SetOneSelection(pn, iChoiceInRow);
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
  int iCurrentRow = current_row_[pn];
  const OptionRow& row = *rows_[iCurrentRow];
  if (row.GetRowDef().layout_type_ == LAYOUT_SHOW_ONE_IN_ROW) {
    return;
  }

  int iWidth, iY;
  GetWidthXY(
      pn, current_row_[pn], row.GetChoiceInRowWithFocus(pn), iWidth,
      focus_x_[pn], iY);
  LOG->Trace(
      "cur selection %ix%i @ %i", current_row_[pn],
      row.GetChoiceInRowWithFocus(pn), focus_x_[pn]);
}

bool ScreenOptions::FocusedItemEndsScreen(PlayerNumber pn) const {
  std::string sScreen = GetNextScreenForFocusedItem(pn);
  return !sScreen.empty();
}

std::string ScreenOptions::GetNextScreenForFocusedItem(PlayerNumber pn) const {
  int iCurRow = this->GetCurrentRow(pn);

  if (iCurRow == -1) {
    return std::string();
  }

  ASSERT(iCurRow >= 0 && iCurRow < (int)rows_.size());
  const OptionRow* pRow = rows_[iCurRow];

  int iChoice = pRow->GetChoiceInRowWithFocus(pn);
  if (pRow->GetFirstItemGoesDown()) {
    iChoice--;
  }

  // not the "goes down" item
  if (iChoice == -1) {
    return std::string();
  }

  const OptionRowHandler* pHand = pRow->GetHandler();
  if (pHand == nullptr) {
    return std::string();
  }
  return pHand->GetScreen(iChoice);
}

void ScreenOptions::BeginFadingOut() {
  this->PostScreenMessage(SM_BeginFadingOut, 0);
}

// Left/right
void ScreenOptions::ChangeValueInRowAbsolute(
    int iRow, PlayerNumber pn, int iChoiceIndex, bool bRepeat) {
  if (iRow == -1) {  // no row selected
    return;          // don't allow a move
  }

  OptionRow& row = *rows_[iRow];

  const int iNumChoices = row.GetRowDef().choices_.size();
  ASSERT(iNumChoices >= 0 && iChoiceIndex < iNumChoices);

  if (bRepeat && !ALLOW_REPEATING_CHANGE_VALUE_INPUT) {
    return;
  }

  int iCurrentChoiceWithFocus = row.GetChoiceInRowWithFocus(pn);
  bool bOneChanged = (iCurrentChoiceWithFocus != iChoiceIndex);

  Message msg("ChangeValue");
  msg.SetParam("PlayerNumber", pn);
  msg.SetParam("RowIndex", iRow);
  MESSAGEMAN->Broadcast(msg);

  row.SetChoiceInRowWithFocus(pn, iChoiceIndex);
  StoreFocus(pn);

  if (row.GetRowDef().one_choice_for_all_players_) {
    /* If this row is bOneChoiceForAllPlayers, then lock the cursors together
     * for this row. Don't do this in toggle modes, since the current selection
     * and the current focus are detached. */
    bool bForceFocusedChoiceTogether = false;
    if (options_navigation_ != NAV_TOGGLE_THREE_KEY &&
        options_navigation_ != NAV_TOGGLE_FIVE_KEY &&
        row.GetRowDef().one_choice_for_all_players_) {
      bForceFocusedChoiceTogether = true;
    }

    // Also lock focus if the screen is explicitly set to share cursors.
    if (input_mode_ == INPUTMODE_SHARE_CURSOR) {
      bForceFocusedChoiceTogether = true;
    }

    if (bForceFocusedChoiceTogether) {
      // lock focus together
      FOREACH_HumanPlayer(p) {
        row.SetChoiceInRowWithFocus(p, iChoiceIndex);
        StoreFocus(p);
      }
    }
  }

  FOREACH_PlayerNumber(p) {
    if (!row.GetRowDef().one_choice_for_all_players_ && p != pn) {
      continue;
    }

    // Don't auto-select the ▼ on value-arrow rows.
    if (iChoiceIndex == 0 && row.IsShowOneValueWithGoDown()) {
      continue;
    }

    // Value-arrow rows auto-select on Left/Right even in toggle mode.
    bool bAutoSelect = options_navigation_ != NAV_TOGGLE_THREE_KEY ||
                       row.IsShowOneValueWithGoDown();

    if (row.GetRowDef().select_type_ == SELECT_ONE && bAutoSelect) {
      row.SetOneSelection(p, iChoiceIndex);
    }
  }

  if (bOneChanged) {
    sound_change_col_.Play(true);
  }

  if (row.GetRowDef().export_on_change_) {
    std::vector<PlayerNumber> vpns;
    FOREACH_HumanPlayer(p) vpns.push_back(p);
    ExportOptions(iRow, vpns);
  }

  this->AfterChangeValueInRow(iRow, pn);
}

void ScreenOptions::ChangeValueInRowRelative(
    int iRow, PlayerNumber pn, int iDelta, bool bRepeat) {
  if (iRow == -1) {  // no row selected
    return;          // don't allow a move
  }

  OptionRow& row = *rows_[iRow];

  const int iNumChoices = row.GetRowDef().choices_.size();

  if (options_navigation_ == NAV_THREE_KEY_MENU && iNumChoices <= 1)  // 1 or 0
  {
    /* There are no other options on the row; move up or down instead of
     * left and right. This allows navigating the options menu with
     * left/right/start.
     * XXX: Only allow repeats if the opposite key isn't pressed; otherwise,
     * holding both directions will repeat in place continuously, which is
     * weird. */
    if (MoveRowRelative(pn, iDelta, bRepeat)) {
      if (iDelta < 0) {
        sound_prev_row_.Play(true);
      } else {
        sound_next_row_.Play(true);
      }
    }
    return;
  }

  if (iNumChoices <= 1) {  // nowhere to move
    return;
  }

  int iCurrentChoiceWithFocus = row.GetChoiceInRowWithFocus(pn);

  // Value-arrow rows confine Left/Right to value choices [1, N-1]; the ▼
  // (choice 0) is reached only via the Start key.
  bool bConfineToValues = row.IsShowOneValueWithGoDown();

  int iNewChoiceWithFocus;
  if (bConfineToValues && iCurrentChoiceWithFocus == 0) {
    // Leaving the ▼; hover the current value without changing it.
    int iSelected = row.GetOneSelection(pn, true);
    iNewChoiceWithFocus = (iSelected >= 1) ? iSelected : 1;
  } else {
    int lo = bConfineToValues ? 1 : 0;
    int hi = iNumChoices - 1;
    iNewChoiceWithFocus = iCurrentChoiceWithFocus + iDelta;
    if (!bRepeat && WRAP_VALUE_IN_ROW.GetValue()) {
      if (bConfineToValues) {
        if (iNewChoiceWithFocus < lo) {
          iNewChoiceWithFocus = hi;
        } else if (iNewChoiceWithFocus > hi) {
          iNewChoiceWithFocus = lo;
        }
      } else {
        wrap(iNewChoiceWithFocus, iNumChoices);
      }
    } else {
      rage_clamp(iNewChoiceWithFocus, lo, hi);
    }
  }

  ChangeValueInRowAbsolute(iRow, pn, iNewChoiceWithFocus, bRepeat);
}

void ScreenOptions::AfterChangeValueInRow(int iRow, PlayerNumber pn) {
  AfterChangeValueOrRow(pn);
}

// Move up/down. Returns true if we actually moved.
bool ScreenOptions::MoveRowRelative(PlayerNumber pn, int iDir, bool bRepeat) {
  // LOG->Trace( "MoveRowRelative(pn %i, dir %i, rep %i)", pn, iDir, bRepeat );

  int iDest = -1;
  ASSERT(rows_.size() != 0);
  for (int r = 1; r < (int)rows_.size(); r++) {
    int iDelta = r * iDir;
    iDest = current_row_[pn] + iDelta;
    wrap(iDest, rows_.size());

    OptionRow& row = *rows_[iDest];
    if (row.GetRowDef().IsEnabledForPlayer(pn)) {
      break;
    }

    iDest = -1;
  }

  if (iDest == -1) {
    return false;
  }
  if (bRepeat) {
    // Don't wrap on repeating inputs.
    if (iDir > 0 && iDest < current_row_[pn]) {
      return false;
    }
    if (iDir < 0 && iDest > current_row_[pn]) {
      return false;
    }
  }

  return MoveRowAbsolute(pn, iDest);
}

void ScreenOptions::AfterChangeRow(PlayerNumber pn) {
  const int iRow = current_row_[pn];
  if (iRow != -1) {
    OptionRow& row = *rows_[iRow];
    if (options_navigation_ == NAV_TOGGLE_FIVE_KEY) {
      if (row.GetRowDef().select_type_ == SELECT_ONE) {
        // For SELECT_ONE, keep focus on selected choice.
        int iSelection = row.GetOneSelection(pn, true);
        if (iSelection != -1) {
          row.SetChoiceInRowWithFocus(pn, iSelection);
        }
      } else if (row.GetRowDef().layout_type_ != LAYOUT_SHOW_ONE_IN_ROW) {
        // Else keep the selection in the row near the focus.
        int iSelectionDist = -1;
        for (unsigned i = 0; i < row.GetTextItemsSize(); ++i) {
          int iWidth, iX, iY;
          GetWidthXY(pn, current_row_[pn], i, iWidth, iX, iY);
          const int iDist = std::abs(iX - focus_x_[pn]);
          if (iSelectionDist == -1 || iDist < iSelectionDist) {
            iSelectionDist = iDist;
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

bool ScreenOptions::MoveRowAbsolute(PlayerNumber pn, int iRow) {
  bool bChanged = false;
  FOREACH_PlayerNumber(p) {
    if (input_mode_ == INPUTMODE_INDIVIDUAL && p != pn) {
      continue;  // skip
    }

    if (current_row_[p] == iRow) {
      continue;
    }

    current_row_[p] = iRow;

    AfterChangeRow(p);
    bChanged = true;

    const OptionRow& row = *rows_[iRow];
    Message msg("ChangeRow");
    msg.SetParam("PlayerNumber", p);
    msg.SetParam("RowIndex", GetCurrentRow(p));
    msg.SetParam("ChangedToExit", row.GetRowType() == OptionRow::RowType_Exit);
    MESSAGEMAN->Broadcast(msg);
  }

  return bChanged;
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

void ScreenOptions::MenuUpDown(const InputEventPlus& input, int iDir) {
  ASSERT(iDir == -1 || iDir == +1);
  PlayerNumber pn = input.pn_;

  if (input.type_ == IET_REPEAT) {
    /* If down is pressed, don't allow up to repeat, and vice versa. This
     * prevents holding both up and down from toggling repeatedly in-place. */
    if (iDir == +1) {
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

  if (MoveRowRelative(pn, iDir, input.type_ != IET_FIRST_PRESS)) {
    if (iDir < 0) {
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
    int row_index, bool visible, bool bRedraw) {
  if (row_index < 0 || row_index >= (int)rows_.size()) {
    LuaHelpers::ReportScriptErrorFmt(
        "SetOptionRowVisible: Row index %d is out of bounds", row_index);
    return;
  }

  OptionRow* pOptRow = rows_[row_index];
  if (pOptRow == nullptr) {
    LuaHelpers::ReportScriptErrorFmt(
        "SetOptionRowVisible: Row index %d is invalid", row_index);
    return;
  }

  pOptRow->SetVisible(visible);

  OptionRowDefinition& def = pOptRow->GetRowDef();
  if (visible) {
    def.hide_on_disable_ = false;
    if (def.enabled_for_players_.empty()) {
      FOREACH_PlayerNumber(pn) def.enabled_for_players_.insert(pn);
    }
  } else {
    def.hide_on_disable_ = true;
    def.enabled_for_players_.clear();
  }

  if (bRedraw) {
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
    OptionRow* pOptRow = p->GetRow(row_index);
    if (pOptRow) {
      pOptRow->PushSelf(L);
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
