#include "LuaDebugDap.h"

namespace LuaDebug {
bool LuaDebug::Header::TryParse(const char* buffer, size_t length) {
  m_headerLength = 0;
  m_contentLength = 0;
  if (length == 0) {
    return false;
  }

  std::string_view bufferView(buffer, length);
  size_t headerEnd = bufferView.find("\r\n\r\n");
  if (headerEnd == std::string::npos) {
    return false;
  }
  // Include one trailing "\r\n" in the view.
  std::string_view headerView(buffer, headerEnd + 2);

  int contentLength = 0;
  for (size_t offset = 0, end = 0;
       (end = headerView.find("\r\n", offset)) != std::string_view::npos;
       offset = end + 2) {
    std::string_view fieldView = headerView.substr(offset, end - offset);
    size_t nameEnd = fieldView.find(": ");
    if (nameEnd == std::string_view::npos) {
      return false;
    }
    if (fieldView.substr(0, nameEnd) == "Content-Length") {
      std::string valueString = std::string(fieldView.substr(nameEnd + 2));
      contentLength = std::atoi(valueString.c_str());
    }
  }
  if (contentLength <= 0) {
    return false;
  }

  m_headerLength = headerEnd + 4;
  m_contentLength = contentLength;
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

Request CreatePauseRequest(ThreadId threadId) {
  Request request = CreateDapRequest("pause");
  request["arguments"]["threadId"] = threadId;
  return request;
}

Request CreateContinueRequest(ThreadId threadId) {
  Request request = CreateDapRequest("continue");
  request["body"]["threadId"] = threadId;
  return request;
}

Request CreateDisconnectRequest(bool suspendDebuggee) {
  Request request = CreateDapRequest("disconnect");
  request["arguments"]["suspendDebuggee"] = suspendDebuggee;
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
    ThreadId threadId, const std::string& reason,
    const std::vector<BreakpointId>& hitBreakpoints) {
  Event event = CreateDapEvent("stopped");
  event["body"]["reason"] = reason;
  event["body"]["threadId"] = threadId;
  event["body"]["allThreadsStopped"] = true;
  if (!hitBreakpoints.empty()) {
    event["body"]["hitBreakpointIds"] = Json::Value(Json::arrayValue);
    for (BreakpointId id : hitBreakpoints) {
      if (id != 0) {
        event["body"]["hitBreakpointIds"].append(id);
      }
    }
  }
  return event;
}

Event CreateBreakpointEvaluationErrorEvent(
    ThreadId threadId, const std::string& errors,
    const std::vector<BreakpointId>& hitBreakpoints) {
  Event event = CreateDapEvent("stopped");
  event["body"]["reason"] = "exception";
  event["body"]["description"] = "Error evaluating breakpoint";
  event["body"]["text"] = errors;
  event["body"]["threadId"] = threadId;
  event["body"]["allThreadsStopped"] = true;
  event["body"]["hitBreakpointIds"] = Json::Value(Json::arrayValue);
  for (BreakpointId id : hitBreakpoints) {
    if (id != 0) {
      event["body"]["hitBreakpointIds"].append(id);
    }
  }
  return event;
}

Event CreateContinuedEvent(ThreadId threadId) {
  Event event = CreateDapEvent("continued");
  event["body"]["threadId"] = threadId;
  event["body"]["allThreadsContinued"] = true;
  return event;
}

Event CreateOutputEvent(
    const std::string& output, const std::string& sourceName,
    const std::string& source, int lineNumber) {
  Event event = CreateDapEvent("output");
  event["body"]["output"] = output;
  if (!sourceName.empty()) {
    event["body"]["source"]["name"] = sourceName;
  }
  if (!source.empty()) {
    event["body"]["source"]["path"] = source;
  }
  if (lineNumber >= 0) {
    event["body"]["line"] = lineNumber;
  }
  return event;
}

Event CreateThreadStartedEvent(ThreadId threadId) {
  Event event = CreateDapEvent("thread");
  event["body"]["reason"] = "started";
  event["body"]["threadId"] = threadId;
  return event;
}

Event CreateThreadExitedEvent(ThreadId threadId) {
  Event event = CreateDapEvent("thread");
  event["body"]["reason"] = "exited";
  event["body"]["threadId"] = threadId;
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
