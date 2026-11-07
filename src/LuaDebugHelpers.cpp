#include "LuaDebugHelpers.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "LuaDebuggeeState.h"
#include "LuaManager.h"
#include "RageFile.h"
#include "RageFileManager.h"
#include "StdString.h"
#include "global.h"

namespace LuaDebug {
std::string LuaQuote(const std::string& s) {
  std::string result;
  result.reserve(s.size() + 2);
  result.push_back('"');
  for (unsigned char c : s) {
    // Special escapes
    if (c == '\a') {
      result.append("\\a");
    } else if (c == '\b') {
      result.append("\\b");
    } else if (c == '\f') {
      result.append("\\f");
    } else if (c == '\n') {
      result.append("\\n");
    } else if (c == '\r') {
      result.append("\\r");
    } else if (c == '\t') {
      result.append("\\t");
    } else if (c == '\v') {
      result.append("\\v");
    } else if (c == '\\') {
      result.append("\\\\");
    } else if (c == '"') {
      result.append("\\\"");
    }
    // Printable characters
    else if (0x20 <= c && c <= 0x7e) {
      result.push_back(c);
    }
    // Numerical escapes
    else {
      result.push_back('\\');
      result.push_back(c / 100 + '0');
      c %= 100;
      result.push_back(c / 10 + '0');
      c %= 10;
      result.push_back(c + '0');
    }
  }
  result.push_back('"');
  return result;
}

std::string LuaValueToString(lua_State* L, int index, bool quote) {
  std::string value;
  int type = lua_type(L, index);
  switch (type) {
    case LUA_TNIL:
      value = "nil";
      break;
    case LUA_TBOOLEAN:
      value = lua_toboolean(L, index) ? "true" : "false";
      break;
    case LUA_TLIGHTUSERDATA:
      value = "<lightuserdata>";
      break;

    case LUA_TNUMBER: {
      // Make a copy first since calling lua_tostring on a number
      // changes the value on the stack to a string.
      lua_pushvalue(L, index);
      size_t length;
      const char* buffer = lua_tolstring(L, -1, &length);
      value = std::string(buffer, length);
      lua_pop(L, 1);
      break;
    }

    case LUA_TSTRING: {
      size_t length;
      const char* buffer = lua_tolstring(L, index, &length);
      value = std::string(buffer, length);
      if (quote) {
        value = LuaQuote(value);
      }
      break;
    }

    case LUA_TTABLE:
      value = "{...}";
      break;
    case LUA_TFUNCTION:
      value = "<function>";
      break;
    case LUA_TUSERDATA:
      value = "<userdata>";
      break;
    case LUA_TTHREAD:
      value = "<thread>";
      break;
  }
  return value;
}

int LuaEvaluate(const std::string& expression, DebuggeeState& frame_or_thread) {
  ASSERT(frame_or_thread);

  DebuggeeState& frame = frame_or_thread.GetStackFrame();
  std::vector<DebuggeeState*> scopes{&frame.GetUpvalues(), &frame.GetLocals()};
  lua_Debug debug = {};
  lua_State* L = frame_or_thread.GetLua();
  ASSERT(L != nullptr);
  // Current Lua stack: [...]

  // Add a "return" in case the expression is not a full statement.
  std::string return_expression = "return " + expression;
  int loadstring_result = luaL_loadstring(L, return_expression.c_str());
  // [..., error or expression]
  if (loadstring_result == LUA_ERRSYNTAX) {
    lua_pop(L, 1);
    // [...]
    // Try again without the return.
    loadstring_result = luaL_loadstring(L, expression.c_str());
    // [..., error or expression]
  }

  if (loadstring_result != 0) {
    return -1;
  }

  if (frame) {
    // Get environment from the stack frame.
    lua_newtable(L);
    // [..., expression, environment]

    for (DebuggeeState* scope : scopes) {
      if (!*scope) {
        continue;
      }
      for (DebuggeeState* var : scope->GetChildren()) {
        if (!var->IsVariable() || var->IsTemporaryVariable()) {
          continue;
        }
        lua_pushstring(L, var->GetName().c_str());
        // [..., expression, environment, name]
        if (var->PushValue()) {  // [..., expression, environment, name, value]
          lua_rawset(L, -3);     // [..., expression, environment]
        } else {
          lua_pop(L, 1);  // [..., expression, environment]
        }
      }
    }

    // Set "__index" of the environment's metatable to the
    // functions environment to get access to globals.
    lua_newtable(L);
    // [..., expression, environment, env metatable]
    lua_pushstring(L, "__index");
    // [..., expression, environment, env metatable, "__index"]
    lua_getstack(L, frame.GetStackLevel(), &debug);
    lua_getinfo(L, "f", &debug);
    // [..., expression, environment, env metatable, "__index", current
    // function]
    lua_getfenv(L, -1);
    // [..., expression, environment, env metatable, "__index", current
    // function, function environment]
    lua_remove(L, -2);
    // [..., expression, environment, env metatable, "__index", function
    // environment]
    lua_rawset(L, -3);
    // [..., expression, environment, env metatable]
    lua_setmetatable(L, -2);
    // [..., expression, environment]
  } else {
    // Use the global environment.
    lua_pushvalue(L, LUA_GLOBALSINDEX);
    // [..., expression, global environment]
  }

  lua_pushvalue(L, -1);
  // [..., expression, environment, environment]
  lua_insert(L, -3);
  // [..., environment, expression, environment]
  int setfenv_result = lua_setfenv(L, -2);
  ASSERT(setfenv_result == 1);
  // [..., environment, expression]

  int environment_index = lua_gettop(L) - 1;
  if (lua_pcall(L, 0, LUA_MULTRET, 0) != 0) {
    // [..., environment, error]
    lua_remove(L, -2);
    // [..., error]
    return -1;
  }
  // [..., environment, return value 1, return value 2, etc]
  int result_count = lua_gettop(L) - environment_index;
  ASSERT(result_count >= 0);

  if (frame) {
    // Copy locals back to the function.
    lua_pushnil(L);
    // [..., environment, return values, nil]
    while (lua_next(L, environment_index) != 0) {
      // [..., environment, return values, key, value]
      std::string var_name = LuaValueToString(L, -2, false);
      bool found = false;
      for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope) {
        if (!*scope) {
          continue;
        }
        DebuggeeState& var = (*scope)->GetVariable(var_name);
        if (var) {
          var.PopValue();
          found = true;
          break;
        }
      }
      if (!found) {
        lua_pop(L, 1);
      }
      // [..., environment, return values, key]
    }
    // [..., environment, return values]
  }

  lua_remove(L, environment_index);
  // [..., return values]

  return result_count;
}

std::string ResolvePath(const std::string& lua_path) {
  std::string_view path_view(lua_path);
  // Drop leading '@'
  if (path_view.substr(0, 1) != "@") {
    return "";
  }
  path_view = path_view.substr(1);

  int error = 0;
  std::unique_ptr<RageFileBasic> source_file(
      FILEMAN->Open(std::string(path_view), RageFile::READ, error));
  if (source_file) {
    return source_file->GetDisplayPath();
  }

  // Fall back to the Lua path without the leading '/'
  if (path_view.substr(0, 1) == "/") {
    path_view = path_view.substr(1);
  }

  return std::string(path_view);
}

std::vector<std::string> UnresolvePath(const std::string& absolute_path) {
  std::string abs_path = absolute_path;
  Replace(abs_path, '\\', '/');

  std::vector<RageFileManager::DriverLocation> mounts;
  FILEMAN->GetLoadedDrivers(mounts);
  std::vector<std::string> result;
  for (RageFileManager::DriverLocation& mount : mounts) {
    std::string root = mount.root_;
    if (Right(root, 1) != "/") {
      root.append("/");
    }
    if (root == "/") {
      continue;
    }
    if (CompareNoCase(Left(abs_path, root.size()), root)) {
      continue;
    }

    std::string path = Right(abs_path, abs_path.size() - root.size());
    std::string lua_path = mount.mount_point_ + path;
    if (Left(lua_path, 1) == "/") {
      lua_path = lua_path.substr(1);
    }
    result.push_back(lua_path);
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
