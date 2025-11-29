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
const char* const DefaultAddress = "127.0.0.1:8173";
const int DefaultTimeoutMs = 50;
}  // namespace

namespace LuaDebug {
class Server : public ix::SocketServer {
 public:
  static std::unique_ptr<Server> Start(
      Debugger& debugger, const std::string& address, bool startPaused) {
    // Split the address to host and port.
    std::string url =
        std::string("tcp://") + (address.empty() ? DefaultAddress : address);
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
    std::pair<bool, std::string> listenOk = server->listen();
    if (!listenOk.first) {
      LOG->Warn("LuaDebugger: listen failed: %s", listenOk.second.c_str());
      return nullptr;
    }

    if (startPaused) {
      debugger.SendRequest(LuaDebug::CreatePauseRequest(0));
    }

    server->m_serverRunning.store(true, std::memory_order_release);
    server->start();
    return server;
  }

  ~Server() { stop(); }

  virtual void stop() override {
    m_serverRunning.store(false, std::memory_order_release);
    ix::SocketServer::stop();

    // Resume execution with a fake disconnect request when terminating
    // the server thread.
    m_debugger.SendRequest(LuaDebug::CreateDisconnectRequest(false));
  }

  virtual size_t getConnectedClientsCount() override {
    return m_connectedClients;
  }

  virtual void handleConnection(
      std::unique_ptr<ix::Socket> socket,
      std::shared_ptr<ix::ConnectionState> connectionState) override {
    LOG->Info("LuaDebugger: client connected");
    m_connectedClients = 1;

    Serve(*socket);
    connectionState->setTerminated();

    // Send a fake disconnect request in case the connection was closed
    // unexpectedly.
    m_debugger.SendRequest(LuaDebug::CreateDisconnectRequest(true));
    LOG->Info("LuaDebugger: client disconnected");
    m_connectedClients = 0;
  }

  void Serve(ix::Socket& socket) {
    bool initialized = false;
    SeqNo nextMessageSeq = 1;
    size_t bufferLength = 1024;
    size_t bufferFilled = 0;
    std::unique_ptr<char[]> buffer(new char[bufferLength]);

    while (m_serverRunning.load(std::memory_order_relaxed)) {
      bool terminate = false;
      LuaDebug::Message response;
      while (m_debugger.TryReceiveResponse(response)) {
        if (!initialized) {
          if (response["type"] == "response" &&
              response["command"] == "initialize") {
            initialized = true;
          } else {
            continue;
          }
        }

        response["seq"] = nextMessageSeq++;
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

      ASSERT(bufferFilled <= bufferLength);
      if (std::string_view(buffer.get(), bufferFilled).find("\r\n\r\n") ==
          std::string_view::npos) {
        // We don't have a complete header; need to read more data.
        size_t freeSpace = bufferLength - bufferFilled;
        if (freeSpace == 0) {
          LOG->Warn("LuaDebugger: too big request header");
          break;
        }
        ASSERT(freeSpace <= INT_MAX);

        ix::PollResultType pollResult = socket.isReadyToRead(DefaultTimeoutMs);
        if (pollResult == ix::PollResultType::Timeout) {
          continue;
        }
        if (pollResult != ix::PollResultType::ReadyForRead) {
          break;
        }

        ssize_t recvResult =
            socket.recv(buffer.get() + bufferFilled, freeSpace);
        if (recvResult < 0) {
          LOG->Warn("LuaDebugger: recv failed");
        }
        if (recvResult <= 0) {
          break;
        }
        ASSERT(recvResult <= (int)freeSpace);
        bufferFilled += recvResult;

        if (std::string_view(buffer.get(), bufferFilled).find("\r\n\r\n") ==
            std::string_view::npos) {
          continue;
        }
      }

      LuaDebug::Header header;
      if (!header.TryParse(buffer.get(), bufferFilled)) {
        LOG->Warn("LuaDebugger: invalid request header");
        break;
      }
      ASSERT(header.GetHeaderLength() <= bufferFilled);

      size_t totalSize = header.GetHeaderLength() + header.GetContentLength();
      if (totalSize > 10000000) {
        LOG->Warn("LuaDebugger: too big request");
        break;
      }

      if (bufferLength < totalSize) {
        bufferLength = totalSize;
        std::unique_ptr<char[]> newBuffer(new char[bufferLength]);
        memcpy(newBuffer.get(), buffer.get(), bufferFilled);
        buffer = std::move(newBuffer);
      }

      if (totalSize > bufferFilled) {
        size_t bytesMissing = totalSize - bufferFilled;
        ASSERT(bytesMissing <= INT_MAX);

        ix::OnProgressCallback onProgress;
        std::pair<bool, std::string> recvResult = socket.readBytes(
            bytesMissing, onProgress,
            [&buffer, &bufferFilled, &bytesMissing](const std::string& chunk) {
              ASSERT(chunk.length() <= bytesMissing);
              std::memcpy(
                  buffer.get() + bufferFilled, chunk.data(), chunk.length());
              bufferFilled += chunk.length();
              bytesMissing -= chunk.length();
            },
            [this] {
              return !m_serverRunning.load(std::memory_order_relaxed);
            });
        if (!recvResult.first) {
          LOG->Warn("LuaDebugger: recv failed");
          break;
        }
        ASSERT(bytesMissing == 0);
      }

      LuaDebug::Request request;
      if (!request.TryParse(
              buffer.get() + header.GetHeaderLength(),
              bufferFilled - header.GetHeaderLength())) {
        LOG->Warn("LuaDebugger: invalid request body");
        break;
      }

      m_debugger.SendRequest(std::move(request));

      // Move rest of the data to the start of the buffer.
      if (bufferFilled > totalSize) {
        memcpy(
            buffer.get(), buffer.get() + totalSize, bufferFilled - totalSize);
      }
      bufferFilled -= totalSize;
    }

    // Notify the client that it's getting disconnected.
    LuaDebug::Event terminatedEvent = LuaDebug::CreateTerminatedEvent();
    terminatedEvent["seq"] = nextMessageSeq++;
    SendDapMessage(socket, terminatedEvent);
  }

 private:
  std::atomic<bool> m_serverRunning = false;
  size_t m_connectedClients = 0;
  LuaDebug::Debugger& m_debugger;

  Server(Debugger& debugger, const std::string& host, int port)
      : ix::SocketServer(port, host, 1, 1), m_debugger(debugger) {}

  bool SendDapMessage(ix::Socket& socket, const LuaDebug::Message& message) {
    std::string body = Json::FastWriter{}.write(message);
    std::string data = std::string("Content-Length: ") +
                       std::to_string(body.length()) + "\r\n\r\n" + body;
    bool isFirstTry = true;
    bool sendOk = socket.writeBytes(data, [this, &isFirstTry] {
      if (isFirstTry) {
        // If we can send the message without waiting, send it
        // event if the server is terminating. This way, the client
        // can receive the terminated event before the connection
        // is closed.
        isFirstTry = false;
        return false;
      }
      return !m_serverRunning.load(std::memory_order_relaxed);
    });

    if (!sendOk) {
      LOG->Warn("LuaDebugger: send failed");
    }
    return sendOk;
  }
};
}  // namespace LuaDebug

LuaDebugManager::LuaDebugManager()
    : m_debugger(new LuaDebug::Debugger()), m_server(nullptr) {}
LuaDebugManager::~LuaDebugManager() = default;

void LuaDebugManager::Start(std::string address, bool startPaused) {
  ix::initNetSystem();
  m_server = LuaDebug::Server::Start(*m_debugger, address, startPaused);
}

void LuaDebugManager::Stop() {
  if (m_server) {
    m_server->stop();
  }
  ix::uninitNetSystem();
}

void LuaDebugManager::ActivateState(lua_State* lua) {
  m_debugger->ActivateState(lua);
}

void LuaDebugManager::DeactivateState(lua_State* lua) {
  m_debugger->DeactivateState(lua);
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
