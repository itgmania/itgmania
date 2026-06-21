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
  static Breakpoint Function(BreakpointId id, const std::string& functionName);
  static Breakpoint Line(
      BreakpointId id, const std::string& fileName, int lineNumber);

  /* Checks if the breakpoint is hit, ignoring condition and hit count
   * condition. Fills in debug.source, debug.currentline, and debug.name as
   * needed. */
  bool IsHit(lua_State* L, lua_Debug& debug);
  bool EvaluateConditions(
      DebuggeeState& stackFrame, std::string& evaluationError);
  std::string EvaluateLogMessage(
      DebuggeeState& stackFrame, std::string& evaluationError);

  BreakpointId GetId() const { return m_id; }
  int GetEventMask() const { return m_eventMask; }
  const std::string& GetFileName() const { return m_fileName; }
  int GetLineNumber() const { return m_lineNumber; }
  int GetMaxStackDepth() const { return m_maxStackDepth; }
  const std::string& GetFunctionName() const { return m_functionName; }
  const lua_State* GetThread() const { return m_thread; }
  const std::string& GetReason() const { return m_reason; }
  bool IsTemporary() const { return m_temporary; }
  bool IsBreak() const { return m_logMessage.empty(); }

  void SetCondition(const std::string& condition) { m_condition = condition; }
  void SetHitCondition(uint64_t hitCondition) { m_hitCondition = hitCondition; }
  void SetLogMessage(const std::string& logMessage) {
    m_logMessage = logMessage;
  }

 private:
  Breakpoint() = default;

  /* Breakpoint identifier. */
  BreakpointId m_id = 0;

  /* Combination of the LUA_MASK* flags that can trigger this breakpoint. */
  int m_eventMask = 0;
  /* If non-empty, the breakpoint only triggers in the specific function. */
  std::string m_functionName;
  /* If non-empty, the breakpoint only triggers in the specific source file. */
  std::string m_fileName;
  /* If nonnegative, the breakpoint only triggers on a specific line number. */
  int m_lineNumber = -1;
  /* If nonnegative, the breakpoint only triggers when the call stack depth is
   * at or below the limit. */
  int m_maxStackDepth = -1;
  /* If non-null, the breakpoint only triggers on a specific thread. */
  const lua_State* m_thread = nullptr;
  /* If non-empty, the breakpoint only triggers if the condition is true. */
  std::string m_condition;
  /* If positive, the breakpoint only triggers after hit count reaches hit
   * condition. */
  uint64_t m_hitCondition = 0;
  /* Number of times the breakpoint has been hit. */
  uint64_t m_hitCount = 0;
  /* If non-empty, the breakpoint logs a message instead of stopping
   * execution when hit. */
  std::string m_logMessage;

  /* Reason reported when stopping at this breakpoint. */
  std::string m_reason;
  /* If true, the breakpoint is deleted after one hit. */
  bool m_temporary = false;
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
