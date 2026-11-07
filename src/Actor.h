#ifndef ACTOR_H
#define ACTOR_H

#include "EnumHelper.h"
#include "LuaReference.h"
#include "PlayerNumber.h"
#include "RageTypes.h"
#include "RageUtil_AutoPtr.h"
class XNode;
struct lua_State;
class LuaClass;
#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "MessageManager.h"
#include "Tween.h"

typedef AutoPtrCopyOnWrite<LuaReference> apActorCommands;

/** @brief The background layer. */
constexpr int DRAW_ORDER_BEFORE_EVERYTHING = -200;
/** @brief The underlay layer. */
constexpr int DRAW_ORDER_UNDERLAY = -100;
/** @brief The decorations layer. */
constexpr int DRAW_ORDER_DECORATIONS = 0;
/** @brief The overlay layer.
 *
 * Normal screen elements go here. */
constexpr int DRAW_ORDER_OVERLAY = 100;
/** @brief The transitions layer. */
constexpr int DRAW_ORDER_TRANSITIONS = 200;
/** @brief The over everything layer. */
constexpr int DRAW_ORDER_AFTER_EVERYTHING = 300;

/** @brief The different horizontal alignments. */
enum HorizAlign {
  HorizAlign_Left,   /**< Align to the left. */
  HorizAlign_Center, /**< Align to the center. */
  HorizAlign_Right,  /**< Align to the right. */
  NUM_HorizAlign,    /**< The number of horizontal alignments. */
  HorizAlign_Invalid
};
LuaDeclareType(HorizAlign);

/** @brief The different vertical alignments. */
enum VertAlign {
  VertAlign_Top,    /**< Align to the top. */
  VertAlign_Middle, /**< Align to the middle. */
  VertAlign_Bottom, /**< Align to the bottom. */
  NUM_VertAlign,    /**< The number of vertical alignments. */
  VertAlign_Invalid
};
LuaDeclareType(VertAlign);

/** @brief The left horizontal alignment constant. */
constexpr float align_left = 0.0f;
/** @brief The center horizontal alignment constant. */
constexpr float align_center = 0.5f;
/** @brief The right horizontal alignment constant. */
constexpr float align_right = 1.0f;
/** @brief The top vertical alignment constant. */
constexpr float align_top = 0.0f;
/** @brief The middle vertical alignment constant. */
constexpr float align_middle = 0.5f;
/** @brief The bottom vertical alignment constant. */
constexpr float align_bottom = 1.0f;

// This is the number of colors in Actor::diffuse.  Actor has multiple
// diffuse colors so that each edge can be a different color, and the actor
// is drawn with a gradient between them.
// I doubt I actually found all the places that touch diffuse and rely on the
// number of diffuse colors, so change this at your own risk. -Kyz
constexpr int NUM_DIFFUSE_COLORS = 4;

// ssc futures:
/*
enum EffectAction
{
        EffectAction_None,			// no_effect
        // [Diffuse]
        EffectAction_DiffuseBlink,	// diffuse_blink
        EffectAction_DiffuseShift,	// diffuse_shift
        EffectAction_DiffuseRamp,	// diffuse_ramp
        EffectAction_Rainbow,		// rainbow
        // [Glow]
        EffectAction_GlowBlink,		// glow_blink
        EffectAction_GlowShift,		// glow_shift
        EffectAction_GlowRamp,		// glow_ramp
        // [Translate]
        EffectAction_Bob,
        EffectAction_Bounce,
        EffectAction_Vibrate,
        // [Rotate]
        EffectAction_Spin,
        EffectAction_Wag,
        // [Zoom]
        EffectAction_Pulse,
        NUM_EffectAction,
        EffectAction_Invalid
};
LuaDeclareType( EffectAction );
*/

/** @brief Base class for all objects that appear on the screen. */
class Actor : public MessageSubscriber {
 public:
  /** @brief Set up the Actor with its initial settings. */
  Actor();
  /**
   * @brief Copy a new Actor to the old one.
   * @param cpy the new Actor to use in place of this one. */
  Actor(const Actor& cpy);
  Actor& operator=(Actor other);
  virtual ~Actor();
  virtual Actor* Copy() const;
  virtual void InitState();
  virtual void LoadFromNode(const XNode* node);

  static void SetBGMTime(
      float time, float beat, float time_no_offset, float beat_no_offset);
  static void SetPlayerBGMBeat(
      PlayerNumber pn, float beat, float beat_no_offset);
  static void SetBGMLight(int light_number, float cabinet_lights);

  /**
   * @brief The list of the different effects.
   *
   * todo: split out into diffuse effects and translation effects, or
   * create an effect stack instead. -aj */
  enum Effect {
    no_effect,
    diffuse_blink,
    diffuse_shift,
    diffuse_ramp,
    glow_blink,
    glow_shift,
    glow_ramp,
    rainbow,
    wag,
    bounce,
    bob,
    pulse,
    spin,
    vibrate
  };

  /** @brief Various values an Actor's effect can be tied to. */
  enum EffectClock {
    CLOCK_TIMER,
    CLOCK_TIMER_GLOBAL,
    CLOCK_BGM_TIME,
    CLOCK_BGM_BEAT,
    CLOCK_BGM_TIME_NO_OFFSET,
    CLOCK_BGM_BEAT_NO_OFFSET,
    CLOCK_BGM_BEAT_PLAYER1,
    CLOCK_BGM_BEAT_PLAYER2,
    CLOCK_LIGHT_1 = 1000,
    CLOCK_LIGHT_LAST = 1100,
    NUM_CLOCKS
  };

  /*
   * @brief What type of Effect this is.
   *
   * This is an internal enum for checking if an effect can be run;
   * You can't have more than one of most EffectTypes in the Effect list. (You
   * might be able to have mutliple EffectType_Translates; not sure yet.) -aj */
  /*
  enum EffectType {
          EffectType_Diffuse,
          EffectType_Glow,
          EffectType_Translate,
          EffectType_Rotate,
          EffectType_Zoom,
          NUM_EffectType,
          EffectType_Invalid
  };
  */

  // todo: use this instead of the Effect enum -aj
  /*
  // This is similar to Attributes in BitmapText as far as implementation.
  struct Effect
  {
          Effect() : m_Action(EffectAction_None), m_Type(EffectType_Invalid),
  m_fSecsIntoEffect(0), m_fEffectDelta(0), m_fEffectRampUp(0.5f),
  m_fEffectHoldAtHalf(0), m_fEffectRampDown(0.5f), m_fEffectHoldAtZero(0),
  m_fEffectOffset(0), m_EffectClock(CLOCK_TIMER),
  m_vEffectMagnitude(RageVector3(0,0,10)), m_effectColor1(RageColor(1,1,1,1)),
  m_effectColor2(RageColor(1,1,1,1)) { }

          std::string			m_sName; // friendly name
          EffectAction	m_Action; // replaces the old Effect enum
          EffectType		m_Type; // determined by EffectAction
          float			m_fSecsIntoEffect;
          float			m_fEffectDelta;
          RageColor		m_EffectColor1;
          RageColor		m_EffectColor2;
          RageVector3		m_vEffectMagnitude;
          EffectClock		m_EffectClock;
          // units depend on m_EffectClock
          float			m_fEffectRampUp;
          float			m_fEffectHoldAtHalf;
          float			m_fEffectRampDown;
          float			m_fEffectHoldAtZero;
          float			m_fEffectOffset;
  };
  */

  /**
   * @brief The present state for the Tween.
   */
  struct TweenState {
    void Init();
    static void MakeWeightedAverage(
        TweenState& average_out, const TweenState& ts1, const TweenState& ts2,
        float percent_between);
    bool operator==(const TweenState& other) const;
    bool operator!=(const TweenState& other) const {
      return !operator==(other);
    }

    // start and end position for tweening
    RageVector3 pos_;
    RageVector3 rotation_;
    RageVector4 quat_;
    RageVector3 scale_;
    float skew_x_, skew_y_;
    /**
     * @brief The amount of cropping involved.
     *
     * If 0, there is no cropping. If 1, it's fully cropped. */
    RectF crop_;
    /**
     * @brief The amount of fading involved.
     *
     * If 0, there is no fade. If 1, it's fully faded. */
    RectF fade_;
    /**
     * @brief Four values making up the diffuse in this TweenState.
     *
     * 0 = UpperLeft, 1 = UpperRight, 2 = LowerLeft, 3 = LowerRight */
    RageColor diffuse_[NUM_DIFFUSE_COLORS];
    /** @brief The glow color for this TweenState. */
    RageColor glow_;
    /** @brief A magical value that nobody really knows the use for. ;) */
    float aux_;
  };

  // PartiallyOpaque broken out of Draw for reuse and clarity.
  bool PartiallyOpaque();
  /**
   * @brief Calls multiple functions for drawing the Actors.
   *
   * It calls the following in order:
   * -# EarlyAbortDraw
   * -# BeginDraw
   * -# DrawPrimitives
   * -# EndDraw
   */
  void Draw();
  /**
   * @brief Allow the Actor to be aborted early.
   *
   * Subclasses may wish to overwrite this to allow for
   * aborted actors.
   * @return false, as by default Actors shouldn't be aborted on drawing. */
  virtual bool EarlyAbortDraw() const { return false; }
  /** @brief Calculate values that may be needed  for drawing. */
  virtual void PreDraw();
  /** @brief Reset internal diffuse and glow. */
  virtual void PostDraw();
  /** @brief Start the drawing and push the transform on the world matrix stack.
   */
  virtual void BeginDraw();
  /**
   * @brief Set the global rendering states of this Actor.
   *
   * This should be called at the beginning of an Actor's DrawPrimitives() call.
   */
  virtual void SetGlobalRenderStates();
  /**
   * @brief Set the texture rendering states of this Actor.
   *
   * This should be called after setting a texture for the Actor. */
  virtual void SetTextureRenderStates();
  /**
   * @brief Draw the primitives of the Actor.
   *
   * Derivative classes should override this function. */
  virtual void DrawPrimitives() {};
  /** @brief Pop the transform from the world matrix stack. */
  virtual void EndDraw();

  // TODO: make Update non virtual and change all classes to override
  // UpdateInternal instead.
  bool IsFirstUpdate() const;
  virtual void Update(
      float delta_time);  // this can short circuit UpdateInternal
  virtual void UpdateInternal(float delta_time);  // override this
  void UpdateTweening(float delta_time);
  void CalcPercentThroughTween();
  // These next functions should all be overridden by a derived class that has
  // its own tweening states to handle.
  virtual void SetCurrentTweenStart() {}
  virtual void EraseHeadTween() {}
  virtual void UpdatePercentThroughTween(float percent_through_tween) {}
  bool get_tween_uses_effect_delta() { return tween_uses_effect_delta_; }
  void set_tween_uses_effect_delta(bool t) { tween_uses_effect_delta_ = t; }

  /**
   * @brief Retrieve the Actor's name.
   * @return the Actor's name. */
  const std::string& GetName() const { return name_; }

  // IsAlias checks first if the Actor's alias isn't empty, and if it isn't,
  // does a simple compare against the provided name.
  bool IsAlias(const std::string& name);
  /**
   * @brief Set the Actor's name to a new one.
   * @param sName the new name for the Actor. */
  virtual void SetName(const std::string& name) { name_ = name; }
  virtual void SetAlias(const std::string alias) { alias_ = alias; }
  /**
   * @brief Give this Actor a new parent.
   * @param pParent the new parent Actor. */
  void SetParent(Actor* parent);
  /**
   * @brief Retrieve the Actor's parent.
   * @return the Actor's parent. */
  Actor* GetParent() { return parent_; }
  /**
   * @brief Retrieve the Actor's lineage.
   * @return the Actor's lineage. */
  std::string GetLineage() const;

  void SetFakeParent(Actor* mailman) { fake_parent_ = mailman; }
  Actor* GetFakeParent() { return fake_parent_; }

  void AddWrapperState();
  void RemoveWrapperState(size_t i);
  Actor* GetWrapperState(size_t i);
  size_t GetNumWrapperStates() const { return wrapper_states_.size(); }

  /**
   * @brief Retrieve the Actor's x position.
   * @return the Actor's x position. */
  float GetX() const { return current_.pos_.x_; };
  /**
   * @brief Retrieve the Actor's y position.
   * @return the Actor's y position. */
  float GetY() const { return current_.pos_.y_; };
  /**
   * @brief Retrieve the Actor's z position.
   * @return the Actor's z position. */
  float GetZ() const { return current_.pos_.z_; };
  float GetDestX() const { return DestTweenState().pos_.x_; };
  float GetDestY() const { return DestTweenState().pos_.y_; };
  float GetDestZ() const { return DestTweenState().pos_.z_; };
  void SetX(float x) { DestTweenState().pos_.x_ = x; };
  void SetY(float y) { DestTweenState().pos_.y_ = y; };
  void SetZ(float z) { DestTweenState().pos_.z_ = z; };
  void SetXY(float x, float y) {
    DestTweenState().pos_.x_ = x;
    DestTweenState().pos_.y_ = y;
  };
  /**
   * @brief Add to the x position of this Actor.
   * @param x the amount to add to the Actor's x position. */
  void AddX(float x) { SetX(GetDestX() + x); }
  /**
   * @brief Add to the y position of this Actor.
   * @param y the amount to add to the Actor's y position. */
  void AddY(float y) { SetY(GetDestY() + y); }
  /**
   * @brief Add to the z position of this Actor.
   * @param z the amount to add to the Actor's z position. */
  void AddZ(float z) { SetZ(GetDestZ() + z); }

  // height and width vary depending on zoom
  float GetUnzoomedWidth() const { return size_.x_; }
  float GetUnzoomedHeight() const { return size_.y_; }
  float GetZoomedWidth() const {
    return size_.x_ * base_scale_.x_ * DestTweenState().scale_.x_;
  }
  float GetZoomedHeight() const {
    return size_.y_ * base_scale_.y_ * DestTweenState().scale_.y_;
  }
  void SetWidth(float width) { size_.x_ = width; }
  void SetHeight(float height) { size_.y_ = height; }

  // Base values
  float GetBaseZoomX() const { return base_scale_.x_; }
  void SetBaseZoomX(float zoom) { base_scale_.x_ = zoom; }
  float GetBaseZoomY() const { return base_scale_.y_; }
  void SetBaseZoomY(float zoom) { base_scale_.y_ = zoom; }
  float GetBaseZoomZ() const { return base_scale_.z_; }
  void SetBaseZoomZ(float zoom) { base_scale_.z_ = zoom; }
  void SetBaseZoom(float zoom) { base_scale_ = RageVector3(zoom, zoom, zoom); }
  void SetBaseRotationX(float rot) { base_rotation_.x_ = rot; }
  void SetBaseRotationY(float rot) { base_rotation_.y_ = rot; }
  void SetBaseRotationZ(float rot) { base_rotation_.z_ = rot; }
  virtual void SetBaseAlpha(float alpha) { base_alpha_ = alpha; }
  void SetInternalDiffuse(const RageColor& c) { internal_diffuse_ = c; }
  void SetInternalGlow(const RageColor& c) { internal_glow_ = c; }

  /**
   * @brief Retrieve the general zoom factor, using the x coordinate of the
   * Actor.
   *
   * Note that this is not accurate in some cases.
   * @return the zoom factor for the x coordinate of the Actor. */
  float GetZoom() const { return DestTweenState().scale_.x_; }
  /**
   * @brief Retrieve the zoom factor for the x coordinate of the Actor.
   * @return the zoom factor for the x coordinate of the Actor. */
  float GetZoomX() const { return DestTweenState().scale_.x_; }
  /**
   * @brief Retrieve the zoom factor for the y coordinate of the Actor.
   * @return the zoom factor for the y coordinate of the Actor. */
  float GetZoomY() const { return DestTweenState().scale_.y_; }
  /**
   * @brief Retrieve the zoom factor for the z coordinate of the Actor.
   * @return the zoom factor for the z coordinate of the Actor. */
  float GetZoomZ() const { return DestTweenState().scale_.z_; }
  /**
   * @brief Set the zoom factor for all dimensions of the Actor.
   * @param zoom the zoom factor for all dimensions. */
  void SetZoom(float zoom) {
    DestTweenState().scale_.x_ = zoom;
    DestTweenState().scale_.y_ = zoom;
    DestTweenState().scale_.z_ = zoom;
  }
  /**
   * @brief Set the zoom factor for the x dimension of the Actor.
   * @param zoom the zoom factor for the x dimension. */
  void SetZoomX(float zoom) { DestTweenState().scale_.x_ = zoom; }
  /**
   * @brief Set the zoom factor for the y dimension of the Actor.
   * @param zoom the zoom factor for the y dimension. */
  void SetZoomY(float zoom) { DestTweenState().scale_.y_ = zoom; }
  /**
   * @brief Set the zoom factor for the z dimension of the Actor.
   * @param zoom the zoom factor for the z dimension. */
  void SetZoomZ(float zoom) { DestTweenState().scale_.z_ = zoom; }
  void ZoomTo(float x, float y) {
    ZoomToWidth(x);
    ZoomToHeight(y);
  }
  void ZoomToWidth(float zoom) { SetZoomX(zoom / GetUnzoomedWidth()); }
  void ZoomToHeight(float zoom) { SetZoomY(zoom / GetUnzoomedHeight()); }

  float GetRotationX() const { return DestTweenState().rotation_.x_; }
  float GetRotationY() const { return DestTweenState().rotation_.y_; }
  float GetRotationZ() const { return DestTweenState().rotation_.z_; }
  void SetRotationX(float rot) { DestTweenState().rotation_.x_ = rot; }
  void SetRotationY(float rot) { DestTweenState().rotation_.y_ = rot; }
  void SetRotationZ(float rot) { DestTweenState().rotation_.z_ = rot; }
  // added in StepNXA, now available in sm-ssc:
  void AddRotationX(float rot) { DestTweenState().rotation_.x_ += rot; };
  void AddRotationY(float rot) { DestTweenState().rotation_.y_ += rot; };
  void AddRotationZ(float rot) { DestTweenState().rotation_.z_ += rot; };
  // and these were normally in SM:
  void AddRotationH(float rot);
  void AddRotationP(float rot);
  void AddRotationR(float rot);

  void SetSkewX(float amount) { DestTweenState().skew_x_ = amount; }
  void SetSkewY(float amount) { DestTweenState().skew_y_ = amount; }
  void SetCropLeft(float percent) { DestTweenState().crop_.left_ = percent; }
  void SetCropTop(float percent) { DestTweenState().crop_.top_ = percent; }
  void SetCropRight(float percent) { DestTweenState().crop_.right_ = percent; }
  void SetCropBottom(float percent) {
    DestTweenState().crop_.bottom_ = percent;
  }

  void SetFadeLeft(float percent) { DestTweenState().fade_.left_ = percent; }
  void SetFadeTop(float percent) { DestTweenState().fade_.top_ = percent; }
  void SetFadeRight(float percent) { DestTweenState().fade_.right_ = percent; }
  void SetFadeBottom(float percent) {
    DestTweenState().fade_.bottom_ = percent;
  }

  void SetGlobalDiffuseColor(RageColor c);

  virtual void SetDiffuse(RageColor c) {
    for (int i = 0; i < NUM_DIFFUSE_COLORS; i++) {
      DestTweenState().diffuse_[i] = c;
    }
  };
  virtual void SetDiffuseAlpha(float f) {
    for (int i = 0; i < NUM_DIFFUSE_COLORS; ++i) {
      RageColor c = GetDiffuses(i);
      c.a_ = f;
      SetDiffuses(i, c);
    }
  }
  float GetCurrentDiffuseAlpha() const { return current_.diffuse_[0].a_; }
  void SetDiffuseColor(RageColor c);
  void SetDiffuses(int i, RageColor c) { DestTweenState().diffuse_[i] = c; };
  void SetDiffuseUpperLeft(RageColor c) { DestTweenState().diffuse_[0] = c; };
  void SetDiffuseUpperRight(RageColor c) { DestTweenState().diffuse_[1] = c; };
  void SetDiffuseLowerLeft(RageColor c) { DestTweenState().diffuse_[2] = c; };
  void SetDiffuseLowerRight(RageColor c) { DestTweenState().diffuse_[3] = c; };
  void SetDiffuseTopEdge(RageColor c) {
    DestTweenState().diffuse_[0] = DestTweenState().diffuse_[1] = c;
  };
  void SetDiffuseRightEdge(RageColor c) {
    DestTweenState().diffuse_[1] = DestTweenState().diffuse_[3] = c;
  };
  void SetDiffuseBottomEdge(RageColor c) {
    DestTweenState().diffuse_[2] = DestTweenState().diffuse_[3] = c;
  };
  void SetDiffuseLeftEdge(RageColor c) {
    DestTweenState().diffuse_[0] = DestTweenState().diffuse_[2] = c;
  };
  RageColor GetDiffuse() const { return DestTweenState().diffuse_[0]; };
  RageColor GetDiffuses(int i) const { return DestTweenState().diffuse_[i]; };
  float GetDiffuseAlpha() const { return DestTweenState().diffuse_[0].a_; };
  void SetGlow(RageColor c) { DestTweenState().glow_ = c; };
  RageColor GetGlow() const { return DestTweenState().glow_; };

  void SetAux(float f) { DestTweenState().aux_ = f; }
  float GetAux() const { return current_.aux_; }

  virtual void BeginTweening(float time, ITween* interp);
  virtual void BeginTweening(float time, TweenType tt = TWEEN_LINEAR);
  virtual void StopTweening();
  void Sleep(float time);
  void QueueCommand(const std::string& command_name);
  void QueueMessage(const std::string& message_name);
  virtual void FinishTweening();
  virtual void HurryTweening(float factor);
  // Let ActorFrame and BGAnimation override
  virtual float GetTweenTimeLeft()
      const;                    // Amount of time until all tweens have stopped
  TweenState& DestTweenState()  // where Actor will end when its tween finish
  {
    if (tweens_.empty()) {  // not tweening
      return current_;
    } else {
      return tweens_.back()->state_;
    }
  }
  const TweenState& DestTweenState() const {
    return const_cast<Actor*>(this)->DestTweenState();
  }

  /** @brief How do we handle stretching the Actor? */
  enum class StretchType {
    kFitInside, /**< Have the Actor fit inside its parent, using the smaller
                   zoom. */
    kCover      /**< Have the Actor cover its parent, using the larger zoom. */
  };

  void ScaleToCover(const RectF& rect) { ScaleTo(rect, StretchType::kCover); }
  void ScaleToFitInside(const RectF& rect) {
    ScaleTo(rect, StretchType::kFitInside);
  };
  void ScaleTo(const RectF& rect, StretchType st);

  void StretchTo(const RectF& rect);

  // Alignment settings.  These need to be virtual for BitmapText
  virtual void SetHorizAlign(float f) { horiz_align_ = f; }
  virtual void SetVertAlign(float f) { vert_align_ = f; }
  void SetHorizAlign(HorizAlign ha) {
    SetHorizAlign(
        (ha == HorizAlign_Left)     ? 0.0f
        : (ha == HorizAlign_Center) ? 0.5f
                                    : +1.0f);
  }
  void SetVertAlign(VertAlign va) {
    SetVertAlign(
        (va == VertAlign_Top)      ? 0.0f
        : (va == VertAlign_Middle) ? 0.5f
                                   : +1.0f);
  }
  virtual float GetHorizAlign() { return horiz_align_; }
  virtual float GetVertAlign() { return vert_align_; }

  // effects
  void StopEffect() { effect_ = no_effect; }
  Effect GetEffect() const { return effect_; }
  float GetSecsIntoEffect() const { return secs_into_effect_; }
  float GetEffectDelta() const { return effect_delta_; }

  void SetEffectColor1(RageColor c) { effect_color1_ = c; }
  void SetEffectColor2(RageColor c) { effect_color2_ = c; }
  void RecalcEffectPeriod();
  void SetEffectPeriod(float time);
  float GetEffectPeriod() const { return effect_period_; }
  bool SetEffectTiming(
      float ramp_toh, float at_half, float ramp_tof, float at_zero,
      float at_full, std::string& err);
  bool SetEffectHoldAtFull(float haf, std::string& err);
  void SetEffectOffset(float time) { effect_offset_ = time; }
  void SetEffectClock(EffectClock c) { effect_clock_ = c; }
  void SetEffectClockString(const std::string& s);  // convenience

  void SetEffectMagnitude(RageVector3 vec) { effect_magnitude_ = vec; }
  RageVector3 GetEffectMagnitude() const { return effect_magnitude_; }

  void ResetEffectTimeIfDifferent(Effect new_effect);
  void SetEffectDiffuseBlink(
      float effect_period_seconds, RageColor c1, RageColor c2);
  void SetEffectDiffuseShift(
      float effect_period_seconds, RageColor c1, RageColor c2);
  void SetEffectDiffuseRamp(
      float effect_period_seconds, RageColor c1, RageColor c2);
  void SetEffectGlowBlink(
      float effect_period_seconds, RageColor c1, RageColor c2);
  void SetEffectGlowShift(
      float effect_period_seconds, RageColor c1, RageColor c2);
  void SetEffectGlowRamp(
      float effect_period_seconds, RageColor c1, RageColor c2);
  void SetEffectRainbow(float effect_period_seconds);
  void SetEffectWag(float period, RageVector3 vect);
  void SetEffectBounce(float period, RageVector3 vect);
  void SetEffectBob(float period, RageVector3 vect);
  void SetEffectPulse(float period, float min_zoom, float max_zoom);
  void SetEffectSpin(RageVector3 vect);
  void SetEffectVibrate(RageVector3 vect);

  // other properties
  /**
   * @brief Determine if the Actor is visible at this time.
   * @return true if it's visible, false otherwise. */
  bool GetVisible() const { return visible_; }
  void SetVisible(bool b) { visible_ = b; }
  void SetShadowLength(float length) {
    shadow_length_x_ = length;
    shadow_length_y_ = length;
  }
  void SetShadowLengthX(float length_x) { shadow_length_x_ = length_x; }
  void SetShadowLengthY(float length_y) { shadow_length_y_ = length_y; }
  void SetShadowColor(RageColor c) { shadow_color_ = c; }
  // TODO: Implement hibernate as a tween type?
  void SetHibernate(float secs) { hibernate_seconds_left_ = secs; }
  void SetDrawOrder(int order) { draw_order_ = order; }
  int GetDrawOrder() const { return draw_order_; }

  virtual void EnableAnimation(bool b) {
    is_animating_ = b;
  }  // Sprite needs to overload this
  void StopAnimating() { this->EnableAnimation(false); }

  // render states
  void SetBlendMode(BlendMode mode) { blend_mode_ = mode; }
  void SetTextureTranslate(float x, float y) {
    tex_translate_.x_ = x;
    tex_translate_.y_ = y;
  }
  void SetTextureWrapping(bool b) { texture_wrapping_ = b; }
  void SetTextureFiltering(bool b) { texture_filtering_ = b; }
  void SetClearZBuffer(bool b) { clear_z_buffer_ = b; }
  void SetUseZBuffer(bool b) {
    SetZTestMode(b ? ZTEST_WRITE_ON_PASS : ZTEST_OFF);
    SetZWrite(b);
  }
  virtual void SetZTestMode(ZTestMode mode) { z_test_mode_ = mode; }
  virtual void SetZWrite(bool b) { z_write_ = b; }
  void SetZBias(float f) { z_bias_ = f; }
  virtual void SetCullMode(CullMode mode) { cull_mode_ = mode; }

  // Lua
  virtual void PushSelf(lua_State* L);
  virtual void PushContext(lua_State* L);

  // Named commands
  void AddCommand(
      const std::string& cmd_name, apActorCommands apac, bool warn = true);
  bool HasCommand(const std::string& cmd_name) const;
  const apActorCommands* GetCommand(const std::string& command_name) const;
  void PlayCommand(const std::string& command_name) {
    HandleMessage(Message(command_name));
  }  // convenience
  void PlayCommandNoRecurse(const Message& msg);

  // Commands by reference
  virtual void RunCommands(
      const LuaReference& cmds, const LuaReference* param_table = nullptr);
  void RunCommands(
      const apActorCommands& cmds, const LuaReference* param_table = nullptr) {
    this->RunCommands(*cmds, param_table);
  }  // convenience
  virtual void RunCommandsRecursively(
      const LuaReference& cmds, const LuaReference* param_table = nullptr) {
    RunCommands(cmds, param_table);
  }
  // If we're a leaf, then execute this command.
  virtual void RunCommandsOnLeaves(
      const LuaReference& cmds, const LuaReference* param_table = nullptr) {
    RunCommands(cmds, param_table);
  }

  // Messages
  virtual void HandleMessage(const Message& msg);

  // Animation
  virtual int GetNumStates() const { return 1; }
  virtual void SetState(int /* iNewState */) {}
  virtual float GetAnimationLengthSeconds() const { return 0; }
  virtual void SetSecondsIntoAnimation(float) {}
  virtual void SetUpdateRate(float) {}
  virtual float GetUpdateRate() { return 1.0f; }

  // Use this to enable/disable scaling an actor's rate with tab or tilde.
  void SetRateScalingEnabled(bool b) { rate_scaling_enabled_ = b; }
  bool GetRateScalingEnabled() { return rate_scaling_enabled_; }

  HiddenPtr<LuaClass> lua_instance_;

 protected:
  /** @brief the name of the Actor. */
  std::string name_;

  // Alias is an alternate name for the actor. The name of the actor is
  // allowed to be empty, whereas if the alias is empty, it is ignored. The
  // point of the alias is to help maintain backward compatability with lua
  // in case an actor name changes
  std::string alias_;

  /** @brief the current parent of this Actor if it exists. */
  Actor* parent_;
  // m_FakeParent exists to provide a way to render the actor inside another's
  // state without making that actor the parent.  It's like having multiple
  // parents. -Kyz
  Actor* fake_parent_;
  // WrapperStates provides a way to wrap the actor inside ActorFrames,
  // applicable to any actor, not just ones the theme creates.
  std::vector<Actor*> wrapper_states_;

  /** @brief Some general information about the Tween. */
  struct TweenInfo {
    // counters for tweening
    TweenInfo();
    ~TweenInfo();
    TweenInfo(const TweenInfo& cpy);
    TweenInfo& operator=(const TweenInfo& rhs);

    ITween* tween_;
    /** @brief How far into the tween are we? */
    float time_left_in_tween_;
    /** @brief The number of seconds between Start and End positions/zooms. */
    float tween_time_;
    /** @brief The command to execute when this TweenState goes into effect. */
    std::string command_name_;
  };

  RageVector3 base_rotation_;
  RageVector3 base_scale_;
  float base_alpha_;
  RageColor internal_diffuse_;
  RageColor internal_glow_;

  RageVector2 size_;
  TweenState current_;
  TweenState start_;
  TweenState current_with_effects_;
  struct TweenStateAndInfo {
    TweenState state_;
    TweenInfo info_;
  };
  std::vector<TweenStateAndInfo*> tweens_;

  /** @brief Temporary variables that are filled just before drawing */
  TweenState* temp_state_;

  bool first_update_;

  // Stuff for alignment
  /** @brief The particular horizontal alignment.
   *
   * Use the defined constant values for best effect. */
  float horiz_align_;
  /** @brief The particular vertical alignment.
   *
   * Use the defined constant values for best effect. */
  float vert_align_;

  // Stuff for effects
  Effect effect_;
  float secs_into_effect_;
  float effect_delta_;

  // units depend on m_EffectClock
  float effect_ramp_to_half_;
  float effect_hold_at_half_;
  float effect_ramp_to_full_;
  float effect_hold_at_full_;
  float effect_hold_at_zero_;
  float effect_offset_;
  // Anything changing ramp_up, hold_at_half, ramp_down, or hold_at_zero must
  // also update the period so the period is only calculated when changed.
  // -Kyz
  float effect_period_;
  EffectClock effect_clock_;
  bool tween_uses_effect_delta_;

  /* This can be used in lieu of the fDeltaTime parameter to Update() to
   * follow the effect clock.  Actor::Update must be called first. */
  float GetEffectDeltaTime() const { return effect_delta_; }

  RageColor effect_color1_;
  RageColor effect_color2_;
  RageVector3 effect_magnitude_;

  // other properties
  bool visible_;
  bool is_animating_;
  float hibernate_seconds_left_;
  float shadow_length_x_;
  float shadow_length_y_;
  RageColor shadow_color_;
  /** @brief The draw order priority.
   *
   * The lower this number is, the sooner it is drawn. */
  int draw_order_;

  // render states
  BlendMode blend_mode_;
  ZTestMode z_test_mode_;
  CullMode cull_mode_;
  RageVector2 tex_translate_;
  bool texture_wrapping_;
  bool texture_filtering_;
  bool clear_z_buffer_;
  bool z_write_;
  /**
   * @brief The amount of bias.
   *
   * If 0, there is no bias. If 1, there is a full bias. */
  float z_bias_;

  // global state
  static float current_bgm_time_, current_bgm_beat_;
  static float current_bgm_time_no_offset_, current_bgm_beat_no_offset_;
  static std::vector<float> current_bgm_beat_player_;
  static std::vector<float> current_bgm_beat_player_no_offset_;

 private:
  // Some actors shouldn't be scaled by the engine i.e. using with tab for
  // speeding up or tilde for slowing down.
  // In the case both are pressed (setting rate to 0), this bool does nothing.
  bool rate_scaling_enabled_;

  // commands
  std::map<std::string, apActorCommands> map_name_to_commands_;
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
