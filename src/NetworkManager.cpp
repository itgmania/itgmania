#include "NetworkManager.h"

#include <ixwebsocket/IXHttpClient.h>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXUrlParser.h>
#include <ixwebsocket/IXWebSocket.h>

#include <algorithm>
#include <climits>
#include <cstddef>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "EnumHelper.h"
#include "LuaManager.h"
#include "Preference.h"
#include "ProductInfo.h"
#include "RageFile.h"
#include "RageFileManager.h"
#include "RageLog.h"
#include "RageTimer.h"
#include "RageUtil.h"
#include "SpecialFiles.h"
#include "StdString.h"
#include "ixwebsocket/IXHttp.h"
#include "ixwebsocket/IXWebSocketHttpHeaders.h"
#include "ixwebsocket/IXWebSocketMessage.h"
#include "ixwebsocket/IXWebSocketMessageType.h"
#include "ver.h"

NetworkManager* NETWORK =
    nullptr;  // global and accessible from anywhere in our program

Preference<bool> NetworkManager::http_enabled_(
    "HttpEnabled", true, nullptr, PreferenceType::Immutable);
Preference<std::string> NetworkManager::http_allow_hosts_(
    "HttpAllowHosts", "*.groovestats.com,*.itgmania.com", nullptr,
    PreferenceType::Immutable);
Preference<int> NetworkManager::http_max_concurrent_requests_(
    "HttpMaxConcurrentRequests", 6, nullptr, PreferenceType::Immutable);
Preference<int> NetworkManager::http_max_concurrent_downloads_(
    "HttpMaxConcurrentDownloads", 6, nullptr, PreferenceType::Immutable);

static const char* HttpErrorCodeNames[] = {
    "Blocked",
    "UnknownError",
    "FileError",
    "CannotConnect",
    "Timeout",
    "Gzip",
    "UrlMalformed",
    "CannotCreateSocket",
    "SendError",
    "ReadError",
    "CannotReadStatusLine",
    "MissingStatus",
    "HeaderParsingError",
    "MissingLocation",
    "TooManyRedirects",
    "ChunkReadError",
    "CannotReadBody",
    "Cancelled",
};
XToString(HttpErrorCode);
StringToX(HttpErrorCode);
LuaXType(HttpErrorCode);

static const char* WebSocketMessageTypeNames[] = {
    "Message",
    "Open",
    "Close",
    "Error",
};

static constexpr int kMaxMainThreadTasksPerUpdate = 32;
static constexpr float kMaxMainThreadTaskSecondsPerUpdate = 0.001f;

// The most requests either pool runs at once, whatever the preferences say.
static constexpr int kMaxConcurrencyLimit = 32;

XToString(WebSocketMessageType);
StringToX(WebSocketMessageType);
LuaXType(WebSocketMessageType);

HttpRequestPool::HttpRequestPool(
    int maxThreads, bool ordered, const ix::SocketTLSOptions& tlsOptions)
    : max_threads_(std::max(maxThreads, 1)),
      ordered_(ordered),
      tls_options_(tlsOptions) {}

HttpRequestPool::~HttpRequestPool() { this->Stop(); }

void HttpRequestPool::Enqueue(
    const ix::HttpRequestArgsPtr& request, Task task) {
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    if (this->stopping_) {
      return;
    }

    this->queue_.push_back(
        Job{request, std::move(task), this->next_sequence_++});

    // Start another thread when there are more requests waiting than idle
    // threads to take them.
    if (this->idle_threads_ < static_cast<int>(this->queue_.size()) &&
        static_cast<int>(this->threads_.size()) < this->max_threads_) {
      this->running_.emplace_back();
      this->threads_.emplace_back(
          &HttpRequestPool::Run, this, this->threads_.size());
    }
  }

  this->cv_.notify_one();
}

void HttpRequestPool::Stop() {
  std::deque<Job> dropped;
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    this->stopping_ = true;
    dropped.swap(this->queue_);
    for (const auto& request : this->running_) {
      if (request) {
        request->cancel = true;
      }
    }
  }

  this->cv_.notify_all();

  for (auto& thread : this->threads_) {
    if (thread.joinable()) {
      thread.join();
    }
  }

  // The dropped requests' callbacks (and any download files they hold) are
  // destroyed here, after the threads are gone and outside the lock.
}

void HttpRequestPool::Run(std::size_t slot) {
  // Synchronous client: one request at a time, on this thread.
  ix::HttpClient client;
  client.setTLSOptions(this->tls_options_);

  while (true) {
    Job job;
    {
      std::unique_lock<std::mutex> lock(this->mutex_);
      ++this->idle_threads_;
      this->cv_.wait(
          lock, [this] { return this->stopping_ || !this->queue_.empty(); });
      --this->idle_threads_;

      if (this->stopping_) {
        return;
      }

      job = std::move(this->queue_.front());
      this->queue_.pop_front();
      this->running_[slot] = job.request_;
    }

    std::function<void()> result = job.task_(client);

    {
      std::lock_guard<std::mutex> lock(this->mutex_);
      this->running_[slot].reset();
    }

    this->Finish(job.sequence_, std::move(result));
  }
}

void HttpRequestPool::Finish(
    std::uint64_t sequence, std::function<void()> result) {
  if (!this->ordered_) {
    if (result) {
      result();
    }
    return;
  }

  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    this->waiting_results_[sequence] = std::move(result);
  }

  // Hand on every result whose turn has come. Whichever thread finishes the
  // request that was holding them up does it, under resultMutex so that no
  // two threads ever hand results on at once and the order holds.
  std::lock_guard<std::mutex> handingOn(this->result_mutex_);
  while (true) {
    std::function<void()> next;
    {
      std::lock_guard<std::mutex> lock(this->mutex_);
      auto it = this->waiting_results_.find(this->next_result_);
      if (it == this->waiting_results_.end()) {
        return;
      }
      next = std::move(it->second);
      this->waiting_results_.erase(it);
      ++this->next_result_;
    }
    if (next) {
      next();
    }
  }
}

NetworkManager::NetworkManager() {
  ix::initNetSystem();

  // Register with Lua.
  {
    Lua* L = LUA->Get();
    this->PushSelf(L);
    lua_setglobal(L, "NETWORK");
    LUA->Release(L);
  }

  RageFile f;
  if (f.Open(SpecialFiles::CA_BUNDLE_PATH)) {
    std::string data;
    f.Read(data);
    f.Close();

    this->tls_options_.caFile = data;
  } else {
    LOG->Warn(
        "Reading '%s' failed: %s", SpecialFiles::CA_BUNDLE_PATH.c_str(),
        f.GetError().c_str());
  }

  this->request_pool_ = std::make_unique<HttpRequestPool>(
      std::clamp(http_max_concurrent_requests_.Get(), 1, kMaxConcurrencyLimit),
      /*ordered=*/true, this->tls_options_);
  this->download_pool_ = std::make_unique<HttpRequestPool>(
      std::clamp(http_max_concurrent_downloads_.Get(), 1, kMaxConcurrencyLimit),
      /*ordered=*/false, this->tls_options_);

  this->ClearDownloads();

  this->web_socket_worker_ =
      std::thread(&NetworkManager::RunWebSocketWorker, this);
}

NetworkManager::~NetworkManager() {
  // Unregister with Lua.
  LUA->UnsetGlobal("NETWORK");

  this->StopWorkers();

  this->CloseAllWebSockets();

  // Set the status to uninitialized.
  ix::uninitNetSystem();
}

void NetworkManager::CloseAllWebSockets() {
  // Drop queued websocket starts so no new connections come up after cleanup.
  {
    std::lock_guard<std::mutex> lock(this->web_socket_worker_mutex_);
    this->web_socket_worker_queue_ = {};
  }

  // Stop all active websocket connections without aggressively clearing.
  std::vector<WebSocketHandlePtr> webSocketHandlesCopy;
  {
    std::lock_guard<std::mutex> lock(this->web_socket_handles_mutex_);
    webSocketHandlesCopy = this->web_socket_handles_;
  }

  for (auto& handle : webSocketHandlesCopy) {
    handle->web_socket_.disableAutomaticReconnection();
    handle->web_socket_.stop();
  }
}

void NetworkManager::RunWebSocketWorker() {
  while (true) {
    std::function<void()> task;

    {
      std::unique_lock<std::mutex> lock(this->web_socket_worker_mutex_);
      this->web_socket_worker_cv_.wait(lock, [this] {
        return this->shutdown_workers_.load() ||
               !this->web_socket_worker_queue_.empty();
      });

      if (this->shutdown_workers_.load() &&
          this->web_socket_worker_queue_.empty()) {
        return;
      }

      task = std::move(this->web_socket_worker_queue_.front());
      this->web_socket_worker_queue_.pop();
    }

    task();
  }
}

void NetworkManager::StopWorkers() {
  this->shutdown_workers_ = true;

  // Cancels the requests still running, so a download in progress doesn't
  // hold up quitting.
  if (this->request_pool_) {
    this->request_pool_->Stop();
  }
  if (this->download_pool_) {
    this->download_pool_->Stop();
  }

  this->web_socket_worker_cv_.notify_one();

  if (this->web_socket_worker_.joinable()) {
    this->web_socket_worker_.join();
  }
}

void NetworkManager::EnqueueMainThreadTask(std::function<void()> task) {
  if (this->shutdown_workers_.load()) {
    return;
  }

  std::lock_guard<std::mutex> lock(this->main_thread_task_mutex_);
  this->main_thread_task_queue_.push(std::move(task));
}

void NetworkManager::Update() {
  RageTimer timer;
  int tasksProcessed = 0;

  while (tasksProcessed < kMaxMainThreadTasksPerUpdate) {
    std::function<void()> task;
    {
      std::lock_guard<std::mutex> lock(this->main_thread_task_mutex_);
      if (this->main_thread_task_queue_.empty()) {
        break;
      }

      task = std::move(this->main_thread_task_queue_.front());
      this->main_thread_task_queue_.pop();
    }

    task();
    ++tasksProcessed;

    if (timer.Ago() >= kMaxMainThreadTaskSecondsPerUpdate) {
      break;
    }
  }
}

bool NetworkManager::IsUrlAllowed(const std::string& url) {
  if (!this->http_enabled_.Get()) {
    return false;
  }

  std::string protocol;
  std::string host;
  std::string path;
  std::string query;
  int port;

  bool valid = ix::UrlParser::parse(url, protocol, host, path, query, port);

  if (!valid) {
    return false;
  }

  if (protocol != "http" && protocol != "https" && protocol != "ws" &&
      protocol != "wss") {
    return false;
  }

  MakeLower(host);

  std::string allowedHostsStr = this->http_allow_hosts_.Get();
  MakeLower(allowedHostsStr);

  std::vector<std::string> allowedHosts;
  split(allowedHostsStr, ",", allowedHosts);

  for (const auto& allowedHost : allowedHosts) {
    // subdomain wildcards; ".domain" doesn't match "*.domain", but "a.domain"
    // does
    if (allowedHost.substr(0, 2) == "*." &&
        host.length() >= allowedHost.length()) {
      size_t pos = host.length() - allowedHost.length() + 1;
      if (host.substr(pos) == allowedHost.substr(1)) {
        return true;
      }
    }

    // literal match
    if (host == allowedHost) {
      return true;
    }
  }

  return false;
}

HttpRequestFuturePtr NetworkManager::HttpRequest(const HttpRequestArgs& args) {
  auto downloadFile = std::make_shared<RageFile>();
  std::string downloadFilename;

  ix::HttpRequestArgsPtr req =
      this->helper_client_.createRequest(args.url_, args.method_);
  req->body = args.body_;
  req->multipartBoundary = args.multipart_boundary_;

  req->extraHeaders["User-Agent"] = this->GetUserAgent();
  for (const auto& entry : args.headers_) {
    req->extraHeaders[entry.first] = entry.second;
  }

  if (!args.download_file_.empty()) {
    downloadFilename = std::string("/Downloads/") + args.download_file_;
    if (!downloadFile->Open(
            downloadFilename, RageFile::WRITE | RageFile::STREAMED)) {
      req->cancel = true;
    }

    // A weak reference: the request owns this callback, and a strong one would
    // keep the request and its file object alive forever.
    std::weak_ptr<ix::HttpRequestArgs> weakReq = req;
    req->onChunkCallback = [downloadFile, weakReq](const std::string& data) {
      if (downloadFile->Write(data.c_str(), data.size()) < 0) {
        if (auto request = weakReq.lock()) {
          request->cancel = true;
        }
      }
    };

    // Don't timeout downloads by default. This value might be
    // overwritten below if a value is specified in the request.
    req->transferTimeout = INT_MAX;
  }

  if (args.connect_timeout_ > 0) {
    req->connectTimeout = args.connect_timeout_;
  }

  if (args.transfer_timeout_ > 0) {
    req->transferTimeout = args.transfer_timeout_;
  }

  if (args.on_progress_) {
    req->onProgressCallback = args.on_progress_;
  }

  HttpRequestPool& pool = args.download_file_.empty() ? *this->request_pool_
                                                      : *this->download_pool_;

  pool.Enqueue(
      req,
      [this, req, args, downloadFile,
       downloadFilename](ix::HttpClient& client) -> std::function<void()> {
        if (this->shutdown_workers_.load()) {
          return nullptr;
        }

        ix::HttpResponsePtr response =
            client.request(req->url, req->verb, req->body, req);

        if (!args.download_file_.empty()) {
          std::string error = downloadFile->GetError();
          downloadFile->Close();

          if (!error.empty()) {
            std::string errorMessage =
                "could not write to " + downloadFile->GetPath() + ": " + error;
            FILEMAN->Remove(downloadFilename);
            return [args, errorMessage]() {
              if (args.on_file_error_) {
                args.on_file_error_(errorMessage);
              }
            };
          }
        }

        return [args, response]() {
          if (args.on_response_) {
            args.on_response_(response);
          }
        };
      });

  return std::make_shared<HttpRequestFuture>(req);
}

WebSocketHandlePtr NetworkManager::WebSocket(const WebSocketArgs& args) {
  auto handle = std::make_shared<WebSocketHandle>();
  handle->on_close_ = args.on_close_;

  handle->web_socket_.setUrl(args.url_);
  handle->web_socket_.setTLSOptions(this->tls_options_);

  if (args.enable_deflate_) {
    handle->web_socket_.enablePerMessageDeflate();
  } else {
    handle->web_socket_.disablePerMessageDeflate();
  }

  handle->web_socket_.setOnMessageCallback(
      [onMessage = args.on_message_](const ix::WebSocketMessagePtr& msg) {
        if (onMessage) {
          onMessage(*msg);
        }
      });

  ix::WebSocketHttpHeaders headers;
  headers["User-Agent"] = this->GetUserAgent();
  for (const auto& entry : args.headers_) {
    headers[entry.first] = entry.second;
  }
  handle->web_socket_.setExtraHeaders(headers);

  if (args.handshake_timeout_ > -1) {
    handle->web_socket_.setHandshakeTimeout(args.handshake_timeout_);
  }

  if (args.ping_interval_ > -1) {
    handle->web_socket_.setPingInterval(args.ping_interval_);
  }

  if (args.automatic_reconnect_) {
    handle->web_socket_.enableAutomaticReconnection();
  } else {
    handle->web_socket_.disableAutomaticReconnection();
  }

  {
    std::lock_guard<std::mutex> lock(this->web_socket_worker_mutex_);
    this->web_socket_worker_queue_.push([this, handle]() {
      if (this->shutdown_workers_.load()) {
        return;
      }

      handle->web_socket_.start();
    });
  }

  this->web_socket_worker_cv_.notify_one();

  {
    std::lock_guard<std::mutex> lock(this->web_socket_handles_mutex_);
    web_socket_handles_.push_back(handle);
  }

  return handle;
}

std::string NetworkManager::UrlEncode(const std::string& value) {
  return this->helper_client_.urlEncode(value);
}

std::string NetworkManager::EncodeQueryParameters(
    const std::unordered_map<std::string, std::string>& query) {
  return this->helper_client_.serializeHttpParameters(query);
}

std::string NetworkManager::GetUserAgent() {
  std::stringstream ss;
  ss << PRODUCT_FAMILY << "/" << product_version;
  return ss.str();
}

void NetworkManager::ClearDownloads() {
  std::vector<std::string> files;
  FILEMAN->GetDirListing("/Downloads/*", files, false, true);

  for (const auto& file : files) {
    if (FILEMAN->IsADirectory(file)) {
      FILEMAN->DeleteRecursive(file + "/");
    } else {
      FILEMAN->Remove(file);
    }
  }
}

int HttpRequestFuture::Collect(lua_State* L) {
  void* udata = luaL_checkudata(L, 1, "HttpRequestFuture");
  auto futptr = static_cast<HttpRequestFuturePtr*>(udata);
  futptr->~shared_ptr();
  return 0;
}

int HttpRequestFuture::Cancel(lua_State* L) {
  void* udata = luaL_checkudata(L, 1, "HttpRequestFuture");
  auto fut = *static_cast<HttpRequestFuturePtr*>(udata);
  fut->args_->cancel = true;
  return 0;
}

WebSocketHandle::~WebSocketHandle() { web_socket_.stop(); }

int WebSocketHandle::Collect(lua_State* L) {
  void* udata = luaL_checkudata(L, 1, "WebSocketHandle");
  auto handleptr = static_cast<WebSocketHandlePtr*>(udata);
  handleptr->~shared_ptr();
  return 0;
}

int WebSocketHandle::Close(lua_State* L) {
  void* udata = luaL_checkudata(L, 1, "WebSocketHandle");
  auto handle = *static_cast<WebSocketHandlePtr*>(udata);

  LUA->YieldLua();
  handle->web_socket_.stop();
  LUA->UnyieldLua();

  if (handle->on_close_) {
    handle->on_close_();
  }

  return 1;
}

int WebSocketHandle::Send(lua_State* L) {
  void* udata = luaL_checkudata(L, 1, "WebSocketHandle");
  auto handle = *static_cast<WebSocketHandlePtr*>(udata);

  size_t len;
  const char* s = luaL_checklstring(L, 2, &len);
  std::string data(s, len);

  bool binary = lua_toboolean(L, 3);

  auto info = handle->web_socket_.send(data, binary);

  lua_pushboolean(L, info.success);
  return 1;
}

// lua start
#include "LuaBinding.h"

static void registerHttpRequestMetatable(lua_State* L) {
  const luaL_Reg HttpRequest_meta[] = {
      {"__gc", HttpRequestFuture::Collect},
      {"Cancel", HttpRequestFuture::Cancel},
      {NULL, NULL},
  };

  luaL_newmetatable(L, "HttpRequestFuture");
  luaL_register(L, NULL, HttpRequest_meta);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  lua_pop(L, 1);
}

REGISTER_WITH_LUA_FUNCTION(registerHttpRequestMetatable)

static void registerWebSocketMetatable(lua_State* L) {
  const luaL_Reg WebSocket_meta[] = {
      {"__gc", WebSocketHandle::Collect},
      {"Close", WebSocketHandle::Close},
      {"Send", WebSocketHandle::Send},
      {NULL, NULL},
  };

  luaL_newmetatable(L, "WebSocketHandle");
  luaL_register(L, NULL, WebSocket_meta);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  lua_pop(L, 1);
}

REGISTER_WITH_LUA_FUNCTION(registerWebSocketMetatable)

/** @brief Allow Lua to have access to the NetworkManager. */
class LunaNetworkManager : public Luna<NetworkManager> {
 public:
  static int IsUrlAllowed(T* p, lua_State* L) {
    std::string url = SArg(1);

    lua_pushboolean(L, p->IsUrlAllowed(url));
    return 1;
  }

  static int HttpRequest(T* p, lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);

    HttpRequestArgs args;
    int onProgressRef = LUA_NOREF;
    int onResponseRef = LUA_NOREF;

    lua_getfield(L, 1, "url");
    if (lua_isnil(L, -1)) {
      luaL_error(L, "url is required");
    } else if (lua_isstring(L, -1)) {
      args.url_ = lua_tostring(L, -1);
    } else {
      luaL_error(L, "url must be a string");
    }
    lua_pop(L, 1);

    lua_getfield(L, 1, "method");
    if (!lua_isnil(L, -1)) {
      if (lua_isstring(L, -1)) {
        std::string method = lua_tostring(L, -1);

        std::vector<std::string> supported_methods{"GET",   "POST",   "PUT",
                                                   "PATCH", "DELETE", "HEAD"};
        if (std::find(
                supported_methods.begin(), supported_methods.end(), method) ==
            supported_methods.end()) {
          luaL_error(L, "unknown method: '%s'", method.c_str());
        }

        args.method_ = method;
      } else {
        luaL_error(L, "method must be a string");
      }
    }
    lua_pop(L, 1);

    lua_getfield(L, 1, "body");
    if (!lua_isnil(L, -1)) {
      if (lua_isstring(L, -1)) {
        size_t len;
        const char* s = lua_tolstring(L, -1, &len);
        args.body_ = std::string(s, len);
      } else {
        luaL_error(L, "body must be a string");
      }
    }
    lua_pop(L, 1);

    lua_getfield(L, 1, "multipartBoundary");
    if (!lua_isnil(L, -1)) {
      if (lua_isstring(L, -1)) {
        size_t len;
        const char* s = lua_tolstring(L, -1, &len);
        args.multipart_boundary_ = std::string(s, len);
      } else {
        luaL_error(L, "multipartBoundary must be a string");
      }
    }
    lua_pop(L, 1);

    lua_getfield(L, 1, "headers");
    if (!lua_isnil(L, -1)) {
      if (lua_istable(L, -1)) {
        lua_pushnil(L);
        while (lua_next(L, -2) != 0) {
          if (!lua_isstring(L, -2)) {
            luaL_error(L, "header keys must be strings");
          }

          if (!lua_isstring(L, -1)) {
            luaL_error(L, "header values must be strings");
          }

          std::string key = lua_tostring(L, -2);
          std::string value = lua_tostring(L, -1);

          args.headers_[key] = value;

          lua_pop(L, 1);
        }
      } else {
        luaL_error(L, "headers must be a table");
      }
    }
    lua_pop(L, 1);

    lua_getfield(L, 1, "connectTimeout");
    if (!lua_isnil(L, -1)) {
      if (lua_isnumber(L, -1)) {
        args.connect_timeout_ = lua_tointeger(L, -1);
      } else {
        luaL_error(L, "connectTimeout must be an integer");
      }
    }
    lua_pop(L, 1);

    lua_getfield(L, 1, "transferTimeout");
    if (!lua_isnil(L, -1)) {
      if (lua_isnumber(L, -1)) {
        args.transfer_timeout_ = lua_tointeger(L, -1);
      } else {
        luaL_error(L, "transferTimeout must be an integer");
      }
    }
    lua_pop(L, 1);

    lua_getfield(L, 1, "downloadFile");
    if (!lua_isnil(L, -1)) {
      if (lua_isstring(L, -1)) {
        args.download_file_ = lua_tostring(L, -1);
      } else {
        luaL_error(L, "downloadFile must be a string");
      }
    }
    lua_pop(L, 1);

    lua_getfield(L, 1, "onProgress");
    if (!lua_isnil(L, -1)) {
      if (lua_isfunction(L, -1)) {
        lua_pushvalue(L, -1);
        onProgressRef = luaL_ref(L, LUA_REGISTRYINDEX);

        args.on_progress_ = [p, onProgressRef](int current, int total) {
          p->EnqueueMainThreadTask([current, total, onProgressRef]() {
            Lua* L = LUA->Get();
            handleProgress(L, current, total, onProgressRef);
            LUA->Release(L);
          });

          return true;
        };
      } else {
        luaL_error(L, "onProgress must be a function");
      }
    }
    lua_pop(L, 1);

    lua_getfield(L, 1, "onResponse");
    if (!lua_isnil(L, -1)) {
      if (lua_isfunction(L, -1)) {
        lua_pushvalue(L, -1);
        onResponseRef = luaL_ref(L, LUA_REGISTRYINDEX);
      } else {
        luaL_unref(L, LUA_REGISTRYINDEX, onProgressRef);
        luaL_error(L, "onResponse must be a function");
      }
    }
    lua_pop(L, 1);

    args.on_file_error_ = [p, onProgressRef,
                           onResponseRef](const std::string& errorMessage) {
      p->EnqueueMainThreadTask([errorMessage, onProgressRef, onResponseRef]() {
        Lua* L = LUA->Get();

        luaL_unref(L, LUA_REGISTRYINDEX, onProgressRef);

        if (onResponseRef != LUA_NOREF) {
          handleFileError(L, errorMessage, onResponseRef);
        }

        LUA->Release(L);
      });
    };

    args.on_response_ = [p, onProgressRef,
                         onResponseRef](const ix::HttpResponsePtr& response) {
      p->EnqueueMainThreadTask([response, onProgressRef, onResponseRef]() {
        Lua* L = LUA->Get();

        luaL_unref(L, LUA_REGISTRYINDEX, onProgressRef);

        if (onResponseRef != LUA_NOREF) {
          handleHttpResponse(L, response, onResponseRef);
        }

        LUA->Release(L);
      });
    };

    if (p->IsUrlAllowed(args.url_)) {
      auto fut = p->HttpRequest(args);

      void* vp = lua_newuserdata(L, sizeof(std::shared_ptr<HttpRequestFuture>));
      new (vp) std::shared_ptr<::HttpRequestFuture>(fut);
      luaL_getmetatable(L, "HttpRequestFuture");
      lua_setmetatable(L, -2);
      return 1;
    } else {
      LOG->Warn("blocked access to %s", args.url_.c_str());
      luaL_unref(L, LUA_REGISTRYINDEX, onProgressRef);
      if (onResponseRef != LUA_NOREF) {
        handleUrlForbidden(L, args.url_, onResponseRef);
      }
      return 0;
    }
  }

  static int WebSocket(T* p, lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);

    WebSocketArgs args;
    int onMessageRef = LUA_NOREF;

    lua_getfield(L, 1, "url");
    if (lua_isnil(L, -1)) {
      luaL_error(L, "url is required");
    } else if (lua_isstring(L, -1)) {
      args.url_ = lua_tostring(L, -1);
    } else {
      luaL_error(L, "url must be a string");
    }
    lua_pop(L, 1);

    lua_getfield(L, 1, "headers");
    if (!lua_isnil(L, -1)) {
      if (lua_istable(L, -1)) {
        lua_pushnil(L);
        while (lua_next(L, -2) != 0) {
          if (!lua_isstring(L, -2)) {
            luaL_error(L, "header keys must be strings");
          }

          if (!lua_isstring(L, -1)) {
            luaL_error(L, "header values must be strings");
          }

          std::string key = lua_tostring(L, -2);
          std::string value = lua_tostring(L, -1);

          args.headers_[key] = value;

          lua_pop(L, 1);
        }
      } else {
        luaL_error(L, "headers must be a table");
      }
    }
    lua_pop(L, 1);

    lua_getfield(L, 1, "handshakeTimeout");
    if (!lua_isnil(L, -1)) {
      if (lua_isnumber(L, -1)) {
        args.handshake_timeout_ = lua_tointeger(L, -1);
      } else {
        luaL_error(L, "handshakeTimeout must be an integer");
      }
    }
    lua_pop(L, 1);

    lua_getfield(L, 1, "pingInterval");
    if (!lua_isnil(L, -1)) {
      if (lua_isnumber(L, -1)) {
        args.ping_interval_ = lua_tointeger(L, -1);
      } else {
        luaL_error(L, "pingInterval must be an integer");
      }
    }
    lua_pop(L, 1);

    lua_getfield(L, 1, "automaticReconnect");
    if (!lua_isnil(L, -1)) {
      if (lua_isboolean(L, -1)) {
        args.automatic_reconnect_ = lua_toboolean(L, -1);
      } else {
        luaL_error(L, "automaticReconnect must be a boolean");
      }
    }
    lua_pop(L, 1);

    args.enable_deflate_ = true;
    lua_getfield(L, 1, "enableDeflate");
    if (!lua_isnil(L, -1)) {
      if (lua_isboolean(L, -1)) {
        args.enable_deflate_ = lua_toboolean(L, -1);
      } else {
        luaL_error(L, "enableDeflate must be a boolean");
      }
    }
    lua_pop(L, 1);

    lua_getfield(L, 1, "onMessage");
    if (!lua_isnil(L, -1)) {
      if (lua_isfunction(L, -1)) {
        lua_pushvalue(L, -1);
        onMessageRef = luaL_ref(L, LUA_REGISTRYINDEX);
      } else {
        luaL_error(L, "onMessage must be a function");
      }
    }
    lua_pop(L, 1);

    args.on_message_ = [p, onMessageRef](const ix::WebSocketMessage& msg) {
      ix::WebSocketMessageType type = msg.type;
      std::string data = msg.str;
      ix::WebSocketErrorInfo errorInfo = msg.errorInfo;
      ix::WebSocketOpenInfo openInfo = msg.openInfo;
      ix::WebSocketCloseInfo closeInfo = msg.closeInfo;
      bool binary = msg.binary;

      p->EnqueueMainThreadTask(
          [type, data, errorInfo, openInfo, closeInfo, binary, onMessageRef]() {
            Lua* L = LUA->Get();

            if (onMessageRef != LUA_NOREF) {
              handleMessage(
                  L, type, data, errorInfo, openInfo, closeInfo, binary,
                  onMessageRef);
            }

            LUA->Release(L);
          });
    };

    args.on_close_ = [p, onMessageRef]() {
      p->EnqueueMainThreadTask([onMessageRef]() {
        Lua* L = LUA->Get();

        if (onMessageRef != LUA_NOREF) {
          luaL_unref(L, LUA_REGISTRYINDEX, onMessageRef);
        }

        LUA->Release(L);
      });
    };

    if (p->IsUrlAllowed(args.url_)) {
      auto handle = p->WebSocket(args);

      void* vp = lua_newuserdata(L, sizeof(std::shared_ptr<WebSocketHandle>));
      new (vp) std::shared_ptr<::WebSocketHandle>(handle);
      luaL_getmetatable(L, "WebSocketHandle");
      lua_setmetatable(L, -2);
      return 1;
    } else {
      LOG->Warn("blocked access to %s", args.url_.c_str());
      if (onMessageRef != LUA_NOREF) {
        handleWebSocketUrlForbidden(L, args.url_, onMessageRef);
      }
      return 0;
    }
  }

  static int UrlEncode(T* p, lua_State* L) {
    std::string url = SArg(1);

    std::string encoded = p->UrlEncode(url);

    lua_pushstring(L, encoded.c_str());
    return 1;
  }

  static int EncodeQueryParameters(T* p, lua_State* L) {
    std::unordered_map<std::string, std::string> query;

    if (lua_istable(L, 1)) {
      lua_pushnil(L);
      while (lua_next(L, -2) != 0) {
        if (!lua_isstring(L, -2)) {
          luaL_error(L, "parameter keys must be strings");
        }

        if (!lua_isstring(L, -1)) {
          luaL_error(L, "parameter values must be strings");
        }

        std::string key = lua_tostring(L, -2);
        std::string value = lua_tostring(L, -1);

        query[key] = value;

        lua_pop(L, 1);
      }
    } else {
      luaL_error(L, "query must be a table");
    }

    std::string encoded = p->EncodeQueryParameters(query);

    lua_pushstring(L, encoded.c_str());
    return 1;
  }

  LunaNetworkManager() {
    ADD_METHOD(IsUrlAllowed);
    ADD_METHOD(HttpRequest);
    ADD_METHOD(WebSocket);
    ADD_METHOD(UrlEncode);
    ADD_METHOD(EncodeQueryParameters);
  }

 private:
  static void handleUrlForbidden(Lua* L, std::string& url, int onResponseRef) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, onResponseRef);
    luaL_unref(L, LUA_REGISTRYINDEX, onResponseRef);

    lua_newtable(L);

    LuaHelpers::Push(L, HttpErrorCode_Blocked);
    lua_setfield(L, -2, "error");

    lua_pushfstring(L, "access to %s is not allowed", url.c_str());
    lua_setfield(L, -2, "errorMessage");

    std::string error = "Lua error in HTTP response handler: ";
    LuaHelpers::RunScriptOnStack(L, error, 1, 0, true);
  }

  static void handleFileError(
      Lua* L, const std::string& errorMessage, int onResponseRef) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, onResponseRef);
    luaL_unref(L, LUA_REGISTRYINDEX, onResponseRef);

    lua_newtable(L);

    LuaHelpers::Push(L, HttpErrorCode_FileError);
    lua_setfield(L, -2, "error");

    lua_pushstring(L, errorMessage.c_str());
    lua_setfield(L, -2, "errorMessage");

    std::string error = "Lua error in HTTP response handler: ";
    LuaHelpers::RunScriptOnStack(L, error, 1, 0, true);
  }

  static void handleHttpResponse(
      Lua* L, const ix::HttpResponsePtr& response, int onResponseRef) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, onResponseRef);
    luaL_unref(L, LUA_REGISTRYINDEX, onResponseRef);

    lua_newtable(L);

    switch (response->errorCode) {
      case ix::HttpErrorCode::Ok:
        lua_pushnil(L);
        break;
      case ix::HttpErrorCode::CannotConnect:
        LuaHelpers::Push(L, HttpErrorCode_CannotConnect);
        break;
      case ix::HttpErrorCode::Timeout:
        LuaHelpers::Push(L, HttpErrorCode_Timeout);
        break;
      case ix::HttpErrorCode::Gzip:
        LuaHelpers::Push(L, HttpErrorCode_Gzip);
        break;
      case ix::HttpErrorCode::UrlMalformed:
        LuaHelpers::Push(L, HttpErrorCode_UrlMalformed);
        break;
      case ix::HttpErrorCode::CannotCreateSocket:
        LuaHelpers::Push(L, HttpErrorCode_CannotCreateSocket);
        break;
      case ix::HttpErrorCode::SendError:
        LuaHelpers::Push(L, HttpErrorCode_SendError);
        break;
      case ix::HttpErrorCode::ReadError:
        LuaHelpers::Push(L, HttpErrorCode_ReadError);
        break;
      case ix::HttpErrorCode::CannotReadStatusLine:
        LuaHelpers::Push(L, HttpErrorCode_CannotReadStatusLine);
        break;
      case ix::HttpErrorCode::MissingStatus:
        LuaHelpers::Push(L, HttpErrorCode_MissingStatus);
        break;
      case ix::HttpErrorCode::HeaderParsingError:
        LuaHelpers::Push(L, HttpErrorCode_HeaderParsingError);
        break;
      case ix::HttpErrorCode::MissingLocation:
        LuaHelpers::Push(L, HttpErrorCode_MissingLocation);
        break;
      case ix::HttpErrorCode::TooManyRedirects:
        LuaHelpers::Push(L, HttpErrorCode_TooManyRedirects);
        break;
      case ix::HttpErrorCode::ChunkReadError:
        LuaHelpers::Push(L, HttpErrorCode_ChunkReadError);
        break;
      case ix::HttpErrorCode::CannotReadBody:
        LuaHelpers::Push(L, HttpErrorCode_CannotReadBody);
        break;
      case ix::HttpErrorCode::Cancelled:
        LuaHelpers::Push(L, HttpErrorCode_Cancelled);
        break;
      default:
        LuaHelpers::Push(L, HttpErrorCode_UnknownError);
        break;
    }
    lua_setfield(L, -2, "error");

    if (response->errorMsg == "") {
      lua_pushnil(L);
    } else {
      lua_pushstring(L, response->errorMsg.c_str());
    }
    lua_setfield(L, -2, "errorMessage");

    lua_pushinteger(L, response->statusCode);
    lua_setfield(L, -2, "statusCode");

    lua_newtable(L);
    for (const auto& entry : response->headers) {
      lua_pushstring(L, entry.second.c_str());
      lua_setfield(L, -2, entry.first.c_str());
    }
    lua_setfield(L, -2, "headers");

    lua_pushlstring(L, response->body.c_str(), response->body.length());
    lua_setfield(L, -2, "body");

    lua_pushnumber(L, response->uploadSize);
    lua_setfield(L, -2, "uploadSize");

    lua_pushnumber(L, response->downloadSize);
    lua_setfield(L, -2, "downloadSize");

    std::string error = "Lua error in HTTP response handler: ";
    LuaHelpers::RunScriptOnStack(L, error, 1, 0, true);
  }

  static void handleProgress(
      Lua* L, int current, int total, int onProgressRef) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, onProgressRef);
    lua_pushinteger(L, current);
    lua_pushinteger(L, total);

    std::string error = "Lua error in HTTP progress handler: ";
    LuaHelpers::RunScriptOnStack(L, error, 2, 0, true);
  }

  static void handleWebSocketUrlForbidden(
      Lua* L, std::string& url, int onMessageRef) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, onMessageRef);
    luaL_unref(L, LUA_REGISTRYINDEX, onMessageRef);

    lua_newtable(L);

    LuaHelpers::Push(L, WebSocketMessageType_Error);
    lua_setfield(L, -2, "type");

    lua_pushfstring(L, "access to %s is not allowed", url.c_str());
    lua_setfield(L, -2, "reason");

    std::string error = "Lua error in WebSocket message handler: ";
    LuaHelpers::RunScriptOnStack(L, error, 1, 0, true);
  }

  static void handleMessage(
      Lua* L, ix::WebSocketMessageType type, const std::string& data,
      const ix::WebSocketErrorInfo& errorInfo,
      const ix::WebSocketOpenInfo& openInfo,
      const ix::WebSocketCloseInfo& closeInfo, bool binary, int onMessageRef) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, onMessageRef);

    lua_newtable(L);

    switch (type) {
      case ix::WebSocketMessageType::Message:
        LuaHelpers::Push(L, WebSocketMessageType_Message);
        lua_setfield(L, -2, "type");

        lua_pushlstring(L, data.c_str(), data.length());
        lua_setfield(L, -2, "data");

        lua_pushboolean(L, binary);
        lua_setfield(L, -2, "binary");
        break;
      case ix::WebSocketMessageType::Open:
        LuaHelpers::Push(L, WebSocketMessageType_Open);
        lua_setfield(L, -2, "type");

        lua_pushstring(L, openInfo.uri.c_str());
        lua_setfield(L, -2, "uri");

        lua_newtable(L);
        for (const auto& entry : openInfo.headers) {
          lua_pushstring(L, entry.second.c_str());
          lua_setfield(L, -2, entry.first.c_str());
        }
        lua_setfield(L, -2, "headers");

        lua_pushstring(L, openInfo.protocol.c_str());
        lua_setfield(L, -2, "protocol");
        break;
      case ix::WebSocketMessageType::Close:
        LuaHelpers::Push(L, WebSocketMessageType_Close);
        lua_setfield(L, -2, "type");

        lua_pushstring(L, closeInfo.reason.c_str());
        lua_setfield(L, -2, "reason");

        lua_pushboolean(L, closeInfo.remote);
        lua_setfield(L, -2, "remote");
        break;
      case ix::WebSocketMessageType::Error:
        LuaHelpers::Push(L, WebSocketMessageType_Error);
        lua_setfield(L, -2, "type");

        lua_pushinteger(L, errorInfo.retries);
        lua_setfield(L, -2, "retries");

        lua_pushnumber(L, errorInfo.wait_time);
        lua_setfield(L, -2, "waitTime");

        if (errorInfo.http_status > 0) {
          lua_pushinteger(L, errorInfo.http_status);
        } else {
          lua_pushnil(L);
        }
        lua_setfield(L, -2, "httpStatusCode");

        lua_pushstring(L, errorInfo.reason.c_str());
        lua_setfield(L, -2, "reason");

        lua_pushboolean(L, errorInfo.decompressionError);
        lua_setfield(L, -2, "decompressionError");
        break;
      case ix::WebSocketMessageType::Ping:
      case ix::WebSocketMessageType::Pong:
      case ix::WebSocketMessageType::Fragment:
      default:
        lua_pop(L, 2);
        return;
    }

    std::string error = "Lua error in WebSocket message handler: ";
    LuaHelpers::RunScriptOnStack(L, error, 1, 0, true);
  }
};

LUA_REGISTER_CLASS(NetworkManager)
// lua end

/*
 * (c) 2021 Martin Natano
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, provided that the above
 * copyright notice(s) and this permission notice appear in all copies of
 * the Software and that both the above copyright notice(s) and this
 * permission notice appear in supporting documentation.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
 * INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
