#include "LuaDebugDap.h"

namespace LuaDebug {
bool LuaDebug::Header::TryParse(const char* buffer, size_t length) {
  header_length_ = 0;
  content_length_ = 0;
  if (length == 0) {
    return false;
  }

  std::string_view buffer_view(buffer, length);
  size_t header_end = buffer_view.find("\r\n\r\n");
  if (header_end == std::string::npos) {
    return false;
  }
  // Include one trailing "\r\n" in the view.
  std::string_view header_view(buffer, header_end + 2);

  int content_length = 0;
  for (size_t offset = 0, end = 0;
       (end = header_view.find("\r\n", offset)) != std::string_view::npos;
       offset = end + 2) {
    std::string_view field_view = header_view.substr(offset, end - offset);
    size_t name_end = field_view.find(": ");
    if (name_end == std::string_view::npos) {
      return false;
    }
    if (field_view.substr(0, name_end) == "Content-Length") {
      std::string value_string = std::string(field_view.substr(name_end + 2));
      content_length = std::atoi(value_string.c_str());
    }
  }
  if (content_length <= 0) {
    return false;
  }

  header_length_ = header_end + 4;
  content_length_ = content_length;
  return true;
}

bool LuaDebug::Message::TryParse(const char* buffer, size_t length) {
  Json::Reader reader(Json::Features::strictMode());
  return reader.parse(buffer, buffer + length, *this);
}

Request CreateDapRequest(const std::string& command) {
  Request request;
  request["seq"] = (SeqNo)0;
  request["type"] = "request";
  request["command"] = command;
  return request;
}

Request CreatePauseRequest(ThreadId thread_id) {
  Request request = CreateDapRequest("pause");
  request["arguments"]["threadId"] = thread_id;
  return request;
}

Request CreateContinueRequest(ThreadId thread_id) {
  Request request = CreateDapRequest("continue");
  request["body"]["threadId"] = thread_id;
  return request;
}

Request CreateDisconnectRequest(bool suspend_debuggee) {
  Request request = CreateDapRequest("disconnect");
  request["arguments"]["suspendDebuggee"] = suspend_debuggee;
  return request;
}

Response CreateDapResponse(const Request& request) {
  Response response;
  response["seq"] = (SeqNo)0;
  response["type"] = "response";
  response["request_seq"] = request["seq"];
  response["success"] = true;
  response["command"] = request["command"];
  return response;
}

Response CreateErrorResponse(
    const Request& request, const std::string& message) {
  Response response = CreateDapResponse(request);
  response["success"] = false;
  response["message"] = message;
  return response;
}

Response CreateNotStoppedResponse(const Request& request) {
  return CreateErrorResponse(request, "notStopped");
}

Event CreateDapEvent(const std::string& name) {
  Event event;
  event["seq"] = (SeqNo)0;
  event["type"] = "event";
  event["event"] = name;
  return event;
}

Event CreateStoppedEvent(
    ThreadId thread_id, const std::string& reason,
    const std::vector<BreakpointId>& hit_breakpoints) {
  Event event = CreateDapEvent("stopped");
  event["body"]["reason"] = reason;
  event["body"]["threadId"] = thread_id;
  event["body"]["allThreadsStopped"] = true;
  if (!hit_breakpoints.empty()) {
    event["body"]["hitBreakpointIds"] = Json::Value(Json::arrayValue);
    for (BreakpointId id : hit_breakpoints) {
      if (id != 0) {
        event["body"]["hitBreakpointIds"].append(id);
      }
    }
  }
  return event;
}

Event CreateBreakpointEvaluationErrorEvent(
    ThreadId thread_id, const std::string& errors,
    const std::vector<BreakpointId>& hit_breakpoints) {
  Event event = CreateDapEvent("stopped");
  event["body"]["reason"] = "exception";
  event["body"]["description"] = "Error evaluating breakpoint";
  event["body"]["text"] = errors;
  event["body"]["threadId"] = thread_id;
  event["body"]["allThreadsStopped"] = true;
  event["body"]["hitBreakpointIds"] = Json::Value(Json::arrayValue);
  for (BreakpointId id : hit_breakpoints) {
    if (id != 0) {
      event["body"]["hitBreakpointIds"].append(id);
    }
  }
  return event;
}

Event CreateContinuedEvent(ThreadId thread_id) {
  Event event = CreateDapEvent("continued");
  event["body"]["threadId"] = thread_id;
  event["body"]["allThreadsContinued"] = true;
  return event;
}

Event CreateOutputEvent(
    const std::string& output, const std::string& source_name,
    const std::string& source, int line_number) {
  Event event = CreateDapEvent("output");
  event["body"]["output"] = output;
  if (!source_name.empty()) {
    event["body"]["source"]["name"] = source_name;
  }
  if (!source.empty()) {
    event["body"]["source"]["path"] = source;
  }
  if (line_number >= 0) {
    event["body"]["line"] = line_number;
  }
  return event;
}

Event CreateThreadStartedEvent(ThreadId thread_id) {
  Event event = CreateDapEvent("thread");
  event["body"]["reason"] = "started";
  event["body"]["threadId"] = thread_id;
  return event;
}

Event CreateTerminatedEvent() { return CreateDapEvent("terminated"); }

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
