#include "RageLog.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "RageFile.h"
#include "RageThreads.h"
#include "RageTimer.h"
#include "RageUtil.h"
#include "global.h"

#if defined(_WIN32)
#include <windows.h>
#endif

RageLog* LOG;  // global and accessible from anywhere in the program

/*
 * We have a couple log types and a couple logs.
 *
 * Traces are for very verbose debug information.  Use them as much as you want.
 *
 * Warnings are for things that shouldn't happen, but can be dealt with.  (If
 * they can't be dealt with, use RageException::Throw, which will also send a
 * warning.)
 *
 * Info is for important bits of information.  These should be used selectively.
 * Try to keep Info text dense; lots of information is fine, but try to keep
 * it to a reasonable total length.
 *
 * log.txt receives all logs.  This file can get rather large.
 *
 * info.txt receives warnings and infos.  This file should be fairly small;
 * small enough to be mailed without having to be edited or zipped, and small
 * enough to be very easily read.
 */

/* Map data names to logged data.
 *
 * This lets us keep individual bits of changing information to be present
 * in crash logs.  For example, we want to know which file was being processed
 * if we crash during a texture load.  However, we don't want to log every
 * load, since there are a huge number, even for log.txt.  We only want to
 * know the current one, if any.
 *
 * So, when a texture begins loading, we do:
 * LOG->MapLog("TextureManager::Load", "Loading foo.png");
 * and when it finishes loading without crashing,
 * LOG->UnmapLog("TextureManager::Load");
 *
 * Each time a mapped log changes, we update a block of static text to be put
 * in info.txt, so we see "Loading foo.png".
 *
 * The identifier is never displayed, so we can use a simple local object to
 * map/unmap, using any mechanism to generate unique IDs. */
static std::map<std::string, std::string> LogMaps;

#define LOG_PATH "/Logs/log.txt"
#define INFO_PATH "/Logs/info.txt"
#define TIME_PATH "/Logs/timelog.txt"
#define USER_PATH "/Logs/userlog.txt"

static RageFile *g_fileLog, *g_fileInfo, *g_fileUserLog, *g_fileTimeLog;

/* Mutex writes to the files.  Writing to files is not thread-aware, and this is
 * the only place we write to the same file from multiple threads. */
static RageMutex* g_Mutex;

/* staticlog gets info.txt
 * crashlog gets log.txt */
enum {
  /* If this is set, the message will also be written to info.txt. (info and
     warnings) */
  WRITE_TO_INFO = 0x01,

  /* If this is set, the message will also be written to userlog.txt. (user
     warnings only) */
  WRITE_TO_USER_LOG = 0x02,

  /* Whether this line should be loud when written to log.txt (warnings). */
  WRITE_LOUD = 0x04,
  WRITE_TO_TIME = 0x08
};

struct RageLog::LogWriter {
  static constexpr int FLUSH_INTERVAL_MS = 250;
  static constexpr int MAX_BATCH_BYTES = 256 * 1024;
  static constexpr float FLUSH_INTERVAL_SEC =
      (float)FLUSH_INTERVAL_MS / 1000.0f;

  enum LogFileMask {
    FileLog = 0x10,
    FileInfo = 0x20,
    FileUser = 0x40,
    FileTime = 0x80
  };

  enum LogCommandType {
    RAGELOG_WRITE_LINE,
    RAGELOG_ENABLE,
    RAGELOG_REQUEST_FLUSH,
    RAGELOG_FLUSH_AND_WAIT,
    RAGELOG_HALT
  };

  struct LogCommand {
    LogCommandType type;
    int fileMask;
    std::string line;
    uint64_t token;
    bool enable;
  };

  RageLog* m_pOwner;
  RageThread m_Thread;
  RageEvent m_Event;
  std::deque<LogCommand> m_Queue;
  uint64_t m_uNextToken;
  uint64_t m_uCompletedToken;
  bool m_bWantFlushSoon;
  bool m_bRequestShutdown;
  int m_iEnabledMask;

  LogWriter(RageLog* owner);
  static int StartThreadMain(void* p);
  void Start();
  void Stop();
  void EnqueueLine(int fileMask, const std::string& line);
  void RequestFlushSoon();
  void RequestFlushAndWait();
  void RequestSetEnabledAndWait(int fileMask, bool enable);

 private:
  uint64_t NextTokenLocked();
  void CompleteTokenLocked(uint64_t token);
  void WaitForTokenLocked(uint64_t token);
  static void AppendLine(std::string& out, const std::string& line);
  void FlushOpenFiles();
  void SetEnabledInternal(int fileMask, bool enable);
  int ThreadMain();
};

RageLog::RageLog()
    : m_bLogToDisk(false),
      m_bInfoToDisk(false),
      m_bUserLogToDisk(false),
      m_bFlush(false),
      m_bShowLogOutput(false),
      m_pLogWriter(nullptr) {
  g_fileLog = new RageFile;
  g_fileInfo = new RageFile;
  g_fileUserLog = new RageFile;
  g_fileTimeLog = new RageFile;

  g_Mutex = new RageMutex("Log");

  m_pLogWriter = new LogWriter(this);
  m_pLogWriter->Start();
}

RageLog::~RageLog() {
  /* Add the mapped log data to info.txt. */
  const std::string AdditionalLog = GetAdditionalLog();
  std::vector<std::string> AdditionalLogLines;
  split(AdditionalLog, "\n", AdditionalLogLines);
  for (unsigned i = 0; i < AdditionalLogLines.size(); ++i) {
    Trim(AdditionalLogLines[i]);
    this->Info("%s", AdditionalLogLines[i].c_str());
  }

  Flush();

  if (m_pLogWriter != nullptr) {
    m_pLogWriter->Stop();
    delete m_pLogWriter;
    m_pLogWriter = nullptr;
  }

  SetShowLogOutput(false);
  g_fileLog->Close();
  g_fileInfo->Close();
  g_fileUserLog->Close();
  g_fileTimeLog->Close();

  RageUtil::SafeDelete(g_Mutex);
  RageUtil::SafeDelete(g_fileLog);
  RageUtil::SafeDelete(g_fileInfo);
  RageUtil::SafeDelete(g_fileUserLog);
}

void RageLog::SetLogToDisk(bool b) {
  if (m_bLogToDisk == b) {
    return;
  }

  m_bLogToDisk = b;

  if (m_pLogWriter != nullptr) {
    m_pLogWriter->RequestSetEnabledAndWait(
        LogWriter::FileLog | LogWriter::FileTime, b);
  }
}

void RageLog::SetInfoToDisk(bool b) {
  if (m_bInfoToDisk == b) {
    return;
  }

  m_bInfoToDisk = b;

  if (m_pLogWriter != nullptr) {
    m_pLogWriter->RequestSetEnabledAndWait(LogWriter::FileInfo, b);
  }
}

void RageLog::SetUserLogToDisk(bool b) {
  if (m_bUserLogToDisk == b) {
    return;
  }

  m_bUserLogToDisk = b;

  if (m_pLogWriter != nullptr) {
    m_pLogWriter->RequestSetEnabledAndWait(LogWriter::FileUser, b);
  }
}

void RageLog::SetFlushing(bool b) { m_bFlush = b; }

/* Enable or disable display of output to stdout, or a console window in
 * Windows. */
void RageLog::SetShowLogOutput(bool show) {
  m_bShowLogOutput = show;

#if defined(_WIN32)
  if (m_bShowLogOutput) {
    // create a new console window and attach standard handles
    AllocConsole();
    SetConsoleOutputCP(CP_UTF8);
    freopen("CONOUT$", "wb", stdout);
    freopen("CONOUT$", "wb", stderr);
  } else {
    FreeConsole();
  }
#endif
}

void RageLog::Trace(const char* fmt, ...) {
  va_list va;
  va_start(va, fmt);
  std::string sBuff = vssprintf(fmt, va);
  va_end(va);

  Write(0, sBuff);
}

/* Use this for more important information; it'll always be included
 * in crash dumps. */
void RageLog::Info(const char* fmt, ...) {
  va_list va;
  va_start(va, fmt);
  std::string sBuff = vssprintf(fmt, va);
  va_end(va);

  Write(WRITE_TO_INFO, sBuff);
}

void RageLog::Warn(const char* fmt, ...) {
  va_list va;
  va_start(va, fmt);
  std::string sBuff = vssprintf(fmt, va);
  va_end(va);

  Write(WRITE_TO_INFO | WRITE_LOUD, sBuff);
}

void RageLog::Time(const char* fmt, ...) {
  va_list va;
  va_start(va, fmt);
  std::string sBuff = vssprintf(fmt, va);
  va_end(va);

  Write(WRITE_TO_TIME, sBuff);
}

void RageLog::UserLog(
    const std::string& sType, const std::string& sElement, const char* fmt,
    ...) {
  va_list va;
  va_start(va, fmt);
  std::string sBuf = vssprintf(fmt, va);
  va_end(va);

  if (!sType.empty()) {
    sBuf =
        ssprintf("%s \"%s\" %s", sType.c_str(), sElement.c_str(), sBuf.c_str());
  }

  Write(WRITE_TO_USER_LOG, sBuf);
}

void RageLog::Write(int where, const std::string& sLine) {
  LockMut(*g_Mutex);

  const char* const sWarningSeparator =
      "/////////////////////////////////////////";
  std::vector<std::string> asLines;
  split(sLine, "\n", asLines, false);
  if (where & WRITE_LOUD) {
    if (m_pLogWriter != nullptr) {
      m_pLogWriter->EnqueueLine(LogWriter::FileLog, sWarningSeparator);
    }
    puts(sWarningSeparator);
  }

  std::string sTimestamp =
      MicrosecondsToMMSSMsMsMs(RageTimer::GetTimeSinceStartMicroseconds()) +
      ": ";
  std::string sWarning;
  if (where & WRITE_LOUD) {
    sWarning = "WARNING: ";
  }

  for (unsigned i = 0; i < asLines.size(); ++i) {
    std::string& sStr = asLines[i];

    if (sWarning.size()) {
      sStr.insert(0, sWarning);
    }

    // NOTE: On Windows, m_bShowLogOutput determines whether we
    // will open a console window for log output.
    // But, on Linux/macOS, that preference won't open a new
    // console window. Instead it determines whether or not
    // info & trace logs are printed to stdout. This way,
    // we have a consistent logging behavior across platforms,
    // since typically in macOS or Linux, if one wants to see
    // the stdout output, they'll run the game from a terminal.
#ifdef _WIN32
    if (m_bShowLogOutput || (where & WRITE_TO_INFO)) {
      puts(sStr.c_str());  // fputws( (const wchar_t *)sStr.c_str(), stdout );
    }
    if (where & WRITE_TO_INFO) {
      AddToInfo(sStr);
    }
#else
    if (where & WRITE_TO_INFO) {
      puts(sStr.c_str());
      AddToInfo(sStr);
    }
#endif

    if (m_bLogToDisk && (where & WRITE_TO_INFO) && g_fileInfo->IsOpen()) {
      g_fileInfo->PutLine(sStr);
    }
    if (m_bUserLogToDisk && (where & WRITE_TO_USER_LOG) &&
        g_fileUserLog->IsOpen()) {
      g_fileUserLog->PutLine(sStr);
    }

    /* Add a timestamp to log.txt and RecentLogs, but not the rest of info.txt
     * and stdout. */
    sStr.insert(0, sTimestamp);

    if (m_pLogWriter != nullptr) {
      if (m_bLogToDisk && (where & WRITE_TO_TIME)) {
        m_pLogWriter->EnqueueLine(LogWriter::FileTime, sStr);
      }
      if (m_bLogToDisk) {
        m_pLogWriter->EnqueueLine(LogWriter::FileLog, sStr);
      }
    }

    AddToRecentLogs(sStr);
  }

  if (where & WRITE_LOUD) {
    if (m_pLogWriter != nullptr) {
      m_pLogWriter->EnqueueLine(LogWriter::FileLog, sWarningSeparator);
    }
    puts(sWarningSeparator);
  }
  if (m_bFlush || (where & WRITE_TO_INFO)) {
    // Hint the writer to flush sooner for info/warn so users see important
    // lines on disk quickly without forcing a synchronous flush.
    if (m_pLogWriter != nullptr) {
      if (where & WRITE_TO_INFO) {
        m_pLogWriter->RequestFlushSoon();
      }
    }
    if (m_bFlush) {
      Flush();
    }
  }
}

void RageLog::Flush() {
  if (m_pLogWriter != nullptr) {
    m_pLogWriter->RequestFlushAndWait();
  }
}

const char* ragelog_thread_name = "RageLog";
RageLog::LogWriter::LogWriter(RageLog* owner)
    : m_pOwner(owner),
      m_Event(ragelog_thread_name),
      m_uNextToken(1),
      m_uCompletedToken(0),
      m_bWantFlushSoon(false),
      m_bRequestShutdown(false),
      m_iEnabledMask(0) {
  m_Thread.SetName(ragelog_thread_name);
}

int RageLog::LogWriter::StartThreadMain(void* p) {
  return static_cast<LogWriter*>(p)->ThreadMain();
}

void RageLog::LogWriter::Start() { m_Thread.Create(StartThreadMain, this); }

void RageLog::LogWriter::Stop() {
  RequestFlushAndWait();
  {
    m_Event.Lock();
    LogCommand cmd;
    cmd.type = RAGELOG_HALT;
    cmd.fileMask = 0;
    cmd.token = NextTokenLocked();
    cmd.enable = false;
    m_Queue.push_back(cmd);
    m_Event.Broadcast();
    WaitForTokenLocked(cmd.token);
    m_Event.Unlock();
  }
  m_Thread.Wait();
}

void RageLog::LogWriter::EnqueueLine(int fileMask, const std::string& line) {
  if (fileMask == 0) {
    return;
  }
  m_Event.Lock();
  LogCommand cmd;
  cmd.type = RAGELOG_WRITE_LINE;
  cmd.fileMask = fileMask;
  cmd.line = line;
  cmd.token = 0;
  cmd.enable = false;
  m_Queue.push_back(cmd);
  m_Event.Broadcast();
  m_Event.Unlock();
}

void RageLog::LogWriter::RequestFlushSoon() {
  m_Event.Lock();
  LogCommand cmd;
  cmd.type = RAGELOG_REQUEST_FLUSH;
  cmd.fileMask = 0;
  cmd.token = 0;
  cmd.enable = false;
  m_Queue.push_back(cmd);
  m_Event.Broadcast();
  m_Event.Unlock();
}

void RageLog::LogWriter::RequestFlushAndWait() {
  m_Event.Lock();
  LogCommand cmd;
  cmd.type = RAGELOG_FLUSH_AND_WAIT;
  cmd.fileMask = 0;
  cmd.token = NextTokenLocked();
  cmd.enable = false;
  m_Queue.push_back(cmd);
  m_Event.Broadcast();
  WaitForTokenLocked(cmd.token);
  m_Event.Unlock();
}

void RageLog::LogWriter::RequestSetEnabledAndWait(int fileMask, bool enable) {
  m_Event.Lock();
  LogCommand cmd;
  cmd.type = RAGELOG_ENABLE;
  cmd.fileMask = fileMask;
  cmd.token = NextTokenLocked();
  cmd.enable = enable;
  m_Queue.push_back(cmd);
  m_Event.Broadcast();
  WaitForTokenLocked(cmd.token);
  m_Event.Unlock();
}

uint64_t RageLog::LogWriter::NextTokenLocked() { return m_uNextToken++; }

void RageLog::LogWriter::CompleteTokenLocked(uint64_t token) {
  if (token > m_uCompletedToken) {
    m_uCompletedToken = token;
  }
  m_Event.Broadcast();
}

void RageLog::LogWriter::WaitForTokenLocked(uint64_t token) {
  while (m_uCompletedToken < token) {
    m_Event.Wait();
  }
}

void RageLog::LogWriter::AppendLine(std::string& out, const std::string& line) {
  out.append(line);
  out.append("\n");
}

void RageLog::LogWriter::FlushOpenFiles() {
  if ((m_iEnabledMask & FileLog) && g_fileLog->IsOpen()) {
    g_fileLog->Flush();
  }
  if ((m_iEnabledMask & FileInfo) && g_fileInfo->IsOpen()) {
    g_fileInfo->Flush();
  }
  if ((m_iEnabledMask & FileTime) && g_fileTimeLog->IsOpen()) {
    g_fileTimeLog->Flush();
  }
  if ((m_iEnabledMask & FileUser) && g_fileUserLog->IsOpen()) {
    g_fileUserLog->Flush();
  }
}

// Enable or disable writing for the specified file mask.
// When enabling, open files as needed.
// when disabling, flush and close to avoid losing buffered logs.
void RageLog::LogWriter::SetEnabledInternal(int fileMask, bool enable) {
  if (enable) {
    m_iEnabledMask |= fileMask;
    if ((fileMask & FileLog) && !g_fileLog->IsOpen()) {
      if (!g_fileLog->Open(LOG_PATH, RageFile::WRITE | RageFile::STREAMED)) {
        fprintf(
            stderr, "Couldn't open %s: %s\n", LOG_PATH,
            g_fileLog->GetError().c_str());
      }
    }
    if ((fileMask & FileInfo) && !g_fileInfo->IsOpen()) {
      if (!g_fileInfo->Open(INFO_PATH, RageFile::WRITE | RageFile::STREAMED)) {
        fprintf(
            stderr, "Couldn't open %s: %s\n", INFO_PATH,
            g_fileInfo->GetError().c_str());
      }
    }
    if ((fileMask & FileUser) && !g_fileUserLog->IsOpen()) {
      if (!g_fileUserLog->Open(
              USER_PATH, RageFile::WRITE | RageFile::STREAMED)) {
        fprintf(
            stderr, "Couldn't open %s: %s\n", USER_PATH,
            g_fileUserLog->GetError().c_str());
      }
    }
    if ((fileMask & FileTime) && !g_fileTimeLog->IsOpen()) {
      if (!g_fileTimeLog->Open(
              TIME_PATH, RageFile::WRITE | RageFile::STREAMED)) {
        fprintf(
            stderr, "Couldn't open %s: %s\n", TIME_PATH,
            g_fileTimeLog->GetError().c_str());
      }
    }
  } else {
    // Drain to disk before closing to avoid dropping buffered logs.
    FlushOpenFiles();
    m_iEnabledMask &= ~fileMask;
    if ((fileMask & FileLog) && g_fileLog->IsOpen()) {
      g_fileLog->Close();
    }
    if ((fileMask & FileInfo) && g_fileInfo->IsOpen()) {
      g_fileInfo->Close();
    }
    if ((fileMask & FileUser) && g_fileUserLog->IsOpen()) {
      g_fileUserLog->Close();
    }
    if ((fileMask & FileTime) && g_fileTimeLog->IsOpen()) {
      g_fileTimeLog->Close();
    }
  }
}

int RageLog::LogWriter::ThreadMain() {
  RageThreadRegister registerThread(ragelog_thread_name);

  bool wroteSinceFlush = false;
  RageTimer nextFlush;
  nextFlush.SetZero();

  while (true) {
    std::deque<LogCommand> local;
    bool timedOut = false;
    m_Event.Lock();
    while (true) {
      if (!m_Queue.empty()) {
        break;
      }
      if (m_bRequestShutdown) {
        break;
      }

      if (wroteSinceFlush) {
        if (nextFlush.IsZero()) {
          nextFlush.Touch();
          nextFlush += FLUSH_INTERVAL_SEC;
        }
        if (!m_Event.Wait(&nextFlush)) {
          timedOut = true;
          break;
        }
      } else {
        m_Event.Wait();
      }
    }

    local.swap(m_Queue);
    m_Event.Unlock();

    if (m_bRequestShutdown && local.empty()) {
      break;
    }

    std::string logBuf, infoBuf, userBuf, timeBuf;
    int batchBytes = 0;
    uint64_t completeToken = 0;
    bool doFlushNow = timedOut;
    bool setNextFlush = false;

    for (LogWriter::LogCommand& cmd : local) {
      switch (cmd.type) {
        case RAGELOG_WRITE_LINE:
          if (cmd.fileMask & FileLog) {
            AppendLine(logBuf, cmd.line);
            batchBytes += cmd.line.size() + 1;
          }
          if (cmd.fileMask & FileInfo) {
            AppendLine(infoBuf, cmd.line);
            batchBytes += cmd.line.size() + 1;
          }
          if (cmd.fileMask & FileUser) {
            AppendLine(userBuf, cmd.line);
            batchBytes += cmd.line.size() + 1;
          }
          if (cmd.fileMask & FileTime) {
            AppendLine(timeBuf, cmd.line);
            batchBytes += cmd.line.size() + 1;
          }
          wroteSinceFlush = true;
          setNextFlush = true;
          break;
        case RAGELOG_ENABLE:
          SetEnabledInternal(cmd.fileMask, cmd.enable);
          completeToken = std::max<uint64_t>(completeToken, cmd.token);
          break;
        case RAGELOG_REQUEST_FLUSH:
          m_bWantFlushSoon = true;
          break;
        case RAGELOG_FLUSH_AND_WAIT:
          doFlushNow = true;
          completeToken = std::max<uint64_t>(completeToken, cmd.token);
          break;
        case RAGELOG_HALT:
          m_bRequestShutdown = true;
          doFlushNow = true;
          completeToken = std::max<uint64_t>(completeToken, cmd.token);
          break;
      }

      if (batchBytes >= MAX_BATCH_BYTES) {
        doFlushNow = true;
      }
    }

    if ((m_iEnabledMask & FileLog) && g_fileLog->IsOpen() && !logBuf.empty()) {
      g_fileLog->Write(logBuf);
    }
    if ((m_iEnabledMask & FileInfo) && g_fileInfo->IsOpen() &&
        !infoBuf.empty()) {
      g_fileInfo->Write(infoBuf);
    }
    if ((m_iEnabledMask & FileUser) && g_fileUserLog->IsOpen() &&
        !userBuf.empty()) {
      g_fileUserLog->Write(userBuf);
    }
    if ((m_iEnabledMask & FileTime) && g_fileTimeLog->IsOpen() &&
        !timeBuf.empty()) {
      g_fileTimeLog->Write(timeBuf);
    }

    if (m_bWantFlushSoon) {
      doFlushNow = true;
      m_bWantFlushSoon = false;
    }

    if (doFlushNow && wroteSinceFlush) {
      FlushOpenFiles();
      wroteSinceFlush = false;
      nextFlush.SetZero();
    } else if (setNextFlush) {
      nextFlush.SetZero();
    }

    if (completeToken != 0) {
      m_Event.Lock();
      CompleteTokenLocked(completeToken);
      m_Event.Unlock();
    }
  }

  FlushOpenFiles();
  return 0;
}

#define NEWLINE "\n"

static char staticlog[1024 * 32] = "";
static unsigned staticlog_size = 0;
void RageLog::AddToInfo(const std::string& str) {
  static bool limit_reached = false;
  if (limit_reached) {
    return;
  }

  unsigned len = str.size() + strlen(NEWLINE);
  if (staticlog_size + len > sizeof(staticlog)) {
    const std::string txt(NEWLINE "Staticlog limit reached" NEWLINE);

    const unsigned int pos =
        std::min<unsigned int>(staticlog_size, sizeof(staticlog) - txt.size());
    memcpy(staticlog + pos, txt.data(), txt.size());
    limit_reached = true;
    return;
  }

  memcpy(staticlog + staticlog_size, str.data(), str.size());
  staticlog_size += str.size();
  memcpy(staticlog + staticlog_size, NEWLINE, strlen(NEWLINE));
  staticlog_size += strlen(NEWLINE);
}

const char* RageLog::GetInfo() {
  staticlog[sizeof(staticlog) - 1] = 0;
  return staticlog;
}

static const int BACKLOG_LINES = 10;
static char backlog[BACKLOG_LINES][1024];
static int backlog_start = 0, backlog_cnt = 0;
void RageLog::AddToRecentLogs(const std::string& str) {
  unsigned len = str.size();
  if (len > sizeof(backlog[backlog_start]) - 1) {
    len = sizeof(backlog[backlog_start]) - 1;
  }

  strncpy(backlog[backlog_start], str.c_str(), len);
  backlog[backlog_start][len] = 0;

  backlog_start++;
  if (backlog_start > backlog_cnt) {
    backlog_cnt = backlog_start;
  }
  backlog_start %= BACKLOG_LINES;
}

const char* RageLog::GetRecentLog(int n) {
  if (n >= BACKLOG_LINES || n >= backlog_cnt) {
    return nullptr;
  }

  if (backlog_cnt == BACKLOG_LINES) {
    n += backlog_start;
    n %= BACKLOG_LINES;
  }
  /* Make sure it's terminated: */
  backlog[n][sizeof(backlog[n]) - 1] = 0;

  return backlog[n];
}

static char g_AdditionalLogStr[10240] = "";
static int g_AdditionalLogSize = 0;

void RageLog::UpdateMappedLog() {
  std::string str;
  for (const auto& i : LogMaps) {
    str += ssprintf("%s" NEWLINE, i.second.c_str());
  }

  g_AdditionalLogSize = std::min(sizeof(g_AdditionalLogStr), str.size() + 1);
  memcpy(g_AdditionalLogStr, str.c_str(), g_AdditionalLogSize);
  g_AdditionalLogStr[sizeof(g_AdditionalLogStr) - 1] = 0;
}

const char* RageLog::GetAdditionalLog() {
  int size = std::min(g_AdditionalLogSize, (int)sizeof(g_AdditionalLogStr) - 1);
  g_AdditionalLogStr[size] = 0;
  return g_AdditionalLogStr;
}

void RageLog::MapLog(const std::string& key, const char* fmt, ...) {
  LockMut(*g_Mutex);
  std::string s;

  va_list va;
  va_start(va, fmt);
  s += vssprintf(fmt, va);
  va_end(va);

  LogMaps[key] = s;
  UpdateMappedLog();
}

void RageLog::UnmapLog(const std::string& key) {
  LockMut(*g_Mutex);
  LogMaps.erase(key);
  UpdateMappedLog();
}

void ShowWarningOrTrace(
    const char* file, int line, const std::string& message, bool bWarning) {
  ShowWarningOrTrace(file, line, message.c_str(), bWarning);
}

void ShowWarningOrTrace(
    const char* file, int line, const char* message, bool bWarning) {
  /* Ignore everything up to and including the first "src/". */
  const char* temp = strstr(file, "src/");
  if (temp) {
    file = temp + 4;
  }

  void (RageLog::*method)(const char* fmt, ...) =
      bWarning ? &RageLog::Warn : &RageLog::Trace;

  if (LOG) {
    (LOG->*method)("%s:%i: %s", file, line, message);
  } else {
    fprintf(stderr, "%s:%i: %s\n", file, line, message);
  }
}

/*
 * Copyright (c) 2001-2004 Chris Danford, Glenn Maynard
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
