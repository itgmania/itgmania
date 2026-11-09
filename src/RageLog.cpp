#include "RageLog.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
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
static std::map<std::string, std::string> log_maps;

#define LOG_PATH "/Logs/log.txt"
#define INFO_PATH "/Logs/info.txt"
#define USER_PATH "/Logs/userlog.txt"

static RageFile *file_log, *file_info, *file_user_log;

/* Mutex writes to the files.  Writing to files is not thread-aware, and this is
 * the only place we write to the same file from multiple threads. */
static RageMutex* mutex;

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
  WRITE_LOUD = 0x04
};

RageLog::RageLog()
    : log_to_disk_(false),
      info_to_disk_(false),
      user_log_to_disk_(false),
      flush_(false),
      show_log_output_(false) {
  file_log = new RageFile;
  file_info = new RageFile;
  file_user_log = new RageFile;

  mutex = new RageMutex("Log");
}

RageLog::~RageLog() {
  /* Add the mapped log data to info.txt. */
  const std::string additional_log = GetAdditionalLog();
  std::vector<std::string> additional_log_lines;
  split(additional_log, "\n", additional_log_lines);
  for (unsigned i = 0; i < additional_log_lines.size(); ++i) {
    Trim(additional_log_lines[i]);
    this->Info("%s", additional_log_lines[i].c_str());
  }

  Flush();
  SetShowLogOutput(false);
  file_log->Close();
  file_info->Close();
  file_user_log->Close();

  RageUtil::SafeDelete(mutex);
  RageUtil::SafeDelete(file_log);
  RageUtil::SafeDelete(file_info);
  RageUtil::SafeDelete(file_user_log);
}

void RageLog::SetLogToDisk(bool b) {
  if (log_to_disk_ == b) {
    return;
  }

  log_to_disk_ = b;

  if (!log_to_disk_) {
    if (file_log->IsOpen()) {
      file_log->Close();
    }
    return;
  }

  if (!file_log->Open(LOG_PATH, RageFile::WRITE | RageFile::STREAMED)) {
    fprintf(
        stderr, "Couldn't open %s: %s\n", LOG_PATH,
        file_log->GetError().c_str());
  }
}

void RageLog::SetInfoToDisk(bool b) {
  if (info_to_disk_ == b) {
    return;
  }

  info_to_disk_ = b;

  if (!info_to_disk_) {
    if (file_info->IsOpen()) {
      file_info->Close();
    }
    return;
  }

  if (!file_info->Open(INFO_PATH, RageFile::WRITE | RageFile::STREAMED)) {
    fprintf(
        stderr, "Couldn't open %s: %s\n", INFO_PATH,
        file_info->GetError().c_str());
  }
}

void RageLog::SetUserLogToDisk(bool b) {
  if (user_log_to_disk_ == b) {
    return;
  }

  user_log_to_disk_ = b;

  if (!user_log_to_disk_) {
    if (file_user_log->IsOpen()) {
      file_user_log->Close();
    }
    return;
  }
  if (!file_user_log->Open(USER_PATH, RageFile::WRITE | RageFile::STREAMED)) {
    fprintf(
        stderr, "Couldn't open %s: %s\n", USER_PATH,
        file_user_log->GetError().c_str());
  }
}

void RageLog::SetFlushing(bool b) { flush_ = b; }

/* Enable or disable display of output to stdout, or a console window in
 * Windows. */
void RageLog::SetShowLogOutput(bool show) {
  show_log_output_ = show;

#if defined(_WIN32)
  if (show_log_output_) {
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
  std::string buff = vssprintf(fmt, va);
  va_end(va);

  Write(0, buff);
}

/* Use this for more important information; it'll always be included
 * in crash dumps. */
void RageLog::Info(const char* fmt, ...) {
  va_list va;
  va_start(va, fmt);
  std::string buff = vssprintf(fmt, va);
  va_end(va);

  Write(WRITE_TO_INFO, buff);
}

void RageLog::Warn(const char* fmt, ...) {
  va_list va;
  va_start(va, fmt);
  std::string buff = vssprintf(fmt, va);
  va_end(va);

  Write(WRITE_TO_INFO | WRITE_LOUD, buff);
}

void RageLog::UserLog(
    const std::string& type, const std::string& element, const char* fmt, ...) {
  va_list va;
  va_start(va, fmt);
  std::string buf = vssprintf(fmt, va);
  va_end(va);

  if (!type.empty()) {
    buf = ssprintf("%s \"%s\" %s", type.c_str(), element.c_str(), buf.c_str());
  }

  Write(WRITE_TO_USER_LOG, buf);
}

void RageLog::Write(int where, const std::string& line) {
  LockMut(*mutex);

  const char* const warning_separator =
      "/////////////////////////////////////////";
  std::vector<std::string> lines;
  split(line, "\n", lines, false);
  if (where & WRITE_LOUD) {
    if (log_to_disk_ && file_log->IsOpen()) {
      file_log->PutLine(warning_separator);
    }
    puts(warning_separator);
  }

  std::string timestamp =
      MicrosecondsToMMSSMsMsMs(RageTimer::GetTimeSinceStartMicroseconds()) +
      ": ";
  std::string warning;
  if (where & WRITE_LOUD) {
    warning = "WARNING: ";
  }

  for (unsigned i = 0; i < lines.size(); ++i) {
    std::string& str = lines[i];

    if (warning.size()) {
      str.insert(0, warning);
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
    if (show_log_output_ || (where & WRITE_TO_INFO)) {
      puts(str.c_str());  // fputws( (const wchar_t *)str.c_str(), stdout );
    }
    if (where & WRITE_TO_INFO) {
      AddToInfo(str);
    }
#else
    if (where & WRITE_TO_INFO) {
      puts(str.c_str());
      AddToInfo(str);
    }
#endif

    if (log_to_disk_ && (where & WRITE_TO_INFO) && file_info->IsOpen()) {
      file_info->PutLine(str);
    }
    if (user_log_to_disk_ && (where & WRITE_TO_USER_LOG) &&
        file_user_log->IsOpen()) {
      file_user_log->PutLine(str);
    }

    /* Add a timestamp to log.txt and RecentLogs, but not the rest of info.txt
     * and stdout. */
    str.insert(0, timestamp);

    AddToRecentLogs(str);

    if (log_to_disk_ && file_log->IsOpen()) {
      file_log->PutLine(str);
    }
  }

  if (where & WRITE_LOUD) {
    if (log_to_disk_ && file_log->IsOpen() && (where & WRITE_LOUD)) {
      file_log->PutLine(warning_separator);
    }
    puts(warning_separator);
  }
  if (flush_ || (where & WRITE_TO_INFO)) {
    Flush();
  }
}

void RageLog::Flush() {
  file_log->Flush();
  file_info->Flush();
  file_user_log->Flush();
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

static const int kBacklogLines = 10;
static char backlog[kBacklogLines][1024];
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
  backlog_start %= kBacklogLines;
}

const char* RageLog::GetRecentLog(int n) {
  if (n >= kBacklogLines || n >= backlog_cnt) {
    return nullptr;
  }

  if (backlog_cnt == kBacklogLines) {
    n += backlog_start;
    n %= kBacklogLines;
  }
  /* Make sure it's terminated: */
  backlog[n][sizeof(backlog[n]) - 1] = 0;

  return backlog[n];
}

static char additional_log_str[10240] = "";
static int additional_log_size = 0;

void RageLog::UpdateMappedLog() {
  std::string str;
  for (const auto& i : log_maps) {
    str += ssprintf("%s" NEWLINE, i.second.c_str());
  }

  additional_log_size = std::min(sizeof(additional_log_str), str.size() + 1);
  memcpy(additional_log_str, str.c_str(), additional_log_size);
  additional_log_str[sizeof(additional_log_str) - 1] = 0;
}

const char* RageLog::GetAdditionalLog() {
  int size = std::min(additional_log_size, (int)sizeof(additional_log_str) - 1);
  additional_log_str[size] = 0;
  return additional_log_str;
}

void RageLog::MapLog(const std::string& key, const char* fmt, ...) {
  std::string s;

  va_list va;
  va_start(va, fmt);
  s += vssprintf(fmt, va);
  va_end(va);

  log_maps[key] = s;
  UpdateMappedLog();
}

void RageLog::UnmapLog(const std::string& key) {
  log_maps.erase(key);
  UpdateMappedLog();
}

void ShowWarningOrTrace(
    const char* file, int line, const std::string& message, bool warning) {
  ShowWarningOrTrace(file, line, message.c_str(), warning);
}

void ShowWarningOrTrace(
    const char* file, int line, const char* message, bool warning) {
  /* Ignore everything up to and including the first "src/". */
  const char* temp = strstr(file, "src/");
  if (temp) {
    file = temp + 4;
  }

  void (RageLog::*method)(const char* fmt, ...) =
      warning ? &RageLog::Warn : &RageLog::Trace;

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
