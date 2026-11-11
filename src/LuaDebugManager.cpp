#include "LuaDebugManager.h"

#include <RageTimer.h>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXSocket.h>
#include <ixwebsocket/IXSocketServer.h>
#include <ixwebsocket/IXUrlParser.h>
#include <json/json.h>

#include <atomic>
#include <climits>
#include <memory>
#include <string_view>

#include "LuaDebugDap.h"
#include "LuaDebugger.h"
#include "RageLog.h"
#include "global.h"

LuaDebugManager* LUADEBUG = nullptr;

namespace {
const char* const kDefaultAddress = "127.0.0.1:8173";
const int kDefaultTimeoutMs = 50;
}  // namespace

namespace LuaDebug {
class Server : public ix::SocketServer {
 public:
  static std::unique_ptr<Server> Start(
      Debugger& debugger, const std::string& address, bool start_paused) {
    // Split the address to host and port.
    std::string url =
        std::string("tcp://") + (address.empty() ? kDefaultAddress : address);
    std::string protocol;
    std::string host;
    std::string path;
    std::string query;
    int port;
    if (!ix::UrlParser::parse(url, protocol, host, path, query, port)) {
      LOG->Warn("LuaDebugger: invalid host/port");
      return nullptr;
    }

    std::unique_ptr<Server> server(new Server(debugger, host, port));
    std::pair<bool, std::string> listen_ok = server->listen();
    if (!listen_ok.first) {
      LOG->Warn("LuaDebugger: listen failed: %s", listen_ok.second.c_str());
      return nullptr;
    }

    if (start_paused) {
      debugger.SendRequest(LuaDebug::CreatePauseRequest(0));
    }

    server->server_running_.store(true, std::memory_order_release);
    server->start();
    return server;
  }

  ~Server() { stop(); }

  virtual void stop() override {
    server_running_.store(false, std::memory_order_release);
    ix::SocketServer::stop();

    // Resume execution with a fake disconnect request when terminating
    // the server thread.
    debugger_.SendRequest(LuaDebug::CreateDisconnectRequest(false));
  }

  virtual size_t getConnectedClientsCount() override {
    return connected_clients_;
  }

  virtual void handleConnection(
      std::unique_ptr<ix::Socket> socket,
      std::shared_ptr<ix::ConnectionState> connection_state) override {
    LOG->Info("LuaDebugger: client connected");
    connected_clients_ = 1;

    Serve(*socket);
    connection_state->setTerminated();

    // Send a fake disconnect request in case the connection was closed
    // unexpectedly.
    debugger_.SendRequest(LuaDebug::CreateDisconnectRequest(true));
    LOG->Info("LuaDebugger: client disconnected");
    connected_clients_ = 0;
  }

  void Serve(ix::Socket& socket) {
    bool initialized = false;
    SeqNo next_message_seq = 1;
    size_t buffer_length = 1024;
    size_t buffer_filled = 0;
    std::unique_ptr<char[]> buffer(new char[buffer_length]);

    while (server_running_.load(std::memory_order_relaxed)) {
      bool terminate = false;
      LuaDebug::Message response;
      while (debugger_.TryReceiveResponse(response)) {
        if (!initialized) {
          if (response["type"] == "response" &&
              response["command"] == "initialize") {
            initialized = true;
          } else {
            continue;
          }
        }

        response["seq"] = next_message_seq++;
        if (!SendDapMessage(socket, response)) {
          terminate = true;
          break;
        }

        if (response["type"] == "response" &&
            response["command"] == "disconnect") {
          terminate = true;
          break;
        }
      }
      if (terminate) {
        break;
      }

      ASSERT(buffer_filled <= buffer_length);
      if (std::string_view(buffer.get(), buffer_filled).find("\r\n\r\n") ==
          std::string_view::npos) {
        // We don't have a complete header; need to read more data.
        size_t free_space = buffer_length - buffer_filled;
        if (free_space == 0) {
          LOG->Warn("LuaDebugger: too big request header");
          break;
        }
        ASSERT(free_space <= INT_MAX);

        ix::PollResultType poll_result =
            socket.isReadyToRead(kDefaultTimeoutMs);
        if (poll_result == ix::PollResultType::Timeout) {
          continue;
        }
        if (poll_result != ix::PollResultType::ReadyForRead) {
          break;
        }

        ssize_t recv_result =
            socket.recv(buffer.get() + buffer_filled, free_space);
        if (recv_result < 0) {
          LOG->Warn("LuaDebugger: recv failed");
        }
        if (recv_result <= 0) {
          break;
        }
        ASSERT(recv_result <= (int)free_space);
        buffer_filled += recv_result;

        if (std::string_view(buffer.get(), buffer_filled).find("\r\n\r\n") ==
            std::string_view::npos) {
          continue;
        }
      }

      LuaDebug::Header header;
      if (!header.TryParse(buffer.get(), buffer_filled)) {
        LOG->Warn("LuaDebugger: invalid request header");
        break;
      }
      ASSERT(header.GetHeaderLength() <= buffer_filled);

      size_t total_size = header.GetHeaderLength() + header.GetContentLength();
      if (total_size > 10000000) {
        LOG->Warn("LuaDebugger: too big request");
        break;
      }

      if (buffer_length < total_size) {
        buffer_length = total_size;
        std::unique_ptr<char[]> new_buffer(new char[buffer_length]);
        memcpy(new_buffer.get(), buffer.get(), buffer_filled);
        buffer = std::move(new_buffer);
      }

      if (total_size > buffer_filled) {
        size_t bytes_missing = total_size - buffer_filled;
        ASSERT(bytes_missing <= INT_MAX);

        ix::OnProgressCallback on_progress;
        std::pair<bool, std::string> recv_result = socket.readBytes(
            bytes_missing, on_progress,
            [&buffer, &buffer_filled,
             &bytes_missing](const std::string& chunk) {
              ASSERT(chunk.length() <= bytes_missing);
              std::memcpy(
                  buffer.get() + buffer_filled, chunk.data(), chunk.length());
              buffer_filled += chunk.length();
              bytes_missing -= chunk.length();
            },
            [this] {
              return !server_running_.load(std::memory_order_relaxed);
            });
        if (!recv_result.first) {
          LOG->Warn("LuaDebugger: recv failed");
          break;
        }
        ASSERT(bytes_missing == 0);
      }

      LuaDebug::Request request;
      if (!request.TryParse(
              buffer.get() + header.GetHeaderLength(),
              buffer_filled - header.GetHeaderLength())) {
        LOG->Warn("LuaDebugger: invalid request body");
        break;
      }

      debugger_.SendRequest(std::move(request));

      // Move rest of the data to the start of the buffer.
      if (buffer_filled > total_size) {
        memcpy(
            buffer.get(), buffer.get() + total_size,
            buffer_filled - total_size);
      }
      buffer_filled -= total_size;
    }

    // Notify the client that it's getting disconnected.
    LuaDebug::Event terminated_event = LuaDebug::CreateTerminatedEvent();
    terminated_event["seq"] = next_message_seq++;
    SendDapMessage(socket, terminated_event);
  }

 private:
  std::atomic<bool> server_running_ = false;
  size_t connected_clients_ = 0;
  LuaDebug::Debugger& debugger_;

  Server(Debugger& debugger, const std::string& host, int port)
      : ix::SocketServer(port, host, 1, 1), debugger_(debugger) {}

  bool SendDapMessage(ix::Socket& socket, const LuaDebug::Message& message) {
    std::string body = Json::FastWriter{}.write(message);
    std::string data = std::string("Content-Length: ") +
                       std::to_string(body.length()) + "\r\n\r\n" + body;
    bool is_first_try = true;
    bool send_ok = socket.writeBytes(data, [this, &is_first_try] {
      if (is_first_try) {
        // If we can send the message without waiting, send it
        // event if the server is terminating. This way, the client
        // can receive the terminated event before the connection
        // is closed.
        is_first_try = false;
        return false;
      }
      return !server_running_.load(std::memory_order_relaxed);
    });

    if (!send_ok) {
      LOG->Warn("LuaDebugger: send failed");
    }
    return send_ok;
  }
};
}  // namespace LuaDebug

LuaDebugManager::LuaDebugManager()
    : debugger_(new LuaDebug::Debugger()), server_(nullptr) {}
LuaDebugManager::~LuaDebugManager() = default;

void LuaDebugManager::Start(std::string address, bool start_paused) {
  ix::initNetSystem();
  server_ = LuaDebug::Server::Start(*debugger_, address, start_paused);
}

void LuaDebugManager::ActivateState(lua_State* lua) {
  debugger_->ActivateState(lua);
}

void LuaDebugManager::DeactivateState(lua_State* lua) {
  debugger_->DeactivateState(lua);
}

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
