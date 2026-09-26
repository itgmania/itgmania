#include "ArchHooks_Unix.h"

#include <sys/wait.h>
#include <unistd.h>

#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <string>

#include "RageFileManager.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "archutils/Unix/AssertionHandler.h"
#include "archutils/Unix/CrashHandler.h"
#include "archutils/Unix/EmergencyShutdown.h"
#include "archutils/Unix/GetSysInfo.h"
#include "archutils/Unix/SignalHandler.h"
#if defined(LINUX)
#include <limits.h>
#endif

extern "C" {
#include <libavcodec/avcodec.h>
}

static bool IsFatalSignal(int signal) {
  switch (signal) {
    case SIGINT:
    case SIGTERM:
    case SIGHUP:
      return false;
    default:
      return true;
  }
}

static bool DoCleanShutdown(int signal, siginfo_t* si, const ucontext_t* uc) {
  if (IsFatalSignal(signal)) {
    return false;
  }

  /* ^C. */
  ArchHooks::SetUserQuit();
  return true;
}

static bool DoCrashSignalHandler(
    int signal, siginfo_t* si, const ucontext_t* uc) {
  /* Don't dump a debug file if the user just hit ^C. */
  if (!IsFatalSignal(signal)) {
    return true;
  }

  CrashHandler::CrashSignalHandler(signal, si, uc);
  return false;
}

static bool EmergencyShutdown(int signal, siginfo_t* si, const ucontext_t* uc) {
  if (!IsFatalSignal(signal)) {
    return false;
  }

  DoEmergencyShutdown();

  /* If we ran the crash handler, then die. */
  kill(getpid(), SIGKILL);

  /* We didn't run the crash handler.  Run the default handler, so we can dump
   * core. */
  return false;
}

#if 1
/* If librt is available, use CLOCK_MONOTONIC to implement
 * GetSystemTimeInMicroseconds, if supported, so changes to the system clock
 * don't cause problems. */
namespace {
clockid_t g_Clock = CLOCK_REALTIME;
void OpenGetTime() {
  static bool bInitialized = false;

  if (bInitialized) {
    return;
  }
  bInitialized = true;

  /* Check whether CLOCK_MONOTONIC is supported.
   * If it isn't available, use CLOCK_REALTIME.*/
  timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1) {
    return;
  }

  g_Clock = CLOCK_MONOTONIC;
}
};  // namespace

clockid_t ArchHooks_Unix::GetClock() {
  OpenGetTime();
  return g_Clock;
}

int64_t ArchHooks::GetSystemTimeInMicroseconds() {
  OpenGetTime();

  timespec ts;
  clock_gettime(g_Clock, &ts);

  int64_t iRet = int64_t(ts.tv_sec) * 1000000 + int64_t(ts.tv_nsec) / 1000;
  if (g_Clock != CLOCK_MONOTONIC) {
    iRet = ArchHooks::FixupTimeIfBackwards(iRet);
  }
  return iRet;
}
#else
int64_t ArchHooks::GetSystemTimeInMicroseconds() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);

  int64_t iRet = int64_t(tv.tv_sec) * 1000000 + int64_t(tv.tv_usec);
  ret = FixupTimeIfBackwards(ret);
  return iRet;
}
#endif

std::string ArchHooks::GetPreferredLanguage() {
  std::string locale;

  if (getenv("LANG")) {
    locale = getenv("LANG");
    std::string region = locale.substr(3, 2);
    locale = locale.substr(0, 2);

    if (locale == "zh") {
      if (region == "CN" || region == "SG") {
        locale = "zh-Hans";
      } else if (region == "HK" || region == "TW") {
        locale = "zh-Hant";
      }
    }
  } else {
    LOG->Warn("Unable to determine system language. Using English.");
    locale = "en";
  }

  return locale;
}

void ArchHooks_Unix::Init() {
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__) || \
    defined(__SANITIZE_UNDEFINED__) || defined(__SANITIZE_MEMORY__)
  return;
#endif
  if (getenv("ITGMANIA_DISABLE_SIGNAL_HANDLER")) {
    return;
  }

  /* First, handle non-fatal termination signals. */
  SignalHandler::OnClose(DoCleanShutdown);

  CrashHandler::CrashHandlerHandleArgs(g_argc, g_argv);
  CrashHandler::InitializeCrashHandler();
  SignalHandler::OnClose(DoCrashSignalHandler);

  /* Set up EmergencyShutdown, to try to shut down the window if we crash.
   * This might blow up, so be sure to do it after the crash handler. */
  SignalHandler::OnClose(EmergencyShutdown);

  InstallExceptionHandler();
}

#ifndef _CS_GNU_LIBC_VERSION
#define _CS_GNU_LIBC_VERSION 2
#endif

static std::string LibcVersion() {
  char buf[1024] = "(error)";
  int ret = confstr(_CS_GNU_LIBC_VERSION, buf, sizeof(buf));
  if (ret == -1) {
    return "(unknown)";
  }

  return buf;
}

void ArchHooks_Unix::DumpDebugInfo() {
  std::string sys;
  int vers;
  GetKernel(sys, vers);
  LOG->Info("OS: %s ver %06i", sys.c_str(), vers);

  LOG->Info("Crash backtrace component: %s", BACKTRACE_METHOD_TEXT);
  LOG->Info("Crash lookup component: %s", BACKTRACE_LOOKUP_METHOD_TEXT);
#if defined(BACKTRACE_DEMANGLE_METHOD_TEXT)
  LOG->Info("Crash demangle component: %s", BACKTRACE_DEMANGLE_METHOD_TEXT);
#endif

  LOG->Info("Runtime library: %s", LibcVersion().c_str());
  LOG->Info("libavcodec: %#x (%u)", avcodec_version(), avcodec_version());
}

void ArchHooks_Unix::SetTime(tm newtime) {
  std::string sCommand = ssprintf(
      "date %02d%02d%02d%02d%04d.%02d", newtime.tm_mon + 1, newtime.tm_mday,
      newtime.tm_hour, newtime.tm_min, newtime.tm_year + 1900, newtime.tm_sec);

  LOG->Trace("executing '%s'", sCommand.c_str());
  int ret = system(sCommand.c_str());
  if (ret == -1 || ret == 127 || !WIFEXITED(ret) || WEXITSTATUS(ret)) {
    LOG->Trace("'%s' failed", sCommand.c_str());
  }

  ret = system("hwclock --systohc");
  if (ret == -1 || ret == 127 || !WIFEXITED(ret) || WEXITSTATUS(ret)) {
    LOG->Trace("'hwclock --systohc' failed");
  }
}

std::string ArchHooks_Unix::GetClipboard() {
  LOG->Warn(
      "ArchHooks_Unix: GetClipboard(): Compiled without any supported "
      "clipboard source!");
  return "";
}

void ArchHooks::MountInitialFilesystems(const std::string& sDirOfExecutable) {
  FILEMAN->Mount("dirro", sDirOfExecutable, "/");

  bool portable = DoesFileExist("/Portable.ini");
  if (portable) {
    FILEMAN->Mount("dir", sDirOfExecutable + "/Announcers", "/Announcers");
    FILEMAN->Mount("dir", sDirOfExecutable + "/BGAnimations", "/BGAnimations");
    FILEMAN->Mount(
        "dir", sDirOfExecutable + "/BackgroundEffects", "/BackgroundEffects");
    FILEMAN->Mount(
        "dir", sDirOfExecutable + "/BackgroundTransitions",
        "/BackgroundTransitions");
    FILEMAN->Mount("dir", sDirOfExecutable + "/Cache", "/Cache");
    FILEMAN->Mount("dir", sDirOfExecutable + "/CDTitles", "/CDTitles");
    FILEMAN->Mount("dir", sDirOfExecutable + "/Characters", "/Characters");
    FILEMAN->Mount("dir", sDirOfExecutable + "/Courses", "/Courses");
    FILEMAN->Mount("dir", sDirOfExecutable + "/Downloads", "/Downloads");
    FILEMAN->Mount("dir", sDirOfExecutable + "/Logs", "/Logs");
    FILEMAN->Mount("dir", sDirOfExecutable + "/NoteSkins", "/NoteSkins");
    FILEMAN->Mount("dir", sDirOfExecutable + "/Packages", "/Packages");
    FILEMAN->Mount("dir", sDirOfExecutable + "/Save", "/Save");
    FILEMAN->Mount("dir", sDirOfExecutable + "/Screenshots", "/Screenshots");
    FILEMAN->Mount("dir", sDirOfExecutable + "/Songs", "/Songs");
    FILEMAN->Mount("dir", sDirOfExecutable + "/RandomMovies", "/RandomMovies");
    FILEMAN->Mount("dir", sDirOfExecutable + "/Themes", "/Themes");
  }
}

void ArchHooks::MountUserFilesystems(const std::string& sDirOfExecutable) {
  /* Path to write general mutable user data when not Portable
   * Lowercase the PRODUCT_ID; dotfiles and directories are almost always
   * lowercase.
   */
  const char* szHome = getenv("HOME");
  std::string sUserDataPath =
      ssprintf("%s/.%s", szHome ? szHome : ".", "itgmania");
  FILEMAN->Mount("dir", sUserDataPath + "/Announcers", "/Announcers");
  FILEMAN->Mount("dir", sUserDataPath + "/BGAnimations", "/BGAnimations");
  FILEMAN->Mount(
      "dir", sUserDataPath + "/BackgroundEffects", "/BackgroundEffects");
  FILEMAN->Mount(
      "dir", sUserDataPath + "/BackgroundTransitions",
      "/BackgroundTransitions");
  FILEMAN->Mount("dir", sUserDataPath + "/Cache", "/Cache");
  FILEMAN->Mount("dir", sUserDataPath + "/CDTitles", "/CDTitles");
  FILEMAN->Mount("dir", sUserDataPath + "/Characters", "/Characters");
  FILEMAN->Mount("dir", sUserDataPath + "/Courses", "/Courses");
  FILEMAN->Mount("dir", sUserDataPath + "/Downloads", "/Downloads");
  FILEMAN->Mount("dir", sUserDataPath + "/Logs", "/Logs");
  FILEMAN->Mount("dir", sUserDataPath + "/NoteSkins", "/NoteSkins");
  FILEMAN->Mount("dir", sUserDataPath + "/Packages", "/Packages");
  FILEMAN->Mount("dir", sUserDataPath + "/Save", "/Save");
  FILEMAN->Mount("dir", sUserDataPath + "/Screenshots", "/Screenshots");
  FILEMAN->Mount("dir", sUserDataPath + "/Songs", "/Songs");
  FILEMAN->Mount("dir", sUserDataPath + "/RandomMovies", "/RandomMovies");
  FILEMAN->Mount("dir", sUserDataPath + "/Themes", "/Themes");
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
