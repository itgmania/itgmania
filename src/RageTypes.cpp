#include "RageTypes.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "EnumHelper.h"
#include "LuaManager.h"
#include "RageUtil.h"

void RageColor::PushTable(lua_State* L) const {
  lua_newtable(L);
  int iTable = lua_gettop(L);

  lua_pushnumber(L, r_);
  lua_rawseti(L, iTable, 1);
  lua_pushnumber(L, g_);
  lua_rawseti(L, iTable, 2);
  lua_pushnumber(L, b_);
  lua_rawseti(L, iTable, 3);
  lua_pushnumber(L, a_);
  lua_rawseti(L, iTable, 4);
}

void RageColor::FromStack(lua_State* L, int iPos) {
  if (lua_type(L, iPos) != LUA_TTABLE) {
    return;
  }

  lua_pushvalue(L, iPos);
  int iFrom = lua_gettop(L);

  lua_rawgeti(L, iFrom, 1);
  r_ = (float)lua_tonumber(L, -1);
  lua_rawgeti(L, iFrom, 2);
  g_ = (float)lua_tonumber(L, -1);
  lua_rawgeti(L, iFrom, 3);
  b_ = (float)lua_tonumber(L, -1);
  lua_rawgeti(L, iFrom, 4);
  a_ = (float)lua_tonumber(L, -1);
  lua_pop(L, 5);
}

void RageColor::FromStackCompat(lua_State* L, int iPos) {
  if (lua_type(L, iPos) == LUA_TTABLE) {
    FromStack(L, iPos);
  } else {
    r_ = FArg(iPos + 0);
    g_ = FArg(iPos + 1);
    b_ = FArg(iPos + 2);
    a_ = FArg(iPos + 3);
  }
}

std::string RageColor::ToString() const {
  int iR = std::clamp(static_cast<int>(std::lrint(r_ * 255)), 0, 255);
  int iG = std::clamp(static_cast<int>(std::lrint(g_ * 255)), 0, 255);
  int iB = std::clamp(static_cast<int>(std::lrint(b_ * 255)), 0, 255);
  int iA = std::clamp(static_cast<int>(std::lrint(a_ * 255)), 0, 255);

  if (iA == 255) {
    return ssprintf("#%02X%02X%02X", iR, iG, iB);
  } else {
    return ssprintf("#%02X%02X%02X%02X", iR, iG, iB, iA);
  }
}

std::string RageColor::NormalizeColorString(std::string sColor) {
  if (sColor.empty()) {
    return "";
  }
  RageColor c;
  if (!c.FromString(sColor)) {
    return "";
  }
  return c.ToString();
}

void lerp_rage_color(
    RageColor& out, const RageColor& a, const RageColor& b, float t) {
  out.b_ = lerp(t, a.b_, b.b_);
  out.g_ = lerp(t, a.g_, b.g_);
  out.r_ = lerp(t, a.r_, b.r_);
  out.a_ = lerp(t, a.a_, b.a_);
}

void WeightedAvergeOfRSVs(
    RageSpriteVertex& average_out, const RageSpriteVertex& rsv1,
    const RageSpriteVertex& rsv2, float percent_between) {
  average_out.p_ = lerp(percent_between, rsv1.p_, rsv2.p_);
  average_out.n_ = lerp(percent_between, rsv1.n_, rsv2.n_);
  average_out.c_.b_ = lerp(percent_between, rsv1.c_.b_, rsv2.c_.b_);
  average_out.c_.g_ = lerp(percent_between, rsv1.c_.g_, rsv2.c_.g_);
  average_out.c_.r_ = lerp(percent_between, rsv1.c_.r_, rsv2.c_.r_);
  average_out.c_.a_ = lerp(percent_between, rsv1.c_.a_, rsv2.c_.a_);
  average_out.t_ = lerp(percent_between, rsv1.t_, rsv2.t_);
}

/** @brief Utilities for working with Lua. */
namespace LuaHelpers {
template <>
bool FromStack<RageColor>(lua_State* L, RageColor& Object, int iOffset) {
  Object.FromStack(L, iOffset);
  return true;
}
template <>
void Push<RageColor>(lua_State* L, const RageColor& Object) {
  Object.PushTable(L);
}
}  // namespace LuaHelpers

static const char* CullModeNames[] = {"Back", "Front", "None"};
XToString(CullMode);
LuaXType(CullMode);

static const char* BlendModeNames[] = {
    "Normal", "Add", "Subtract", "Modulate",

    /*
     * Copy the source directly to the destination.  Alpha is not premultiplied.
     *
     * Co = Cs
     * Ao = As
     */
    "CopySrc",

    /*
     * Leave the color alone, and apply the source alpha on top of the existing
     * alpha. Transparent areas in the source become transparent in the
     * destination.
     *
     * Be sure to disable alpha test with this blend mode.
     *
     * Co = Cd
     * Ao = Ad*As
     */
    "AlphaMask",

    /*
     * Leave the color alone, and apply the source alpha on top of the existing
     * alpha. Transparent areas in the source become transparent in the
     * destination. Co = Cd Ao = Ad*(1-As)
     */
    "AlphaKnockOut", "AlphaMultiply", "WeightedMultiply", "InvertDest",
    "NoEffect"};
XToString(BlendMode);
StringToX(BlendMode);
LuaXType(BlendMode);

static const char* TextureModeNames[] = {
    "Modulate",
    "Glow",
    "Add",
};
XToString(TextureMode);
LuaXType(TextureMode);

static const char* EffectModeNames[] = {
    /* Normal blending.  All supported texture modes have their standard
       effects. */
    "Normal",

    /* After rendering to a destination alpha render target, the color will be
     * premultiplied with its alpha.  An Unpremultiply pass with CopySrc
     * blending must be performed to correct this. */
    "Unpremultiply",

    /* Layered blending.    These shaders take two source textures. */
    "ColorBurn", "ColorDodge", "VividLight", "HardMix", "Overlay", "Screen",

    "YUYV422",
    /* Draws a graphic from a signed distance field. */
    "DistanceField"};
XToString(EffectMode);
LuaXType(EffectMode);

static const char* ZTestModeNames[] = {"Off", "WriteOnPass", "WriteOnFail"};
XToString(ZTestMode);
LuaXType(ZTestMode);

static const char* TextGlowModeNames[] = {"Inner", "Stroke", "Both"};
XToString(TextGlowMode);
LuaXType(TextGlowMode);

int LuaFunc_color(lua_State* L) {
  std::string sColor = SArg(1);
  RageColor c;
  c.FromString(sColor);
  c.PushTable(L);
  return 1;
}
LUAFUNC_REGISTER_COMMON(color);

int LuaFunc_lerp_color(lua_State* L) {
  // Args:  percent, color, color
  // Returns:  color
  float percent = FArg(1);
  RageColor a, b, c;
  a.FromStack(L, 2);
  b.FromStack(L, 3);
  lerp_rage_color(c, a, b, percent);
  c.PushTable(L);
  return 1;
}
LUAFUNC_REGISTER_COMMON(lerp_color);

/*
 * Copyright (c) 2006 Glenn Maynard
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
