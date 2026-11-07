#include "WheelItemBase.h"

#include <string>

#include "Actor.h"
#include "ActorFrame.h"
#include "EnumHelper.h"
#include "LuaManager.h"
#include "RageTypes.h"
#include "global.h"

static const char* WheelItemDataTypeNames[] = {
    "Generic", "Section", "ParentSection", "Song", "Roulette",
    "Random",  "Portal",  "Course",        "Sort", "Custom",
};
XToString(WheelItemDataType);
StringToX(WheelItemDataType);
LuaXType(WheelItemDataType);

WheelItemBaseData::WheelItemBaseData(
    WheelItemDataType type, std::string text, RageColor color) {
  type_ = type;
  text_ = text;
  color_ = color;
}

// begin WheelItemBase
WheelItemBase::WheelItemBase(const WheelItemBase& cpy)
    : ActorFrame(cpy), data_(cpy.data_) {
  // FIXME
  // if( cpy.m_pGrayBar == cpy.m_sprBar )
  //	m_pGrayBar = m_sprBar;
}

WheelItemBase::WheelItemBase(std::string type) {
  SetName(type);
  data_ = nullptr;
  gray_bar_ = nullptr;
  Load(type);
}

void WheelItemBase::Load(std::string type) {
  color_locked_ = RageColor(0, 0, 0, 0.25f);
}

void WheelItemBase::LoadFromWheelItemData(
    const WheelItemBaseData* wid, int index, bool has_focus, int draw_index) {
  ASSERT(wid != nullptr);
  data_ = wid;
}

void WheelItemBase::DrawGrayBar(Actor& bar) {
  if (color_locked_.a_ == 0) {
    return;
  }

  RageColor glow = bar.GetGlow();
  RageColor diffuse = bar.GetDiffuse();

  bar.SetGlow(color_locked_);
  bar.SetDiffuse(RageColor(0, 0, 0, 0));

  bar.Draw();

  bar.SetGlow(glow);
  bar.SetDiffuse(diffuse);
}

void WheelItemBase::DrawPrimitives() {
  ActorFrame::DrawPrimitives();

  if (gray_bar_ != nullptr) {
    DrawGrayBar(*gray_bar_);
  }
}

// lua start
#include "LuaBinding.h"

/** @brief Allow Lua to have access to the WheelItemBase. */
class LunaWheelItemBase : public Luna<WheelItemBase> {
 public:
#define IS_LOADED_CHECK                                                       \
  if (!p->IsLoaded()) {                                                       \
    luaL_error(                                                               \
        L,                                                                    \
        "Wheel item is not loaded yet.  Use WheelItem:IsLoaded() to check."); \
  }

  static int GetColor(T* p, lua_State* L) {
    IS_LOADED_CHECK;
    LuaHelpers::Push(L, p->GetColor());
    return 1;
  }

  static int GetText(T* p, lua_State* L) {
    IS_LOADED_CHECK;
    LuaHelpers::Push(L, p->GetText());
    return 1;
  }

  static int GetType(T* p, lua_State* L) {
    IS_LOADED_CHECK;
    lua_pushnumber(L, p->GetType());
    return 1;
  }

  static int IsLoaded(T* p, lua_State* L) {
    lua_pushboolean(L, p->IsLoaded());
    return 1;
  }

  LunaWheelItemBase() {
    ADD_METHOD(GetColor);
    ADD_METHOD(GetText);
    ADD_METHOD(GetType);
    ADD_METHOD(IsLoaded);
  }
};
LUA_REGISTER_DERIVED_CLASS(WheelItemBase, ActorFrame)
// lua end

/*
 * (c) 2001-2004 Chris Danford, Chris Gomez, Glenn Maynard, Josh Allen
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
