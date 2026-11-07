#ifndef ActorScroller_H
#define ActorScroller_H

#include <string>

#include "ActorFrame.h"
#include "LuaExpressionTransform.h"
#include "LuaReference.h"
#include "Quad.h"

class XNode;

/** @brief ActorFrame that moves its children. */
class ActorScroller : public ActorFrame {
 public:
  ActorScroller();

  void SetTransformFromReference(const LuaReference& ref);
  void SetTransformFromExpression(const std::string& transform_function);
  void SetTransformFromWidth(float item_width);
  void SetTransformFromHeight(float item_height);

  void Load2();

  void EnableMask(float width, float height);
  void DisableMask();

  virtual void UpdateInternal(float delta);
  virtual void DrawPrimitives();  // handles drawing and doesn't call
                                  // ActorFrame::DrawPrimitives

  void PositionItems();

  void LoadFromNode(const XNode* node);
  virtual ActorScroller* Copy() const;

  void SetLoop(bool loop) { loop_ = loop; }
  void SetWrap(bool wrap) { wrap_ = wrap; }
  void SetNumItemsToDraw(float num_items_to_draw) {
    num_items_to_draw_ = num_items_to_draw;
  }
  void SetDestinationItem(float item_index) { destination_item_ = item_index; }
  void SetCurrentAndDestinationItem(float item_index) {
    current_item_ = destination_item_ = item_index;
  }
  float GetCurrentItem() const { return current_item_; }
  float GetDestinationItem() const { return destination_item_; }
  void ScrollThroughAllItems();
  void ScrollWithPadding(float item_padding_start, float item_padding_end);
  void SetPauseCountdownSeconds(float secs) { pause_countdown_seconds_ = secs; }
  void SetFastCatchup(bool on) { fast_catchup_ = on; }
  void SetSecondsPerItem(float seconds) { seconds_per_item_ = seconds; }
  void SetSecondsPauseBetweenItems(float seconds) {
    seconds_pause_between_items_ = seconds;
  }
  float GetSecondsPauseBetweenItems() { return seconds_pause_between_items_; }
  void SetNumSubdivisions(int num_subdivisions) {
    expr_transform_function_.SetNumSubdivisions(num_subdivisions);
  }
  float GetSecondsForCompleteScrollThrough() const;
  float GetSecondsToDestination() const;
  int GetNumItems() const { return num_items_; }

  // Commands
  void PushSelf(lua_State* L);

 protected:
  void PositionItemsAndDrawPrimitives(bool draw_primitives);
  virtual void ShiftSubActors(int dist);

  int num_items_;
  /**
   * @brief the current item we are focused on.
   *
   * An item at the center of the list, usually between 0 and
   * m_SubActors.size(), will approach its destination.
   *
   * The above comment was paraphrased from what was here previously. It could
   * use some clearing up. -Wolfman2000 */
  float current_item_;
  float destination_item_;
  /**
   * @brief How many seconds are there per item?
   *
   * If this is less than zero, then we are not scrolling. */
  float seconds_per_item_;
  float seconds_pause_between_items_;
  float num_items_to_draw_;
  int first_sub_actor_index_;
  bool loop_;
  bool wrap_;
  bool fast_catchup_;
  bool function_depends_on_position_offset_;
  bool function_depends_on_item_index_;
  float pause_countdown_seconds_;
  float quantize_pixels_;

  Quad quad_mask_;
  float mask_width_, mask_height_;

  LuaExpressionTransform
      expr_transform_function_;  // params: self,offset,itemIndex,numItems
};

class ActorScrollerAutoDeleteChildren : public ActorScroller {
 public:
  ActorScrollerAutoDeleteChildren() { DeleteChildrenWhenDone(true); }
  virtual bool AutoLoadChildren() const { return true; }
  virtual ActorScrollerAutoDeleteChildren* Copy() const;
};

#endif

/**
 * @file
 * @author Chris Danford (c) 2003-2004
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
