#ifndef LUA_MANAGER_H
#define LUA_MANAGER_H

#include <vector>

#include "RageThreads.h"
#include "XmlFile.h"
#include "arch/Dialog/Dialog.h"

struct lua_State;
typedef lua_State Lua;
typedef void (*RegisterWithLuaFn)(lua_State*);

// TODO(teejusb): Remove forward declaration.
class LuaReference;

#ifndef _MSC_VER
extern "C" {
#endif
#include "../extern/lua-5.1/src/lauxlib.h"
#include "../extern/lua-5.1/src/lua.h"
#include "../extern/lua-5.1/src/lualib.h"
#ifndef _MSC_VER
}
#endif

class LuaManager {
 public:
  // Every Actor should register its class at program initialization.
  static void Register(RegisterWithLuaFn func);

  LuaManager();
  ~LuaManager();

  Lua* Get();
  void Release(Lua*& p);

  // Explicitly lock and unlock Lua access. This is done automatically by Get()
	// and Release().
  void YieldLua();
  void UnyieldLua();

  // Register all subscribing types.
  // There's no harm in registering when already registered.
  void RegisterTypes();

  void SetGlobal(const RString& name, int val);
  void SetGlobal(const RString& name, const RString& val);
  void UnsetGlobal(const RString& name);

 private:
  lua_State* lua_main_;
  // Swallow up warnings. If they must be used, define them.
  LuaManager& operator=(const LuaManager& rhs);
  LuaManager(const LuaManager& rhs);
};

extern LuaManager* LUA;

// Utilities for working with Lua.
namespace LuaHelpers {

// Load the given script with the given name. On success, the resulting chunk
// will be on the stack. On error, the error is stored in error and the stack
// is unchanged.
bool LoadScript(
    Lua* L, const RString& script, const RString& name, RString& error);

// Report the error three ways:  Broadcast message, Warn, and Dialog.
// If use_abort is true, reports the error through Dialog::AbortRetryIgnore
// and returns the result.
// If use_abort is false, reports the error through Dialog::OK and returns
// Dialog::ok.
Dialog::Result ReportScriptError(
    const RString& error, RString error_type = "LUA_ERROR",
    bool use_abort = false);
// Just the broadcast message part, for things that need to do the rest
// differently.
void ScriptErrorMessage(const RString& error);
// For convenience when replacing uses of LOG->Warn.
void ReportScriptErrorFmt(const char* fmt, ...);

// Run the function with arguments at the top of the stack, with the given
// number of arguments. The specified number of return values are left on the
// Lua stack. On error, nils are left on the stack, "error" is set and false is
// returned.
// If report_error is true, "error" should contain the string to prepend when
// reporting. The error is reported through LOG->Warn and
// SCREENMAN->SystemMessage.
bool RunScriptOnStack(
    Lua* L, RString& error, int args = 0, int return_values = 0,
    bool report_error = false);

// LoadScript the given script, and RunScriptOnStack it. "args" arguments are at
// the top of the stack.
bool RunScript(
    Lua* L, const RString& script, const RString& name, RString& error,
    int args = 0, int return_values = 0, bool report_error = false);

// Run the given expression, returning a single value, and leave the return
// value on the stack. On error, push nil.
bool RunExpression(
    Lua* L, const RString& expression, const RString& name = "");

bool RunScriptFile(const RString& file);

// Create a Lua array (a table with indices starting at 1) of the given vector,
// and push it on the stack.
void CreateTableFromArrayB(Lua* L, const std::vector<bool>& in);

// Create a Lua table with contents set from this XNode, then push it on the
// stack.
void CreateTableFromXNode(Lua* L, const XNode* node);

// Recursively copy elements from the table at stack element -2 into the table
// at stack -1. Pop both elements from the stack.
void DeepCopy(lua_State* L);

// Read the table at the top of the stack back into a vector.
void ReadArrayFromTableB(Lua* L, std::vector<bool>& out);

void ParseCommandList(
    lua_State* L, const RString& commands, const RString& name, bool legacy);

XNode* GetLuaInformation();

// Pops the last iArgs arguments from the stack, and return a function that
// returns those values.
void PushValueFunc(lua_State* L, int args);

template <class T>
void Push(lua_State* L, const T& object);

template <class T>
bool FromStack(lua_State* L, T& object, int offset);

template <class T>
bool Pop(lua_State* L, T& val) {
  bool ret = LuaHelpers::FromStack(L, val, -1);
  lua_pop(L, 1);
  return ret;
}

template <class T>
void ReadArrayFromTable(std::vector<T>& out, lua_State* L) {
  luaL_checktype(L, -1, LUA_TTABLE);

  int i = 0;
  while (lua_rawgeti(L, -1, ++i), !lua_isnil(L, -1)) {
    T value = T();
    LuaHelpers::Pop(L, value);
    out.push_back(value);
  }
  lua_pop(L, 1);  // pop nil
}
template <class T>
void CreateTableFromArray(const std::vector<T>& in, lua_State* L) {
  lua_newtable(L);
  for (unsigned i = 0; i < in.size(); ++i) {
    LuaHelpers::Push(L, in[i]);
    lua_rawseti(L, -2, i + 1);
  }
}

int TypeError(Lua* L, int num_args, const char* name);
inline int AbsIndex(Lua* L, int i) {
  if (i > 0 || i <= LUA_REGISTRYINDEX) {
    return i;
  }
  return lua_gettop(L) + i + 1;
}

}  // namespace LuaHelpers

class LuaThreadVariable {
 public:
  LuaThreadVariable(const RString& name, const RString& value_str);
  LuaThreadVariable(const RString& name, const LuaReference& value);
  LuaThreadVariable(lua_State* L);  // name and value are on stack
  ~LuaThreadVariable();
  static void GetThreadVariable(lua_State* L);

 private:
  LuaThreadVariable(const LuaThreadVariable& cpy);  // not defined

  void SetFromStack(lua_State* L);
  int AdjustCount(lua_State* L, int add);
  static bool PushThreadTable(lua_State* L, bool create);
  static RString GetCurrentThreadIDString();

  LuaReference* name_;
  LuaReference* old_value_;

  // Swallow up warnings. If they must be used, define them.
  LuaThreadVariable& operator=(const LuaThreadVariable& rhs);
};

// Iterate over all elements in the table.
//
// For safety reasons, the key is pushed onto the stack and can be read (safely)
// as a string and popped or altered in any way. Stack management is handled
// automatically. That is, you need not remove all stack elements above the key.
// Once the loop exits normally, the top of the stack will be where it was
// before. If you break out of the loop early, you need to handle that
// explicitly.
#define FOREACH_LUATABLE(L, index)                                       \
  for (const int SM_UNIQUE_NAME(tab) = LuaHelpers::AbsIndex(L, index),   \
                 SM_UNIQUE_NAME(top) = (lua_pushnil(L), lua_gettop(L));  \
       lua_next(L, SM_UNIQUE_NAME(tab)) && (lua_pushvalue(L, -2), true); \
       lua_settop(L, SM_UNIQUE_NAME(top)))

// Iterate over the array elements of a table.
#define FOREACH_LUATABLEI(L, index, i)                                \
  for (int SM_UNIQUE_NAME(tab) = LuaHelpers::AbsIndex(L, index),      \
           SM_UNIQUE_NAME(top) = lua_gettop(L), i = 1;                \
       lua_rawgeti(L, SM_UNIQUE_NAME(tab), i),                        \
           lua_isnil(L, -1)                                           \
               ? (lua_pop(L, 1), false)                               \
               : (true); /* if nil, pop the nil and stop traversal */ \
       lua_settop(L, SM_UNIQUE_NAME(top)), ++i)

struct RegisterLuaFunction {
  RegisterLuaFunction(RegisterWithLuaFn pfn) { LuaManager::Register(pfn); }
};
#define REGISTER_WITH_LUA_FUNCTION(Fn) \
  static RegisterLuaFunction register##Fn(Fn);

inline bool MyLua_checkboolean(lua_State* L, int numArg) {
  luaL_checktype(L, numArg, LUA_TBOOLEAN);
  return !!lua_toboolean(L, numArg);
}

// BIArg is like BArg, except 1 is accepted as a true value and (as a special
// case) 0 is accepted as a false value. This is to help transitions where
// "cmd,0" is used to mean "cmd,false".
inline bool MyLua_checkintboolean(lua_State* L, int arg) {
  luaL_checkany(L, arg);
  int type = lua_type(L, arg);
  if (type == LUA_TNUMBER) {
    return lua_tointeger(L, arg) != 0;
  }

  return MyLua_checkboolean(L, arg);
}

// Checks the table at index to verify that it contains strings.
inline bool TableContainsOnlyStrings(lua_State* L, int index) {
  bool passed = true;
  lua_pushnil(L);
  while (lua_next(L, index) != 0) {
    // `key' is at index -2 and `value' at index -1
    const char* value = lua_tostring(L, -1);
    if (value == nullptr) {
      // NOTE(Kyz): Was going to print an error to the log with the key that
      // failed, but didn't want to pull in RageLog.
      passed = false;
    }
    lua_pop(L, 1);
  }
  return passed;
}

#define SArg(n) (luaL_checkstring(L, (n)))
#define BIArg(n) (MyLua_checkintboolean(L, (n)))
#define IArg(n) (luaL_checkint(L, (n)))
#define BArg(n) (MyLua_checkboolean(L, (n)))
#define FArg(n) ((float)luaL_checknumber(L, (n)))

// SafeFArg is for places that need to get a number off the lua stack, but
// can't risk an error being raised.  IArg and luaL_optnumber would both raise
// an error on a type mismatch. -Kyz
inline int SafeFArg(lua_State* L, int index, const RString& err, int def) {
  if (lua_isnumber(L, index)) {
    return static_cast<int>(lua_tonumber(L, index));
  }
  LuaHelpers::ReportScriptError(err);
  return def;
}

#define LuaFunction(func, expr)                \
  int LuaFunc_##func(lua_State* L);            \
  int LuaFunc_##func(lua_State* L) {           \
    LuaHelpers::Push(L, expr);                 \
    return 1;                                  \
  }                                            \
  void LuaFunc_Register_##func(lua_State* L);  \
  void LuaFunc_Register_##func(lua_State* L) { \
    lua_register(L, #func, LuaFunc_##func);    \
  }                                            \
  REGISTER_WITH_LUA_FUNCTION(LuaFunc_Register_##func);

#define LUAFUNC_REGISTER_COMMON(func_name)            \
  void LuaFunc_Register_##func_name(lua_State* L);    \
  void LuaFunc_Register_##func_name(lua_State* L) {   \
    lua_register(L, #func_name, LuaFunc_##func_name); \
  }                                                   \
  REGISTER_WITH_LUA_FUNCTION(LuaFunc_Register_##func_name);

#endif  // LUA_MANAGER_H

/*
 * (c) 2004 Glenn Maynard
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
