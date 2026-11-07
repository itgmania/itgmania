#include "RageUtil_WorkerThread.h"

#include <string>

#include "RageLog.h"
#include "RageThreads.h"
#include "RageUtil.h"
#include "global.h"

RageWorkerThread::RageWorkerThread(const std::string& name)
    : worker_event_("\"" + name + "\" worker event"),
      heartbeat_event_("\"" + name + "\" heartbeat event") {
  name_ = name;
  timeout_.SetZero();
  request_ = REQ_NONE;
  timed_out_ = false;
  heartbeat_ = -1;
  request_finished_ = false;

  worker_thread_.SetName("Worker thread (" + name + ")");
}

RageWorkerThread::~RageWorkerThread() {
  /* The worker thread must be stopped by the derived class. */
  ASSERT(!worker_thread_.IsCreated());
}

void RageWorkerThread::SetTimeout(float seconds) {
  worker_event_.Lock();
  if (seconds < 0) {
    timeout_.SetZero();
  } else {
    timeout_.Touch();
    timeout_ += seconds;
  }
  worker_event_.Unlock();
}

void RageWorkerThread::StartThread() {
  ASSERT(!worker_thread_.IsCreated());

  worker_thread_.Create(StartWorkerMain, this);
}

void RageWorkerThread::StopThread() {
  /* If we're timed out, wait. */
  worker_event_.Lock();
  if (timed_out_) {
    LOG->Trace(
        "Waiting for timed-out worker thread \"%s\" to complete ...",
        name_.c_str());
    while (timed_out_) {
      worker_event_.Wait();
    }
  }
  worker_event_.Unlock();

  /* Disable the timeout.  This will ensure that we really wait for the worker
   * thread to shut down. */
  SetTimeout(-1);

  /* Shut down. */
  DoRequest(REQ_SHUTDOWN);
  worker_thread_.Wait();
}

bool RageWorkerThread::DoRequest(int request) {
  ASSERT(!timed_out_);
  ASSERT(request_ == REQ_NONE);

  if (timeout_.IsZero() && request != REQ_SHUTDOWN) {
    LOG->Warn(
        "Request made with timeout disabled (%s, iRequest = %i)", name_.c_str(),
        request);
  }

  /* Set the request, and wake up the worker thread. */
  worker_event_.Lock();

  request_ = request;
  worker_event_.Broadcast();

  /* Wait for it to complete or time out. */
  while (!request_finished_) {
    bool timed_out = !worker_event_.Wait(&timeout_);
    if (timed_out) {
      break;
    }
  }

  const bool request_finished = request_finished_;
  if (request_finished_) {
    /* The request finished successfully.  It's the calling function's
     * responsibility to clean up. */
    request_finished_ = false;
  } else {
    /* The request hasn't finished yet.  Set m_bTimedOut true.  This tells the
     * still-running request that it timed out, and so it's the thread's
     * responsibility to clean up--we can't do it, since we'd collide with the
     * request. */
    timed_out_ = true;
  }
  worker_event_.Unlock();

  return request_finished;
}

void RageWorkerThread::WorkerMain() {
  for (;;) {
    bool time_to_run_heartbeat = false;
    worker_event_.Lock();
    while (request_ == REQ_NONE && !time_to_run_heartbeat) {
      if (!worker_event_.Wait(heartbeat_ != -1 ? &next_heartbeat_ : nullptr)) {
        time_to_run_heartbeat = true;
      }
    }
    const int request = request_;
    request_ = REQ_NONE;

    worker_event_.Unlock();

    /* If it's time to run a heartbeat, do so. */
    if (time_to_run_heartbeat) {
      DoHeartbeat();

      /* Wake up anyone waiting for a heartbeat. */
      heartbeat_event_.Lock();
      heartbeat_event_.Broadcast();
      heartbeat_event_.Unlock();

      /* Schedule the next heartbeat. */
      next_heartbeat_.Touch();
      next_heartbeat_ += heartbeat_;
    }

    if (request != REQ_NONE) {
      /* Handle the request. */
      if (request != REQ_SHUTDOWN) {
        CHECKPOINT_M(ssprintf("HandleRequest(%i)", request));
        HandleRequest(request);
        CHECKPOINT_M(ssprintf("HandleRequest(%i) done", request));
      }

      /* Lock the mutex, to keep DoRequest where it is (if it's still running).
       */
      /* The request is finished.  If it timed out, clear the timeout flag and
       * call RequestTimedOut, to allow cleaning up. */
      worker_event_.Lock();

      if (timed_out_) {
        LOG->Trace("Request %i timed out", request);

        /* The calling thread timed out.  It's already gone and moved on, so
         * it's our responsibility to clean up.  No new requests will come in
         * until we clear m_bTimedOut, so we can safely unlock and clean up. */
        worker_event_.Unlock();

        RequestTimedOut();

        /* Clear the time-out flag, indicating that we can work again.  Wake
         * up StopThread, which may be waiting for this. */
        worker_event_.Lock();
        timed_out_ = false;
        worker_event_.Broadcast();
        worker_event_.Unlock();
      } else {
        CHECKPOINT_M(ssprintf("HandleRequest(%i) OK", request));

        request_finished_ = true;

        /* We're finished.  Wake up the requester. */
        worker_event_.Broadcast();
        worker_event_.Unlock();
      }
    }

    if (request == REQ_SHUTDOWN) {
      break;
    }
  }
}

bool RageWorkerThread::WaitForOneHeartbeat() {
  /* It doesn't make sense to wait for a heartbeat if there is no heartbeat. */
  ASSERT(heartbeat_ != -1);

  heartbeat_event_.Lock();
  bool timed_out = !heartbeat_event_.Wait(&timeout_);
  heartbeat_event_.Unlock();

  return !timed_out;
}

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
