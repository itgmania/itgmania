#include "ScreenSelectMaster.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "ActorScroller.h"
#include "ActorUtil.h"
#include "AnnouncerManager.h"
#include "AutoActor.h"
#include "EnumHelper.h"
#include "GameCommand.h"
#include "GameInput.h"
#include "GameSoundManager.h"
#include "GameState.h"
#include "InputEventPlus.h"
#include "InputFilter.h"
#include "LuaManager.h"
#include "MessageManager.h"
#include "PlayerNumber.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "Screen.h"
#include "ScreenManager.h"
#include "ScreenMessage.h"
#include "ScreenSelect.h"
#include "ScreenWithMenuElements.h"
#include "StdString.h"
#include "ThemeManager.h"
#include "global.h"

static const char* MenuDirNames[] = {
    "Up", "Down", "Left", "Right", "Auto",
};
XToString(MenuDir);

AutoScreenMessage(SM_PlayPostSwitchPage);

static std::string CURSOR_OFFSET_X_FROM_ICON_NAME(size_t p) {
  return ssprintf("CursorP%dOffsetXFromIcon", int(p + 1));
}
static std::string CURSOR_OFFSET_Y_FROM_ICON_NAME(size_t p) {
  return ssprintf("CursorP%dOffsetYFromIcon", int(p + 1));
}
// e.g. "OptionOrderLeft=0:1,1:2,2:3,3:4"
static std::string OPTION_ORDER_NAME(size_t dir) {
  return "OptionOrder" + MenuDirToString((MenuDir)dir);
}

REGISTER_SCREEN_CLASS(ScreenSelectMaster);

#define GetActiveElementPlayerNumbers(vpns)   \
  if (SHARED_SELECTION) {                     \
    vpns.push_back(PLAYER_1);                 \
  } else {                                    \
    FOREACH_HumanPlayer(p) vpns.push_back(p); \
  }

ScreenSelectMaster::ScreenSelectMaster() {
  ZERO(choice_);
  ZERO(chosen_);
  ZERO(double_choice_);
}

void ScreenSelectMaster::Init() {
  SHOW_ICON.Load(name_, "ShowIcon");
  SHOW_SCROLLER.Load(name_, "ShowScroller");
  SHOW_CURSOR.Load(name_, "ShowCursor");
  SHARED_SELECTION.Load(name_, "SharedSelection");
  USE_ICON_METRICS.Load(name_, "UseIconMetrics");
  NUM_CHOICES_ON_PAGE_1.Load(name_, "NumChoicesOnPage1");
  CURSOR_OFFSET_X_FROM_ICON.Load(
      name_, CURSOR_OFFSET_X_FROM_ICON_NAME, NUM_PLAYERS);
  CURSOR_OFFSET_Y_FROM_ICON.Load(
      name_, CURSOR_OFFSET_Y_FROM_ICON_NAME, NUM_PLAYERS);
  PER_CHOICE_ICON_ELEMENT.Load(name_, "PerChoiceIconElement");
  PRE_SWITCH_PAGE_SECONDS.Load(name_, "PreSwitchPageSeconds");
  POST_SWITCH_PAGE_SECONDS.Load(name_, "PostSwitchPageSeconds");
  OPTION_ORDER.Load(name_, OPTION_ORDER_NAME, NUM_MenuDir);
  DO_SWITCH_ANYWAYS.Load(name_, "DoSwitchAnyways");

  WRAP_CURSOR.Load(name_, "WrapCursor");
  WRAP_SCROLLER.Load(name_, "WrapScroller");
  LOOP_SCROLLER.Load(name_, "LoopScroller");
  PER_CHOICE_SCROLL_ELEMENT.Load(name_, "PerChoiceScrollElement");
  ALLOW_REPEATING_INPUT.Load(name_, "AllowRepeatingInput");
  SCROLLER_SECONDS_PER_ITEM.Load(name_, "ScrollerSecondsPerItem");
  SCROLLER_NUM_ITEMS_TO_DRAW.Load(name_, "ScrollerNumItemsToDraw");
  SCROLLER_TRANSFORM.Load(name_, "ScrollerTransform");
  // SCROLLER_TWEEN.Load( m_sName, "ScrollerTween" );
  SCROLLER_SUBDIVISIONS.Load(name_, "ScrollerSubdivisions");
  DEFAULT_CHOICE.Load(name_, "DefaultChoice");
  DOUBLE_PRESS_TO_SELECT.Load(name_, "DoublePressToSelect");

  ScreenSelect::Init();

  tracking_repeating_input_ = GameButton_Invalid;

  std::vector<PlayerNumber> vpns;
  GetActiveElementPlayerNumbers(vpns);

#define PLAYER_APPEND_NO_SPACE(p) \
  (SHARED_SELECTION ? std::string() : ssprintf("P%d", (p) + 1))
  this->SubscribeToMessage(SM_MenuTimer);

  // init cursor
  if (SHOW_CURSOR) {
    for (const PlayerNumber& p : vpns) {
      std::string element = "Cursor" + PLAYER_APPEND_NO_SPACE(p);
      spr_cursor_[p].Load(THEME->GetPathG(name_, element));
      Replace(element, " ", "");
      spr_cursor_[p]->SetName(element);
      this->AddChild(spr_cursor_[p]);
      LOAD_ALL_COMMANDS(spr_cursor_[p]);
    }
  }

  // Resize vectors depending on how many choices there are
  spr_icon_.resize(game_commands_.size());
  for (const PlayerNumber& p : vpns) {
    spr_scroll_[p].resize(game_commands_.size());
  }

  std::vector<RageVector3> positions;
  bool positions_set_by_lua = false;
  if (THEME->HasMetric(name_, "IconChoicePosFunction")) {
    positions_set_by_lua = true;
    LuaReference command = THEME->GetMetricR(name_, "IconChoicePosFunction");
    if (command.GetLuaType() != LUA_TFUNCTION) {
      LuaHelpers::ReportScriptError(
          name_ + "::IconChoicePosFunction must be a function.");
      positions_set_by_lua = false;
    } else {
      Lua* L = LUA->Get();
      command.PushSelf(L);
      lua_pushnumber(L, game_commands_.size());
      std::string err = name_ + "::IconChoicePosFunction: ";
      if (!LuaHelpers::RunScriptOnStack(L, err, 1, 1, true)) {
        positions_set_by_lua = false;
      } else {
        if (!lua_istable(L, -1)) {
          LuaHelpers::ReportScriptError(
              name_ +
              "::IconChoicePosFunction did not return a table of positions.");
          positions_set_by_lua = false;
        } else {
          size_t poses = lua_objlen(L, -1);
          for (size_t p = 1; p <= poses; ++p) {
            lua_rawgeti(L, -1, p);
            RageVector3 pos(0.0f, 0.0f, 0.0f);
            if (!lua_istable(L, -1)) {
              LuaHelpers::ReportScriptErrorFmt(
                  "Position %zu is not a table.", p);
            } else {
#define SET_POS_PART(i, part)     \
  lua_rawgeti(L, -1, i);          \
  pos.part = lua_tonumber(L, -1); \
  lua_pop(L, 1);
              // If part of the position is not provided, we want it to
              // default to zero, which lua_tonumber does. -Kyz
              SET_POS_PART(1, x_);
              SET_POS_PART(2, y_);
              SET_POS_PART(3, z_);
#undef SET_POS_PART
            }
            lua_pop(L, 1);
            positions.push_back(pos);
          }
        }
      }
      lua_settop(L, 0);
      LUA->Release(L);
    }
  }

  for (unsigned c = 0; c < game_commands_.size(); c++) {
    GameCommand& mc = game_commands_[c];

    LuaThreadVariable var("GameCommand", LuaReference::Create(&mc));

    // init icon
    if (SHOW_ICON) {
      std::vector<std::string> vs;
      vs.push_back("Icon");
      if (PER_CHOICE_ICON_ELEMENT) {
        vs.push_back("Choice" + mc.name_);
      }
      std::string element = join(" ", vs);
      spr_icon_[c].Load(THEME->GetPathG(name_, element));
      std::string name =
          "Icon"
          "Choice" +
          mc.name_;
      spr_icon_[c]->SetName(name);
      if (USE_ICON_METRICS) {
        if (positions_set_by_lua) {
          LOAD_ALL_COMMANDS(spr_icon_[c]);
          spr_icon_[c]->SetXY(positions[c].x_, positions[c].y_);
          spr_icon_[c]->SetZ(positions[c].z_);
        } else {
          LOAD_ALL_COMMANDS_AND_SET_XY(spr_icon_[c]);
        }
#define OPTIONAL_COMMAND(onoff)                                                \
  if (THEME->HasMetric(name_, "IconChoice" onoff "Command")) {                 \
    spr_icon_[c]->AddCommand(                                                  \
        onoff, THEME->GetMetricA(name_, "IconChoice" onoff "Command"), false); \
  }
        OPTIONAL_COMMAND("On");
        OPTIONAL_COMMAND("Off");
#undef OPTIONAL_COMMAND
      }
      this->AddChild(spr_icon_[c]);
    }

    // init scroll
    if (SHOW_SCROLLER) {
      for (const PlayerNumber& p : vpns) {
        std::vector<std::string> vs;
        vs.push_back("Scroll");
        if (PER_CHOICE_SCROLL_ELEMENT) {
          vs.push_back("Choice" + mc.name_);
        }
        if (!SHARED_SELECTION) {
          vs.push_back(PLAYER_APPEND_NO_SPACE(p));
        }
        std::string element = join(" ", vs);
        spr_scroll_[p][c].Load(THEME->GetPathG(name_, element));
        std::string name =
            "Scroll"
            "Choice" +
            mc.name_;
        if (!SHARED_SELECTION) {
          name += PLAYER_APPEND_NO_SPACE(p);
        }
        spr_scroll_[p][c]->SetName(name);
        scroller_[p].AddChild(spr_scroll_[p][c]);
      }
    }
  }

  // init scroll
  if (SHOW_SCROLLER) {
    for (const PlayerNumber& p : vpns) {
      scroller_[p].SetLoop(LOOP_SCROLLER);
      scroller_[p].SetNumItemsToDraw(SCROLLER_NUM_ITEMS_TO_DRAW);
      scroller_[p].Load2();
      scroller_[p].SetTransformFromReference(SCROLLER_TRANSFORM);
      scroller_[p].SetSecondsPerItem(SCROLLER_SECONDS_PER_ITEM);
      scroller_[p].SetNumSubdivisions(SCROLLER_SUBDIVISIONS);
      scroller_[p].SetName("Scroller" + PLAYER_APPEND_NO_SPACE(p));
      LOAD_ALL_COMMANDS_AND_SET_XY(scroller_[p]);
      this->AddChild(&scroller_[p]);
    }
  }

  FOREACH_ENUM(Page, page) {
    spr_more_[page].Load(
        THEME->GetPathG(name_, ssprintf("more page%d", page + 1)));
    spr_more_[page]->SetName(ssprintf("MorePage%d", page + 1));
    LOAD_ALL_COMMANDS_AND_SET_XY(spr_more_[page]);
    this->AddChild(spr_more_[page]);

    spr_explanation_[page].Load(
        THEME->GetPathG(name_, ssprintf("explanation page%d", page + 1)));
    spr_explanation_[page]->SetName(ssprintf("ExplanationPage%d", page + 1));
    LOAD_ALL_COMMANDS_AND_SET_XY(spr_explanation_[page]);
    this->AddChild(spr_explanation_[page]);
  }

  sound_change_.Load(THEME->GetPathS(name_, "change"), true);
  sound_difficult_.Load(ANNOUNCER->GetPathTo("select difficulty challenge"));
  sound_start_.Load(THEME->GetPathS(name_, "start"));

  // init m_Next order info
  FOREACH_MenuDir(dir) {
    const std::string order = OPTION_ORDER.GetValue(dir);
    std::vector<std::string> parts;
    split(order, ",", parts, true);

    for (unsigned part = 0; part < parts.size(); ++part) {
      int from, to;
      if (sscanf(parts[part].c_str(), "%d:%d", &from, &to) != 2) {
        LuaHelpers::ReportScriptErrorFmt(
            "%s::OptionOrder%s parse error", name_.c_str(),
            MenuDirToString(dir).c_str());
        continue;
      }

      --from;
      --to;

      map_current_choice_to_next_choice_[dir][from] = to;
    }

    if (map_current_choice_to_next_choice_[dir]
            .empty())  // Didn't specify any mappings
    {
      // Fill with reasonable defaults
      for (unsigned c = 0; c < game_commands_.size(); ++c) {
        int add;
        switch (dir) {
          case MenuDir_Up:
          case MenuDir_Left:
            add = -1;
            break;
          default:
            add = +1;
            break;
        }

        map_current_choice_to_next_choice_[dir][c] = c + add;
        // Always wrap around MenuDir_Auto.
        if (dir == MenuDir_Auto || (bool)WRAP_CURSOR) {
          wrap(
              map_current_choice_to_next_choice_[dir][c],
              game_commands_.size());
        } else {
          map_current_choice_to_next_choice_[dir][c] = std::clamp(
              map_current_choice_to_next_choice_[dir][c], 0,
              (int)game_commands_.size() - 1);
        }
      }
    }
  }

  double_choice_no_sound_ = false;
}

void ScreenSelectMaster::BeginScreen() {
  // TODO: Move default choice to ScreenSelect
  int default_choice = -1;
  for (unsigned c = 0; c < game_commands_.size(); c++) {
    const GameCommand& mc = game_commands_[c];
    if (mc.name_ == (std::string)DEFAULT_CHOICE) {
      default_choice = c;
      break;
    }
  }

  FOREACH_PlayerNumber(p) {
    choice_[p] = (default_choice != -1) ? default_choice : 0;
    rage_clamp(choice_[p], 0, (int)game_commands_.size() - 1);
    chosen_[p] = false;
    double_choice_[p] = false;
  }
  if (!SHARED_SELECTION) {
    FOREACH_ENUM(PlayerNumber, pn) {
      if (GAMESTATE->IsHumanPlayer(pn)) {
        continue;
      }
      if (SHOW_CURSOR) {
        if (spr_cursor_[pn]) {
          spr_cursor_[pn]->SetVisible(false);
        }
      }
      if (SHOW_SCROLLER) {
        scroller_[pn].SetVisible(false);
      }
    }
  }

  this->UpdateSelectableChoices();

  ScreenSelect::BeginScreen();

  // Call GetTweenTimeLeft after the base BeginScreen has started the in
  // Transition.
  lock_input_secs_ = this->GetTweenTimeLeft();
}

void ScreenSelectMaster::HandleScreenMessage(const ScreenMessage sm) {
  ScreenSelect::HandleScreenMessage(sm);

  std::vector<PlayerNumber> vpns;
  GetActiveElementPlayerNumbers(vpns);

  if (sm == SM_PlayPostSwitchPage) {
    const int new_choice = choice_[GAMESTATE->GetMasterPlayerNumber()];
    Page new_page = GetPage(new_choice);

    Message msg("PostSwitchPage");
    msg.SetParam("NewPageIndex", (int)new_page);

    if (SHOW_CURSOR) {
      for (const PlayerNumber& p : vpns) {
        spr_cursor_[p]->HandleMessage(msg);
      }
    }

    if (SHOW_SCROLLER) {
      for (const PlayerNumber& p : vpns) {
        const int choice = choice_[p];
        spr_scroll_[p][choice]->HandleMessage(msg);
      }
    }
    MESSAGEMAN->Broadcast(msg);

    lock_input_secs_ = POST_SWITCH_PAGE_SECONDS;
  } else if (sm == SM_MenuTimer) {
    if (DOUBLE_PRESS_TO_SELECT) {
      FOREACH_HumanPlayer(p) {
        double_choice_no_sound_ = true;
        double_choice_[p] = true;
        InputEventPlus iep;
        iep.pn_ = p;
        MenuStart(iep);
      }
    }
  }
}

void ScreenSelectMaster::HandleMessage(const Message& msg) {
  if (msg == Message_PlayerJoined) {
    UpdateSelectableChoices();
  }

  ScreenSelect::HandleMessage(msg);
}

int ScreenSelectMaster::GetSelectionIndex(PlayerNumber pn) {
  return choice_[pn];
}

void ScreenSelectMaster::UpdateSelectableChoices() {
  std::vector<PlayerNumber> vpns;
  GetActiveElementPlayerNumbers(vpns);
  int first_playable = -1;
  bool on_unplayable[NUM_PLAYERS];
  FOREACH_PlayerNumber(pn) { on_unplayable[pn] = false; }

  for (unsigned c = 0; c < game_commands_.size(); c++) {
    std::string command = "Enabled";
    bool disabled = false;
    if (!game_commands_[c].IsPlayable()) {
      command = "Disabled";
      disabled = true;
    } else if (first_playable == -1) {
      first_playable = c;
    }
    if (SHOW_ICON) {
      spr_icon_[c]->PlayCommand(command);
    }

    for (PlayerNumber& p : vpns) {
      if (disabled && choice_[p] == static_cast<int>(c)) {
        on_unplayable[p] = true;
      }
      if (spr_scroll_[p][c].IsLoaded()) {
        spr_scroll_[p][c]->PlayCommand(command);
      }
    }
  }
  for (PlayerNumber& pn : vpns) {
    if (on_unplayable[pn] && first_playable != -1) {
      ChangeSelection(
          pn, first_playable < choice_[pn] ? MenuDir_Left : MenuDir_Right,
          first_playable);
    }
  }

  /* If no options are playable at all, just wait.  Some external
   * stimulus may make options available (such as coin insertion).
   * If any options are playable, make sure one is selected. */
  FOREACH_HumanPlayer(p) {
    if (!game_commands_.empty() && !game_commands_[choice_[p]].IsPlayable()) {
      Move(p, MenuDir_Auto);
    }
  }
}

bool ScreenSelectMaster::AnyOptionsArePlayable() const {
  for (unsigned i = 0; i < game_commands_.size(); ++i) {
    if (game_commands_[i].IsPlayable()) {
      return true;
    }
  }

  return false;
}

bool ScreenSelectMaster::Move(PlayerNumber pn, MenuDir dir) {
  if (!AnyOptionsArePlayable()) {
    return false;
  }

  int switch_to_index = choice_[pn];
  std::set<int> seen;

  do {
    std::map<int, int>::const_iterator iter =
        map_current_choice_to_next_choice_[dir].find(switch_to_index);
    if (iter != map_current_choice_to_next_choice_[dir].end()) {
      switch_to_index = iter->second;
    }

    if (switch_to_index < 0 ||
        switch_to_index >= (int)game_commands_.size()) {  // out of choice range
      return false;                                       // can't go that way
    }
    if (seen.find(switch_to_index) != seen.end()) {
      return false;  // went full circle and none found
    }
    seen.insert(switch_to_index);
  } while (!game_commands_.empty() &&
           !game_commands_[switch_to_index].IsPlayable() && !DO_SWITCH_ANYWAYS);

  return ChangeSelection(pn, dir, switch_to_index);
}

bool ScreenSelectMaster::MenuLeft(const InputEventPlus& input) {
  PlayerNumber pn = input.pn_;
  if (lock_input_secs_ > 0 || chosen_[pn]) {
    return false;
  }
  if (input.type_ == IET_RELEASE) {
    return false;
  }
  if (input.type_ != IET_FIRST_PRESS) {
    if (!ALLOW_REPEATING_INPUT) {
      return false;
    }
    if (tracking_repeating_input_ != input.menu_i_) {
      return false;
    }
  }
  if (Move(pn, MenuDir_Left)) {
    tracking_repeating_input_ = input.menu_i_;
    sound_change_.Play(true);
    MESSAGEMAN->Broadcast(
        (MessageID)(Message_MenuLeftP1 + Enum::to_integral(pn)));
    MESSAGEMAN->Broadcast((MessageID)(Message_MenuSelectionChanged));

    // if they use double select
    if (DOUBLE_PRESS_TO_SELECT) {
      double_choice_[pn] = false;  // player has cancelled their selection
    }
    return true;
  }
  return false;
}

bool ScreenSelectMaster::MenuRight(const InputEventPlus& input) {
  PlayerNumber pn = input.pn_;
  if (lock_input_secs_ > 0 || chosen_[pn]) {
    return false;
  }
  if (input.type_ == IET_RELEASE) {
    return false;
  }
  if (input.type_ != IET_FIRST_PRESS) {
    if (!ALLOW_REPEATING_INPUT) {
      return false;
    }
    if (tracking_repeating_input_ != input.menu_i_) {
      return false;
    }
  }
  if (Move(pn, MenuDir_Right)) {
    tracking_repeating_input_ = input.menu_i_;
    sound_change_.Play(true);
    MESSAGEMAN->Broadcast(
        (MessageID)(Message_MenuRightP1 + Enum::to_integral(pn)));
    MESSAGEMAN->Broadcast((MessageID)(Message_MenuSelectionChanged));

    // if they use double select
    if (DOUBLE_PRESS_TO_SELECT) {
      double_choice_[pn] = false;  // player has cancelled their selection
    }
    return true;
  }
  return false;
}

bool ScreenSelectMaster::MenuUp(const InputEventPlus& input) {
  PlayerNumber pn = input.pn_;
  if (lock_input_secs_ > 0 || chosen_[pn]) {
    return false;
  }
  if (input.type_ == IET_RELEASE) {
    return false;
  }
  if (input.type_ != IET_FIRST_PRESS) {
    if (!ALLOW_REPEATING_INPUT) {
      return false;
    }
    if (tracking_repeating_input_ != input.menu_i_) {
      return false;
    }
  }
  if (Move(pn, MenuDir_Up)) {
    tracking_repeating_input_ = input.menu_i_;
    sound_change_.Play(true);
    MESSAGEMAN->Broadcast(
        (MessageID)(Message_MenuUpP1 + Enum::to_integral(pn)));
    MESSAGEMAN->Broadcast((MessageID)(Message_MenuSelectionChanged));

    // if they use double select
    if (DOUBLE_PRESS_TO_SELECT) {
      double_choice_[pn] = false;  // player has cancelled their selection
    }
    return true;
  }
  return false;
}

bool ScreenSelectMaster::MenuDown(const InputEventPlus& input) {
  PlayerNumber pn = input.pn_;
  if (lock_input_secs_ > 0 || chosen_[pn]) {
    return false;
  }
  if (input.type_ == IET_RELEASE) {
    return false;
  }
  if (input.type_ != IET_FIRST_PRESS) {
    if (!ALLOW_REPEATING_INPUT) {
      return false;
    }
    if (tracking_repeating_input_ != input.menu_i_) {
      return false;
    }
  }
  if (Move(pn, MenuDir_Down)) {
    tracking_repeating_input_ = input.menu_i_;
    sound_change_.Play(true);
    MESSAGEMAN->Broadcast(
        (MessageID)(Message_MenuDownP1 + Enum::to_integral(pn)));
    MESSAGEMAN->Broadcast((MessageID)(Message_MenuSelectionChanged));

    // if they use double select
    if (DOUBLE_PRESS_TO_SELECT) {
      double_choice_[pn] = false;  // player has cancelled their selection
    }
    return true;
  }
  return false;
}

bool ScreenSelectMaster::ChangePage(int new_choice) {
  Page old_page = GetPage(choice_[GAMESTATE->GetMasterPlayerNumber()]);
  Page new_page = GetPage(new_choice);

  // If anyone has already chosen, don't allow changing of pages
  FOREACH_PlayerNumber(p) {
    if (GAMESTATE->IsHumanPlayer(p) && chosen_[p]) {
      return false;
    }
  }

  // change both players
  FOREACH_PlayerNumber(p) choice_[p] = new_choice;

  const std::string icon_and_explanation_command =
      ssprintf("SwitchToPage%d", new_page + 1);
  if (SHOW_ICON) {
    for (unsigned c = 0; c < game_commands_.size(); ++c) {
      spr_icon_[c]->PlayCommand(icon_and_explanation_command);
    }
  }

  FOREACH_ENUM(Page, page) {
    spr_explanation_[page]->PlayCommand(icon_and_explanation_command);
    spr_more_[page]->PlayCommand(icon_and_explanation_command);
  }

  std::vector<PlayerNumber> vpns;
  GetActiveElementPlayerNumbers(vpns);

  Message msg("PreSwitchPage");
  msg.SetParam("OldPageIndex", (int)old_page);
  msg.SetParam("NewPageIndex", (int)new_page);
  MESSAGEMAN->Broadcast(msg);

  for (const PlayerNumber& p : vpns) {
    if (GAMESTATE->IsHumanPlayer(p) || SHARED_SELECTION) {
      if (SHOW_CURSOR) {
        spr_cursor_[p]->HandleMessage(msg);
        spr_cursor_[p]->SetXY(GetCursorX(p), GetCursorY(p));
      }

      if (SHOW_SCROLLER) {
        spr_scroll_[p][choice_[p]]->HandleMessage(msg);
      }
    }
  }

  if (new_page == PAGE_2) {
    // XXX: only play this once (I thought we already did that?)
    // Play it on every change to page 2.  -Chris
    // That sounds ugly if you go back and forth quickly. -g
    // Should we lock input while it's scrolling? -Chris
    sound_difficult_.Stop();
    sound_difficult_.PlayRandom();
  }

  lock_input_secs_ = PRE_SWITCH_PAGE_SECONDS;
  this->PostScreenMessage(SM_PlayPostSwitchPage, GetTweenTimeLeft());
  return true;
}

bool ScreenSelectMaster::ChangeSelection(
    PlayerNumber pn, MenuDir dir, int new_choice) {
  if (new_choice == choice_[pn]) {
    return false;  // already there
  }

  Page page = GetPage(new_choice);
  if (GetPage(choice_[pn]) != page) {
    return ChangePage(new_choice);
  }

  std::vector<PlayerNumber> vpns;
  if (SHARED_SELECTION || page != PAGE_1) {
    /* Set the new m_iChoice even for disabled players, since a player might
     * join on a SHARED_SELECTION after the cursor has been moved. */
    FOREACH_ENUM(PlayerNumber, p)
    vpns.push_back(p);
  } else {
    vpns.push_back(pn);
  }

  for (const PlayerNumber& p : vpns) {
    const int old_choice = choice_[p];
    choice_[p] = new_choice;

    if (SHOW_ICON) {
      /* XXX: If !SharedPreviewAndCursor, this is incorrect. (Nothing uses
       * both icon focus and !SharedPreviewAndCursor right now.)
       * What is SharedPreviewAndCursor? -- Steve */
      bool old_still_has_focus = false;
      bool new_already_had_focus = false;
      FOREACH_HumanPlayer(p2) {
        if (p2 == p) {
          continue;
        }
        old_still_has_focus |= choice_[p2] == old_choice;
        new_already_had_focus |= choice_[p2] == new_choice;
      }

      if (DOUBLE_PRESS_TO_SELECT) {
        // this player is currently on a single press, which they are cancelling
        if (double_choice_[pn]) {
          if (!old_still_has_focus) {
            spr_icon_[old_choice]->PlayCommand("LostSelectedLoseFocus");
          }
          if (!new_already_had_focus) {
            spr_icon_[new_choice]->PlayCommand("LostSelectedGainFocus");
          }
        } else {
          if (!old_still_has_focus) {
            spr_icon_[old_choice]->PlayCommand("LoseFocus");
          }
          if (!new_already_had_focus) {
            spr_icon_[new_choice]->PlayCommand("GainFocus");
          }
        }
      } else  // not using double selection
      {
        if (!old_still_has_focus) {
          spr_icon_[old_choice]->PlayCommand("LoseFocus");
        }
        if (!new_already_had_focus) {
          spr_icon_[new_choice]->PlayCommand("GainFocus");
        }
      }
    }

    if (SHOW_CURSOR) {
      if (GAMESTATE->IsHumanPlayer(p)) {
        PlayerNumber ep = p;
        if (SHARED_SELECTION) {
          ep = PLAYER_1;
        }
        spr_cursor_[ep]->PlayCommand("Change");
        spr_cursor_[ep]->SetXY(GetCursorX(p), GetCursorY(p));
      }
    }

    if (SHOW_SCROLLER) {
      ActorScroller& scroller =
          (SHARED_SELECTION || page != PAGE_1 ? scroller_[0] : scroller_[p]);
      std::vector<AutoActor>& scroll =
          (SHARED_SELECTION || page != PAGE_1 ? spr_scroll_[0]
                                              : spr_scroll_[p]);

      if (WRAP_SCROLLER) {
        // HACK: We can't tell from the option orders whether or not we wrapped.
        // For now, assume that the order is increasing left to right.
        int pressed_dir = (dir == MenuDir_Left || dir == MenuDir_Up) ? -1 : +1;
        int actual_dir = (old_choice < new_choice) ? +1 : -1;

        if (pressed_dir != actual_dir)  // wrapped
        {
          float item = scroller.GetCurrentItem();
          int num_choices = game_commands_.size();
          item += actual_dir * num_choices;
          scroller.SetCurrentAndDestinationItem(item);
        }
      }

      scroller.SetDestinationItem((float)new_choice);

      // using double selections
      if (DOUBLE_PRESS_TO_SELECT) {
        // this player is currently on a single press, which they are cancelling
        if (double_choice_[pn]) {
          scroll[old_choice]->PlayCommand("LostSelectedLoseFocus");
          scroll[new_choice]->PlayCommand("LostSelectedGainFocus");
        } else  // the player hasn't made any selections yet
        {
          scroll[old_choice]->PlayCommand("LoseFocus");
          scroll[new_choice]->PlayCommand("GainFocus");
        }
      } else  // regular lose/gain focus
      {
        scroll[old_choice]->PlayCommand("LoseFocus");
        scroll[new_choice]->PlayCommand("GainFocus");
      }
    }
  }

  return true;
}

PlayerNumber ScreenSelectMaster::GetSharedPlayer() {
  if (GAMESTATE->GetMasterPlayerNumber() != PLAYER_INVALID) {
    return GAMESTATE->GetMasterPlayerNumber();
  }

  return PLAYER_1;
}

ScreenSelectMaster::Page ScreenSelectMaster::GetPage(int choice_index) const {
  return choice_index < NUM_CHOICES_ON_PAGE_1 ? PAGE_1 : PAGE_2;
}

ScreenSelectMaster::Page ScreenSelectMaster::GetCurrentPage() const {
  // Both players are guaranteed to be on the same page.
  return GetPage(choice_[GetSharedPlayer()]);
}

float ScreenSelectMaster::DoMenuStart(PlayerNumber pn) {
  if (chosen_[pn]) {
    return 0;
  }

  bool any_chosen = false;
  FOREACH_PlayerNumber(p) any_chosen |= chosen_[p];

  chosen_[pn] = true;

  this->PlayCommand("MadeChoice" + PlayerNumberToString(pn));

  bool is_first_to_choose = any_chosen;

  float secs = 0;

  if (is_first_to_choose) {
    FOREACH_ENUM(Page, page) {
      spr_more_[page]->PlayCommand("Off");
      secs = std::max(secs, spr_more_[page]->GetTweenTimeLeft());
    }
  }
  if (SHOW_CURSOR) {
    if (spr_cursor_[pn] != nullptr) {
      spr_cursor_[pn]->PlayCommand("Choose");
      secs = std::max(secs, spr_cursor_[pn]->GetTweenTimeLeft());
    }
  }

  return secs;
}

bool ScreenSelectMaster::MenuStart(const InputEventPlus& input) {
  if (input.type_ != IET_FIRST_PRESS) {
    return false;
  }
  PlayerNumber pn = input.pn_;

  if (lock_input_secs_ > 0) {
    return false;
  }
  if (SHARED_SELECTION || GetCurrentPage() == PAGE_2) {
    // Return if any player has chosen
    FOREACH_EnabledPlayer(p) {
      if (chosen_[p]) {
        return false;
      }
    }
  } else if (chosen_[pn]) {
    // Return if this player has already chosen
    return false;
  }

  if (!ProcessMenuStart(pn)) {
    return false;
  }

  // double press is enabled and the player hasn't made their first press
  if (DOUBLE_PRESS_TO_SELECT && !double_choice_[pn]) {
    sound_start_.PlayCopy(true);
    double_choice_[pn] = true;

    if (SHOW_SCROLLER) {
      std::vector<AutoActor>& scroll =
          SHARED_SELECTION ? spr_scroll_[0] : spr_scroll_[pn];
      scroll[choice_[pn]]->PlayCommand("InitialSelection");
    }

    return true;
  }

  GameCommand empty_mc;
  // This is so we can avoid having problems when the GameCommands in the
  // choices were all invalid or didn't load or similar. -Kyz
  GameCommand* mc = &empty_mc;
  if (!game_commands_.empty()) {
    mc = &(game_commands_[choice_[pn]]);
  }

  /* If no options are playable, then we're just waiting for one to become
   * available. If any options are playable, then the selection must be
   * playable. */
  if (!AnyOptionsArePlayable()) {
    return false;
  }

  SOUND->PlayOnceFromDir(ANNOUNCER->GetPathTo(
      ssprintf("%s comment %s", name_.c_str(), mc->name_.c_str())));

  // Play a copy of the sound, so it'll finish playing even if we leave the
  // screen immediately.
  if (mc->sound_path_.empty() && !double_choice_no_sound_) {
    sound_start_.PlayCopy(true);
  }

  if (mc->screen_.empty()) {
    mc->ApplyToAllPlayers();
    // We want to be able to broadcast a Start message to the theme, in
    // case a themer wants to handle something. -aj
    Message msg(MessageIDToString(
        (MessageID)(Message_MenuStartP1 + Enum::to_integral(pn))));
    msg.SetParam("ScreenEmpty", true);
    MESSAGEMAN->Broadcast(msg);
    return true;
  }

  float secs = 0;
  bool all_done = true;
  if ((bool)SHARED_SELECTION || GetCurrentPage() == PAGE_2) {
    // Only one player has to pick. Choose this for all the other players, too.
    FOREACH_EnabledPlayer(p) {
      ASSERT(!chosen_[p]);
      secs = std::max(secs, DoMenuStart(p));
    }
  } else {
    secs = std::max(secs, DoMenuStart(pn));
    // check to see if everyone has chosen
    FOREACH_HumanPlayer(p) all_done &= chosen_[p];
  }

  if (all_done) {
    // broadcast MenuStart just like MenuLeft/Right/etc.
    MESSAGEMAN->Broadcast(
        (MessageID)(Message_MenuStartP1 + Enum::to_integral(pn)));
    this->PostScreenMessage(
        SM_BeginFadingOut, secs);  // tell our owner it's time to move on
  }
  return true;
}

/* We want all items to always run OnCommand and either GainFocus or LoseFocus
 * on tween-in. If we only run OnCommand, then it has to contain a copy of
 * either GainFocus or LoseFocus, which implies that the default setting is
 * hard-coded in the theme. Always run both.
 * However, the actual tween-in is OnCommand; we don't always want to actually
 * run through the Gain/LoseFocus tweens during initial tween-in. So, we run
 * the focus command first, do a FinishTweening to pop it in place, and then
 * run OnCommand. This means that the focus command should be position neutral;
 * eg. only use "addx", not "x". */
void ScreenSelectMaster::TweenOnScreen() {
  std::vector<PlayerNumber> vpns;
  GetActiveElementPlayerNumbers(vpns);

  if (SHOW_ICON) {
    for (unsigned c = 0; c < game_commands_.size(); c++) {
      spr_icon_[c]->PlayCommand(
          (int(c) == choice_[0]) ? "GainFocus" : "LoseFocus");
      spr_icon_[c]->FinishTweening();
    }
  }

  if (SHOW_SCROLLER) {
    for (const PlayerNumber& p : vpns) {
      // Play Gain/LoseFocus before playing the on command.
      // Gain/Lose will often stop tweening, which ruins the OnCommand.
      for (unsigned c = 0; c < game_commands_.size(); c++) {
        spr_scroll_[p][c]->PlayCommand(
            int(c) == choice_[p] ? "GainFocus" : "LoseFocus");
        spr_scroll_[p][c]->FinishTweening();
      }

      scroller_[p].SetCurrentAndDestinationItem((float)choice_[p]);
    }
  }

  // Need to SetXY of Cursor after Icons since it depends on the Icons'
  // positions.
  if (SHOW_CURSOR) {
    for (const PlayerNumber& p : vpns) {
      if (GAMESTATE->IsHumanPlayer(p) || SHARED_SELECTION) {
        spr_cursor_[p]->SetXY(GetCursorX(p), GetCursorY(p));
      }
    }
  }

  ScreenSelect::TweenOnScreen();
}

void ScreenSelectMaster::TweenOffScreen() {
  ScreenSelect::TweenOffScreen();

  std::vector<PlayerNumber> vpns;
  GetActiveElementPlayerNumbers(vpns);

  for (unsigned c = 0; c < game_commands_.size(); c++) {
    if (GetPage(c) != GetCurrentPage()) {
      continue;  // skip
    }

    bool selected_by_either_player =
        std::any_of(vpns.begin(), vpns.end(), [&](const PlayerNumber& p) {
          return choice_[p] == static_cast<int>(c);
        });

    if (SHOW_ICON) {
      spr_icon_[c]->PlayCommand(
          selected_by_either_player ? "OffFocused" : "OffUnfocused");
    }

    if (SHOW_SCROLLER) {
      for (const PlayerNumber& p : vpns) {
        spr_scroll_[p][c]->PlayCommand(
            selected_by_either_player ? "OffFocused" : "OffUnfocused");
      }
    }
  }
}

// Use DestX and DestY so that the cursor can move to where the icon will be
// rather than where it is.
float ScreenSelectMaster::GetCursorX(PlayerNumber pn) {
  int choice = choice_[pn];
  AutoActor& spr = spr_icon_[choice];
  return spr->GetDestX() + CURSOR_OFFSET_X_FROM_ICON.GetValue(pn);
}

float ScreenSelectMaster::GetCursorY(PlayerNumber pn) {
  int choice = choice_[pn];
  AutoActor& spr = spr_icon_[choice];
  return spr->GetDestY() + CURSOR_OFFSET_Y_FROM_ICON.GetValue(pn);
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the ScreenSelectMaster. */
class LunaScreenSelectMaster : public Luna<ScreenSelectMaster> {
 public:
  static int GetSelectionIndex(T* p, lua_State* L) {
    lua_pushnumber(
        L, p->GetPlayerSelectionIndex(Enum::Check<PlayerNumber>(L, 1)));
    return 1;
  }
  // should I even bother adding this? -aj
  // would have to make a public function to get this in ssmaster.h:
  // m_aGameCommands[i].m_sName
  // static int SelectionIndexToChoiceName( T* p, lua_State *L ){  return 1; }

  LunaScreenSelectMaster() { ADD_METHOD(GetSelectionIndex); }
};

LUA_REGISTER_DERIVED_CLASS(ScreenSelectMaster, ScreenWithMenuElements)
// lua end

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
