#include "ActorFrameTexture.h"

#include <cinttypes>
#include <cstdint>

#include "ActorFrame.h"
#include "ActorUtil.h"
#include "LuaManager.h"
#include "RageDisplay.h"
#include "RageTexture.h"
#include "RageTextureID.h"
#include "RageTextureManager.h"
#include "RageTextureRenderTarget.h"
#include "RageUtil.h"
#include "global.h"

REGISTER_ACTOR_CLASS_WITH_NAME(
    ActorFrameTextureAutoDeleteChildren, ActorFrameTexture);
ActorFrameTexture* ActorFrameTexture::Copy() const {
  return new ActorFrameTexture(*this);
}

ActorFrameTexture::ActorFrameTexture() {
  depth_buffer_ = false;
  alpha_buffer_ = false;
  float_ = false;
  preserve_texture_ = false;
  static uint64_t i = 0;
  ++i;
  texture_name_ = ssprintf("ActorFrameTexture %" PRIu64, i);

  render_target_ = nullptr;
}

ActorFrameTexture::ActorFrameTexture(const ActorFrameTexture& cpy)
    : ActorFrame(cpy) {
  FAIL_M("ActorFrameTexture copy not implemented");
}

ActorFrameTexture::~ActorFrameTexture() {
  /* Release our reference to the texture. */
  TEXTUREMAN->UnloadTexture(render_target_);
}

void ActorFrameTexture::Create() {
  if (render_target_ != nullptr) {
    LuaHelpers::ReportScriptError(
        "Can't Create an already created ActorFrameTexture");
    return;
  }

  if (TEXTUREMAN->IsTextureRegistered(texture_name_)) {
    LuaHelpers::ReportScriptError(
        "ActorFrameTexture: Texture Name already in use.");
    return;
  }

  if ((int)size_.x_ < 1 || (int)size_.y_ < 1) {
    LuaHelpers::ReportScriptError(
        "ActorFrameTexture: Cannot have width or height less than 1");
    return;
  }
  RageTextureID id(texture_name_);
  id.policy_ = RageTextureID::TEX_VOLATILE;

  RenderTargetParam param;
  param.with_depth_buffer_ = depth_buffer_;
  param.with_alpha_ = alpha_buffer_;
  param.float_ = float_;
  param.width_ = (int)size_.x_;
  param.height_ = (int)size_.y_;
  render_target_ = new RageTextureRenderTarget(id, param);
  render_target_->was_used_ = true;

  /* This passes ownership of m_pRenderTarget to TEXTUREMAN, but we retain
   * our reference to it until we call TEXTUREMAN->UnloadTexture. */
  TEXTUREMAN->RegisterTexture(id, render_target_);
}

void ActorFrameTexture::DrawPrimitives() {
  if (render_target_ == nullptr) {
    return;
  }

  render_target_->BeginRenderingTo(preserve_texture_);

  ActorFrame::DrawPrimitives();

  render_target_->FinishRenderingTo();
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the ActorFrameTexture. */
class LunaActorFrameTexture : public Luna<ActorFrameTexture> {
 public:
  static int Create(T* p, lua_State* L) {
    p->Create();
    COMMON_RETURN_SELF;
  }
  static int EnableDepthBuffer(T* p, lua_State* L) {
    p->EnableDepthBuffer(BArg(1));
    COMMON_RETURN_SELF;
  }
  static int EnableAlphaBuffer(T* p, lua_State* L) {
    p->EnableAlphaBuffer(BArg(1));
    COMMON_RETURN_SELF;
  }
  static int EnableFloat(T* p, lua_State* L) {
    p->EnableFloat(BArg(1));
    COMMON_RETURN_SELF;
  }
  static int EnablePreserveTexture(T* p, lua_State* L) {
    p->EnablePreserveTexture(BArg(1));
    COMMON_RETURN_SELF;
  }
  static int SetTextureName(T* p, lua_State* L) {
    p->SetTextureName(SArg(1));
    COMMON_RETURN_SELF;
  }
  static int GetTexture(T* p, lua_State* L) {
    RageTexture* texture = p->GetTexture();
    if (texture == nullptr) {
      lua_pushnil(L);
    } else {
      texture->PushSelf(L);
    }
    return 1;
  }

  LunaActorFrameTexture() {
    ADD_METHOD(Create);
    ADD_METHOD(EnableDepthBuffer);
    ADD_METHOD(EnableAlphaBuffer);
    ADD_METHOD(EnableFloat);
    ADD_METHOD(EnablePreserveTexture);
    ADD_METHOD(SetTextureName);
    ADD_METHOD(GetTexture);
  }
};

LUA_REGISTER_DERIVED_CLASS(ActorFrameTexture, ActorFrame)
// lua end

/*
 * (c) 2006 Glenn Maynard
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
