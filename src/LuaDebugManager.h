#ifndef LUA_DEBUG_MANAGER_H
#define LUA_DEBUG_MANAGER_H

#include <memory>
#include <string>

struct lua_State;

namespace LuaDebug {
class Debugger;
class Server;
}  // namespace LuaDebug

/* Debugger server for Lua code. Depends on LogManager and RageFileManager. */
class LuaDebugManager {
 public:
  LuaDebugManager();
  ~LuaDebugManager();

  /** Starts the debug server.
   * @param address address to listen on
   * @param startPaused if true, pauses Lua at first line of code executed */
  void Start(std::string address, bool startPaused = false);

  /* Signals the debug server to stop. Blocks until the server stops. */
  void Stop();

  /* Marks a Lua state as active for the debugger. The calling thread should
  be holding the global Lua mutex in LuaManager. */
  void ActivateState(lua_State* lua);

  /* Marks a Lua state as inactive for the debugger. The calling thread
  should be holding the global Lua mutex in LuaManager. */
  void DeactivateState(lua_State* lua);

 private:
  std::unique_ptr<LuaDebug::Debugger> m_debugger;
  std::unique_ptr<LuaDebug::Server> m_server;
};

extern LuaDebugManager* LUADEBUG;

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
