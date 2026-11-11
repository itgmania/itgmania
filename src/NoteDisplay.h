#ifndef NOTE_DISPLAY_H
#define NOTE_DISPLAY_H

#include <string>
#include <vector>

#include "Actor.h"
#include "CubicSpline.h"
#include "EnumHelper.h"
#include "GameInput.h"
#include "NoteData.h"
#include "PlayerNumber.h"
#include "RageTypes.h"
#include "Tween.h"

class Sprite;
class Model;
class PlayerState;
class GhostArrowRow;
class ReceptorArrowRow;
struct TapNote;
struct HoldNoteResult;
struct NoteMetricCache_t;
/** @brief the various parts of a Note. */
enum NotePart {
  NotePart_Tap,           /**< The part representing a traditional TapNote. */
  NotePart_Mine,          /**< The part representing a mine. */
  NotePart_Lift,          /**< The part representing a lift note. */
  NotePart_Fake,          /**< The part representing a fake note. */
  NotePart_HoldHead,      /**< The part representing a hold head. */
  NotePart_HoldTail,      /**< The part representing a hold tail. */
  NotePart_HoldTopCap,    /**< The part representing a hold's top cap. */
  NotePart_HoldBody,      /**< The part representing a hold's body. */
  NotePart_HoldBottomCap, /**< The part representing a hold's bottom cap. */
  NUM_NotePart,
  NotePart_Invalid
};

/** @brief the color type of a Note. */
enum NoteColorType {
  NoteColorType_Denominator,       /**< Color by note type. */
  NoteColorType_Progress,          /**< Color by progress. */
  NoteColorType_ProgressAlternate, /**< Color by progress, except the frame
                                      boundaries are slightly later. */
  NUM_NoteColorType,
  NoteColorType_Invalid
};
const std::string& NoteColorTypeToString(NoteColorType nct);
NoteColorType StringToNoteColorType(const std::string& s);

struct NoteResource;

struct NoteColorActor {
  NoteColorActor();
  ~NoteColorActor();
  void Load(
      const std::string& button, const std::string& element, PlayerNumber,
      GameController, const std::string& color);
  Actor* Get(const std::string& color);

 private:
  std::map<std::string, NoteResource*> p_;
};

struct NoteColorSprite {
  NoteColorSprite();
  ~NoteColorSprite();
  void Load(
      const std::string& button, const std::string& element, PlayerNumber,
      GameController, const std::string& color);
  Sprite* Get(const std::string& color);

 private:
  std::map<std::string, NoteResource*> p_;
};
/** @brief What types of holds are there? */
enum HoldType {
  hold, /**< Merely keep your foot held on the body for it to count. */
  roll, /**< Keep hitting the hold body for it to stay alive. */
  // minefield,
  NUM_HoldType,
  HoldType_Invalid
};
/** @brief Loop through each HoldType. */
#define FOREACH_HoldType(i) FOREACH_ENUM(HoldType, i)
const std::string& HoldTypeToString(HoldType ht);

enum ActiveType { active, inactive, NUM_ActiveType, ActiveType_Invalid };
/** @brief Loop through each ActiveType. */
#define FOREACH_ActiveType(i) FOREACH_ENUM(ActiveType, i)
const std::string& ActiveTypeToString(ActiveType at);

enum NoteColumnSplineMode {
  NCSM_Disabled,
  NCSM_Offset,
  NCSM_Position,
  NUM_NoteColumnSplineMode,
  NoteColumnSplineMode_Invalid
};

const std::string& NoteColumnSplineModeToString(NoteColumnSplineMode ncsm);
LuaDeclareType(NoteColumnSplineMode);

// A little pod struct to carry the data the NoteField needs to pass to the
// NoteDisplay during rendering.
struct NoteFieldRenderArgs {
  const PlayerState* player_state_;  // to look up PlayerOptions
  float reverse_offset_pixels_;
  ReceptorArrowRow* receptor_row_;
  GhostArrowRow* ghost_row_;
  const NoteData* note_data_;
  int first_row_;
  int last_row_;
  float draw_pixels_before_targets_;
  float draw_pixels_after_targets_;
  int* selection_begin_marker_;
  int* selection_end_marker_;
  float selection_glow_;
  float fail_fade_;
  float fade_before_targets_;
};

// NCSplineHandler exists to allow NoteColumnRenderer to have separate
// splines for position, rotation, and zoom, while concisely presenting the
// same interface for all three.
struct NCSplineHandler {
  NCSplineHandler() {
    spline_.redimension(3);
    spline_.owned_by_actor_ = true;
    spline_mode_ = NCSM_Disabled;
    receptor_t_ = 0.0f;
    beats_per_t_ = 1.0f;
    subtract_song_beat_from_curr_ = true;
  }
  float BeatToTValue(float song_beat, float note_beat) const;
  void EvalForBeat(float song_beat, float note_beat, RageVector3& ret) const;
  void EvalDerivForBeat(
      float song_beat, float note_beat, RageVector3& ret) const;
  void EvalForReceptor(float song_beat, RageVector3& ret) const;
  static void MakeWeightedAverage(
      NCSplineHandler& out, const NCSplineHandler& from,
      const NCSplineHandler& to, float between);

  CubicSplineN spline_;
  NoteColumnSplineMode spline_mode_;
  float receptor_t_;
  float beats_per_t_;
  bool subtract_song_beat_from_curr_;

  void PushSelf(lua_State* L);
};

struct NoteColumnRenderArgs {
  void spae_pos_for_beat(
      const PlayerState* player_state, float beat, float y_offset,
      float y_reverse_offset, RageVector3& sp_pos, RageVector3& ae_pos) const;
  void spae_zoom_for_beat(
      const PlayerState* state, float beat, RageVector3& sp_zoom,
      RageVector3& ae_zoom, int col_num, float y_offset) const;
  void SetPRZForActor(
      Actor* actor, const RageVector3& sp_pos, const RageVector3& ae_pos,
      const RageVector3& sp_rot, const RageVector3& ae_rot,
      const RageVector3& sp_zoom, const RageVector3& ae_zoom) const;
  const NCSplineHandler* pos_handler_;
  const NCSplineHandler* rot_handler_;
  const NCSplineHandler* zoom_handler_;
  RageColor diffuse_;
  RageColor glow_;
  float song_beat_;
  int column_;
};

/** @brief Draws TapNotes and HoldNotes. */
class NoteDisplay {
 public:
  NoteDisplay();
  ~NoteDisplay();

  void Load(
      int col_num, const PlayerState* player_state,
      float y_reverse_offset_pixels);

  static void Update(float delta_time);

  bool IsOnScreen(
      float beat, int col, int draw_distance_after_targets_pixels,
      int draw_distance_before_targets_pixels) const;

  bool DrawHoldsInRange(
      const NoteFieldRenderArgs& field_args,
      const NoteColumnRenderArgs& column_args,
      const std::vector<NoteData::TrackMap::const_iterator>& tap_set);
  bool DrawTapsInRange(
      const NoteFieldRenderArgs& field_args,
      const NoteColumnRenderArgs& column_args,
      const std::vector<NoteData::TrackMap::const_iterator>& tap_set);
  /**
   * @brief Draw the TapNote onto the NoteField.
   * @param tn the TapNote in question.
   * @param iCol the column.
   * @param float fBeat the beat to draw them on.
   * @param bOnSameRowAsHoldStart a flag to see if a hold is on the same beat.
   * @param bOnSameRowAsRollStart a flag to see if a roll is on the same beat.
   * @param bIsAddition a flag to see if this note was added via mods.
   * @param fPercentFadeToFail at what point do the notes fade on failure?
   * @param fReverseOffsetPixels How are the notes adjusted on Reverse?
   * @param fDrawDistanceAfterTargetsPixels how much to draw after the
   * receptors.
   * @param fDrawDistanceBeforeTargetsPixels how much ot draw before the
   * receptors.
   * @param fFadeInPercentOfDrawFar when to start fading in. */
  void DrawTap(
      const TapNote& tn, const NoteFieldRenderArgs& field_args,
      const NoteColumnRenderArgs& column_args, float beat,
      bool on_same_row_as_hold_start, bool on_same_row_as_roll_beat,
      bool is_addition, float percent_fade_to_fail);
  void DrawHold(
      const TapNote& tn, const NoteFieldRenderArgs& field_args,
      const NoteColumnRenderArgs& column_args, int row, bool is_being_held,
      const HoldNoteResult& result, bool is_addition,
      float percent_fade_to_fail);

  bool DrawHoldHeadForTapsOnSameRow() const;
  bool DrawRollHeadForTapsOnSameRow() const;

 private:
  void SetActiveFrame(
      float note_beat, Actor& actor_to_set, float animation_length, bool vivid);
  Actor* GetTapActor(NoteColorActor& nca, NotePart part, float note_beat);
  Actor* GetHoldActor(
      NoteColorActor nca[NUM_HoldType][NUM_ActiveType], NotePart part,
      float note_beat, bool is_roll, bool is_being_held);
  Sprite* GetHoldSprite(
      NoteColorSprite ncs[NUM_HoldType][NUM_ActiveType], NotePart part,
      float note_beat, bool is_roll, bool is_being_held);

  struct draw_hold_part_args {
    int y_step_;
    float percent_fade_to_fail_;
    float color_scale_;
    float overlapped_time_;
    float y_top_;
    float y_bottom_;
    float y_start_pos_;
    float y_end_pos_;
    float top_beat_;
    float bottom_beat_;
    bool wrapping_;
    bool anchor_to_top_;
    bool flip_texture_vertically_;
  };

  void DrawActor(
      const TapNote& tn, Actor* actor, NotePart part,
      const NoteFieldRenderArgs& field_args,
      const NoteColumnRenderArgs& column_args, float y_offset, float beat,
      bool is_addition, float percent_fade_to_fail, float color_scale,
      bool is_being_held);
  void DrawHoldPart(
      std::vector<Sprite*>& spr, const NoteFieldRenderArgs& field_args,
      const NoteColumnRenderArgs& column_args,
      const draw_hold_part_args& part_args, bool glow, int part_type);
  void DrawHoldBodyInternal(
      std::vector<Sprite*>& sprite_top, std::vector<Sprite*>& sprite_body,
      std::vector<Sprite*>& sprite_bottom,
      const NoteFieldRenderArgs& field_args,
      const NoteColumnRenderArgs& column_args, draw_hold_part_args& part_args,
      const float head_minus_top, const float tail_plus_bottom,
      const float y_head, const float y_tail, const float top_beat,
      const float bottom_beat, bool glow);
  void DrawHoldBody(
      const TapNote& tn, const NoteFieldRenderArgs& field_args,
      const NoteColumnRenderArgs& column_args, float beat, bool being_held,
      float y_head, float y_tail, float percent_fade_to_fail, float color_scale,
      float top_beat, float bottom_beat);

  const PlayerState* player_state_;  // to look up PlayerOptions
  NoteMetricCache_t* cache_;

  NoteColorActor tap_note_;
  NoteColorActor tap_mine_;
  NoteColorActor tap_lift_;
  NoteColorActor tap_fake_;
  NoteColorActor hold_head_[NUM_HoldType][NUM_ActiveType];
  NoteColorSprite hold_top_cap_[NUM_HoldType][NUM_ActiveType];
  NoteColorSprite hold_body_[NUM_HoldType][NUM_ActiveType];
  NoteColorSprite hold_bottom_cap_[NUM_HoldType][NUM_ActiveType];
  NoteColorActor hold_tail_[NUM_HoldType][NUM_ActiveType];
  float y_reverse_offset_pixels_;
};

// So, this is a bit screwy, and it's partly because routine forces rendering
// notes from different noteskins in the same column.
// NoteColumnRenderer exists to hold all the data needed for rendering a
// column and apply any transforms from that column's actor to the
// NoteDisplays that render the notes.
// NoteColumnRenderer is also used as a fake parent for the receptor and ghost
// actors so they can move with the rest of the column.  I didn't use
// ActorProxy because the receptor/ghost actors need to pull in the parent
// state of their rows and the parent state of the column. -Kyz

struct NoteColumnRenderer : public Actor {
  NoteDisplay* displays_[kPlayerInvalid + 1];
  NoteFieldRenderArgs* field_render_args_;
  NoteColumnRenderArgs column_render_args_;
  int column_;

  // UpdateReceptorGhostStuff takes care of the logic for making the ghost
  // and receptor positions follow the splines.  It's called by their row
  // update functions. -Kyz
  void UpdateReceptorGhostStuff(Actor* receptor) const;
  virtual void DrawPrimitives() override;
  virtual void PushSelf(lua_State* L) override;

  struct NCR_TweenState {
    NCR_TweenState();
    NCSplineHandler pos_handler_;
    NCSplineHandler rot_handler_;
    NCSplineHandler zoom_handler_;
    static void MakeWeightedAverage(
        NCR_TweenState& out, const NCR_TweenState& from,
        const NCR_TweenState& to, float between);
    bool operator==(const NCR_TweenState& other) const;
    bool operator!=(const NCR_TweenState& other) const {
      return !operator==(other);
    }
  };

  NCR_TweenState& NCR_DestTweenState() {
    if (ncr_tweens_.empty()) {
      return ncr_current_;
    } else {
      return ncr_tweens_.back();
    }
  }
  const NCR_TweenState& NCR_DestTweenState() const {
    return const_cast<NoteColumnRenderer*>(this)->NCR_DestTweenState();
  }

  virtual void SetCurrentTweenStart() override;
  virtual void EraseHeadTween() override;
  virtual void UpdatePercentThroughTween(float between) override;
  virtual void BeginTweening(float time, ITween* interp) override;
  virtual void StopTweening() override;
  virtual void FinishTweening() override;

  NCSplineHandler* GetPosHandler() {
    return &NCR_DestTweenState().pos_handler_;
  }
  NCSplineHandler* GetRotHandler() {
    return &NCR_DestTweenState().rot_handler_;
  }
  NCSplineHandler* GetZoomHandler() {
    return &NCR_DestTweenState().zoom_handler_;
  }

 private:
  std::vector<NCR_TweenState> ncr_tweens_;
  NCR_TweenState ncr_current_;
  NCR_TweenState ncr_start_;
};

#endif

/**
 * NoteColumnRenderer and associated spline stuff (c) Eric Reese 2014-2015
 * @file
 * @author Brian Bugh, Ben Nordstrom, Chris Danford, Steve Checkoway (c)
 * 2001-2006
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
