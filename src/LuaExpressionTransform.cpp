#include "LuaExpressionTransform.h"

#include <map>
#include <string>

#include "Actor.h"
#include "LuaManager.h"
#include "RageUtil.h"
#include "global.h"

LuaExpressionTransform::LuaExpressionTransform() { num_subdivisions_ = 1; }

LuaExpressionTransform::~LuaExpressionTransform() {}

void LuaExpressionTransform::SetFromReference(const LuaReference& ref) {
  expr_transform_function_ = ref;
}

void LuaExpressionTransform::TransformItemDirect(
    Actor& a, float position_offset_from_center, int item_index,
    int num_items) const {
  Lua* L = LUA->Get();
  expr_transform_function_.PushSelf(L);
  ASSERT(!lua_isnil(L, -1));
  a.PushSelf(L);
  LuaHelpers::Push(L, position_offset_from_center);
  LuaHelpers::Push(L, item_index);
  LuaHelpers::Push(L, num_items);
  std::string error = "Lua error in Transform function: ";
  LuaHelpers::RunScriptOnStack(L, error, 4, 0, true);
  LUA->Release(L);
}

const Actor::TweenState& LuaExpressionTransform::GetTransformCached(
    float position_offset_from_center, int item_index, int num_items) const {
  PositionOffsetAndItemIndex key = {position_offset_from_center, item_index};

  std::map<PositionOffsetAndItemIndex, Actor::TweenState>::const_iterator iter =
      map_position_to_tween_state_cache_.find(key);
  if (iter != map_position_to_tween_state_cache_.end()) {
    return iter->second;
  }

  Actor a;
  TransformItemDirect(a, position_offset_from_center, item_index, num_items);
  return map_position_to_tween_state_cache_[key] = a.DestTweenState();
}

void LuaExpressionTransform::TransformItemCached(
    Actor& a, float position_offset_from_center, int item_index,
    int num_items) {
  float interval = 1.0f / num_subdivisions_;
  float f_floor = QuantizeDown(position_offset_from_center, interval);
  float f_ceil = QuantizeUp(position_offset_from_center, interval);

  if (f_floor == f_ceil) {
    a.DestTweenState() = GetTransformCached(f_ceil, item_index, num_items);
  } else {
    const Actor::TweenState& ts_floor =
        GetTransformCached(f_floor, item_index, num_items);
    const Actor::TweenState& ts_ceil =
        GetTransformCached(f_ceil, item_index, num_items);

    float percent_toward_ceil =
        SCALE(position_offset_from_center, f_floor, f_ceil, 0.0f, 1.0f);
    Actor::TweenState::MakeWeightedAverage(
        a.DestTweenState(), ts_floor, ts_ceil, percent_toward_ceil);
  }
}

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
