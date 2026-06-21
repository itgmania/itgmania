#include "LuaDebuggeeState.h"

#include <string>

#include "LuaManager.h"
#include "global.h"

namespace LuaDebug {
class DebuggeeState::Impl {
 public:
  static void AddToParent(DebuggeeState* state) {
    state->m_parent->m_children.push_back(state);
    if (state->m_id != 0) {
      state->GetRoot().m_objects.insert({state->m_id, *state});
    }
  }

  static void CreateThread(
      DebuggeeState& root, ThreadId threadId, lua_State* L) {
    auto thread = new DebuggeeState();
    thread->m_type = Type::Thread;
    thread->m_parent = &root, thread->m_lua = L, thread->m_threadId = threadId,
    AddToParent(thread);
  }

  static void CreateStackFrame(DebuggeeState& parentThread, int level) {
    auto frame = new DebuggeeState(parentThread, Type::StackFrame);
    frame->m_stackLevel = level;
    AddToParent(frame);
  }

  static void CreateGlobals(DebuggeeState& parentThread) {
    AddToParent(new DebuggeeState(parentThread, Type::GlobalsScope));
  }

  static void CreateUpvalues(DebuggeeState& parentFrame) {
    AddToParent(new DebuggeeState(parentFrame, Type::UpvaluesScope));
  }

  static void CreateLocals(DebuggeeState& parentFrame) {
    AddToParent(new DebuggeeState(parentFrame, Type::LocalsScope));
  }

  static void CreateReferences(DebuggeeState& parentThread) {
    auto references = new DebuggeeState(parentThread, Type::References);
    references->m_index = LUA_NOREF;
    AddToParent(references);
  }

  static void CreateNamedVariable(
      DebuggeeState& parent, const std::string& name) {
    auto var = new DebuggeeState(parent, Type::NamedVariable);
    var->m_name = name;
    AddToParent(std::move(var));
  }

  static void CreateIndexedVariable(
      DebuggeeState& parent, const std::string& name, int index) {
    auto var = new DebuggeeState(parent, Type::IndexedVariable);
    var->m_name = name;
    var->m_index = index;
    AddToParent(std::move(var));
  }

  static void CreateMetaTableVariable(DebuggeeState& parent) {
    auto var = new DebuggeeState(parent, Type::MetaTableVariable);
    var->m_name = "__metatable";
    AddToParent(var);
  }

  static bool PushLocalToStack(DebuggeeState& var) {
    ASSERT(var.m_type == Type::IndexedVariable);
    ASSERT(var.m_parent->m_type == Type::LocalsScope);

    lua_State* L = var.m_lua;
    lua_Debug debug;
    if (lua_getstack(L, var.m_stackLevel, &debug) &&
        lua_getlocal(L, &debug, var.m_index) != nullptr) {
      return true;
    }

    lua_pushnil(L);
    return false;
  }

  static bool PushUpvalueToStack(DebuggeeState& var) {
    ASSERT(var.m_type == Type::IndexedVariable);
    ASSERT(var.m_parent->m_type == Type::UpvaluesScope);

    lua_State* L = var.m_lua;
    lua_Debug debug;

    lua_getstack(L, var.m_stackLevel, &debug);
    // Pushes the active function onto the stack.
    lua_getinfo(L, "f", &debug);
    if (lua_isnil(L, -1)) {
      return false;
    }

    if (lua_getupvalue(L, -1, var.m_index) == nullptr) {
      lua_pop(L, 1);
      lua_pushnil(L);
      return false;
    }

    // Remove the function from the stack.
    lua_remove(L, -2);
    return true;
  }

  static bool PushGlobalToStack(DebuggeeState& var) {
    ASSERT(var.m_parent->m_type == Type::GlobalsScope);
    lua_State* L = var.m_lua;

    if (var.m_type == Type::NamedVariable) {
      lua_pushstring(L, var.m_name.c_str());
      lua_rawget(L, LUA_GLOBALSINDEX);
    } else if (var.m_type == Type::MetaTableVariable) {
      if (!lua_getmetatable(L, LUA_GLOBALSINDEX)) {
        return false;
      }
    } else {
      FAIL_M("invalid type");
    }

    return true;
  }

  static bool PushReferenceToStack(DebuggeeState& var) {
    ASSERT(var.m_type == Type::IndexedVariable);
    ASSERT(var.m_parent->m_type == Type::References);
    if (var.m_parent->m_index == LUA_NOREF) {
      return false;
    }

    lua_State* L = var.m_lua;
    lua_getref(L, var.m_parent->m_index);
    lua_rawgeti(L, -1, var.m_index);
    lua_remove(L, -2);
    return !lua_isnil(L, -1);
  }

  static bool PopLocalFromStack(DebuggeeState& var) {
    ASSERT(var.m_type == Type::IndexedVariable);
    ASSERT(var.m_parent->m_type == Type::LocalsScope);

    lua_State* L = var.m_lua;
    lua_Debug debug;
    if (!lua_getstack(L, var.m_stackLevel, &debug) ||
        lua_setlocal(L, &debug, var.m_index) == nullptr) {
      lua_pop(L, 1);
      return false;
    }
    return true;
  }

  static bool PopUpvalueFromStack(DebuggeeState& var) {
    ASSERT(var.m_type == Type::IndexedVariable);
    ASSERT(var.m_parent->m_type == Type::UpvaluesScope);

    lua_State* L = var.m_lua;
    int topOnReturn = lua_gettop(L) - 1;
    bool ok = false;

    lua_Debug debug;
    if (!lua_getstack(L, var.m_stackLevel, &debug)) {
      goto done;
    }
    // Pushes the active function onto the stack.
    if (!lua_getinfo(L, "f", &debug)) {
      goto done;
    }
    if (lua_isnil(L, -1)) {
      goto done;
    }

    lua_insert(L, -2);
    if (lua_setupvalue(L, -2, var.m_index) == nullptr) {
      goto done;
    }
    ok = true;

  done:
    lua_settop(L, topOnReturn);
    return ok;
  }

  static bool PopGlobalFromStack(DebuggeeState& var) {
    ASSERT(var.m_parent->m_type == Type::GlobalsScope);
    lua_State* L = var.m_lua;

    if (var.m_type == Type::NamedVariable) {
      lua_pushstring(L, var.m_name.c_str());
      lua_insert(L, -2);
      lua_rawset(L, LUA_GLOBALSINDEX);
    } else if (var.m_type == Type::MetaTableVariable) {
      if (lua_type(L, -1) != LUA_TTABLE) {
        lua_pop(L, 1);
        return false;
      }

      lua_setmetatable(L, LUA_GLOBALSINDEX);
    } else {
      FAIL_M("invalid type");
    }

    return true;
  }

  static bool PopReferenceFromStack(DebuggeeState& var) {
    ASSERT(var.m_type == Type::IndexedVariable);
    ASSERT(var.m_parent->m_type == Type::References);
    if (var.m_parent->m_index == LUA_NOREF) {
      return false;
    }

    lua_State* L = var.m_lua;
    lua_getref(L, var.m_parent->m_index);
    lua_insert(L, -2);
    lua_rawseti(L, -2, var.m_index);
    lua_pop(L, 1);
    return true;
  }

  static void InitLocals(DebuggeeState& locals) {
    ASSERT(locals.m_type == Type::LocalsScope);
    if (locals.m_lua == nullptr) {
      return;
    }

    lua_State* L = locals.m_lua;
    lua_Debug debug;
    lua_getstack(L, locals.m_stackLevel, &debug);

    const char* name;
    // Local variable indexes start from 1.
    for (int i = 1; (name = lua_getlocal(L, &debug, i)) != nullptr; i++) {
      // Omit nil temporaries since they are not very interesting.
      if (!lua_isnil(L, -1) || name[0] != '(') {
        CreateIndexedVariable(locals, name, i);
      }
      lua_pop(L, 1);
    }
  }

  static void InitUpvalues(DebuggeeState& upvalues) {
    ASSERT(upvalues.m_type == Type::UpvaluesScope);
    if (upvalues.m_lua == nullptr) {
      return;
    }

    lua_State* L = upvalues.m_lua;
    lua_Debug debug;
    lua_getstack(L, upvalues.m_stackLevel, &debug);
    // Pushes the active function onto the stack.
    lua_getinfo(L, "f", &debug);
    if (!lua_isnil(L, -1)) {
      const char* name;
      for (int i = 1; (name = lua_getupvalue(L, -1, i)) != nullptr; i++) {
        CreateIndexedVariable(upvalues, name, i);
        lua_pop(L, 1);
      }
    }
    // Pop the function pushed by lua_getinfo.
    lua_pop(L, 1);
  }

  static void InitGlobals(DebuggeeState& globals) {
    ASSERT(globals.m_type == Type::GlobalsScope);
    if (globals.m_lua == nullptr) {
      return;
    }

    lua_State* L = globals.m_lua;
    // Push the initial key onto the stack.
    lua_pushnil(L);
    // Pops the key from the stack.
    while (lua_next(L, LUA_GLOBALSINDEX)) {
      // lua_next succeeded so the stack contains a key (-2) and a value (-1).
      if (lua_isstring(L, -2)) {
        std::string name = lua_tostring(L, -2);
        CreateNamedVariable(globals, name);
      }
      lua_pop(L, 1);
      // Leave the key on the stack for the next lua_next call.
    }

    if (lua_getmetatable(L, LUA_GLOBALSINDEX)) {
      lua_pop(L, 1);
      CreateMetaTableVariable(globals);
    }
  }

  static void InitVariable(DebuggeeState& var) {
    ASSERT(var.IsVariable());
    if (var.m_lua == nullptr) {
      return;
    }

    lua_State* L = var.m_lua;
    var.PushValue();
    if (!lua_istable(L, -1)) {
      lua_pop(L, 1);
      return;
    }

    // Push the initial key onto the stack.
    lua_pushnil(L);
    // Pops the key from the stack.
    while (lua_next(L, -2)) {
      // lua_next succeeded so the stack contains a key (-2) and a value (-1).
      int keyType = lua_type(L, -2);
      if (keyType == LUA_TNUMBER) {
        int index = lua_tonumber(L, -2);
        CreateIndexedVariable(var, std::to_string(index), index);
      } else if (keyType == LUA_TSTRING) {
        std::string name = lua_tostring(L, -2);
        CreateNamedVariable(var, name);
      }
      lua_pop(L, 1);
      // Leave the key on the stack for the next lua_next call.
    }

    if (lua_getmetatable(L, -1)) {
      lua_pop(L, 1);
      CreateMetaTableVariable(var);
    }

    // Pop the table.
    lua_pop(L, 1);
  }

  static void InitThread(DebuggeeState& thread) {
    ASSERT(thread.GetType() == Type::Thread);
    if (thread.m_lua == nullptr) {
      return;
    }

    CreateGlobals(thread);
    CreateReferences(thread);

    lua_Debug debug;
    for (int level = 0; lua_getstack(thread.m_lua, level, &debug); level++) {
      CreateStackFrame(thread, level);
    }
  }

  static void InitStackFrame(DebuggeeState& frame) {
    ASSERT(frame.m_type == Type::StackFrame);
    if (frame.m_lua == nullptr) {
      return;
    }

    lua_Debug debug;
    if (!lua_getstack(frame.m_lua, frame.m_stackLevel, &debug)) {
      return;
    }
    if (!lua_getinfo(frame.m_lua, "u", &debug)) {
      return;
    }

    CreateLocals(frame);
    if (debug.nups > 0) {
      CreateUpvalues(frame);
    }
  }

  static DebuggeeState g_null;
};

DebuggeeState DebuggeeState::Impl::g_null;

DebuggeeState::DebuggeeState(Root) : m_type(Type::Root) {}

DebuggeeState::operator bool() const { return m_type != Type::None; }

DebuggeeState& DebuggeeState::GetRoot() {
  DebuggeeState* o = this;
  while (o->m_parent) {
    o = o->m_parent;
  }
  return *o;
}

DebuggeeState& DebuggeeState::GetThread() {
  DebuggeeState* o = this;
  while (o != nullptr && o->GetType() != Type::Thread) {
    o = o->m_parent;
  }
  return o == nullptr ? Impl::g_null : *o;
}

DebuggeeState& DebuggeeState::GetStackFrame() {
  DebuggeeState* o = this;
  while (o != nullptr && o->GetType() != Type::StackFrame) {
    o = o->m_parent;
  }
  return o == nullptr ? Impl::g_null : *o;
}

DebuggeeState& DebuggeeState::GetObject(ObjectRef id) {
  DebuggeeState& root = GetRoot();
  auto it = root.m_objects.find(id);
  if (it == root.m_objects.end()) {
    return Impl::g_null;
  }
  return it->second;
}

DebuggeeState& DebuggeeState::GetThread(ThreadId threadId) {
  for (DebuggeeState* thread : GetRoot().GetChildren()) {
    if (thread->GetType() == Type::Thread &&
        thread->GetThreadId() == threadId) {
      return *thread;
    }
  }
  return Impl::g_null;
}

DebuggeeState& DebuggeeState::GetThread(lua_State* L) {
  for (DebuggeeState* thread : GetRoot().GetChildren()) {
    if (thread->GetType() == Type::Thread && thread->GetLua() == L) {
      return *thread;
    }
  }
  return Impl::g_null;
}

DebuggeeState& DebuggeeState::GetStackFrame(int level) {
  for (DebuggeeState* frame : GetThread().GetChildren()) {
    if (frame->GetType() == Type::StackFrame &&
        frame->GetStackLevel() == level) {
      return *frame;
    }
  }
  return Impl::g_null;
}

DebuggeeState& DebuggeeState::GetGlobals() {
  for (DebuggeeState* child : GetThread().GetChildren()) {
    if (child->GetType() == Type::GlobalsScope) {
      return *child;
    }
  }
  return Impl::g_null;
}

DebuggeeState& DebuggeeState::GetReferences() {
  for (DebuggeeState* child : GetThread().GetChildren()) {
    if (child->GetType() == Type::References) {
      return *child;
    }
  }
  return Impl::g_null;
}

DebuggeeState& DebuggeeState::GetUpvalues() {
  for (DebuggeeState* child : GetStackFrame().GetChildren()) {
    if (child->GetType() == Type::UpvaluesScope) {
      return *child;
    }
  }
  return Impl::g_null;
}

DebuggeeState& DebuggeeState::GetLocals() {
  for (DebuggeeState* child : GetStackFrame().GetChildren()) {
    if (child->GetType() == Type::LocalsScope) {
      return *child;
    }
  }
  return Impl::g_null;
}

DebuggeeState& DebuggeeState::GetVariable(const std::string& name) {
  for (DebuggeeState* child : GetChildren()) {
    if (child->IsVariable() && child->GetName() == name) {
      return *child;
    }
  }
  return Impl::g_null;
}

DebuggeeState& DebuggeeState::GetVariable(int index) {
  for (DebuggeeState* child : GetChildren()) {
    if (child->GetType() == Type::IndexedVariable &&
        child->GetIndex() == index) {
      return *child;
    }
  }
  return Impl::g_null;
}

const std::vector<DebuggeeState*>& DebuggeeState::GetChildren() {
  if (!m_childrenInitialized) {
    switch (GetType()) {
      case Type::None:
        break;
      case Type::Root:
        break;
      case Type::Thread:
        Impl::InitThread(*this);
        break;
      case Type::StackFrame:
        Impl::InitStackFrame(*this);
        break;
      case Type::References:
        break;
      case Type::GlobalsScope:
        Impl::InitGlobals(*this);
        break;
      case Type::UpvaluesScope:
        Impl::InitUpvalues(*this);
        break;
      case Type::LocalsScope:
        Impl::InitLocals(*this);
        break;
      case Type::NamedVariable:
        Impl::InitVariable(*this);
        break;
      case Type::IndexedVariable:
        Impl::InitVariable(*this);
        break;
      case Type::MetaTableVariable:
        Impl::InitVariable(*this);
        break;
      default:
        FAIL_M("invalid type");
    }
    m_childrenInitialized = true;
  }
  return m_children;
}

bool DebuggeeState::IsVariable() const {
  return m_type == Type::NamedVariable || m_type == Type::IndexedVariable ||
         m_type == Type::MetaTableVariable;
}

bool DebuggeeState::IsScope() const {
  return m_type == Type::GlobalsScope || m_type == Type::UpvaluesScope ||
         m_type == Type::LocalsScope;
}

bool DebuggeeState::IsVariablesContainer() const {
  return IsVariable() || IsScope() || m_type == Type::References;
}

bool DebuggeeState::IsTemporaryVariable() const {
  return IsVariable() && (m_name.empty() || m_name[0] == '(');
}

bool DebuggeeState::PushValue() {
  ASSERT(IsVariable());
  lua_State* L = m_lua;
  int top = lua_gettop(L);

  std::vector<DebuggeeState*> pathToRoot;
  for (DebuggeeState* o = this; o != nullptr && o->IsVariable();
       o = o->m_parent) {
    pathToRoot.push_back(o);
  }

  bool ok = false;
  DebuggeeState& first = *pathToRoot.back();
  switch (first.m_parent->m_type) {
    case Type::LocalsScope:
      ok = Impl::PushLocalToStack(first);
      break;
    case Type::UpvaluesScope:
      ok = Impl::PushUpvalueToStack(first);
      break;
    case Type::GlobalsScope:
      ok = Impl::PushGlobalToStack(first);
      break;
    case Type::References:
      ok = Impl::PushReferenceToStack(first);
      break;
    default:
      FAIL_M("invalid type");
  }
  pathToRoot.pop_back();

  while (ok && !pathToRoot.empty()) {
    int type = lua_type(L, -1);
    if (type != LUA_TTABLE) {
      lua_pop(L, 1);
      lua_pushnil(L);
      ok = false;
      break;
    }

    DebuggeeState& next = *pathToRoot.back();
    switch (next.m_type) {
      case Type::NamedVariable:
        lua_pushstring(L, next.m_name.c_str());
        lua_rawget(L, -2);
        lua_remove(L, -2);
        break;
      case Type::IndexedVariable:
        lua_rawgeti(L, -1, next.m_index);
        lua_remove(L, -2);
        break;
      case Type::MetaTableVariable:
        if (!lua_getmetatable(L, -1)) {
          lua_pushnil(L);
        }
        lua_remove(L, -2);
        break;
      default:
        FAIL_M("invalid type");
    }
    pathToRoot.pop_back();
  }

  ASSERT(lua_gettop(L) == top + 1);
  return ok;
}

bool DebuggeeState::PopValue() {
  ASSERT(IsVariable());
  lua_State* L = m_lua;
  int top = lua_gettop(L);

  bool ok = false;
  if (m_parent->IsVariable()) {
    m_parent->PushValue();
    switch (m_type) {
      case Type::NamedVariable:
        if (lua_type(L, -1) == LUA_TTABLE) {
          lua_pushstring(L, m_name.c_str());
          lua_pushvalue(L, -3);
          lua_rawset(L, -3);
          lua_pop(L, 2);
          ok = true;
        }
        break;

      case Type::IndexedVariable:
        if (lua_type(L, -1) == LUA_TTABLE) {
          lua_insert(L, -2);
          lua_rawseti(L, -2, m_index);
          lua_pop(L, 1);
          ok = true;
        }
        break;

      case Type::MetaTableVariable:
        if (lua_type(L, -2) == LUA_TTABLE) {
          lua_insert(L, -2);
          lua_setmetatable(L, -2);
          lua_pop(L, 1);
          ok = true;
        }
        break;

      default:
        FAIL_M("invalid type");
    }

    if (!ok) {
      lua_pop(L, 2);
    }
  } else {
    switch (m_parent->m_type) {
      case Type::LocalsScope:
        ok = Impl::PopLocalFromStack(*this);
        break;
      case Type::UpvaluesScope:
        ok = Impl::PopUpvalueFromStack(*this);
        break;
      case Type::GlobalsScope:
        ok = Impl::PopGlobalFromStack(*this);
        break;
      case Type::References:
        ok = Impl::PopReferenceFromStack(*this);
        break;
      default:
        FAIL_M("invalid scope type");
    }
  }

  if (ok) {
    Invalidate();
  }
  ASSERT(lua_gettop(L) == top - 1);
  return ok;
}

DebuggeeState& DebuggeeState::CreateReferenceFromStack() {
  DebuggeeState& thread = GetThread();
  ASSERT(thread);
  lua_State* L = thread.m_lua;
  int top = lua_gettop(L);

  DebuggeeState& references = thread.GetReferences();
  ASSERT(references);

  if (references.m_index == LUA_NOREF) {
    lua_newtable(L);
    references.m_index = lua_ref(L, true);
  }

  int index = references.m_children.size() + 1;
  Impl::CreateIndexedVariable(references, std::to_string(index), index);
  DebuggeeState& var = *references.m_children.back();
  bool ok = var.PopValue();
  ASSERT(lua_gettop(L) == top - 1);
  return ok ? var : Impl::g_null;
}

void DebuggeeState::Invalidate() {
  // Keep threads but delete other children.
  size_t keptItems = 0;
  for (size_t i = 0; i < m_children.size(); i++) {
    if (m_children[i]->GetType() != Type::Thread) {
      delete m_children[i];
      m_children[i] = nullptr;
    } else {
      m_children[i]->Invalidate();
      if (keptItems != i) {
        std::swap(m_children[keptItems], m_children[i]);
      }
      keptItems++;
    }
  }
  m_children.resize(keptItems);
  m_childrenInitialized = false;

  // Reset numbering.
  m_nextId = 1;
}

void DebuggeeState::AddThread(ThreadId threadId, lua_State* lua) {
  Impl::CreateThread(GetRoot(), threadId, lua);
}

void DebuggeeState::RemoveThread(ThreadId threadId) {
  DebuggeeState& root = GetRoot();
  for (auto it = root.m_children.begin(); it != root.m_children.end(); ++it) {
    DebuggeeState* thread = *it;
    if (thread->m_type == Type::Thread && thread->m_threadId == threadId) {
      root.m_children.erase(it);
      delete thread;
      return;
    }
  }
}

DebuggeeState::DebuggeeState(DebuggeeState& parent, Type type)
    : m_id(parent.GetRoot().m_nextId++),
      m_type(type),
      m_parent(&parent),
      m_lua(parent.m_lua),
      m_threadId(parent.m_threadId),
      m_stackLevel(parent.m_stackLevel) {}

DebuggeeState::~DebuggeeState() {
  if (m_id != 0) {
    GetRoot().m_objects.erase(m_id);
  }

  if (m_type == Type::References && m_index != LUA_NOREF) {
    lua_unref(m_lua, m_index);
    m_index = LUA_NOREF;
  }

  while (!m_children.empty()) {
    delete m_children.back();
    m_children.pop_back();
  }
}

}  // namespace LuaDebug

/*
 * Copyright (C) 2025  Arttu Ylä-Outinen
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
