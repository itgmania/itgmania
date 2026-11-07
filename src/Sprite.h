#ifndef SPRITE_H
#define SPRITE_H

#include <string>
#include <vector>

#include "Actor.h"
#include "RageTextureID.h"
#include "RageTypes.h"

void TexCoordArrayFromRect(float image_coords[8], const RectF& rect);

class RageTexture;
/** @brief A bitmap Actor that animates and moves around. */
class Sprite : public Actor {
 public:
  /** @brief The Sprite's present state. */
  struct State {
    RectF rect_;
    /** @brief The number of "seconds to show". */
    float delay_;
  };

  Sprite();
  Sprite(const Sprite& cpy);
  virtual ~Sprite();

  // See explanation in source.
  static Sprite* NewBlankSprite();

  virtual void InitState() override;

  void LoadFromNode(const XNode* node) override;
  virtual Sprite* Copy() const override;

  virtual bool EarlyAbortDraw() const override;
  virtual void DrawPrimitives() override;
  virtual void Update(float delta_time) override;

  void
  UpdateAnimationState();  // take m_fSecondsIntoState, and move to a new state

  // Adjust texture properties for song backgrounds.
  static RageTextureID SongBGTexture(RageTextureID id);

  // Adjust texture properties for song banners.
  static RageTextureID SongBannerTexture(RageTextureID id);

  virtual void Load(RageTextureID id);
  void SetTexture(RageTexture* texture);

  void UnloadTexture();
  RageTexture* GetTexture() { return texture_; };

  virtual void EnableAnimation(bool enable) override;

  virtual int GetNumStates() const override;
  virtual void SetState(int new_state) override;
  int GetState() { return cur_state_; }
  virtual float GetAnimationLengthSeconds() const override {
    return animation_length_seconds_;
  }
  virtual void RecalcAnimationLengthSeconds();
  virtual void SetSecondsIntoAnimation(float seconds) override;
  void SetStateProperties(const std::vector<State>& new_states) {
    states_ = new_states;
    RecalcAnimationLengthSeconds();
    SetState(0);
  }

  std::string GetTexturePath() const;

  void SetCustomTextureRect(const RectF& new_texcoord_frect);
  void SetCustomTextureCoords(float tex_coords[8]);
  void SetCustomImageRect(RectF rect_image_coords);  // in image pixel space
  void SetCustomPosCoords(float pos_coords[8]);
  const RectF* GetCurrentTextureCoordRect() const;
  const RectF* GetTextureCoordRectForState(int state) const;
  void StopUsingCustomCoords();
  void StopUsingCustomPosCoords();
  void GetActiveTextureCoords(float tex_coords_out[8]) const;
  void StretchTexCoords(float x, float y);
  void AddImageCoords(float x, float y);  // in image pixel space
  void SetEffectMode(EffectMode em) { effect_mode_ = em; }

  void LoadFromCached(const std::string& dir, const std::string& path);

  void SetTexCoordVelocity(float vel_x, float vel_y);
  /**
   * @brief Scale the Sprite while maintaining the aspect ratio.
   *
   * It has to fit within and become clipped to the given parameters.
   * @param fWidth the new width.
   * @param fHeight the new height. */
  void ScaleToClipped(float width, float height);
  void CropTo(float width, float height);

  // Commands
  virtual void PushSelf(lua_State* L) override;

  void SetAllStateDelays(float delay);

  bool decode_movie_;

  bool use_effect_clock_for_texcoords_;

 protected:
  void LoadFromTexture(RageTextureID id);

 private:
  void LoadStatesFromTexture();

  void DrawTexture(const TweenState* state);

  RageTexture* texture_;

  std::vector<State> states_;
  int cur_state_;
  /** @brief The number of seconds that have elapsed since we switched to this
   * frame. */
  float secs_into_state_;
  float animation_length_seconds_;

  EffectMode effect_mode_;
  bool using_custom_tex_coords_;
  bool using_custom_pos_coords_;
  bool skip_next_update_;
  /**
   * @brief Set up the coordinates for the texture.
   *
   * The first two parameters are the (x, y) coordinates for the top left.
   * The remaining six are for the (x, y) coordinates for bottom left,
   * bottom right, and top right respectively. */
  float custom_tex_coords_[8];
  /**
   * @brief Set up the coordinates for the position.
   *
   * These are offsets for the quad the sprite will be drawn to.
   * The first two are the (x, y) offsets for the top left.
   * The remaining six are for the (x, y) coordinates for bottom left,
   * bottom right, and top right respectively.
   * These are offsets instead of a replacement for m_size to avoid
   * complicating the cropping code. */
  float custom_pos_coords_[8];

  // Remembered clipped dimensions are applied on Load().
  // -1 means no remembered dimensions;
  float remembered_clip_width_, remembered_clip_height_;

  float tex_coord_velocity_x_;
  float tex_coord_velocity_y_;
};

#endif

/**
 * @file
 * @author Chris Danford (c) 2001-2004
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
