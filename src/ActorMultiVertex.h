#ifndef ACTORMULTIVERTEX_H
#define ACTORMULTIVERTEX_H

/** @brief ActorMultiVertex - An actor with mutiple vertices. Can be used to
 * create shapes that quads can't. */

#include <cstddef>
#include <string>
#include <vector>

#include "Actor.h"
#include "CubicSpline.h"
#include "EnumHelper.h"
#include "RageTextureID.h"
#include "RageTypes.h"
#include "Tween.h"
#include "global.h"

enum DrawMode {
  DrawMode_Quads = 0,
  DrawMode_QuadStrip,
  DrawMode_Fan,
  DrawMode_Strip,
  DrawMode_Triangles,
  DrawMode_LineStrip,
  DrawMode_SymmetricQuadStrip,
  NUM_DrawMode,
  DrawMode_Invalid
};

const std::string& DrawModeToString(DrawMode cat);
const std::string& DrawModeToLocalizedString(DrawMode cat);
LuaDeclareType(DrawMode);

class RageTexture;

class ActorMultiVertex : public Actor {
 public:
  static const size_t kNumVertSplines = 4;
  ActorMultiVertex();
  ActorMultiVertex(const ActorMultiVertex& cpy);
  virtual ~ActorMultiVertex();

  void LoadFromNode(const XNode* Node) override;
  virtual ActorMultiVertex* Copy() const override;

  struct AMV_TweenState {
    AMV_TweenState()
        : draw_mode_(DrawMode_Invalid),
          first_to_draw_(0),
          num_to_draw_(-1),
          line_width_(1.0f) {}
    static void MakeWeightedAverage(
        AMV_TweenState& average_out, const AMV_TweenState& ts1,
        const AMV_TweenState& ts2, float percent_between);
    bool operator==(const AMV_TweenState& other) const;
    bool operator!=(const AMV_TweenState& other) const {
      return !operator==(other);
    }

    void SetDrawState(DrawMode dm, int first, int num);
    int GetSafeNumToDraw(DrawMode dm, int num) const;

    std::vector<RageSpriteVertex> vertices_;
    std::vector<size_t> quad_states_;

    DrawMode draw_mode_;
    int first_to_draw_;
    int num_to_draw_;

    // needed for DrawMode_LineStrip
    float line_width_;
  };

  AMV_TweenState& AMV_DestTweenState() {
    if (amv_tweens_.empty()) {
      return amv_current_;
    } else {
      return amv_tweens_.back();
    }
  }
  const AMV_TweenState& AMV_DestTweenState() const {
    return const_cast<ActorMultiVertex*>(this)->AMV_DestTweenState();
  }

  virtual void EnableAnimation(bool bEnable) override;
  virtual void Update(float fDelta) override;
  virtual bool EarlyAbortDraw() const override;
  virtual void DrawPrimitives() override;
  virtual void DrawInternal(const AMV_TweenState* TS);

  void SetCurrentTweenStart() override;
  void EraseHeadTween() override;
  void UpdatePercentThroughTween(float PercentThroughTween) override;
  void BeginTweening(float time, ITween* pInterp) override;

  void StopTweening() override;
  void FinishTweening() override;

  void SetTexture(RageTexture* Texture);
  RageTexture* GetTexture() { return texture_; };
  void LoadFromTexture(RageTextureID ID);

  void UnloadTexture();
  void SetNumVertices(size_t n);

  void AddVertices(int Add);

  void SetEffectMode(EffectMode em) { effect_mode_ = em; }
  void SetTextureMode(TextureMode tm) { texture_mode_ = tm; }
  void SetLineWidth(float width) { AMV_DestTweenState().line_width_ = width; }

  void SetDrawState(DrawMode dm, int first, int num) {
    AMV_DestTweenState().SetDrawState(dm, first, num);
  }

  DrawMode GetDestDrawMode() const { return AMV_DestTweenState().draw_mode_; }
  int GetDestFirstToDraw() const { return AMV_DestTweenState().first_to_draw_; }
  int GetDestNumToDraw() const { return AMV_DestTweenState().num_to_draw_; }
  DrawMode GetCurrDrawMode() const { return amv_current_.draw_mode_; }
  int GetCurrFirstToDraw() const { return amv_current_.first_to_draw_; }
  int GetCurrNumToDraw() const { return amv_current_.num_to_draw_; }
  size_t GetNumVertices() { return AMV_DestTweenState().vertices_.size(); }

  void SetVertexPos(int index, float x, float y, float z);
  void SetVertexColor(int index, RageColor c);
  void SetVertexCoords(int index, float TexCoordX, float TexCoordY);

  inline void SetVertsFromSplinesInternal(
      size_t num_splines, size_t start_vert);
  void SetVertsFromSplines();
  CubicSplineN* GetSpline(size_t i);

  struct State {
    RectF rect_;
    float delay_;
  };
  int GetNumStates() const override { return states_.size(); }
  void AddState(const State& new_state) { states_.push_back(new_state); }
  void RemoveState(size_t i) {
    ASSERT(i < states_.size());
    states_.erase(states_.begin() + i);
  }
  size_t GetState() { return cur_state_; }
  State& GetStateData(size_t i) {
    ASSERT(i < states_.size());
    return states_[i];
  }
  void SetStateData(size_t i, const State& s) {
    ASSERT(i < states_.size());
    states_[i] = s;
  }
  void SetStateProperties(const std::vector<State>& new_states) {
    states_ = new_states;
    SetState(0);
  }
  void SetState(int i) override;
  void SetAllStateDelays(float delay);
  float GetAnimationLengthSeconds() const override;
  void SetSecondsIntoAnimation(float seconds) override;
  void UpdateAnimationState(bool force_update = false);
  size_t GetNumQuadStates() const {
    return AMV_DestTweenState().quad_states_.size();
  }
  void AddQuadState(size_t s) {
    AMV_DestTweenState().quad_states_.push_back(s);
  }
  void RemoveQuadState(size_t i) {
    AMV_DestTweenState().quad_states_.erase(
        AMV_DestTweenState().quad_states_.begin() + i);
  }
  size_t GetQuadState(size_t i) { return AMV_DestTweenState().quad_states_[i]; }
  void SetQuadState(size_t i, size_t s) {
    AMV_DestTweenState().quad_states_[i] = s;
  }
  bool use_animation_state_;
  bool decode_movie_;

  virtual void PushSelf(lua_State* L) override;

 private:
  RageTexture* texture_;

  std::vector<AMV_TweenState> amv_tweens_;
  AMV_TweenState amv_current_;
  AMV_TweenState amv_start_;

  // required to handle diffuse and glow
  AMV_TweenState* amv_temp_state_;

  EffectMode effect_mode_;
  TextureMode texture_mode_;

  // Four splines for controlling vert positions, because quads drawmode
  // requires four. -Kyz
  std::vector<CubicSplineN> splines_;

  bool skip_next_update_;
  float secs_into_state_;
  size_t cur_state_;
  std::vector<State> states_;
};

/**
 * @file
 * @author Matthew Gardner and Eric Reese (c) 2014
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
#endif
