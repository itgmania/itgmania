#include "ActorMultiTexture.h"

#include <cstddef>

#include "Actor.h"
#include "ActorUtil.h"
#include "EnumHelper.h"
#include "LuaBinding.h"
#include "LuaManager.h"
#include "RageDisplay.h"
#include "RageLog.h"
#include "RageTexture.h"
#include "RageTextureManager.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "XmlFile.h"

REGISTER_ACTOR_CLASS(ActorMultiTexture);

ActorMultiTexture::ActorMultiTexture() { effect_mode_ = EffectMode_Normal; }

ActorMultiTexture::~ActorMultiTexture() { ClearTextures(); }

ActorMultiTexture::ActorMultiTexture(const ActorMultiTexture& cpy)
    : Actor(cpy) {
#define CPY(a) a = cpy.a
  CPY(rect_);
  CPY(texture_units_);
#undef CPY

  for (TextureUnitState& tex : texture_units_) {
    tex.texture_ = TEXTUREMAN->CopyTexture(tex.texture_);
  }
}

void ActorMultiTexture::SetTextureCoords(const RectF& r) { rect_ = r; }

void ActorMultiTexture::LoadFromNode(const XNode* pNode) {
  rect_ = RectF(0, 0, 1, 1);
  Actor::LoadFromNode(pNode);
}

void ActorMultiTexture::SetSizeFromTexture(RageTexture* pTexture) {
  ActorMultiTexture::size_.x_ = pTexture->GetSourceWidth();
  ActorMultiTexture::size_.y_ = pTexture->GetSourceHeight();
}

void ActorMultiTexture::ClearTextures() {
  for (TextureUnitState& tex : texture_units_) {
    TEXTUREMAN->UnloadTexture(tex.texture_);
  }
  texture_units_.clear();
}

int ActorMultiTexture::AddTexture(RageTexture* pTexture) {
  if (pTexture == nullptr) {
    LOG->Warn("Can't add nil texture to ActorMultiTexture");
    return texture_units_.size();
  }
  LOG->Trace(
      "ActorMultiTexture::AddTexture( %s )",
      pTexture->GetID().filename_.c_str());

  texture_units_.push_back(TextureUnitState());
  texture_units_.back().texture_ = TEXTUREMAN->CopyTexture(pTexture);
  return texture_units_.size();
}

void ActorMultiTexture::SetTextureMode(int iIndex, TextureMode tm) {
  if (iIndex >= (int)texture_units_.size()) {
    LOG->Warn("Can't set texture mode, index %d too high.", iIndex);
    return;
  }
  texture_units_[iIndex].texture_mode_ = tm;
}

void ActorMultiTexture::DrawPrimitives() {
  Actor::SetGlobalRenderStates();  // set Actor-specified render states

  RectF quadVerticies;
  quadVerticies.left_ = -size_.x_ / 2.0f;
  quadVerticies.right_ = +size_.x_ / 2.0f;
  quadVerticies.top_ = -size_.y_ / 2.0f;
  quadVerticies.bottom_ = +size_.y_ / 2.0f;

  DISPLAY->ClearAllTextures();
  for (size_t i = 0; i < texture_units_.size(); ++i) {
    TextureUnit tu = enum_add2(TextureUnit_1, i);
    DISPLAY->SetTexture(tu, texture_units_[i].texture_->GetTexHandle());
    DISPLAY->SetTextureWrapping(tu, texture_wrapping_);
    DISPLAY->SetTextureMode(tu, texture_units_[i].texture_mode_);
  }

  DISPLAY->SetEffectMode(effect_mode_);

  static RageSpriteVertex v[4];
  v[0].p_ =
      RageVector3(quadVerticies.left_, quadVerticies.top_, 0);  // top left
  v[1].p_ = RageVector3(
      quadVerticies.left_, quadVerticies.bottom_, 0);  // bottom left
  v[2].p_ = RageVector3(
      quadVerticies.right_, quadVerticies.bottom_, 0);  // bottom right
  v[3].p_ =
      RageVector3(quadVerticies.right_, quadVerticies.top_, 0);  // top right

  const RectF* pTexCoordRect = &rect_;
  v[0].t_ = RageVector2(pTexCoordRect->left_, pTexCoordRect->top_);  // top left
  v[1].t_ =
      RageVector2(pTexCoordRect->left_, pTexCoordRect->bottom_);  // bottom left
  v[2].t_ = RageVector2(
      pTexCoordRect->right_, pTexCoordRect->bottom_);  // bottom right
  v[3].t_ =
      RageVector2(pTexCoordRect->right_, pTexCoordRect->top_);  // top right

  v[0].c_ = temp_state_->diffuse_[0];  // top left
  v[1].c_ = temp_state_->diffuse_[2];  // bottom left
  v[2].c_ = temp_state_->diffuse_[3];  // bottom right
  v[3].c_ = temp_state_->diffuse_[1];  // top right

  DISPLAY->DrawQuad(v);

  for (size_t i = 0; i < texture_units_.size(); ++i) {
    DISPLAY->SetTexture(enum_add2(TextureUnit_1, i), 0);
  }

  DISPLAY->SetEffectMode(EffectMode_Normal);
}

bool ActorMultiTexture::EarlyAbortDraw() const {
  return texture_units_.empty();
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the ActorMultiTexture. */
class LunaActorMultiTexture : public Luna<ActorMultiTexture> {
 public:
  static int ClearTextures(T* p, lua_State* L) {
    p->ClearTextures();
    COMMON_RETURN_SELF;
  }
  static int AddTexture(T* p, lua_State* L) {
    RageTexture* pTexture = Luna<RageTexture>::check(L, 1);
    int iRet = p->AddTexture(pTexture);
    lua_pushinteger(L, iRet);
    return 1;
  }
  static int SetTextureMode(T* p, lua_State* L) {
    int iIndex = IArg(1);
    TextureMode tm = Enum::Check<TextureMode>(L, 2);
    p->SetTextureMode(iIndex, tm);
    COMMON_RETURN_SELF;
  }
  static int SetTextureCoords(T* p, lua_State* L) {
    p->SetTextureCoords(RectF(FArg(1), FArg(2), FArg(3), FArg(4)));
    COMMON_RETURN_SELF;
  }
  static int SetSizeFromTexture(T* p, lua_State* L) {
    RageTexture* pTexture = Luna<RageTexture>::check(L, 1);
    p->SetSizeFromTexture(pTexture);
    COMMON_RETURN_SELF;
  }
  static int SetEffectMode(T* p, lua_State* L) {
    EffectMode em = Enum::Check<EffectMode>(L, 1);
    p->SetEffectMode(em);
    COMMON_RETURN_SELF;
  }

  LunaActorMultiTexture() {
    ADD_METHOD(ClearTextures);
    ADD_METHOD(AddTexture);
    ADD_METHOD(SetTextureMode);
    ADD_METHOD(SetTextureCoords);
    ADD_METHOD(SetSizeFromTexture);
    ADD_METHOD(SetEffectMode);
  }
};

LUA_REGISTER_DERIVED_CLASS(ActorMultiTexture, Actor)
// lua end

/*
 * (c) 2001-2007 Glenn Maynard, Chris Danford
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
