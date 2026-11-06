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
    const std::string& sTransformFunction) {
  LuaReference ref;
  ref.SetFromExpression(sTransformFunction);
  SetTransformFromReference(ref);
}

void ActorScroller::SetTransformFromWidth(float fItemWidth) {
  SetTransformFromExpression(ssprintf(
      "function(self,offset,itemIndex,numItems) self:x(%f*offset) end",
      fItemWidth));
}

void ActorScroller::SetTransformFromHeight(float fItemHeight) {
  SetTransformFromExpression(ssprintf(
      "function(self,offset,itemIndex,numItems) self:y(%f*offset) end",
      fItemHeight));
}

void ActorScroller::EnableMask(float fWidth, float fHeight) {
  quad_mask_.SetVisible(fWidth != 0 && fHeight != 0);
  quad_mask_.SetWidth(fWidth);
  mask_width_ = fWidth;
  quad_mask_.SetHeight(fHeight);
  mask_height_ = fHeight;
}

void ActorScroller::DisableMask() { quad_mask_.SetVisible(false); }

void ActorScroller::ScrollThroughAllItems() {
  current_item_ = (loop_ || wrap_) ? +num_items_to_draw_ / 2.0f
                                   : -(num_items_to_draw_ / 2.0f) - 1;
  destination_item_ = (float)(num_items_ + num_items_to_draw_ / 2.0f + 1);
}

void ActorScroller::ScrollWithPadding(
    float fItemPaddingStart, float fItemPaddingEnd) {
  current_item_ = -fItemPaddingStart;
  destination_item_ = num_items_ - 1 + fItemPaddingEnd;
}

float ActorScroller::GetSecondsForCompleteScrollThrough() const {
  float fTotalItems = num_items_to_draw_ + num_items_;
  return fTotalItems * (seconds_per_item_ + seconds_pause_between_items_);
}

float ActorScroller::GetSecondsToDestination() const {
  float fTotalItemsToMove = std::abs(current_item_ - destination_item_);
  return fTotalItemsToMove * seconds_per_item_;
}

void ActorScroller::LoadFromNode(const XNode* pNode) {
  ActorFrame::LoadFromNode(pNode);

  Load2();

  float fNumItemsToDraw = 0;
  if (pNode->GetAttrValue("NumItemsToDraw", fNumItemsToDraw)) {
    SetNumItemsToDraw(fNumItemsToDraw);
  }

  float fSecondsPerItem = 0;
  if (pNode->GetAttrValue("SecondsPerItem", fSecondsPerItem)) {
    ActorScroller::SetSecondsPerItem(fSecondsPerItem);
  }

  Lua* L = LUA->Get();
  pNode->PushAttrValue(L, "TransformFunction");
  {
    LuaReference ref;
    ref.SetFromStack(L);
    if (!ref.IsNil()) {
      SetTransformFromReference(ref);
    }
  }
  LUA->Release(L);

  int iSubdivisions = 1;
  if (pNode->GetAttrValue("Subdivisions", iSubdivisions)) {
    ActorScroller::SetNumSubdivisions(iSubdivisions);
  }

  bool bUseMask = false;
  pNode->GetAttrValue("UseMask", bUseMask);

  if (bUseMask) {
    pNode->GetAttrValue("MaskWidth", mask_width_);
    pNode->GetAttrValue("MaskHeight", mask_height_);
    EnableMask(mask_width_, mask_height_);
  }

  pNode->GetAttrValue("QuantizePixels", quantize_pixels_);
  pNode->GetAttrValue("LoopScroller", loop_);
  pNode->GetAttrValue("WrapScroller", wrap_);
}

void ActorScroller::UpdateInternal(float fDeltaTime) {
  ActorFrame::UpdateInternal(fDeltaTime);

  // If we have no children, the code below will busy loop.
  if (!sub_actors_.size()) {
    return;
  }

  // handle pause
  if (fDeltaTime > pause_countdown_seconds_) {
    fDeltaTime -= pause_countdown_seconds_;
    pause_countdown_seconds_ = 0;
  } else {
    pause_countdown_seconds_ -= fDeltaTime;
    fDeltaTime = 0;
    return;
  }

  if (current_item_ == destination_item_) {
    return;  // done scrolling
  }

  float fOldItemAtTop = current_item_;
  if (seconds_per_item_ > 0) {
    float fApproachSpeed = fDeltaTime / seconds_per_item_;
    if (fast_catchup_) {
      float fDistanceToMove = std::abs(current_item_ - destination_item_);
      if (fDistanceToMove > 1) {
        fApproachSpeed *= fDistanceToMove * fDistanceToMove;
      }
    }

    fapproach(current_item_, destination_item_, fApproachSpeed);
  }

  // if items changed, then pause
  if ((int)fOldItemAtTop != (int)current_item_) {
    pause_countdown_seconds_ = seconds_pause_between_items_;
  }

  if (wrap_) {
    float Delta = destination_item_ - current_item_;
    current_item_ = std::fmod(current_item_, (float)num_items_);
    destination_item_ = current_item_ + Delta;
  }

  if (loop_) {
    current_item_ = std::fmod(current_item_, (float)num_items_);
  }
}

void ActorScroller::DrawPrimitives() { PositionItemsAndDrawPrimitives(true); }

void ActorScroller::PositionItems() { PositionItemsAndDrawPrimitives(false); }

/* Shift m_SubActors forward by iDist. This will place item
 * m_iFirstSubActorIndex in m_SubActors[0]. */
void ActorScroller::ShiftSubActors(int iDist) {
  if (iDist != INT_MAX) {
    CircularShift(sub_actors_, iDist);
  }
}

void ActorScroller::PositionItemsAndDrawPrimitives(bool bDrawPrimitives) {
  if (sub_actors_.empty()) {
    return;
  }

  float fNumItemsToDraw = num_items_to_draw_;
  if (quad_mask_.GetVisible()) {
    // write to z buffer so that top and bottom are clipped
    // Draw an extra item; this is the one that will be masked.
    fNumItemsToDraw++;
    float fPositionFullyOffScreenTop = -(fNumItemsToDraw) / 2.f;
    float fPositionFullyOffScreenBottom = (fNumItemsToDraw) / 2.f;

    expr_transform_function_.TransformItemCached(
        quad_mask_, fPositionFullyOffScreenTop, -1, num_items_);
    if (bDrawPrimitives) {
      quad_mask_.Draw();
    }

    expr_transform_function_.TransformItemCached(
        quad_mask_, fPositionFullyOffScreenBottom, num_items_, num_items_);
    if (bDrawPrimitives) {
      quad_mask_.Draw();
    }
  }

  float fFirstItemToDraw = current_item_ - fNumItemsToDraw / 2.f;
  float fLastItemToDraw = current_item_ + fNumItemsToDraw / 2.f;
  int iFirstItemToDraw = std::ceil(fFirstItemToDraw);
  int iLastItemToDraw = std::ceil(fLastItemToDraw);
  if (!loop_ && !wrap_) {
    iFirstItemToDraw = std::clamp(iFirstItemToDraw, 0, num_items_);
    iLastItemToDraw = std::clamp(iLastItemToDraw, 0, num_items_);
  }

  std::vector<Actor*> subs;

  {
    // Shift m_SubActors so iFirstItemToDraw is at the beginning.
    int iNewFirstIndex = iFirstItemToDraw;
    int iDist = iNewFirstIndex - first_sub_actor_index_;
    first_sub_actor_index_ = iNewFirstIndex;
    ShiftSubActors(iDist);
  }

  int iNumToDraw = iLastItemToDraw - iFirstItemToDraw;
  for (int i = 0; i < iNumToDraw; ++i) {
    int iItem = i + iFirstItemToDraw;
    float fPosition = iItem - current_item_;
    int iIndex = i;  // index into m_SubActors
    if (loop_ || wrap_) {
      wrap(iIndex, sub_actors_.size());
    } else if (iIndex < 0 || iIndex >= (int)sub_actors_.size()) {
      continue;
    }

    // Optimization: Zero out unused parameters so that they don't create new,
    // unnecessary  entries in the position cache. On scrollers with lots of
    // items, especially with Subdivisions > 1, m_exprTransformFunction uses
    // too much memory.
    if (!function_depends_on_position_offset_) {
      fPosition = 0;
    }
    if (!function_depends_on_item_index_) {
      iItem = 0;
    }

    expr_transform_function_.TransformItemCached(
        *sub_actors_[iIndex], fPosition, iItem, num_items_);
    if (bDrawPrimitives) {
      if (draw_by_z_position_) {
        subs.push_back(sub_actors_[iIndex]);
      } else {
        sub_actors_[iIndex]->Draw();
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
