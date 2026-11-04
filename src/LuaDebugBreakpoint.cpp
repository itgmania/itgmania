#include "LuaDebugBreakpoint.h"

#include <string>

#include "LuaDebugHelpers.h"
#include "LuaDebuggeeState.h"
#include "LuaManager.h"
#include "StdString.h"

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
  bp.event_mask_ = ALL_EVENTS;
  bp.reason_ = "pause";
  bp.temporary_ = true;
  return bp;
}

Breakpoint Breakpoint::Return(lua_State* L) {
  Breakpoint bp;
  bp.event_mask_ = ALL_EVENTS;
  bp.max_stack_depth_ = GetCallStackDepth(L) - 1;
  bp.thread_ = L;
  bp.reason_ = "step";
  bp.temporary_ = true;
  return bp;
}

Breakpoint Breakpoint::Step(lua_State* L) {
  Breakpoint bp;
  bp.event_mask_ = ALL_EVENTS;
  bp.thread_ = L;
  bp.reason_ = "step";
  bp.temporary_ = true;
  return bp;
}

Breakpoint Breakpoint::Next(lua_State* L) {
  Breakpoint bp;
  bp.event_mask_ = LUA_MASKLINE;
  bp.max_stack_depth_ = GetCallStackDepth(L);
  bp.thread_ = L;
  bp.reason_ = "step";
  bp.temporary_ = true;
  return bp;
}

Breakpoint Breakpoint::Function(
    BreakpointId id, const std::string& functionName) {
  Breakpoint bp;
  bp.id_ = id;
  bp.event_mask_ = LUA_MASKCALL;
  bp.function_name_ = functionName;
  bp.reason_ = "function breakpoint";
  return bp;
}

Breakpoint Breakpoint::Line(
    BreakpointId id, const std::string& fileName, int lineNumber) {
  Breakpoint bp;
  bp.id_ = id;
  bp.event_mask_ = LUA_MASKLINE;
  bp.file_name_ = fileName;
  bp.line_number_ = lineNumber;
  bp.reason_ = "breakpoint";
  return bp;
}

bool Breakpoint::IsHit(lua_State* L, lua_Debug& debug) {
  if ((event_mask_ & (1 << debug.event)) == 0) {
    return false;
  }

  if (thread_ != nullptr && thread_ != L) {
    return false;
  }

  if (line_number_ >= 0) {
    if (debug.currentline == INT_MIN) {
      lua_getinfo(L, "l", &debug);
    }
    if (line_number_ != debug.currentline) {
      return false;
    }
  }

  if (!function_name_.empty()) {
    if (debug.name == nullptr) {
      lua_getinfo(L, "n", &debug);
      if (debug.name == nullptr) {
        debug.name = "";
        return false;
      }
    }
    if (function_name_ != debug.name) {
      return false;
    }
  }

  if (!file_name_.empty()) {
    if (debug.source == nullptr) {
      lua_getinfo(L, "S", &debug);
      if (debug.source == nullptr) {
        debug.source = "";
        return false;
      } else {
        // Ignore leading "@/".
        if (debug.source[0] == '@') {
          debug.source++;
        }
        if (debug.source[0] == '/') {
          debug.source++;
        }
      }
    }
    if (!EqualsNoCase(file_name_, debug.source)) {
      return false;
    }
  }

  lua_Debug tmp;
  if (max_stack_depth_ >= 0 && lua_getstack(L, max_stack_depth_, &tmp)) {
    return false;
  }

  return true;
}

bool Breakpoint::EvaluateConditions(
    DebuggeeState& stackFrame, std::string& evaluationError) {
  if (!condition_.empty()) {
    lua_State* L = stackFrame.GetLua();
    int resultCount = LuaEvaluate(condition_, stackFrame);
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

  if (hit_condition_ > 0 && ++hit_count_ < hit_condition_) {
    return false;
  }

  hit_count_ = 0;
  return true;
}

std::string Breakpoint::EvaluateLogMessage(
    DebuggeeState& stackFrame, std::string& evaluationError) {
  // Evaluates any Lua expressions inside { } in the log message.

  if (log_message_.empty()) {
    return log_message_;
  }
  lua_State* L = stackFrame.GetLua();

  std::string result;
  result.reserve(log_message_.size());
  for (size_t i = 0; i < log_message_.size(); i++) {
    if (log_message_[i] == '{') {
      i++;
      if (i < log_message_.size() && log_message_[i] == '{') {
        // Double {{ escapes it.
        result.push_back('{');
      } else {
        // Try to find the end of the expression.
        size_t expressionStart = i;
        for (; i < log_message_.size(); i++) {
          if (log_message_[i] != '}') {
            continue;
          }
          // Found a closing }. Check if the substring from
          // expressionStart to i forms a syntactically valid Lua
          // expression.

          std::string expression =
              log_message_.substr(expressionStart, i - expressionStart);
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

        if (i >= log_message_.size()) {
          evaluationError = "Unbalanced \"{\"";
          return {};
        }
      }
    } else if (log_message_[i] == '}') {
      i++;
      if (i < log_message_.size() && log_message_[i] == '}') {
        // Double }} escapes it.
        result.push_back('}');
      } else {
        evaluationError = "Unbalanced \"}\"";
        return {};
      }
    } else {
      result.push_back(log_message_[i]);
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
