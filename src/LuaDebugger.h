#ifndef LUA_DEBUGGER_H
#define LUA_DEBUGGER_H

#include <memory>

#include "LuaDebugDap.h"

struct lua_State;
struct lua_Debug;

namespace LuaDebug {

class Debugger {
 public:
  Debugger();
  ~Debugger();

  void ActivateState(lua_State* lua);
  void DeactivateState(lua_State* lua);

  void SendRequest(Request&& request);
  bool TryReceiveResponse(Response& message);

 private:
  class Impl;
  std::unique_ptr<Impl> m_impl;
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
