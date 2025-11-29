#ifndef LUA_DEBUG_HELPERS_H
#define LUA_DEBUG_HELPERS_H

#include <string>
#include <vector>

struct lua_State;

namespace LuaDebug {
class DebuggeeState;

/** Returns a Lua-quoted version of a string. */
std::string LuaQuote(const std::string& s);

/** Returns a string representation of a value on the Lua stack.
 * @param L Lua
 * @param index index on the stack
 * @param quote quote strings if true
 */
std::string LuaValueToString(lua_State* L, int index, bool quote = true);

/** Evaluate a Lua expression. Leaves the return values on the Lua
 * stack on success, or an error message on error.
 * @param expression expression to evaluate
 * @param frameOrThread stack frame or thread used to evaluate the expression
 * @return number of return values on success, or -1 on error */
int LuaEvaluate(const std::string& expression, DebuggeeState& frameOrThread);

/** Returns the absolute path to a Lua source file. */
std::string ResolvePath(const std::string& luaPath);

/** Returns a list of Lua source files that map to the absolute path. */
std::vector<std::string> UnresolvePath(const std::string& absolutePath);
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
