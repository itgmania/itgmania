/* LuaExpressionTransform -  */

#ifndef LuaExpressionTransform_H
#define LuaExpressionTransform_H

#include <map>

#include "Actor.h"
#include "LuaReference.h"
#include "global.h"

/**
 * @brief Handle transforming a list of items
 *
 * Cache item transforms based on fPositionOffsetFromCenter and iItemIndex for
 * speed. */
class LuaExpressionTransform {
 public:
  LuaExpressionTransform();
  ~LuaExpressionTransform();

  void SetFromReference(const LuaReference& ref);
  void SetNumSubdivisions(int num_subdivisions) {
    ASSERT(num_subdivisions > 0);
    num_subdivisions_ = num_subdivisions;
  }

  void TransformItemCached(
      Actor& a, float position_offset_from_center, int item_index,
      int num_items);
  void TransformItemDirect(
      Actor& a, float position_offset_from_center, int item_index,
      int num_items) const;
  const Actor::TweenState& GetTransformCached(
      float position_offset_from_center, int item_index, int num_items) const;
  void ClearCache() { map_position_to_tween_state_cache_.clear(); }

 protected:
  LuaReference
      expr_transform_function_;  // params: self,offset,itemIndex,numItems
  int num_subdivisions_;         // 1 == one evaluation per position
  struct PositionOffsetAndItemIndex {
    float position_offset_from_center_;
    int item_index_;

    bool operator<(const PositionOffsetAndItemIndex& other) const {
      if (position_offset_from_center_ != other.position_offset_from_center_) {
        return position_offset_from_center_ <
               other.position_offset_from_center_;
      }
      return item_index_ < other.item_index_;
    }
  };
  mutable std::map<PositionOffsetAndItemIndex, Actor::TweenState>
      map_position_to_tween_state_cache_;
};

#endif

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
