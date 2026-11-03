#include "WheelBase.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <string>
#include <vector>

#include "ActorFrame.h"
#include "ActorUtil.h"
#include "EnumHelper.h"
#include "GameState.h"
#include "LuaManager.h"
#include "PrefsManager.h"
#include "RageLog.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "ScreenManager.h"  // for sending SM_PlayMusicSample
#include "ScreenMessage.h"
#include "ThemeManager.h"
#include "ThemeMetric.h"
#include "WheelItemBase.h"
#include "global.h"

const int MAX_WHEEL_SOUND_SPEED = 15;
AutoScreenMessage(
    SM_SongChanged);  // TODO: Replace this with a Message and MESSAGEMAN

static const char* WheelStateNames[] = {
    "Selecting",        "FlyingOffBeforeNextSort", "FlyingOnAfterNextSort",
    "RouletteSpinning", "RouletteSlowingDown",     "Locked"};
XToString(WheelState);
StringToX(WheelState);
LuaXType(WheelState);

WheelBase::~WheelBase() {
  for (WheelItemBase* i : wheel_base_items_) {
    RageUtil::SafeDelete(i);
  }
  wheel_base_items_.clear();
}

void WheelBase::Load(std::string type) {
  LOG->Trace("WheelBase::Load('%s')", type.c_str());
  ASSERT(this->GetNumChildren() == 0);  // only load once

  empty_ = false;
  selection_ = 0;
  time_left_in_state_ = 0;
  position_offset_from_selection_ = 0;
  switches_left_in_spin_down_ = 0;
  moving_ = 0;

  SWITCH_SECONDS.Load(type, "SwitchSeconds");
  LOCKED_INITIAL_VELOCITY.Load(type, "LockedInitialVelocity");
  SCROLL_BAR_HEIGHT.Load(type, "ScrollBarHeight");
  expr_item_transform_function_.SetFromReference(
      THEME->GetMetricR(type, "ItemTransformFunction"));
  NUM_WHEEL_ITEMS_TO_DRAW.Load(type, "NumWheelItems");
  WHEEL_ITEM_LOCKED_COLOR.Load(type, "WheelItemLockedColor");

  sound_change_music_.Load(THEME->GetPathS(type, "change"), true);
  sound_locked_.Load(THEME->GetPathS(type, "locked"), true);

  WheelItemBase* templ = MakeItem();
  ActorUtil::LoadAllCommands(*templ, name_);
  templ->PlayCommand("Init");
  for (int i = 0; i < NUM_WHEEL_ITEMS; i++) {
    WheelItemBase* item = templ->Copy();
    DEBUG_ASSERT(item);
    wheel_base_items_.push_back(item);
  }
  RageUtil::SafeDelete(templ);

  // draw outside->inside
  for (int i = 0; i < NUM_WHEEL_ITEMS / 2; i++) {
    this->AddChild(wheel_base_items_[i]);
  }
  for (int i = NUM_WHEEL_ITEMS - 1; i >= NUM_WHEEL_ITEMS / 2; i--) {
    this->AddChild(wheel_base_items_[i]);
  }

  spr_highlight_.Load(THEME->GetPathG(type, "highlight"));
  spr_highlight_->SetName("Highlight");
  this->AddChild(spr_highlight_);
  ActorUtil::LoadAllCommands(*spr_highlight_, name_);

  scroll_bar_.SetName("ScrollBar");
  scroll_bar_.SetBarHeight(SCROLL_BAR_HEIGHT);
  this->AddChild(&scroll_bar_);
  ActorUtil::LoadAllCommands(scroll_bar_, name_);

  SetPositions();
}

void WheelBase::BeginScreen() { wheel_state_ = STATE_SELECTING; }

void WheelBase::SetItemPosition(
    Actor& item, int item_index, float offset_from_middle) {
  Actor::TweenState ts = expr_item_transform_function_.GetTransformCached(
      offset_from_middle, item_index, NUM_WHEEL_ITEMS);
  item.DestTweenState() = ts;
}

void WheelBase::UpdateScrollbar() {
  int total_num_items = cur_wheel_item_data_.size();
  float item_at = selection_ - position_offset_from_selection_;

  {
    float size = float(NUM_WHEEL_ITEMS) / total_num_items;
    float center = item_at / total_num_items;
    size *= 0.5f;

    scroll_bar_.SetPercentage(center, size);
  }
}

bool WheelBase::IsSettled() const {
  if (moving_) {
    return false;
  }
  if (wheel_state_ != STATE_SELECTING && wheel_state_ != STATE_LOCKED) {
    return false;
  }
  if (position_offset_from_selection_ != 0) {
    return false;
  }

  return true;
}

void WheelBase::SetPositions() {
  for (int i = 0; i < NUM_WHEEL_ITEMS; i++) {
    WheelItemBase* display = wheel_base_items_[i];
    const float offset_from_selection =
        i - NUM_WHEEL_ITEMS / 2 + position_offset_from_selection_;
    if (std::abs(offset_from_selection) > NUM_WHEEL_ITEMS_TO_DRAW / 2) {
      display->SetVisible(false);
    } else {
      display->SetVisible(true);
    }

    SetItemPosition(*display, i, offset_from_selection);
  }
}

void WheelBase::Update(float delta_time) {
  ActorFrame::Update(delta_time);

  // If tweens aren't controlling the position of the wheel, set positions.
  if (!GetTweenTimeLeft()) {
    SetPositions();
  }

  for (int i = 0; i < NUM_WHEEL_ITEMS; i++) {
    WheelItemBase* display = wheel_base_items_[i];
    if (wheel_state_ == STATE_LOCKED && i != NUM_WHEEL_ITEMS / 2) {
      display->color_locked_ = WHEEL_ITEM_LOCKED_COLOR.GetValue();
    } else {
      display->color_locked_ = RageColor(0, 0, 0, 0);
    }
  }

  // Moved to CommonUpdateProcedure, seems to work fine. Revert if it happens
  // to break something.
  UpdateScrollbar();

  if (moving_) {
    time_before_moving_begins_ -= delta_time;
    time_before_moving_begins_ = std::max(time_before_moving_begins_, 0.0f);
  }

  // update wheel state
  time_left_in_state_ -= delta_time;
  if (time_left_in_state_ <= 0) {  // time to go to a new state
    UpdateSwitch();
  }

  if (wheel_state_ == STATE_LOCKED) {
    /* Do this in at most .1 sec chunks, so we don't get weird if we stop
     * for some reason (and so it behaves the same when being single stepped).
     */
    float time = delta_time;
    while (time > 0) {
      float t = std::min(time, 0.1f);
      time -= t;

      position_offset_from_selection_ =
          std::clamp(position_offset_from_selection_, -0.3f, +0.3f);

      float spring_force =
          -position_offset_from_selection_ * LOCKED_INITIAL_VELOCITY;
      locked_wheel_velocity_ += spring_force;

      float drag = -locked_wheel_velocity_ * t * 4;
      locked_wheel_velocity_ += drag;

      position_offset_from_selection_ += locked_wheel_velocity_ * t;

      if (std::abs(position_offset_from_selection_) < 0.01f &&
          std::abs(locked_wheel_velocity_) < 0.01f) {
        position_offset_from_selection_ = 0;
        locked_wheel_velocity_ = 0;
      }
    }
  }

  if (IsMoving()) {
    // We're automatically moving. Move linearly, and don't clamp to the
    // selection.
    float spin_speed = spin_speed_ * moving_;
    position_offset_from_selection_ -= spin_speed * delta_time;

    /* Make sure that we don't go further than 1 away, in case the speed is
     * very high or we miss a lot of frames. */
    position_offset_from_selection_ =
        std::clamp(position_offset_from_selection_, -1.0f, 1.0f);

    // If it passed the selection, move again.
    if ((moving_ == -1 && position_offset_from_selection_ >= 0) ||
        (moving_ == 1 && position_offset_from_selection_ <= 0)) {
      ChangeMusic(moving_);

      if (PREFSMAN->music_wheel_switch_speed_ < MAX_WHEEL_SOUND_SPEED) {
        sound_change_music_.Play(true);
      }
    }

    if (PREFSMAN->music_wheel_switch_speed_ >= MAX_WHEEL_SOUND_SPEED &&
        moving_sound_timer_.Ago() >= 1.0f / MAX_WHEEL_SOUND_SPEED) {
      moving_sound_timer_.GetDeltaTime();
      sound_change_music_.Play(true);
    }
  } else {
    // "rotate" wheel toward selected song
    float spin_speed =
        0.2f + std::abs(position_offset_from_selection_) / SWITCH_SECONDS;

    if (position_offset_from_selection_ > 0) {
      position_offset_from_selection_ -= spin_speed * delta_time;
      if (position_offset_from_selection_ < 0) {
        position_offset_from_selection_ = 0;
      }
    } else if (position_offset_from_selection_ < 0) {
      position_offset_from_selection_ += spin_speed * delta_time;
      if (position_offset_from_selection_ > 0) {
        position_offset_from_selection_ = 0;
      }
    }
  }
}

void WheelBase::UpdateSwitch() {
  switch (wheel_state_) {
    case STATE_SELECTING:
      time_left_in_state_ = 0;
      break;
    case STATE_LOCKED:
      break;
    default:
      FAIL_M(ssprintf("Invalid wheel state: %i", wheel_state_));
  }
}

bool WheelBase::Select()  // return true if this selection can end the screen
{
  LOG->Trace("WheelBase::Select()");

  moving_ = 0;

  if (empty_) {
    return false;
  }

  switch (cur_wheel_item_data_[selection_]->type_) {
    case WheelItemDataType_Generic:
      return true;
    case WheelItemDataType_Section:
    case WheelItemDataType_ParentSection: {
      std::string this_item_section_name =
          cur_wheel_item_data_[selection_]->text_;
      // Keep track of the open section so that we can restore it
      // when navigating back to ScreenSelectMusic.
      GAMESTATE->last_open_section_ = this_item_section_name;
      if (expanded_section_name_ == this_item_section_name)  // already expanded
      {
        SetOpenSection("");  // collapse it
        sound_collapse_.Play(true);
      } else  // already collapsed
      {
        SetOpenSection(this_item_section_name);  // expand it
        sound_expand_.Play(true);
      }
    }
      // Opening/closing sections cannot end the screen
      return false;
    default:
      return true;
  }
}

int WheelBase::IsMoving() const {
  return moving_ && time_before_moving_begins_ == 0;
}

void WheelBase::TweenOnScreenForSort() {
  position_offset_from_selection_ = 0;

  /* Before we send SortOn, position items back to their destinations, so
   * commands can use this as a reference point. */
  SetPositions();

  wheel_state_ = STATE_FLYING_ON_AFTER_NEXT_SORT;

  this->PlayCommand("SortOn");

  time_left_in_state_ = GetTweenTimeLeft();
}

void WheelBase::TweenOffScreenForSort() {
  wheel_state_ = STATE_FLYING_OFF_BEFORE_NEXT_SORT;

  this->PlayCommand("SortOff");

  time_left_in_state_ = GetTweenTimeLeft();
}

void WheelBase::ChangeMusicUnlessLocked(int n) {
  if (wheel_state_ == STATE_LOCKED) {
    if (n) {
      int sign = n / std::abs(n);
      locked_wheel_velocity_ = sign * LOCKED_INITIAL_VELOCITY;
      sound_locked_.Play(true);
    }
    return;
  }

  ChangeMusic(n);
}

void WheelBase::Move(int n) {
  if (n == moving_) {
    return;
  }

  if (wheel_state_ == STATE_LOCKED) {
    if (n) {
      int sign = n / std::abs(n);
      locked_wheel_velocity_ = sign * LOCKED_INITIAL_VELOCITY;
      sound_locked_.Play(true);
    }
    return;
  }

  if (!MoveSpecific(n)) {
    return;
  }

  time_before_moving_begins_ = 1 / 4.0f;
  spin_speed_ = float(PREFSMAN->music_wheel_switch_speed_);
  moving_ = n;

  if (moving_) {
    ChangeMusic(moving_);
  }
}

bool WheelBase::MoveSpecific(int n) {
  /* If we're not selecting, discard this.  We won't ignore it; we'll
   * get called again every time the key is repeated. */
  /* Still process Move(0) so we sometimes continue moving immediate
   * after the sort change finished and before the repeat event causes a
   * Move(0). -Chris */
  switch (wheel_state_) {
    case STATE_SELECTING:
      break;
    case STATE_FLYING_OFF_BEFORE_NEXT_SORT:
    case STATE_FLYING_ON_AFTER_NEXT_SORT:
      if (n != 0) {
        return false;
      }
      break;
    default:
      return false;  // don't continue
  }

  if (moving_ != 0 && n == 0 && time_before_moving_begins_ == 0) {
    /* We were moving, and now we're stopping.  If we're really close to
     * the selection, move to the next one, so we have a chance to spin down
     * smoothly. */
    if (std::abs(position_offset_from_selection_) < 0.25f) {
      ChangeMusic(moving_);
    }

    /* Make sure the user always gets an SM_SongChanged when
     * Moving() is 0, so the final banner, etc. always gets set. */
    SCREENMAN->PostMessageToTopScreen(SM_SongChanged, 0);
  }

  return true;
}

void WheelBase::ChangeMusic(int dist) {
  selection_ += dist;
  wrap(selection_, cur_wheel_item_data_.size());

  RebuildWheelItems(dist);

  position_offset_from_selection_ += dist;

  //	SCREENMAN->PostMessageToTopScreen( SM_SongChanged, 0 );

  /* If we're moving automatically, don't play this; it'll be called in Update.
   */
  if (!IsMoving()) {
    sound_change_music_.Play(true);
  }
}

void WheelBase::RebuildWheelItems(int dist) {
  const std::vector<WheelItemBaseData*>& data = cur_wheel_item_data_;
  std::vector<WheelItemBase*>& items = wheel_base_items_;

  // rewind to first index that will be displayed;
  int first_visible_index = selection_;
  if (selection_ > int(data.size() - 1)) {
    selection_ = 0;
  }

  // find the first wheel item shown
  first_visible_index -= NUM_WHEEL_ITEMS / 2;

  ASSERT(data.size() != 0);
  wrap(first_visible_index, data.size());

  // iIndex is now the index of the lowest WheelItem to draw

  if (dist != INT_MAX) {
    // Shift items and refresh only those that have changed.
    CircularShift(items, dist);
  }

  for (int i = 0; i < (int)items.size(); i++) {
    int index = first_visible_index + i;
    wrap(index, data.size());
    const WheelItemBaseData* p_data = data[index];
    WheelItemBase* display = items[i];
    display->LoadFromWheelItemData(p_data, index, i);
  }
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the WheelBase. */
class LunaWheelBase : public Luna<WheelBase> {
 public:
  static int Move(T* p, lua_State* L) {
    p->Move(IArg(1));
    COMMON_RETURN_SELF;
  }
  static int GetWheelItem(T* p, lua_State* L) {
    int i_item = IArg(1);

    WheelItemBase* p_item = p->GetWheelItem(i_item);
    if (p_item == nullptr) {
      luaL_error(L, "%i out of bounds", i_item);
    }
    p_item->PushSelf(L);

    return 1;
  }
  static int IsSettled(T* p, lua_State* L) {
    lua_pushboolean(L, p->IsSettled());
    return 1;
  }
  static int SetOpenSection(T* p, lua_State* L) {
    p->SetOpenSection(SArg(1));
    COMMON_RETURN_SELF;
  }
  static int GetCurrentIndex(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetCurrentIndex());
    return 1;
  }
  static int GetNumItems(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetNumItems());
    return 1;
  }
  // evil shit
  // static int ChangeMusic( T* p, lua_State *L ){ p->ChangeMusicUnlessLocked(
  // IArg(1) ); return 0; }

  DEFINE_METHOD(GetSelectedType, GetSelectedType())
  DEFINE_METHOD(GetWheelState, GetWheelState())

  // deprecated; use GetWheelState instead:
  static int IsLocked(T* p, lua_State* L) {
    lua_pushboolean(L, p->WheelIsLocked());
    return 1;
  }

  LunaWheelBase() {
    ADD_METHOD(Move);
    ADD_METHOD(GetWheelItem);
    ADD_METHOD(IsSettled);
    ADD_METHOD(IsLocked);
    ADD_METHOD(SetOpenSection);
    ADD_METHOD(GetCurrentIndex);
    ADD_METHOD(GetNumItems);
    ADD_METHOD(GetSelectedType);
    // evil shit
    // ADD_METHOD( Move );
    // ADD_METHOD( ChangeMusic );
  }
};

LUA_REGISTER_DERIVED_CLASS(WheelBase, ActorFrame)
// lua end

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
