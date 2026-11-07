/* MusicWheel - A wheel with song names used in the Select Music screen. */

#ifndef MUSIC_WHEEL_H
#define MUSIC_WHEEL_H

#include <string>
#include <vector>

#include "GameConstantsAndTypes.h"
#include "LuaReference.h"
#include "MusicWheelItem.h"
#include "RageSound.h"
#include "RageTypes.h"
#include "ThemeMetric.h"
#include "WheelBase.h"
#include "WheelItemBase.h"

class Course;
class Song;

struct CompareSongPointerArrayBySectionName;

class MusicWheel : public WheelBase {
  friend struct CompareSongPointerArrayBySectionName;

 public:
  virtual ~MusicWheel();
  virtual void Load(std::string type);
  void BeginScreen();

  bool ChangeSort(
      SortOrder new_so,
      bool allow_same_sort = false);  // return true if change successful
  bool NextSort();                    // return true if change successful
  void StartRoulette();
  void StartRandom();
  bool IsRouletting() const;

  virtual bool Select();  // return true if this selection ends the screen
  WheelItemDataType GetSelectedType() {
    return GetCurWheelItemData(selection_)->type_;
  }
  Song* GetSelectedSong();
  Course* GetSelectedCourse() {
    return GetCurWheelItemData(selection_)->course_;
  }
  std::string GetSelectedSection() {
    return GetCurWheelItemData(selection_)->text_;
  }

  Song* GetPreferredSelectionForRandomOrPortal();

  bool SelectSong(const Song* p);
  bool SelectCourse(const Course* p);
  bool HasSongs();
  bool SelectSection(const std::string& section_name);
  bool CloseOpenSectionOneLevel();
  void SetOpenSection(std::string group);
  SortOrder GetSortOrder() const { return sort_order_; }
  virtual void ChangeMusic(int dist); /* +1 or -1 */  // CHECK THIS
  void FinishChangingSorts();
  void PlayerJoined();
  // sm-ssc additions
  std::string JumpToNextGroup();
  std::string JumpToPrevGroup();
  const MusicWheelItemData* GetCurWheelItemData(int i) {
    return (const MusicWheelItemData*)cur_wheel_item_data_[i];
  }

  virtual void ReloadSongList();

  void GetCurrentSections(std::vector<std::string>& sections);
  // Lua
  void PushSelf(lua_State* L);

 protected:
  MusicWheelItem* MakeItem();

  void GetSongList(std::vector<Song*>& array_songs, SortOrder so);
  bool SelectSongOrCourse();
  bool SelectModeMenuItem();

  virtual void UpdateSwitch();

  std::vector<MusicWheelItemData*>& getWheelItemsData(SortOrder so);
  void readyWheelItemsData(SortOrder so);

  std::string last_mode_menu_item_;
  SortOrder sort_order_;
  RageSound sound_change_sort_;

  ThemeMetric<float> ROULETTE_SWITCH_SECONDS;
  ThemeMetric<int> ROULETTE_SLOW_DOWN_SWITCHES;
  ThemeMetric<int> NUM_SECTION_COLORS;
  ThemeMetric<RageColor> SONG_REAL_EXTRA_COLOR;
  ThemeMetric<RageColor> SORT_MENU_COLOR;
  ThemeMetric<bool> SHOW_ROULETTE;
  ThemeMetric<bool> SHOW_RANDOM;
  ThemeMetric<bool> SHOW_PORTAL;
  ThemeMetric<bool> RANDOM_PICKS_LOCKED_SONGS;
  ThemeMetric<int> MOST_PLAYED_SONGS_TO_SHOW;
  ThemeMetric<int> RECENT_SONGS_TO_SHOW;
  ThemeMetric<std::string> MODE_MENU_CHOICE_NAMES;
  ThemeMetricMap<std::string> CHOICE;
  ThemeMetric1D<RageColor> SECTION_COLORS;
  ThemeMetric<LuaReference> SORT_ORDERS;
  ThemeMetric<bool> SHOW_EASY_FLAG;
  // sm-ssc additions:
  ThemeMetric<bool> USE_SECTIONS_WITH_PREFERRED_GROUP;
  ThemeMetric<bool> HIDE_INACTIVE_SECTIONS;
  ThemeMetric<bool> HIDE_INACTIVE_PARENT_SECTIONS;
  ThemeMetric<bool> HIDE_ACTIVE_SECTION_TITLE;
  ThemeMetric<bool> REMIND_WHEEL_POSITIONS;
  ThemeMetric<RageColor> ROULETTE_COLOR;
  ThemeMetric<RageColor> RANDOM_COLOR;
  ThemeMetric<RageColor> PORTAL_COLOR;
  ThemeMetric<RageColor> EMPTY_COLOR;
  std::vector<int> wheel_positions_;
  ThemeMetric<std::string> CUSTOM_WHEEL_ITEM_NAMES;
  ThemeMetricMap<std::string> CUSTOM_CHOICES;
  ThemeMetricMap<RageColor> CUSTOM_CHOICE_COLORS;
  std::string expanded_parent_section_name_;

 private:
  // use getWheelItemsData instead of touching this one
  enum { INVALID, NEEDREFILTER, VALID } wheel_item_datas_status_[NUM_SortOrder];
  std::vector<MusicWheelItemData*> wheel_item_datas_[NUM_SortOrder];
  std::vector<MusicWheelItemData*> un_filtered_wheel_item_datas_[NUM_SortOrder];

  void BuildWheelItemDatas(
      std::vector<MusicWheelItemData*>& array_wheel_items, SortOrder so);
  void FilterWheelItemDatas(
      std::vector<MusicWheelItemData*>& un_filtered_datas,
      std::vector<MusicWheelItemData*>& filtered_data, SortOrder so);
  void SetOpenSections(
      const std::string& parent_section, const std::string& section);
};

#endif

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
