#include "LuaManager.h"

#include <algorithm>
#include <cmath>
#include <csetjmp>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <sstream>  // conversion for lua functions.
#include <string>
#include <vector>

#include "Command.h"
#include "LuaDebugManager.h"
#include "LuaReference.h"
#include "MessageManager.h"
#include "RageException.h"
#include "RageLog.h"
#include "RageThreads.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "StdString.h"
#include "XmlFile.h"
#include "arch/Dialog/Dialog.h"
#include "global.h"
#include "ver.h"

LuaManager* LUA = nullptr;
struct Impl {
  Impl() : lock_("Lua") {}
  std::vector<lua_State*> free_state_list_;
  std::map<lua_State*, bool> active_states_;

  RageMutex lock_;
};
static Impl* pImpl = nullptr;

#if defined(_MSC_VER)
/* "interaction between '_setjmp' and C++ object destruction is non-portable"
 * We don't care; we'll throw a fatal exception immediately anyway. */
#pragma warning(disable : 4611)
#endif

/** @brief Utilities for working with Lua. */
namespace LuaHelpers {
template <>
void Push<bool>(lua_State* L, const bool& object);
template <>
void Push<float>(lua_State* L, const float& object);
template <>
void Push<int>(lua_State* L, const int& object);
template <>
void Push<std::string>(lua_State* L, const std::string& object);

template <>
bool FromStack<bool>(Lua* L, bool& object, int offset);
template <>
bool FromStack<float>(Lua* L, float& object, int offset);
template <>
bool FromStack<int>(Lua* L, int& object, int offset);
template <>
bool FromStack<unsigned int>(Lua* L, unsigned int& object, int offset);
template <>
bool FromStack<std::string>(Lua* L, std::string& object, int offset);

bool InReportScriptError = false;
}  // namespace LuaHelpers

void LuaManager::SetGlobal(const std::string& name, int val) {
  Lua* L = Get();
  LuaHelpers::Push(L, val);
  lua_setglobal(L, name.c_str());
  Release(L);
}

void LuaManager::UnsetGlobal(const std::string& name) {
  Lua* L = Get();
  lua_pushnil(L);
  lua_setglobal(L, name.c_str());
  Release(L);
}

/** @brief Utilities for working with Lua. */
namespace LuaHelpers {
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
bool FromStack<std::string>(Lua* L, std::string& object, int offset) {
  size_t len;
  const char* str = lua_tolstring(L, offset, &len);
  if (str != nullptr) {
    object.assign(str, len);
  } else {
    object.clear();
  }

  return str != nullptr;
}
}  // namespace LuaHelpers

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
    lua_pushstring(L, attr->first.c_str());  // push key
    node->PushAttrValue(L, attr->first);     // push value

    // add key-value pair to our table
    lua_settable(L, -3);
  }

  FOREACH_CONST_Child(node, c) {
    const XNode* child = c;
    lua_pushstring(L, child->name_.c_str());  // push key

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
  std::string err;
  LuaHelpers::Pop(L, err);

  lua_Debug ar;

  for (int level = 0; lua_getstack(L, level, &ar); ++level) {
    if (!lua_getinfo(L, "nSluf", &ar)) {
      break;
    }
    // The function is now on the top of the stack.
    const char* file = ar.source[0] == '@' ? ar.source + 1 : ar.short_src;
    const char* name;
    std::vector<std::string> args;

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

    err += ssprintf("\n%s:", file);
    if (ar.currentline != -1) {
      err += ssprintf("%i:", ar.currentline);
    }

    if (ar.name && ar.name[0]) {
      err += ssprintf(" %s", ar.name);
    } else if (
        !strcmp(ar.what, "main") || !strcmp(ar.what, "tail") ||
        !strcmp(ar.what, "C")) {
      err += ssprintf(" %s", ar.what);
    } else {
      err += ssprintf(" unknown");
    }
    err += ssprintf("(%s)", join(",", args).c_str());
  }

  LuaHelpers::Push(L, err);
  return 1;
}

static int LuaPanic(lua_State* L) {
  GetLuaStack(L);

  std::string err;
  LuaHelpers::Pop(L, err);

  RageException::Throw("[Lua panic] %s", err.c_str());
}

// Actor registration
static std::vector<RegisterWithLuaFn>* g_vRegisterActorTypes = nullptr;

void LuaManager::Register(RegisterWithLuaFn pfn) {
  if (g_vRegisterActorTypes == nullptr) {
    g_vRegisterActorTypes = new std::vector<RegisterWithLuaFn>;
  }

  g_vRegisterActorTypes->push_back(pfn);
}

LuaManager::LuaManager() {
  pImpl = new Impl;
  LUA = this;  // so that LUA is available when we call the Register functions

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
  RageUtil::SafeDelete(pImpl);
}

Lua* LuaManager::Get() {
  bool locked = false;
  if (!pImpl->lock_.IsLockedByThisThread()) {
    pImpl->lock_.Lock();
    locked = true;
  }

  ASSERT(lua_gettop(lua_main_) == 1);

  lua_State* ret;
  if (pImpl->free_state_list_.empty()) {
    ret = lua_newthread(lua_main_);

    // Store the new thread in THREAD_POOL, so it isn't collected.
    int last = lua_objlen(lua_main_, THREAD_POOL);
    lua_rawseti(lua_main_, THREAD_POOL, last + 1);
  } else {
    ret = pImpl->free_state_list_.back();
    pImpl->free_state_list_.pop_back();
  }

  pImpl->active_states_[ret] = locked;
  if (LUADEBUG) {
    LUADEBUG->ActivateState(ret);
  }
  return ret;
}

void LuaManager::Release(Lua*& p) {
  if (LUADEBUG) {
    LUADEBUG->DeactivateState(p);
  }
  pImpl->free_state_list_.push_back(p);

  ASSERT(lua_gettop(p) == 0);
  ASSERT(pImpl->active_states_.find(p) != pImpl->active_states_.end());
  bool do_unlock = pImpl->active_states_[p];
  pImpl->active_states_.erase(p);

  if (do_unlock) {
    pImpl->lock_.Unlock();
  }
  p = nullptr;
}

/*
 * Low-level access to Lua is always serialized through pImpl->g_pLock; we never
 * run the Lua core simultaneously from multiple threads.  However, when a
 * thread has an acquired lua_State, it can release Lua for use by other
 * threads.  This allows Lua bindings to process long-running actions, without
 * blocking all other threads from using Lua until it finishes.
 *
 * Lua *L = LUA->Get();			// acquires L and locks Lua
 * lua_newtable(L);				// does something with Lua
 * LUA->YieldLua();				// unlocks Lua for lengthy
 * operation; L is still owned, but can't be used std::string s =
 * ReadFile("/filename.txt");	// time-consuming operation; other threads may
 * use Lua in the meantime LUA->UnyieldLua();			// relock Lua
 * lua_pushstring( L, s );		// finish working with it
 * LUA->Release( L );			// release L and unlock Lua
 *
 * YieldLua() must not be called when already yielded, or when a lua_State has
 * not been acquired (you have nothing to yield), and always unyield before
 * releasing the state.  Recursive handling is OK:
 *
 * L1 = LUA->Get();
 * LUA->YieldLua();				// yields
 *   L2 = LUA->Get();			// unyields
 *   LUA->Release(L2);			// re-yields
 * LUA->UnyieldLua();
 * LUA->Release(L1);
 */
void LuaManager::YieldLua() {
  ASSERT(pImpl->lock_.IsLockedByThisThread());

  pImpl->lock_.Unlock();
}

void LuaManager::UnyieldLua() { pImpl->lock_.Lock(); }

void LuaManager::RegisterTypes() {
  Lua* L = Get();

  if (g_vRegisterActorTypes) {
    for (unsigned i = 0; i < g_vRegisterActorTypes->size(); i++) {
      RegisterWithLuaFn fn = (*g_vRegisterActorTypes)[i];
      fn(L);
    }
  }

  Release(L);
}

LuaThreadVariable::LuaThreadVariable(
    const std::string& name, const std::string& value) {
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
    const std::string& name, const LuaReference& value) {
  name_ = new LuaReference;
  old_value_ = new LuaReference;

  Lua* L = LUA->Get();
  LuaHelpers::Push(L, name);
  name_->SetFromStack(L);

  value.PushSelf(L);
  SetFromStack(L);
  LUA->Release(L);
}

// name and value are on the stack
LuaThreadVariable::LuaThreadVariable(lua_State* L) {
  name_ = new LuaReference;
  old_value_ = new LuaReference;

  lua_pushvalue(L, -2);
  name_->SetFromStack(L);

  SetFromStack(L);

  lua_pop(L, 1);
}

std::string LuaThreadVariable::GetCurrentThreadIDString() {
  uint64_t id = RageThread::GetCurrentThreadID();
  return ssprintf("%08x%08x", uint32_t(id >> 32), uint32_t(id));
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

  std::string thread_id_string = GetCurrentThreadIDString();
  LuaHelpers::Push(L, thread_id_string);
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

    LuaHelpers::Push(L, thread_id_string);
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
  std::string base_name_;
  std::vector<std::string> methods_;
};
}  // namespace

XNode* LuaHelpers::GetLuaInformation() {
  XNode* lua_node = new XNode("Lua");

  XNode* globals_node = lua_node->AppendChild("GlobalFunctions");
  XNode* classes_node = lua_node->AppendChild("Classes");
  XNode* namespaces_node = lua_node->AppendChild("Namespaces");
  XNode* singletons_node = lua_node->AppendChild("Singletons");
  XNode* enums_node = lua_node->AppendChild("Enums");
  XNode* constants_node = lua_node->AppendChild("Constants");

  std::vector<std::string> functions;
  std::map<std::string, LClass> classes;
  std::map<std::string, std::vector<std::string>> namespaces;
  std::map<std::string, std::string> singletons;
  std::map<std::string, float> constants;
  std::map<std::string, std::string> string_constants;
  std::map<std::string, std::vector<std::string>> enums;

  Lua* L = LUA->Get();
  FOREACH_LUATABLE(L, LUA_GLOBALSINDEX) {
    std::string key;
    LuaHelpers::Pop(L, key);

    switch (lua_type(L, -1)) {
      case LUA_TTABLE: {
        if (luaL_getmetafield(L, -1, "class")) {
          const char* name = lua_tostring(L, -1);

          if (!name) {
            break;
          }
          LClass& c = classes[name];
          lua_pop(L, 1);  // pop name

          // Get base class.
          luaL_getmetatable(L, name);
          ASSERT(!lua_isnil(L, -1));
          lua_getfield(L, -1, "base");
          name = lua_tostring(L, -1);

          if (name) {
            c.base_name_ = name;
          }
          lua_pop(L, 2);  // pop name and metatable

          // Get methods.
          FOREACH_LUATABLE(L, -1) {
            std::string method;
            if (LuaHelpers::FromStack(L, method, -1)) {
              c.methods_.push_back(method);
            }
          }
          sort(c.methods_.begin(), c.methods_.end());
          break;
        }
        [[fallthrough]];
      }
      case LUA_TUSERDATA:  // table or userdata: class instance
      {
        if (!luaL_callmeta(L, -1, "__type")) {
          break;
        }
        std::string type;
        if (!LuaHelpers::Pop(L, type)) {
          break;
        }
        if (type == "Enum") {
          LuaHelpers::ReadArrayFromTable(enums[key], L);
        } else {
          singletons[key] = type;
        }
        break;
      }
      case LUA_TNUMBER:
        LuaHelpers::FromStack(L, constants[key], -1);
        break;
      case LUA_TSTRING:
        LuaHelpers::FromStack(L, string_constants[key], -1);
        break;
      case LUA_TFUNCTION:
        functions.push_back(key);
        /*
        {
                lua_Debug ar;
                lua_getfield( L, LUA_GLOBALSINDEX, sKey );
                lua_getinfo( L, ">S", &ar ); // Pops the function
                printf( "%s: %s\n", sKey.c_str(), ar.short_src );
        }
        */
        break;
    }
  }

  // Find namespaces
  lua_pushcfunction(L, luaopen_package);
  lua_call(L, 0, 0);
  lua_getglobal(L, "package");
  ASSERT(lua_istable(L, -1));
  lua_getfield(L, -1, "loaded");
  ASSERT(lua_istable(L, -1));

  // const std::string BuiltInPackages[] = { "_G", "coroutine", "debug", "math",
  // "package", "string", "table" };
  const std::string built_in_packages[] = {
      "_G", "coroutine", "debug", "math", "package", "string", "table"};
  const std::string* const end =
      built_in_packages + ARRAYLEN(built_in_packages);
  FOREACH_LUATABLE(L, -1) {
    std::string s_namespace;
    LuaHelpers::Pop(L, s_namespace);
    if (std::find(built_in_packages, end, s_namespace) != end) {
      continue;
    }
    std::vector<std::string>& namespace_functions = namespaces[s_namespace];
    FOREACH_LUATABLE(L, -1) {
      std::string function;
      LuaHelpers::Pop(L, function);
      namespace_functions.push_back(function);
    }
    sort(namespace_functions.begin(), namespace_functions.end());
  }
  lua_pop(L, 2);

  LUA->Release(L);

  /* Globals */
  sort(functions.begin(), functions.end());
  for (const std::string& func : functions) {
    XNode* function_node = globals_node->AppendChild("Function");
    function_node->AppendAttr("name", func);
  }

  /* Classes */
  for (const auto& c : classes) {
    XNode* class_node = classes_node->AppendChild("Class");

    class_node->AppendAttr("name", c.first);
    if (!c.second.base_name_.empty()) {
      class_node->AppendAttr("base", c.second.base_name_);
    }
    for (const std::string& m : c.second.methods_) {
      XNode* method_node = class_node->AppendChild("Function");
      method_node->AppendAttr("name", m);
    }
  }

  /* Singletons */
  for (const auto& s : singletons) {
    if (classes.find(s.first) != classes.end()) {
      continue;
    }
    XNode* singleton_node = singletons_node->AppendChild("Singleton");
    singleton_node->AppendAttr("name", s.first);
    singleton_node->AppendAttr("class", s.second);
  }

  /* Namespaces */
  for (std::map<std::string, std::vector<std::string>>::const_iterator iter =
           namespaces.begin();
       iter != namespaces.end(); ++iter) {
    XNode* namespace_node = namespaces_node->AppendChild("Namespace");
    const std::vector<std::string>& v_namespace = iter->second;
    namespace_node->AppendAttr("name", iter->first);

    for (const std::string& func : v_namespace) {
      XNode* function_node = namespace_node->AppendChild("Function");
      function_node->AppendAttr("name", func);
    }
  }

  /* Enums */
  for (std::map<std::string, std::vector<std::string>>::const_iterator iter =
           enums.begin();
       iter != enums.end(); ++iter) {
    XNode* enum_node = enums_node->AppendChild("Enum");

    const std::vector<std::string>& v_enum = iter->second;
    enum_node->AppendAttr("name", iter->first);

    for (unsigned i = 0; i < v_enum.size(); ++i) {
      XNode* enum_value_node = enum_node->AppendChild("EnumValue");
      enum_value_node->AppendAttr("name", ssprintf("'%s'", v_enum[i].c_str()));
      enum_value_node->AppendAttr("value", i);
    }
  }

  /* Constants, String Constants */
  for (const auto& c : constants) {
    XNode* constant_node = constants_node->AppendChild("Constant");

    constant_node->AppendAttr("name", c.first);
    if (c.second == std::trunc(c.second)) {
      constant_node->AppendAttr("value", static_cast<int>(c.second));
    } else {
      constant_node->AppendAttr("value", c.second);
    }
  }
  for (const auto& s : string_constants) {
    XNode* constant_node = constants_node->AppendChild("Constant");
    constant_node->AppendAttr("name", s.first);
    constant_node->AppendAttr("value", ssprintf("'%s'", s.second.c_str()));
  }

  return lua_node;
}

bool LuaHelpers::RunScriptFile(const std::string& file) {
  std::string script;
  if (!GetFileContents(file, script)) {
    return false;
  }

  Lua* L = LUA->Get();

  std::string error;
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
    Lua* L, const std::string& script, const std::string& name,
    std::string& error) {
  // load string
  int ret = luaL_loadbuffer(L, script.data(), script.size(), name.c_str());
  if (ret) {
    LuaHelpers::Pop(L, error);
    return false;
  }

  return true;
}

void LuaHelpers::ScriptErrorMessage(const std::string& error) {
  Message msg("ScriptError");
  msg.SetParam("message", error);
  MESSAGEMAN->Broadcast(msg);
}

Dialog::Result LuaHelpers::ReportScriptError(
    const std::string& error, std::string error_type, bool use_abort) {
  // Protect from a recursion loop resulting from a mistake in the error
  // reporting lua.
  if (!InReportScriptError) {
    InReportScriptError = true;
    ScriptErrorMessage(error);
    InReportScriptError = false;
  }
  LOG->Warn("%s", error.c_str());
  if (use_abort) {
    std::string with_correct =
        error + "  Correct this and click Retry, or Cancel to break.";
    return Dialog::AbortRetryIgnore(with_correct, error_type);
  }
  // Dialog::OK(Error, ErrorType);
  return Dialog::ok;
}

// For convenience when replacing uses of LOG->Warn.
void LuaHelpers::ReportScriptErrorFmt(const char* fmt, ...) {
  va_list va;
  va_start(va, fmt);
  std::string buff = vssprintf(fmt, va);
  va_end(va);
  ReportScriptError(buff);
}

bool LuaHelpers::RunScriptOnStack(
    Lua* L, std::string& error, int args, int return_values,
    bool report_error) {
  lua_pushcfunction(L, GetLuaStack);

  // move the error function above the function and params
  int err_func = lua_gettop(L) - args - 1;
  lua_insert(L, err_func);

  // evaluate
  int ret = lua_pcall(L, args, return_values, err_func);
  if (ret) {
    if (report_error) {
      std::string lerror;
      LuaHelpers::Pop(L, lerror);
      error += lerror;
      ReportScriptError(error);
    } else {
      LuaHelpers::Pop(L, error);
    }
    lua_remove(L, err_func);
    for (int i = 0; i < return_values; ++i) {
      lua_pushnil(L);
    }
    return false;
  }

  lua_remove(L, err_func);
  return true;
}

bool LuaHelpers::RunScript(
    Lua* L, const std::string& script, const std::string& name,
    std::string& error, int args, int return_values, bool report_error) {
  std::string lerror;
  if (!LoadScript(L, script, name, lerror)) {
    error += lerror;
    if (report_error) {
      ReportScriptError(error);
    }
    lua_pop(L, args);
    for (int i = 0; i < return_values; ++i) {
      lua_pushnil(L);
    }
    return false;
  }

  // move the function above the params
  lua_insert(L, lua_gettop(L) - args);

  return LuaHelpers::RunScriptOnStack(
      L, error, args, return_values, report_error);
}

bool LuaHelpers::RunExpression(
    Lua* L, const std::string& expression, const std::string& name) {
  std::string error = ssprintf(
      "Lua runtime error parsing \"%s\": ",
      name.size() ? name.c_str() : expression.c_str());
  if (!LuaHelpers::RunScript(
          L, "return " + expression, name.empty() ? std::string("in") : name,
          error, 0, 1, true)) {
    return false;
  }
  return true;
}

void LuaHelpers::ParseCommandList(
    Lua* L, const std::string& commands, const std::string& name, bool legacy) {
  std::string lua_function;
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
    std::ostringstream s;

    s << "return function(self)\n";

    if (legacy) {
      s << "\tparent = self:GetParent();\n";
    }

    for (const Command& cmd : cmds.v_) {
      std::string cmd_name = cmd.GetName();
      if (legacy) {
        MakeLower(cmd_name);
      }
      s << "\tself:" << cmd_name << "(";

      bool first_param_is_string =
          legacy && (cmd_name == "horizalign" || cmd_name == "vertalign" ||
                     cmd_name == "effectclock" || cmd_name == "blend" ||
                     cmd_name == "ztestmode" || cmd_name == "cullmode" ||
                     cmd_name == "playcommand" || cmd_name == "queuecommand" ||
                     cmd_name == "queuemessage" || cmd_name == "settext");

      for (unsigned i = 1; i < cmd.args_.size(); i++) {
        std::string arg = cmd.args_[i];

        // "+200" -> "200"
        if (arg[0] == '+') {
          arg.erase(arg.begin());
        }

        if (i == 1 && first_param_is_string)  // string literal, legacy only
        {
          Replace(arg, "'", "\\'");  // escape quote
          s << "'" << arg << "'";
        } else if (arg[0] == '#')  // HTML color
        {
          RageColor col;  // in case FromString fails
          col.FromString(arg);
          // col is still valid if FromString fails
          s << col.r_ << "," << col.g_ << "," << col.b_ << "," << col.a_;
        } else {
          s << arg;
        }

        if (i != cmd.args_.size() - 1) {
          s << ",";
        }
      }
      s << ")\n";
    }

    s << "end\n";

    lua_function = s.str();
  }

  std::string error;
  if (!LuaHelpers::RunScript(L, lua_function, name, error, 0, 1)) {
    LOG->Warn("Compiling \"%s\": %s", lua_function.c_str(), error.c_str());
  }

  // The function is now on the stack.
}

/* Like luaL_typerror, but without the special case for argument 1 being "self"
 * in method calls, so we give a correct error message after we remove self. */
int LuaHelpers::TypeError(Lua* L, int arg_no, const char* name) {
  std::string type;
  luaL_pushtype(L, arg_no);
  LuaHelpers::Pop(L, type);

  lua_Debug debug;
  if (!lua_getstack(L, 0, &debug)) {
    return luaL_error(
        L, "invalid type (%s expected, got %s)", name, type.c_str());
  } else {
    lua_getinfo(L, "n", &debug);
    return luaL_error(
        L, "bad argument #%d to \"%s\" (%s expected, got %s)", arg_no,
        debug.name ? debug.name : "(unknown)", name, type.c_str());
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
  int args = lua_tointeger(L, lua_upvalueindex(1));
  for (int i = 0; i < args; ++i) {
    lua_pushvalue(L, lua_upvalueindex(i + 2));
  }
  return args;
}
}  // namespace

void LuaHelpers::PushValueFunc(lua_State* L, int args) {
  int top = lua_gettop(L) - args + 1;
  lua_pushinteger(L, args);
  lua_insert(L, top);
  lua_pushcclosure(L, lua_pushvalues, args + 1);
}

#include "ProductInfo.h"
LuaFunction(ProductFamily, (std::string)PRODUCT_FAMILY);
LuaFunction(ProductVersion, (std::string)product_version);
LuaFunction(ProductID, (std::string)PRODUCT_ID);

extern const char* const version_date;
extern const char* const version_time;
LuaFunction(VersionDate, (std::string)version_date);
LuaFunction(VersionTime, (std::string)version_time);

static float scale(float x, float l1, float h1, float l2, float h2) {
  return SCALE(x, l1, h1, l2, h2);
}
LuaFunction(scale, scale(FArg(1), FArg(2), FArg(3), FArg(4), FArg(5)));

LuaFunction(clamp, std::clamp(FArg(1), FArg(2), FArg(3)));

#include "LuaBinding.h"
namespace {
static int Trace(lua_State* L) {
  std::string string = SArg(1);
  LOG->Trace("%s", string.c_str());
  return 0;
}
static int Warn(lua_State* L) {
  std::string string = SArg(1);
  LOG->Warn("%s", string.c_str());
  return 0;
}
static int Flush(lua_State* L) {
  LOG->Flush();
  return 0;
}
static int CheckType(lua_State* L) {
  std::string type = SArg(1);
  bool ret = LuaBinding::CheckLuaObjectType(L, 2, type.c_str());
  LuaHelpers::Push(L, ret);
  return 1;
}
static int ReadFile(lua_State* L) {
  std::string path = SArg(1);

  /* Release Lua while we call GetFileContents, so we don't access
   * it while we read from the disk. */
  LUA->YieldLua();

  std::string file_contents;
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

/* RunWithThreadVariables(func, { a = "x", b = "y" }, arg1, arg2, arg3 ... }
 * calls func(arg1, arg2, arg3) with two LuaThreadVariable set, and returns
 * the return values of func(). */
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

  /* XXX: We want to clean up apVars on errors, but if we lua_pcall,
   * we won't propagate the error upwards. */
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
  std::string error = "Script error occurred.";
  std::string error_type = "LUA_ERROR";
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
