#ifndef LUA_DEBUG_BREAKPOINT
#define LUA_DEBUG_BREAKPOINT

#include <cstdint>
#include <string>

struct lua_State;
struct lua_Debug;

namespace LuaDebug {
class DebuggeeState;

typedef uint64_t BreakpointId;

/* Breakpoint in Lua code. */
class Breakpoint {
 public:
  static Breakpoint Pause();
  static Breakpoint Return(lua_State* L);
  static Breakpoint Step(lua_State* L);
  static Breakpoint Next(lua_State* L);
  static Breakpoint Function(BreakpointId id, const std::string& function_name);
  static Breakpoint Line(
      BreakpointId id, const std::string& file_name, int line_number);

  /* Checks if the breakpoint is hit, ignoring condition and hit count
   * condition. Fills in debug.source, debug.currentline, and debug.name as
   * needed. */
  bool IsHit(lua_State* L, lua_Debug& debug);
  bool EvaluateConditions(
      DebuggeeState& stack_frame, std::string& evaluation_error);
  std::string EvaluateLogMessage(
      DebuggeeState& stack_frame, std::string& evaluation_error);

  BreakpointId GetId() const { return id_; }
  int GetEventMask() const { return event_mask_; }
  const std::string& GetFileName() const { return file_name_; }
  const std::string& GetFunctionName() const { return function_name_; }
  const lua_State* GetThread() const { return thread_; }
  const std::string& GetReason() const { return reason_; }
  bool IsTemporary() const { return temporary_; }
  bool IsBreak() const { return log_message_.empty(); }

  void SetCondition(const std::string& condition) { condition_ = condition; }
  void SetHitCondition(uint64_t hit_condition) {
    hit_condition_ = hit_condition;
  }
  void SetLogMessage(const std::string& log_message) {
    log_message_ = log_message;
  }

 private:
  Breakpoint() = default;

  /* Breakpoint identifier. */
  BreakpointId id_ = 0;

  /* Combination of the LUA_MASK* flags that can trigger this breakpoint. */
  int event_mask_ = 0;
  /* If non-empty, the breakpoint only triggers in the specific function. */
  std::string function_name_;
  /* If non-empty, the breakpoint only triggers in the specific source file. */
  std::string file_name_;
  /* If nonnegative, the breakpoint only triggers on a specific line number. */
  int line_number_ = -1;
  /* If nonnegative, the breakpoint only triggers when the call stack depth is
   * at or below the limit. */
  int max_stack_depth_ = -1;
  /* If non-null, the breakpoint only triggers on a specific thread. */
  const lua_State* thread_ = nullptr;
  /* If non-empty, the breakpoint only triggers if the condition is true. */
  std::string condition_;
  /* If positive, the breakpoint only triggers after hit count reaches hit
   * condition. */
  uint64_t hit_condition_ = 0;
  /* Number of times the breakpoint has been hit. */
  uint64_t hit_count_ = 0;
  /* If non-empty, the breakpoint logs a message instead of stopping
   * execution when hit. */
  std::string log_message_;

  /* Reason reported when stopping at this breakpoint. */
  std::string reason_;
  /* If true, the breakpoint is deleted after one hit. */
  bool temporary_ = false;
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
