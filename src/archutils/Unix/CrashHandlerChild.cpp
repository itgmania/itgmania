#include <sys/signal.h>
#include <unistd.h>

#include <csignal>
#include <cstdlib>

#include "Backtrace.h"
#include "BacktraceNames.h"
#include "CrashHandler.h"
#include "CrashHandlerInternal.h"
#include "ProductInfo.h"
#include "RageUtil.h"
#include "arch/ArchHooks/ArchHooks.h"

#if defined(MACOSX)
#include "archutils/Darwin/Crash.h"
#endif

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "ver.h"

bool child_read(int fd, void* p, int size);

const char* g_pCrashHandlerArgv0 = nullptr;

static void output_stack_trace(FILE* out, const void** backtrace_pointers) {
  if (backtrace_pointers[0] == BACKTRACE_METHOD_NOT_AVAILABLE) {
    fprintf(out, "No backtrace method available.\n");
    return;
  }

  if (!backtrace_pointers[0]) {
    fprintf(out, "Backtrace was empty.\n");
    return;
  }

  for (int i = 0; backtrace_pointers[i]; ++i) {
    BacktraceNames bn;
    bn.FromAddr(const_cast<void*>(backtrace_pointers[i]));
    bn.Demangle();

    /* Don't show the main module name. */
    if (bn.file_ == g_pCrashHandlerArgv0 && !bn.symbol_.empty()) {
      bn.file_ = "";
    }

    if (bn.symbol_ == "__libc_start_main") {
      break;
    }

    fprintf(out, "%s\n", bn.Format().c_str());
  }
}

bool child_read(int fd, void* p, int size) {
  char* buf = (char*)p;
  int got = 0;
  while (got < size) {
    int ret = read(fd, buf + got, size - got);
    if (ret == -1) {
      if (errno == EINTR) {
        continue;
      }
      fprintf(
          stderr, "Crash handler: error communicating with parent: %s\n",
          strerror(errno));
      return false;
    }

    if (ret == 0) {
      fprintf(stderr, "Crash handler: EOF communicating with parent.\n");
      return false;
    }

    got += ret;
  }

  return true;
}

/* Once we get here, we should be * safe to do whatever we want;
 * heavyweights like malloc and std::string are OK. (Don't crash!) */
static void child_process() {
  /* 1. Read the CrashData. */
  CrashData crash;
  if (!child_read(3, &crash, sizeof(CrashData))) {
    return;
  }

  /* 2. Read info. */
  int size;
  if (!child_read(3, &size, sizeof(size))) {
    return;
  }
  char* info = new char[size];
  if (!child_read(3, info, size)) {
    return;
  }

  /* 3. Read AdditionalLog. */
  if (!child_read(3, &size, sizeof(size))) {
    return;
  }

  char* additional_log = new char[size];
  if (!child_read(3, additional_log, size)) {
    return;
  }

  /* 4. Read RecentLogs. */
  int cnt = 0;
  if (!child_read(3, &cnt, sizeof(cnt))) {
    return;
  }
  char* recent[1024];
  for (int i = 0; i < cnt; ++i) {
    if (!child_read(3, &size, sizeof(size))) {
      return;
    }
    recent[i] = new char[size];
    if (!child_read(3, recent[i], size)) {
      return;
    }
  }

  /* 5. Read CHECKPOINTs. */
  if (!child_read(3, &size, sizeof(size))) {
    return;
  }

  char* temp = new char[size];
  if (!child_read(3, temp, size)) {
    return;
  }

  std::vector<std::string> checkpoints;
  split(temp, "$$", checkpoints);
  delete[] temp;

  /* 6. Read the crashed thread's name. */
  if (!child_read(3, &size, sizeof(size))) {
    return;
  }
  temp = new char[size];
  if (!child_read(3, temp, size)) {
    return;
  }
  const std::string crashed_thread(temp);
  delete[] temp;

  /* Wait for the child to either finish cleaning up or die. */
  fd_set rs;
  struct timeval timeout = {5, 0};  // 5 seconds

  FD_ZERO(&rs);
  FD_SET(3, &rs);
  int ret = select(4, &rs, nullptr, nullptr, &timeout);

  if (ret == 0) {
    fputs("Timeout exceeded.\n", stderr);
  } else if ((ret == -1 && errno != EPIPE) || ret != 1) {
    fprintf(
        stderr, "Unexpected return from select() result: %d (%s)\n", ret,
        strerror(errno));
    // Keep going.
  } else {
    char x;

    // No need to check FD_ISSET( 3, &rs ) because it was the only descriptor in
    // the set.
    ret = read(3, &x, sizeof(x));
    if (ret > 0) {
      fprintf(stderr, "Unexpected child read() result: %i\n", ret);
      /* keep going */
    } else if ((ret == -1 && errno != EPIPE) || ret != 0) {
      /* We expect an EOF or EPIPE.  What happened? */
      fprintf(
          stderr, "Unexpected child read() result: %i (%s)\n", ret,
          strerror(errno));
      /* keep going */
    }
  }

  std::string crash_info_path = "/tmp";
#if defined(MACOSX)
  crash_info_path = CrashHandler::GetLogsDirectory();
#else
  const char* home = getenv("HOME");
  if (home) {
    crash_info_path = home;
  }
#endif
  crash_info_path += "/crashinfo.txt";

  FILE* crash_dump = fopen(crash_info_path.c_str(), "w+");
  if (crash_dump == nullptr) {
    fprintf(
        stderr, "Couldn't open %s: %s\n", crash_info_path.c_str(),
        strerror(errno));
    exit(1);
  }

  fprintf(crash_dump, "%s%s crash report", PRODUCT_FAMILY, product_version);
  fprintf(
      crash_dump, " (build %s, %s @ %s)", ::sm_version_git_hash, version_date,
      version_time);
  fprintf(crash_dump, "\n");
  fprintf(crash_dump, "--------------------------------------\n");
  fprintf(crash_dump, "\n");

  std::string reason;
  switch (crash.type_) {
    case CrashData::SIGNAL: {
      reason = ssprintf(
          "%s - %s", SignalName(crash.signal_),
          SignalCodeName(crash.signal_, crash.si_.si_code));

      /* Linux puts the PID that sent the signal in si_addr for SI_USER. */
      if (crash.si_.si_code == SI_USER) {
        reason += ssprintf(" from pid %li", (long)crash.si_.si_addr);
      } else {
        switch (crash.signal_) {
          case SIGILL:
          case SIGFPE:
          case SIGSEGV:
          case SIGBUS:
            reason += ssprintf(
                " at 0x%0*lx", int(sizeof(void*) * 2),
                (unsigned long)crash.si_.si_addr);
        }
        break;
      }
      [[fallthrough]];
    }
    case CrashData::FORCE_CRASH:
      crash.reason_[sizeof(crash.reason_) - 1] = 0;
      reason = crash.reason_;
      break;
  }

  fprintf(crash_dump, "Architecture:   %s\n", HOOKS->GetArchName().c_str());
  fprintf(crash_dump, "Crash reason:   %s\n", reason.c_str());
  fprintf(crash_dump, "Crashed thread: %s\n\n", crashed_thread.c_str());

  fprintf(crash_dump, "Checkpoints:\n");
  for (unsigned i = 0; i < checkpoints.size(); ++i) {
    fputs(checkpoints[i].c_str(), crash_dump);
  }
  fprintf(crash_dump, "\n");

  for (int i = 0; i < CrashData::MAX_BACKTRACE_THREADS; ++i) {
    if (!crash.backtrace_pointers_[i][0]) {
      break;
    }
    fprintf(crash_dump, "Thread: %s\n", crash.thread_name_[i]);
    output_stack_trace(crash_dump, crash.backtrace_pointers_[i]);
    fprintf(crash_dump, "\n");
  }

  fprintf(crash_dump, "Static log:\n");
  fprintf(crash_dump, "%s", info);
  fprintf(crash_dump, "%s", additional_log);
  fprintf(crash_dump, "\nPartial log:\n");
  for (int i = 0; i < cnt; ++i) {
    fprintf(crash_dump, "%s\n", recent[i]);
  }
  fprintf(crash_dump, "\n");
  fprintf(crash_dump, "-- End of report\n");
  fclose(crash_dump);

#if defined(MACOSX)
  CrashHandler::InformUserOfCrash(crash_info_path);
#else
  /* stdout may have been inadvertently closed by the crash in the parent;
   * write to /dev/tty instead. */
  FILE* tty = fopen("/dev/tty", "w");
  if (tty == nullptr) {
    tty = stderr;
  }

  fputs(
      ("\n" PRODUCT_ID " has crashed.  Debug information has been output to\n"
       "\n"
       "    " +
       crash_info_path +
       "\n"
       "\n"
       "Please report a bug at:\n"
       "\n"
       "    " REPORT_BUG_URL "\n"
       "\n")
          .c_str(),
      tty);
#endif
}

void CrashHandler::CrashHandlerHandleArgs(int argc, char* argv[]) {
  g_pCrashHandlerArgv0 = argv[0];
  if (argc == 2 && !strcmp(argv[1], CHILD_MAGIC_PARAMETER)) {
    child_process();
    exit(0);
  }
}

/*
 * (c) 2003-2004 Glenn Maynard
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
