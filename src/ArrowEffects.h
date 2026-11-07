#ifndef ARROWEFFECTS_H
#define ARROWEFFECTS_H

#include "PlayerNumber.h"
#include "RageTypes.h"

class PlayerState;
class PlayerOptions;
/** @brief Functions that return properties of arrows based on Style and
 * PlayerOptions. */
class ArrowEffects {
 public:
  static void Init(PlayerNumber pn);
  static float GetTime();
  static void Update();
  // SetCurrentOptions and the hidden static variable it set exists so that
  // ArrowEffects doesn't have to reach through the PlayerState to check
  // every option.  Also, it will make it easier to implement per-column
  // mods later. -Kyz
  static void SetCurrentOptions(const PlayerOptions* options);

  // fYOffset is a vertical position in pixels relative to the center
  // (positive if has not yet been stepped on, negative if has already passed).
  // The ArrowEffect and ScrollSpeed is applied in this stage.
  static float GetYOffset(
      const PlayerState* player_state, int col, float note_beat,
      float& peak_y_offset_out, bool& is_past_peak_y_offset,
      bool absolute = false);
  static float GetYOffset(
      const PlayerState* player_state, int col, float note_beat,
      bool absolute = false) {
    float f_throw_away;
    bool b_throw_away;
    return GetYOffset(
        player_state, col, note_beat, f_throw_away, b_throw_away, absolute);
  }

  static void GetXYZPos(
      const PlayerState* player_state, int col, float y_offset,
      float y_reverse_offset, RageVector3& ret, bool with_reverse = true) {
    ret.x_ = GetMoveX(col) + GetXPos(player_state, col, y_offset);
    ret.y_ =
        GetMoveY(col) +
        GetYPos(player_state, col, y_offset, y_reverse_offset, with_reverse);
    ret.z_ = GetMoveZ(col) + GetZPos(player_state, col, y_offset);
  }

  /**
   * @brief Retrieve the actual display position.
   *
   * In this case, reverse and post-reverse-effects are factored in (fYOffset ->
   * YPos).
   * @param pPlayerState the Player's state in question, including mods.
   * @param iCol the specific arrow column.
   * @param fYOffset the original display position.
   * @param fYReverseOffsetPixels the amount offset due to reverse.
   * @param WithReverse a flag to see if the Reverse mod is on.
   * @return the actual display position. */
  static float GetYPos(
      const PlayerState* player_state, int col, float y_offset,
      float y_reverse_offset_pixels, bool with_reverse = true);

  // Inverse of ArrowGetYPos (YPos -> fYOffset).
  static float GetYOffsetFromYPos(
      int col, float y_pos, float y_reverse_offset_pixels);

  // fRotation is Z rotation of an arrow.  This will depend on the column of
  // the arrow and possibly the Arrow effect and the fYOffset (in the case of
  // EFFECT_DIZZY).
  static float GetRotationZ(
      const PlayerState* player_state, float note_beat, bool is_hold_head,
      int col);
  static float ReceptorGetRotationZ(const PlayerState* player_state, int col);

  // Due to the handling logic for holds on Twirl, we need to use an offset
  // instead. It's more intuitive for Roll to be based off offset, so use an
  // offset there too.
  static float GetRotationX(
      const PlayerState* player_state, float y_offset, bool is_hold_cap,
      int col);
  static float GetRotationY(
      const PlayerState* player_state, float y_offset, int col);

  static float ReceptorGetRotationX(const PlayerState* player_state, int col);
  static float ReceptorGetRotationY(const PlayerState* player_state, int col);

  static float GetMoveX(int col);
  static float GetMoveY(int col);
  static float GetMoveZ(int col);

  // fXPos is a horizontal position in pixels relative to the center of the
  // field. This depends on the column of the arrow and possibly the Arrow
  // effect and fYPos (in the case of EFFECT_DRUNK).
  static float GetXPos(
      const PlayerState* player_state, int col, float y_offset);

  /**
   * @brief Retrieve the Z position.
   *
   * This is normally 0. This is only visible with perspective modes.
   * @param pPlayerState the Player's state, including the mods.
   * @param iCol the specific arrow column.
   * @param fYPos the Y position of the arrow.
   * @return the Z position. */
  static float GetZPos(const PlayerState* player_state, int col, float y_pos);

  // Enable this if any ZPos effects are enabled.
  static bool NeedZBuffer();

  // fAlpha is the transparency of the arrow.  It depends on fYPos and the
  // AppearanceType.
  static float GetAlpha(
      const PlayerState* player_state, int col, float y_pos,
      float percent_fade_to_fail, float y_reverse_offset_pixels,
      float draw_distance_before_targets_pixels,
      float fade_in_percent_of_draw_far);

  // fAlpha is the transparency of the arrow.  It depends on fYPos and the
  // AppearanceType.
  static float GetGlow(
      const PlayerState* player_state, int col, float y_pos,
      float percent_fade_to_fail, float y_reverse_offset_pixels,
      float draw_distance_before_targets_pixels,
      float fade_in_percent_of_draw_far);

  /**
   * @brief Retrieve the current brightness.
   *
   * Note that this depends on fYOffset.
   * @param pPlayerState the present PlayerState.
   * @param fNoteBeat the current beat.
   * @return the current brightness. */
  static float GetBrightness(const PlayerState* player_state, float note_beat);

  // This is the zoom of the individual tracks, not of the whole Player.
  static float GetZoom(
      const PlayerState* player_state, float y_offset, int col);
  static float GetZoomVariable(float y_offset, int col, float cur_zoom);
  static float GetPulseInner();

  static float GetFrameWidthScale(
      const PlayerState* player_state, float y_offset, float overlapped_time);
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
