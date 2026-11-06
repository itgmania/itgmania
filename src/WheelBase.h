#ifndef WHEELBASE_H
#define WHEELBASE_H

#include <climits>
#include <string>
#include <vector>

#include "ActorFrame.h"
#include "AutoActor.h"
#include "EnumHelper.h"
#include "LuaExpressionTransform.h"
#include "RageSound.h"
#include "RageTimer.h"
#include "RageTypes.h"
#include "ScrollBar.h"
#include "ThemeMetric.h"
#include "WheelItemBase.h"

#define NUM_WHEEL_ITEMS ((int)std::ceil(NUM_WHEEL_ITEMS_TO_DRAW + 2))

enum WheelState {
  STATE_SELECTING,
  STATE_FLYING_OFF_BEFORE_NEXT_SORT,
  STATE_FLYING_ON_AFTER_NEXT_SORT,
  STATE_ROULETTE_SPINNING,
  STATE_ROULETTE_SLOWING_DOWN,
  STATE_LOCKED,
  NUM_WheelState,
  WheelState_Invalid,
};
const std::string& WheelStateToString(WheelState ws);
WheelState StringToWheelState(const std::string& sDC);
LuaDeclareType(WheelState);

/** @brief A wheel with data elements. */
class WheelBase : public ActorFrame {
 public:
  virtual ~WheelBase();
  virtual void Load(std::string sType);
  void BeginScreen();

  virtual void Update(float fDeltaTime);

  virtual void Move(int n);
  void ChangeMusicUnlessLocked(int n); /* +1 or -1 */
  virtual void ChangeMusic(int dist);  /* +1 or -1 */
  virtual void SetOpenSection(std::string group) {}

  // Return true if we're moving fast automatically.
  int IsMoving() const;
  bool IsSettled() const;

  void SetItemPosition(Actor& item, int item_index, float offset_from_middle);

  virtual bool Select();  // return true if this selection can end the screen

  WheelState GetWheelState() { return wheel_state_; }
  bool WheelIsLocked() { return (wheel_state_ == STATE_LOCKED ? true : false); }
  void RebuildWheelItems(int dist = INT_MAX);  // INT_MAX = refresh all
  // Update the list of songs to match whatever songs are indexed by the song
  // manager (SONGMAN)
  virtual void ReloadSongList() {}

  virtual unsigned int GetNumItems() const {
    return cur_wheel_item_data_.size();
  }
  bool IsEmpty() { return empty_; }
  WheelItemBase* GetWheelItem(int i) {
    if (i < 0 || i >= (int)wheel_base_items_.size()) {
      return nullptr;
    }
    return wheel_base_items_[i];
  }
  std::string GetExpandedSectionName() { return expanded_section_name_; }
  int GetCurrentIndex() { return selection_; }

  WheelItemDataType GetSelectedType() {
    return cur_wheel_item_data_[selection_]->type_;
  }

  // Lua
  void PushSelf(lua_State* L);

 protected:
  void TweenOnScreenForSort();
  void TweenOffScreenForSort();

  virtual WheelItemBase* MakeItem() = 0;
  virtual void UpdateSwitch();
  virtual bool MoveSpecific(int n);
  void SetPositions();

  ScrollBar scroll_bar_;
  AutoActor spr_highlight_;

  std::vector<WheelItemBaseData*> cur_wheel_item_data_;
  std::vector<WheelItemBase*> wheel_base_items_;

  bool empty_;
  int selection_;  // index into m_CurWheelItemBaseData
  std::string expanded_section_name_;

  int switches_left_in_spin_down_;
  float locked_wheel_velocity_;
  // 0 = none; -1 or 1 = up/down
  int moving_;
  RageTimer moving_sound_timer_;
  float time_before_moving_begins_;
  float spin_speed_;

  WheelState wheel_state_;
  float time_left_in_state_;
  float position_offset_from_selection_;

  RageSound sound_change_music_;
  RageSound sound_expand_;
  RageSound sound_collapse_;
  RageSound sound_locked_;

  //	bool WheelItemIsVisible(int n);
  void UpdateScrollbar();

  ThemeMetric<float> SWITCH_SECONDS;
  ThemeMetric<float> LOCKED_INITIAL_VELOCITY;
  ThemeMetric<int> SCROLL_BAR_HEIGHT;
  LuaExpressionTransform expr_item_transform_function_;
  ThemeMetric<float> NUM_WHEEL_ITEMS_TO_DRAW;
  ThemeMetric<RageColor> WHEEL_ITEM_LOCKED_COLOR;
};

#endif

/*
 * (c) 2001-2004 Chris Danford, Chris Gomez, Glenn Maynard, Josh Allen
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
