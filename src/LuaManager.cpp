#include "global.h"

#include "LuaManager.h"

#include <cassert>
#include <cmath>
#include <csetjmp>
#include <cstddef>
#include <cstdint>
#include <map>
#include <sstream>  // conversion for lua functions.
#include <vector>

#include "Command.h"
#include "LuaReference.h"
#include "MessageManager.h"
#include "RageFile.h"
#include "RageLog.h"
#include "RageThreads.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "XmlFile.h"
#include "arch/Dialog/Dialog.h"
#include "ver.h"

LuaManager* LUA = nullptr;
struct Impl {
  Impl() : lock("Lua") {}
  std::vector<lua_State*> free_state_list;
  std::map<lua_State*, bool> active_states;

  RageMutex lock;
};
static Impl* impl = nullptr;

#if defined(_MSC_VER)
// "interaction between '_setjmp' and C++ object destruction is non-portable"
// We don't care; we'll throw a fatal exception immediately anyway.
#pragma warning(disable : 4611)
#endif

// Utilities for working with Lua.
namespace LuaHelpers {

bool in_report_script_error = false;

template <>
void Push<bool>(lua_State* L, const bool& object) {
  lua_pushboolean(L, object);
}
template <>
void Push<float>(lua_State* L, const float& object) {
  lua_pushnumber(L, object);
}
template <>
void Push<double>(lua_State* L, const double& object) {
  lua_pushnumber(L, object);
}
template <>
void Push<int>(lua_State* L, const int& object) {
  lua_pushinteger(L, object);
}
template <>
void Push<unsigned int>(lua_State* L, const unsigned int& object) {
  lua_pushnumber(L, double(object));
}
template <>
void Push<unsigned long long>(lua_State* L, const unsigned long long& object) {
  lua_pushnumber(L, double(object));
}
template <>
void Push<RString>(lua_State* L, const RString& object) {
  lua_pushlstring(L, object.data(), object.size());
}
template <>
void Push<std::string>(lua_State* L, const std::string& object) {
  lua_pushlstring(L, object.data(), object.size());
}

template <>
bool FromStack<bool>(Lua* L, bool& object, int offset) {
  object = !!lua_toboolean(L, offset);
  return true;
}
template <>
bool FromStack<float>(Lua* L, float& object, int offset) {
  object = (float)lua_tonumber(L, offset);
  return true;
}
template <>
bool FromStack<int>(Lua* L, int& object, int offset) {
  object = lua_tointeger(L, offset);
  return true;
}
template <>
bool FromStack<unsigned int>(Lua* L, unsigned int& object, int offset) {
  object = lua_tointeger(L, offset);
  return true;
}
template <>
bool FromStack<RString>(Lua* L, RString& object, int offset) {
  std::size_t length;
  const char* str = lua_tolstring(L, offset, &length);
  if (str != nullptr) {
    object.assign(str, length);
  } else {
    object.clear();
  }

  return str != nullptr;
}

}  // namespace LuaHelpers

void LuaManager::SetGlobal(const RString& name, int val) {
  Lua* L = Get();
  LuaHelpers::Push(L, val);
  lua_setglobal(L, name);
  Release(L);
}

void LuaManager::SetGlobal(const RString& name, const RString& val) {
  Lua* L = Get();
  LuaHelpers::Push(L, val);
  lua_setglobal(L, name);
  Release(L);
}

void LuaManager::UnsetGlobal(const RString& name) {
  Lua* L = Get();
  lua_pushnil(L);
  lua_setglobal(L, name);
  Release(L);
}

void LuaHelpers::CreateTableFromArrayB(Lua* L, const std::vector<bool>& in) {
  lua_newtable(L);
  for (unsigned i = 0; i < in.size(); ++i) {
    lua_pushboolean(L, in[i]);
    lua_rawseti(L, -2, i + 1);
  }
}

void LuaHelpers::ReadArrayFromTableB(Lua* L, std::vector<bool>& out) {
  luaL_checktype(L, -1, LUA_TTABLE);

  for (unsigned i = 0; i < out.size(); ++i) {
    lua_rawgeti(L, -1, i + 1);
    bool on = !!lua_toboolean(L, -1);
    out[i] = on;
    lua_pop(L, 1);
  }
}

namespace {

// Creates a table from an XNode and leaves it on the stack.
void CreateTableFromXNodeRecursive(Lua* L, const XNode* node) {
  // create our base table
  lua_newtable(L);

  FOREACH_CONST_Attr(node, attr) {
    lua_pushstring(L, attr->first);       // push key
    node->PushAttrValue(L, attr->first);  // push value

    // add key-value pair to our table
    lua_settable(L, -3);
  }

  FOREACH_CONST_Child(node, c) {
    const XNode* child = c;
    lua_pushstring(L, child->m_sName);  // push key

    // push value (more correctly, build this child's table and leave it there)
    CreateTableFromXNodeRecursive(L, child);

    // add key-value pair to the table
    lua_settable(L, -3);
  }
}
}  // namespace

void LuaHelpers::CreateTableFromXNode(Lua* L, const XNode* node) {
  // This creates our table and leaves it on the stack.
  CreateTableFromXNodeRecursive(L, node);
}

static int GetLuaStack(lua_State* L) {
  RString error;
  LuaHelpers::Pop(L, error);

  lua_Debug ar;

  for (int level = 0; lua_getstack(L, level, &ar); ++level) {
    if (!lua_getinfo(L, "nSluf", &ar)) {
      break;
    }
    // The function is now on the top of the stack.
    const char* file = ar.source[0] == '@' ? ar.source + 1 : ar.short_src;
    const char* name;
    std::vector<RString> args;

    if (!strcmp(ar.what, "C")) {
      for (int i = 1;
           i <= ar.nups && (name = lua_getupvalue(L, -1, i)) != nullptr; ++i) {
        args.push_back(ssprintf("%s = %s", name, lua_tostring(L, -1)));
        lua_pop(L, 1);  // pop value
      }
    } else {
      for (int i = 1; (name = lua_getlocal(L, &ar, i)) != nullptr; ++i) {
        args.push_back(ssprintf("%s = %s", name, lua_tostring(L, -1)));
        lua_pop(L, 1);  // pop value
      }
    }

    // If the first call is this function, omit it from the trace.
    if (level == 0 && lua_iscfunction(L, -1) &&
        lua_tocfunction(L, 1) == GetLuaStack) {
      lua_pop(L, 1);  // pop function
      continue;
    }
    lua_pop(L, 1);  // pop function

    error += ssprintf("\n%s:", file);
    if (ar.currentline != -1) {
      error += ssprintf("%i:", ar.currentline);
    }

    if (ar.name && ar.name[0]) {
      error += ssprintf(" %s", ar.name);
    } else if (
        !strcmp(ar.what, "main") || !strcmp(ar.what, "tail") ||
        !strcmp(ar.what, "C")) {
      error += ssprintf(" %s", ar.what);
    } else {
      error += ssprintf(" unknown");
    }
    error += ssprintf("(%s)", join(",", args).c_str());
  }

  LuaHelpers::Push(L, error);
  return 1;
}

static int LuaPanic(lua_State* L) {
  GetLuaStack(L);

  RString error;
  LuaHelpers::Pop(L, error);

  RageException::Throw("[Lua panic] %s", error.c_str());
}

// Actor registration
static std::vector<RegisterWithLuaFn>* g_vRegisterActorTypes = nullptr;

void LuaManager::Register(RegisterWithLuaFn func) {
  if (g_vRegisterActorTypes == nullptr) {
    g_vRegisterActorTypes = new std::vector<RegisterWithLuaFn>;
  }

  g_vRegisterActorTypes->push_back(func);
}

LuaManager::LuaManager() {
  impl = new Impl;
  LUA = this;  // So that LUA is available when we call the Register functions.

  lua_State* L = lua_open();
  ASSERT(L != nullptr);

  lua_atpanic(L, LuaPanic);
  lua_main_ = L;

  lua_pushcfunction(L, luaopen_base);
  lua_call(L, 0, 0);
  lua_pushcfunction(L, luaopen_math);
  lua_call(L, 0, 0);
  lua_pushcfunction(L, luaopen_string);
  lua_call(L, 0, 0);
  lua_pushcfunction(L, luaopen_table);
  lua_call(L, 0, 0);
  lua_pushcfunction(L, luaopen_debug);
  lua_call(L, 0, 0);

  // Store the thread pool in a table on the stack, in the main thread.
#define THREAD_POOL 1
  lua_newtable(L);

  RegisterTypes();
}

LuaManager::~LuaManager() {
  lua_close(lua_main_);
  SAFE_DELETE(impl);
}

Lua* LuaManager::Get() {
  bool bLocked = false;
  if (!impl->lock.IsLockedByThisThread()) {
    impl->lock.Lock();
    bLocked = true;
  }

  ASSERT(lua_gettop(lua_main_) == 1);

  lua_State* ret;
  if (impl->free_state_list.empty()) {
    ret = lua_newthread(lua_main_);

    // Store the new thread in THREAD_POOL, so it isn't collected.
    int last = lua_objlen(lua_main_, THREAD_POOL);
    lua_rawseti(lua_main_, THREAD_POOL, last + 1);
  } else {
    ret = impl->free_state_list.back();
    impl->free_state_list.pop_back();
  }

  impl->active_states[ret] = bLocked;
  return ret;
}

void LuaManager::Release(Lua*& p) {
  impl->free_state_list.push_back(p);

  ASSERT(lua_gettop(p) == 0);
  ASSERT(impl->active_states.find(p) != impl->active_states.end());
  bool bDoUnlock = impl->active_states[p];
  impl->active_states.erase(p);

  if (bDoUnlock) {
    impl->lock.Unlock();
  }
  p = nullptr;
}

// Low-level access to Lua is always serialized through impl->lock; we never run
// the Lua core simultaneously from multiple threads. However, when a thread has
// an acquired lua_State, it can release Lua for use by other threads. This
// allows Lua bindings to process long-running actions, without blocking all
// other threads from using Lua until it finishes.
//
// Lua *L = LUA->Get();     // acquires L and locks Lua
// lua_newtable(L);         // does something with Lua
// LUA->YieldLua();         // unlocks Lua for lengthy operation; L is still
//                          // owned, but can't be used
// RString s = ReadFile("/filename.txt");	// time-consuming operation; other
//                                        // threads may use Lua in the meantime
// LUA->UnyieldLua();       // relock Lua
// lua_pushstring(L, s);    // finish working with it
// LUA->Release(L);         // release L and unlock Lua
//
// YieldLua() must not be called when already yielded, or when a lua_State has
// not been acquired (you have nothing to yield), and always unyield before
// releasing the state.  Recursive handling is OK:
//
// L1 = LUA->Get();
// LUA->YieldLua();         // yields
// L2 = LUA->Get();         // unyields
// LUA->Release(L2);        // re-yields
// LUA->UnyieldLua();
// LUA->Release(L1);
void LuaManager::YieldLua() {
  ASSERT(impl->lock.IsLockedByThisThread());

  impl->lock.Unlock();
}

void LuaManager::UnyieldLua() { impl->lock.Lock(); }

void LuaManager::RegisterTypes() {
  Lua* L = Get();

  if (g_vRegisterActorTypes) {
    for (unsigned i = 0; i < g_vRegisterActorTypes->size(); ++i) {
      RegisterWithLuaFn fn = (*g_vRegisterActorTypes)[i];
      fn(L);
    }
  }

  Release(L);
}

LuaThreadVariable::LuaThreadVariable(
    const RString& name, const RString& value) {
  name_ = new LuaReference;
  old_value_ = new LuaReference;

  Lua* L = LUA->Get();
  LuaHelpers::Push(L, name);
  name_->SetFromStack(L);
  LuaHelpers::Push(L, value);
  SetFromStack(L);
  LUA->Release(L);
}

LuaThreadVariable::LuaThreadVariable(
    const RString& name, const LuaReference& value) {
  name_ = new LuaReference;
  old_value_ = new LuaReference;

  Lua* L = LUA->Get();
  LuaHelpers::Push(L, name);
  name_->SetFromStack(L);

  value.PushSelf(L);
  SetFromStack(L);
  LUA->Release(L);
}

// Name and value are on the stack.
LuaThreadVariable::LuaThreadVariable(lua_State* L) {
  name_ = new LuaReference;
  old_value_ = new LuaReference;

  lua_pushvalue(L, -2);
  name_->SetFromStack(L);

  SetFromStack(L);

  lua_pop(L, 1);
}

RString LuaThreadVariable::GetCurrentThreadIDString() {
  std::uint64_t id = RageThread::GetCurrentThreadID();
  return ssprintf("%08x%08x", std::uint32_t(id >> 32), std::uint32_t(id));
}

bool LuaThreadVariable::PushThreadTable(lua_State* L, bool create) {
  lua_getfield(L, LUA_REGISTRYINDEX, "LuaThreadVariableTable");
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    if (!create) {
      return false;
    }
    lua_newtable(L);

    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, "LuaThreadVariableTable");
  }

  RString thread_id_str = GetCurrentThreadIDString();
  LuaHelpers::Push(L, thread_id_str);
  lua_gettable(L, -2);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    if (!create) {
      lua_pop(L, 1);
      return false;
    }
    lua_newtable(L);

    lua_pushinteger(L, 0);
    lua_rawseti(L, -2, 0);

    LuaHelpers::Push(L, thread_id_str);
    lua_pushvalue(L, -2);
    lua_settable(L, -4);
  }

  lua_remove(L, -2);
  return true;
}

void LuaThreadVariable::GetThreadVariable(lua_State* L) {
  if (!PushThreadTable(L, false)) {
    lua_pop(L, 1);
    lua_pushnil(L);
    return;
  }

  lua_pushvalue(L, -2);
  lua_gettable(L, -2);
  lua_remove(L, -2);
  lua_remove(L, -2);
}

int LuaThreadVariable::AdjustCount(lua_State* L, int add) {
  ASSERT(lua_istable(L, -1));

  lua_rawgeti(L, -1, 0);
  ASSERT(lua_isnumber(L, -1) != 0);

  int count = lua_tointeger(L, -1);
  lua_pop(L, 1);

  count += add;
  lua_pushinteger(L, count);
  lua_rawseti(L, -2, 0);

  return count;
}

void LuaThreadVariable::SetFromStack(lua_State* L) {
  ASSERT(!old_value_->IsSet());  // don't call twice

  PushThreadTable(L, true);

  name_->PushSelf(L);
  lua_gettable(L, -2);
  old_value_->SetFromStack(L);

  name_->PushSelf(L);
  lua_pushvalue(L, -3);
  lua_settable(L, -3);

  AdjustCount(L, +1);

  lua_pop(L, 2);
}

LuaThreadVariable::~LuaThreadVariable() {
  Lua* L = LUA->Get();

  PushThreadTable(L, true);
  name_->PushSelf(L);
  old_value_->PushSelf(L);
  lua_settable(L, -3);

  if (AdjustCount(L, -1) == 0) {
    // if empty, delete the table
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaThreadVariableTable");
    ASSERT(lua_istable(L, -1));

    LuaHelpers::Push(L, GetCurrentThreadIDString());
    lua_pushnil(L);
    lua_settable(L, -3);
    lua_pop(L, 1);
  }
  lua_pop(L, 1);

  LUA->Release(L);

  delete old_value_;
  delete name_;
}

namespace {

struct LClass {
  RString base_name;
  std::vector<RString> methods;
};

}  // namespace

XNode* LuaHelpers::GetLuaInformation() {
  XNode* lua_node = new XNode("Lua");

  XNode* globals_node = lua_node->AppendChild("GlobalFunctions");
  XNode* classes_node = lua_node->AppendChild("Classes");
  XNode* namespace_node = lua_node->AppendChild("Namespaces");
  XNode* singletons_node = lua_node->AppendChild("Singletons");
  XNode* enums_node = lua_node->AppendChild("Enums");
  XNode* constants_node = lua_node->AppendChild("Constants");

  std::vector<RString> functions;
  std::map<RString, LClass> classes_map;
  std::map<RString, std::vector<RString>> namespaces_map;
  std::map<RString, RString> singeltons_map;
  std::map<RString, float> constants_map;
  std::map<RString, RString> string_constants_map;
  std::map<RString, std::vector<RString>> enums_map;

  Lua* L = LUA->Get();
  FOREACH_LUATABLE(L, LUA_GLOBALSINDEX) {
    RString key;
    LuaHelpers::Pop(L, key);

    switch (lua_type(L, -1)) {
      case LUA_TTABLE: {
        if (luaL_getmetafield(L, -1, "class")) {
          const char* name = lua_tostring(L, -1);

          if (!name) {
            break;
          }
          LClass& c = classes_map[name];
          lua_pop(L, 1);  // pop name

          // Get base class.
          luaL_getmetatable(L, name);
          ASSERT(!lua_isnil(L, -1));
          lua_getfield(L, -1, "base");
          name = lua_tostring(L, -1);

          if (name) {
            c.base_name = name;
          }
          lua_pop(L, 2);  // pop name and metatable

          // Get methods.
          FOREACH_LUATABLE(L, -1) {
            RString method;
            if (LuaHelpers::FromStack(L, method, -1)) {
              c.methods.push_back(method);
            }
          }
          std::sort(c.methods.begin(), c.methods.end());
          break;
        }
        [[fallthrough]];
      }
      case LUA_TUSERDATA:  // table or userdata: class instance
      {
        if (!luaL_callmeta(L, -1, "__type")) {
          break;
        }
        RString type;
        if (!LuaHelpers::Pop(L, type)) {
          break;
        }
        if (type == "Enum") {
          LuaHelpers::ReadArrayFromTable(enums_map[key], L);
        } else {
          singeltons_map[key] = type;
        }
        break;
      }
      case LUA_TNUMBER:
        LuaHelpers::FromStack(L, constants_map[key], -1);
        break;
      case LUA_TSTRING:
        LuaHelpers::FromStack(L, string_constants_map[key], -1);
        break;
      case LUA_TFUNCTION:
        functions.push_back(key);
        break;
    }
  }

  // Find namespaces.
  lua_pushcfunction(L, luaopen_package);
  lua_call(L, 0, 0);
  lua_getglobal(L, "package");
  ASSERT(lua_istable(L, -1));
  lua_getfield(L, -1, "loaded");
  ASSERT(lua_istable(L, -1));

  const RString built_in_packages[] = {"_G",      "coroutine", "debug", "math",
                                       "package", "string",    "table"};
  const RString* const end = built_in_packages + ARRAYLEN(built_in_packages);
  FOREACH_LUATABLE(L, -1) {
    RString namespace_str;
    LuaHelpers::Pop(L, namespace_str);
    if (std::find(built_in_packages, end, namespace_str) != end) {
      continue;
    }
    std::vector<RString>& namespace_functions = namespaces_map[namespace_str];
    FOREACH_LUATABLE(L, -1) {
      RString function;
      LuaHelpers::Pop(L, function);
      namespace_functions.push_back(function);
    }
    sort(namespace_functions.begin(), namespace_functions.end());
  }
  lua_pop(L, 2);

  LUA->Release(L);

  // Globals
  std::sort(functions.begin(), functions.end());
  for (const RString& func : functions) {
    XNode* function_node = globals_node->AppendChild("Function");
    function_node->AppendAttr("name", func);
  }

  // Classes
  for (const auto& c : classes_map) {
    XNode* class_node = classes_node->AppendChild("Class");

    class_node->AppendAttr("name", c.first);
    if (!c.second.base_name.empty()) {
      class_node->AppendAttr("base", c.second.base_name);
    }
    for (const RString& m : c.second.methods) {
      XNode* method_node = class_node->AppendChild("Function");
      method_node->AppendAttr("name", m);
    }
  }

  // Singletons
  for (const auto& s : singeltons_map) {
    if (classes_map.find(s.first) != classes_map.end()) {
      continue;
    }
    XNode* singleton_node = singletons_node->AppendChild("Singleton");
    singleton_node->AppendAttr("name", s.first);
    singleton_node->AppendAttr("class", s.second);
  }

  // Namespaces
  for (auto it = namespaces_map.begin(); it != namespaces_map.end(); ++it) {
    XNode* namespace_node = namespace_node->AppendChild("Namespace");
    const std::vector<RString>& namespaces = it->second;
    namespace_node->AppendAttr("name", it->first);

    for (const RString& func : namespaces) {
      XNode* function_node = namespace_node->AppendChild("Function");
      function_node->AppendAttr("name", func);
    }
  }

  // Enums
  for (auto it = enums_map.begin(); it != enums_map.end(); ++it) {
    XNode* enum_node = enums_node->AppendChild("Enum");

    const std::vector<RString>& enums = it->second;
    enum_node->AppendAttr("name", it->first);

    for (unsigned i = 0; i < enums.size(); ++i) {
      XNode* enum_value_node = enum_node->AppendChild("EnumValue");
      enum_value_node->AppendAttr("name", ssprintf("'%s'", enums[i].c_str()));
      enum_value_node->AppendAttr("value", i);
    }
  }

  // Constants, String Constants
  for (const auto& c : constants_map) {
    XNode* constant_node = constants_node->AppendChild("Constant");

    constant_node->AppendAttr("name", c.first);
    if (c.second == std::trunc(c.second)) {
      constant_node->AppendAttr("value", static_cast<int>(c.second));
    } else {
      constant_node->AppendAttr("value", c.second);
    }
  }
  for (const auto& s : string_constants_map) {
    XNode* constant_node = constants_node->AppendChild("Constant");
    constant_node->AppendAttr("name", s.first);
    constant_node->AppendAttr("value", ssprintf("'%s'", s.second.c_str()));
  }

  return lua_node;
}

bool LuaHelpers::RunScriptFile(const RString& file) {
  RString script;
  if (!GetFileContents(file, script)) {
    return false;
  }

  Lua* L = LUA->Get();

  RString error;
  if (!LuaHelpers::RunScript(L, script, "@" + file, error, 0)) {
    LUA->Release(L);
    error = ssprintf("Lua runtime error: %s", error.c_str());
    LuaHelpers::ReportScriptError(error);
    return false;
  }
  LUA->Release(L);

  return true;
}

bool LuaHelpers::LoadScript(
    Lua* L, const RString& script, const RString& name, RString& error) {
  // Load string.
  int ret = luaL_loadbuffer(L, script.data(), script.size(), name);
  if (ret) {
    LuaHelpers::Pop(L, error);
    return false;
  }

  return true;
}

void LuaHelpers::ScriptErrorMessage(const RString& error) {
  Message msg("ScriptError");
  msg.SetParam("message", error);
  MESSAGEMAN->Broadcast(msg);
}

Dialog::Result LuaHelpers::ReportScriptError(
    const RString& error, RString error_type, bool use_abort) {
  // Protect from a recursion loop resulting from a mistake in the error
  // reporting lua.
  if (!in_report_script_error) {
    in_report_script_error = true;
    ScriptErrorMessage(error);
    in_report_script_error = false;
  }
  LOG->Warn("%s", error.c_str());
  if (use_abort) {
    RString with_correct =
        error + "  Correct this and click Retry, or Cancel to break.";
    return Dialog::AbortRetryIgnore(with_correct, error_type);
  }
  return Dialog::ok;
}

// For convenience when replacing uses of LOG->Warn.
void LuaHelpers::ReportScriptErrorFmt(const char* fmt, ...) {
  va_list va;
  va_start(va, fmt);
  RString Buff = vssprintf(fmt, va);
  va_end(va);
  ReportScriptError(Buff);
}

bool LuaHelpers::RunScriptOnStack(
    Lua* L, RString& error, int args, int return_values, bool report_error) {
  lua_pushcfunction(L, GetLuaStack);

  // Move the error function above the function and params.
  int error_func = lua_gettop(L) - args - 1;
  lua_insert(L, error_func);

  // Evaluate.
  int ret = lua_pcall(L, args, return_values, error_func);
  if (ret) {
    if (report_error) {
      RString error_part;
      LuaHelpers::Pop(L, error_part);
      error += error_part;
      ReportScriptError(error);
    } else {
      LuaHelpers::Pop(L, error);
    }
    lua_remove(L, error_func);
    for (int i = 0; i < return_values; ++i) {
      lua_pushnil(L);
    }
    return false;
  }

  lua_remove(L, error_func);
  return true;
}

bool LuaHelpers::RunScript(
    Lua* L, const RString& script, const RString& name, RString& error,
    int args, int return_values, bool report_error) {
  RString error_part;
  if (!LoadScript(L, script, name, error_part)) {
    error += error_part;
    if (report_error) {
      ReportScriptError(error);
    }
    lua_pop(L, args);
    for (int i = 0; i < return_values; ++i) {
      lua_pushnil(L);
    }
    return false;
  }

  // Move the function above the params.
  lua_insert(L, lua_gettop(L) - args);

  return LuaHelpers::RunScriptOnStack(
      L, error, args, return_values, report_error);
}

bool LuaHelpers::RunExpression(
    Lua* L, const RString& expression, const RString& name) {
  RString error = ssprintf(
      "Lua runtime error parsing \"%s\": ",
      name.size() ? name.c_str() : expression.c_str());
  if (!LuaHelpers::RunScript(
          L, "return " + expression, name.empty() ? RString("in") : name,
          error, 0, 1, true)) {
    return false;
  }
  return true;
}

void LuaHelpers::ParseCommandList(
    Lua* L, const RString& commands, const RString& name, bool legacy) {
  RString lua_function;
  if (commands.size() > 0 && commands[0] == '\033') {
    // This is a compiled Lua chunk. Just pass it on directly.
    lua_function = commands;
  } else if (commands.size() > 0 && commands[0] == '%') {
    lua_function = "return ";
    lua_function.append(commands.begin() + 1, commands.end());
  } else {
    Commands cmds;
    ParseCommands(commands, cmds, legacy);

    // Convert cmds to a Lua function
    std::ostringstream oss;

    oss << "return function(self)\n";

    if (legacy) {
      oss << "\tparent = self:GetParent();\n";
    }

    for (const Command& cmd : cmds.v) {
      RString cmd_name = cmd.GetName();
      if (legacy) {
        cmd_name.MakeLower();
      }
      oss << "\tself:" << cmd_name << "(";

      bool first_param_is_string =
          legacy && (cmd_name == "horizalign" || cmd_name == "vertalign" ||
                      cmd_name == "effectclock" || cmd_name == "blend" ||
                      cmd_name == "ztestmode" || cmd_name == "cullmode" ||
                      cmd_name == "playcommand" || cmd_name == "queuecommand" ||
                      cmd_name == "queuemessage" || cmd_name == "settext");

      for (unsigned i = 1; i < cmd.args_.size(); i++) {
        RString arg = cmd.args_[i];

        // "+200" -> "200"
        if (arg[0] == '+') {
          arg.erase(arg.begin());
        }

				// String literal, legacy only.
        if (i == 1 && first_param_is_string) {
					// Escape quote.
          arg.Replace("'", "\\'");
          oss << "'" << arg << "'";
				// HTML color
        } else if (arg[0] == '#') {
					// In case FromString fails.
          RageColor color;
          color.FromString(arg);
          // color is still valid if FromString fails
          oss << color.r << "," << color.g << "," << color.b << "," << color.a;
        } else {
          oss << arg;
        }

        if (i != cmd.args_.size() - 1) {
          oss << ",";
        }
      }
      oss << ")\n";
    }

    oss << "end\n";

    lua_function = oss.str();
  }

  RString error;
  if (!LuaHelpers::RunScript(L, lua_function, name, error, 0, 1)) {
    LOG->Warn("Compiling \"%s\": %s", lua_function.c_str(), error.c_str());
  }

  // The function is now on the stack.
}

// Like luaL_typerror, but without the special case for argument 1 being "self"
// in method calls, so we give a correct error message after we remove self.
int LuaHelpers::TypeError(Lua* L, int num_args, const char* name) {
  RString sType;
  luaL_pushtype(L, num_args);
  LuaHelpers::Pop(L, sType);

  lua_Debug debug;
  if (!lua_getstack(L, 0, &debug)) {
    return luaL_error(
        L, "invalid type (%s expected, got %s)", name, sType.c_str());
  } else {
    lua_getinfo(L, "n", &debug);
    return luaL_error(
        L, "bad argument #%d to \"%s\" (%s expected, got %s)", num_args,
        debug.name ? debug.name : "(unknown)", name, sType.c_str());
  }
}

void LuaHelpers::DeepCopy(lua_State* L) {
  luaL_checktype(L, -2, LUA_TTABLE);
  luaL_checktype(L, -1, LUA_TTABLE);

  // Call DeepCopy(t, u), where t is our referenced object and u is the new
  // table.
  lua_getglobal(L, "DeepCopy");

  ASSERT_M(!lua_isnil(L, -1), "DeepCopy() missing");
  ASSERT_M(lua_isfunction(L, -1), "DeepCopy() not a function");
  lua_insert(L, lua_gettop(L) - 2);

  lua_call(L, 2, 0);
}

namespace {

int lua_pushvalues(lua_State* L) {
  int iArgs = lua_tointeger(L, lua_upvalueindex(1));
  for (int i = 0; i < iArgs; ++i) {
    lua_pushvalue(L, lua_upvalueindex(i + 2));
  }
  return iArgs;
}

}  // namespace

void LuaHelpers::PushValueFunc(lua_State* L, int args) {
  int top = lua_gettop(L) - args + 1;
  lua_pushinteger(L, args);
  lua_insert(L, top);
  lua_pushcclosure(L, lua_pushvalues, args + 1);
}

#include "ProductInfo.h"
LuaFunction(ProductFamily, (RString)PRODUCT_FAMILY);
LuaFunction(ProductVersion, (RString)product_version);
LuaFunction(ProductID, (RString)PRODUCT_ID);

extern const char* const version_date;
extern const char* const version_time;
LuaFunction(VersionDate, (RString)version_date);
LuaFunction(VersionTime, (RString)version_time);

static float scale(float x, float l1, float h1, float l2, float h2) {
  return SCALE(x, l1, h1, l2, h2);
}
LuaFunction(scale, scale(FArg(1), FArg(2), FArg(3), FArg(4), FArg(5)));

LuaFunction(clamp, clamp(FArg(1), FArg(2), FArg(3)));

#include "LuaBinding.h"
namespace {

static int Trace(lua_State* L) {
  RString str = SArg(1);
  LOG->Trace("%s", str.c_str());
  return 0;
}
static int Warn(lua_State* L) {
  RString str = SArg(1);
  LOG->Warn("%s", str.c_str());
  return 0;
}
static int Flush(lua_State* L) {
  LOG->Flush();
  return 0;
}
static int CheckType(lua_State* L) {
  RString type = SArg(1);
  bool ret = LuaBinding::CheckLuaObjectType(L, 2, type);
  LuaHelpers::Push(L, ret);
  return 1;
}
static int ReadFile(lua_State* L) {
  RString path = SArg(1);

  // Release Lua while we call GetFileContents, so we don't access it while we
	// read from the disk.
  LUA->YieldLua();

  RString file_contents;
  bool ret = GetFileContents(path, file_contents);

  LUA->UnyieldLua();
  if (!ret) {
    lua_pushnil(L);
    lua_pushstring(L, "error");  // XXX
    return 2;
  } else {
    LuaHelpers::Push(L, file_contents);
    return 1;
  }
}

// RunWithThreadVariables(func, { a = "x", b = "y" }, arg1, arg2, arg3 ... }
// calls func(arg1, arg2, arg3) with two LuaThreadVariable set, and returns the
// return values of func().
static int RunWithThreadVariables(lua_State* L) {
  luaL_checktype(L, 1, LUA_TFUNCTION);
  luaL_checktype(L, 2, LUA_TTABLE);

  std::vector<LuaThreadVariable*> vars;
  FOREACH_LUATABLE(L, 2) {
    lua_pushvalue(L, -2);
    LuaThreadVariable* var = new LuaThreadVariable(L);
    vars.push_back(var);
  }

  lua_remove(L, 2);

  // We want to clean up vars on errors, but if we lua_pcall, we won't propagate
	// the error upwards.
  int args = lua_gettop(L) - 1;
  lua_call(L, args, LUA_MULTRET);
  int vals = lua_gettop(L);

  for (LuaThreadVariable* v : vars) {
    delete v;
  }
  return vals;
}

static int GetThreadVariable(lua_State* L) {
  luaL_checkstring(L, 1);
  lua_pushvalue(L, 1);
  LuaThreadVariable::GetThreadVariable(L);
  return 1;
}

static int ReportScriptError(lua_State* L) {
  RString error = "Script error occurred.";
  RString error_type = "LUA_ERROR";
  if (lua_isstring(L, 1)) {
    error = SArg(1);
  }
  if (lua_isstring(L, 2)) {
    error_type = SArg(2);
  }
  LuaHelpers::ReportScriptError(error, error_type);
  return 0;
}

const luaL_Reg luaTable[] = {
    LIST_METHOD(Trace),
    LIST_METHOD(Warn),
    LIST_METHOD(Flush),
    LIST_METHOD(CheckType),
    LIST_METHOD(ReadFile),
    LIST_METHOD(RunWithThreadVariables),
    LIST_METHOD(GetThreadVariable),
    LIST_METHOD(ReportScriptError),
    {nullptr, nullptr}};

}  // namespace

LUA_REGISTER_NAMESPACE(lua)

/*
 * (c) 2004-2006 Glenn Maynard, Steve Checkoway
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
