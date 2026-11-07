#ifndef LUA_DEBUG_DAP
#define LUA_DEBUG_DAP

#include <cstdint>
#include <string>
#include <vector>

#include "LuaDebugBreakpoint.h"
#include "json/json.h"

struct lua_State;

namespace LuaDebug {
/* Debug Adapter Protocol requests and responses. */

typedef int ObjectRef;
typedef int ThreadId;
typedef uint64_t SeqNo;

class Header {
 public:
  size_t GetHeaderLength() const { return header_length_; }
  size_t GetContentLength() const { return content_length_; }
  bool TryParse(const char* buffer, size_t length);

 private:
  size_t header_length_;
  size_t content_length_;
};

class Message : public Json::Value {
 public:
  bool TryParse(const char* buffer, size_t length);
};

typedef Message Request;
typedef Message Response;
typedef Message Event;

Request CreateDapRequest(const std::string& command);
Request CreatePauseRequest(ThreadId thread_id);
Request CreateContinueRequest(ThreadId thread_id);
Request CreateDisconnectRequest(bool suspend_debuggee);

Event CreateDapEvent(const std::string& event);
Event CreateStoppedEvent(
    ThreadId thread_id, const std::string& reason,
    const std::vector<BreakpointId>& hit_breakpoints);
Event CreateBreakpointEvaluationErrorEvent(
    ThreadId thread_id, const std::string& errors,
    const std::vector<BreakpointId>& hit_breakpoints);
Event CreateContinuedEvent(ThreadId thread_id);
Event CreateOutputEvent(
    const std::string& output, const std::string& source_name,
    const std::string& source, int line_number);
Event CreateThreadStartedEvent(ThreadId thread_id);
Event CreateTerminatedEvent();

Response CreateDapResponse(const Request& request);
Response CreateErrorResponse(
    const Request& request, const std::string& message);
Response CreateNotStoppedResponse(const Request& request);

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
