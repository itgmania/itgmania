#include "MusicWheelItem.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

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
#include "PrefsManager.h"
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
    WheelItemDataType type, Song* song, std::string section_name,
    Course* course, Group* group, RageColor color, int section_count,
    std::string parent_section)
    : WheelItemBaseData(type, section_name, color),
      course_(course),
      song_(song),
      group_(group),
      flags_(WheelNotifyIcon::Flags()),
      pinned_difficulty_(Difficulty_Invalid),
      pinned_edit_description_(""),
      pinned_meter_(-1),
      section_count_(section_count),
      parent_section_(parent_section),
      label_(""),
      action_() {}

bool MusicWheelItemData::IsPinned() const {
  if (pinned_difficulty_ != Difficulty_Invalid) {
    return true;
  }
  return pinned_meter_ >= 0 && PREFSMAN != nullptr &&
         PREFSMAN->pin_steps_in_meter_sort_;
}

void MusicWheelItemData::GetPinnedStepsList(std::vector<Steps*>& out) const {
  out.clear();
  if (!IsPinned() || song_ == nullptr) {
    return;
  }

  std::vector<Steps*> playable_steps;
  SongUtil::GetPlayableSteps(song_, playable_steps);

  // Pinning is based on difficulty (ie: within a playlist).
  if (pinned_difficulty_ != Difficulty_Invalid) {
    for (Steps* steps : playable_steps) {
      if (steps->GetDifficulty() != pinned_difficulty_) {
        continue;
      }
      if (pinned_difficulty_ == Difficulty_Edit &&
          !pinned_edit_description_.empty()) {
        // Compare both for compatibility with different .sm and .ssc formats.
        if (CompareNoCase(steps->GetDescription(), pinned_edit_description_) !=
                0 &&
            CompareNoCase(steps->GetChartName(), pinned_edit_description_) !=
                0) {
          continue;
        }
      }
      out.push_back(steps);
    }
    return;
  }

  // Pinning is based on meter (ie: sort by meter).
  if (pinned_meter_ >= 0 && PREFSMAN != nullptr &&
      PREFSMAN->pin_steps_in_meter_sort_) {
    for (Steps* steps : playable_steps) {
      if (steps->GetMeter() == pinned_meter_) {
        out.push_back(steps);
      }
    }
  }
}

MusicWheelItem::MusicWheelItem(std::string type) : WheelItemBase(type) {
  GRADES_SHOW_MACHINE.Load(type, "GradesShowMachine");

  FOREACH_ENUM(MusicWheelItemType, i) {
    spr_color_part_[i].Load(
        THEME->GetPathG(type, MusicWheelItemTypeToString(i) + " ColorPart"));
    spr_color_part_[i]->SetName(MusicWheelItemTypeToString(i) + "ColorPart");
    ActorUtil::LoadAllCommands(spr_color_part_[i], "MusicWheelItem");
    this->AddChild(spr_color_part_[i]);

    spr_normal_part_[i].Load(
        THEME->GetPathG(type, MusicWheelItemTypeToString(i) + " NormalPart"));
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
        THEME->GetPathG(type, MusicWheelItemTypeToString(i) + " OverPart"));
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
        THEME->GetPathF(type, MusicWheelItemTypeToString(i)));
    ActorUtil::SetXY(text_[i], "MusicWheelItem");
    text_[i]->PlayCommand("On");
    this->AddChild(text_[i]);
  }

  text_section_count_ = new BitmapText;
  text_section_count_->SetName("SectionCount");
  ActorUtil::LoadAllCommands(text_section_count_, "MusicWheelItem");
  text_section_count_->LoadFromFont(THEME->GetPathF(type, "SectionCount"));
  ActorUtil::SetXY(text_section_count_, "MusicWheelItem");
  text_section_count_->PlayCommand("On");
  this->AddChild(text_section_count_);

  wheel_notify_icon_.SetName("WheelNotifyIcon");
  ActorUtil::LoadAllCommands(wheel_notify_icon_, "MusicWheelItem");
  ActorUtil::SetXY(wheel_notify_icon_, "MusicWheelItem");
  wheel_notify_icon_.PlayCommand("On");
  this->AddChild(&wheel_notify_icon_);

  FOREACH_PlayerNumber(p) {
    grade_display_[p].Load(THEME->GetPathG(type, "grades"));
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
    const WheelItemBaseData* data, int index, int draw_index) {
  WheelItemBase::LoadFromWheelItemData(data, index, draw_index);

  const MusicWheelItemData* wid = dynamic_cast<const MusicWheelItemData*>(data);

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
  std::string display_name, translit_name;
  MusicWheelItemType type = MusicWheelItemType_Invalid;
  const bool is_child_section =
      wid->type_ == WheelItemDataType_Section && !wid->parent_section_.empty();

  switch (wid->type_) {
    DEFAULT_FAIL(wid->type_);
    case WheelItemDataType_Song:
      type = MusicWheelItemType_Song;

      text_banner_.SetFromSong(wid->song_);
      // We can do this manually if we wanted... maybe have a metric for
      // overrides? -aj
      text_banner_.SetDiffuse(wid->color_);
      text_banner_.SetVisible(true);

      wheel_notify_icon_.SetFlags(wid->flags_);
      wheel_notify_icon_.SetVisible(true);
      RefreshGrades();
      break;
    case WheelItemDataType_ParentSection: {
      display_name = SONGMAN->ShortenSeriesName(
          wid->label_.empty() ? wid->text_ : wid->label_);
      translit_name = display_name;
      if (GAMESTATE->expanded_parent_section_name_ == wid->text_) {
        type = MusicWheelItemType_ParentExpanded;
      } else {
        type = MusicWheelItemType_ParentCollapsed;
      }
      text_section_count_->SetText(ssprintf("%d", wid->section_count_));
      text_section_count_->SetVisible(true);
    } break;
    case WheelItemDataType_Section: {
      if (wid->group_ == nullptr) {
        display_name = SONGMAN->ShortenGroupName(wid->text_);
      } else {
        display_name =
            SONGMAN->ShortenGroupName(wid->group_->GetDisplayTitle());
        translit_name =
            SONGMAN->ShortenGroupName(wid->group_->GetTranslitTitle());
      }
      if (GAMESTATE->expanded_section_name_ == wid->text_) {
        type = MusicWheelItemType_SectionExpanded;
      } else {
        type = MusicWheelItemType_SectionCollapsed;
      }

      text_section_count_->SetText(ssprintf("%d", wid->section_count_));
      text_section_count_->SetVisible(true);
    } break;
    case WheelItemDataType_Course:
      display_name = wid->course_->GetDisplayFullTitle();
      translit_name = wid->course_->GetTranslitFullTitle();
      type = MusicWheelItemType_Course;
      wheel_notify_icon_.SetFlags(wid->flags_);
      wheel_notify_icon_.SetVisible(true);
      break;
    case WheelItemDataType_Sort:
      display_name = wid->label_;
      // hack to get mode items working. -freem
      if (wid->action_->pm_ != PlayMode_Invalid) {
        type = MusicWheelItemType_Mode;
      } else {
        type = MusicWheelItemType_Sort;
      }
      break;
    case WheelItemDataType_Roulette:
      display_name = THEME->GetString("MusicWheel", "Roulette");
      type = MusicWheelItemType_Roulette;
      break;
    case WheelItemDataType_Random:
      display_name = THEME->GetString("MusicWheel", "Random");
      type = MusicWheelItemType_Random;
      break;
    case WheelItemDataType_Portal:
      display_name = THEME->GetString("MusicWheel", "Portal");
      type = MusicWheelItemType_Portal;
      break;
    case WheelItemDataType_Custom:
      display_name = wid->label_;
      type = MusicWheelItemType_Custom;
      break;
  }

  spr_color_part_[type]->SetVisible(true);
  spr_color_part_[type]->SetDiffuse(wid->color_);
  spr_normal_part_[type]->SetVisible(true);
  spr_over_part_[type]->SetVisible(true);
  BitmapText* bt = text_[type];
  if (bt) {
    bt->SetText(display_name, translit_name);
    bt->SetDiffuse(wid->color_);
    bt->SetVisible(true);
    if (is_child_section) {
      bt->SetX(THEME->GetMetricF("MusicWheelItem", "ChildSectionX"));
      bt->SetY(THEME->GetMetricF("MusicWheelItem", "ChildSectionY"));
    } else {
      ActorUtil::SetXY(*bt, "MusicWheelItem");
    }
  }

  if (text_section_count_) {
    if (is_child_section) {
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
    msg.SetParam("Song", wid->song_);
    msg.SetParam("Course", wid->course_);
    msg.SetParam("Index", index);
    msg.SetParam("Text", wid->text_);
    msg.SetParam("DrawIndex", draw_index);
    msg.SetParam("Type", MusicWheelItemTypeToString(type));
    msg.SetParam("Color", wid->color_);
    msg.SetParam("Label", wid->label_);
    msg.SetParam("ParentSection", wid->parent_section_);
    msg.SetParam(
        "IsParentSection", wid->type_ == WheelItemDataType_ParentSection);

    this->HandleMessage(msg);
  }
}

void MusicWheelItem::RefreshGrades() {
  if (!IsLoaded()) {
    return;
  }
  const MusicWheelItemData* wid =
      dynamic_cast<const MusicWheelItemData*>(data_);

  if (wid == nullptr) {
    return;  // LoadFromWheelItemData() hasn't been called yet.
  }

  std::vector<Steps*> pinned_list;
  if (wid->song_ && wid->IsPinned()) {
    wid->GetPinnedStepsList(pinned_list);
  }

  FOREACH_HumanPlayer(p) {
    grade_display_[p]->SetVisible(false);

    if (wid->song_ == nullptr && wid->course_ == nullptr) {
      continue;
    }

    Difficulty dc;
    if (GAMESTATE->HasPinnedSteps() &&
        GAMESTATE->preferred_difficulty_[p] != Difficulty_Invalid) {
      dc = GAMESTATE->preferred_difficulty_[p];
    } else if (GAMESTATE->cur_steps_[p]) {
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

    Profile* profile = PROFILEMAN->GetProfile(ps);

    HighScoreList* hsl = nullptr;
    if (PROFILEMAN->IsPersistentProfile(ps)) {
      if (wid->song_ && wid->IsPinned()) {
        const Steps* steps = nullptr;
        auto cur_it = std::find(
            pinned_list.begin(), pinned_list.end(), GAMESTATE->cur_steps_[p]);
        if (cur_it != pinned_list.end()) {
          // Active song: show the grade for the pinned chart the player chose.
          steps = *cur_it;
        } else if (!pinned_list.empty()) {
          // Neighboring wheel items: pick the pinned chart closest to dc.
          steps = pinned_list[0];
          if (dc != Difficulty_Invalid) {
            int best_diff = std::abs(steps->GetDifficulty() - dc);
            for (size_t i = 1; i < pinned_list.size(); ++i) {
              int d = std::abs(pinned_list[i]->GetDifficulty() - dc);
              if (d < best_diff) {
                steps = pinned_list[i];
                best_diff = d;
              }
            }
          }
        }
        if (steps != nullptr) {
          hsl = &profile->GetStepsHighScoreList(wid->song_, steps);
        }
      } else if (dc != Difficulty_Invalid) {
        if (wid->song_) {
          const Steps* steps =
              SongUtil::GetStepsByDifficulty(wid->song_, st, dc);
          if (steps != nullptr) {
            hsl = &profile->GetStepsHighScoreList(wid->song_, steps);
          }
        } else if (wid->course_) {
          const Trail* trail = wid->course_->GetTrail(st, dc);
          if (trail != nullptr) {
            hsl = &profile->GetCourseHighScoreList(wid->course_, trail);
          }
        }
      }
    }

    Message msg("SetGrade");
    msg.SetParam("PlayerNumber", p);
    if (hsl) {
      msg.SetParam("Grade", hsl->high_grade_);
      msg.SetParam("NumTimesPlayed", hsl->GetNumTimesPlayed());

      // Until we get a Grade for quints (need W0 support), send back the entire
      // HighScoreList so we can determine if a player has a quint or not.
      // TODO: Remove this once we have W0 support.
      msg.SetParam("HighScoreList", hsl);
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
    const MusicWheelItemData* wid =
        dynamic_cast<const MusicWheelItemData*>(data_);
    MusicWheelItemType type = MusicWheelItemType_Invalid;

    switch (wid->type_) {
      DEFAULT_FAIL(wid->type_);
      case WheelItemDataType_Song:
        type = MusicWheelItemType_Song;
        break;
      case WheelItemDataType_Section:
        if (GAMESTATE->expanded_section_name_ == wid->text_) {
          type = MusicWheelItemType_SectionExpanded;
        } else {
          type = MusicWheelItemType_SectionCollapsed;
        }
        break;
      case WheelItemDataType_ParentSection:
        if (GAMESTATE->expanded_parent_section_name_ == wid->text_) {
          type = MusicWheelItemType_ParentExpanded;
        } else {
          type = MusicWheelItemType_ParentCollapsed;
        }
        break;
      case WheelItemDataType_Course:
        type = MusicWheelItemType_Course;
        break;
      case WheelItemDataType_Sort:
        if (wid->action_->pm_ != PlayMode_Invalid) {
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

    Message set_msg("Set");
    set_msg.SetParam("Song", wid->song_);
    set_msg.SetParam("Course", wid->course_);
    set_msg.SetParam("Text", wid->text_);
    set_msg.SetParam("Type", MusicWheelItemTypeToString(type));
    set_msg.SetParam("Color", wid->color_);
    set_msg.SetParam("Label", wid->label_);
    set_msg.SetParam("ParentSection", wid->parent_section_);
    set_msg.SetParam(
        "IsParentSection", wid->type_ == WheelItemDataType_ParentSection);
    this->HandleMessage(set_msg);

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
