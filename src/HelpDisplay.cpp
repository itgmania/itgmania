#include "global.h"

#include "HelpDisplay.h"

#include <vector>

#include "ActorUtil.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "ThemeManager.h"

REGISTER_ACTOR_CLASS(HelpDisplay);

HelpDisplay::HelpDisplay() {
  cur_tip_index_ = 0;
  seconds_until_switch_ = 0;
  seconds_between_switches_ = 1;
}

void HelpDisplay::Load(const RString& type) {
  RunCommands(THEME->GetMetricA(type, "TipOnCommand"));
  seconds_until_switch_ = THEME->GetMetricF(type, "TipShowTime");
  seconds_between_switches_ = THEME->GetMetricF(type, "TipSwitchTime");
}

void HelpDisplay::SetTips(
    const std::vector<RString>& array_tips,
    const std::vector<RString>& array_tips_alt) {
  ASSERT(array_tips.size() == array_tips_alt.size());

  if (array_tips == array_tips_ && array_tips_alt == array_tips_alt_) {
    return;
  }

  SetText("");

  array_tips_ = array_tips;
  array_tips_alt_ = array_tips_alt;

  cur_tip_index_ = 0;
  seconds_until_switch_ = 0;
  Update(0);
}

void HelpDisplay::Update(float delta) {
  float hibernate = m_fHibernateSecondsLeft;

  BitmapText::Update(delta);

  if (array_tips_.empty()) {
    return;
  }

  seconds_until_switch_ -= std::max(delta - hibernate, 0.0f);
  if (seconds_until_switch_ > 0) {
    return;
  }

  // Time to switch states.
  seconds_until_switch_ = seconds_between_switches_;
  SetText(array_tips_[cur_tip_index_], array_tips_alt_[cur_tip_index_]);
  cur_tip_index_++;
  cur_tip_index_ = cur_tip_index_ % array_tips_.size();
}

#include "FontCharAliases.h"
#include "LuaBinding.h"

// Allow Lua to have access to the HelpDisplay.
class LunaHelpDisplay : public Luna<HelpDisplay> {
 public:
  static int settips(T* p, lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushvalue(L, 1);
    std::vector<RString> array_tips;
    LuaHelpers::ReadArrayFromTable(array_tips, L);
    lua_pop(L, 1);
    for (unsigned i = 0; i < array_tips.size(); ++i) {
      FontCharAliases::ReplaceMarkers(array_tips[i]);
    }
    if (lua_gettop(L) > 1 && !lua_isnil(L, 2)) {
      std::vector<RString> array_tips_alt;
      luaL_checktype(L, 2, LUA_TTABLE);
      lua_pushvalue(L, 2);
      LuaHelpers::ReadArrayFromTable(array_tips_alt, L);
      lua_pop(L, 1);
      for (unsigned i = 0; i < array_tips_alt.size(); ++i) {
        FontCharAliases::ReplaceMarkers(array_tips_alt[i]);
      }

      p->SetTips(array_tips, array_tips_alt);
    } else {
      p->SetTips(array_tips);
    }

    COMMON_RETURN_SELF;
  }
  static int SetTipsColonSeparated(T* p, lua_State* L) {
    std::vector<RString> tips;
    split(SArg(1), "::", tips);
    p->SetTips(tips);
    COMMON_RETURN_SELF;
  }

  static int gettips(T* p, lua_State* L) {
    std::vector<RString> array_tips;
		std::vector<RString> array_tips_alt;
    p->GetTips(array_tips, array_tips_alt);

    LuaHelpers::CreateTableFromArray(array_tips, L);
    LuaHelpers::CreateTableFromArray(array_tips_alt, L);

    return 2;
  }
  static int SetSecsBetweenSwitches(T* p, lua_State* L) {
    p->SetSecsBetweenSwitches(FArg(1));
    COMMON_RETURN_SELF;
  }

  LunaHelpDisplay() {
    ADD_METHOD(settips);
    ADD_METHOD(SetTipsColonSeparated);
    ADD_METHOD(gettips);
    ADD_METHOD(SetSecsBetweenSwitches);
  }
};

LUA_REGISTER_DERIVED_CLASS(HelpDisplay, BitmapText)

/*
 * (c) 2001-2003 Chris Danford, Glenn Maynard
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
