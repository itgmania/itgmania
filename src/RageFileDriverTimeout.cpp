/*
 * This is a filesystem wrapper driver.  To use it, mount it on top of another
 * filesystem at a different mountpoint.  For example, mount the local path
 * "d:/" as a normal directory to /cdrom-native:
 *
 * FILEMAN->Mount( "dir", "d:/", "/cdrom-native" );
 *
 * and then mount a timeout filesystem on top of that:
 *
 * FILEMAN->Mount( "timeout", "/cdrom-native", "/cdrom-timeout" );
 *
 * and do all access to the device at /cdrom-timeout.
 *
 * A common problem with accessing devices, such as CDROMs or flaky pen drives,
 * is that they can take a very long time to fail under error conditions.  A
 * single request may have a timeout of 500ms, but a single operation may do
 * many requests.  The system will usually retry operations, and we want to
 * allow it to do so, but we don't want the high-level operation to take too
 * long as a whole.
 *
 * There's no portable way to abort a running filesystem operation.  In Unix,
 * you may be able to use SIGALRM, which should cause a running operation to
 * fail with EINTR, but I don't trust that, and it's not portable.
 *
 * This driver abstracts all access to another driver, and enforces a timeout
 * on each operation.  Operations run one at a time, in order, in a worker
 * thread.  If an operation takes too long once it starts, the calling thread
 * will see it fail.  The operation in the thread will actually continue, and
 * will time out for real eventually.  To prevent accumulation of stuck
 * operations, if an operation times out, we'll refuse all further access,
 * including operations already queued, until the thread is idle again.  (Load
 * a separate driver for each device, so if one device fails, others continue
 * to function.)
 *
 * Callers can also set a deadline for a whole task, for devices that respond,
 * but very slowly.
 *
 * All operations must run in the thread, including retrieving directory lists,
 * Open() and closing files.  Operations own everything they use, such as read
 * and write buffers, so we don't clobber stuff if the operation times out, the
 * call returns and the operation then completes.
 *
 * Unmounting the filesystem will wait for all timed-out operations to complete.
 */

#include "RageFileDriverTimeout.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "RageFile.h"
#include "RageFileBasic.h"
#include "RageFileDriver.h"
#include "RageFileManager.h"
#include "RageLog.h"
#include "RageThreads.h"
#include "RageUtil.h"
#include "RageUtil_FileDB.h"
#include "global.h"

constexpr std::chrono::seconds kOperationTimeout(5);

/* Directories with many files take longer to list. */
constexpr std::chrono::seconds kListTimeout(10);

class ThreadedFileWorker {
 public:
  ThreadedFileWorker(const std::string& path);
  ~ThreadedFileWorker();

  RageFileDriver* GetChildDriver() const { return child_driver_; }

  /* Runs fn in the worker and waits up to timeout.  Rejects calls during a
   * timeout.  fn may finish after a timeout, so it must capture by value. */
  bool Run(
      std::function<void()> fn,
      std::chrono::seconds timeout = kOperationTimeout);

  /* Runs fn in the worker, not rejected or dropped on timeout. */
  void RunEventually(std::function<void()> fn);

  void SetDeadline(std::chrono::steady_clock::time_point deadline);

 private:
  struct Operation {
    std::function<void()> fn_;
    bool must_run_ = false;
    bool started_ = false;
    bool finished_ = false;
    bool cancelled_ = false;
    std::condition_variable changed_;
  };

  /* Call these with m_Mutex held. */
  std::shared_ptr<Operation> Queue(std::function<void()> fn, bool must_run);
  bool Wait(
      std::unique_lock<std::mutex>& lock, Operation& op,
      std::chrono::seconds timeout,
      std::chrono::steady_clock::time_point deadline);

  static int StartWorkerMain(void* p_this) {
    static_cast<ThreadedFileWorker*>(p_this)->WorkerMain();
    return 0;
  }
  void WorkerMain();

  std::string path_;
  RageFileDriver* child_driver_;
  RageThread worker_thread_;

  std::mutex mutex_;
  std::condition_variable work_available_;
  std::deque<std::shared_ptr<Operation>> queue_;
  /* An operation timed out, and the worker hasn't been idle since. */
  bool timed_out_ = false;
  std::chrono::steady_clock::time_point deadline_ =
      std::chrono::steady_clock::time_point::max();
  bool shutdown_ = false;
};

ThreadedFileWorker::ThreadedFileWorker(const std::string& path) : path_(path) {
  /* Grab a reference to the child driver.  We'll operate on it directly. */
  child_driver_ = FILEMAN->GetFileDriver(path);
  if (child_driver_ == nullptr) {
    WARN(ssprintf(
        "ThreadedFileWorker: Mountpoint \"%s\" not found", path.c_str()));
  }

  worker_thread_.SetName(path);
  worker_thread_.Create(StartWorkerMain, this);
}

ThreadedFileWorker::~ThreadedFileWorker() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    shutdown_ = true;
    if (timed_out_) {
      LOG->Trace(
          "Waiting for timed-out operations on \"%s\" to complete ...",
          path_.c_str());
    }
  }
  work_available_.notify_one();

  worker_thread_.Wait();

  if (child_driver_ != nullptr) {
    FILEMAN->ReleaseFileDriver(child_driver_);
  }
}

void ThreadedFileWorker::WorkerMain() {
  std::unique_lock<std::mutex> lock(mutex_);
  for (;;) {
    work_available_.wait(lock, [this] { return shutdown_ || !queue_.empty(); });
    if (queue_.empty()) {
      return;
    }

    std::shared_ptr<Operation> op = queue_.front();
    queue_.pop_front();
    if (!op->cancelled_) {
      op->started_ = true;
      op->changed_.notify_one();

      lock.unlock();
      op->fn_();
      lock.lock();

      op->finished_ = true;
      op->changed_.notify_one();
    }

    if (timed_out_ && queue_.empty()) {
      timed_out_ = false;
      LOG->Trace(
          "Timed-out operations on \"%s\" have completed", path_.c_str());
    }
  }
}

std::shared_ptr<ThreadedFileWorker::Operation> ThreadedFileWorker::Queue(
    std::function<void()> fn, bool must_run) {
  auto op = std::make_shared<Operation>();
  op->fn_ = std::move(fn);
  op->must_run_ = must_run;
  queue_.push_back(op);
  work_available_.notify_one();
  return op;
}

bool ThreadedFileWorker::Wait(
    std::unique_lock<std::mutex>& lock, Operation& op,
    std::chrono::seconds timeout,
    std::chrono::steady_clock::time_point deadline) {
  op.changed_.wait_until(lock, deadline, [&] {
    return op.started_ || op.cancelled_ || timed_out_;
  });
  if (!op.started_) {
    op.cancelled_ = !op.must_run_;
    return false;
  }

  deadline = std::min(std::chrono::steady_clock::now() + timeout, deadline);
  if (op.changed_.wait_until(lock, deadline, [&op] { return op.finished_; })) {
    return true;
  }

  timed_out_ = true;
  for (const std::shared_ptr<Operation>& queued : queue_) {
    queued->cancelled_ = !queued->must_run_;
    queued->changed_.notify_one();
  }
  LOG->Warn(
      "An operation on \"%s\" timed out; failing further access until it "
      "completes",
      path_.c_str());
  return false;
}

bool ThreadedFileWorker::Run(
    std::function<void()> fn, std::chrono::seconds timeout) {
  std::unique_lock<std::mutex> lock(mutex_);
  if (child_driver_ == nullptr || timed_out_ ||
      std::chrono::steady_clock::now() >= deadline_) {
    return false;
  }

  std::shared_ptr<Operation> op = Queue(std::move(fn), false);
  return Wait(lock, *op, timeout, deadline_);
}

void ThreadedFileWorker::SetDeadline(
    std::chrono::steady_clock::time_point deadline) {
  std::lock_guard<std::mutex> lock(mutex_);
  deadline_ = deadline;
}

void ThreadedFileWorker::RunEventually(std::function<void()> fn) {
  std::unique_lock<std::mutex> lock(mutex_);
  std::shared_ptr<Operation> op = Queue(std::move(fn), true);
  Wait(
      lock, *op, kOperationTimeout,
      std::chrono::steady_clock::time_point::max());
}

class RageFileObjTimeout : public RageFileObj {
 public:
  RageFileObjTimeout(
      ThreadedFileWorker* worker, RageFileBasic* file, int size, int mode)
      : worker_(worker), file_(file), file_size_(size), mode_(mode) {
    /* We have a lot of overhead per read and write operation, since we send
     * commands to the worker.  Buffer these operations. */
    if (mode & RageFile::WRITE) {
      EnableWriteBuffering();
    }
    if (mode & RageFile::READ) {
      EnableReadBuffering();
    }
  }

  RageFileObjTimeout(const RageFileObjTimeout& cpy, RageFileBasic* file)
      : RageFileObj(cpy),
        worker_(cpy.worker_),
        file_(file),
        file_size_(cpy.file_size_),
        mode_(cpy.mode_) {}

  ~RageFileObjTimeout() {
    if (file_ != nullptr && (mode_ & RageFile::WRITE)) {
      Flush();
    }
    if (file_ != nullptr) {
      Close();
    }
  }

  int GetFileSize() const { return file_size_; }

  RageFileBasic* Copy() const {
    RageFileBasic* p_copy = nullptr;
    if (file_ != nullptr) {
      auto pp_copy = std::make_shared<RageFileBasic*>(nullptr);
      RageFileBasic* file = file_;
      if (worker_->Run([file, pp_copy] { *pp_copy = file->Copy(); })) {
        p_copy = *pp_copy;
      } else {
        /* Give up on this file, and on the copy if it's made after all. */
        Close();
        worker_->RunEventually([pp_copy] { delete *pp_copy; });
      }
    }

    return new RageFileObjTimeout(*this, p_copy);
  }

 protected:
  int SeekInternal(int pos) {
    return RunOnFile([pos](RageFileBasic& file) { return file.Seek(pos); });
  }

  int ReadInternal(void* buffer, size_t bytes) {
    auto data = std::make_shared<std::vector<char>>(bytes);
    const int got = RunOnFile([data](RageFileBasic& file) {
      return file.Read(data->data(), data->size());
    });
    if (got > 0) {
      memcpy(buffer, data->data(), got);
    }
    return got;
  }

  int WriteInternal(const void* buffer, size_t bytes) {
    auto data =
        std::make_shared<std::string>(static_cast<const char*>(buffer), bytes);
    return RunOnFile([data](RageFileBasic& file) {
      return file.Write(data->data(), data->size());
    });
  }

  int FlushInternal() {
    return RunOnFile([](RageFileBasic& file) { return file.Flush(); });
  }

 private:
  /* See ThreadedFileWorker::Run for what fn may capture. */
  int RunOnFile(std::function<int(RageFileBasic&)> fn) {
    if (file_ == nullptr) {
      SetError("Operation timed out");
      return -1;
    }

    struct Result {
      int ret_ = -1;
      std::string error_;
    };
    auto result = std::make_shared<Result>();
    RageFileBasic* file = file_;
    if (!worker_->Run([file, result, fn] {
          result->ret_ = fn(*file);
          if (result->ret_ == -1) {
            result->error_ = file->GetError();
          }
        })) {
      Close();
      SetError("Operation timed out");
      return -1;
    }

    if (result->ret_ == -1) {
      SetError(result->error_);
    }
    return result->ret_;
  }

  /* Closing can block too, so it's done in the worker.  This waits for it,
   * since closing a written file is what saves it, unless an earlier
   * operation timed out and may still be using the file. */
  void Close() const {
    RageFileBasic* file = file_;
    file_ = nullptr;
    worker_->RunEventually([file] { delete file; });
  }

  ThreadedFileWorker* worker_;

  /* Null once given up after a timeout.  Mutable because Copy() is const,
   * but can give it up. */
  mutable RageFileBasic* file_;

  int file_size_;
  int mode_;
};

/* This FilenameDB runs PopulateFileSet in the worker. */
class TimedFilenameDB : public FilenameDB {
 public:
  TimedFilenameDB() {
    expire_seconds_ = -1;
    worker_ = nullptr;
  }

  void SetWorker(ThreadedFileWorker* worker) {
    ASSERT(worker != nullptr);
    worker_ = worker;
  }

  void PopulateFileSet(FileSet& fs, const std::string& path) {
    ASSERT(worker_ != nullptr);

    auto result = std::make_shared<FileSet>();
    RageFileDriver* child_driver = worker_->GetChildDriver();
    if (!worker_->Run(
            [child_driver, result, path] {
              child_driver->fdb_->GetFileSetCopy(path, *result);
            },
            kListTimeout)) {
      LOG->Trace("PopulateFileSet(%s) failed", path.c_str());
      return;
    }

    /* Only copy the files.  fs.m_bFilled tells other threads when fs is ready,
     * and FilenameDB sets it when we return. */
    fs.files_ = std::move(result->files_);

    for (const File& file : fs.files_) {
      const_cast<File&>(file).dirp_ = nullptr;
    }
  }

 private:
  ThreadedFileWorker* worker_;
};

RageFileDriverTimeout::RageFileDriverTimeout(const std::string& path)
    : RageFileDriver(new TimedFilenameDB()) {
  worker_ = new ThreadedFileWorker(path);

  static_cast<TimedFilenameDB*>(fdb_)->SetWorker(worker_);
}

RageFileBasic* RageFileDriverTimeout::Open(
    const std::string& path, int mode, int& err) {
  RageFileDriver* child_driver = worker_->GetChildDriver();
  if (child_driver == nullptr) {
    err = ENODEV;
    return nullptr;
  }

  struct Result {
    RageFileBasic* file_ = nullptr;
    int err_ = 0;
    int size_ = 0;
  };
  auto result = std::make_shared<Result>();
  if (!worker_->Run([child_driver, result, path, mode] {
        result->file_ = child_driver->Open(path, mode, result->err_);
        /* GetFileSize can't report errors, and asking the device later could
         * time out, so get the size while opening. */
        if (result->file_ != nullptr && (mode & RageFile::READ)) {
          result->size_ = result->file_->GetFileSize();
        }
      })) {
    worker_->RunEventually([result] { delete result->file_; });
    LOG->Trace("Open(%s) failed", path.c_str());
    err = EFAULT; /* Win32 has no ETIMEDOUT */
    return nullptr;
  }

  err = result->err_;
  if (result->file_ == nullptr) {
    return nullptr;
  }

  return new RageFileObjTimeout(worker_, result->file_, result->size_, mode);
}

void RageFileDriverTimeout::SetDeadline(float seconds) {
  auto deadline = std::chrono::steady_clock::time_point::max();
  if (seconds >= 0) {
    deadline = std::chrono::steady_clock::now() +
               std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                   std::chrono::duration<float>(seconds));
  }
  worker_->SetDeadline(deadline);
}

void RageFileDriverTimeout::FlushDirCache(const std::string& path) {
  RageFileDriver::FlushDirCache(path);

  RageFileDriver* child_driver = worker_->GetChildDriver();
  if (child_driver == nullptr) {
    return;
  }

  if (!worker_->Run(
          [child_driver, path] { child_driver->FlushDirCache(path); })) {
    LOG->Trace("FlushDirCache(%s) failed", path.c_str());
  }
}

bool RageFileDriverTimeout::Move(
    const std::string& old_path, const std::string& new_path) {
  auto result = std::make_shared<bool>(false);
  RageFileDriver* child_driver = worker_->GetChildDriver();
  if (!worker_->Run([child_driver, result, old_path, new_path] {
        *result = child_driver->Move(old_path, new_path);
      }) ||
      !*result) {
    WARN(ssprintf(
        "RageFileDriverTimeout::Move(%s,%s) failed", old_path.c_str(),
        new_path.c_str()));
    return false;
  }

  return true;
}

bool RageFileDriverTimeout::Remove(const std::string& path) {
  auto result = std::make_shared<bool>(false);
  RageFileDriver* child_driver = worker_->GetChildDriver();
  if (!worker_->Run([child_driver, result, path] {
        *result = child_driver->Remove(path);
      }) ||
      !*result) {
    WARN(ssprintf("RageFileDriverTimeout::Remove(%s) failed", path.c_str()));
    return false;
  }

  return true;
}

RageFileDriverTimeout::~RageFileDriverTimeout() { delete worker_; }

static struct FileDriverEntry_Timeout : public FileDriverEntry {
  FileDriverEntry_Timeout() : FileDriverEntry("TIMEOUT") {}
  RageFileDriver* Create(const std::string& root) const {
    return new RageFileDriverTimeout(root);
  }
} const kRegisterDriver;

/*
 * Copyright (c) 2005 Glenn Maynard
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
