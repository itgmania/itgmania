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
 * All operations must run in the thread, including retrieving directory lists,
 * Open() and closing files.  Operations own everything they use, such as read
 * and write buffers, so we don't clobber stuff if the operation times out, the
 * call returns and the operation then completes.
 *
 * Unmounting the filesystem will wait for all timed-out operations to complete.
 */

#include "RageFileDriverTimeout.h"

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

constexpr std::chrono::seconds OPERATION_TIMEOUT(5);

/* Directories with many files take longer to list. */
constexpr std::chrono::seconds LIST_TIMEOUT(10);

class ThreadedFileWorker {
 public:
  ThreadedFileWorker(const std::string& sPath);
  ~ThreadedFileWorker();

  RageFileDriver* GetChildDriver() const { return m_pChildDriver; }

  /* Runs fn in the worker and waits up to timeout.  Rejects calls during a
   * timeout.  fn may finish after a timeout, so it must capture by value. */
  bool Run(
      std::function<void()> fn,
      std::chrono::seconds timeout = OPERATION_TIMEOUT);

  /* Runs fn in the worker, not rejected or dropped on timeout. */
  void RunEventually(std::function<void()> fn);

 private:
  struct Operation {
    std::function<void()> fn;
    bool bMustRun = false;
    bool bStarted = false;
    bool bFinished = false;
    bool bCancelled = false;
    std::condition_variable Changed;
  };

  /* Call these with m_Mutex held. */
  std::shared_ptr<Operation> Queue(std::function<void()> fn, bool bMustRun);
  bool Wait(
      std::unique_lock<std::mutex>& lock, Operation& op,
      std::chrono::seconds timeout);

  static int StartWorkerMain(void* pThis) {
    static_cast<ThreadedFileWorker*>(pThis)->WorkerMain();
    return 0;
  }
  void WorkerMain();

  std::string m_sPath;
  RageFileDriver* m_pChildDriver;
  RageThread m_WorkerThread;

  std::mutex m_Mutex;
  std::condition_variable m_WorkAvailable;
  std::deque<std::shared_ptr<Operation>> m_Queue;
  /* An operation timed out, and the worker hasn't been idle since. */
  bool m_bTimedOut = false;
  bool m_bShutdown = false;
};

ThreadedFileWorker::ThreadedFileWorker(const std::string& sPath)
    : m_sPath(sPath) {
  /* Grab a reference to the child driver.  We'll operate on it directly. */
  m_pChildDriver = FILEMAN->GetFileDriver(sPath);
  if (m_pChildDriver == nullptr) {
    WARN(ssprintf(
        "ThreadedFileWorker: Mountpoint \"%s\" not found", sPath.c_str()));
  }

  m_WorkerThread.SetName(sPath);
  m_WorkerThread.Create(StartWorkerMain, this);
}

ThreadedFileWorker::~ThreadedFileWorker() {
  {
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_bShutdown = true;
    if (m_bTimedOut) {
      LOG->Trace(
          "Waiting for timed-out operations on \"%s\" to complete ...",
          m_sPath.c_str());
    }
  }
  m_WorkAvailable.notify_one();

  m_WorkerThread.Wait();

  if (m_pChildDriver != nullptr) {
    FILEMAN->ReleaseFileDriver(m_pChildDriver);
  }
}

void ThreadedFileWorker::WorkerMain() {
  std::unique_lock<std::mutex> lock(m_Mutex);
  for (;;) {
    m_WorkAvailable.wait(
        lock, [this] { return m_bShutdown || !m_Queue.empty(); });
    if (m_Queue.empty()) {
      return;
    }

    std::shared_ptr<Operation> pOp = m_Queue.front();
    m_Queue.pop_front();
    if (!pOp->bCancelled) {
      pOp->bStarted = true;
      pOp->Changed.notify_one();

      lock.unlock();
      pOp->fn();
      lock.lock();

      pOp->bFinished = true;
      pOp->Changed.notify_one();
    }

    if (m_bTimedOut && m_Queue.empty()) {
      m_bTimedOut = false;
      LOG->Trace(
          "Timed-out operations on \"%s\" have completed", m_sPath.c_str());
    }
  }
}

std::shared_ptr<ThreadedFileWorker::Operation> ThreadedFileWorker::Queue(
    std::function<void()> fn, bool bMustRun) {
  auto pOp = std::make_shared<Operation>();
  pOp->fn = std::move(fn);
  pOp->bMustRun = bMustRun;
  m_Queue.push_back(pOp);
  m_WorkAvailable.notify_one();
  return pOp;
}

bool ThreadedFileWorker::Wait(
    std::unique_lock<std::mutex>& lock, Operation& op,
    std::chrono::seconds timeout) {
  op.Changed.wait(
      lock, [&] { return op.bStarted || op.bCancelled || m_bTimedOut; });
  if (!op.bStarted) {
    return false;
  }

  if (op.Changed.wait_for(lock, timeout, [&op] { return op.bFinished; })) {
    return true;
  }

  m_bTimedOut = true;
  for (const std::shared_ptr<Operation>& pQueued : m_Queue) {
    pQueued->bCancelled = !pQueued->bMustRun;
    pQueued->Changed.notify_one();
  }
  LOG->Warn(
      "An operation on \"%s\" timed out; failing further access until it "
      "completes",
      m_sPath.c_str());
  return false;
}

bool ThreadedFileWorker::Run(
    std::function<void()> fn, std::chrono::seconds timeout) {
  std::unique_lock<std::mutex> lock(m_Mutex);
  if (m_pChildDriver == nullptr || m_bTimedOut) {
    return false;
  }

  std::shared_ptr<Operation> pOp = Queue(std::move(fn), false);
  return Wait(lock, *pOp, timeout);
}

void ThreadedFileWorker::RunEventually(std::function<void()> fn) {
  std::unique_lock<std::mutex> lock(m_Mutex);
  std::shared_ptr<Operation> pOp = Queue(std::move(fn), true);
  Wait(lock, *pOp, OPERATION_TIMEOUT);
}

class RageFileObjTimeout : public RageFileObj {
 public:
  RageFileObjTimeout(
      ThreadedFileWorker* pWorker, RageFileBasic* pFile, int iSize, int iMode)
      : m_pWorker(pWorker), m_pFile(pFile), m_iFileSize(iSize), m_iMode(iMode) {
    /* We have a lot of overhead per read and write operation, since we send
     * commands to the worker.  Buffer these operations. */
    if (iMode & RageFile::WRITE) {
      EnableWriteBuffering();
    }
    if (iMode & RageFile::READ) {
      EnableReadBuffering();
    }
  }

  RageFileObjTimeout(const RageFileObjTimeout& cpy, RageFileBasic* pFile)
      : RageFileObj(cpy),
        m_pWorker(cpy.m_pWorker),
        m_pFile(pFile),
        m_iFileSize(cpy.m_iFileSize),
        m_iMode(cpy.m_iMode) {}

  ~RageFileObjTimeout() {
    if (m_pFile != nullptr && (m_iMode & RageFile::WRITE)) {
      Flush();
    }
    if (m_pFile != nullptr) {
      Close();
    }
  }

  int GetFileSize() const { return m_iFileSize; }

  RageFileBasic* Copy() const {
    RageFileBasic* pCopy = nullptr;
    if (m_pFile != nullptr) {
      auto ppCopy = std::make_shared<RageFileBasic*>(nullptr);
      RageFileBasic* pFile = m_pFile;
      if (m_pWorker->Run([pFile, ppCopy] { *ppCopy = pFile->Copy(); })) {
        pCopy = *ppCopy;
      } else {
        /* Give up on this file, and on the copy if it's made after all. */
        Close();
        m_pWorker->RunEventually([ppCopy] { delete *ppCopy; });
      }
    }

    return new RageFileObjTimeout(*this, pCopy);
  }

 protected:
  int SeekInternal(int iPos) {
    return RunOnFile([iPos](RageFileBasic& file) { return file.Seek(iPos); });
  }

  int ReadInternal(void* pBuffer, size_t iBytes) {
    auto pData = std::make_shared<std::vector<char>>(iBytes);
    const int iGot = RunOnFile([pData](RageFileBasic& file) {
      return file.Read(pData->data(), pData->size());
    });
    if (iGot > 0) {
      memcpy(pBuffer, pData->data(), iGot);
    }
    return iGot;
  }

  int WriteInternal(const void* pBuffer, size_t iBytes) {
    auto pData = std::make_shared<std::string>(
        static_cast<const char*>(pBuffer), iBytes);
    return RunOnFile([pData](RageFileBasic& file) {
      return file.Write(pData->data(), pData->size());
    });
  }

  int FlushInternal() {
    return RunOnFile([](RageFileBasic& file) { return file.Flush(); });
  }

 private:
  /* See ThreadedFileWorker::Run for what fn may capture. */
  int RunOnFile(std::function<int(RageFileBasic&)> fn) {
    if (m_pFile == nullptr) {
      SetError("Operation timed out");
      return -1;
    }

    struct Result {
      int iRet = -1;
      std::string sError;
    };
    auto pResult = std::make_shared<Result>();
    RageFileBasic* pFile = m_pFile;
    if (!m_pWorker->Run([pFile, pResult, fn] {
          pResult->iRet = fn(*pFile);
          if (pResult->iRet == -1) {
            pResult->sError = pFile->GetError();
          }
        })) {
      Close();
      SetError("Operation timed out");
      return -1;
    }

    if (pResult->iRet == -1) {
      SetError(pResult->sError);
    }
    return pResult->iRet;
  }

  /* Closing can block too, so it's done in the worker.  This waits for it,
   * since closing a written file is what saves it, unless an earlier
   * operation timed out and may still be using the file. */
  void Close() const {
    RageFileBasic* pFile = m_pFile;
    m_pFile = nullptr;
    m_pWorker->RunEventually([pFile] { delete pFile; });
  }

  ThreadedFileWorker* m_pWorker;

  /* Null once given up after a timeout.  Mutable because Copy() is const,
   * but can give it up. */
  mutable RageFileBasic* m_pFile;

  int m_iFileSize;
  int m_iMode;
};

/* This FilenameDB runs PopulateFileSet in the worker. */
class TimedFilenameDB : public FilenameDB {
 public:
  TimedFilenameDB() {
    ExpireSeconds = -1;
    m_pWorker = nullptr;
  }

  void SetWorker(ThreadedFileWorker* pWorker) {
    ASSERT(pWorker != nullptr);
    m_pWorker = pWorker;
  }

  void PopulateFileSet(FileSet& fs, const std::string& sPath) {
    ASSERT(m_pWorker != nullptr);

    auto pResult = std::make_shared<FileSet>();
    RageFileDriver* pChildDriver = m_pWorker->GetChildDriver();
    if (!m_pWorker->Run(
            [pChildDriver, pResult, sPath] {
              pChildDriver->FDB->GetFileSetCopy(sPath, *pResult);
            },
            LIST_TIMEOUT)) {
      LOG->Trace("PopulateFileSet(%s) failed", sPath.c_str());
      return;
    }

    /* Only copy the files.  fs.m_bFilled tells other threads when fs is ready,
     * and FilenameDB sets it when we return. */
    fs.files = std::move(pResult->files);

    for (const File& file : fs.files) {
      const_cast<File&>(file).dirp = nullptr;
    }
  }

 private:
  ThreadedFileWorker* m_pWorker;
};

RageFileDriverTimeout::RageFileDriverTimeout(const std::string& sPath)
    : RageFileDriver(new TimedFilenameDB()) {
  m_pWorker = new ThreadedFileWorker(sPath);

  static_cast<TimedFilenameDB*>(FDB)->SetWorker(m_pWorker);
}

RageFileBasic* RageFileDriverTimeout::Open(
    const std::string& sPath, int iMode, int& iErr) {
  RageFileDriver* pChildDriver = m_pWorker->GetChildDriver();
  if (pChildDriver == nullptr) {
    iErr = ENODEV;
    return nullptr;
  }

  struct Result {
    RageFileBasic* pFile = nullptr;
    int iErr = 0;
    int iSize = 0;
  };
  auto pResult = std::make_shared<Result>();
  if (!m_pWorker->Run([pChildDriver, pResult, sPath, iMode] {
        pResult->pFile = pChildDriver->Open(sPath, iMode, pResult->iErr);
        /* GetFileSize can't report errors, and asking the device later could
         * time out, so get the size while opening. */
        if (pResult->pFile != nullptr && (iMode & RageFile::READ)) {
          pResult->iSize = pResult->pFile->GetFileSize();
        }
      })) {
    m_pWorker->RunEventually([pResult] { delete pResult->pFile; });
    LOG->Trace("Open(%s) failed", sPath.c_str());
    iErr = EFAULT; /* Win32 has no ETIMEDOUT */
    return nullptr;
  }

  iErr = pResult->iErr;
  if (pResult->pFile == nullptr) {
    return nullptr;
  }

  return new RageFileObjTimeout(
      m_pWorker, pResult->pFile, pResult->iSize, iMode);
}

void RageFileDriverTimeout::FlushDirCache(const std::string& sPath) {
  RageFileDriver::FlushDirCache(sPath);

  RageFileDriver* pChildDriver = m_pWorker->GetChildDriver();
  if (pChildDriver == nullptr) {
    return;
  }

  if (!m_pWorker->Run(
          [pChildDriver, sPath] { pChildDriver->FlushDirCache(sPath); })) {
    LOG->Trace("FlushDirCache(%s) failed", sPath.c_str());
  }
}

bool RageFileDriverTimeout::Move(
    const std::string& sOldPath, const std::string& sNewPath) {
  auto pResult = std::make_shared<bool>(false);
  RageFileDriver* pChildDriver = m_pWorker->GetChildDriver();
  if (!m_pWorker->Run([pChildDriver, pResult, sOldPath, sNewPath] {
        *pResult = pChildDriver->Move(sOldPath, sNewPath);
      }) ||
      !*pResult) {
    WARN(ssprintf(
        "RageFileDriverTimeout::Move(%s,%s) failed", sOldPath.c_str(),
        sNewPath.c_str()));
    return false;
  }

  return true;
}

bool RageFileDriverTimeout::Remove(const std::string& sPath) {
  auto pResult = std::make_shared<bool>(false);
  RageFileDriver* pChildDriver = m_pWorker->GetChildDriver();
  if (!m_pWorker->Run([pChildDriver, pResult, sPath] {
        *pResult = pChildDriver->Remove(sPath);
      }) ||
      !*pResult) {
    WARN(ssprintf("RageFileDriverTimeout::Remove(%s) failed", sPath.c_str()));
    return false;
  }

  return true;
}

RageFileDriverTimeout::~RageFileDriverTimeout() { delete m_pWorker; }

static struct FileDriverEntry_Timeout : public FileDriverEntry {
  FileDriverEntry_Timeout() : FileDriverEntry("TIMEOUT") {}
  RageFileDriver* Create(const std::string& sRoot) const {
    return new RageFileDriverTimeout(sRoot);
  }
} const g_RegisterDriver;

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
