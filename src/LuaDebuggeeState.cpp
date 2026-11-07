#include "LuaDebuggeeState.h"

#include <string>

#include "LuaManager.h"
#include "global.h"

namespace LuaDebug {
class DebuggeeState::Impl {
 public:
  static void AddToParent(DebuggeeState* state) {
    state->parent_->children_.push_back(state);
    if (state->id_ != 0) {
      state->GetRoot().objects_.insert({state->id_, *state});
    }
  }

  static void CreateThread(
      DebuggeeState& root, ThreadId thread_id, lua_State* L) {
    auto thread = new DebuggeeState();
    thread->type_ = Type::Thread;
    thread->parent_ = &root, thread->lua_ = L, thread->thread_id_ = thread_id,
    AddToParent(thread);
  }

  static void CreateStackFrame(DebuggeeState& parent_thread, int level) {
    auto frame = new DebuggeeState(parent_thread, Type::StackFrame);
    frame->stack_level_ = level;
    AddToParent(frame);
  }

  static void CreateGlobals(DebuggeeState& parent_thread) {
    AddToParent(new DebuggeeState(parent_thread, Type::GlobalsScope));
  }

  static void CreateUpvalues(DebuggeeState& parent_frame) {
    AddToParent(new DebuggeeState(parent_frame, Type::UpvaluesScope));
  }

  static void CreateLocals(DebuggeeState& parent_frame) {
    AddToParent(new DebuggeeState(parent_frame, Type::LocalsScope));
  }

  static void CreateReferences(DebuggeeState& parent_thread) {
    auto references = new DebuggeeState(parent_thread, Type::References);
    references->index_ = LUA_NOREF;
    AddToParent(references);
  }

  static void CreateNamedVariable(
      DebuggeeState& parent, const std::string& name) {
    auto var = new DebuggeeState(parent, Type::NamedVariable);
    var->name_ = name;
    AddToParent(std::move(var));
  }

  static void CreateIndexedVariable(
      DebuggeeState& parent, const std::string& name, int index) {
    auto var = new DebuggeeState(parent, Type::IndexedVariable);
    var->name_ = name;
    var->index_ = index;
    AddToParent(std::move(var));
  }

  static void CreateMetaTableVariable(DebuggeeState& parent) {
    auto var = new DebuggeeState(parent, Type::MetaTableVariable);
    var->name_ = "__metatable";
    AddToParent(var);
  }

  static bool PushLocalToStack(DebuggeeState& var) {
    ASSERT(var.type_ == Type::IndexedVariable);
    ASSERT(var.parent_->type_ == Type::LocalsScope);

    lua_State* L = var.lua_;
    lua_Debug debug;
    if (lua_getstack(L, var.stack_level_, &debug) &&
        lua_getlocal(L, &debug, var.index_) != nullptr) {
      return true;
    }

    lua_pushnil(L);
    return false;
  }

  static bool PushUpvalueToStack(DebuggeeState& var) {
    ASSERT(var.type_ == Type::IndexedVariable);
    ASSERT(var.parent_->type_ == Type::UpvaluesScope);

    lua_State* L = var.lua_;
    lua_Debug debug;

    lua_getstack(L, var.stack_level_, &debug);
    // Pushes the active function onto the stack.
    lua_getinfo(L, "f", &debug);
    if (lua_isnil(L, -1)) {
      return false;
    }

    if (lua_getupvalue(L, -1, var.index_) == nullptr) {
      lua_pop(L, 1);
      lua_pushnil(L);
      return false;
    }

    // Remove the function from the stack.
    lua_remove(L, -2);
    return true;
  }

  static bool PushGlobalToStack(DebuggeeState& var) {
    ASSERT(var.parent_->type_ == Type::GlobalsScope);
    lua_State* L = var.lua_;

    if (var.type_ == Type::NamedVariable) {
      lua_pushstring(L, var.name_.c_str());
      lua_rawget(L, LUA_GLOBALSINDEX);
    } else if (var.type_ == Type::MetaTableVariable) {
      if (!lua_getmetatable(L, LUA_GLOBALSINDEX)) {
        return false;
      }
    } else {
      FAIL_M("invalid type");
    }

    return true;
  }

  static bool PushReferenceToStack(DebuggeeState& var) {
    ASSERT(var.type_ == Type::IndexedVariable);
    ASSERT(var.parent_->type_ == Type::References);
    if (var.parent_->index_ == LUA_NOREF) {
      return false;
    }

    lua_State* L = var.lua_;
    lua_getref(L, var.parent_->index_);
    lua_rawgeti(L, -1, var.index_);
    lua_remove(L, -2);
    return !lua_isnil(L, -1);
  }

  static bool PopLocalFromStack(DebuggeeState& var) {
    ASSERT(var.type_ == Type::IndexedVariable);
    ASSERT(var.parent_->type_ == Type::LocalsScope);

    lua_State* L = var.lua_;
    lua_Debug debug;
    if (!lua_getstack(L, var.stack_level_, &debug) ||
        lua_setlocal(L, &debug, var.index_) == nullptr) {
      lua_pop(L, 1);
      return false;
    }
    return true;
  }

  static bool PopUpvalueFromStack(DebuggeeState& var) {
    ASSERT(var.type_ == Type::IndexedVariable);
    ASSERT(var.parent_->type_ == Type::UpvaluesScope);

    lua_State* L = var.lua_;
    int top_on_return = lua_gettop(L) - 1;
    bool ok = false;

    lua_Debug debug;
    if (!lua_getstack(L, var.stack_level_, &debug)) {
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
    if (lua_setupvalue(L, -2, var.index_) == nullptr) {
      goto done;
    }
    ok = true;

  done:
    lua_settop(L, top_on_return);
    return ok;
  }

  static bool PopGlobalFromStack(DebuggeeState& var) {
    ASSERT(var.parent_->type_ == Type::GlobalsScope);
    lua_State* L = var.lua_;

    if (var.type_ == Type::NamedVariable) {
      lua_pushstring(L, var.name_.c_str());
      lua_insert(L, -2);
      lua_rawset(L, LUA_GLOBALSINDEX);
    } else if (var.type_ == Type::MetaTableVariable) {
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
    ASSERT(var.type_ == Type::IndexedVariable);
    ASSERT(var.parent_->type_ == Type::References);
    if (var.parent_->index_ == LUA_NOREF) {
      return false;
    }

    lua_State* L = var.lua_;
    lua_getref(L, var.parent_->index_);
    lua_insert(L, -2);
    lua_rawseti(L, -2, var.index_);
    lua_pop(L, 1);
    return true;
  }

  static void InitLocals(DebuggeeState& locals) {
    ASSERT(locals.type_ == Type::LocalsScope);
    if (locals.lua_ == nullptr) {
      return;
    }

    lua_State* L = locals.lua_;
    lua_Debug debug;
    lua_getstack(L, locals.stack_level_, &debug);

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
    ASSERT(upvalues.type_ == Type::UpvaluesScope);
    if (upvalues.lua_ == nullptr) {
      return;
    }

    lua_State* L = upvalues.lua_;
    lua_Debug debug;
    lua_getstack(L, upvalues.stack_level_, &debug);
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
    ASSERT(globals.type_ == Type::GlobalsScope);
    if (globals.lua_ == nullptr) {
      return;
    }

    lua_State* L = globals.lua_;
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
    if (var.lua_ == nullptr) {
      return;
    }

    lua_State* L = var.lua_;
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
      int key_type = lua_type(L, -2);
      if (key_type == LUA_TNUMBER) {
        int index = lua_tonumber(L, -2);
        CreateIndexedVariable(var, std::to_string(index), index);
      } else if (key_type == LUA_TSTRING) {
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
    if (thread.lua_ == nullptr) {
      return;
    }

    CreateGlobals(thread);
    CreateReferences(thread);

    lua_Debug debug;
    for (int level = 0; lua_getstack(thread.lua_, level, &debug); level++) {
      CreateStackFrame(thread, level);
    }
  }

  static void InitStackFrame(DebuggeeState& frame) {
    ASSERT(frame.type_ == Type::StackFrame);
    if (frame.lua_ == nullptr) {
      return;
    }

    lua_Debug debug;
    if (!lua_getstack(frame.lua_, frame.stack_level_, &debug)) {
      return;
    }
    if (!lua_getinfo(frame.lua_, "u", &debug)) {
      return;
    }

    CreateLocals(frame);
    if (debug.nups > 0) {
      CreateUpvalues(frame);
    }
  }

  static DebuggeeState null_;
};

DebuggeeState DebuggeeState::Impl::null_;

DebuggeeState::DebuggeeState(Root) : type_(Type::Root) {}

DebuggeeState::operator bool() const { return type_ != Type::None; }

DebuggeeState& DebuggeeState::GetRoot() {
  DebuggeeState* o = this;
  while (o->parent_) {
    o = o->parent_;
  }
  return *o;
}

DebuggeeState& DebuggeeState::GetThread() {
  DebuggeeState* o = this;
  while (o != nullptr && o->GetType() != Type::Thread) {
    o = o->parent_;
  }
  return o == nullptr ? Impl::null_ : *o;
}

DebuggeeState& DebuggeeState::GetStackFrame() {
  DebuggeeState* o = this;
  while (o != nullptr && o->GetType() != Type::StackFrame) {
    o = o->parent_;
  }
  return o == nullptr ? Impl::null_ : *o;
}

DebuggeeState& DebuggeeState::GetObject(ObjectRef id) {
  DebuggeeState& root = GetRoot();
  auto it = root.objects_.find(id);
  if (it == root.objects_.end()) {
    return Impl::null_;
  }
  return it->second;
}

DebuggeeState& DebuggeeState::GetThread(ThreadId thread_id) {
  for (DebuggeeState* thread : GetRoot().GetChildren()) {
    if (thread->GetType() == Type::Thread &&
        thread->GetThreadId() == thread_id) {
      return *thread;
    }
  }
  return Impl::null_;
}

DebuggeeState& DebuggeeState::GetThread(lua_State* L) {
  for (DebuggeeState* thread : GetRoot().GetChildren()) {
    if (thread->GetType() == Type::Thread && thread->GetLua() == L) {
      return *thread;
    }
  }
  return Impl::null_;
}

DebuggeeState& DebuggeeState::GetStackFrame(int level) {
  for (DebuggeeState* frame : GetThread().GetChildren()) {
    if (frame->GetType() == Type::StackFrame &&
        frame->GetStackLevel() == level) {
      return *frame;
    }
  }
  return Impl::null_;
}

DebuggeeState& DebuggeeState::GetGlobals() {
  for (DebuggeeState* child : GetThread().GetChildren()) {
    if (child->GetType() == Type::GlobalsScope) {
      return *child;
    }
  }
  return Impl::null_;
}

DebuggeeState& DebuggeeState::GetReferences() {
  for (DebuggeeState* child : GetThread().GetChildren()) {
    if (child->GetType() == Type::References) {
      return *child;
    }
  }
  return Impl::null_;
}

DebuggeeState& DebuggeeState::GetUpvalues() {
  for (DebuggeeState* child : GetStackFrame().GetChildren()) {
    if (child->GetType() == Type::UpvaluesScope) {
      return *child;
    }
  }
  return Impl::null_;
}

DebuggeeState& DebuggeeState::GetLocals() {
  for (DebuggeeState* child : GetStackFrame().GetChildren()) {
    if (child->GetType() == Type::LocalsScope) {
      return *child;
    }
  }
  return Impl::null_;
}

DebuggeeState& DebuggeeState::GetVariable(const std::string& name) {
  for (DebuggeeState* child : GetChildren()) {
    if (child->IsVariable() && child->GetName() == name) {
      return *child;
    }
  }
  return Impl::null_;
}

const std::vector<DebuggeeState*>& DebuggeeState::GetChildren() {
  if (!children_initialized_) {
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
    children_initialized_ = true;
  }
  return children_;
}

bool DebuggeeState::IsVariable() const {
  return type_ == Type::NamedVariable || type_ == Type::IndexedVariable ||
         type_ == Type::MetaTableVariable;
}

bool DebuggeeState::IsScope() const {
  return type_ == Type::GlobalsScope || type_ == Type::UpvaluesScope ||
         type_ == Type::LocalsScope;
}

bool DebuggeeState::IsVariablesContainer() const {
  return IsVariable() || IsScope() || type_ == Type::References;
}

bool DebuggeeState::IsTemporaryVariable() const {
  return IsVariable() && (name_.empty() || name_[0] == '(');
}

bool DebuggeeState::PushValue() {
  ASSERT(IsVariable());
  lua_State* L = lua_;
  int top = lua_gettop(L);

  std::vector<DebuggeeState*> path_to_root;
  for (DebuggeeState* o = this; o != nullptr && o->IsVariable();
       o = o->parent_) {
    path_to_root.push_back(o);
  }

  bool ok = false;
  DebuggeeState& first = *path_to_root.back();
  switch (first.parent_->type_) {
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
  path_to_root.pop_back();

  while (ok && !path_to_root.empty()) {
    int type = lua_type(L, -1);
    if (type != LUA_TTABLE) {
      lua_pop(L, 1);
      lua_pushnil(L);
      ok = false;
      break;
    }

    DebuggeeState& next = *path_to_root.back();
    switch (next.type_) {
      case Type::NamedVariable:
        lua_pushstring(L, next.name_.c_str());
        lua_rawget(L, -2);
        lua_remove(L, -2);
        break;
      case Type::IndexedVariable:
        lua_rawgeti(L, -1, next.index_);
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
    path_to_root.pop_back();
  }

  ASSERT(lua_gettop(L) == top + 1);
  return ok;
}

bool DebuggeeState::PopValue() {
  ASSERT(IsVariable());
  lua_State* L = lua_;
  int top = lua_gettop(L);

  bool ok = false;
  if (parent_->IsVariable()) {
    parent_->PushValue();
    switch (type_) {
      case Type::NamedVariable:
        if (lua_type(L, -1) == LUA_TTABLE) {
          lua_pushstring(L, name_.c_str());
          lua_pushvalue(L, -3);
          lua_rawset(L, -3);
          lua_pop(L, 2);
          ok = true;
        }
        break;

      case Type::IndexedVariable:
        if (lua_type(L, -1) == LUA_TTABLE) {
          lua_insert(L, -2);
          lua_rawseti(L, -2, index_);
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
    switch (parent_->type_) {
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
  lua_State* L = thread.lua_;
  int top = lua_gettop(L);

  DebuggeeState& references = thread.GetReferences();
  ASSERT(references);

  if (references.index_ == LUA_NOREF) {
    lua_newtable(L);
    references.index_ = lua_ref(L, true);
  }

  int index = references.children_.size() + 1;
  Impl::CreateIndexedVariable(references, std::to_string(index), index);
  DebuggeeState& var = *references.children_.back();
  bool ok = var.PopValue();
  ASSERT(lua_gettop(L) == top - 1);
  return ok ? var : Impl::null_;
}

void DebuggeeState::Invalidate() {
  // Keep threads but delete other children.
  size_t kept_items = 0;
  for (size_t i = 0; i < children_.size(); i++) {
    if (children_[i]->GetType() != Type::Thread) {
      delete children_[i];
      children_[i] = nullptr;
    } else {
      children_[i]->Invalidate();
      if (kept_items != i) {
        std::swap(children_[kept_items], children_[i]);
      }
      kept_items++;
    }
  }
  children_.resize(kept_items);
  children_initialized_ = false;

  // Reset numbering.
  next_id_ = 1;
}

void DebuggeeState::AddThread(ThreadId thread_id, lua_State* lua) {
  Impl::CreateThread(GetRoot(), thread_id, lua);
}

void DebuggeeState::RemoveThread(ThreadId thread_id) {
  DebuggeeState& root = GetRoot();
  for (auto it = root.children_.begin(); it != root.children_.end(); ++it) {
    DebuggeeState* thread = *it;
    if (thread->type_ == Type::Thread && thread->thread_id_ == thread_id) {
      root.children_.erase(it);
      delete thread;
      return;
    }
  }
}

DebuggeeState::DebuggeeState(DebuggeeState& parent, Type type)
    : id_(parent.GetRoot().next_id_++),
      type_(type),
      parent_(&parent),
      lua_(parent.lua_),
      thread_id_(parent.thread_id_),
      stack_level_(parent.stack_level_) {}

DebuggeeState::~DebuggeeState() {
  if (id_ != 0) {
    GetRoot().objects_.erase(id_);
  }

  if (type_ == Type::References && index_ != LUA_NOREF) {
    lua_unref(lua_, index_);
    index_ = LUA_NOREF;
  }

  while (!children_.empty()) {
    delete children_.back();
    children_.pop_back();
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
