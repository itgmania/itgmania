#include "Sprite.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

#include "Actor.h"
#include "ActorUtil.h"
#include "EnumHelper.h"
#include "ImageCache.h"
#include "LuaBinding.h"
#include "LuaManager.h"
#include "RageDisplay.h"
#include "RageTexture.h"
#include "RageTextureID.h"
#include "RageTextureManager.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "ThemeManager.h"
#include "XmlFile.h"
#include "global.h"

REGISTER_ACTOR_CLASS(Sprite);

const float min_state_delay = 0.0001f;

Sprite::Sprite() {
  texture_ = nullptr;
  cur_state_ = 0;
  secs_into_state_ = 0.0f;
  animation_length_seconds_ = 0.0f;
  using_custom_tex_coords_ = false;
  using_custom_pos_coords_ = false;
  skip_next_update_ = true;
  decode_movie_ = false;
  effect_mode_ = EffectMode_Normal;

  remembered_clip_width_ = -1;
  remembered_clip_height_ = -1;

  tex_coord_velocity_x_ = 0;
  tex_coord_velocity_y_ = 0;
  use_effect_clock_for_texcoords_ = false;
}

// NoteSkinManager needs a sprite with a texture set to return in cases where
// the noteskin doesn't return a valid actor.  I would really prefer to make
// Sprite::Sprite load the default texture, but that causes problems for
// banners on ScreenSelectMusic and videos on ScreenGameplay.  So rather than
// dig through either of those, NoteSkinManager uses this special function.
Sprite* Sprite::NewBlankSprite() {
  Sprite* news = new Sprite;
  news->Load(TEXTUREMAN->GetDefaultTextureID());
  return news;
}

Sprite::~Sprite() { UnloadTexture(); }

Sprite::Sprite(const Sprite& cpy) : Actor(cpy) {
#define CPY(a) a = cpy.a
  CPY(states_);
  CPY(animation_length_seconds_);
  CPY(cur_state_);
  CPY(secs_into_state_);
  CPY(using_custom_tex_coords_);
  CPY(using_custom_pos_coords_);
  CPY(skip_next_update_);
  CPY(decode_movie_);
  CPY(effect_mode_);
  memcpy(
      custom_tex_coords_, cpy.custom_tex_coords_, sizeof(custom_tex_coords_));
  memcpy(
      custom_pos_coords_, cpy.custom_pos_coords_, sizeof(custom_pos_coords_));
  CPY(remembered_clip_width_);
  CPY(remembered_clip_height_);
  CPY(tex_coord_velocity_x_);
  CPY(tex_coord_velocity_y_);
  CPY(use_effect_clock_for_texcoords_);
#undef CPY

  if (cpy.texture_ != nullptr) {
    texture_ = TEXTUREMAN->CopyTexture(cpy.texture_);
  } else {
    texture_ = nullptr;
  }
}

void Sprite::InitState() {
  Actor::InitState();
  cur_state_ = 0;
  secs_into_state_ = 0.0f;
  skip_next_update_ = true;
}

void Sprite::SetAllStateDelays(float fDelay) {
  for (unsigned int i = 0; i < states_.size(); i++) {
    states_[i].delay_ = fDelay;
  }
  RecalcAnimationLengthSeconds();
}

RageTextureID Sprite::SongBGTexture(RageTextureID ID) {
  ID.mip_maps_ = true;

  /* Song backgrounds are, by definition, in the background, so there's no need
   * to keep alpha. */
  ID.alpha_bits_ = 0;

  /* By default, song graphics are volatile: they're removed after one use. This
   * is because some screens iteratively load and display lots of them (eg.
   * ScreenSelectMusic,, ScreenEditMenu) one at a time, and we don't want to
   * have hundreds of banners loaded at once. */
  ID.policy_ = RageTextureID::TEX_VOLATILE;

  ID.dither_ = true;

  return ID;
}

RageTextureID Sprite::SongBannerTexture(RageTextureID ID) {
  // Older song banners often have HOT PINK color keys.
  /* Use color keying only if the graphic is a diagonal banner.
   * The color key convention is causing small holes in moderm banners that
   * use magenta, and it's not good to require graphic makers to know about
   * archaic color key conventions. -Chris */
  // I disabled this anyways, it's extremely annoying -Colby
  ID.hot_pink_color_key_ = false;

  /* Ignore the texture color depth preference and always use 32-bit textures
   * if possible. Banners are loaded while moving the wheel, so we want it to
   * be as fast as possible. */
  ID.color_depth_ = 32;

  /* If we don't support RGBA8 (and will probably fall back on RGBA4), we're
   * probably on something very old and slow, so let's opt for banding
   * instead of slowing things down further by dithering. */
  // ID.bDither = true;

  ID.policy_ = RageTextureID::TEX_VOLATILE;

  return ID;
}

void Sprite::Load(RageTextureID ID) {
  if (!ID.filename_.empty()) {
    LoadFromTexture(ID);
  }

  LoadStatesFromTexture();
};

void Sprite::LoadFromNode(const XNode* pNode) {
  /* Texture may refer to the ID of a render target; if it's already
   * registered, use it without trying to resolve it. */
  std::string sPath;
  pNode->GetAttrValue("Texture", sPath);
  if (!sPath.empty() &&
      !TEXTUREMAN->IsTextureRegistered(RageTextureID(sPath))) {
    ActorUtil::GetAttrPath(pNode, "Texture", sPath);
  }

  if (!sPath.empty()) {
    // Load the texture
    LoadFromTexture(sPath);

    LoadStatesFromTexture();

    // Read in frames and delays from the sprite file,
    // overwriting the states that LoadFromTexture created.
    // If the .sprite file doesn't define any states, leave
    // frames and delays created during LoadFromTexture().
    std::vector<State> aStates;

    const XNode* pFrames = pNode->GetChild("Frames");
    if (pFrames != nullptr) {
      /* All attributes are optional.  If Frame is omitted, use the previous
       * state's frame (or 0 if the first). Frames = { { Delay=1.0f; Frame=0; {
       * x=0, y=0 }, { x=1, y=1 } };
       * }
       */
      int iFrameIndex = 0;
      for (int i = 0; true; i++) {
        const XNode* pFrame =
            pFrames->GetChild(ssprintf("%i", i + 1));  // +1 for Lua's arrays
        if (pFrame == nullptr) {
          break;
        }

        State newState;
        if (!pFrame->GetAttrValue("Delay", newState.delay_)) {
          newState.delay_ = 0.1f;
        }

        pFrame->GetAttrValue("Frame", iFrameIndex);
        if (iFrameIndex >= texture_->GetNumFrames()) {
          LuaHelpers::ReportScriptErrorFmt(
              "%s: State #%i is frame %d, but the texture \"%s\" only has %d "
              "frames",
              ActorUtil::GetWhere(pNode).c_str(), i + 1, iFrameIndex,
              sPath.c_str(), texture_->GetNumFrames());
        }
        newState.rect_ = *texture_->GetTextureCoordRect(iFrameIndex);

        const XNode* pPoints[2] = {
            pFrame->GetChild("1"), pFrame->GetChild("2")};
        if (pPoints[0] != nullptr && pPoints[1] != nullptr) {
          RectF r = newState.rect_;

          float fX = 1.0f, fY = 1.0f;
          pPoints[0]->GetAttrValue("x", fX);
          pPoints[0]->GetAttrValue("y", fY);
          newState.rect_.left_ = SCALE(fX, 0.0f, 1.0f, r.left_, r.right_);
          newState.rect_.top_ = SCALE(fY, 0.0f, 1.0f, r.top_, r.bottom_);

          pPoints[1]->GetAttrValue("x", fX);
          pPoints[1]->GetAttrValue("y", fY);
          newState.rect_.right_ = SCALE(fX, 0.0f, 1.0f, r.left_, r.right_);
          newState.rect_.bottom_ = SCALE(fY, 0.0f, 1.0f, r.top_, r.bottom_);
        }

        aStates.push_back(newState);
      }
    } else {
      for (int i = 0; true; i++) {
        // deprecated
        std::string sFrameKey = ssprintf("Frame%04d", i);
        std::string sDelayKey = ssprintf("Delay%04d", i);
        State newState;

        int iFrameIndex;
        if (!pNode->GetAttrValue(sFrameKey, iFrameIndex)) {
          break;
        }
        if (iFrameIndex >= texture_->GetNumFrames()) {
          LuaHelpers::ReportScriptErrorFmt(
              "%s: %s is %d, but the texture \"%s\" only has %d frames",
              ActorUtil::GetWhere(pNode).c_str(), sFrameKey.c_str(),
              iFrameIndex, sPath.c_str(), texture_->GetNumFrames());
        }

        newState.rect_ = *texture_->GetTextureCoordRect(iFrameIndex);

        if (!pNode->GetAttrValue(sDelayKey, newState.delay_)) {
          break;
        }

        aStates.push_back(newState);
      }
    }

    if (!aStates.empty()) {
      states_ = aStates;
      Sprite::size_.x_ =
          aStates[0].rect_.GetWidth() / texture_->GetSourceToTexCoordsRatioX();
      Sprite::size_.y_ =
          aStates[0].rect_.GetHeight() / texture_->GetSourceToTexCoordsRatioY();
    }
  }

  Actor::LoadFromNode(pNode);
  RecalcAnimationLengthSeconds();
}

void Sprite::UnloadTexture() {
  if (texture_ != nullptr)  // If there was a previous bitmap...
  {
    TEXTUREMAN->UnloadTexture(texture_);  // Unload it.
    texture_ = nullptr;

    /* Make sure we're reset to frame 0, so if we're reused, we aren't left
     * on a frame number that may be greater than the number of frames in
     * the newly loaded image. */
    SetState(0);
  }
}

void Sprite::EnableAnimation(bool bEnable) {
  bool bWasEnabled = is_animating_;
  Actor::EnableAnimation(bEnable);

  if (bEnable && !bWasEnabled) {
    /* When we start animating a movie, we need to discard the first update;
     * send 0 instead of the passed time. This is for two reasons:
     *
     * First, and most fundamentally, the time we'll receive on the next update
     * represents time that passed *before* the movie was started. For example,
     * 1: 20ms passes; 2: EnableAnimation(true); 3: Update(.020). We don't want
     * to send that 20ms to the texture; on the first update, the movie's time
     * should be 0.
     *
     * Second, we don't receive Update() calls if we're in a BGAnimation that
     * doesn't have focus. It looks like 1: EnableAnimation(false); 2: 30
     * seconds pass; 3: EnableAnimation(true); 4: Update(30). We must be sure
     * not to send that long 30-second update to the movie.
     *
     * (detail: the timestamps here are actually coming from
     * GetEffectDeltaTime())
     */
    skip_next_update_ = true;
  }
}

void Sprite::SetTexture(RageTexture* pTexture) {
  ASSERT(pTexture != nullptr);

  if (texture_ != pTexture) {
    UnloadTexture();
    texture_ = pTexture;
  }

  ASSERT(texture_->GetTextureWidth() >= 0);
  ASSERT(texture_->GetTextureHeight() >= 0);

  // the size of the sprite is the size of the image before it was scaled
  Sprite::size_.x_ = (float)texture_->GetSourceFrameWidth();
  Sprite::size_.y_ = (float)texture_->GetSourceFrameHeight();

  // apply clipping (if any)
  if (remembered_clip_width_ != -1 && remembered_clip_height_ != -1) {
    ScaleToClipped(remembered_clip_width_, remembered_clip_height_);
  }

  // Load default states if we haven't before.
  if (states_.empty()) {
    LoadStatesFromTexture();
  }
}

void Sprite::LoadFromTexture(RageTextureID ID) {
  // LOG->Trace( "Sprite::LoadFromTexture( %s )", ID.filename.c_str() );

  RageTexture* pTexture = nullptr;
  if (texture_ && texture_->GetID() == ID) {
    pTexture = texture_;
  } else {
    pTexture = TEXTUREMAN->LoadTexture(ID);
  }

  if (ActorUtil::GetFileType(ID.filename_) == FT_Movie) {
    decode_movie_ = true;
  }

  SetTexture(pTexture);
}

void Sprite::LoadFromCached(const std::string& sDir, const std::string& sPath) {
  if (sPath.empty()) {
    Load(THEME->GetPathG("Common", "fallback %s", sDir.c_str()));
    return;
  }

  RageTextureID ID;

  // Try to load the low quality version.
  ID = IMAGECACHE->LoadCachedImage(sDir, sPath);

  if (TEXTUREMAN->IsTextureRegistered(ID)) {
    Load(ID);
  } else if (IsAFile(sPath)) {
    Load(sPath);
  } else {
    Load(THEME->GetPathG("Common", "fallback %s", sDir.c_str()));
  }
}

void Sprite::LoadStatesFromTexture() {
  // Assume the frames of this animation play in sequential order with 0.1
  // second delay.
  states_.clear();

  if (texture_ == nullptr) {
    State newState;
    newState.delay_ = 0.1f;
    newState.rect_ = RectF(0, 0, 1, 1);
    states_.push_back(newState);
    return;
  }

  for (int i = 0; i < texture_->GetNumFrames(); ++i) {
    State newState;
    newState.delay_ = 0.1f;
    newState.rect_ = *texture_->GetTextureCoordRect(i);
    states_.push_back(newState);
  }
  RecalcAnimationLengthSeconds();
}

void Sprite::UpdateAnimationState() {
  // Don't bother with state switching logic if there's only one state.
  // We already know what's going to show.
  if (states_.size() > 1) {
    // UpdateAnimationState changed to not loop forever on negative state
    // delay.  This allows a state to last forever when it is reached, so
    // the animation has a built-in ending point. -Kyz
    while (states_[cur_state_].delay_ > min_state_delay &&
           secs_into_state_ + min_state_delay > states_[cur_state_].delay_)
    // it's time to switch frames
    {
      // increment frame and reset the counter
      secs_into_state_ -=
          states_[cur_state_]
              .delay_;  // leave the left over time for the next frame
      cur_state_ = (cur_state_ + 1) % states_.size();
      if (cur_state_ == 0) {
        PlayCommand("AnimationFinished");
      }
    }
  }
}

/* We treat frame animation and movie animation slightly differently.
 *
 * Sprite animation is always tied directly to the effect timer. If you pause
 * animation, wait a while and restart it, sprite animations will snap back to
 * the effect timer. This allows sprites to animate to the beat in BGAnimations,
 * where they might be disabled for a while.
 *
 * Movies don't do this; if you pause a movie, wait a while and restart it,
 * it'll pick up where it left off. We may have a lot of movies loaded, so
 * it's too expensive to decode movies that aren't being displayed. Movies
 * also don't loop when the effect timer loops.
 *
 * (I'd like to handle sprite and movie animation as consistently as possible;
 * the above is just documentation of current practice.) -glenn */
// todo: see if "current" practice is just that. -aj
void Sprite::Update(float fDelta) {
  Actor::Update(fDelta);  // do tweening

  const bool bSkipThisMovieUpdate = skip_next_update_;
  skip_next_update_ = false;

  if (!is_animating_) {
    return;
  }

  if (!texture_) {  // no texture, nothing to animate
    return;
  }

  float fTimePassed = GetEffectDeltaTime();
  secs_into_state_ += fTimePassed;

  if (secs_into_state_ < 0) {
    wrap(secs_into_state_, GetAnimationLengthSeconds());
  }

  UpdateAnimationState();

  // If the texture is a movie, decode frames.
  if (!bSkipThisMovieUpdate && decode_movie_) {
    texture_->UpdateMovie(std::max(0.0f, fTimePassed));
  }

  // update scrolling
  if (tex_coord_velocity_x_ != 0 || tex_coord_velocity_y_ != 0) {
    float coord_delta = fDelta;
    if (use_effect_clock_for_texcoords_) {
      coord_delta = fTimePassed;
    }
    float fTexCoords[8];
    Sprite::GetActiveTextureCoords(fTexCoords);

    // top left, bottom left, bottom right, top right
    fTexCoords[0] += coord_delta * tex_coord_velocity_x_;
    fTexCoords[1] += coord_delta * tex_coord_velocity_y_;
    fTexCoords[2] += coord_delta * tex_coord_velocity_x_;
    fTexCoords[3] += coord_delta * tex_coord_velocity_y_;
    fTexCoords[4] += coord_delta * tex_coord_velocity_x_;
    fTexCoords[5] += coord_delta * tex_coord_velocity_y_;
    fTexCoords[6] += coord_delta * tex_coord_velocity_x_;
    fTexCoords[7] += coord_delta * tex_coord_velocity_y_;

    /* When wrapping, avoid gradual loss of precision and sending
     * unreasonably large texture coordinates to the renderer by pushing
     * texture coordinates back to 0. As long as we adjust all four
     * coordinates by the same amount, this won't be visible. */
    if (texture_wrapping_) {
      const float fXAdjust = std::floor(fTexCoords[0]);
      const float fYAdjust = std::floor(fTexCoords[1]);
      fTexCoords[0] -= fXAdjust;
      fTexCoords[2] -= fXAdjust;
      fTexCoords[4] -= fXAdjust;
      fTexCoords[6] -= fXAdjust;
      fTexCoords[1] -= fYAdjust;
      fTexCoords[3] -= fYAdjust;
      fTexCoords[5] -= fYAdjust;
      fTexCoords[7] -= fYAdjust;
    }

    Sprite::SetCustomTextureCoords(fTexCoords);
  }
}

void TexCoordArrayFromRect(float fImageCoords[8], const RectF& rect) {
  fImageCoords[0] = rect.left_;
  fImageCoords[1] = rect.top_;  // top left
  fImageCoords[2] = rect.left_;
  fImageCoords[3] = rect.bottom_;  // bottom left
  fImageCoords[4] = rect.right_;
  fImageCoords[5] = rect.bottom_;  // bottom right
  fImageCoords[6] = rect.right_;
  fImageCoords[7] = rect.top_;  // top right
}

void Sprite::DrawTexture(const TweenState* state) {
  Actor::SetGlobalRenderStates();  // set Actor-specified render states

  RectF crop = state->crop_;
  // bail if cropped all the way
  if (crop.left_ + crop.right_ >= 1 || crop.top_ + crop.bottom_ >= 1) {
    return;
  }

  // use m_temp_* variables to draw the object
  RectF quadVerticies;
  quadVerticies.left_ = -size_.x_ / 2.0f;
  quadVerticies.right_ = +size_.x_ / 2.0f;
  quadVerticies.top_ = -size_.y_ / 2.0f;
  quadVerticies.bottom_ = +size_.y_ / 2.0f;

  /* Don't draw anything outside of the texture's image area.  Texels outside
   * of the image area aren't guaranteed to be initialized. */
  /* HACK: Clamp the crop values. It would be more accurate to clip the
   * vertices so that the diffuse value is adjusted. */
  rage_clamp(crop.left_, 0, 1);
  rage_clamp(crop.right_, 0, 1);
  rage_clamp(crop.top_, 0, 1);
  rage_clamp(crop.bottom_, 0, 1);

  RectF croppedQuadVerticies = quadVerticies;
#define IF_CROP_POS(side, opp_side) \
  if (state->crop_.side != 0)       \
  croppedQuadVerticies.side =       \
      SCALE(crop.side, 0.f, 1.f, quadVerticies.side, quadVerticies.opp_side)
  IF_CROP_POS(left_, right_);
  IF_CROP_POS(top_, bottom_);
  IF_CROP_POS(right_, left_);
  IF_CROP_POS(bottom_, top_);

  static RageSpriteVertex v[4];
  v[0].p_ = RageVector3(
      croppedQuadVerticies.left_, croppedQuadVerticies.top_, 0);  // top left
  v[1].p_ = RageVector3(
      croppedQuadVerticies.left_, croppedQuadVerticies.bottom_,
      0);  // bottom left
  v[2].p_ = RageVector3(
      croppedQuadVerticies.right_, croppedQuadVerticies.bottom_,
      0);  // bottom right
  v[3].p_ = RageVector3(
      croppedQuadVerticies.right_, croppedQuadVerticies.top_, 0);  // top right
  if (using_custom_pos_coords_) {
    for (int i = 0; i < 4; ++i) {
      v[i].p_.x_ += custom_pos_coords_[i * 2];
      v[i].p_.y_ += custom_pos_coords_[(i * 2) + 1];
    }
  }

  DISPLAY->ClearAllTextures();
  DISPLAY->SetTexture(TextureUnit_1, texture_ ? texture_->GetTexHandle() : 0);

  // Must call this after setting the texture or else texture
  // parameters have no effect.
  Actor::SetTextureRenderStates();  // set Actor-specified render states
  DISPLAY->SetEffectMode(effect_mode_);

  if (texture_) {
    float f[8];
    GetActiveTextureCoords(f);

    if (state->crop_.left_ || state->crop_.right_ || state->crop_.top_ ||
        state->crop_.bottom_) {
      RageVector2 texCoords[4] = {
          RageVector2(f[0], f[1]),  // top left
          RageVector2(f[2], f[3]),  // bottom left
          RageVector2(f[4], f[5]),  // bottom right
          RageVector2(f[6], f[7])   // top right
      };

      for (int i = 0; i < 4; ++i) {
        RageSpriteVertex* pVert = &v[i];

        float fTopX = SCALE(
            pVert->p_.x_, quadVerticies.left_, quadVerticies.right_,
            texCoords[0].x_, texCoords[3].x_);
        float fBottomX = SCALE(
            pVert->p_.x_, quadVerticies.left_, quadVerticies.right_,
            texCoords[1].x_, texCoords[2].x_);
        pVert->t_.x_ = SCALE(
            pVert->p_.y_, quadVerticies.top_, quadVerticies.bottom_, fTopX,
            fBottomX);

        float fLeftY = SCALE(
            pVert->p_.y_, quadVerticies.top_, quadVerticies.bottom_,
            texCoords[0].y_, texCoords[1].y_);
        float fRightY = SCALE(
            pVert->p_.y_, quadVerticies.top_, quadVerticies.bottom_,
            texCoords[3].y_, texCoords[2].y_);
        pVert->t_.y_ = SCALE(
            pVert->p_.x_, quadVerticies.left_, quadVerticies.right_, fLeftY,
            fRightY);
      }
    } else {
      v[0].t_ = RageVector2(f[0], f[1]);  // top left
      v[1].t_ = RageVector2(f[2], f[3]);  // bottom left
      v[2].t_ = RageVector2(f[4], f[5]);  // bottom right
      v[3].t_ = RageVector2(f[6], f[7]);  // top right
    }
  } else {
    // Just make sure we don't throw NaN/INF at the renderer:
    for (unsigned i = 0; i < 4; ++i) {
      v[i].t_.x_ = v[i].t_.y_ = 0;
    }
  }

  // Draw if we're not fully transparent
  if (state->diffuse_[0].a_ > 0 || state->diffuse_[1].a_ > 0 ||
      state->diffuse_[2].a_ > 0 || state->diffuse_[3].a_ > 0) {
    DISPLAY->SetTextureMode(TextureUnit_1, TextureMode_Modulate);

    // render the shadow
    if (shadow_length_x_ != 0 || shadow_length_y_ != 0) {
      DISPLAY->PushMatrix();
      DISPLAY->TranslateWorld(
          shadow_length_x_, shadow_length_y_, 0);  // shift by 5 units
      RageColor c = shadow_color_;
      c.a_ *= state->diffuse_[0].a_;
      v[0].c_ = v[1].c_ = v[2].c_ = v[3].c_ = c;  // semi-transparent black
      DISPLAY->DrawQuad(v);
      DISPLAY->PopMatrix();
    }

    // render the diffuse pass
    v[0].c_ = state->diffuse_[0];  // top left
    v[1].c_ = state->diffuse_[2];  // bottom left
    v[2].c_ = state->diffuse_[3];  // bottom right
    v[3].c_ = state->diffuse_[1];  // top right
    DISPLAY->DrawQuad(v);
  }

  // render the glow pass
  if (state->glow_.a_ > 0.0001f) {
    DISPLAY->SetTextureMode(TextureUnit_1, TextureMode_Glow);
    v[0].c_ = v[1].c_ = v[2].c_ = v[3].c_ = state->glow_;
    DISPLAY->DrawQuad(v);
  }
  DISPLAY->SetEffectMode(EffectMode_Normal);
}

bool Sprite::EarlyAbortDraw() const { return texture_ == nullptr; }

void Sprite::DrawPrimitives() {
  if (temp_state_->fade_.top_ > 0 || temp_state_->fade_.bottom_ > 0 ||
      temp_state_->fade_.left_ > 0 || temp_state_->fade_.right_ > 0) {
    // We're fading the edges.
    const RectF& FadeDist = temp_state_->fade_;

    // Actual size of the fade on each side:
    RectF FadeSize = FadeDist;

    // If the cropped size is less than the fade distance in either dimension,
    // clamp.
    const float HorizRemaining =
        1.0f - (temp_state_->crop_.left_ + temp_state_->crop_.right_);
    if (FadeDist.left_ + FadeDist.right_ > 0 &&
        HorizRemaining < FadeDist.left_ + FadeDist.right_) {
      const float LeftPercent =
          FadeDist.left_ / (FadeDist.left_ + FadeDist.right_);
      FadeSize.left_ = LeftPercent * HorizRemaining;
      FadeSize.right_ = (1.0f - LeftPercent) * HorizRemaining;
    }

    const float VertRemaining =
        1.0f - (temp_state_->crop_.top_ + temp_state_->crop_.bottom_);
    if (FadeDist.top_ + FadeDist.bottom_ > 0 &&
        VertRemaining < FadeDist.top_ + FadeDist.bottom_) {
      const float TopPercent =
          FadeDist.top_ / (FadeDist.top_ + FadeDist.bottom_);
      FadeSize.top_ = TopPercent * VertRemaining;
      FadeSize.bottom_ = (1.0f - TopPercent) * VertRemaining;
    }

    // Alpha value of the un-faded side of each fade rect:
    const float RightAlpha = SCALE(FadeSize.right_, FadeDist.right_, 0, 1, 0);
    const float LeftAlpha = SCALE(FadeSize.left_, FadeDist.left_, 0, 1, 0);
    const float TopAlpha = SCALE(FadeSize.top_, FadeDist.top_, 0, 1, 0);
    const float BottomAlpha =
        SCALE(FadeSize.bottom_, FadeDist.bottom_, 0, 1, 0);

    // Draw the inside:
    TweenState ts = *temp_state_;
    ts.crop_.left_ += FadeDist.left_;
    ts.crop_.right_ += FadeDist.right_;
    ts.crop_.top_ += FadeDist.top_;
    ts.crop_.bottom_ += FadeDist.bottom_;
    DrawTexture(&ts);

    if (FadeSize.left_ > 0.001f) {
      // Draw the left:
      ts.crop_ = temp_state_->crop_;  // restore
      ts.crop_.right_ = 1 - (ts.crop_.left_ + FadeSize.left_);
      ts.crop_.top_ +=
          FadeDist.top_;  // lop off the corner if fading both x and y
      ts.crop_.bottom_ += FadeDist.bottom_;

      ts.diffuse_[0] = temp_state_->diffuse_[0];  // top left -> top left
      ts.diffuse_[2] = temp_state_->diffuse_[2];  // bottom left -> bottom left
      ts.diffuse_[3] = temp_state_->diffuse_[2];  // bottom left -> bottom right
      ts.diffuse_[1] = temp_state_->diffuse_[0];  // top left -> top right
      ts.diffuse_[0].a_ = 0;                      // top left
      ts.diffuse_[2].a_ = 0;                      // bottom left
      ts.diffuse_[3].a_ *= LeftAlpha;             // bottom right
      ts.diffuse_[1].a_ *= LeftAlpha;             // top right
      DrawTexture(&ts);
    }

    if (FadeSize.right_ > 0.001f) {
      // Draw the right:
      ts.crop_ = temp_state_->crop_;  // restore
      ts.crop_.left_ = 1 - (ts.crop_.right_ + FadeSize.right_);
      ts.crop_.top_ += FadeDist.top_;
      ts.crop_.bottom_ += FadeDist.bottom_;

      ts.diffuse_[0] = temp_state_->diffuse_[1];  // top right -> top left
      ts.diffuse_[2] = temp_state_->diffuse_[3];  // bottom right -> bottom left
      ts.diffuse_[3] =
          temp_state_->diffuse_[3];  // bottom right -> bottom right
      ts.diffuse_[1] = temp_state_->diffuse_[1];  // top right -> top right
      ts.diffuse_[0].a_ *= RightAlpha;            // top left
      ts.diffuse_[2].a_ *= RightAlpha;            // bottom left
      ts.diffuse_[3].a_ = 0;                      // bottom right
      ts.diffuse_[1].a_ = 0;                      // top right

      DrawTexture(&ts);
    }

    if (FadeSize.top_ > 0.001f) {
      // Draw the top:
      ts.crop_ = temp_state_->crop_;  // restore
      ts.crop_.bottom_ = 1 - (ts.crop_.top_ + FadeSize.top_);
      ts.crop_.left_ += FadeDist.left_;
      ts.crop_.right_ += FadeDist.right_;

      ts.diffuse_[0] = temp_state_->diffuse_[0];  // top left -> top left
      ts.diffuse_[2] = temp_state_->diffuse_[0];  // top left -> bottom left
      ts.diffuse_[3] = temp_state_->diffuse_[1];  // top right -> bottom right
      ts.diffuse_[1] = temp_state_->diffuse_[1];  // top right -> top right
      ts.diffuse_[0].a_ = 0;                      // top left
      ts.diffuse_[2].a_ *= TopAlpha;              // bottom left
      ts.diffuse_[3].a_ *= TopAlpha;              // bottom right
      ts.diffuse_[1].a_ = 0;                      // top right

      DrawTexture(&ts);
    }

    if (FadeSize.bottom_ > 0.001f) {
      // Draw the bottom:
      ts.crop_ = temp_state_->crop_;  // restore
      ts.crop_.top_ = 1 - (ts.crop_.bottom_ + FadeSize.bottom_);
      ts.crop_.left_ += FadeDist.left_;
      ts.crop_.right_ += FadeDist.right_;

      ts.diffuse_[0] = temp_state_->diffuse_[2];  // bottom left -> top left
      ts.diffuse_[2] = temp_state_->diffuse_[2];  // bottom left -> bottom left
      ts.diffuse_[3] =
          temp_state_->diffuse_[3];  // bottom right -> bottom right
      ts.diffuse_[1] = temp_state_->diffuse_[3];  // bottom right -> top right
      ts.diffuse_[0].a_ *= BottomAlpha;           // top left
      ts.diffuse_[2].a_ = 0;                      // bottom left
      ts.diffuse_[3].a_ = 0;                      // bottom right
      ts.diffuse_[1].a_ *= BottomAlpha;           // top right
      DrawTexture(&ts);
    }
  } else {
    DrawTexture(temp_state_);
  }
}

int Sprite::GetNumStates() const { return states_.size(); }

void Sprite::SetState(int iNewState) {
  /*
   * This assert will likely trigger if the "missing" theme element graphic
   * is loaded in place of a multi-frame sprite. We want to know about these
   * problems in debug builds, but they're not fatal.
   */

  // Never warn about setting state 0.
  if (iNewState != 0 && (iNewState < 0 || iNewState >= (int)states_.size())) {
    // Don't warn about number of states in "_blank" or "_missing".
    if (!texture_ ||
        (texture_->GetID().filename_.find("_blank") == std::string::npos &&
         texture_->GetID().filename_.find("_missing") == std::string::npos)) {
      std::string sError;
      if (texture_) {
        sError = ssprintf(
            "A Sprite '%s' (\"%s\") tried to set state to frame %d, but it has "
            "only %u frames.",
            /*
             * Using the state directly tends to give you an error message like
             * "tried to set frame 6 of 6" which is very confusing if you don't
             * know that one is 0-indexed and the other is 1-indexed.
             * - Colby
             */
            texture_->GetID().filename_.c_str(), this->name_.c_str(),
            iNewState + 1, unsigned(states_.size()));
      } else {
        sError = ssprintf(
            "A Sprite (\"%s\") tried to set state index %d, but no texture is "
            "loaded.",
            this->name_.c_str(), iNewState);
      }
      LuaHelpers::ReportScriptError(sError, "SPRITE_INVALID_FRAME");
    }
  }

  rage_clamp(iNewState, 0, (int)states_.size() - 1);
  cur_state_ = iNewState;
  secs_into_state_ = 0.0f;
}

void Sprite::RecalcAnimationLengthSeconds() {
  animation_length_seconds_ = 0;
  for (const State& s : states_) {
    animation_length_seconds_ += s.delay_;
  }
}

void Sprite::SetSecondsIntoAnimation(float fSeconds) {
  SetState(0);  // rewind to the first state
  if (texture_) {
    texture_->SetPosition(fSeconds);
  }
  secs_into_state_ = fSeconds;
  UpdateAnimationState();
}

std::string Sprite::GetTexturePath() const {
  if (texture_ == nullptr) {
    return std::string();
  }

  return texture_->GetID().filename_;
}

void Sprite::SetCustomTextureRect(const RectF& new_texcoord_frect) {
  using_custom_tex_coords_ = true;
  texture_wrapping_ = true;
  TexCoordArrayFromRect(custom_tex_coords_, new_texcoord_frect);
}

void Sprite::SetCustomTextureCoords(
    float
        fTexCoords[8])  // order: top left, bottom left, bottom right, top right
{
  using_custom_tex_coords_ = true;
  texture_wrapping_ = true;
  for (int i = 0; i < 8; i++) {
    custom_tex_coords_[i] = fTexCoords[i];
  }
}

void Sprite::SetCustomImageRect(RectF rectImageCoords) {
  // Convert to a rectangle in texture coordinate space.
  rectImageCoords.left_ *=
      texture_->GetImageWidth() / (float)texture_->GetTextureWidth();
  rectImageCoords.right_ *=
      texture_->GetImageWidth() / (float)texture_->GetTextureWidth();
  rectImageCoords.top_ *=
      texture_->GetImageHeight() / (float)texture_->GetTextureHeight();
  rectImageCoords.bottom_ *=
      texture_->GetImageHeight() / (float)texture_->GetTextureHeight();

  SetCustomTextureRect(rectImageCoords);
}

void Sprite::SetCustomPosCoords(
    float
        fPosCoords[8])  // order: top left, bottom left, bottom right, top right
{
  using_custom_pos_coords_ = true;
  for (int i = 0; i < 8; ++i) {
    custom_pos_coords_[i] = fPosCoords[i];
  }
}

const RectF* Sprite::GetCurrentTextureCoordRect() const {
  return GetTextureCoordRectForState(cur_state_);
}

const RectF* Sprite::GetTextureCoordRectForState(int iState) const {
  ASSERT_M(
      iState < (int)states_.size(),
      ssprintf("%d, %d", int(iState), int(states_.size())));

  return &states_[iState].rect_;
}

/* If we're using custom coordinates, return them; otherwise return the
 * coordinates for the current state. */
void Sprite::GetActiveTextureCoords(float fTexCoordsOut[8]) const {
  if (using_custom_tex_coords_) {
    // GetCustomTextureCoords
    for (int i = 0; i < 8; i++) {
      fTexCoordsOut[i] = custom_tex_coords_[i];
    }
  } else {
    // GetCurrentTextureCoords
    const RectF* pTexCoordRect = GetCurrentTextureCoordRect();
    TexCoordArrayFromRect(fTexCoordsOut, *pTexCoordRect);
  }
}

void Sprite::StopUsingCustomCoords() { using_custom_tex_coords_ = false; }

void Sprite::StopUsingCustomPosCoords() { using_custom_pos_coords_ = false; }

void Sprite::SetTexCoordVelocity(float fVelX, float fVelY) {
  tex_coord_velocity_x_ = fVelX;
  tex_coord_velocity_y_ = fVelY;
}

void Sprite::ScaleToClipped(float fWidth, float fHeight) {
  remembered_clip_width_ = fWidth;
  remembered_clip_height_ = fHeight;

  if (!texture_) {
    return;
  }

  float fScaleFudgePercent =
      0.15f;  // scale up to this amount in one dimension to avoid clipping.

  // save the original X and Y.  We're going to restore them later.
  float fOriginalX = GetX();
  float fOriginalY = GetY();

  if (fWidth != -1 && fHeight != -1) {
    // this is probably a background graphic or something not intended to be a
    // CroppedSprite
    Sprite::StopUsingCustomCoords();

    // first find the correct zoom
    Sprite::ScaleToCover(RectF(0, 0, fWidth, fHeight));
    // find which dimension is larger
    bool bXDimNeedsToBeCropped = GetZoomedWidth() > fWidth + 0.01;

    if (bXDimNeedsToBeCropped)  // crop X
    {
      float fPercentageToCutOff =
          (this->GetZoomedWidth() - fWidth) / this->GetZoomedWidth();
      fPercentageToCutOff =
          std::max(fPercentageToCutOff - fScaleFudgePercent, 0.0f);
      float fPercentageToCutOffEachSide = fPercentageToCutOff / 2;

      // generate a rectangle with new texture coordinates
      RectF fCustomImageRect(
          fPercentageToCutOffEachSide, 0, 1 - fPercentageToCutOffEachSide, 1);
      SetCustomImageRect(fCustomImageRect);
    } else  // crop Y
    {
      float fPercentageToCutOff =
          (this->GetZoomedHeight() - fHeight) / this->GetZoomedHeight();
      fPercentageToCutOff =
          std::max(fPercentageToCutOff - fScaleFudgePercent, 0.0f);
      float fPercentageToCutOffEachSide = fPercentageToCutOff / 2;

      // generate a rectangle with new texture coordinates
      RectF fCustomImageRect(
          0, fPercentageToCutOffEachSide, 1, 1 - fPercentageToCutOffEachSide);
      SetCustomImageRect(fCustomImageRect);
    }
    size_ = RageVector2(fWidth, fHeight);
    SetZoom(1);
  }

  // restore original XY
  SetXY(fOriginalX, fOriginalY);
}

// magic hurr
// This code should either be removed or refactored in the future -aj
void Sprite::CropTo(float fWidth, float fHeight) {
  remembered_clip_width_ = fWidth;
  remembered_clip_height_ = fHeight;

  if (!texture_) {
    return;
  }

  // save the original X&Y.  We're going to restore them later.
  float fOriginalX = GetX();
  float fOriginalY = GetY();

  if (fWidth != -1 && fHeight != -1) {
    // this is probably a background graphic or something not intended to be a
    // CroppedSprite
    Sprite::StopUsingCustomCoords();

    // first find the correct zoom
    Sprite::ScaleToCover(RectF(0, 0, fWidth, fHeight));
    // find which dimension is larger
    bool bXDimNeedsToBeCropped = GetZoomedWidth() > fWidth + 0.01;

    if (bXDimNeedsToBeCropped)  // crop X
    {
      float fPercentageToCutOff =
          (this->GetZoomedWidth() - fWidth) / this->GetZoomedWidth();
      float fPercentageToCutOffEachSide = fPercentageToCutOff / 2;

      // generate a rectangle with new texture coordinates
      RectF fCustomImageRect(
          fPercentageToCutOffEachSide, 0, 1 - fPercentageToCutOffEachSide, 1);
      SetCustomImageRect(fCustomImageRect);
    } else  // crop Y
    {
      float fPercentageToCutOff =
          (this->GetZoomedHeight() - fHeight) / this->GetZoomedHeight();
      float fPercentageToCutOffEachSide = fPercentageToCutOff / 2;

      // generate a rectangle with new texture coordinates
      RectF fCustomImageRect(
          0, fPercentageToCutOffEachSide, 1, 1 - fPercentageToCutOffEachSide);
      SetCustomImageRect(fCustomImageRect);
    }
    size_ = RageVector2(fWidth, fHeight);
    SetZoom(1);
  }

  // restore original XY
  SetXY(fOriginalX, fOriginalY);
}
// end magic

void Sprite::StretchTexCoords(float fX, float fY) {
  float fTexCoords[8];
  GetActiveTextureCoords(fTexCoords);

  for (int j = 0; j < 8; j += 2) {
    fTexCoords[j] += fX;
    fTexCoords[j + 1] += fY;
  }

  SetCustomTextureCoords(fTexCoords);
}

void Sprite::AddImageCoords(float fX, float fY) {
  float fTexCoords[8];
  GetActiveTextureCoords(fTexCoords);

  for (int j = 0; j < 8; j += 2) {
    fTexCoords[j] += fX / (float)texture_->GetTextureWidth();
    fTexCoords[j + 1] += fY / (float)texture_->GetTextureHeight();
  }

  SetCustomTextureCoords(fTexCoords);
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the Sprite. */
class LunaSprite : public Luna<Sprite> {
 public:
  static int Load(T* p, lua_State* L) {
    if (lua_isnil(L, 1)) {
      p->UnloadTexture();
    } else {
      RageTextureID ID(SArg(1));
      if (lua_isstring(L, 2)) {
        std::string additional_hints = SArg(2);
        ID.additional_texture_hints_ = additional_hints;
      }
      p->Load(ID);
    }
    COMMON_RETURN_SELF;
  }
  static int LoadBackground(T* p, lua_State* L) {
    RageTextureID ID(SArg(1));
    TEXTUREMAN->DisableOddDimensionWarning();
    p->Load(Sprite::SongBGTexture(ID));
    TEXTUREMAN->EnableOddDimensionWarning();
    return 1;
  }
  static int LoadBanner(T* p, lua_State* L) {
    RageTextureID ID(SArg(1));
    TEXTUREMAN->DisableOddDimensionWarning();
    p->Load(Sprite::SongBannerTexture(ID));
    TEXTUREMAN->EnableOddDimensionWarning();
    return 1;
  }

  /* Commands that go in the tweening queue:
   * Commands that take effect immediately (ignoring the tweening queue): */
  static int customtexturerect(T* p, lua_State* L) {
    p->SetCustomTextureRect(RectF(FArg(1), FArg(2), FArg(3), FArg(4)));
    COMMON_RETURN_SELF;
  }
  static int SetCustomImageRect(T* p, lua_State* L) {
    p->SetCustomImageRect(RectF(FArg(1), FArg(2), FArg(3), FArg(4)));
    COMMON_RETURN_SELF;
  }
  static int SetCustomPosCoords(T* p, lua_State* L) {
    float coords[8];
    for (int i = 0; i < 8; ++i) {
      coords[i] = FArg(i + 1);
      if (std::isnan(coords[i])) {
        coords[i] = 0.0f;
      }
    }
    p->SetCustomPosCoords(coords);
    COMMON_RETURN_SELF;
  }
  static int StopUsingCustomPosCoords(T* p, lua_State* L) {
    p->StopUsingCustomPosCoords();
    COMMON_RETURN_SELF;
  }
  static int texcoordvelocity(T* p, lua_State* L) {
    p->SetTexCoordVelocity(FArg(1), FArg(2));
    COMMON_RETURN_SELF;
  }
  static int get_use_effect_clock_for_texcoords(T* p, lua_State* L) {
    lua_pushboolean(L, p->use_effect_clock_for_texcoords_);
    return 1;
  }
  static int set_use_effect_clock_for_texcoords(T* p, lua_State* L) {
    p->use_effect_clock_for_texcoords_ = BArg(1);
    COMMON_RETURN_SELF;
  }
  static int scaletoclipped(T* p, lua_State* L) {
    p->ScaleToClipped(FArg(1), FArg(2));
    COMMON_RETURN_SELF;
  }
  static int CropTo(T* p, lua_State* L) {
    p->CropTo(FArg(1), FArg(2));
    COMMON_RETURN_SELF;
  }
  static int stretchtexcoords(T* p, lua_State* L) {
    p->StretchTexCoords(FArg(1), FArg(2));
    COMMON_RETURN_SELF;
  }
  static int addimagecoords(T* p, lua_State* L) {
    p->AddImageCoords(FArg(1), FArg(2));
    COMMON_RETURN_SELF;
  }
  static int setstate(T* p, lua_State* L) {
    p->SetState(IArg(1));
    COMMON_RETURN_SELF;
  }
  static int GetState(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetState());
    return 1;
  }
  static int SetStateProperties(T* p, lua_State* L) {
    // States table example:
    // {{Frame= 0, Delay= .016, {0, 0}, {.25, .25}}}
    // Each element in the States table must have a Frame and a Delay.
    // Frame is optional, defaulting to 0.
    // Delay is optional, defaulting to 0.
    // The two tables in the state are the upper left and lower right and are
    // optional.
    if (!lua_istable(L, 1)) {
      luaL_error(L, "State properties must be in a table.");
    }
    std::vector<Sprite::State> new_states;
    size_t num_states = lua_objlen(L, 1);
    if (num_states == 0) {
      luaL_error(L, "A Sprite cannot have zero states.");
    }
    for (size_t s = 0; s < num_states; ++s) {
      Sprite::State new_state;
      lua_rawgeti(L, 1, s + 1);
      lua_getfield(L, -1, "Frame");
      int frame_index = 0;
      if (lua_isnumber(L, -1)) {
        frame_index = IArg(-1);
        if (frame_index < 0 || frame_index >= p->GetTexture()->GetNumFrames()) {
          luaL_error(
              L, "Frame index out of range 0-%d.",
              p->GetTexture()->GetNumFrames() - 1);
        }
      }
      new_state.rect_ = *p->GetTexture()->GetTextureCoordRect(frame_index);
      lua_pop(L, 1);
      lua_getfield(L, -1, "Delay");
      if (lua_isnumber(L, -1)) {
        new_state.delay_ = FArg(-1);
      }
      lua_pop(L, 1);
      RectF r = new_state.rect_;
      lua_rawgeti(L, -1, 1);
      if (lua_istable(L, -1)) {
        lua_rawgeti(L, -1, 1);
        // I have no idea why the points are from 0 to 1 and make it use only
        // a portion of the state.  This is just copied from LoadFromNode.
        // -Kyz
        new_state.rect_.left_ = SCALE(FArg(-1), 0.0f, 1.0f, r.left_, r.right_);
        lua_pop(L, 1);
        lua_rawgeti(L, -1, 2);
        new_state.rect_.top_ = SCALE(FArg(-1), 0.0f, 1.0f, r.top_, r.bottom_);
        lua_pop(L, 1);
      }
      lua_pop(L, 1);
      lua_rawgeti(L, -1, 2);
      if (lua_istable(L, -1)) {
        lua_rawgeti(L, -1, 1);
        // I have no idea why the points are from 0 to 1 and make it use only
        // a portion of the state.  This is just copied from LoadFromNode.
        // -Kyz
        new_state.rect_.right_ = SCALE(FArg(-1), 0.0f, 1.0f, r.left_, r.right_);
        lua_pop(L, 1);
        lua_rawgeti(L, -1, 2);
        new_state.rect_.bottom_ =
            SCALE(FArg(-1), 0.0f, 1.0f, r.top_, r.bottom_);
        lua_pop(L, 1);
      }
      lua_pop(L, 1);
      new_states.push_back(new_state);
      lua_pop(L, 1);
    }
    p->SetStateProperties(new_states);
    COMMON_RETURN_SELF;
  }
  static int GetAnimationLengthSeconds(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetAnimationLengthSeconds());
    return 1;
  }
  static int SetSecondsIntoAnimation(T* p, lua_State* L) {
    p->SetSecondsIntoAnimation(FArg(0));
    COMMON_RETURN_SELF;
  }
  static int SetTexture(T* p, lua_State* L) {
    RageTexture* pTexture = Luna<RageTexture>::check(L, 1);
    pTexture = TEXTUREMAN->CopyTexture(pTexture);
    p->SetTexture(pTexture);
    COMMON_RETURN_SELF;
  }
  static int GetTexture(T* p, lua_State* L) {
    RageTexture* pTexture = p->GetTexture();
    if (pTexture != nullptr) {
      pTexture->PushSelf(L);
    } else {
      lua_pushnil(L);
    }
    return 1;
  }
  static int SetEffectMode(T* p, lua_State* L) {
    EffectMode em = Enum::Check<EffectMode>(L, 1);
    p->SetEffectMode(em);
    COMMON_RETURN_SELF;
  }
  static int GetNumStates(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetNumStates());
    return 1;
  }
  static int SetAllStateDelays(T* p, lua_State* L) {
    p->SetAllStateDelays(FArg(-1));
    COMMON_RETURN_SELF;
  }
  DEFINE_METHOD(GetDecodeMovie, decode_movie_);
  static int SetDecodeMovie(T* p, lua_State* L) {
    p->decode_movie_ = BArg(1);
    COMMON_RETURN_SELF;
  }
  static int LoadFromCached(T* p, lua_State* L) {
    p->LoadFromCached(SArg(1), SArg(2));
    COMMON_RETURN_SELF;
  }

  LunaSprite() {
    ADD_METHOD(Load);
    ADD_METHOD(LoadBanner);
    ADD_METHOD(LoadBackground);
    ADD_METHOD(LoadFromCached);
    ADD_METHOD(customtexturerect);
    ADD_METHOD(SetCustomImageRect);
    ADD_METHOD(SetCustomPosCoords);
    ADD_METHOD(StopUsingCustomPosCoords);
    ADD_METHOD(texcoordvelocity);
    ADD_METHOD(get_use_effect_clock_for_texcoords);
    ADD_METHOD(set_use_effect_clock_for_texcoords);
    ADD_METHOD(scaletoclipped);
    ADD_METHOD(CropTo);
    ADD_METHOD(stretchtexcoords);
    ADD_METHOD(addimagecoords);
    ADD_METHOD(setstate);
    ADD_METHOD(GetState);
    ADD_METHOD(SetStateProperties);
    ADD_METHOD(GetAnimationLengthSeconds);
    ADD_METHOD(SetSecondsIntoAnimation);
    ADD_METHOD(SetTexture);
    ADD_METHOD(GetTexture);
    ADD_METHOD(SetEffectMode);
    ADD_METHOD(GetNumStates);
    ADD_METHOD(SetAllStateDelays);
    ADD_METHOD(GetDecodeMovie);
    ADD_METHOD(SetDecodeMovie);
  }
};

LUA_REGISTER_DERIVED_CLASS(Sprite, Actor)
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
