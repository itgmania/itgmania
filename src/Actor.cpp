#include "Actor.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <string>
#include <typeinfo>
#include <vector>

#include "ActorFrame.h"
#include "ActorUtil.h"
#include "EnumHelper.h"
#include "GameLoop.h"
#include "LightsManager.h"  // for NUM_CabinetLight
#include "LuaBinding.h"
#include "LuaManager.h"
#include "LuaReference.h"
#include "MessageManager.h"
#include "PlayerNumber.h"
#include "Preference.h"
#include "RageDisplay.h"
#include "RageMath.h"
#include "RageTimer.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "RageUtil/RandomNumbers.h"
#include "RageUtil_AutoPtr.h"
#include "StdString.h"
#include "Tween.h"
#include "XmlFile.h"
#include "global.h"

static Preference<bool> show_masks("ShowMasks", false);
static const float kDefaultEffectPeriod = 1.0f;

/**
 * @brief Set up a hidden Actor that won't be drawn.
 *
 * It's useful to be able to construct a basic Actor in XML, in
 * order to simply delay a Transition, or receive and send broadcasts.
 * Since these actors will never draw, set them hidden by default. */
class HiddenActor : public Actor {
 public:
  HiddenActor() { SetVisible(false); }
  virtual HiddenActor* Copy() const;
};
REGISTER_ACTOR_CLASS_WITH_NAME(HiddenActor, Actor);

float Actor::current_bgm_time_ = 0, Actor::current_bgm_beat_;
float Actor::current_bgm_time_no_offset_ = 0,
      Actor::current_bgm_beat_no_offset_ = 0;
std::vector<float> Actor::current_bgm_beat_player_(NUM_PlayerNumber, 0);
std::vector<float> Actor::current_bgm_beat_player_no_offset_(
    NUM_PlayerNumber, 0);

Actor* Actor::Copy() const { return new Actor(*this); }

static float g_cabinet_lights[NUM_CabinetLight];

static const char* HorizAlignNames[] = {"Left", "Center", "Right"};
XToString(HorizAlign);
LuaXType(HorizAlign);

static const char* VertAlignNames[] = {"Top", "Middle", "Bottom"};
XToString(VertAlign);
LuaXType(VertAlign);

void Actor::SetBGMTime(
    float time, float beat, float time_no_offset, float beat_no_offset) {
  current_bgm_time_ = time;
  current_bgm_beat_ = beat;

  /* This timer is generally only used for effects tied to the background music
   * when GameSoundManager is aligning music beats.  Alignment doesn't handle
   * g_fVisualDelaySeconds. */
  current_bgm_time_no_offset_ = time_no_offset;
  current_bgm_beat_no_offset_ = beat_no_offset;
}

void Actor::SetPlayerBGMBeat(
    PlayerNumber pn, float beat, float beat_no_offset) {
  current_bgm_beat_player_[pn] = beat;
  current_bgm_beat_player_no_offset_[pn] = beat_no_offset;
}

void Actor::SetBGMLight(int light_number, float cabinet_lights) {
  ASSERT(light_number < NUM_CabinetLight);
  g_cabinet_lights[light_number] = cabinet_lights;
}

void Actor::InitState() {
  this->StopTweening();

  temp_state_ = nullptr;

  base_rotation_ = RageVector3(0, 0, 0);
  base_scale_ = RageVector3(1, 1, 1);
  base_alpha_ = 1;
  internal_diffuse_ = RageColor(1, 1, 1, 1);
  internal_glow_ = RageColor(0, 0, 0, 0);

  start_.Init();
  current_.Init();

  horiz_align_ = 0.5f;
  vert_align_ = 0.5f;
  effect_ = no_effect;
  secs_into_effect_ = 0;
  effect_delta_ = 0;
  SetEffectPeriod(kDefaultEffectPeriod);
  effect_offset_ = 0;
  effect_clock_ = CLOCK_TIMER;
  effect_magnitude_ = RageVector3(0, 0, 10);
  effect_color1_ = RageColor(1, 1, 1, 1);
  effect_color2_ = RageColor(1, 1, 1, 1);

  visible_ = true;
  shadow_length_x_ = 0;
  shadow_length_y_ = 0;
  shadow_color_ = RageColor(0, 0, 0, 0.5f);
  is_animating_ = true;
  hibernate_seconds_left_ = 0;
  draw_order_ = 0;

  texture_wrapping_ = false;
  texture_filtering_ = true;
  tex_translate_ = RageVector2(0, 0);

  blend_mode_ = BLEND_NORMAL;
  z_bias_ = 0;
  clear_z_buffer_ = false;
  z_test_mode_ = ZTEST_OFF;
  z_write_ = false;
  cull_mode_ = CULL_NONE;
}

static bool GetMessageNameFromCommandName(
    const std::string& command_name, std::string& message_name_out) {
  if (Right(command_name, 7) == "Message") {
    message_name_out = Left(command_name, command_name.size() - 7);
    return true;
  } else {
    return false;
  }
}

Actor::Actor() {
  lua_instance_ = new LuaClass;
  Lua* L = LUA->Get();
  lua_instance_->PushSelf(L);
  lua_newtable(L);
  lua_pushvalue(L, -1);
  lua_setmetatable(L, -2);
  lua_setfield(L, -2, "ctx");
  lua_pop(L, 1);
  LUA->Release(L);

  size_ = RageVector2(1, 1);
  InitState();
  parent_ = nullptr;
  fake_parent_ = nullptr;
  first_update_ = true;
  tween_uses_effect_delta_ = false;
  rate_scaling_enabled_ = true;
}

Actor::~Actor() {
  StopTweening();
  UnsubscribeAll();
  for (size_t i = 0; i < wrapper_states_.size(); ++i) {
    RageUtil::SafeDelete(wrapper_states_[i]);
  }
  wrapper_states_.clear();
}

Actor::Actor(const Actor& cpy) : MessageSubscriber(cpy) {
  /* Don't copy an Actor in the middle of rendering. */
  ASSERT(cpy.temp_state_ == nullptr);
  temp_state_ = nullptr;

#define CPY(x) x = cpy.x
  CPY(name_);
  CPY(alias_);
  CPY(parent_);
  CPY(fake_parent_);
  CPY(lua_instance_);

  wrapper_states_.resize(cpy.wrapper_states_.size());
  for (size_t i = 0; i < wrapper_states_.size(); ++i) {
    wrapper_states_[i] =
        new ActorFrame(*dynamic_cast<ActorFrame*>(cpy.wrapper_states_[i]));
  }

  CPY(base_rotation_);
  CPY(base_scale_);
  CPY(base_alpha_);
  CPY(internal_diffuse_);
  CPY(internal_glow_);

  CPY(size_);
  CPY(current_);
  CPY(start_);
  for (unsigned i = 0; i < cpy.tweens_.size(); ++i) {
    tweens_.push_back(new TweenStateAndInfo(*cpy.tweens_[i]));
  }

  CPY(first_update_);

  CPY(horiz_align_);
  CPY(vert_align_);
  CPY(effect_);
  CPY(secs_into_effect_);
  CPY(effect_delta_);
  CPY(effect_ramp_to_half_);
  CPY(effect_hold_at_half_);
  CPY(effect_ramp_to_full_);
  CPY(effect_hold_at_full_);
  CPY(effect_hold_at_zero_);
  CPY(effect_period_);
  CPY(effect_offset_);
  CPY(effect_clock_);
  CPY(tween_uses_effect_delta_);

  CPY(effect_color1_);
  CPY(effect_color2_);
  CPY(effect_magnitude_);

  CPY(visible_);
  CPY(hibernate_seconds_left_);
  CPY(shadow_length_x_);
  CPY(shadow_length_y_);
  CPY(shadow_color_);
  CPY(is_animating_);
  CPY(draw_order_);

  CPY(texture_wrapping_);
  CPY(texture_filtering_);
  CPY(blend_mode_);
  CPY(clear_z_buffer_);
  CPY(z_test_mode_);
  CPY(z_write_);
  CPY(z_bias_);
  CPY(cull_mode_);

  CPY(rate_scaling_enabled_);
  CPY(map_name_to_commands_);

  CPY(tween_uses_effect_delta_);
  CPY(rate_scaling_enabled_);
#undef CPY
}

Actor& Actor::operator=(Actor other) {
  /* Don't copy an Actor in the middle of rendering. */
  ASSERT(other.temp_state_ == nullptr);
  temp_state_ = nullptr;

#define SWAP(x) std::swap(x, other.x)
  SWAP(name_);
  SWAP(alias_);
  SWAP(parent_);
  SWAP(fake_parent_);
  SWAP(lua_instance_);

  SWAP(wrapper_states_);

  SWAP(base_rotation_);
  SWAP(base_scale_);
  SWAP(base_alpha_);
  SWAP(internal_diffuse_);
  SWAP(internal_glow_);

  SWAP(size_);
  SWAP(current_);
  SWAP(start_);
  SWAP(tweens_);

  SWAP(first_update_);

  SWAP(horiz_align_);
  SWAP(vert_align_);
  SWAP(effect_);
  SWAP(secs_into_effect_);
  SWAP(effect_delta_);
  SWAP(effect_ramp_to_half_);
  SWAP(effect_hold_at_half_);
  SWAP(effect_ramp_to_full_);
  SWAP(effect_hold_at_full_);
  SWAP(effect_hold_at_zero_);
  SWAP(effect_period_);
  SWAP(effect_offset_);
  SWAP(effect_clock_);

  SWAP(effect_color1_);
  SWAP(effect_color2_);
  SWAP(effect_magnitude_);

  SWAP(visible_);
  SWAP(hibernate_seconds_left_);
  SWAP(shadow_length_x_);
  SWAP(shadow_length_y_);
  SWAP(shadow_color_);
  SWAP(is_animating_);
  SWAP(draw_order_);

  SWAP(texture_wrapping_);
  SWAP(texture_filtering_);
  SWAP(blend_mode_);
  SWAP(clear_z_buffer_);
  SWAP(z_test_mode_);
  SWAP(z_write_);
  SWAP(z_bias_);
  SWAP(cull_mode_);

  SWAP(map_name_to_commands_);

  SWAP(tween_uses_effect_delta_);
  SWAP(rate_scaling_enabled_);
#undef SWAP
  return *this;
}

bool Actor::IsAlias(const std::string& name) {
  if (alias_.empty()) {
    return false;
  }
  return name == alias_;
}

/* XXX: This calls InitCommand, which must happen after all other
 * initialization (eg. ActorFrame loading children).  However, it
 * also loads input variables, which should happen first.  The
 * former is more important. */
void Actor::LoadFromNode(const XNode* node) {
  Lua* L = LUA->Get();
  FOREACH_CONST_Attr(node, attr) {
    // Load Name, if any.
    const std::string& key_name = attr->first;
    const XNodeValue* value = attr->second;
    if (EndsWith(key_name, "Command")) {
      LuaReference* ref = new LuaReference;
      value->PushValue(L);
      ref->SetFromStack(L);
      std::string cmd_name = Left(key_name, key_name.size() - 7);
      AddCommand(cmd_name, apActorCommands(ref));
    } else if (key_name == "Name") {
      SetName(value->GetValue<std::string>());
    } else if (key_name == "BaseRotationX") {
      SetBaseRotationX(value->GetValue<float>());
    } else if (key_name == "BaseRotationY") {
      SetBaseRotationY(value->GetValue<float>());
    } else if (key_name == "BaseRotationZ") {
      SetBaseRotationZ(value->GetValue<float>());
    } else if (key_name == "BaseZoomX") {
      SetBaseZoomX(value->GetValue<float>());
    } else if (key_name == "BaseZoomY") {
      SetBaseZoomY(value->GetValue<float>());
    } else if (key_name == "BaseZoomZ") {
      SetBaseZoomZ(value->GetValue<float>());
    }
  }

  LUA->Release(L);

  // Don't recurse Init.  It gets called once for every Actor when the
  // Actor is loaded, and we don't want to call it again.
  PlayCommandNoRecurse(Message("Init"));
}

bool Actor::PartiallyOpaque() {
  return temp_state_->diffuse_[0].a_ > 0 || temp_state_->diffuse_[1].a_ > 0 ||
         temp_state_->diffuse_[2].a_ > 0 || temp_state_->diffuse_[3].a_ > 0 ||
         temp_state_->glow_.a_ > 0;
}

void Actor::Draw() {
  if (!visible_ || hibernate_seconds_left_ > 0 || this->EarlyAbortDraw()) {
    return;  // early abort
  }
  if (fake_parent_) {
    if (!fake_parent_->visible_ || fake_parent_->hibernate_seconds_left_ > 0 ||
        fake_parent_->EarlyAbortDraw()) {
      return;
    }
    fake_parent_->PreDraw();
    if (!fake_parent_->PartiallyOpaque()) {
      fake_parent_->PostDraw();
      return;
    }
  }

  if (fake_parent_) {
    fake_parent_->BeginDraw();
  }
  size_t wrapper_states_used = 0;
  RageColor last_diffuse;
  RageColor last_glow;
  bool use_last_diffuse = false;
  // dont_abort_draw exists because if one of the layers is invisible,
  // there's no point in continuing. -Kyz
  bool dont_abort_draw = true;
  // abort_with_end_draw exists because PreDraw happens before the
  // opaqueness test, so if we abort at the opaqueness test, there isn't
  // a BeginDraw to match the EndDraw. -Kyz
  bool abort_with_end_draw = true;
  // It's more intuitive to apply the highest index wrappers first.
  // On the lua side, it looks like this:
  // wrapper[3] is the outermost frame.  wrapper[2] is inside wrapper[3].
  // wrapper[1] is inside wrapper[2].  The actor is inside wrapper[1].
  // -Kyz
  for (size_t i = wrapper_states_.size(); i > 0 && dont_abort_draw; --i) {
    Actor* state = wrapper_states_[i - 1];
    if (!state->visible_ || state->hibernate_seconds_left_ > 0 ||
        state->EarlyAbortDraw()) {
      dont_abort_draw = false;
    } else {
      state->PreDraw();
      if (state->PartiallyOpaque()) {
        state->BeginDraw();
        last_diffuse = state->temp_state_->diffuse_[0];
        last_glow = state->temp_state_->glow_;
        use_last_diffuse = true;
        if (i > 1) {
          wrapper_states_[i - 2]->SetInternalDiffuse(last_diffuse);
          wrapper_states_[i - 2]->SetInternalGlow(last_glow);
        }
      } else {
        dont_abort_draw = false;
        abort_with_end_draw = false;
      }
      ++wrapper_states_used;
    }
  }
  // call the most-derived versions
  if (dont_abort_draw) {
    if (use_last_diffuse) {
      this->SetInternalDiffuse(last_diffuse);
      this->SetInternalGlow(last_glow);
    }
    this->PreDraw();
    ASSERT(temp_state_ != nullptr);
    if (PartiallyOpaque()) {
      this->BeginDraw();
      this->DrawPrimitives();
      this->EndDraw();
    }
    this->PostDraw();
  }
  for (size_t i = 0; i < wrapper_states_used; ++i) {
    Actor* state = wrapper_states_[i];
    if (abort_with_end_draw) {
      state->EndDraw();
    }
    abort_with_end_draw = true;
    state->PostDraw();
    state->temp_state_ = nullptr;
  }

  if (fake_parent_) {
    fake_parent_->EndDraw();
    fake_parent_->PostDraw();
    fake_parent_->temp_state_ = nullptr;
  }
  temp_state_ = nullptr;
}

void Actor::PostDraw()  // reset internal diffuse and glow
{
  internal_diffuse_ = RageColor(1, 1, 1, 1);
  internal_glow_.a_ = 0;
}

void Actor::PreDraw()  // calculate actor properties
{
  // Somthing below may set m_pTempState to tempState
  temp_state_ = &current_;

  if (effect_ == no_effect) {
  } else {
    temp_state_ = &current_with_effects_;
    current_with_effects_ = current_;

    const float total_period = GetEffectPeriod();
    ASSERT(total_period > 0);
    const float time_into_effect =
        fmodfp(secs_into_effect_ + effect_offset_, total_period);

    float percent_through_effect;
    const float rup_plus_ath = effect_ramp_to_half_ + effect_hold_at_half_;
    const float rupath_plus_rdown = rup_plus_ath + effect_ramp_to_full_;
    const float rupathrdown_plus_atf = rupath_plus_rdown + effect_hold_at_full_;
    if (time_into_effect < effect_ramp_to_half_) {
      percent_through_effect =
          SCALE(time_into_effect, 0, effect_ramp_to_half_, 0.0f, 0.5f);
    } else if (time_into_effect < rup_plus_ath) {
      percent_through_effect = 0.5f;
    } else if (time_into_effect < rupath_plus_rdown) {
      percent_through_effect =
          SCALE(time_into_effect, rup_plus_ath, rupath_plus_rdown, 0.5f, 1.0f);
    } else if (time_into_effect < rupathrdown_plus_atf) {
      percent_through_effect = 1.0f;
    } else {
      percent_through_effect = 0;
    }
    ASSERT_M(
        percent_through_effect >= 0 && percent_through_effect <= 1,
        ssprintf("PercentThroughEffect: %f", percent_through_effect));

    bool blink_on = percent_through_effect > 0.5f;
    float percent_between_colors =
        std::sin((percent_through_effect + 0.25f) * 2 * kPi) / 2 + 0.5f;
    ASSERT_M(
        percent_between_colors >= 0 && percent_between_colors <= 1,
        ssprintf(
            "PercentBetweenColors: %f, PercentThroughEffect: %f",
            percent_between_colors, percent_through_effect));
    float original_alpha = current_with_effects_.diffuse_[0].a_;

    // todo: account for SSC_FUTURES -aj
    switch (effect_) {
      case diffuse_blink:
        /* XXX: Should diffuse_blink and diffuse_shift multiply the
         * m_current_with_effects color? (That would have the same effect with
         * 1,1,1,1, and allow tweening the diffuse while blinking and shifting.)
         */
        for (int i = 0; i < kNumDiffuseColors; i++) {
          current_with_effects_.diffuse_[i] =
              blink_on ? effect_color1_ : effect_color2_;
          current_with_effects_.diffuse_[i].a_ *=
              original_alpha;  // multiply the alphas so we can fade even while
                               // an effect is playing
        }
        break;
      case diffuse_shift:
        for (int i = 0; i < kNumDiffuseColors; i++) {
          current_with_effects_.diffuse_[i] =
              effect_color1_ * percent_between_colors +
              effect_color2_ * (1.0f - percent_between_colors);
          current_with_effects_.diffuse_[i].a_ *=
              original_alpha;  // multiply the alphas so we can fade even while
                               // an effect is playing
        }
        break;
      case diffuse_ramp:
        for (int i = 0; i < kNumDiffuseColors; i++) {
          current_with_effects_.diffuse_[i] =
              effect_color1_ * percent_through_effect +
              effect_color2_ * (1.0f - percent_through_effect);
          current_with_effects_.diffuse_[i].a_ *=
              original_alpha;  // multiply the alphas so we can fade even while
                               // an effect is playing
        }
        break;
      case glow_blink:
        current_with_effects_.glow_ =
            blink_on ? effect_color1_ : effect_color2_;
        current_with_effects_.glow_.a_ *=
            original_alpha;  // don't glow if the Actor is transparent!
        break;
      case glow_shift:
        current_with_effects_.glow_ =
            effect_color1_ * percent_between_colors +
            effect_color2_ * (1.0f - percent_between_colors);
        current_with_effects_.glow_.a_ *=
            original_alpha;  // don't glow if the Actor is transparent!
        break;
      case glow_ramp:
        current_with_effects_.glow_ =
            effect_color1_ * percent_through_effect +
            effect_color2_ * (1.0f - percent_through_effect);
        current_with_effects_.glow_.a_ *=
            original_alpha;  // don't glow if the Actor is transparent!
        break;
      case rainbow:
        current_with_effects_.diffuse_[0] = RageColor(
            std::cos(percent_between_colors * 2 * kPi) * 0.5f + 0.5f,
            std::cos(percent_between_colors * 2 * kPi + kPi * 2.0f / 3.0f) *
                    0.5f +
                0.5f,
            std::cos(percent_between_colors * 2 * kPi + kPi * 4.0f / 3.0f) *
                    0.5f +
                0.5f,
            original_alpha);
        for (int i = 1; i < kNumDiffuseColors; i++) {
          current_with_effects_.diffuse_[i] = current_with_effects_.diffuse_[0];
        }
        break;
      case wag:
        current_with_effects_.rotation_ +=
            effect_magnitude_ * std::sin(percent_through_effect * 2.0f * kPi);
        break;
      case spin:
        // nothing needs to be here
        break;
      case vibrate:
        current_with_effects_.pos_.x_ +=
            effect_magnitude_.x_ * randomf(-1.0f, 1.0f) * GetZoom();
        current_with_effects_.pos_.y_ +=
            effect_magnitude_.y_ * randomf(-1.0f, 1.0f) * GetZoom();
        current_with_effects_.pos_.z_ +=
            effect_magnitude_.z_ * randomf(-1.0f, 1.0f) * GetZoom();
        break;
      case bounce: {
        float percent_offset = std::sin(percent_through_effect * kPi);
        current_with_effects_.pos_ += effect_magnitude_ * percent_offset;
      } break;
      case bob: {
        float percent_offset = std::sin(percent_through_effect * kPi * 2);
        current_with_effects_.pos_ += effect_magnitude_ * percent_offset;
      } break;
      case pulse: {
        float min_zoom = effect_magnitude_[0];
        float max_zoom = effect_magnitude_[1];
        float percent_offset = std::sin(percent_through_effect * kPi);
        float zoom = SCALE(percent_offset, 0.f, 1.f, min_zoom, max_zoom);
        current_with_effects_.scale_ *= zoom;

        // Use the color as a Vector3 to scale the effect for added control
        RageColor c =
            SCALE(percent_offset, 0.f, 1.f, effect_color1_, effect_color2_);
        current_with_effects_.scale_.x_ *= c.r_;
        current_with_effects_.scale_.y_ *= c.g_;
        current_with_effects_.scale_.z_ *= c.b_;
      } break;
      default:
        FAIL_M(ssprintf("Invalid effect: %i", effect_));
    }
  }

  if (base_alpha_ != 1) {
    internal_diffuse_.a_ *= base_alpha_;
  }

  if (internal_diffuse_ != RageColor(1, 1, 1, 1)) {
    if (temp_state_ != &current_with_effects_) {
      temp_state_ = &current_with_effects_;
      current_with_effects_ = current_;
    }

    for (int i = 0; i < kNumDiffuseColors; i++) {
      current_with_effects_.diffuse_[i] *= internal_diffuse_;
    }
  }

  if (internal_glow_.a_ > 0) {
    if (temp_state_ != &current_with_effects_) {
      temp_state_ = &current_with_effects_;
      current_with_effects_ = current_;
    }

    // Blend using Screen mode
    current_with_effects_.glow_ = current_with_effects_.glow_ + internal_glow_ -
                                  internal_glow_ * current_with_effects_.glow_;
  }
}

void Actor::BeginDraw() {
  DISPLAY->PushMatrix();  // Save the current transformation matrix

  // Get the position of the actor
  const float pos_x = temp_state_->pos_.x_;
  const float pos_y = temp_state_->pos_.y_;
  const float pos_z = temp_state_->pos_.z_;

  if (pos_x != 0 || pos_y != 0 || pos_z != 0) {
    RageMatrix m;
    RageMatrixTranslate(&m, pos_x, pos_y, pos_z);
    DISPLAY->PreMultMatrix(m);
  }

  // Get the rotation of the actor
  {
    const float rotation_x = temp_state_->rotation_.x_ + base_rotation_.x_;
    const float rotation_y = temp_state_->rotation_.y_ + base_rotation_.y_;
    const float rotation_z = temp_state_->rotation_.z_ + base_rotation_.z_;

    if (rotation_x != 0 || rotation_y != 0 || rotation_z != 0) {
      RageMatrix m;
      RageMatrixRotationXYZ(&m, rotation_x, rotation_y, rotation_z);
      DISPLAY->PreMultMatrix(m);
    }
  }

  // Get the scale of the actor
  {
    const float scale_x = temp_state_->scale_.x_ * base_scale_.x_;
    const float scale_y = temp_state_->scale_.y_ * base_scale_.y_;
    const float scale_z = temp_state_->scale_.z_ * base_scale_.z_;

    if (scale_x != 1 || scale_y != 1 || scale_z != 1) {
      RageMatrix m;
      RageMatrixScale(&m, scale_x, scale_y, scale_z);
      DISPLAY->PreMultMatrix(m);
    }
  }

  // Adjust the alignment of the actor
  if (unlikely(horiz_align_ != 0.5f || vert_align_ != 0.5f)) {
    float x =
        SCALE(horiz_align_, 0.0f, 1.0f, +size_.x_ / 2.0f, -size_.x_ / 2.0f);
    float y =
        SCALE(vert_align_, 0.0f, 1.0f, +size_.y_ / 2.0f, -size_.y_ / 2.0f);
    RageMatrix m;
    RageMatrixTranslate(&m, x, y, 0);
    DISPLAY->PreMultMatrix(m);
  }

  // Get the quaternion of the actor
  const float quat_x = temp_state_->quat_.x_;
  const float quat_y = temp_state_->quat_.y_;
  const float quat_z = temp_state_->quat_.z_;
  const float quat_w = temp_state_->quat_.w_;

  if (quat_x != 0 || quat_y != 0 || quat_z != 0 || quat_w != 1) {
    RageMatrix mat;
    RageMatrixFromQuat(&mat, temp_state_->quat_);
    DISPLAY->MultMatrix(mat);
  }

  // Get the skew of the actor along the X-axis
  const float skew_x = temp_state_->skew_x_;
  if (skew_x != 0) {
    DISPLAY->SkewX(skew_x);
  }

  // Get the skew of the actor along the Y-axis
  const float skew_y = temp_state_->skew_y_;
  if (skew_y != 0) {
    DISPLAY->SkewY(skew_y);
  }

  // If the texture is not at the origin, translate the texture
  if (tex_translate_.x_ != 0 || tex_translate_.y_ != 0) {
    DISPLAY->TexturePushMatrix();
    DISPLAY->TextureTranslate(tex_translate_.x_, tex_translate_.y_);
  }
}

void Actor::SetGlobalRenderStates() {
  // set Actor-defined render states
  if (!show_masks.Get() || blend_mode_ != BLEND_NO_EFFECT) {
    DISPLAY->SetBlendMode(blend_mode_);
  }
  DISPLAY->SetZWrite(z_write_);
  DISPLAY->SetZTestMode(z_test_mode_);

  // BLEND_NO_EFFECT is used to draw masks to the Z-buffer, which always wants
  // Z-bias enabled.
  if (z_bias_ == 0 && blend_mode_ == BLEND_NO_EFFECT) {
    DISPLAY->SetZBias(1.0f);
  } else {
    DISPLAY->SetZBias(z_bias_);
  }

  if (clear_z_buffer_) {
    DISPLAY->ClearZBuffer();
  }
  DISPLAY->SetCullMode(cull_mode_);
}

void Actor::SetTextureRenderStates() {
  DISPLAY->SetTextureWrapping(TextureUnit_1, texture_wrapping_);
  DISPLAY->SetTextureFiltering(TextureUnit_1, texture_filtering_);
}

void Actor::EndDraw() {
  DISPLAY->PopMatrix();

  if (tex_translate_.x_ != 0 || tex_translate_.y_ != 0) {
    DISPLAY->TexturePopMatrix();
  }
}

void Actor::CalcPercentThroughTween() {
  TweenState& ts = tweens_[0]->state_;
  TweenInfo& ti = tweens_[0]->info_;
  const float percent_through = 1 - (ti.time_left_in_tween_ / ti.tween_time_);
  // distort the percentage if appropriate
  float percent_along = ti.tween_->Tween(percent_through);
  TweenState::MakeWeightedAverage(current_, start_, ts, percent_along);
  UpdatePercentThroughTween(percent_along);
}

void Actor::UpdateTweening(float delta_time) {
  if (delta_time < 0.0 && !tweens_.empty()) {
    tweens_[0]->info_.time_left_in_tween_ -= delta_time;
    CalcPercentThroughTween();
    return;
  }
  while (!tweens_.empty()    // something to do
         && delta_time > 0)  // something will change
  {
    // update current tween state
    // earliest tween
    auto& first_tween = tweens_[0];
    TweenState& ts = first_tween->state_;
    TweenInfo& ti = first_tween->info_;

    bool beginning = ti.time_left_in_tween_ == ti.tween_time_;

    float secs_to_subtract = std::min(ti.time_left_in_tween_, delta_time);
    ti.time_left_in_tween_ -= secs_to_subtract;
    delta_time -= secs_to_subtract;

    std::string command = ti.command_name_;
    if (beginning)  // we are just beginning this tween
    {
      start_ = current_;  // set the start position
      SetCurrentTweenStart();
    }

    if (ti.time_left_in_tween_ == 0)  // Current tween is over.  Stop.
    {
      current_ = ts;

      // delete the head tween
      delete first_tween;
      tweens_.erase(tweens_.begin());
      EraseHeadTween();
    } else  // in the middle of tweening. Recalcute the current position.
    {
      CalcPercentThroughTween();
    }

    if (beginning) {
      // Execute the command in this tween (if any). Do this last, and don't
      // access TI or TS after, since this may modify the tweening queue.
      if (!command.empty()) {
        if (Left(command, 1) == "!") {
          MESSAGEMAN->Broadcast(command.substr(1));
        } else {
          this->PlayCommand(command);
        }
      }
    }
  }
}

bool Actor::IsFirstUpdate() const { return first_update_; }

void Actor::Update(float delta_time) {
  //	LOG->Trace( "Actor::Update( %f )", fDeltaTime );

  float rate = GameLoop::GetUpdateRate();
  if (rate != 1 && !rate_scaling_enabled_) {
    // Prevent divide by 0 when tab + tilde are both pressed.
    if (rate != 0) {
      delta_time *= (1 / rate);
    }
  }

  ASSERT_M(delta_time >= 0, ssprintf("DeltaTime: %f", delta_time));

  if (hibernate_seconds_left_ > 0) {
    hibernate_seconds_left_ -= delta_time;
    if (hibernate_seconds_left_ > 0) {
      return;
    }

    // Grab the leftover time.
    delta_time = -hibernate_seconds_left_;
    hibernate_seconds_left_ = 0;
  }
  for (size_t i = 0; i < wrapper_states_.size(); ++i) {
    wrapper_states_[i]->Update(delta_time);
  }

  this->UpdateInternal(delta_time);
}

static void generic_global_timer_update(
    float new_time, float& effect_delta_time, float& time_into_effect) {
  effect_delta_time = new_time - time_into_effect;
  time_into_effect = new_time;
}

void Actor::UpdateInternal(float delta_time) {
  if (first_update_) {
    first_update_ = false;
  }

  const float effect_period = GetEffectPeriod();
  switch (effect_clock_) {
    case CLOCK_TIMER:
      secs_into_effect_ += delta_time;
      effect_delta_ = delta_time;
      // Wrap the counter, so it doesn't increase indefinitely (causing loss
      // of precision if a screen is left to sit for a day).
      if (secs_into_effect_ > effect_period) {
        secs_into_effect_ -= effect_period;
      }
      break;
    case CLOCK_TIMER_GLOBAL:
      generic_global_timer_update(
          RageTimer::GetTimeSinceStartMicroseconds(), effect_delta_,
          secs_into_effect_);
      break;
    case CLOCK_BGM_BEAT:
      generic_global_timer_update(
          current_bgm_beat_, effect_delta_, secs_into_effect_);
      break;
    case CLOCK_BGM_BEAT_PLAYER1:
      generic_global_timer_update(
          current_bgm_beat_player_[PLAYER_1], effect_delta_, secs_into_effect_);
      break;
    case CLOCK_BGM_BEAT_PLAYER2:
      generic_global_timer_update(
          current_bgm_beat_player_[PLAYER_2], effect_delta_, secs_into_effect_);
      break;
    case CLOCK_BGM_TIME:
      generic_global_timer_update(
          current_bgm_time_, effect_delta_, secs_into_effect_);
      break;
    case CLOCK_BGM_BEAT_NO_OFFSET:
      generic_global_timer_update(
          current_bgm_beat_no_offset_, effect_delta_, secs_into_effect_);
      break;
    case CLOCK_BGM_TIME_NO_OFFSET:
      generic_global_timer_update(
          current_bgm_time_no_offset_, effect_delta_, secs_into_effect_);
      break;
    default:
      if (effect_clock_ >= CLOCK_LIGHT_1 && effect_clock_ <= CLOCK_LIGHT_LAST) {
        int light_index = effect_clock_ - CLOCK_LIGHT_1;
        generic_global_timer_update(
            g_cabinet_lights[light_index], effect_delta_, secs_into_effect_);
      }
      break;
  }

  // update effect
  switch (effect_) {
    case spin:
      current_.rotation_ += effect_delta_ * effect_magnitude_;
      wrap(current_.rotation_.x_, 360);
      wrap(current_.rotation_.y_, 360);
      wrap(current_.rotation_.z_, 360);
      break;
    default:
      break;
  }

  if (tween_uses_effect_delta_) {
    delta_time = effect_delta_;
  }
  this->UpdateTweening(delta_time);
}

std::string Actor::GetLineage() const {
  std::string path;

  if (parent_) {
    path = parent_->GetLineage() + '/';
  }
  path += ssprintf("<type %s> %s", typeid(*this).name(), name_.c_str());
  return path;
}

void Actor::AddWrapperState() {
  ActorFrame* wrapper = new ActorFrame;
  wrapper->InitState();
  wrapper_states_.push_back(wrapper);
}

void Actor::RemoveWrapperState(size_t i) {
  ASSERT(i < wrapper_states_.size());
  RageUtil::SafeDelete(wrapper_states_[i]);
  wrapper_states_.erase(wrapper_states_.begin() + i);
}

Actor* Actor::GetWrapperState(size_t i) {
  ASSERT(i < wrapper_states_.size());
  return wrapper_states_[i];
}

void Actor::BeginTweening(float time, ITween* tween) {
  ASSERT(time >= 0);

  // If the number of tweens to ever gets this large, there's probably an
  // infinitely recursing ActorCommand.
  if (tweens_.size() > 50) {
    LuaHelpers::ReportScriptErrorFmt(
        "Tween overflow: \"%s\"; infinitely recursing ActorCommand?",
        GetLineage().c_str());
    this->FinishTweening();
  }

  // add a new TweenState to the tail, and initialize it
  tweens_.push_back(new TweenStateAndInfo);

  // latest
  TweenState& ts = tweens_.back()->state_;
  TweenInfo& ti = tweens_.back()->info_;

  if (tweens_.size() >= 2)  // if there was already a TS on the stack
  {
    // initialize the new TS from the last TS in the list
    ts = tweens_[tweens_.size() - 2]->state_;
  } else {
    // This new TS is the only TS.
    // Set our tween starting and ending values to the current position.
    ts = current_;
  }

  ti.tween_ = tween;
  ti.tween_time_ = time;
  ti.time_left_in_tween_ = time;
}

void Actor::BeginTweening(float time, TweenType tt) {
  ASSERT(time >= 0);

  ITween* tween = ITween::CreateFromType(tt);
  this->BeginTweening(time, tween);
}

void Actor::StopTweening() {
  for (unsigned i = 0; i < tweens_.size(); ++i) {
    delete tweens_[i];
  }
  tweens_.clear();
}

void Actor::FinishTweening() {
  if (!tweens_.empty()) {
    current_ = DestTweenState();
  }
  this->StopTweening();
}

void Actor::HurryTweening(float factor) {
  for (unsigned i = 0; i < tweens_.size(); ++i) {
    tweens_[i]->info_.time_left_in_tween_ *= factor;
    tweens_[i]->info_.tween_time_ *= factor;
  }
}

void Actor::ScaleTo(const RectF& rect, StretchType st) {
  // width and height of rectangle
  float rect_width = rect.GetWidth();
  float rect_height = rect.GetHeight();

  if (rect_width < 0) {
    SetRotationY(180);
  }
  if (rect_height < 0) {
    SetRotationX(180);
  }

  // zoom fActor needed to scale the Actor to fill the rectangle
  float new_zoom_x = std::abs(rect_width / size_.x_);
  float new_zoom_y = std::abs(rect_height / size_.y_);

  float new_zoom = 0.f;

  switch (st) {
    case StretchType::kCover:
      new_zoom =
          new_zoom_x > new_zoom_y ? new_zoom_x : new_zoom_y;  // use larger zoom
      break;
    case StretchType::kFitInside:
      new_zoom = new_zoom_x > new_zoom_y ? new_zoom_y
                                         : new_zoom_x;  // use smaller zoom
      break;
  }

  SetX(rect.left_ + rect_width * horiz_align_);
  SetY(rect.top_ + rect_height * vert_align_);

  SetZoom(new_zoom);
}

void Actor::SetEffectClockString(const std::string& s) {
  if (EqualsNoCase(s, "timer")) {
    this->SetEffectClock(CLOCK_TIMER);
  } else if (EqualsNoCase(s, "timerglobal")) {
    this->SetEffectClock(CLOCK_TIMER_GLOBAL);
  } else if (EqualsNoCase(s, "beat")) {
    this->SetEffectClock(CLOCK_BGM_BEAT);
  } else if (EqualsNoCase(s, "music")) {
    this->SetEffectClock(CLOCK_BGM_TIME);
  } else if (EqualsNoCase(s, "bgm")) {
    this->SetEffectClock(CLOCK_BGM_BEAT);  // compat, deprecated
  } else if (EqualsNoCase(s, "musicnooffset")) {
    this->SetEffectClock(CLOCK_BGM_TIME_NO_OFFSET);
  } else if (EqualsNoCase(s, "beatnooffset")) {
    this->SetEffectClock(CLOCK_BGM_BEAT_NO_OFFSET);
  } else {
    CabinetLight cl = StringToCabinetLight(s);
    if (cl == CabinetLight_Invalid) {
      LuaHelpers::ReportScriptErrorFmt(
          "String '%s' is not an effect clock string or the name of a cabinet "
          "light.",
          s.c_str());
    } else {
      this->SetEffectClock(
          static_cast<EffectClock>(Enum::to_integral(cl) + CLOCK_LIGHT_1));
    }
  }
}

void Actor::StretchTo(const RectF& r) {
  // width and height of rectangle
  float width = r.GetWidth();
  float height = r.GetHeight();

  // center of the rectangle
  float cx = r.left_ + width / 2.0f;
  float cy = r.top_ + height / 2.0f;

  // zoom fActor needed to scale the Actor to fill the rectangle
  float new_zoom_x = width / size_.x_;
  float new_zoom_y = height / size_.y_;

  SetXY(cx, cy);
  SetZoomX(new_zoom_x);
  SetZoomY(new_zoom_y);
}

void Actor::RecalcEffectPeriod() {
  effect_period_ = effect_ramp_to_half_ + effect_hold_at_half_ +
                   effect_ramp_to_full_ + effect_hold_at_full_ +
                   effect_hold_at_zero_;
}

void Actor::SetEffectPeriod(float time) {
  ASSERT(time > 0);
  effect_ramp_to_half_ = time / 2;
  effect_hold_at_half_ = 0;
  effect_ramp_to_full_ = time / 2;
  effect_hold_at_full_ = 0;
  effect_hold_at_zero_ = 0;
  RecalcEffectPeriod();
}

bool Actor::SetEffectTiming(
    float ramp_toh, float at_half, float ramp_tof, float at_full, float at_zero,
    std::string& err) {
  // No negative timings
  if (ramp_toh < 0 || at_half < 0 || ramp_tof < 0 || at_full < 0 ||
      at_zero < 0) {
    err = ssprintf(
        "Effect timings (%f,%f,%f,%f,%f) must not be negative;", ramp_toh,
        at_half, ramp_tof, at_zero, at_full);
    return false;
  }
  // and at least one positive timing.
  if (ramp_toh <= 0 && at_half <= 0 && ramp_tof <= 0 && at_full <= 0 &&
      at_zero <= 0) {
    err = "Effect timings (0,0,0,0,0) must not all be zero;";
    return false;
  }
  effect_ramp_to_half_ = ramp_toh;
  effect_hold_at_half_ = at_half;
  effect_ramp_to_full_ = ramp_tof;
  effect_hold_at_full_ = at_full;
  effect_hold_at_zero_ = at_zero;
  RecalcEffectPeriod();
  return true;
}

bool Actor::SetEffectHoldAtFull(float haf, std::string& err) {
  return SetEffectTiming(
      effect_ramp_to_half_, effect_hold_at_half_, effect_ramp_to_full_, haf,
      effect_hold_at_zero_, err);
}

// effect "macros"

void Actor::ResetEffectTimeIfDifferent(Effect new_effect) {
  if (effect_ != new_effect) {
    effect_ = new_effect;
    secs_into_effect_ = 0;
  }
}

void Actor::SetEffectDiffuseBlink(
    float effect_period_seconds, RageColor c1, RageColor c2) {
  ASSERT(effect_period_seconds > 0);
  ResetEffectTimeIfDifferent(diffuse_blink);
  SetEffectPeriod(effect_period_seconds);
  effect_color1_ = c1;
  effect_color2_ = c2;
}

void Actor::SetEffectDiffuseShift(
    float effect_period_seconds, RageColor c1, RageColor c2) {
  ASSERT(effect_period_seconds > 0);
  ResetEffectTimeIfDifferent(diffuse_shift);
  SetEffectPeriod(effect_period_seconds);
  effect_color1_ = c1;
  effect_color2_ = c2;
}

void Actor::SetEffectDiffuseRamp(
    float effect_period_seconds, RageColor c1, RageColor c2) {
  ASSERT(effect_period_seconds > 0);
  ResetEffectTimeIfDifferent(diffuse_ramp);
  SetEffectPeriod(effect_period_seconds);
  effect_color1_ = c1;
  effect_color2_ = c2;
}

void Actor::SetEffectGlowBlink(
    float effect_period_seconds, RageColor c1, RageColor c2) {
  ASSERT(effect_period_seconds > 0);
  ResetEffectTimeIfDifferent(glow_blink);
  SetEffectPeriod(effect_period_seconds);
  effect_color1_ = c1;
  effect_color2_ = c2;
}

void Actor::SetEffectGlowShift(
    float effect_period_seconds, RageColor c1, RageColor c2) {
  ASSERT(effect_period_seconds > 0);
  ResetEffectTimeIfDifferent(glow_shift);
  SetEffectPeriod(effect_period_seconds);
  effect_color1_ = c1;
  effect_color2_ = c2;
}

void Actor::SetEffectGlowRamp(
    float effect_period_seconds, RageColor c1, RageColor c2) {
  ASSERT(effect_period_seconds > 0);
  ResetEffectTimeIfDifferent(glow_ramp);
  SetEffectPeriod(effect_period_seconds);
  effect_color1_ = c1;
  effect_color2_ = c2;
}

void Actor::SetEffectRainbow(float effect_period_seconds) {
  ASSERT(effect_period_seconds > 0);
  ResetEffectTimeIfDifferent(rainbow);
  SetEffectPeriod(effect_period_seconds);
}

void Actor::SetEffectWag(float period, RageVector3 vect) {
  ASSERT(period > 0);
  ResetEffectTimeIfDifferent(wag);
  SetEffectPeriod(period);
  effect_magnitude_ = vect;
}

void Actor::SetEffectBounce(float period, RageVector3 vect) {
  ASSERT(period > 0);
  effect_ = bounce;
  SetEffectPeriod(period);
  effect_magnitude_ = vect;
  secs_into_effect_ = 0;
}

void Actor::SetEffectBob(float period, RageVector3 vect) {
  ASSERT(period > 0);
  if (effect_ != bob || GetEffectPeriod() != period) {
    effect_ = bob;
    SetEffectPeriod(period);
    secs_into_effect_ = 0;
  }
  effect_magnitude_ = vect;
}

void Actor::SetEffectSpin(RageVector3 vect) {
  effect_ = spin;
  effect_magnitude_ = vect;
}

void Actor::SetEffectVibrate(RageVector3 vect) {
  effect_ = vibrate;
  effect_magnitude_ = vect;
}

void Actor::SetEffectPulse(float period, float min_zoom, float max_zoom) {
  ASSERT(period > 0);
  effect_ = pulse;
  SetEffectPeriod(period);
  effect_magnitude_[0] = min_zoom;
  effect_magnitude_[1] = max_zoom;
}

void Actor::AddRotationH(float rot) {
  RageQuatMultiply(
      &DestTweenState().quat_, DestTweenState().quat_, RageQuatFromH(rot));
}

void Actor::AddRotationP(float rot) {
  RageQuatMultiply(
      &DestTweenState().quat_, DestTweenState().quat_, RageQuatFromP(rot));
}

void Actor::AddRotationR(float rot) {
  RageQuatMultiply(
      &DestTweenState().quat_, DestTweenState().quat_, RageQuatFromR(rot));
}

void Actor::RunCommands(
    const LuaReference& cmds, const LuaReference* param_table) {
  if (!cmds.IsSet() || cmds.IsNil()) {
    LuaHelpers::ReportScriptErrorFmt(
        "RunCommands: commands for %s are unset or nil", GetLineage().c_str());
    return;
  }

  Lua* L = LUA->Get();

  // function
  cmds.PushSelf(L);
  if (lua_isnil(L, -1)) {
    LuaHelpers::ReportScriptErrorFmt(
        "RunCommands: Error compiling commands for %s", GetLineage().c_str());
    LUA->Release(L);
    return;
  }

  // 1st parameter
  this->PushSelf(L);

  // 2nd parameter
  if (param_table == nullptr) {
    lua_pushnil(L);
  } else {
    param_table->PushSelf(L);
  }

  // call function with 2 arguments and 0 results
  std::string error = "Error playing command:";
  LuaHelpers::RunScriptOnStack(L, error, 2, 0, true);

  LUA->Release(L);
}

float Actor::GetTweenTimeLeft() const {
  float tot = 0;

  tot += hibernate_seconds_left_;

  for (unsigned i = 0; i < tweens_.size(); ++i) {
    tot += tweens_[i]->info_.time_left_in_tween_;
  }

  return tot;
}

/* This is a hack to change all tween states while leaving existing tweens
 * alone.
 *
 * Hmm. Most commands actually act on a TweenStateAndInfo, not the Actor itself.
 * Conceptually, it wouldn't be hard to give TweenState a presence in Lua, so
 * we can simply say eg. "for x in states(Actor) do x.SetDiffuseColor(c) end".
 * However, we'd then have to give every TweenState a userdata in Lua while it's
 * being manipulated, which would add overhead ... */
void Actor::SetGlobalDiffuseColor(RageColor c) {
  for (int i = 0; i < kNumDiffuseColors; i++)  // color, not alpha
  {
    for (unsigned ts = 0; ts < tweens_.size(); ++ts) {
      tweens_[ts]->state_.diffuse_[i].r_ = c.r_;
      tweens_[ts]->state_.diffuse_[i].g_ = c.g_;
      tweens_[ts]->state_.diffuse_[i].b_ = c.b_;
    }
    current_.diffuse_[i].r_ = c.r_;
    current_.diffuse_[i].g_ = c.g_;
    current_.diffuse_[i].b_ = c.b_;
    start_.diffuse_[i].r_ = c.r_;
    start_.diffuse_[i].g_ = c.g_;
    start_.diffuse_[i].b_ = c.b_;
  }
}

void Actor::SetDiffuseColor(RageColor c) {
  for (int i = 0; i < kNumDiffuseColors; i++) {
    DestTweenState().diffuse_[i].r_ = c.r_;
    DestTweenState().diffuse_[i].g_ = c.g_;
    DestTweenState().diffuse_[i].b_ = c.b_;
  }
}

void Actor::TweenState::Init() {
  pos_ = RageVector3(0, 0, 0);
  rotation_ = RageVector3(0, 0, 0);
  quat_ = RageVector4(0, 0, 0, 1);
  scale_ = RageVector3(1, 1, 1);
  skew_x_ = 0;
  skew_y_ = 0;
  crop_ = RectF(0, 0, 0, 0);
  fade_ = RectF(0, 0, 0, 0);
  for (int i = 0; i < kNumDiffuseColors; i++) {
    diffuse_[i] = RageColor(1, 1, 1, 1);
  }
  glow_ = RageColor(1, 1, 1, 0);
  aux_ = 0;
}

bool Actor::TweenState::operator==(const TweenState& other) const {
  if (pos_ != other.pos_) {
    return false;
  }
  if (rotation_ != other.rotation_) {
    return false;
  }
  if (quat_ != other.quat_) {
    return false;
  }
  if (scale_ != other.scale_) {
    return false;
  }
  if (skew_x_ != other.skew_x_) {
    return false;
  }
  if (skew_y_ != other.skew_y_) {
    return false;
  }
  if (crop_ != other.crop_) {
    return false;
  }
  if (fade_ != other.fade_) {
    return false;
  }
  for (unsigned i = 0; i < ARRAYLEN(diffuse_); i++) {
    if (diffuse_[i] != other.diffuse_[i]) {
      return false;
    }
  }
  if (glow_ != other.glow_) {
    return false;
  }
  if (aux_ != other.aux_) {
    return false;
  }
  return true;
}

void Actor::TweenState::MakeWeightedAverage(
    TweenState& average_out, const TweenState& ts1, const TweenState& ts2,
    float percent_between) {
  average_out.pos_ = lerp(percent_between, ts1.pos_, ts2.pos_);
  average_out.scale_ = lerp(percent_between, ts1.scale_, ts2.scale_);
  average_out.rotation_ = lerp(percent_between, ts1.rotation_, ts2.rotation_);
  RageQuatSlerp(&average_out.quat_, ts1.quat_, ts2.quat_, percent_between);
  average_out.skew_x_ = lerp(percent_between, ts1.skew_x_, ts2.skew_x_);
  average_out.skew_y_ = lerp(percent_between, ts1.skew_y_, ts2.skew_y_);

  average_out.crop_.left_ =
      lerp(percent_between, ts1.crop_.left_, ts2.crop_.left_);
  average_out.crop_.top_ =
      lerp(percent_between, ts1.crop_.top_, ts2.crop_.top_);
  average_out.crop_.right_ =
      lerp(percent_between, ts1.crop_.right_, ts2.crop_.right_);
  average_out.crop_.bottom_ =
      lerp(percent_between, ts1.crop_.bottom_, ts2.crop_.bottom_);

  average_out.fade_.left_ =
      lerp(percent_between, ts1.fade_.left_, ts2.fade_.left_);
  average_out.fade_.top_ =
      lerp(percent_between, ts1.fade_.top_, ts2.fade_.top_);
  average_out.fade_.right_ =
      lerp(percent_between, ts1.fade_.right_, ts2.fade_.right_);
  average_out.fade_.bottom_ =
      lerp(percent_between, ts1.fade_.bottom_, ts2.fade_.bottom_);

  for (int i = 0; i < kNumDiffuseColors; ++i) {
    average_out.diffuse_[i] =
        lerp(percent_between, ts1.diffuse_[i], ts2.diffuse_[i]);
  }

  average_out.glow_ = lerp(percent_between, ts1.glow_, ts2.glow_);
  average_out.aux_ = lerp(percent_between, ts1.aux_, ts2.aux_);
}

void Actor::Sleep(float time) {
  ASSERT(time >= 0);

  BeginTweening(time, TWEEN_LINEAR);
  BeginTweening(0, TWEEN_LINEAR);
}

void Actor::QueueCommand(const std::string& command_name) {
  BeginTweening(0, TWEEN_LINEAR);
  TweenInfo& ti = tweens_.back()->info_;
  ti.command_name_ = command_name;
}

void Actor::QueueMessage(const std::string& message_name) {
  // Hack: use "!" as a marker to broadcast a command, instead of playing a
  // command, so we don't have to add yet another element to every tween
  // state for this rarely-used command.
  BeginTweening(0, TWEEN_LINEAR);
  TweenInfo& ti = tweens_.back()->info_;
  ti.command_name_ = "!" + message_name;
}

void Actor::AddCommand(
    const std::string& cmd_name, apActorCommands apac, bool warn) {
  if (HasCommand(cmd_name) && warn) {
    std::string warning =
        GetLineage() + "'s command '" + cmd_name + "' defined twice";
    LuaHelpers::ReportScriptError(warning, "COMMAND_DEFINED_TWICE");
  }

  std::string message;
  if (GetMessageNameFromCommandName(cmd_name, message)) {
    SubscribeToMessage(message);
    map_name_to_commands_[message] = apac;  // sCmdName w/o "Message" at the end
  } else {
    map_name_to_commands_[cmd_name] = apac;
  }
}

bool Actor::HasCommand(const std::string& cmd_name) const {
  return GetCommand(cmd_name) != nullptr;
}

const apActorCommands* Actor::GetCommand(
    const std::string& command_name) const {
  std::map<std::string, apActorCommands>::const_iterator it =
      map_name_to_commands_.find(command_name);
  if (it == map_name_to_commands_.end()) {
    return nullptr;
  }
  return &it->second;
}

void Actor::HandleMessage(const Message& msg) { PlayCommandNoRecurse(msg); }

void Actor::PlayCommandNoRecurse(const Message& msg) {
  const apActorCommands* cmd = GetCommand(msg.GetName());
  if (cmd != nullptr && (*cmd)->IsSet() && !(*cmd)->IsNil()) {
    RunCommands(*cmd, &msg.GetParamTable());
  }
}

void Actor::PushContext(lua_State* L) {
  // self.ctx should already exist
  lua_instance_->PushSelf(L);
  lua_getfield(L, -1, "ctx");
  lua_remove(L, -2);
}

void Actor::SetParent(Actor* parent) {
  parent_ = parent;

  Lua* L = LUA->Get();
  int top = lua_gettop(L);

  this->PushContext(L);
  lua_pushstring(L, "__index");
  parent->PushContext(L);
  lua_settable(L, -3);

  lua_settop(L, top);
  LUA->Release(L);
}

Actor::TweenInfo::TweenInfo() { tween_ = nullptr; }

Actor::TweenInfo::~TweenInfo() { delete tween_; }

Actor::TweenInfo::TweenInfo(const TweenInfo& cpy) {
  tween_ = nullptr;
  *this = cpy;
}

Actor::TweenInfo& Actor::TweenInfo::operator=(const TweenInfo& rhs) {
  delete tween_;
  tween_ = (rhs.tween_ ? rhs.tween_->Copy() : nullptr);
  time_left_in_tween_ = rhs.time_left_in_tween_;
  tween_time_ = rhs.tween_time_;
  command_name_ = rhs.command_name_;
  return *this;
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the Actor. */
class LunaActor : public Luna<Actor> {
 public:
  static int name(T* p, lua_State* L) {
    p->SetName(SArg(1));
    COMMON_RETURN_SELF;
  }
  static int sleep(T* p, lua_State* L) {
    float time = FArg(1);
    if (time < 0) {
      LuaHelpers::ReportScriptErrorFmt(
          "Lua: sleep(%f): time must not be negative", time);
      COMMON_RETURN_SELF;
    }
    p->Sleep(time);
    COMMON_RETURN_SELF;
  }
  static int linear(T* p, lua_State* L) {
    float time = FArg(1);
    if (time < 0) {
      LuaHelpers::ReportScriptErrorFmt(
          "Lua: linear(%f): tween time must not be negative", time);
      COMMON_RETURN_SELF;
    }
    p->BeginTweening(time, TWEEN_LINEAR);
    COMMON_RETURN_SELF;
  }
  static int accelerate(T* p, lua_State* L) {
    float time = FArg(1);
    if (time < 0) {
      LuaHelpers::ReportScriptErrorFmt(
          "Lua: accelerate(%f): tween time must not be negative", time);
      COMMON_RETURN_SELF;
    }
    p->BeginTweening(time, TWEEN_ACCELERATE);
    COMMON_RETURN_SELF;
  }
  static int decelerate(T* p, lua_State* L) {
    float time = FArg(1);
    if (time < 0) {
      LuaHelpers::ReportScriptErrorFmt(
          "Lua: decelerate(%f): tween time must not be negative", time);
      COMMON_RETURN_SELF;
    }
    p->BeginTweening(time, TWEEN_DECELERATE);
    COMMON_RETURN_SELF;
  }
  static int spring(T* p, lua_State* L) {
    float time = FArg(1);
    if (time < 0) {
      LuaHelpers::ReportScriptErrorFmt(
          "Lua: spring(%f): tween time must not be negative", time);
      COMMON_RETURN_SELF;
    }
    p->BeginTweening(time, TWEEN_SPRING);
    COMMON_RETURN_SELF;
  }
  static int tween(T* p, lua_State* L) {
    float time = FArg(1);
    if (time < 0) {
      LuaHelpers::ReportScriptErrorFmt(
          "Lua: tween(%f): tween time must not be negative", time);
      COMMON_RETURN_SELF;
    }
    ITween* p_tween = ITween::CreateFromStack(L, 2);
    if (p_tween != nullptr) {
      p->BeginTweening(time, p_tween);
    }
    COMMON_RETURN_SELF;
  }
  static int stoptweening(T* p, lua_State* L) {
    p->StopTweening();
    COMMON_RETURN_SELF;
  }
  static int finishtweening(T* p, lua_State* L) {
    p->FinishTweening();
    COMMON_RETURN_SELF;
  }
  static int hurrytweening(T* p, lua_State* L) {
    float time = FArg(1);
    if (time < 0.0f) {
      luaL_error(L, "Tweening hurry factor cannot be negative. %f", time);
    }
    p->HurryTweening(time);
    COMMON_RETURN_SELF;
  }
  static int GetTweenTimeLeft(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetTweenTimeLeft());
    return 1;
  }
  static int x(T* p, lua_State* L) {
    p->SetX(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int y(T* p, lua_State* L) {
    p->SetY(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int z(T* p, lua_State* L) {
    p->SetZ(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int xy(T* p, lua_State* L) {
    p->SetXY(FArg(1), FArg(2));
    COMMON_RETURN_SELF;
  }
  static int addx(T* p, lua_State* L) {
    p->AddX(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int addy(T* p, lua_State* L) {
    p->AddY(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int addz(T* p, lua_State* L) {
    p->AddZ(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int zoom(T* p, lua_State* L) {
    p->SetZoom(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int zoomx(T* p, lua_State* L) {
    p->SetZoomX(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int zoomy(T* p, lua_State* L) {
    p->SetZoomY(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int zoomz(T* p, lua_State* L) {
    p->SetZoomZ(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int zoomto(T* p, lua_State* L) {
    p->ZoomTo(FArg(1), FArg(2));
    COMMON_RETURN_SELF;
  }
  static int zoomtowidth(T* p, lua_State* L) {
    p->ZoomToWidth(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int zoomtoheight(T* p, lua_State* L) {
    p->ZoomToHeight(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int setsize(T* p, lua_State* L) {
    p->SetWidth(FArg(1));
    p->SetHeight(FArg(2));
    COMMON_RETURN_SELF;
  }
  static int SetWidth(T* p, lua_State* L) {
    p->SetWidth(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int SetHeight(T* p, lua_State* L) {
    p->SetHeight(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int basealpha(T* p, lua_State* L) {
    p->SetBaseAlpha(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int basezoom(T* p, lua_State* L) {
    p->SetBaseZoom(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int basezoomx(T* p, lua_State* L) {
    p->SetBaseZoomX(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int basezoomy(T* p, lua_State* L) {
    p->SetBaseZoomY(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int basezoomz(T* p, lua_State* L) {
    p->SetBaseZoomZ(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int stretchto(T* p, lua_State* L) {
    p->StretchTo(RectF(FArg(1), FArg(2), FArg(3), FArg(4)));
    COMMON_RETURN_SELF;
  }
  static int cropleft(T* p, lua_State* L) {
    p->SetCropLeft(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int croptop(T* p, lua_State* L) {
    p->SetCropTop(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int cropright(T* p, lua_State* L) {
    p->SetCropRight(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int cropbottom(T* p, lua_State* L) {
    p->SetCropBottom(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int fadeleft(T* p, lua_State* L) {
    p->SetFadeLeft(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int fadetop(T* p, lua_State* L) {
    p->SetFadeTop(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int faderight(T* p, lua_State* L) {
    p->SetFadeRight(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int fadebottom(T* p, lua_State* L) {
    p->SetFadeBottom(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int diffuse(T* p, lua_State* L) {
    RageColor c;
    c.FromStackCompat(L, 1);
    p->SetDiffuse(c);
    COMMON_RETURN_SELF;
  }
  static int diffuseupperleft(T* p, lua_State* L) {
    RageColor c;
    c.FromStackCompat(L, 1);
    p->SetDiffuseUpperLeft(c);
    COMMON_RETURN_SELF;
  }
  static int diffuseupperright(T* p, lua_State* L) {
    RageColor c;
    c.FromStackCompat(L, 1);
    p->SetDiffuseUpperRight(c);
    COMMON_RETURN_SELF;
  }
  static int diffuselowerleft(T* p, lua_State* L) {
    RageColor c;
    c.FromStackCompat(L, 1);
    p->SetDiffuseLowerLeft(c);
    COMMON_RETURN_SELF;
  }
  static int diffuselowerright(T* p, lua_State* L) {
    RageColor c;
    c.FromStackCompat(L, 1);
    p->SetDiffuseLowerRight(c);
    COMMON_RETURN_SELF;
  }
  static int diffuseleftedge(T* p, lua_State* L) {
    RageColor c;
    c.FromStackCompat(L, 1);
    p->SetDiffuseLeftEdge(c);
    COMMON_RETURN_SELF;
  }
  static int diffuserightedge(T* p, lua_State* L) {
    RageColor c;
    c.FromStackCompat(L, 1);
    p->SetDiffuseRightEdge(c);
    COMMON_RETURN_SELF;
  }
  static int diffusetopedge(T* p, lua_State* L) {
    RageColor c;
    c.FromStackCompat(L, 1);
    p->SetDiffuseTopEdge(c);
    COMMON_RETURN_SELF;
  }
  static int diffusebottomedge(T* p, lua_State* L) {
    RageColor c;
    c.FromStackCompat(L, 1);
    p->SetDiffuseBottomEdge(c);
    COMMON_RETURN_SELF;
  }
  static int diffusealpha(T* p, lua_State* L) {
    p->SetDiffuseAlpha(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int diffusecolor(T* p, lua_State* L) {
    RageColor c;
    c.FromStackCompat(L, 1);
    p->SetDiffuseColor(c);
    COMMON_RETURN_SELF;
  }
  static int glow(T* p, lua_State* L) {
    RageColor c;
    c.FromStackCompat(L, 1);
    p->SetGlow(c);
    COMMON_RETURN_SELF;
  }
  static int aux(T* p, lua_State* L) {
    p->SetAux(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int getaux(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetAux());
    return 1;
  }
  static int rotationx(T* p, lua_State* L) {
    p->SetRotationX(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int rotationy(T* p, lua_State* L) {
    p->SetRotationY(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int rotationz(T* p, lua_State* L) {
    p->SetRotationZ(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int addrotationx(T* p, lua_State* L) {
    p->AddRotationX(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int addrotationy(T* p, lua_State* L) {
    p->AddRotationY(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int addrotationz(T* p, lua_State* L) {
    p->AddRotationZ(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int getrotation(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetRotationX());
    lua_pushnumber(L, p->GetRotationY());
    lua_pushnumber(L, p->GetRotationZ());
    return 3;
  }
  static int baserotationx(T* p, lua_State* L) {
    p->SetBaseRotationX(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int baserotationy(T* p, lua_State* L) {
    p->SetBaseRotationY(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int baserotationz(T* p, lua_State* L) {
    p->SetBaseRotationZ(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int skewx(T* p, lua_State* L) {
    p->SetSkewX(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int skewy(T* p, lua_State* L) {
    p->SetSkewY(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int heading(T* p, lua_State* L) {
    p->AddRotationH(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int pitch(T* p, lua_State* L) {
    p->AddRotationP(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int roll(T* p, lua_State* L) {
    p->AddRotationR(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int shadowlength(T* p, lua_State* L) {
    p->SetShadowLength(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int shadowlengthx(T* p, lua_State* L) {
    p->SetShadowLengthX(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int shadowlengthy(T* p, lua_State* L) {
    p->SetShadowLengthY(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int shadowcolor(T* p, lua_State* L) {
    RageColor c;
    c.FromStackCompat(L, 1);
    p->SetShadowColor(c);
    COMMON_RETURN_SELF;
  }
  static int horizalign(T* p, lua_State* L) {
    p->SetHorizAlign(Enum::Check<HorizAlign>(L, 1));
    COMMON_RETURN_SELF;
  }
  static int vertalign(T* p, lua_State* L) {
    p->SetVertAlign(Enum::Check<VertAlign>(L, 1));
    COMMON_RETURN_SELF;
  }
  static int halign(T* p, lua_State* L) {
    p->SetHorizAlign(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int valign(T* p, lua_State* L) {
    p->SetVertAlign(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int diffuseblink(T* p, lua_State* L) {
    p->SetEffectDiffuseBlink(
        1.0f, RageColor(0.5f, 0.5f, 0.5f, 0.5f), RageColor(1, 1, 1, 1));
    COMMON_RETURN_SELF;
  }
  static int diffuseshift(T* p, lua_State* L) {
    p->SetEffectDiffuseShift(
        1.0f, RageColor(0, 0, 0, 1), RageColor(1, 1, 1, 1));
    COMMON_RETURN_SELF;
  }
  static int diffuseramp(T* p, lua_State* L) {
    p->SetEffectDiffuseRamp(1.0f, RageColor(0, 0, 0, 1), RageColor(1, 1, 1, 1));
    COMMON_RETURN_SELF;
  }
  static int glowblink(T* p, lua_State* L) {
    p->SetEffectGlowBlink(
        1.0f, RageColor(1, 1, 1, 0.2f), RageColor(1, 1, 1, 0.8f));
    COMMON_RETURN_SELF;
  }
  static int glowshift(T* p, lua_State* L) {
    p->SetEffectGlowShift(
        1.0f, RageColor(1, 1, 1, 0.2f), RageColor(1, 1, 1, 0.8f));
    COMMON_RETURN_SELF;
  }
  static int glowramp(T* p, lua_State* L) {
    p->SetEffectGlowRamp(
        1.0f, RageColor(1, 1, 1, 0.2f), RageColor(1, 1, 1, 0.8f));
    COMMON_RETURN_SELF;
  }
  static int rainbow(T* p, lua_State* L) {
    p->SetEffectRainbow(2.0f);
    COMMON_RETURN_SELF;
  }
  static int wag(T* p, lua_State* L) {
    p->SetEffectWag(2.0f, RageVector3(0, 0, 20));
    COMMON_RETURN_SELF;
  }
  static int bounce(T* p, lua_State* L) {
    p->SetEffectBounce(2.0f, RageVector3(0, 20, 0));
    COMMON_RETURN_SELF;
  }
  static int bob(T* p, lua_State* L) {
    p->SetEffectBob(2.0f, RageVector3(0, 20, 0));
    COMMON_RETURN_SELF;
  }
  static int pulse(T* p, lua_State* L) {
    p->SetEffectPulse(2.0f, 0.5f, 1.0f);
    COMMON_RETURN_SELF;
  }
  static int spin(T* p, lua_State* L) {
    p->SetEffectSpin(RageVector3(0, 0, 180));
    COMMON_RETURN_SELF;
  }
  static int vibrate(T* p, lua_State* L) {
    p->SetEffectVibrate(RageVector3(10, 10, 10));
    COMMON_RETURN_SELF;
  }
  static int stopeffect(T* p, lua_State* L) {
    p->StopEffect();
    COMMON_RETURN_SELF;
  }
  static int effectcolor1(T* p, lua_State* L) {
    RageColor c;
    c.FromStackCompat(L, 1);
    p->SetEffectColor1(c);
    COMMON_RETURN_SELF;
  }
  static int effectcolor2(T* p, lua_State* L) {
    RageColor c;
    c.FromStackCompat(L, 1);
    p->SetEffectColor2(c);
    COMMON_RETURN_SELF;
  }
  static int effectperiod(T* p, lua_State* L) {
    float period = FArg(1);
    if (period <= 0) {
      LuaHelpers::ReportScriptErrorFmt(
          "Effect period (%f) must be positive; ignoring", period);
      COMMON_RETURN_SELF;
    }
    p->SetEffectPeriod(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int effecttiming(T* p, lua_State* L) {
    float rth = FArg(1);
    float hah = FArg(2);
    float rtf = FArg(3);
    float haz = FArg(4);
    // Compatibility:  hold_at_full is optional.
    float haf = 0;
    if (lua_isnumber(L, 5)) {
      haf = FArg(5);
    }
    std::string err;
    if (!p->SetEffectTiming(rth, hah, rtf, haf, haz, err)) {
      luaL_error(L, err.c_str());
    }
    COMMON_RETURN_SELF;
  }
  static int effect_hold_at_full(T* p, lua_State* L) {
    std::string err;
    if (!p->SetEffectHoldAtFull(FArg(1), err)) {
      luaL_error(L, err.c_str());
    }
    COMMON_RETURN_SELF;
  }
  static int effectoffset(T* p, lua_State* L) {
    p->SetEffectOffset(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int effectclock(T* p, lua_State* L) {
    p->SetEffectClockString(SArg(1));
    COMMON_RETURN_SELF;
  }
  static int effectmagnitude(T* p, lua_State* L) {
    p->SetEffectMagnitude(RageVector3(FArg(1), FArg(2), FArg(3)));
    COMMON_RETURN_SELF;
  }
  static int geteffectmagnitude(T* p, lua_State* L) {
    RageVector3 v = p->GetEffectMagnitude();
    lua_pushnumber(L, v[0]);
    lua_pushnumber(L, v[1]);
    lua_pushnumber(L, v[2]);
    return 3;
  }
  GETTER_SETTER_BOOL_METHOD(tween_uses_effect_delta);
  static int scaletocover(T* p, lua_State* L) {
    p->ScaleToCover(RectF(FArg(1), FArg(2), FArg(3), FArg(4)));
    COMMON_RETURN_SELF;
  }
  static int scaletofit(T* p, lua_State* L) {
    p->ScaleToFitInside(RectF(FArg(1), FArg(2), FArg(3), FArg(4)));
    COMMON_RETURN_SELF;
  }
  static int animate(T* p, lua_State* L) {
    p->EnableAnimation(BIArg(1));
    COMMON_RETURN_SELF;
  }
  static int play(T* p, lua_State* L) {
    p->EnableAnimation(true);
    COMMON_RETURN_SELF;
  }
  static int pause(T* p, lua_State* L) {
    p->EnableAnimation(false);
    COMMON_RETURN_SELF;
  }
  static int setstate(T* p, lua_State* L) {
    p->SetState(IArg(1));
    COMMON_RETURN_SELF;
  }
  static int GetNumStates(T* p, lua_State* L) {
    LuaHelpers::Push(L, p->GetNumStates());
    return 1;
  }
  static int texturetranslate(T* p, lua_State* L) {
    p->SetTextureTranslate(FArg(1), FArg(2));
    COMMON_RETURN_SELF;
  }
  static int texturewrapping(T* p, lua_State* L) {
    p->SetTextureWrapping(BIArg(1));
    COMMON_RETURN_SELF;
  }
  static int SetTextureFiltering(T* p, lua_State* L) {
    p->SetTextureFiltering(BArg(1));
    COMMON_RETURN_SELF;
  }
  static int blend(T* p, lua_State* L) {
    p->SetBlendMode(Enum::Check<BlendMode>(L, 1));
    COMMON_RETURN_SELF;
  }
  static int zbuffer(T* p, lua_State* L) {
    p->SetUseZBuffer(BIArg(1));
    COMMON_RETURN_SELF;
  }
  static int ztest(T* p, lua_State* L) {
    p->SetZTestMode((BIArg(1)) ? ZTEST_WRITE_ON_PASS : ZTEST_OFF);
    COMMON_RETURN_SELF;
  }
  static int ztestmode(T* p, lua_State* L) {
    p->SetZTestMode(Enum::Check<ZTestMode>(L, 1));
    COMMON_RETURN_SELF;
  }
  static int zwrite(T* p, lua_State* L) {
    p->SetZWrite(BIArg(1));
    COMMON_RETURN_SELF;
  }
  static int zbias(T* p, lua_State* L) {
    p->SetZBias(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int clearzbuffer(T* p, lua_State* L) {
    p->SetClearZBuffer(BIArg(1));
    COMMON_RETURN_SELF;
  }
  static int backfacecull(T* p, lua_State* L) {
    p->SetCullMode((BIArg(1)) ? CULL_BACK : CULL_NONE);
    COMMON_RETURN_SELF;
  }
  static int cullmode(T* p, lua_State* L) {
    p->SetCullMode(Enum::Check<CullMode>(L, 1));
    COMMON_RETURN_SELF;
  }
  static int visible(T* p, lua_State* L) {
    p->SetVisible(BIArg(1));
    COMMON_RETURN_SELF;
  }
  static int hibernate(T* p, lua_State* L) {
    p->SetHibernate(FArg(1));
    COMMON_RETURN_SELF;
  }
  static int draworder(T* p, lua_State* L) {
    p->SetDrawOrder(IArg(1));
    COMMON_RETURN_SELF;
  }
  static int playcommand(T* p, lua_State* L) {
    if (!lua_istable(L, 2) && !lua_isnoneornil(L, 2)) {
      luaL_typerror(L, 2, "table or nil");
    }

    LuaReference param_table;
    lua_pushvalue(L, 2);
    param_table.SetFromStack(L);

    Message msg(SArg(1), param_table);
    p->HandleMessage(msg);

    COMMON_RETURN_SELF;
  }
  static int queuecommand(T* p, lua_State* L) {
    p->QueueCommand(SArg(1));
    COMMON_RETURN_SELF;
  }
  static int queuemessage(T* p, lua_State* L) {
    p->QueueMessage(SArg(1));
    COMMON_RETURN_SELF;
  }
  static int addcommand(T* p, lua_State* L) {
    LuaReference* ref = new LuaReference;
    ref->SetFromStack(L);
    p->AddCommand(SArg(1), apActorCommands(ref));
    COMMON_RETURN_SELF;
  }
  static int GetCommand(T* p, lua_State* L) {
    const apActorCommands* command = p->GetCommand(SArg(1));
    if (command == nullptr) {
      lua_pushnil(L);
    } else {
      (*command)->PushSelf(L);
    }

    return 1;
  }
  static int RunCommandsRecursively(T* p, lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    if (!lua_istable(L, 2) && !lua_isnoneornil(L, 2)) {
      luaL_typerror(L, 2, "table or nil");
    }

    LuaReference ref;
    lua_pushvalue(L, 1);
    ref.SetFromStack(L);

    LuaReference param_table;
    lua_pushvalue(L, 2);
    param_table.SetFromStack(L);

    p->RunCommandsRecursively(ref, &param_table);
    COMMON_RETURN_SELF;
  }

  static int GetX(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetX());
    return 1;
  }
  static int GetY(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetY());
    return 1;
  }
  static int GetZ(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetZ());
    return 1;
  }
  static int GetDestX(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetDestX());
    return 1;
  }
  static int GetDestY(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetDestY());
    return 1;
  }
  static int GetDestZ(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetDestZ());
    return 1;
  }
  static int GetWidth(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetUnzoomedWidth());
    return 1;
  }
  static int GetHeight(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetUnzoomedHeight());
    return 1;
  }
  static int GetZoomedWidth(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetZoomedWidth());
    return 1;
  }
  static int GetZoomedHeight(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetZoomedHeight());
    return 1;
  }
  static int GetZoom(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetZoom());
    return 1;
  }
  static int GetZoomX(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetZoomX());
    return 1;
  }
  static int GetZoomY(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetZoomY());
    return 1;
  }
  static int GetZoomZ(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetZoomZ());
    return 1;
  }
  static int GetBaseZoomX(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetBaseZoomX());
    return 1;
  }
  static int GetBaseZoomY(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetBaseZoomY());
    return 1;
  }
  static int GetBaseZoomZ(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetBaseZoomZ());
    return 1;
  }
  static int GetRotationX(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetRotationX());
    return 1;
  }
  static int GetRotationY(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetRotationY());
    return 1;
  }
  static int GetRotationZ(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetRotationZ());
    return 1;
  }
  static int GetSecsIntoEffect(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetSecsIntoEffect());
    return 1;
  }
  static int GetEffectDelta(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetEffectDelta());
    return 1;
  }
  DEFINE_METHOD(GetDiffuse, GetDiffuse())
  DEFINE_METHOD(GetGlow, GetGlow())
  static int GetDiffuseAlpha(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetDiffuseAlpha());
    return 1;
  }
  static int GetVisible(T* p, lua_State* L) {
    lua_pushboolean(L, p->GetVisible());
    return 1;
  }
  static int GetHAlign(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetHorizAlign());
    return 1;
  }
  static int GetVAlign(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetVertAlign());
    return 1;
  }

  static int GetName(T* p, lua_State* L) {
    lua_pushstring(L, p->GetName().c_str());
    return 1;
  }
  static int GetParent(T* p, lua_State* L) {
    Actor* parent = p->GetParent();
    if (parent == nullptr) {
      lua_pushnil(L);
    } else {
      parent->PushSelf(L);
    }
    return 1;
  }
  static int GetFakeParent(T* p, lua_State* L) {
    Actor* fake = p->GetFakeParent();
    if (fake == nullptr) {
      lua_pushnil(L);
    } else {
      fake->PushSelf(L);
    }
    return 1;
  }
  static int SetFakeParent(T* p, lua_State* L) {
    if (lua_isnoneornil(L, 1)) {
      p->SetFakeParent(nullptr);
    } else {
      Actor* fake = Luna<Actor>::check(L, 1);
      p->SetFakeParent(fake);
    }
    COMMON_RETURN_SELF;
  }
  static int AddWrapperState(T* p, lua_State* L) {
    p->AddWrapperState();
    p->GetWrapperState(p->GetNumWrapperStates() - 1)->PushSelf(L);
    return 1;
  }
  static size_t get_state_index(T* p, lua_State* L, int stack_index) {
    // Lua is one indexed.
    int i = IArg(stack_index) - 1;
    const size_t si = static_cast<size_t>(i);
    if (i < 0 || si >= p->GetNumWrapperStates()) {
      luaL_error(L, "%d is not a valid wrapper state index.", i + 1);
    }
    return si;
  }
  static int RemoveWrapperState(T* p, lua_State* L) {
    size_t si = get_state_index(p, L, 1);
    p->RemoveWrapperState(si);
    COMMON_RETURN_SELF;
  }
  static int GetNumWrapperStates(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetNumWrapperStates());
    return 1;
  }
  static int GetWrapperState(T* p, lua_State* L) {
    size_t si = get_state_index(p, L, 1);
    p->GetWrapperState(si)->PushSelf(L);
    return 1;
  }
  static int Draw(T* p, lua_State* L) {
    LUA->YieldLua();
    p->Draw();
    LUA->UnyieldLua();
    COMMON_RETURN_SELF;
  }

  static int SetRateScalingEnabled(T* p, lua_State* L) {
    p->SetRateScalingEnabled(BArg(1));
    COMMON_RETURN_SELF;
  }
  static int GetRateScalingEnabled(T* p, lua_State* L) {
    lua_pushboolean(L, p->GetRateScalingEnabled());
    return 1;
  }

  LunaActor() {
    ADD_METHOD(name);
    ADD_METHOD(sleep);
    ADD_METHOD(linear);
    ADD_METHOD(accelerate);
    ADD_METHOD(decelerate);
    ADD_METHOD(spring);
    ADD_METHOD(tween);
    ADD_METHOD(stoptweening);
    ADD_METHOD(finishtweening);
    ADD_METHOD(hurrytweening);
    ADD_METHOD(GetTweenTimeLeft);
    ADD_METHOD(x);
    ADD_METHOD(y);
    ADD_METHOD(z);
    ADD_METHOD(xy);
    ADD_METHOD(addx);
    ADD_METHOD(addy);
    ADD_METHOD(addz);
    ADD_METHOD(zoom);
    ADD_METHOD(zoomx);
    ADD_METHOD(zoomy);
    ADD_METHOD(zoomz);
    ADD_METHOD(zoomto);
    ADD_METHOD(zoomtowidth);
    ADD_METHOD(zoomtoheight);
    ADD_METHOD(setsize);
    ADD_METHOD(SetWidth);
    ADD_METHOD(SetHeight);
    ADD_METHOD(basealpha);
    ADD_METHOD(basezoom);
    ADD_METHOD(basezoomx);
    ADD_METHOD(basezoomy);
    ADD_METHOD(basezoomz);
    ADD_METHOD(stretchto);
    ADD_METHOD(cropleft);
    ADD_METHOD(croptop);
    ADD_METHOD(cropright);
    ADD_METHOD(cropbottom);
    ADD_METHOD(fadeleft);
    ADD_METHOD(fadetop);
    ADD_METHOD(faderight);
    ADD_METHOD(fadebottom);
    ADD_METHOD(diffuse);
    ADD_METHOD(diffuseupperleft);
    ADD_METHOD(diffuseupperright);
    ADD_METHOD(diffuselowerleft);
    ADD_METHOD(diffuselowerright);
    ADD_METHOD(diffuseleftedge);
    ADD_METHOD(diffuserightedge);
    ADD_METHOD(diffusetopedge);
    ADD_METHOD(diffusebottomedge);
    ADD_METHOD(diffusealpha);
    ADD_METHOD(diffusecolor);
    ADD_METHOD(glow);
    ADD_METHOD(aux);
    ADD_METHOD(getaux);
    ADD_METHOD(rotationx);
    ADD_METHOD(rotationy);
    ADD_METHOD(rotationz);
    ADD_METHOD(addrotationx);
    ADD_METHOD(addrotationy);
    ADD_METHOD(addrotationz);
    ADD_METHOD(getrotation);
    ADD_METHOD(baserotationx);
    ADD_METHOD(baserotationy);
    ADD_METHOD(baserotationz);
    ADD_METHOD(skewx);
    ADD_METHOD(skewy);
    ADD_METHOD(heading);
    ADD_METHOD(pitch);
    ADD_METHOD(roll);
    ADD_METHOD(shadowlength);
    ADD_METHOD(shadowlengthx);
    ADD_METHOD(shadowlengthy);
    ADD_METHOD(shadowcolor);
    ADD_METHOD(horizalign);
    ADD_METHOD(vertalign);
    ADD_METHOD(halign);
    ADD_METHOD(valign);
    ADD_METHOD(diffuseblink);
    ADD_METHOD(diffuseshift);
    ADD_METHOD(diffuseramp);
    ADD_METHOD(glowblink);
    ADD_METHOD(glowshift);
    ADD_METHOD(glowramp);
    ADD_METHOD(rainbow);
    ADD_METHOD(wag);
    ADD_METHOD(bounce);
    ADD_METHOD(bob);
    ADD_METHOD(pulse);
    ADD_METHOD(spin);
    ADD_METHOD(vibrate);
    ADD_METHOD(stopeffect);
    ADD_METHOD(effectcolor1);
    ADD_METHOD(effectcolor2);
    ADD_METHOD(effectperiod);
    ADD_METHOD(effecttiming);
    ADD_METHOD(effect_hold_at_full);
    ADD_METHOD(effectoffset);
    ADD_METHOD(effectclock);
    ADD_METHOD(effectmagnitude);
    ADD_METHOD(geteffectmagnitude);
    ADD_GET_SET_METHODS(tween_uses_effect_delta);
    ADD_METHOD(scaletocover);
    ADD_METHOD(scaletofit);
    ADD_METHOD(animate);
    ADD_METHOD(play);
    ADD_METHOD(pause);
    ADD_METHOD(setstate);
    ADD_METHOD(GetNumStates);
    ADD_METHOD(texturetranslate);
    ADD_METHOD(texturewrapping);
    ADD_METHOD(SetTextureFiltering);
    ADD_METHOD(blend);
    ADD_METHOD(zbuffer);
    ADD_METHOD(ztest);
    ADD_METHOD(ztestmode);
    ADD_METHOD(zwrite);
    ADD_METHOD(zbias);
    ADD_METHOD(clearzbuffer);
    ADD_METHOD(backfacecull);
    ADD_METHOD(cullmode);
    ADD_METHOD(visible);
    ADD_METHOD(hibernate);
    ADD_METHOD(draworder);
    ADD_METHOD(playcommand);
    ADD_METHOD(queuecommand);
    ADD_METHOD(queuemessage);
    ADD_METHOD(addcommand);
    ADD_METHOD(GetCommand);
    ADD_METHOD(RunCommandsRecursively);

    ADD_METHOD(GetX);
    ADD_METHOD(GetY);
    ADD_METHOD(GetZ);
    ADD_METHOD(GetDestX);
    ADD_METHOD(GetDestY);
    ADD_METHOD(GetDestZ);
    ADD_METHOD(GetWidth);
    ADD_METHOD(GetHeight);
    ADD_METHOD(GetZoomedWidth);
    ADD_METHOD(GetZoomedHeight);
    ADD_METHOD(GetZoom);
    ADD_METHOD(GetZoomX);
    ADD_METHOD(GetZoomY);
    ADD_METHOD(GetZoomZ);
    ADD_METHOD(GetRotationX);
    ADD_METHOD(GetRotationY);
    ADD_METHOD(GetRotationZ);
    ADD_METHOD(GetBaseZoomX);
    ADD_METHOD(GetBaseZoomY);
    ADD_METHOD(GetBaseZoomZ);
    ADD_METHOD(GetSecsIntoEffect);
    ADD_METHOD(GetEffectDelta);
    ADD_METHOD(GetDiffuse);
    ADD_METHOD(GetDiffuseAlpha);
    ADD_METHOD(GetGlow);
    ADD_METHOD(GetVisible);
    ADD_METHOD(GetHAlign);
    ADD_METHOD(GetVAlign);

    ADD_METHOD(GetName);
    ADD_METHOD(GetParent);
    ADD_METHOD(GetFakeParent);
    ADD_METHOD(SetFakeParent);
    ADD_METHOD(AddWrapperState);
    ADD_METHOD(RemoveWrapperState);
    ADD_METHOD(GetNumWrapperStates);
    ADD_METHOD(GetWrapperState);

    ADD_METHOD(SetRateScalingEnabled);
    ADD_METHOD(GetRateScalingEnabled);

    ADD_METHOD(Draw);
  }
};

LUA_REGISTER_INSTANCED_BASE_CLASS(Actor)
// lua end

/*
 * (c) 2001-2004 Chris Danford
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
