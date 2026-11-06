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
    Actor& a, float fPositionOffsetFromCenter, int iItemIndex,
    int iNumItems) const {
  Lua* L = LUA->Get();
  expr_transform_function_.PushSelf(L);
  ASSERT(!lua_isnil(L, -1));
  a.PushSelf(L);
  LuaHelpers::Push(L, fPositionOffsetFromCenter);
  LuaHelpers::Push(L, iItemIndex);
  LuaHelpers::Push(L, iNumItems);
  std::string error = "Lua error in Transform function: ";
  LuaHelpers::RunScriptOnStack(L, error, 4, 0, true);
  LUA->Release(L);
}

const Actor::TweenState& LuaExpressionTransform::GetTransformCached(
    float fPositionOffsetFromCenter, int iItemIndex, int iNumItems) const {
  PositionOffsetAndItemIndex key = {fPositionOffsetFromCenter, iItemIndex};

  std::map<PositionOffsetAndItemIndex, Actor::TweenState>::const_iterator iter =
      map_position_to_tween_state_cache_.find(key);
  if (iter != map_position_to_tween_state_cache_.end()) {
    return iter->second;
  }

  Actor a;
  TransformItemDirect(a, fPositionOffsetFromCenter, iItemIndex, iNumItems);
  return map_position_to_tween_state_cache_[key] = a.DestTweenState();
}

void LuaExpressionTransform::TransformItemCached(
    Actor& a, float fPositionOffsetFromCenter, int iItemIndex, int iNumItems) {
  float fInterval = 1.0f / num_subdivisions_;
  float fFloor = QuantizeDown(fPositionOffsetFromCenter, fInterval);
  float fCeil = QuantizeUp(fPositionOffsetFromCenter, fInterval);

  if (fFloor == fCeil) {
    a.DestTweenState() = GetTransformCached(fCeil, iItemIndex, iNumItems);
  } else {
    const Actor::TweenState& tsFloor =
        GetTransformCached(fFloor, iItemIndex, iNumItems);
    const Actor::TweenState& tsCeil =
        GetTransformCached(fCeil, iItemIndex, iNumItems);

    float fPercentTowardCeil =
        SCALE(fPositionOffsetFromCenter, fFloor, fCeil, 0.0f, 1.0f);
    Actor::TweenState::MakeWeightedAverage(
        a.DestTweenState(), tsFloor, tsCeil, fPercentTowardCeil);
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
