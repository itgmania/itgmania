#include "LuaDebugBreakpoint.h"

#include <string>

#include "LuaDebugHelpers.h"
#include "LuaDebuggeeState.h"
#include "LuaManager.h"

namespace LuaDebug {
namespace {
static const int ALL_EVENTS = LUA_MASKCALL | LUA_MASKRET | LUA_MASKLINE;

int GetCallStackDepth(lua_State* L) {
  int depth;
  lua_Debug debug;
  for (depth = 0; lua_getstack(L, depth, &debug); depth++) {
  }
  return depth;
}
}  // namespace

Breakpoint Breakpoint::Pause() {
  Breakpoint bp;
  bp.m_eventMask = ALL_EVENTS;
  bp.m_reason = "pause";
  bp.m_temporary = true;
  return bp;
}

Breakpoint Breakpoint::Return(lua_State* L) {
  Breakpoint bp;
  bp.m_eventMask = ALL_EVENTS;
  bp.m_maxStackDepth = GetCallStackDepth(L) - 1;
  bp.m_thread = L;
  bp.m_reason = "step";
  bp.m_temporary = true;
  return bp;
}

Breakpoint Breakpoint::Step(lua_State* L) {
  Breakpoint bp;
  bp.m_eventMask = ALL_EVENTS;
  bp.m_thread = L;
  bp.m_reason = "step";
  bp.m_temporary = true;
  return bp;
}

Breakpoint Breakpoint::Next(lua_State* L) {
  Breakpoint bp;
  bp.m_eventMask = LUA_MASKLINE;
  bp.m_maxStackDepth = GetCallStackDepth(L);
  bp.m_thread = L;
  bp.m_reason = "step";
  bp.m_temporary = true;
  return bp;
}

Breakpoint Breakpoint::Function(
    BreakpointId id, const std::string& functionName) {
  Breakpoint bp;
  bp.m_id = id;
  bp.m_eventMask = LUA_MASKCALL;
  bp.m_functionName = functionName;
  bp.m_reason = "function breakpoint";
  return bp;
}

Breakpoint Breakpoint::Line(
    BreakpointId id, const std::string& fileName, int lineNumber) {
  Breakpoint bp;
  bp.m_id = id;
  bp.m_eventMask = LUA_MASKLINE;
  bp.m_fileName = fileName;
  bp.m_lineNumber = lineNumber;
  bp.m_reason = "breakpoint";
  return bp;
}

bool Breakpoint::IsHit(lua_State* L, lua_Debug& debug) {
  if ((m_eventMask & (1 << debug.event)) == 0) {
    return false;
  }
  if (m_thread != nullptr && m_thread != L) {
    return false;
  }
  if (!m_functionName.empty() &&
      (debug.name == nullptr || m_functionName != debug.name)) {
    return false;
  }
  if (!m_fileName.empty() &&
      (debug.source == nullptr || m_fileName != debug.source)) {
    return false;
  }
  if (m_lineNumber >= 0 && m_lineNumber != debug.currentline) {
    return false;
  }
  lua_Debug tmp;
  if (m_maxStackDepth >= 0 && lua_getstack(L, m_maxStackDepth, &tmp)) {
    return false;
  }
  return true;
}

bool Breakpoint::EvaluateConditions(
    DebuggeeState& stackFrame, std::string& evaluationError) {
  if (!m_condition.empty()) {
    lua_State* L = stackFrame.GetLua();
    int resultCount = LuaEvaluate(m_condition, stackFrame);
    if (resultCount < 0) {
      evaluationError = LuaValueToString(L, -1, false);
      lua_pop(L, 1);
      return true;
    }

    bool isTrue = resultCount > 0 && lua_toboolean(L, -resultCount);
    lua_pop(L, resultCount);
    if (!isTrue) {
      return false;
    }
  }

  if (m_hitCondition > 0 && ++m_hitCount < m_hitCondition) {
    return false;
  }

  m_hitCount = 0;
  return true;
}

std::string Breakpoint::EvaluateLogMessage(
    DebuggeeState& stackFrame, std::string& evaluationError) {
  // Evaluates any Lua expressions inside { } in the log message.

  if (m_logMessage.empty()) {
    return m_logMessage;
  }
  lua_State* L = stackFrame.GetLua();

  std::string result;
  result.reserve(m_logMessage.size());
  for (size_t i = 0; i < m_logMessage.size(); i++) {
    if (m_logMessage[i] == '{') {
      i++;
      if (i < m_logMessage.size() && m_logMessage[i] == '{') {
        // Double {{ escapes it.
        result.push_back('{');
      } else {
        // Try to find the end of the expression.
        size_t expressionStart = i;
        for (; i < m_logMessage.size(); i++) {
          if (m_logMessage[i] != '}') {
            continue;
          }
          // Found a closing }. Check if the substring from
          // expressionStart to i forms a syntactically valid Lua
          // expression.

          std::string expression =
              m_logMessage.substr(expressionStart, i - expressionStart);
          std::string returnExpression = "return " + expression;

          int loadstringResult = luaL_loadstring(L, returnExpression.c_str());
          if (loadstringResult == LUA_ERRSYNTAX) {
            lua_pop(L, 1);
            loadstringResult = luaL_loadstring(L, expression.c_str());
          }

          if (loadstringResult == LUA_ERRSYNTAX) {
            lua_pop(L, 1);
            continue;
          }

          if (loadstringResult != 0) {
            evaluationError = LuaValueToString(L, -1, false);
            lua_pop(L, 1);
            return {};
          }

          lua_pop(L, 1);
          int firstResultIndex = lua_gettop(L) + 1;
          int resultCount = LuaEvaluate(expression, stackFrame);
          if (resultCount < 0) {
            evaluationError = LuaValueToString(L, -1, false);
            lua_pop(L, 1);
            return {};
          }

          for (int resultIndex = 0; resultIndex < resultCount; resultIndex++) {
            if (resultIndex > 0) {
              result.append(", ");
            }
            result.append(LuaValueToString(L, firstResultIndex + resultIndex));
          }
          lua_pop(L, resultCount);
          break;
        }

        if (i >= m_logMessage.size()) {
          evaluationError = "Unbalanced \"{\"";
          return {};
        }
      }
    } else if (m_logMessage[i] == '}') {
      i++;
      if (i < m_logMessage.size() && m_logMessage[i] == '}') {
        // Double }} escapes it.
        result.push_back('}');
      } else {
        evaluationError = "Unbalanced \"}\"";
        return {};
      }
    } else {
      result.push_back(m_logMessage[i]);
    }
  }

  if (!result.empty()) {
    result.push_back('\n');
  }
  return result;
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
