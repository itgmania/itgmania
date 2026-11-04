#include "OptionRow.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include "Actor.h"
#include "ActorFrame.h"
#include "ActorUtil.h"
#include "BitmapText.h"
#include "CommonMetrics.h"
#include "Course.h"
#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "GameState.h"
#include "MessageManager.h"
#include "ModIcon.h"
#include "OptionRowHandler.h"
#include "OptionsCursor.h"
#include "PlayerNumber.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "Song.h"
#include "StdString.h"
#include "Style.h"
#include "ThemeManager.h"
#include "global.h"

const std::string NEXT_ROW_NAME = "NextRow";
const std::string EXIT_NAME = "Exit";

std::string OptionRow::GetThemedItemText(int iChoice) const {
  std::string s = hand_->GetThemedItemText(iChoice);

  // HACK: Always theme the NEXT_ROW and EXIT items.
  if (first_item_goes_down_ && iChoice == 0) {
    s = CommonMetrics::LocalizeOptionItem(NEXT_ROW_NAME, false);
  } else if (row_type_ == OptionRow::RowType_Exit) {
    s = CommonMetrics::LocalizeOptionItem(EXIT_NAME, false);
  }

  return s;
}

std::string ITEMS_LONG_ROW_X_NAME(size_t p) {
  return ssprintf("ItemsLongRowP%dX", int(p + 1));
}
std::string MOD_ICON_X_NAME(size_t p) {
  return ssprintf("ModIconP%dX", int(p + 1));
}

OptionRow::OptionRow(const OptionRowType* pSource) {
  parent_type_ = pSource;
  hand_ = nullptr;

  text_title_ = nullptr;
  ZERO(mod_icons_);

  Clear();
  this->AddChild(&frame_);
}

OptionRow::~OptionRow() { Clear(); }

void OptionRow::Clear() {
  ActorFrame::RemoveAllChildren();

  FOREACH_PlayerNumber(p) selected_[p].clear();

  frame_.DeleteAllChildren();
  text_items_.clear();
  FOREACH_PlayerNumber(p) underline_[p].clear();

  if (hand_ != nullptr) {
    for (const std::string& m : hand_->reload_row_messages_) {
      MESSAGEMAN->Unsubscribe(this, m);
    }
  }
  RageUtil::SafeDelete(hand_);

  first_item_goes_down_ = false;
  ZERO(row_has_focus_);
  ZERO(choice_in_row_with_focus_);
}

void OptionRowType::Load(const std::string& sMetricsGroup, Actor* pParent) {
  metrics_group_ = sMetricsGroup;

  ITEMS_START_X.Load(sMetricsGroup, "ItemsStartX");
  ITEMS_END_X.Load(sMetricsGroup, "ItemsEndX");
  ITEMS_GAP_X.Load(sMetricsGroup, "ItemsGapX");
  ITEMS_MIN_BASE_ZOOM.Load(sMetricsGroup, "ItemsMinBaseZoom");
  ITEMS_LONG_ROW_X.Load(sMetricsGroup, ITEMS_LONG_ROW_X_NAME, NUM_PLAYERS);
  ITEMS_LONG_ROW_SHARED_X.Load(sMetricsGroup, "ItemsLongRowSharedX");
  MOD_ICON_X.Load(sMetricsGroup, MOD_ICON_X_NAME, NUM_PLAYERS);
  COLOR_SELECTED.Load(sMetricsGroup, "ColorSelected");
  COLOR_NOT_SELECTED.Load(sMetricsGroup, "ColorNotSelected");
  COLOR_DISABLED.Load(sMetricsGroup, "ColorDisabled");
  TWEEN_SECONDS.Load(sMetricsGroup, "TweenSeconds");
  SHOW_BPM_IN_SPEED_TITLE.Load(sMetricsGroup, "ShowBpmInSpeedTitle");
  SHOW_MOD_ICONS.Load(sMetricsGroup, "ShowModIcons");
  SHOW_UNDERLINES.Load(sMetricsGroup, "ShowUnderlines");
  MOD_ICON_METRICS_GROUP.Load(sMetricsGroup, "ModIconMetricsGroup");

  text_item_.LoadFromFont(THEME->GetPathF(sMetricsGroup, "Item"));
  text_item_.SetName("Item");
  ActorUtil::LoadAllCommands(text_item_, sMetricsGroup);

  if (SHOW_UNDERLINES) {
    FOREACH_PlayerNumber(p) underline_[p].Load(
        "OptionsUnderline" + PlayerNumberToString(p), false);
  }

  text_title_.LoadFromFont(THEME->GetPathF(sMetricsGroup, "title"));
  text_title_.SetName("Title");
  ActorUtil::LoadAllCommandsAndSetXY(text_title_, sMetricsGroup);

  Actor* pActor =
      ActorUtil::MakeActor(THEME->GetPathG(sMetricsGroup, "Frame"), pParent);
  if (pActor == nullptr) {
    pActor = new Actor;
  }
  spr_frame_.Load(pActor);
  spr_frame_->SetName("Frame");
  ActorUtil::LoadAllCommandsAndSetXY(spr_frame_, sMetricsGroup);

  if (SHOW_MOD_ICONS) {
    mod_icon_.Load(MOD_ICON_METRICS_GROUP);
    mod_icon_.SetName("ModIcon");
    ActorUtil::LoadAllCommands(mod_icon_, sMetricsGroup);
  }
}

void OptionRow::LoadNormal(OptionRowHandler* pHand, bool bFirstItemGoesDown) {
  row_type_ = OptionRow::RowType_Normal;
  hand_ = pHand;
  first_item_goes_down_ = bFirstItemGoesDown;

  for (const std::string& m : hand_->reload_row_messages_) {
    MESSAGEMAN->Subscribe(this, m);
  }

  ChoicesChanged(RowType_Normal);
}

void OptionRow::LoadExit() {
  row_type_ = OptionRow::RowType_Exit;
  OptionRowHandler* pHand = OptionRowHandlerUtil::MakeNull();
  pHand->def_.select_type_ = SELECT_NONE;
  pHand->def_.name_ = EXIT_NAME;
  pHand->def_.choices_.push_back(EXIT_NAME);
  pHand->def_.layout_type_ = LAYOUT_SHOW_ONE_IN_ROW;
  pHand->def_.one_choice_for_all_players_ = true;
  hand_ = pHand;

  ChoicesChanged(RowType_Exit);
}

void OptionRow::ChoicesChanged(RowType type, bool reset_focus) {
  ASSERT_M(
      !hand_->def_.choices_.empty(), hand_->def_.name_ + " has no choices");

  // Remove the NextRow marker before reloading choices
  if (hand_->def_.choices_[0] == NEXT_ROW_NAME) {
    hand_->def_.choices_.erase(hand_->def_.choices_.begin());
    FOREACH_PlayerNumber(p) selected_[p].erase(selected_[p].begin());
  }

  FOREACH_PlayerNumber(p) {
    std::vector<bool>& vbSelected = selected_[p];
    vbSelected.resize(0);
    vbSelected.resize(hand_->def_.choices_.size(), false);

    // set select the first item if a SELECT_ONE row
    if (vbSelected.size() && hand_->def_.select_type_ == SELECT_ONE) {
      vbSelected[0] = true;
    }
  }

  // TRICKY: Insert a down arrow as the first choice in the row.
  if (first_item_goes_down_) {
    hand_->def_.choices_.insert(hand_->def_.choices_.begin(), NEXT_ROW_NAME);
    FOREACH_PlayerNumber(p) selected_[p].insert(selected_[p].begin(), false);
  }

  InitText(type);

  // Lua can change the choices now, and when it does, we don't want to change
  // focus.
  if (reset_focus) {
    // When choices change, the old focus position is meaningless; reset it.
    FOREACH_PlayerNumber(p) SetChoiceInRowWithFocus(p, 0);
  }

  text_title_->SetText(GetRowTitle());
}

std::string OptionRow::GetRowTitle() const {
  std::string sTitle = hand_->OptionTitle();

  // HACK: tack the BPM onto the name of the speed line
  if (CompareNoCase(hand_->def_.name_, "speed") == 0) {
    bool bShowBpmInSpeedTitle = parent_type_->SHOW_BPM_IN_SPEED_TITLE;

    if (GAMESTATE->cur_course_) {
      const Trail* pTrail =
          GAMESTATE->cur_trail_[GAMESTATE->GetMasterPlayerNumber()];
      ASSERT(pTrail != nullptr);
      const int iNumCourseEntries = pTrail->entries_.size();
      if (iNumCourseEntries >
          CommonMetrics::MAX_COURSE_ENTRIES_BEFORE_VARIOUS) {
        bShowBpmInSpeedTitle = false;
      }
    }

    if (bShowBpmInSpeedTitle) {
      DisplayBpms bpms;
      if (GAMESTATE->cur_song_) {
        const Song* pSong = GAMESTATE->cur_song_;
        pSong->GetDisplayBpms(bpms);
      } else if (GAMESTATE->cur_course_) {
        const Course* pCourse = GAMESTATE->cur_course_;
        StepsType st =
            GAMESTATE->GetCurrentStyle(GAMESTATE->GetMasterPlayerNumber())
                ->steps_type_;
        const Trail* pTrail = pCourse->GetTrail(st);
        ASSERT(pTrail != nullptr);
        pTrail->GetDisplayBpms(bpms);
      }

      if (bpms.IsSecret()) {
        sTitle += ssprintf(
            " (??"
            "?)");  // split so gcc doesn't think this is a trigraph
      } else if (bpms.BpmIsConstant()) {
        sTitle += ssprintf(" (%.0f)", bpms.GetMin());
      } else {
        sTitle += ssprintf(" (%.0f-%.0f)", bpms.GetMin(), bpms.GetMax());
      }
    }
  }

  return sTitle;
}

/* Set up text, underlines and titles for options. This can be called as soon as
 * m_pHand->m_Def is available. */
void OptionRow::InitText(RowType type) {
  /* If we have elements already, we're being updated from a new set of options.
   * Delete the old ones. */
  frame_.DeleteAllChildren();
  text_items_.clear();
  FOREACH_PlayerNumber(p) underline_[p].clear();

  text_title_ = new BitmapText(parent_type_->text_title_);
  frame_.AddChild(text_title_);

  spr_frame_ = parent_type_->spr_frame_->Copy();
  spr_frame_->SetDrawOrder(-1);  // under title
  frame_.AddChild(spr_frame_);

  if (parent_type_->SHOW_MOD_ICONS) {
    switch (row_type_) {
      case RowType_Normal:
        FOREACH_PlayerNumber(p) {
          mod_icons_[p] = new ModIcon(parent_type_->mod_icon_);
          mod_icons_[p]->SetDrawOrder(-1);  // under title
          mod_icons_[p]->PlayCommand("On");

          frame_.AddChild(mod_icons_[p]);

          GameCommand gc;
          SetModIcon(p, "", gc);
        }
        break;
      case RowType_Exit:
        break;
    }
  }

  // If the items will go off the edge of the screen, then force
  // LAYOUT_SHOW_ONE_IN_ROW.
  float fBaseZoom = 1.0f;
  {
    BitmapText bt(parent_type_->text_item_);
    bt.PlayCommand("On");

    // Figure out the width of the row.
    float fWidth = 0;
    for (unsigned c = 0; c < hand_->def_.choices_.size(); c++) {
      std::string sText = GetThemedItemText(c);
      bt.SetText(sText);

      fWidth += bt.GetZoomedWidth();

      if (c != hand_->def_.choices_.size() - 1) {
        fWidth += parent_type_->ITEMS_GAP_X;
      }
    }

    // Try to fit everything on one line.
    float fTotalWidth = parent_type_->ITEMS_END_X - parent_type_->ITEMS_START_X;
    if (fWidth > fTotalWidth) {
      float fPossibleBaseZoom = fTotalWidth / fWidth;
      if (fPossibleBaseZoom >= parent_type_->ITEMS_MIN_BASE_ZOOM) {
        fBaseZoom = fPossibleBaseZoom;
      } else {
        hand_->def_.layout_type_ = LAYOUT_SHOW_ONE_IN_ROW;
      }
    }
  }

  // load m_textItems
  switch (hand_->def_.layout_type_) {
    case LAYOUT_SHOW_ONE_IN_ROW: {
      if (IsShowOneValueWithGoDown()) {
        // Show a persistent ▼.
        BitmapText* pArrow = new BitmapText(parent_type_->text_item_);
        text_items_.push_back(pArrow);
        pArrow->PlayCommand("On");
        pArrow->SetText(GetThemedItemText(0));
        pArrow->SetX(
            parent_type_->ITEMS_START_X + pArrow->GetZoomedWidth() / 2);
      }

      // init text
      FOREACH_PlayerNumber(p) {
        BitmapText* pText = new BitmapText(parent_type_->text_item_);
        text_items_.push_back(pText);

        pText->PlayCommand("On");

        if (hand_->def_.one_choice_for_all_players_) {
          pText->SetX(parent_type_->ITEMS_LONG_ROW_SHARED_X);
          break;  // only initialize one item since it's shared
        } else {
          pText->SetX(parent_type_->ITEMS_LONG_ROW_X.GetValue(p));
        }

        // Set the text now, so SetWidthXY below is correct.
        UpdateText(p);

        // init underlines
        if (parent_type_->SHOW_UNDERLINES &&
            GetRowType() != OptionRow::RowType_Exit) {
          OptionsCursor* pCursor =
              new OptionsCursor(parent_type_->underline_[p]);
          underline_[p].push_back(pCursor);

          int iWidth, iX, iY;
          GetWidthXY(p, 0, iWidth, iX, iY);
          pCursor->SetX(float(iX));
          pCursor->SetBarWidth(iWidth);
        }
      }
      break;
    }

    case LAYOUT_SHOW_ALL_IN_ROW: {
      float fX = parent_type_->ITEMS_START_X;
      for (unsigned c = 0; c < hand_->def_.choices_.size(); c++) {
        // init text
        BitmapText* bt = new BitmapText(parent_type_->text_item_);
        text_items_.push_back(bt);
        bt->SetBaseZoomX(fBaseZoom);
        bt->PlayCommand("On");
        // Set text after running OnCommand so e.g. uppercase,true works -aj
        std::string sText = GetThemedItemText(c);
        bt->SetText(sText);

        // set the X position of each item in the line
        float fItemWidth = bt->GetZoomedWidth();
        fX += fItemWidth / 2;
        bt->SetX(fX);

        // init underlines
        if (parent_type_->SHOW_UNDERLINES) {
          FOREACH_PlayerNumber(p) {
            OptionsCursor* ul = new OptionsCursor(parent_type_->underline_[p]);
            underline_[p].push_back(ul);
            ul->SetX(fX);
            ul->SetBarWidth(int(fItemWidth));
          }
        }

        fX += fItemWidth / 2 + parent_type_->ITEMS_GAP_X * fBaseZoom;
      }
    } break;

    default:
      FAIL_M(
          ssprintf("Invalid option row layout: %i", hand_->def_.layout_type_));
  }

  for (unsigned c = 0; c < text_items_.size(); c++) {
    frame_.AddChild(text_items_[c]);
  }
  FOREACH_PlayerNumber(p) for (unsigned c = 0; c < underline_[p].size(); c++)
      frame_.AddChild(underline_[p][c]);

  // This is set in OptionRow::AfterImportOptions, so if we're reused with a
  // different song selected, SHOW_BPM_IN_SPEED_TITLE will show the new BPM.
  // m_textTitle->SetText( GetRowTitle() );
  text_title_->PlayCommand("On");

  spr_frame_->PlayCommand("On");

  frame_.SortByDrawOrder();
  this->SortByDrawOrder();
}

// After importing options, choose which item is focused.
void OptionRow::AfterImportOptions(PlayerNumber pn) {
  /* We load items for both players on start, since we don't know which players
   * will be joined when we're displayed. Hide items for inactive players. */
  if (hand_->def_.layout_type_ == LAYOUT_SHOW_ONE_IN_ROW &&
      !hand_->def_.one_choice_for_all_players_) {
    unsigned item_no = pn + (IsShowOneValueWithGoDown() ? 1 : 0);
    text_items_[item_no]->SetVisible(GAMESTATE->IsHumanPlayer(pn));
  }

  // Hide underlines for disabled players or disabled options.
  if (!GAMESTATE->IsHumanPlayer(pn) ||
      hand_->def_.enabled_for_players_.find(pn) ==
          hand_->def_.enabled_for_players_.end()) {
    for (unsigned c = 0; c < underline_[pn].size(); c++) {
      underline_[pn][c]->SetVisible(false);
    }
  }

  // Make all selections the same if bOneChoiceForAllPlayers.
  // Hack: we only import active players, so if only player 2 is imported,
  // we need to copy p2 to p1, not p1 to p2.
  if (hand_->def_.one_choice_for_all_players_) {
    PlayerNumber pnCopyFrom = GAMESTATE->GetMasterPlayerNumber();
    if (GAMESTATE->GetMasterPlayerNumber() == PLAYER_INVALID) {
      pnCopyFrom = PLAYER_1;
    }
    FOREACH_PlayerNumber(p) selected_[p] = selected_[pnCopyFrom];
  }

  switch (hand_->def_.select_type_) {
    case SELECT_ONE: {
      // Make sure the row actually has a selection.
      int iSelection = GetOneSelection(pn, true);
      if (iSelection == -1) {
        ASSERT(!selected_[pn].empty());
        selected_[pn][0] = true;
      }
    }
    default:
      break;
  }

  ResetFocusFromSelection(pn);

  PositionUnderlines(pn);
}

void OptionRow::PositionUnderlines(PlayerNumber pn) {
  std::vector<OptionsCursor*>& vpUnderlines = underline_[pn];
  if (vpUnderlines.empty()) {
    return;
  }

  PlayerNumber pnTakeSelectedFrom =
      hand_->def_.one_choice_for_all_players_ ? PLAYER_1 : pn;

  for (int i = 0; i < (int)vpUnderlines.size(); i++) {
    OptionsCursor& ul = *vpUnderlines[i];

    int iChoiceWithFocus = (hand_->def_.layout_type_ == LAYOUT_SHOW_ONE_IN_ROW)
                               ? GetChoiceInRowWithFocus(pn)
                               : i;

    if (IsShowOneValueWithGoDown() && iChoiceWithFocus == 0) {
      int iSelected = GetOneSelection(pn, true);
      iChoiceWithFocus = (iSelected >= 1) ? iSelected : 1;
    }

    float fAlpha = 1.0f;
    if (hand_->def_.layout_type_ == LAYOUT_SHOW_ONE_IN_ROW) {
      bool bRowEnabled = hand_->def_.enabled_for_players_.find(pn) !=
                         hand_->def_.enabled_for_players_.end();

      if (!hand_->def_.one_choice_for_all_players_) {
        if (row_has_focus_[pn]) {
          fAlpha = parent_type_->COLOR_SELECTED.GetValue().a_;
        } else if (bRowEnabled) {
          fAlpha = parent_type_->COLOR_NOT_SELECTED.GetValue().a_;
        } else {
          fAlpha = parent_type_->COLOR_DISABLED.GetValue().a_;
        }
      }
    }

    // Don't tween X movement and color changes.
    ul.StopTweening();

    int iWidth, iX, iY;
    GetWidthXY(pn, iChoiceWithFocus, iWidth, iX, iY);
    ul.SetX((float)iX);
    // only set alpha, in case a theme tries to color underlines. -aj
    ul.SetDiffuseAlpha(fAlpha);

    ASSERT(selected_[pnTakeSelectedFrom].size() == hand_->def_.choices_.size());

    bool bSelected = (iChoiceWithFocus == -1)
                         ? false
                         : selected_[pnTakeSelectedFrom][iChoiceWithFocus];
    bool bVisible = bSelected && GAMESTATE->IsHumanPlayer(pn);

    ul.BeginTweening(parent_type_->TWEEN_SECONDS);
    ul.SetVisible(bVisible);
    ul.SetBarWidth(iWidth);
  }
}

void OptionRow::PositionIcons(PlayerNumber pn) {
  ModIcon* pIcon = mod_icons_[pn];
  if (pIcon == nullptr) {
    return;
  }

  pIcon->SetX(parent_type_->MOD_ICON_X.GetValue(pn));
}

// This is called when the focus changes, to update "long row" text.
void OptionRow::UpdateText(PlayerNumber p) {
  if (hand_->def_.layout_type_ == LAYOUT_SHOW_ONE_IN_ROW) {
    unsigned pn = hand_->def_.one_choice_for_all_players_ ? 0 : p;
    int iChoiceWithFocus = choice_in_row_with_focus_[pn];
    if (iChoiceWithFocus == -1) {
      return;
    }

    bool bValueArrow = IsShowOneValueWithGoDown();
    int iDisplayChoice = iChoiceWithFocus;

    if (bValueArrow) {
      // Always display the ▼.
      text_items_[0]->SetText(GetThemedItemText(0));

      if (iChoiceWithFocus == 0) {
        // Focus is on the ▼; show the actively-selected value instead.
        int iSelected = GetOneSelection(p, true);
        iDisplayChoice = (iSelected >= 1) ? iSelected : 1;
      }
    }

    std::string sText = GetThemedItemText(iDisplayChoice);

    int index = std::min(
        static_cast<int>(pn) + (bValueArrow ? 1 : 0),
        static_cast<int>(text_items_.size()) - 1);

    text_items_[index]->SetText(sText);
  }
}

void OptionRow::SetRowHasFocus(PlayerNumber pn, bool bRowHasFocus) {
  row_has_focus_[pn] = bRowHasFocus;
}

void OptionRow::SetDestination(Actor::TweenState& ts, bool bTween) {
  if (frame_.DestTweenState() != ts) {
    frame_.StopTweening();
    if (bTween && parent_type_->TWEEN_SECONDS != 0.0f) {
      frame_.BeginTweening(parent_type_->TWEEN_SECONDS);
    }
    frame_.DestTweenState() = ts;
  }
}

void OptionRow::UpdateEnabledDisabled() {
  bool bThisRowHasFocusByAny = false;
  FOREACH_HumanPlayer(p) bThisRowHasFocusByAny |= row_has_focus_[p];

  bool bThisRowHasFocusByAll = true;
  FOREACH_HumanPlayer(p) bThisRowHasFocusByAll &= row_has_focus_[p];

  bool bRowEnabled = !hand_->def_.enabled_for_players_.empty();

  // Don't tween selection colors at all.
  std::string sCmdName;
  if (bThisRowHasFocusByAny) {
    sCmdName = "GainFocus";
  } else if (bRowEnabled) {
    sCmdName = "LoseFocus";
  } else {
    sCmdName = "Disabled";
  }

  RageColor color;
  if (bThisRowHasFocusByAny) {
    color = parent_type_->COLOR_SELECTED;
  } else if (bRowEnabled) {
    color = parent_type_->COLOR_NOT_SELECTED;
  } else {
    color = parent_type_->COLOR_DISABLED;
  }

  spr_frame_->PlayCommand(sCmdName);
  text_title_->PlayCommand(sCmdName);

  for (unsigned j = 0; j < text_items_.size(); j++) {
    bool bThisItemHasFocusByAny = false;
    FOREACH_HumanPlayer(p) {
      if (row_has_focus_[p]) {
        if ((int)j == GetChoiceInRowWithFocus(p)) {
          bThisItemHasFocusByAny = true;
          break;
        }
      }
    }

    if (bThisItemHasFocusByAny) {
      text_items_[j]->PlayCommand("GainFocus");
    } else {
      text_items_[j]->PlayCommand("LoseFocus");
    }
  }

  switch (hand_->def_.layout_type_) {
    case LAYOUT_SHOW_ALL_IN_ROW:
      for (unsigned j = 0; j < text_items_.size(); j++) {
        if (text_items_[j]->DestTweenState().diffuse_[0] == color) {
          continue;
        }

        text_items_[j]->StopTweening();
        text_items_[j]->BeginTweening(parent_type_->TWEEN_SECONDS);
        text_items_[j]->SetDiffuse(color);
      }

      break;
    case LAYOUT_SHOW_ONE_IN_ROW: {
      bool bValueArrow = IsShowOneValueWithGoDown();

      if (bValueArrow) {
        // Color the ▼ with the row-level color.
        BitmapText& arrow = *text_items_[0];
        if (arrow.DestTweenState().diffuse_[0] != color) {
          arrow.StopTweening();
          arrow.BeginTweening(parent_type_->TWEEN_SECONDS);
          arrow.SetDiffuse(color);
        }
      }

      FOREACH_HumanPlayer(pn) {
        bRowEnabled = hand_->def_.enabled_for_players_.find(pn) !=
                      hand_->def_.enabled_for_players_.end();

        if (!hand_->def_.one_choice_for_all_players_) {
          bool bValueFocused =
              row_has_focus_[pn] &&
              (!bValueArrow || GetChoiceInRowWithFocus(pn) >= 1);
          if (bValueFocused) {
            color = parent_type_->COLOR_SELECTED;
          } else if (bRowEnabled) {
            color = parent_type_->COLOR_NOT_SELECTED;
          } else {
            color = parent_type_->COLOR_DISABLED;
          }
        }

        unsigned item_no = hand_->def_.one_choice_for_all_players_ ? 0 : pn;
        if (bValueArrow) {
          item_no += 1;
        }

        // If player_no is 2 and there is no player 1:
        item_no = std::min<unsigned int>(item_no, text_items_.size() - 1);

        BitmapText& bt = *text_items_[item_no];

        if (bt.DestTweenState().diffuse_[0] != color) {
          bt.StopTweening();
          bt.BeginTweening(parent_type_->TWEEN_SECONDS);
          bt.SetDiffuse(color);
        }
      }
      break;
    }
    default:
      FAIL_M(
          ssprintf("Invalid option row layout: %i", hand_->def_.layout_type_));
  }
}

void OptionRow::SetModIcon(
    PlayerNumber pn, const std::string& sText, GameCommand& gc) {
  // update row frame
  Message msg("Refresh");
  msg.SetParam("GameCommand", &gc);
  msg.SetParam("Text", sText);
  spr_frame_->HandleMessage(msg);
  if (mod_icons_[pn] != nullptr) {
    mod_icons_[pn]->Set(sText);
  }
}

const BitmapText& OptionRow::GetTextItemForRow(
    PlayerNumber pn, int iChoiceOnRow) const {
  bool bOneChoice = hand_->def_.one_choice_for_all_players_;
  int index = -1;
  switch (hand_->def_.layout_type_) {
    case LAYOUT_SHOW_ONE_IN_ROW:
      if (IsShowOneValueWithGoDown()) {
        if (iChoiceOnRow == 0) {
          index = 0;
        } else {
          index = bOneChoice ? 1 : pn + 1;
        }
      } else {
        index = bOneChoice ? 0 : pn;
        if (text_items_.size() == 1) {
          index = 0;
        }
      }
      break;
    case LAYOUT_SHOW_ALL_IN_ROW:
      index = iChoiceOnRow;
      break;
    default:
      FAIL_M(
          ssprintf("Invalid option row layout: %i", hand_->def_.layout_type_));
  }

  ASSERT_M(
      index < (int)text_items_.size(),
      ssprintf("%i < %i", index, (int)text_items_.size()));
  return *text_items_[index];
}

void OptionRow::GetWidthXY(
    PlayerNumber pn, int iChoiceOnRow, int& iWidthOut, int& iXOut,
    int& iYOut) const {
  const BitmapText& text = GetTextItemForRow(pn, iChoiceOnRow);

  iWidthOut = std::lrint(text.GetZoomedWidth());
  iXOut = std::lrint(text.GetDestX());
  iYOut = std::lrint(frame_.GetDestY());
}

int OptionRow::GetOneSelection(PlayerNumber pn, bool bAllowFail) const {
  for (unsigned i = 0; i < selected_[pn].size(); i++) {
    if (selected_[pn][i]) {
      return i;
    }
  }

  ASSERT(
      bAllowFail);  // shouldn't call this if not expecting one to be selected
  return -1;
}

int OptionRow::GetOneSharedSelection(bool bAllowFail) const {
  return GetOneSelection(PLAYER_1, bAllowFail);
}

void OptionRow::SetOneSelection(PlayerNumber pn, int iChoice) {
  std::vector<bool>& vb = selected_[pn];
  if (vb.empty()) {
    return;
  }
  std::fill_n(vb.begin(), vb.size(), false);
  vb[iChoice] = true;
  NotifyHandlerOfSelection(pn, iChoice);
}

void OptionRow::SetOneSharedSelection(int iChoice) {
  FOREACH_PlayerNumber(pn) SetOneSelection(pn, iChoice);
}

void OptionRow::SetOneSharedSelectionIfPresent(const std::string& sChoice) {
  for (unsigned i = 0; i < hand_->def_.choices_.size(); i++) {
    if (sChoice == hand_->def_.choices_[i]) {
      SetOneSharedSelection(i);
      break;
    }
  }
}

int OptionRow::GetChoiceInRowWithFocus(PlayerNumber pn) const {
  if (hand_->def_.one_choice_for_all_players_) {
    pn = PLAYER_1;
  }
  if (hand_->def_.choices_.empty()) {
    return -1;
  }
  int iChoice = choice_in_row_with_focus_[pn];
  return iChoice;
}

void OptionRow::SetChoiceInRowWithFocus(PlayerNumber pn, int iChoice) {
  if (hand_->def_.one_choice_for_all_players_) {
    pn = PLAYER_1;
  }
  ASSERT(iChoice >= 0 && iChoice < (int)hand_->def_.choices_.size());
  choice_in_row_with_focus_[pn] = iChoice;

  UpdateText(pn);
  // PositionUnderlines( pn );
}

void OptionRow::ResetFocusFromSelection(PlayerNumber pn) {
  int iSelection = -1;
  switch (hand_->def_.select_type_) {
    case SELECT_ONE:
      // Import the focus from the selected option.
      iSelection = GetOneSelection(pn, true);
    default:
      break;
  }

  // HACK: Set focus to one item in the row, which is "go down"
  if (first_item_goes_down_) {
    iSelection = 0;
  }

  if (iSelection != -1) {
    SetChoiceInRowWithFocus(pn, iSelection);
  }
}

bool OptionRow::GetSelected(PlayerNumber pn, int iChoice) const {
  if (hand_->def_.one_choice_for_all_players_) {
    pn = PLAYER_1;
  }
  return selected_[pn][iChoice];
}

const OptionRowDefinition& OptionRow::GetRowDef() const { return hand_->def_; }

OptionRowDefinition& OptionRow::GetRowDef() { return hand_->def_; }

bool OptionRow::SetSelected(PlayerNumber pn, int iChoice, bool b) {
  if (hand_->def_.one_choice_for_all_players_) {
    pn = PLAYER_1;
  }
  selected_[pn][iChoice] = b;
  return NotifyHandlerOfSelection(pn, iChoice);
}

bool OptionRow::NotifyHandlerOfSelection(PlayerNumber pn, int choice) {
  bool changed = hand_->NotifyOfSelection(pn, choice - first_item_goes_down_);
  if (changed) {
    ChoicesChanged(row_type_, false);
    std::vector<PlayerNumber> vpns;
    FOREACH_HumanPlayer(p) vpns.push_back(p);
    ImportOptions(vpns);
    FOREACH_PlayerNumber(p) { PositionUnderlines(p); }
    UpdateEnabledDisabled();
  }
  return changed;
}

bool OptionRow::IsShowOneValueWithGoDown() const {
  return first_item_goes_down_ &&
         hand_->def_.layout_type_ == LAYOUT_SHOW_ONE_IN_ROW &&
         hand_->def_.select_type_ == SELECT_ONE;
}

bool OptionRow::GoToFirstOnStart() { return hand_->GoToFirstOnStart(); }

void OptionRow::Reload() {
  // TODO: Nothing uses this yet and it causes skips when changing options.
  /*
  if( m_pHand->m_Def.m_bExportOnChange )
  {
          bool bRowHasFocus[NUM_PLAYERS];
          ZERO( bRowHasFocus );
          ExportOptions( vpns, bRowHasFocus );
  }
  */

  switch (hand_->Reload()) {
    case RELOAD_CHANGED_NONE:
      break;

    case RELOAD_CHANGED_ALL: {
      ChoicesChanged(row_type_);

      std::vector<PlayerNumber> vpns;
      FOREACH_HumanPlayer(p) vpns.push_back(p);
      ImportOptions(vpns);
      FOREACH_HumanPlayer(p) AfterImportOptions(p);
      [[fallthrough]];
    }

    case RELOAD_CHANGED_ENABLED:
      UpdateEnabledDisabled();
      FOREACH_HumanPlayer(pn) PositionUnderlines(pn);
      break;

    default:
      break;
  }

  // TODO: Nothing uses this yet and it causes skips when changing options.
  /*
  if( m_pHand->m_Def.m_bExportOnChange )
  {
          bool bRowHasFocus[NUM_PLAYERS];
          ZERO( bRowHasFocus );
          ExportOptions( vpns, bRowHasFocus );
  }
  */
}

void OptionRow::HandleMessage(const Message& msg) {
  bool bReload = false;
  for (const std::string& m : hand_->reload_row_messages_) {
    if (m == msg.GetName()) {
      bReload = true;
      break;
    }
  }
  if (bReload) {
    Reload();
  }

  ActorFrame::HandleMessage(msg);
}

/* Hack: the NextRow entry is never set, and should be transparent.
 * Remove it, and readd it below. */
#define ERASE_ONE_BOOL_AT_FRONT_IF_NEEDED(vbSelected) \
  if (GetFirstItemGoesDown()) vbSelected.erase(vbSelected.begin());
#define INSERT_ONE_BOOL_AT_FRONT_IF_NEEDED(vbSelected) \
  if (GetFirstItemGoesDown()) vbSelected.insert(vbSelected.begin(), false);

void OptionRow::ImportOptions(const std::vector<PlayerNumber>& vpns) {
  ASSERT(hand_->def_.choices_.size() > 0);

  for (const PlayerNumber& p : vpns) {
    std::fill_n(selected_[p].begin(), selected_[p].size(), false);

    ASSERT(selected_[p].size() == hand_->def_.choices_.size());
    ERASE_ONE_BOOL_AT_FRONT_IF_NEEDED(selected_[p]);
  }

  hand_->ImportOption(this, vpns, selected_);

  for (const PlayerNumber& p : vpns) {
    INSERT_ONE_BOOL_AT_FRONT_IF_NEEDED(selected_[p]);
    VerifySelected(hand_->def_.select_type_, selected_[p], hand_->def_.name_);
  }
}

int OptionRow::ExportOptions(
    const std::vector<PlayerNumber>& vpns, bool bRowHasFocus[NUM_PLAYERS]) {
  ASSERT(hand_->def_.choices_.size() > 0);

  int iChangeMask = 0;

  for (const PlayerNumber& p : vpns) {
    bool bFocus = bRowHasFocus[p];

    VerifySelected(hand_->def_.select_type_, selected_[p], hand_->def_.name_);
    ASSERT(selected_[p].size() == hand_->def_.choices_.size());
    ERASE_ONE_BOOL_AT_FRONT_IF_NEEDED(selected_[p]);

    // SELECT_NONE rows get exported if they have focus when the user presses
    // Start.
    int iChoice = GetChoiceInRowWithFocus(p);
    if (hand_->def_.select_type_ == SELECT_NONE && bFocus) {
      selected_[p][iChoice] = true;
    }
  }

  iChangeMask |= hand_->ExportOption(vpns, selected_);

  for (const PlayerNumber& p : vpns) {
    bool bFocus = bRowHasFocus[p];

    int iChoice = GetChoiceInRowWithFocus(p);
    if (hand_->def_.select_type_ == SELECT_NONE && bFocus) {
      selected_[p][iChoice] = false;
    }

    INSERT_ONE_BOOL_AT_FRONT_IF_NEEDED(selected_[p]);
  }

  return iChangeMask;
}

// lua start
#include "LuaBinding.h"

class LunaOptionRow : public Luna<OptionRow> {
 public:
  DEFINE_METHOD(FirstItemGoesDown, GetFirstItemGoesDown())
  static int GetChoiceInRowWithFocus(T* p, lua_State* L) {
    lua_pushnumber(
        L, p->GetChoiceInRowWithFocus(Enum::Check<PlayerNumber>(L, 1)));
    return 1;
  }
  DEFINE_METHOD(GetLayoutType, GetHandler()->def_.layout_type_)
  static int GetName(T* p, lua_State* L) {
    lua_pushstring(L, p->GetHandler()->def_.name_.c_str());
    return 1;
  }
  static int GetNumChoices(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetHandler()->def_.choices_.size());
    return 1;
  }
  DEFINE_METHOD(GetSelectType, GetHandler()->def_.select_type_)
  DEFINE_METHOD(GetRowTitle, GetRowTitle())
  static int HasFocus(T* p, lua_State* L) {
    lua_pushboolean(L, p->GetRowHasFocus(Enum::Check<PlayerNumber>(L, 1)));
    return 1;
  }
  static int OneChoiceForAllPlayers(T* p, lua_State* L) {
    lua_pushboolean(L, p->GetHandler()->def_.one_choice_for_all_players_);
    return 1;
  }

  LunaOptionRow() {
    ADD_METHOD(FirstItemGoesDown);
    ADD_METHOD(GetChoiceInRowWithFocus);
    ADD_METHOD(GetLayoutType);
    ADD_METHOD(GetName);
    ADD_METHOD(GetNumChoices);
    ADD_METHOD(GetRowTitle);
    ADD_METHOD(GetSelectType);
    ADD_METHOD(HasFocus);
    ADD_METHOD(OneChoiceForAllPlayers);
  }
};

LUA_REGISTER_DERIVED_CLASS(OptionRow, ActorFrame)
// lua end

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
