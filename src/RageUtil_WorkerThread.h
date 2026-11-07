/* RageWorkerThread - a worker thread for operations that are allowed to time
 * out. */

#ifndef RAGE_UTIL_WORKER_THREAD_H
#define RAGE_UTIL_WORKER_THREAD_H

#include <string>

#include "RageThreads.h"
#include "RageTimer.h"

class RageWorkerThread {
 public:
  RageWorkerThread(const std::string& name);
  virtual ~RageWorkerThread();

  /* Call SetTimeout(10) to start a timeout period of 10 seconds.  This is not a
   * per-request timeout; you have 10 seconds to do your work, at which point
   * all requests time out until SetTimeout is called again. */
  void SetTimeout(float seconds);
  bool TimeoutEnabled() const { return !timeout_.IsZero(); }

  /* Return true if the last operation has timed out and has not yet recovered.
   */
  bool IsTimedOut() const { return timed_out_; }

  /* Pause until the next heartbeat completes.  Returns false if timed out. This
   * triggers no actions, so no cleanup is run and IsTimedOut() is not affected.
   */
  bool WaitForOneHeartbeat();

 protected:
  /* Call this in the derived class to start and stop the thread. */
  void StartThread();
  void StopThread();

  /* Run the given request.  Return true if the operation completed, false on
   * timeout. Always call IsTimedOut() first; if true is returned, the thread is
   * currently timed out and DoRequest() must not be called. */
  bool DoRequest(int request);

  /* Overload this in the derived class to handle requests. */
  virtual void HandleRequest(int request) = 0;

  /* If DoRequest times out, this will be called in the thread after completion.
   * Clean up.  No new requests will be allowed until this completes. */
  virtual void RequestTimedOut() {}

  /* Enable a heartbeat.  DoHeartbeat will be called every fSeconds while idle.
   * DoHeartbeat may safely time out; if DoRequest tries to start a request in
   * the main thread, it'll simply time out. */
  void SetHeartbeat(float seconds) {
    heartbeat_ = seconds;
    next_heartbeat_.Touch();
  }
  virtual void DoHeartbeat() {}

 private:
  static int StartWorkerMain(void* p_this) {
    ((RageWorkerThread*)(p_this))->WorkerMain();
    return 0;
  }
  void WorkerMain();

  enum { REQ_SHUTDOWN = -1, REQ_NONE = -2 };
  RageThread worker_thread_;
  RageEvent worker_event_;
  std::string name_;
  int request_;
  bool request_finished_;
  bool timed_out_;
  RageTimer timeout_;

  float heartbeat_;
  RageTimer next_heartbeat_;
  RageEvent heartbeat_event_;
};

#endif

/*
 * (c) 2005 Glenn Maynard
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
