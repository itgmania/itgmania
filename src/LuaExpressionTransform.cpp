#include "global.h"

#include "LuaExpressionTransform.h"

#include "LuaManager.h"
#include "RageUtil.h"

LuaExpressionTransform::LuaExpressionTransform() { num_subdivisions_ = 1; }

LuaExpressionTransform::~LuaExpressionTransform() {}

void LuaExpressionTransform::SetFromReference(const LuaReference& ref) {
  transform_function_ = ref;
}

void LuaExpressionTransform::TransformItemDirect(
    Actor& actor, float position_offset_from_center, int item_index,
    int num_items) const {
  Lua* L = LUA->Get();
  transform_function_.PushSelf(L);
  ASSERT(!lua_isnil(L, -1));
  actor.PushSelf(L);
  LuaHelpers::Push(L, position_offset_from_center);
  LuaHelpers::Push(L, item_index);
  LuaHelpers::Push(L, num_items);
  RString error = "Lua error in Transform function: ";
  LuaHelpers::RunScriptOnStack(L, error, 4, 0, true);
  LUA->Release(L);
}

const Actor::TweenState& LuaExpressionTransform::GetTransformCached(
    float position_offset_from_center, int item_index, int num_iems) const {
  PositionOffsetAndItemIndex key = {position_offset_from_center, item_index};

  auto iter = position_to_tween_state_cache_.find(key);
  if (iter != position_to_tween_state_cache_.end()) {
    return iter->second;
  }

  Actor actor;
  TransformItemDirect(actor, position_offset_from_center, item_index, num_iems);
  return position_to_tween_state_cache_[key] = actor.DestTweenState();
}

void LuaExpressionTransform::TransformItemCached(
    Actor& actor, float position_offset_from_center, int item_index,
		int num_iems) {
  float interval = 1.0f / num_subdivisions_;
  float floor_val = QuantizeDown(position_offset_from_center, interval);
  float ceil_val = QuantizeUp(position_offset_from_center, interval);

  if (floor_val == ceil_val) {
    actor.DestTweenState() = GetTransformCached(ceil_val, item_index, num_iems);
  } else {
    const Actor::TweenState& tween_state_floor =
        GetTransformCached(floor_val, item_index, num_iems);
    const Actor::TweenState& tween_state_ceil =
        GetTransformCached(ceil_val, item_index, num_iems);

    float percent_toward_ceil =
        SCALE(position_offset_from_center, floor_val, ceil_val, 0.0f, 1.0f);
    Actor::TweenState::MakeWeightedAverage(
        actor.DestTweenState(), tween_state_floor, tween_state_ceil,
				percent_toward_ceil);
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
