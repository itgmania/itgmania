#include "ActorScroller.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <string>
#include <vector>

#include "Actor.h"
#include "ActorFrame.h"
#include "ActorUtil.h"
#include "LuaBinding.h"
#include "LuaManager.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "XmlFile.h"

/* Tricky: We need ActorFrames created in Lua to auto delete their children.
 * We don't want classes that derive from ActorFrame to auto delete their
 * children. The name "ActorFrame" is widely used in Lua, so we'll have
 * that string instead create an ActorFrameAutoDeleteChildren object. */
// REGISTER_ACTOR_CLASS( ActorScroller );
REGISTER_ACTOR_CLASS_WITH_NAME(ActorScrollerAutoDeleteChildren, ActorScroller);
ActorScroller* ActorScroller::Copy() const { return new ActorScroller(*this); }

ActorScroller::ActorScroller() {
  num_items_ = 0;
  current_item_ = 0;
  destination_item_ = 0;
  seconds_per_item_ = 1;
  seconds_pause_between_items_ = 0;
  num_items_to_draw_ = 7;
  first_sub_actor_index_ = 0;
  loop_ = false;
  wrap_ = false;
  fast_catchup_ = false;
  function_depends_on_position_offset_ = true;
  function_depends_on_item_index_ = true;
  pause_countdown_seconds_ = 0;
  quantize_pixels_ = 0;

  quad_mask_.SetBlendMode(BLEND_NO_EFFECT);  // don't change color values
  quad_mask_.SetUseZBuffer(true);            // we want to write to the Zbuffer
  mask_width_ = 0;
  mask_height_ = 0;
  DisableMask();
}

void ActorScroller::Load2() {
  num_items_ = sub_actors_.size();

  Lua* L = LUA->Get();
  for (unsigned i = 0; i < sub_actors_.size(); ++i) {
    lua_pushnumber(L, i);
    this->sub_actors_[i]->lua_instance_->Set(L, "ItemIndex");
  }
  LUA->Release(L);
}

void ActorScroller::SetTransformFromReference(const LuaReference& ref) {
  expr_transform_function_.SetFromReference(ref);

  // Probe to find which of the parameters are used.
#define GP(a, b) expr_transform_function_.GetTransformCached(a, b, 2)
  function_depends_on_position_offset_ =
      (GP(0, 0) != GP(1, 0)) && (GP(0, 1) != GP(1, 1));
  function_depends_on_item_index_ =
      (GP(0, 0) != GP(0, 1)) && (GP(1, 0) != GP(1, 1));
  expr_transform_function_.ClearCache();
}

void ActorScroller::SetTransformFromExpression(
    const std::string& transform_function) {
  LuaReference ref;
  ref.SetFromExpression(transform_function);
  SetTransformFromReference(ref);
}

void ActorScroller::SetTransformFromWidth(float item_width) {
  SetTransformFromExpression(ssprintf(
      "function(self,offset,itemIndex,numItems) self:x(%f*offset) end",
      item_width));
}

void ActorScroller::SetTransformFromHeight(float item_height) {
  SetTransformFromExpression(ssprintf(
      "function(self,offset,itemIndex,numItems) self:y(%f*offset) end",
      item_height));
}

void ActorScroller::EnableMask(float width, float height) {
  quad_mask_.SetVisible(width != 0 && height != 0);
  quad_mask_.SetWidth(width);
  mask_width_ = width;
  quad_mask_.SetHeight(height);
  mask_height_ = height;
}

void ActorScroller::DisableMask() { quad_mask_.SetVisible(false); }

void ActorScroller::ScrollThroughAllItems() {
  current_item_ = (loop_ || wrap_) ? +num_items_to_draw_ / 2.0f
                                   : -(num_items_to_draw_ / 2.0f) - 1;
  destination_item_ = (float)(num_items_ + num_items_to_draw_ / 2.0f + 1);
}

void ActorScroller::ScrollWithPadding(
    float item_padding_start, float item_padding_end) {
  current_item_ = -item_padding_start;
  destination_item_ = num_items_ - 1 + item_padding_end;
}

float ActorScroller::GetSecondsForCompleteScrollThrough() const {
  float total_items = num_items_to_draw_ + num_items_;
  return total_items * (seconds_per_item_ + seconds_pause_between_items_);
}

float ActorScroller::GetSecondsToDestination() const {
  float total_items_to_move = std::abs(current_item_ - destination_item_);
  return total_items_to_move * seconds_per_item_;
}

void ActorScroller::LoadFromNode(const XNode* node) {
  ActorFrame::LoadFromNode(node);

  Load2();

  float num_items_to_draw = 0;
  if (node->GetAttrValue("NumItemsToDraw", num_items_to_draw)) {
    SetNumItemsToDraw(num_items_to_draw);
  }

  float seconds_per_item = 0;
  if (node->GetAttrValue("SecondsPerItem", seconds_per_item)) {
    ActorScroller::SetSecondsPerItem(seconds_per_item);
  }

  Lua* L = LUA->Get();
  node->PushAttrValue(L, "TransformFunction");
  {
    LuaReference ref;
    ref.SetFromStack(L);
    if (!ref.IsNil()) {
      SetTransformFromReference(ref);
    }
  }
  LUA->Release(L);

  int subdivisions = 1;
  if (node->GetAttrValue("Subdivisions", subdivisions)) {
    ActorScroller::SetNumSubdivisions(subdivisions);
  }

  bool use_mask = false;
  node->GetAttrValue("UseMask", use_mask);

  if (use_mask) {
    node->GetAttrValue("MaskWidth", mask_width_);
    node->GetAttrValue("MaskHeight", mask_height_);
    EnableMask(mask_width_, mask_height_);
  }

  node->GetAttrValue("QuantizePixels", quantize_pixels_);
  node->GetAttrValue("LoopScroller", loop_);
  node->GetAttrValue("WrapScroller", wrap_);
}

void ActorScroller::UpdateInternal(float delta_time) {
  ActorFrame::UpdateInternal(delta_time);

  // If we have no children, the code below will busy loop.
  if (!sub_actors_.size()) {
    return;
  }

  // handle pause
  if (delta_time > pause_countdown_seconds_) {
    delta_time -= pause_countdown_seconds_;
    pause_countdown_seconds_ = 0;
  } else {
    pause_countdown_seconds_ -= delta_time;
    delta_time = 0;
    return;
  }

  if (current_item_ == destination_item_) {
    return;  // done scrolling
  }

  float old_item_at_top = current_item_;
  if (seconds_per_item_ > 0) {
    float approach_speed = delta_time / seconds_per_item_;
    if (fast_catchup_) {
      float distance_to_move = std::abs(current_item_ - destination_item_);
      if (distance_to_move > 1) {
        approach_speed *= distance_to_move * distance_to_move;
      }
    }

    fapproach(current_item_, destination_item_, approach_speed);
  }

  // if items changed, then pause
  if ((int)old_item_at_top != (int)current_item_) {
    pause_countdown_seconds_ = seconds_pause_between_items_;
  }

  if (wrap_) {
    float delta = destination_item_ - current_item_;
    current_item_ = std::fmod(current_item_, (float)num_items_);
    destination_item_ = current_item_ + delta;
  }

  if (loop_) {
    current_item_ = std::fmod(current_item_, (float)num_items_);
  }
}

void ActorScroller::DrawPrimitives() { PositionItemsAndDrawPrimitives(true); }

void ActorScroller::PositionItems() { PositionItemsAndDrawPrimitives(false); }

/* Shift m_SubActors forward by iDist. This will place item
 * m_iFirstSubActorIndex in m_SubActors[0]. */
void ActorScroller::ShiftSubActors(int dist) {
  if (dist != INT_MAX) {
    CircularShift(sub_actors_, dist);
  }
}

void ActorScroller::PositionItemsAndDrawPrimitives(bool draw_primitives) {
  if (sub_actors_.empty()) {
    return;
  }

  float num_items_to_draw = num_items_to_draw_;
  if (quad_mask_.GetVisible()) {
    // write to z buffer so that top and bottom are clipped
    // Draw an extra item; this is the one that will be masked.
    num_items_to_draw++;
    float position_fully_off_screen_top = -(num_items_to_draw) / 2.f;
    float position_fully_off_screen_bottom = (num_items_to_draw) / 2.f;

    expr_transform_function_.TransformItemCached(
        quad_mask_, position_fully_off_screen_top, -1, num_items_);
    if (draw_primitives) {
      quad_mask_.Draw();
    }

    expr_transform_function_.TransformItemCached(
        quad_mask_, position_fully_off_screen_bottom, num_items_, num_items_);
    if (draw_primitives) {
      quad_mask_.Draw();
    }
  }

  float f_first_item_to_draw = current_item_ - num_items_to_draw / 2.f;
  float f_last_item_to_draw = current_item_ + num_items_to_draw / 2.f;
  int i_first_item_to_draw = std::ceil(f_first_item_to_draw);
  int i_last_item_to_draw = std::ceil(f_last_item_to_draw);
  if (!loop_ && !wrap_) {
    i_first_item_to_draw = std::clamp(i_first_item_to_draw, 0, num_items_);
    i_last_item_to_draw = std::clamp(i_last_item_to_draw, 0, num_items_);
  }

  std::vector<Actor*> subs;

  {
    // Shift m_SubActors so iFirstItemToDraw is at the beginning.
    int new_first_index = i_first_item_to_draw;
    int dist = new_first_index - first_sub_actor_index_;
    first_sub_actor_index_ = new_first_index;
    ShiftSubActors(dist);
  }

  int num_to_draw = i_last_item_to_draw - i_first_item_to_draw;
  for (int i = 0; i < num_to_draw; ++i) {
    int item = i + i_first_item_to_draw;
    float position = item - current_item_;
    int index = i;  // index into m_SubActors
    if (loop_ || wrap_) {
      wrap(index, sub_actors_.size());
    } else if (index < 0 || index >= (int)sub_actors_.size()) {
      continue;
    }

    // Optimization: Zero out unused parameters so that they don't create new,
    // unnecessary  entries in the position cache. On scrollers with lots of
    // items, especially with Subdivisions > 1, m_exprTransformFunction uses
    // too much memory.
    if (!function_depends_on_position_offset_) {
      position = 0;
    }
    if (!function_depends_on_item_index_) {
      item = 0;
    }

    expr_transform_function_.TransformItemCached(
        *sub_actors_[index], position, item, num_items_);
    if (draw_primitives) {
      if (draw_by_z_position_) {
        subs.push_back(sub_actors_[index]);
      } else {
        sub_actors_[index]->Draw();
      }
    }
  }

  if (draw_by_z_position_) {
    ActorUtil::SortByZPosition(subs);
    for (Actor* a : subs) {
      a->Draw();
    }
  }
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the ActorScroller. */
class LunaActorScroller : public Luna<ActorScroller> {
 public:
  static int PositionItems(T* p, lua_State* L) {
    p->PositionItems();
    COMMON_RETURN_SELF;
  }
  static int SetTransformFromFunction(T* p, lua_State* L) {
    LuaReference ref;
    LuaHelpers::FromStack(L, ref, 1);
    p->SetTransformFromReference(ref);
    COMMON_RETURN_SELF;
  }
  static int SetTransformFromHeight(T* p, lua_State* L) {
    p->SetTransformFromHeight(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int SetTransformFromWidth(T* p, lua_State* L) {
    p->SetTransformFromWidth(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int SetCurrentAndDestinationItem(T* p, lua_State* L) {
    p->SetCurrentAndDestinationItem(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int SetDestinationItem(T* p, lua_State* L) {
    p->SetDestinationItem(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int GetSecondsToDestination(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetSecondsToDestination());
    return 1;
  }
  static int SetSecondsPerItem(T* p, lua_State* L) {
    p->SetSecondsPerItem(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int GetSecondsPauseBetweenItems(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetSecondsPauseBetweenItems());
    return 1;
  }
  static int SetSecondsPauseBetweenItems(T* p, lua_State* L) {
    p->SetSecondsPauseBetweenItems(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int SetPauseCountdownSeconds(T* p, lua_State* L) {
    p->SetPauseCountdownSeconds(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int SetNumSubdivisions(T* p, lua_State* L) {
    p->SetNumSubdivisions(IArg(1));
    COMMON_RETURN_SELF;
  }
  static int ScrollThroughAllItems(T* p, lua_State* L) {
    p->ScrollThroughAllItems();
    COMMON_RETURN_SELF;
  }
  static int ScrollWithPadding(T* p, lua_State* L) {
    p->ScrollWithPadding(FArg(1), FArg(2));
    COMMON_RETURN_SELF;
  }
  static int SetFastCatchup(T* p, lua_State* L) {
    p->SetFastCatchup(BArg(1));
    COMMON_RETURN_SELF;
  }
  static int SetLoop(T* p, lua_State* L) {
    p->SetLoop(BArg(1));
    COMMON_RETURN_SELF;
  }
  static int SetWrap(T* p, lua_State* L) {
    p->SetWrap(BArg(1));
    COMMON_RETURN_SELF;
  }
  static int SetMask(T* p, lua_State* L) {
    p->EnableMask(FArg(1), FArg(2));
    COMMON_RETURN_SELF;
  }

  static int SetNumItemsToDraw(T* p, lua_State* L) {
    p->SetNumItemsToDraw(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int GetFullScrollLengthSeconds(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetSecondsForCompleteScrollThrough());
    return 1;
  }
  static int GetCurrentItem(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetCurrentItem());
    return 1;
  }
  static int GetDestinationItem(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetDestinationItem());
    return 1;
  }
  static int GetNumItems(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetNumItems());
    return 1;
  }

  LunaActorScroller() {
    ADD_METHOD(PositionItems);
    ADD_METHOD(SetTransformFromFunction);
    ADD_METHOD(SetTransformFromHeight);
    ADD_METHOD(SetTransformFromWidth);
    ADD_METHOD(SetCurrentAndDestinationItem);
    ADD_METHOD(SetDestinationItem);
    ADD_METHOD(GetSecondsToDestination);
    ADD_METHOD(SetSecondsPerItem);
    ADD_METHOD(SetSecondsPauseBetweenItems);
    ADD_METHOD(GetSecondsPauseBetweenItems);
    ADD_METHOD(SetPauseCountdownSeconds);
    ADD_METHOD(SetNumSubdivisions);
    ADD_METHOD(ScrollThroughAllItems);
    ADD_METHOD(ScrollWithPadding);
    ADD_METHOD(SetFastCatchup);
    ADD_METHOD(SetLoop);
    ADD_METHOD(SetWrap);
    ADD_METHOD(SetMask);
    ADD_METHOD(SetNumItemsToDraw);
    ADD_METHOD(GetFullScrollLengthSeconds);
    ADD_METHOD(GetCurrentItem);
    ADD_METHOD(GetDestinationItem);
    ADD_METHOD(GetNumItems);
  }
};

LUA_REGISTER_DERIVED_CLASS(ActorScroller, ActorFrame)
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
