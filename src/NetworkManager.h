#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <ixwebsocket/IXHttp.h>
#include <ixwebsocket/IXHttpClient.h>
#include <ixwebsocket/IXSocketTLSOptions.h>
#include <ixwebsocket/IXWebSocket.h>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "EnumHelper.h"
#include "Preference.h"
#include "ixwebsocket/IXWebSocketCloseInfo.h"
#include "ixwebsocket/IXWebSocketErrorInfo.h"
#include "ixwebsocket/IXWebSocketMessage.h"
#include "ixwebsocket/IXWebSocketMessageType.h"
#include "ixwebsocket/IXWebSocketOpenInfo.h"

struct lua_State;

enum HttpErrorCode {
  HttpErrorCode_Blocked,
  HttpErrorCode_UnknownError,
  HttpErrorCode_FileError,

  // from IXWebSocket
  HttpErrorCode_CannotConnect,
  HttpErrorCode_Timeout,
  HttpErrorCode_Gzip,
  HttpErrorCode_UrlMalformed,
  HttpErrorCode_CannotCreateSocket,
  HttpErrorCode_SendError,
  HttpErrorCode_ReadError,
  HttpErrorCode_CannotReadStatusLine,
  HttpErrorCode_MissingStatus,
  HttpErrorCode_HeaderParsingError,
  HttpErrorCode_MissingLocation,
  HttpErrorCode_TooManyRedirects,
  HttpErrorCode_ChunkReadError,
  HttpErrorCode_CannotReadBody,
  HttpErrorCode_Cancelled,

  NUM_HttpErrorCode,
  HttpErrorCode_Invalid,
};
const std::string& HttpErrorCodeToString(HttpErrorCode dc);
HttpErrorCode StringToHttpErrorCode(const std::string& sDC);
LuaDeclareType(HttpErrorCode);

enum WebSocketMessageType {
  // from IXWebSocket
  WebSocketMessageType_Message,
  WebSocketMessageType_Open,
  WebSocketMessageType_Close,
  WebSocketMessageType_Error,

  NUM_WebSocketMessageType,
  WebSocketMessageType_Invalid,
};
const std::string& WebSocketMessageTypeToString(WebSocketMessageType dc);
WebSocketMessageType StringToWebSocketMessageType(const std::string& sDC);
LuaDeclareType(WebSocketMessageType);

struct HttpRequestArgs {
  std::string url_;
  std::string method_ = ix::HttpClient::kGet;
  std::string body_;
  std::string multipart_boundary_;
  std::unordered_map<std::string, std::string> headers_;
  int connect_timeout_ = -1;
  int transfer_timeout_ = -1;
  std::string download_file_;
  std::function<bool(int current, int total)> on_progress_;
  std::function<void(const ix::HttpResponsePtr& response)> on_response_;
  std::function<void(const std::string& errorMessage)> on_file_error_;
};

class HttpRequestFuture {
 public:
  HttpRequestFuture(ix::HttpRequestArgsPtr& args) : args_(args) {};

  static int Collect(lua_State* L);
  static int Cancel(lua_State* L);

 private:
  ix::HttpRequestArgsPtr args_;
};

typedef std::shared_ptr<HttpRequestFuture> HttpRequestFuturePtr;

struct WebSocketArgs {
  std::string url_;
  std::unordered_map<std::string, std::string> headers_;
  int handshake_timeout_ = -1;
  int ping_interval_ = -1;
  bool automatic_reconnect_ = true;
  bool enable_deflate_ = true;
  std::function<void(const ix::WebSocketMessage& response)> on_message_;
  std::function<void()> on_close_;
};

class WebSocketHandle {
 public:
  WebSocketHandle() {};
  ~WebSocketHandle();

  static int Collect(lua_State* L);
  static int Close(lua_State* L);
  static int Send(lua_State* L);

  ix::WebSocket web_socket_;
  std::function<void()> on_close_;
};

typedef std::shared_ptr<WebSocketHandle> WebSocketHandlePtr;

// Runs HTTP requests on up to maxThreads threads at once. Each thread sends
// one request at a time over an ix::HttpClient (and so a connection) of its
// own, taking the next request from a queue the threads share, so requests
// start in the order they were made. Threads are started as the queue needs
// them and run until Stop().
//
// An ordered pool hands its results on in the order the requests were made,
// whichever finishes first, the way a single connection did: Lua written
// against one request at a time can rely on that order without knowing it.
class HttpRequestPool {
 public:
  // Sends a request on the given client and returns what to do with the
  // result; the pool runs that, in order if the pool is ordered.
  typedef std::function<std::function<void()>(ix::HttpClient& client)> Task;

  HttpRequestPool(
      int maxThreads, bool ordered, const ix::SocketTLSOptions& tlsOptions);
  ~HttpRequestPool();

  // Runs task on a pool thread with that thread's client. request is the
  // request task sends; it is cancelled if the pool stops while it runs.
  void Enqueue(const ix::HttpRequestArgsPtr& request, Task task);

  // Drops the queued requests, cancels the running ones and waits for every
  // thread to finish.
  void Stop();

 private:
  struct Job {
    ix::HttpRequestArgsPtr request_;
    Task task_;
    std::uint64_t sequence_;
  };

  void Run(std::size_t slot);
  void Finish(std::uint64_t sequence, std::function<void()> result);

  const int max_threads_;
  const bool ordered_;
  const ix::SocketTLSOptions tls_options_;

  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<Job> queue_;
  std::vector<std::thread> threads_;
  // The request each thread is sending, if any; indexed like threads.
  std::vector<ix::HttpRequestArgsPtr> running_;
  int idle_threads_ = 0;
  bool stopping_ = false;

  // Ordered pools: the next sequence number to hand out and to hand on, and
  // the results that finished ahead of their turn.
  std::uint64_t next_sequence_ = 0;
  std::uint64_t next_result_ = 0;
  std::map<std::uint64_t, std::function<void()>> waiting_results_;
  // Held while results are handed on, so two threads never run them at once.
  std::mutex result_mutex_;
};

class NetworkManager {
 public:
  NetworkManager();
  ~NetworkManager();

  bool IsUrlAllowed(const std::string& url);
  HttpRequestFuturePtr HttpRequest(const HttpRequestArgs& args);
  WebSocketHandlePtr WebSocket(const WebSocketArgs& args);
  void CloseAllWebSockets();
  void Update();
  void EnqueueMainThreadTask(std::function<void()> task);
  std::string UrlEncode(const std::string& value);
  std::string EncodeQueryParameters(
      const std::unordered_map<std::string, std::string>& query);

  // Lua
  void PushSelf(lua_State* L);

 private:
  void RunWebSocketWorker();
  void StopWorkers();

  std::string GetUserAgent();
  void ClearDownloads();

  // Builds requests and encodes URLs; never sends anything itself.
  ix::HttpClient helper_client_;
  ix::SocketTLSOptions tls_options_;

  static Preference<bool> http_enabled_;
  static Preference<std::string> http_allow_hosts_;
  static Preference<int> http_max_concurrent_requests_;
  static Preference<int> http_max_concurrent_downloads_;

  std::atomic<bool> shutdown_workers_{false};

  // Requests that keep their response in memory, and requests that write it
  // to a downloadFile. Downloads get a pool of their own so that a large one
  // never holds up a small API request. In-memory responses reach Lua in the
  // order the requests were made, as they did over one connection; downloads
  // finish in whatever order they come in, so a small file never waits on a
  // large one.
  std::unique_ptr<HttpRequestPool> request_pool_;
  std::unique_ptr<HttpRequestPool> download_pool_;

  std::thread web_socket_worker_;
  std::mutex web_socket_worker_mutex_;
  std::condition_variable web_socket_worker_cv_;
  std::queue<std::function<void()>> web_socket_worker_queue_;

  std::mutex web_socket_handles_mutex_;
  std::vector<std::shared_ptr<WebSocketHandle>> web_socket_handles_;

  std::mutex main_thread_task_mutex_;
  std::queue<std::function<void()>> main_thread_task_queue_;
};

extern NetworkManager* NETWORK;

#endif

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
