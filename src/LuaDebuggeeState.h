#ifndef LUA_DEBUGGEE_STATE_H
#define LUA_DEBUGGEE_STATE_H

#include <string>
#include <unordered_map>
#include <vector>

struct lua_State;

namespace LuaDebug {
typedef int ThreadId;
typedef int ObjectRef;

/** Identifies a Lua thread, stack frame, scope, variable etc that can be
 * inspected when execution is paused. */
class DebuggeeState {
 public:
  enum class Type {
    None,
    Root,
    Thread,
    StackFrame,
    References,
    GlobalsScope,
    UpvaluesScope,
    LocalsScope,
    NamedVariable,
    IndexedVariable,
    MetaTableVariable,
  };

  struct Root {};

  /** Creates the root state. */
  DebuggeeState(Root);
  ~DebuggeeState();
  DebuggeeState(const DebuggeeState&) = delete;
  DebuggeeState& operator=(const DebuggeeState&) = delete;

  explicit operator bool() const;

  ObjectRef GetId() const { return id_; }
  Type GetType() const { return type_; }
  lua_State* GetLua() { return lua_; }
  ThreadId GetThreadId() const { return thread_id_; }
  int GetStackLevel() const { return stack_level_; }
  const std::string& GetName() const { return name_; }
  int GetIndex() const { return index_; }

  DebuggeeState& GetRoot();
  DebuggeeState& GetThread();
  DebuggeeState& GetStackFrame();

  DebuggeeState& GetObject(ObjectRef id);
  DebuggeeState& GetThread(ThreadId threadId);
  DebuggeeState& GetThread(lua_State* L);
  DebuggeeState& GetStackFrame(int level);
  DebuggeeState& GetGlobals();
  DebuggeeState& GetReferences();
  DebuggeeState& GetUpvalues();
  DebuggeeState& GetLocals();
  DebuggeeState& GetVariable(const std::string& name);
  const std::vector<DebuggeeState*>& GetChildren();
  bool IsVariable() const;
  bool IsScope() const;
  bool IsVariablesContainer() const;
  bool IsTemporaryVariable() const;

  /** Pushes the value of this variable onto the Lua stack. Must have
   * IsVariable() == true. On failure, returns false and pushes nil. */
  bool PushValue();

  /** Pops a value from the Lua stack and tries to store it in this
   * variable. Must have IsVariable() == true. */
  bool PopValue();

  /** Pops a value from the Lua stack and tries to create a new reference
   * to it. Must have IsVariable() == true. */
  DebuggeeState& CreateReferenceFromStack();

  /** Called before resuming Lua execution to invalidate all stack frames
   * and reset reference numbering. */
  void Invalidate();

  /** Called when a new Lua thread is created. */
  void AddThread(ThreadId threadId, lua_State* lua);
  /** Called when a Lua thread terminates. */
  void RemoveThread(ThreadId threadId);

 private:
  class Impl;
  DebuggeeState() = default;
  DebuggeeState(DebuggeeState& parent, Type type);

  ObjectRef id_ = 0;
  Type type_ = Type::None;
  DebuggeeState* parent_ = nullptr;

  lua_State* lua_ = nullptr;
  ThreadId thread_id_ = 0;
  int stack_level_ = 0;
  std::string name_;
  int index_ = 0;

  std::vector<DebuggeeState*> children_;
  bool children_initialized_ = false;
  std::unordered_map<ObjectRef, DebuggeeState&> objects_;
  int next_id_ = 1;
};
}  // namespace LuaDebug

#endif

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
