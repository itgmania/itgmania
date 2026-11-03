#ifndef MUSIC_WHEEL_ITEM_H
#define MUSIC_WHEEL_ITEM_H

#include <string>

#include "AutoActor.h"
#include "BitmapText.h"
#include "GameCommand.h"
#include "Group.h"
#include "MessageManager.h"
#include "PlayerNumber.h"
#include "RageTypes.h"
#include "RageUtil_AutoPtr.h"
#include "Song.h"
#include "TextBanner.h"
#include "ThemeMetric.h"
#include "WheelItemBase.h"
#include "WheelNotifyIcon.h"

class Course;
class Song;

struct MusicWheelItemData;

enum MusicWheelItemType {
  MusicWheelItemType_Song,
  MusicWheelItemType_ParentExpanded,
  MusicWheelItemType_ParentCollapsed,
  MusicWheelItemType_SectionExpanded,
  MusicWheelItemType_SectionCollapsed,
  MusicWheelItemType_Roulette,
  MusicWheelItemType_Course,
  MusicWheelItemType_Sort,
  MusicWheelItemType_Mode,
  MusicWheelItemType_Random,
  MusicWheelItemType_Portal,
  MusicWheelItemType_Custom,
  NUM_MusicWheelItemType,
  MusicWheelItemType_Invalid,
};
const std::string& MusicWheelItemTypeToString(MusicWheelItemType i);
/** @brief An item on the MusicWheel. */
class MusicWheelItem : public WheelItemBase {
 public:
  MusicWheelItem(std::string type = "MusicWheelItem");
  MusicWheelItem(const MusicWheelItem& cpy);
  virtual ~MusicWheelItem();
  virtual MusicWheelItem* Copy() const { return new MusicWheelItem(*this); }

  virtual void LoadFromWheelItemData(
      const WheelItemBaseData* wid, int index, int draw_index);
  virtual void HandleMessage(const Message& msg);
  void RefreshGrades();

 private:
  ThemeMetric<bool> GRADES_SHOW_MACHINE;

  AutoActor spr_color_part_[NUM_MusicWheelItemType];
  AutoActor spr_normal_part_[NUM_MusicWheelItemType];
  AutoActor spr_over_part_[NUM_MusicWheelItemType];

  TextBanner text_banner_;  // used by Type_Song instead of m_pText
  BitmapText* text_[NUM_MusicWheelItemType];
  BitmapText* text_section_count_;

  WheelNotifyIcon wheel_notify_icon_;
  AutoActor grade_display_[NUM_PLAYERS];
};

struct MusicWheelItemData : public WheelItemBaseData {
  MusicWheelItemData()
      : course_(nullptr),
        song_(nullptr),
        group_(nullptr),
        flags_(),
        section_count_(0),
        parent_section_(""),
        label_(""),
        action_() {}
  MusicWheelItemData(
      WheelItemDataType type, Song* song, std::string section_name,
      Course* course, Group* group, RageColor color, int section_count,
      std::string parent_section = "");

  Course* course_;
  Song* song_;
  Group* group_;
  WheelNotifyIcon::Flags flags_;

  // for TYPE_SECTION
  int section_count_;
  std::string parent_section_;

  // for TYPE_SORT
  std::string label_;
  HiddenPtr<GameCommand> action_;
};

#endif

/**
 * @file
 * @author Chris Danford, Chris Gomez, Glenn Maynard (c) 2001-2004
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
