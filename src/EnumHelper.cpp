#include "global.h"

#include "EnumHelper.h"

#include "LuaManager.h"
#include "RageLog.h"
#include "RageUtil.h"

int CheckEnum(
    lua_State* L, LuaReference& table, int pos, int invalid, const char* type,
    bool allow_invalid, bool allow_anything) {
  if (lua_isnoneornil(L, pos)) {
    if (allow_invalid) {
      return invalid;
    }

    LuaHelpers::Push(L, ssprintf("Expected %s; got nil", type));
    lua_error(L);
  }

  pos = LuaHelpers::AbsIndex(L, pos);

  table.PushSelf(L);
  lua_pushvalue(L, pos);
  lua_gettable(L, -2);

  // If not found, check case-insensitively for legacy compatibility
  if (lua_isnil(L, -1) && lua_isstring(L, pos)) {
    RString lower;

    // Get rid of nil value on stack
    lua_pop(L, 1);

    // Get the string and lowercase it
    lua_pushvalue(L, pos);
    LuaHelpers::Pop(L, lower);
    lower.MakeLower();

    // Try again to read the value
    table.PushSelf(L);
    LuaHelpers::Push(L, lower);
    lua_gettable(L, -2);
  }

  // If the result is nil, then a string was passed that is not a member of this
  // enum. Throw an error. To specify the invalid value, pass nil. That way,
  // typos will throw an error, and not silently result in nil, or an
  // out-of-bounds value.
  if (unlikely(lua_isnil(L, -1))) {
    RString got;
    if (lua_isstring(L, pos)) {
      /* We were given a string, but it wasn't a valid value for this enum. Show
       * the string. */
      lua_pushvalue(L, pos);
      LuaHelpers::Pop(L, got);
      got = ssprintf("\"%s\"", got.c_str());
    } else {
      /* We didn't get a string.  Show the type. */
      luaL_pushtype(L, pos);
      LuaHelpers::Pop(L, got);
    }
    LuaHelpers::Push(L, ssprintf("Expected %s; got %s", type, got.c_str()));
    // There are a couple places where CheckEnum is used outside of a
    // function called from lua. If we use lua_error from one of them,
    // StepMania crashes out completely. allow_anything allows those places
    // to avoid crashing over theme mistakes.
    if (allow_anything) {
      RString error;
      LuaHelpers::Pop(L, error);
      LuaHelpers::ReportScriptError(error);
      lua_pop(L, 2);
      return invalid;
    }
    lua_error(L);
  }
  int ret = lua_tointeger(L, -1);
  lua_pop(L, 2);
  return ret;
}

// name_array is of size max; name_cache is of size max + 2.
const RString& EnumToString(
    int val, int max, const char** name_array,
    std::unique_ptr<RString>* name_cache) {
  if (unlikely(name_cache[0].get() == nullptr)) {
    for (int i = 0; i < max; ++i) {
      std::unique_ptr<RString> ap(new RString(name_array[i]));
      name_cache[i] = move(ap);
    }

    std::unique_ptr<RString> ap(new RString);
    name_cache[max + 1] = move(ap);
  }

  // NOTE(Chris): max+1 is "Invalid". max+0 is the NUM_ size value, which can
  // not be converted to a string. Maybe we should assert on _Invalid? It seems
  // better to make the caller check that they're supplying a valid enum value
  // instead of returning an inconspicuous garbage value (empty string).
  if (val < 0) {
    FAIL_M(ssprintf(
        "Value %i cannot be negative for enums! Enum hint: %s", val,
        name_array[0]));
  }
  if (val == max) {
    FAIL_M(ssprintf(
        "Value %i cannot be a string with value %i! Enum hint: %s", val, max,
        name_array[0]));
  }
  if (val > max + 1) {
    FAIL_M(ssprintf(
        "Value %i is past the invalid value %i! Enum hint: %s", val, max,
        name_array[0]));
  }
  return *name_cache[val];
}

namespace {

int GetName(lua_State* L) {
  luaL_checktype(L, 1, LUA_TTABLE);

  // Look up the reverse table.
  luaL_getmetafield(L, 1, "name");

  // If there was no metafield, then we were called on the wrong type.
  if (lua_isnil(L, -1)) {
    luaL_typerror(L, 1, "enum");
  }

  return 1;
}

int Reverse(lua_State* L) {
  luaL_checktype(L, 1, LUA_TTABLE);

  // Look up the reverse table. If there is no metafield, then we were
  // called on the wrong type.
  if (!luaL_getmetafield(L, 1, "reverse")) {
    luaL_typerror(L, 1, "enum");
  }

  return 1;
}

}  // namespace

static const luaL_Reg EnumLib[] = {
    {"GetName", GetName}, {"Reverse", Reverse}, {nullptr, nullptr}};

static void PushEnumMethodTable(lua_State* L) {
  luaL_register(L, "Enum", EnumLib);
}

// Set up the enum table on the stack, and pop the table.
void Enum::SetMetatable(
    lua_State* L, LuaReference& enum_table, LuaReference& enum_index_table,
    const char* szName) {
  enum_table.PushSelf(L);
  {
    lua_newtable(L);
    enum_index_table.PushSelf(L);
    lua_setfield(L, -2, "reverse");

    lua_pushstring(L, szName);
    lua_setfield(L, -2, "name");

    PushEnumMethodTable(L);
    lua_setfield(L, -2, "__index");

    lua_pushliteral(L, "Enum");
    LuaHelpers::PushValueFunc(L, 1);
    lua_setfield(L, -2, "__type");  // for luaL_pushtype
  }
  lua_setmetatable(L, -2);
  lua_pop(L, 2);
}

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
