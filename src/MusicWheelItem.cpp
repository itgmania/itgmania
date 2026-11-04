#include "MusicWheelItem.h"

#include <string>

#include "ActorUtil.h"
#include "BitmapText.h"
#include "Course.h"
#include "Difficulty.h"
#include "EnumHelper.h"
#include "GameConstantsAndTypes.h"
#include "GameManager.h"
#include "GameState.h"
#include "Group.h"
#include "HighScore.h"
#include "MessageManager.h"
#include "PlayerNumber.h"
#include "Profile.h"
#include "ProfileManager.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "ScreenManager.h"
#include "Song.h"
#include "SongManager.h"
#include "SongUtil.h"
#include "Steps.h"
#include "Style.h"
#include "ThemeManager.h"
#include "ThemeMetric.h"
#include "WheelItemBase.h"
#include "WheelNotifyIcon.h"
#include "global.h"

static const char* MusicWheelItemTypeNames[] = {
    "Song",
    "ParentExpanded",
    "ParentCollapsed",
    "SectionExpanded",
    "SectionCollapsed",
    "Roulette",
    "Course",
    "Sort",
    "Mode",
    "Random",
    "Portal",
    "Custom",
};
XToString(MusicWheelItemType);

MusicWheelItemData::MusicWheelItemData(
    WheelItemDataType type, Song* pSong, std::string sSectionName,
    Course* pCourse, Group* pGroup, RageColor color, int iSectionCount,
    std::string sParentSection)
    : WheelItemBaseData(type, sSectionName, color),
      course_(pCourse),
      song_(pSong),
      group_(pGroup),
      flags_(WheelNotifyIcon::Flags()),
      section_count_(iSectionCount),
      parent_section_(sParentSection),
      label_(""),
      action_() {}

MusicWheelItem::MusicWheelItem(std::string sType) : WheelItemBase(sType) {
  GRADES_SHOW_MACHINE.Load(sType, "GradesShowMachine");

  FOREACH_ENUM(MusicWheelItemType, i) {
    spr_color_part_[i].Load(
        THEME->GetPathG(sType, MusicWheelItemTypeToString(i) + " ColorPart"));
    spr_color_part_[i]->SetName(MusicWheelItemTypeToString(i) + "ColorPart");
    ActorUtil::LoadAllCommands(spr_color_part_[i], "MusicWheelItem");
    this->AddChild(spr_color_part_[i]);

    spr_normal_part_[i].Load(
        THEME->GetPathG(sType, MusicWheelItemTypeToString(i) + " NormalPart"));
    spr_normal_part_[i]->SetName(MusicWheelItemTypeToString(i) + "NormalPart");
    ActorUtil::LoadAllCommands(spr_normal_part_[i], "MusicWheelItem");
    this->AddChild(spr_normal_part_[i]);
  }

  text_banner_.SetName("SongName");
  ActorUtil::LoadAllCommands(text_banner_, "MusicWheelItem");
  text_banner_.Load("TextBanner");
  ActorUtil::SetXY(text_banner_, "MusicWheelItem");
  text_banner_.PlayCommand("On");
  this->AddChild(&text_banner_);

  FOREACH_ENUM(MusicWheelItemType, i) {
    spr_over_part_[i].Load(
        THEME->GetPathG(sType, MusicWheelItemTypeToString(i) + " OverPart"));
    spr_over_part_[i]->SetName(MusicWheelItemTypeToString(i) + "OverPart");
    ActorUtil::LoadAllCommands(spr_over_part_[i], "MusicWheelItem");
    this->AddChild(spr_over_part_[i]);
  }

  FOREACH_ENUM(MusicWheelItemType, i) {
    text_[i] = nullptr;

    // Don't init text for Type_Song. It uses a TextBanner.
    if (i == MusicWheelItemType_Song) {
      continue;
    }

    text_[i] = new BitmapText;
    text_[i]->SetName(MusicWheelItemTypeToString(i));
    ActorUtil::LoadAllCommands(text_[i], "MusicWheelItem");
    text_[i]->LoadFromFont(
        THEME->GetPathF(sType, MusicWheelItemTypeToString(i)));
    ActorUtil::SetXY(text_[i], "MusicWheelItem");
    text_[i]->PlayCommand("On");
    this->AddChild(text_[i]);
  }

  text_section_count_ = new BitmapText;
  text_section_count_->SetName("SectionCount");
  ActorUtil::LoadAllCommands(text_section_count_, "MusicWheelItem");
  text_section_count_->LoadFromFont(THEME->GetPathF(sType, "SectionCount"));
  ActorUtil::SetXY(text_section_count_, "MusicWheelItem");
  text_section_count_->PlayCommand("On");
  this->AddChild(text_section_count_);

  wheel_notify_icon_.SetName("WheelNotifyIcon");
  ActorUtil::LoadAllCommands(wheel_notify_icon_, "MusicWheelItem");
  ActorUtil::SetXY(wheel_notify_icon_, "MusicWheelItem");
  wheel_notify_icon_.PlayCommand("On");
  this->AddChild(&wheel_notify_icon_);

  FOREACH_PlayerNumber(p) {
    grade_display_[p].Load(THEME->GetPathG(sType, "grades"));
    grade_display_[p]->SetName(ssprintf("GradeP%d", int(p + 1)));
    this->AddChild(grade_display_[p]);
    LOAD_ALL_COMMANDS_AND_SET_XY(grade_display_[p]);
  }

  this->SubscribeToMessage(Message_CurrentStepsP1Changed);
  this->SubscribeToMessage(Message_CurrentStepsP2Changed);
  this->SubscribeToMessage(Message_CurrentTrailP1Changed);
  this->SubscribeToMessage(Message_CurrentTrailP2Changed);
  this->SubscribeToMessage(Message_PreferredDifficultyP1Changed);
  this->SubscribeToMessage(Message_PreferredDifficultyP2Changed);
  this->SubscribeToMessage(Message_PlayerProfileSet);
}

MusicWheelItem::MusicWheelItem(const MusicWheelItem& cpy)
    : WheelItemBase(cpy),
      GRADES_SHOW_MACHINE(cpy.GRADES_SHOW_MACHINE),
      text_banner_(cpy.text_banner_),
      wheel_notify_icon_(cpy.wheel_notify_icon_) {
  FOREACH_ENUM(MusicWheelItemType, i) {
    spr_color_part_[i] = cpy.spr_color_part_[i];
    this->AddChild(spr_color_part_[i]);

    spr_normal_part_[i] = cpy.spr_normal_part_[i];
    this->AddChild(spr_normal_part_[i]);
  }

  this->AddChild(&text_banner_);

  FOREACH_ENUM(MusicWheelItemType, i) {
    spr_over_part_[i] = cpy.spr_over_part_[i];
    this->AddChild(spr_over_part_[i]);
  }

  FOREACH_ENUM(MusicWheelItemType, i) {
    if (cpy.text_[i] == nullptr) {
      text_[i] = nullptr;
    } else {
      text_[i] = new BitmapText(*cpy.text_[i]);
      this->AddChild(text_[i]);
    }
  }

  text_section_count_ = new BitmapText(*cpy.text_section_count_);
  this->AddChild(text_section_count_);

  this->AddChild(&wheel_notify_icon_);

  FOREACH_PlayerNumber(p) {
    grade_display_[p] = cpy.grade_display_[p];
    this->AddChild(grade_display_[p]);
  }
}

MusicWheelItem::~MusicWheelItem() {
  FOREACH_ENUM(MusicWheelItemType, i) { RageUtil::SafeDelete(text_[i]); }
  delete text_section_count_;
}

void MusicWheelItem::LoadFromWheelItemData(
    const WheelItemBaseData* pData, int iIndex, bool bHasFocus,
    int iDrawIndex) {
  WheelItemBase::LoadFromWheelItemData(pData, iIndex, bHasFocus, iDrawIndex);

  const MusicWheelItemData* pWID =
      dynamic_cast<const MusicWheelItemData*>(pData);

  // hide all
  FOREACH_ENUM(MusicWheelItemType, i) {
    spr_color_part_[i]->SetVisible(false);
    spr_normal_part_[i]->SetVisible(false);
    spr_over_part_[i]->SetVisible(false);
  }
  text_banner_.SetVisible(false);
  FOREACH_ENUM(MusicWheelItemType, i)
  if (text_[i]) {
    text_[i]->SetVisible(false);
  }
  text_section_count_->SetVisible(false);
  wheel_notify_icon_.SetVisible(false);
  FOREACH_PlayerNumber(p) grade_display_[p]->SetVisible(false);

  // Fill these in below
  std::string sDisplayName, sTranslitName;
  MusicWheelItemType type = MusicWheelItemType_Invalid;
  const bool isChildSection = pWID->type_ == WheelItemDataType_Section &&
                              !pWID->parent_section_.empty();

  switch (pWID->type_) {
    DEFAULT_FAIL(pWID->type_);
    case WheelItemDataType_Song:
      type = MusicWheelItemType_Song;

      text_banner_.SetFromSong(pWID->song_);
      // We can do this manually if we wanted... maybe have a metric for
      // overrides? -aj
      text_banner_.SetDiffuse(pWID->color_);
      text_banner_.SetVisible(true);

      wheel_notify_icon_.SetFlags(pWID->flags_);
      wheel_notify_icon_.SetVisible(true);
      RefreshGrades();
      break;
    case WheelItemDataType_ParentSection: {
      sDisplayName = SONGMAN->ShortenSeriesName(
          pWID->label_.empty() ? pWID->text_ : pWID->label_);
      sTranslitName = sDisplayName;
      if (GAMESTATE->expanded_parent_section_name_ == pWID->text_) {
        type = MusicWheelItemType_ParentExpanded;
      } else {
        type = MusicWheelItemType_ParentCollapsed;
      }
      text_section_count_->SetText(ssprintf("%d", pWID->section_count_));
      text_section_count_->SetVisible(true);
    } break;
    case WheelItemDataType_Section: {
      if (pWID->group_ == nullptr) {
        sDisplayName = SONGMAN->ShortenGroupName(pWID->text_);
      } else {
        sDisplayName =
            SONGMAN->ShortenGroupName(pWID->group_->GetDisplayTitle());
        sTranslitName =
            SONGMAN->ShortenGroupName(pWID->group_->GetTranslitTitle());
      }
      if (GAMESTATE->expanded_section_name_ == pWID->text_) {
        type = MusicWheelItemType_SectionExpanded;
      } else {
        type = MusicWheelItemType_SectionCollapsed;
      }

      text_section_count_->SetText(ssprintf("%d", pWID->section_count_));
      text_section_count_->SetVisible(true);
    } break;
    case WheelItemDataType_Course:
      sDisplayName = pWID->course_->GetDisplayFullTitle();
      sTranslitName = pWID->course_->GetTranslitFullTitle();
      type = MusicWheelItemType_Course;
      wheel_notify_icon_.SetFlags(pWID->flags_);
      wheel_notify_icon_.SetVisible(true);
      break;
    case WheelItemDataType_Sort:
      sDisplayName = pWID->label_;
      // hack to get mode items working. -freem
      if (pWID->action_->pm_ != PlayMode_Invalid) {
        type = MusicWheelItemType_Mode;
      } else {
        type = MusicWheelItemType_Sort;
      }
      break;
    case WheelItemDataType_Roulette:
      sDisplayName = THEME->GetString("MusicWheel", "Roulette");
      type = MusicWheelItemType_Roulette;
      break;
    case WheelItemDataType_Random:
      sDisplayName = THEME->GetString("MusicWheel", "Random");
      type = MusicWheelItemType_Random;
      break;
    case WheelItemDataType_Portal:
      sDisplayName = THEME->GetString("MusicWheel", "Portal");
      type = MusicWheelItemType_Portal;
      break;
    case WheelItemDataType_Custom:
      sDisplayName = pWID->label_;
      type = MusicWheelItemType_Custom;
      break;
  }

  spr_color_part_[type]->SetVisible(true);
  spr_color_part_[type]->SetDiffuse(pWID->color_);
  spr_normal_part_[type]->SetVisible(true);
  spr_over_part_[type]->SetVisible(true);
  BitmapText* bt = text_[type];
  if (bt) {
    bt->SetText(sDisplayName, sTranslitName);
    bt->SetDiffuse(pWID->color_);
    bt->SetVisible(true);
    if (isChildSection) {
      bt->SetX(THEME->GetMetricF("MusicWheelItem", "ChildSectionX"));
      bt->SetY(THEME->GetMetricF("MusicWheelItem", "ChildSectionY"));
    } else {
      ActorUtil::SetXY(*bt, "MusicWheelItem");
    }
  }

  if (text_section_count_) {
    if (isChildSection) {
      text_section_count_->SetX(
          THEME->GetMetricF("MusicWheelItem", "ChildSectionCountX"));
      text_section_count_->SetY(
          THEME->GetMetricF("MusicWheelItem", "ChildSectionCountY"));
    } else {
      ActorUtil::SetXY(*text_section_count_, "MusicWheelItem");
    }
  }

  FOREACH_ENUM(MusicWheelItemType, i) {
    if (spr_color_part_[i]->GetVisible()) {
      SetGrayBar(spr_color_part_[i]);
      break;
    }
  }

  // Call "Set" so that elements like TextBanner react to the change in song.
  {
    Message msg("Set");
    msg.SetParam("Song", pWID->song_);
    msg.SetParam("Course", pWID->course_);
    msg.SetParam("Index", iIndex);
    msg.SetParam("HasFocus", bHasFocus);
    msg.SetParam("Text", pWID->text_);
    msg.SetParam("DrawIndex", iDrawIndex);
    msg.SetParam("Type", MusicWheelItemTypeToString(type));
    msg.SetParam("Color", pWID->color_);
    msg.SetParam("Label", pWID->label_);
    msg.SetParam("ParentSection", pWID->parent_section_);
    msg.SetParam(
        "IsParentSection", pWID->type_ == WheelItemDataType_ParentSection);

    this->HandleMessage(msg);
  }
}

void MusicWheelItem::RefreshGrades() {
  if (!IsLoaded()) {
    return;
  }
  const MusicWheelItemData* pWID =
      dynamic_cast<const MusicWheelItemData*>(data_);

  if (pWID == nullptr) {
    return;  // LoadFromWheelItemData() hasn't been called yet.
  }
  FOREACH_HumanPlayer(p) {
    grade_display_[p]->SetVisible(false);

    if (pWID->song_ == nullptr && pWID->course_ == nullptr) {
      continue;
    }

    Difficulty dc;
    if (GAMESTATE->cur_steps_[p]) {
      dc = GAMESTATE->cur_steps_[p]->GetDifficulty();
    } else if (GAMESTATE->cur_trail_[p]) {
      dc = GAMESTATE->cur_trail_[p]->course_difficulty_;
    } else {
      dc = GAMESTATE->preferred_difficulty_[p];
    }

    ProfileSlot ps;
    if (PROFILEMAN->IsPersistentProfile(p)) {
      ps = (ProfileSlot)p;
    } else if (GRADES_SHOW_MACHINE) {
      ps = ProfileSlot_Machine;
    } else {
      continue;
    }

    StepsType st;
    if (GAMESTATE->cur_steps_[p]) {
      st = GAMESTATE->cur_steps_[p]->steps_type_;
    } else if (GAMESTATE->cur_trail_[p]) {
      st = GAMESTATE->cur_trail_[p]->steps_type_;
    } else {
      st = GAMESTATE->GetCurrentStyle(PLAYER_INVALID)->steps_type_;
    }

    grade_display_[p]->SetVisible(true);

    Profile* pProfile = PROFILEMAN->GetProfile(ps);

    HighScoreList* pHSL = nullptr;
    if (PROFILEMAN->IsPersistentProfile(ps) && dc != Difficulty_Invalid) {
      if (pWID->song_) {
        const Steps* pSteps =
            SongUtil::GetStepsByDifficulty(pWID->song_, st, dc);
        if (pSteps != nullptr) {
          pHSL = &pProfile->GetStepsHighScoreList(pWID->song_, pSteps);
        }
      } else if (pWID->course_) {
        const Trail* pTrail = pWID->course_->GetTrail(st, dc);
        if (pTrail != nullptr) {
          pHSL = &pProfile->GetCourseHighScoreList(pWID->course_, pTrail);
        }
      }
    }

    Message msg("SetGrade");
    msg.SetParam("PlayerNumber", p);
    if (pHSL) {
      msg.SetParam("Grade", pHSL->high_grade_);
      msg.SetParam("NumTimesPlayed", pHSL->GetNumTimesPlayed());

      // Until we get a Grade for quints (need W0 support), send back the entire
      // HighScoreList so we can determine if a player has a quint or not.
      // TODO: Remove this once we have W0 support.
      msg.SetParam("HighScoreList", pHSL);
    }
    grade_display_[p]->HandleMessage(msg);
  }
}

void MusicWheelItem::HandleMessage(const Message& msg) {
  if (!IsLoaded()) {
    return;
  }
  if (msg == Message_CurrentStepsP1Changed ||
      msg == Message_CurrentStepsP2Changed ||
      msg == Message_CurrentTrailP1Changed ||
      msg == Message_CurrentTrailP2Changed ||
      msg == Message_PreferredDifficultyP1Changed ||
      msg == Message_PreferredDifficultyP2Changed ||
      msg == Message_PlayerProfileSet) {
    const MusicWheelItemData* pWID =
        dynamic_cast<const MusicWheelItemData*>(data_);
    MusicWheelItemType type = MusicWheelItemType_Invalid;

    switch (pWID->type_) {
      DEFAULT_FAIL(pWID->type_);
      case WheelItemDataType_Song:
        type = MusicWheelItemType_Song;
        break;
      case WheelItemDataType_Section:
        if (GAMESTATE->expanded_section_name_ == pWID->text_) {
          type = MusicWheelItemType_SectionExpanded;
        } else {
          type = MusicWheelItemType_SectionCollapsed;
        }
        break;
      case WheelItemDataType_ParentSection:
        if (GAMESTATE->expanded_parent_section_name_ == pWID->text_) {
          type = MusicWheelItemType_ParentExpanded;
        } else {
          type = MusicWheelItemType_ParentCollapsed;
        }
        break;
      case WheelItemDataType_Course:
        type = MusicWheelItemType_Course;
        break;
      case WheelItemDataType_Sort:
        if (pWID->action_->pm_ != PlayMode_Invalid) {
          type = MusicWheelItemType_Mode;
        } else {
          type = MusicWheelItemType_Sort;
        }
        break;
      case WheelItemDataType_Roulette:
        type = MusicWheelItemType_Roulette;
        break;
      case WheelItemDataType_Random:
        type = MusicWheelItemType_Random;
        break;
      case WheelItemDataType_Portal:
        type = MusicWheelItemType_Portal;
        break;
      case WheelItemDataType_Custom:
        type = MusicWheelItemType_Custom;
        break;
    }

    Message setMsg("Set");
    setMsg.SetParam("Song", pWID->song_);
    setMsg.SetParam("Course", pWID->course_);
    setMsg.SetParam("Text", pWID->text_);
    setMsg.SetParam("Type", MusicWheelItemTypeToString(type));
    setMsg.SetParam("Color", pWID->color_);
    setMsg.SetParam("Label", pWID->label_);
    setMsg.SetParam("ParentSection", pWID->parent_section_);
    setMsg.SetParam(
        "IsParentSection", pWID->type_ == WheelItemDataType_ParentSection);
    this->HandleMessage(setMsg);

    RefreshGrades();
  }

  WheelItemBase::HandleMessage(msg);
}

/*
 * (c) 2001-2004 Chris Danford, Chris Gomez, Glenn Maynard
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
