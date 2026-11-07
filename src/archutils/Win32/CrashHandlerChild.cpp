// clang-format off
#include <windows.h>
#include <commctrl.h>
#include <io.h>
// clang-format on

#include <fcntl.h>
#include <shellapi.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "Crash.h"
#include "CrashHandlerInternal.h"
#include "LocalizedString.h"
#include "ProductInfo.h"
#include "RageFileDriverDeflate.h"
#include "RageUtil.h"
#include "arch/ArchHooks/ArchHooks.h"
#include "archutils/Win32/DialogUtil.h"
#include "archutils/Win32/ErrorStrings.h"
#include "archutils/Win32/RestartProgram.h"
#include "archutils/Win32/SpecialDirs.h"
#include "archutils/Win32/WindowsDialogBox.h"
#include "archutils/Win32/WindowsResources.h"
#include "dbghelp.h"
#include "global.h"
#include "ver.h"

#pragma comment(lib, "dbghelp.lib")

// XXX: What happens when we *don't* have version info? Does that ever actually
// happen?
#include "ver.h"

#if _WIN64
#define ADDRESS_ZEROS "016"
#else
#define ADDRESS_ZEROS "08"
#endif

// VDI symbol lookup:
namespace VDDebugInfo {
struct Context {
  Context() { rva_heap_ = nullptr; }
  bool Loaded() const { return rva_heap_ != nullptr; }
  std::string raw_block_;

  const unsigned char* rva_heap_;
  uintptr_t first_rva_;

  const char* func_name_heap_;
  const uintptr_t (*p_segments_)[2];
  int n_segments_;
  char filename_[1024];
  std::string error_;
};

static void GetVDIPath(char* buf, int bufsiz) {
  GetModuleFileName(nullptr, buf, bufsiz);
  buf[bufsiz - 5] = 0;
  char* p = strrchr(buf, '.');
  if (p) {
    strcpy(p, ".vdi");
  } else {
    strcat(buf, ".vdi");
  }
}

bool VDDebugInfoInitFromMemory(Context* pctx) {
  if (pctx->raw_block_[0] == '\x1f' && pctx->raw_block_[1] == '\x8b') {
    std::string buf_out;
    std::string error;
    if (!GunzipString(pctx->raw_block_, buf_out, error)) {
      pctx->error_ =
          werr_ssprintf(GetLastError(), "VDI error: %s", error.c_str());
      return false;
    }

    pctx->raw_block_ = buf_out;
  }

  const unsigned char* src = (const unsigned char*)pctx->raw_block_.data();

  pctx->rva_heap_ = nullptr;

  static const char* kHeader = "symbolic debug information";
  if (memcmp(src, kHeader, strlen(kHeader))) {
    pctx->error_ = "header doesn't match";
    return false;
  }

  // Extract fields

  src += 64;
  const size_t* rva_size = reinterpret_cast<const size_t*>(src + sizeof(int));
  const size_t* f_nam_size =
      reinterpret_cast<const size_t*>(src + sizeof(int) + sizeof(size_t));
  const int* seg_cnt =
      reinterpret_cast<const int*>(src + sizeof(int) + 2 * sizeof(size_t));
  src += 2 * (sizeof(int) + sizeof(size_t));

  pctx->rva_heap_ =
      reinterpret_cast<const unsigned char*>(src + sizeof(uintptr_t));
  pctx->first_rva_ = *reinterpret_cast<const uintptr_t*>(src);
  pctx->func_name_heap_ = reinterpret_cast<const char*>(src + *rva_size);
  pctx->p_segments_ =
      reinterpret_cast<const uintptr_t (*)[2]>(src + *rva_size + *f_nam_size);
  pctx->n_segments_ = *seg_cnt;

  return true;
}

void VDDebugInfoDeinit(Context* pctx) {
  if (!pctx->raw_block_.empty()) {
    pctx->raw_block_ = std::string();
  }
}

bool VDDebugInfoInitFromFile(Context* pctx) {
  if (pctx->Loaded()) {
    return true;
  }

  pctx->raw_block_ = std::string();
  pctx->rva_heap_ = nullptr;
  GetVDIPath(pctx->filename_, ARRAYLEN(pctx->filename_));
  pctx->error_ = std::string();

  HANDLE h = CreateFile(
      pctx->filename_, GENERIC_READ, 0, nullptr, OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    pctx->error_ = werr_ssprintf(GetLastError(), "CreateFile failed");
    return false;
  }

  do {
    DWORD file_size = GetFileSize(h, nullptr);
    if (file_size == INVALID_FILE_SIZE) {
      break;
    }

    char* buffer = new char[static_cast<size_t>(file_size) + 1];
    std::fill(buffer, buffer + file_size + 1, '\0');

    DWORD actual;
    int ret = ReadFile(h, buffer, file_size, &actual, nullptr);
    CloseHandle(h);
    pctx->raw_block_ = buffer;
    delete[] buffer;

    if (!ret || actual != file_size) {
      break;
    }

    if (VDDebugInfoInitFromMemory(pctx)) {
      return true;
    }
  } while (0);

  VDDebugInfoDeinit(pctx);
  return false;
}

static bool PointerIsInAnySegment(const Context* pctx, uintptr_t rva) {
  for (int i = 0; i < pctx->n_segments_; ++i) {
    if (rva >= pctx->p_segments_[i][0] &&
        rva < pctx->p_segments_[i][0] + pctx->p_segments_[i][1]) {
      return true;
    }
  }

  return false;
}

static const char* GetNameFromHeap(const char* heap, size_t idx) {
  while (idx--) {
    while (*heap++);
  }

  return heap;
}

intptr_t VDDebugInfoLookupRVA(
    const Context* pctx, uintptr_t rva, char* buf, int buflen) {
  if (!PointerIsInAnySegment(pctx, rva)) {
    return -1;
  }

  const unsigned char* pr = pctx->rva_heap_;
  const unsigned char* pr_limit = (const unsigned char*)pctx->func_name_heap_;
  size_t idx = 0;

  // Linearly unpack RVA deltas and find lower_bound
  rva -= pctx->first_rva_;

  if (static_cast<intptr_t>(rva) < 0) {
    return -1;
  }

  while (pr < pr_limit) {
    unsigned char c;
    uintptr_t diff = 0;

    do {
      c = *pr++;

      diff = (diff << 7) | (c & 0x7f);
    } while (c & 0x80);

    rva -= diff;

    if (static_cast<intptr_t>(rva) < 0) {
      rva += diff;
      break;
    }

    ++idx;
  }
  if (pr >= pr_limit) {
    return -1;
  }

  // Decompress name for RVA
  const char* fn_name = GetNameFromHeap(pctx->func_name_heap_, idx);

  if (!*fn_name) {
    fn_name = "(special)";
  }

  strncpy(buf, fn_name, buflen);
  buf[buflen - 1] = 0;

  return static_cast<intptr_t>(rva);
}
}  // namespace VDDebugInfo

bool ReadFromParent(int fd, void* p, int size) {
  char* buf = (char*)p;
  int got = 0;
  while (got < size) {
    int ret = _read(fd, buf + got, size - got);
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

static bool ReadStringFromParent(int fd, std::string& out_string) {
  int size;
  if (!ReadFromParent(fd, &size, sizeof(size)) || size < 0) {
    return false;
  }

  std::string tmp(static_cast<size_t>(size), '\0');
  if (!ReadFromParent(fd, tmp.data(), size)) {
    return false;
  }

  out_string = std::move(tmp);
  return true;
}

// General symbol lookup; uses VDDebugInfo for detailed information within the
// process, and DbgHelp for simpler information about loaded DLLs.
namespace SymbolLookup {
HANDLE g_hParent;

bool InitDbghelp() {
  static bool initted = false;
  if (!initted) {
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);

    if (!SymInitialize(g_hParent, nullptr, TRUE)) {
      return false;
    }

    initted = true;
  }

  return true;
}

SYMBOL_INFO* GetSym(uintptr_t ptr, DWORD64& disp) {
  InitDbghelp();

  static BYTE buffer[1024];
  SYMBOL_INFO* symbol = (PSYMBOL_INFO)buffer;

  symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
  symbol->MaxNameLen = sizeof(buffer) - sizeof(SYMBOL_INFO) + 1;

  if (!SymFromAddr(g_hParent, ptr, &disp, symbol)) {
    return nullptr;
  }

  return symbol;
}

const char* Demangle(const char* buf) {
  if (!InitDbghelp()) {
    return buf;
  }

  static char obuf[1024];
  if (!UnDecorateSymbolName(
          buf, obuf, sizeof(obuf),
          UNDNAME_COMPLETE | UNDNAME_NO_CV_THISTYPE |
              UNDNAME_NO_ALLOCATION_MODEL |
              UNDNAME_NO_ACCESS_SPECIFIERS  // no public:
              | UNDNAME_NO_MS_KEYWORDS      // no __cdecl
              | UNDNAME_NO_MEMBER_TYPE      // no virtual, static
          )) {
    return buf;
  }

  if (obuf[0] == '_') {
    strcat(obuf, "()");  // _main -> _main()
    return obuf + 1;     // _main -> main
  }

  return obuf;
}

std::string CrashChildGetModuleBaseName(HMODULE mod) {
  _write(_fileno(stdout), &mod, sizeof(mod));

  int fd = _fileno(stdin);
  int size;
  if (!ReadFromParent(fd, &size, sizeof(size))) {
    return "???";
  }
  std::string name;
  char* buffer = new char[static_cast<size_t>(size) + 1];
  std::fill(buffer, buffer + size + 1, '\0');
  if (!ReadFromParent(fd, buffer, size)) {
    name = "???";
  } else {
    name = buffer;
  }
  delete[] buffer;
  return name;
}

void SymLookup(VDDebugInfo::Context* pctx, const void* ptr, char* buf) {
  if (!pctx->Loaded()) {
    strcpy(buf, "error");
    return;
  }

  MEMORY_BASIC_INFORMATION meminfo;
  VirtualQueryEx(g_hParent, ptr, &meminfo, sizeof meminfo);

  char tmp[512];
  intptr_t address = VDDebugInfo::VDDebugInfoLookupRVA(
      pctx, reinterpret_cast<uintptr_t>(ptr), tmp, sizeof(tmp));
  if (address >= 0) {
    wsprintf(
        buf, "%" ADDRESS_ZEROS "Ix: %s [%" ADDRESS_ZEROS "Ix+%Ix+%Ix]",
        reinterpret_cast<uintptr_t>(ptr), Demangle(tmp), pctx->first_rva_,
        reinterpret_cast<uintptr_t>(ptr) - pctx->first_rva_ - address, address);
    return;
  }

  std::string name =
      CrashChildGetModuleBaseName((HMODULE)meminfo.AllocationBase);

  DWORD64 disp;
  SYMBOL_INFO* symbol = GetSym(reinterpret_cast<uintptr_t>(ptr), disp);

  if (symbol) {
    wsprintf(
        buf, "%" ADDRESS_ZEROS "Ix: %s!%s [%" ADDRESS_ZEROS "Ix+%Ix+%Ix]",
        reinterpret_cast<uintptr_t>(ptr), name.c_str(), symbol->Name,
        reinterpret_cast<uintptr_t>(meminfo.AllocationBase),
        static_cast<uintptr_t>(symbol->Address) -
            reinterpret_cast<uintptr_t>(meminfo.AllocationBase),
        static_cast<ULONG_PTR>(disp));
    return;
  }

  wsprintf(
      buf, "%" ADDRESS_ZEROS "Ix: %s!%" ADDRESS_ZEROS "Ix",
      reinterpret_cast<uintptr_t>(ptr), name.c_str(),
      reinterpret_cast<uintptr_t>(meminfo.AllocationBase));
}
}  // namespace SymbolLookup

namespace {

std::string SpliceProgramPath(std::string fn) {
  char buf[MAX_PATH];
  GetModuleFileName(nullptr, buf, sizeof(buf));

  char mod_name[MAX_PATH];
  char* file;
  GetFullPathName(buf, sizeof(mod_name), mod_name, &file);
  strcpy(file, fn.c_str());

  return mod_name;
}

namespace {
VDDebugInfo::Context g_debugInfo;

std::string ReportCallStack(const void* const* backtrace) {
  if (!g_debugInfo.Loaded()) {
    return ssprintf(
        "debug resource file '%s': %s.\n", g_debugInfo.filename_,
        g_debugInfo.error_.c_str());
  }
  /*
  if( g_debugInfo.nBuildNumber != int(version_num) )
  {
          return ssprintf( "Incorrect %s file (build %d, expected %d) for this
  version of " PRODUCT_FAMILY " -- call stack unavailable.\n",
                  g_debugInfo.sFilename, g_debugInfo.nBuildNumber,
  int(version_num) );
  }
  */
  std::string ret;
  for (int i = 0; backtrace[i]; ++i) {
    char buf[10240];
    SymbolLookup::SymLookup(&g_debugInfo, backtrace[i], buf);
    ret += ssprintf("%s\n", buf);
  }

  return ret;
}
}  // namespace

struct CompleteCrashData {
  CrashInfo crash_info_;
  std::string info_;
  std::string additional_log_;
  std::string crashed_thread_;
  std::vector<std::string> recent_;
  std::vector<std::string> checkpoints_;
};

static void MakeCrashReport(const CompleteCrashData& data, std::string& out) {
  out += ssprintf(
      "%s crash report (build %s, %s @ %s)\n"
      "--------------------------------------\n\n",
      (std::string(PRODUCT_FAMILY) + product_version).c_str(),
      ::sm_version_git_hash, version_date, version_time);

  out += ssprintf("Crash reason: %s\n", data.crash_info_.crash_reason_);
  out += ssprintf("\n");

  // Dump thread stacks
  static char buf[1024 * 32];
  out += ssprintf("%s\n", join("\n", data.checkpoints_).c_str());

  out += ReportCallStack(data.crash_info_.backtrace_pointers_);
  out += ssprintf("\n");

  if (data.crash_info_.alternate_thread_backtrace_[0]) {
    for (int i = 0; i < CrashInfo::kMaxBacktraceThreads; ++i) {
      if (!data.crash_info_.alternate_thread_backtrace_[i][0]) {
        continue;
      }

      out +=
          ssprintf("Thread %s:\n", data.crash_info_.alternate_thread_name_[i]);
      out += ssprintf("\n");
      out += ReportCallStack(data.crash_info_.alternate_thread_backtrace_[i]);
      out += ssprintf("");
    }
  }

  out += ssprintf("Static log:\n");
  out += ssprintf("%s", data.info_.c_str());
  out += ssprintf("%s", data.additional_log_.c_str());
  out += ssprintf("\n");

  out += ssprintf("Partial log:\n");
  for (size_t i = 0; i < data.recent_.size(); ++i) {
    out += ssprintf("%s\n", data.recent_[i].c_str());
  }
  out += ssprintf("\n");

  out += ssprintf("-- End of report\n");
}

static void DoSave(const std::string& report) {
  std::string name = SpliceProgramPath("../crashinfo.txt");

  SetFileAttributes(name.c_str(), FILE_ATTRIBUTE_NORMAL);
  FILE* file = fopen(name.c_str(), "w+");
  if (file == nullptr) {
    return;
  }
  fprintf(file, "%s", report.c_str());

  fclose(file);

  // Discourage changing crashinfo.txt.
  SetFileAttributes(name.c_str(), FILE_ATTRIBUTE_READONLY);
}

bool ReadCrashDataFromParent(int fd, CompleteCrashData& data) {
  _setmode(_fileno(stdin), O_BINARY);

  // 0. Read the parent handle.
  if (!ReadFromParent(
          fd, &SymbolLookup::g_hParent, sizeof(SymbolLookup::g_hParent))) {
    return false;
  }

  // 1. Read the CrashData.
  if (!ReadFromParent(fd, &data.crash_info_, sizeof(data.crash_info_))) {
    return false;
  }

  // 2. Read info.
  if (!ReadStringFromParent(fd, data.info_)) {
    return false;
  }

  // 3. Read AdditionalLog.
  if (!ReadStringFromParent(fd, data.additional_log_)) {
    return false;
  }

  // 4. Read RecentLogs.
  int cnt = 0;
  if (!ReadFromParent(fd, &cnt, sizeof(cnt))) {
    return false;
  }
  for (int i = 0; i < cnt; ++i) {
    std::string recent_log;
    if (!ReadStringFromParent(fd, recent_log)) {
      return false;
    }
    data.recent_.push_back(recent_log);
  }

  // 5. Read CHECKPOINTs.
  std::string checkpoints;
  if (!ReadStringFromParent(fd, checkpoints)) {
    return false;
  }
  split(checkpoints, "$$", data.checkpoints_);

  // 6. Read the crashed thread's name.
  if (!ReadStringFromParent(fd, data.crashed_thread_)) {
    return false;
  }

  return true;
}

/* Localization for the crash handler is different, and a little tricky. We
 * don't have ThemeManager loaded, so we have to localize it ourself. We can
 * supply translations with our own substitution function. We need to figure out
 * which language to use. Since these strings won't be pulled from the theme,
 * defer loading them until we use them. XXX */
static LocalizedString A_CRASH_HAS_OCCURRED;
static LocalizedString CLOSE;

void LoadLocalizedStrings() {
  A_CRASH_HAS_OCCURRED.Load(
      "CrashHandler",
      "A crash has occurred.  Diagnostic information has been saved to a file "
      "called \"crashinfo.txt\" in the game program directory.");
  CLOSE.Load("CrashHandler", "&Close");
}

class CrashDialog : public WindowsDialogBox {
 public:
  CrashDialog();
  ~CrashDialog() = default;

 protected:
  virtual INT_PTR HandleMessage(UINT msg, WPARAM w_param, LPARAM l_param);

 private:
  void SetDialogInitial();
};

CrashDialog::CrashDialog() { LoadLocalizedStrings(); }

void CrashDialog::SetDialogInitial() {
  HWND dlg = GetHwnd();

  SetWindowText(
      GetDlgItem(dlg, IDC_MAIN_TEXT), A_CRASH_HAS_OCCURRED.GetValue().c_str());
  SetWindowText(GetDlgItem(dlg, IDC_BUTTON_CLOSE), CLOSE.GetValue().c_str());
  ShowWindow(GetDlgItem(dlg, IDC_PROGRESS), false);
  ShowWindow(GetDlgItem(dlg, IDC_BUTTON_AUTO_REPORT), true);
}

INT_PTR CrashDialog::HandleMessage(UINT msg, WPARAM w_param, LPARAM l_param) {
  HWND dlg = GetHwnd();

  switch (msg) {
    case WM_INITDIALOG:
      SetDialogInitial();
      DialogUtil::SetHeaderFont(dlg, IDC_STATIC_HEADER_TEXT);
      return TRUE;

    case WM_CTLCOLORSTATIC: {
      HDC hdc = (HDC)w_param;
      HWND hwnd_static = (HWND)l_param;
      HBRUSH hbr = nullptr;

      // TODO: Change any attributes of the DC here
      switch (GetDlgCtrlID(hwnd_static)) {
        case IDC_STATIC_HEADER_TEXT:
        case IDC_STATIC_ICON:
          hbr = (HBRUSH)::GetStockObject(WHITE_BRUSH);
          SetBkMode(hdc, OPAQUE);
          SetBkColor(hdc, RGB(255, 255, 255));
          break;
      }

      // TODO: Return a different brush if the default is not desired
      return reinterpret_cast<INT_PTR>(hbr);
    }

    case WM_COMMAND:
      switch (LOWORD(w_param)) {
        case IDC_BUTTON_CLOSE:
          // Close the dialog.
          EndDialog(dlg, FALSE);
          return TRUE;
        case IDOK:
          // EndDialog(dlg, TRUE); // don't always exit on ENTER
          return TRUE;
        case IDC_VIEW_LOG: {
          std::string log_path;
          FILE* file = fopen(SpliceProgramPath("../Portable.ini").c_str(), "r");
          if (file != nullptr) {
            log_path = SpliceProgramPath("../Logs/log.txt");
            fclose(file);
          } else {
            log_path =
                SpecialDirs::GetAppDataDir() + PRODUCT_ID + "/Logs/log.txt";
          }

          ShellExecute(
              nullptr, "open", log_path.c_str(), "", "", SW_SHOWNORMAL);
        } break;
        case IDC_CRASH_SAVE:
          ShellExecute(
              nullptr, "open", SpliceProgramPath("../crashinfo.txt").c_str(),
              "", "", SW_SHOWNORMAL);
          return TRUE;
        case IDC_BUTTON_RESTART:
          Win32RestartProgram();
          EndDialog(dlg, FALSE);
          break;
        case IDC_BUTTON_REPORT:
          // safe to remove button?
          break;
        case IDC_BUTTON_AUTO_REPORT:
          // same here
          break;
      }
      break;
  }

  return FALSE;
}

void ChildProcess() {
  // Read the crash data from the crashed parent.
  CompleteCrashData data;
  ReadCrashDataFromParent(_fileno(stdin), data);

  std::string crash_report;
  VDDebugInfo::VDDebugInfoInitFromFile(&g_debugInfo);
  MakeCrashReport(data, crash_report);
  VDDebugInfo::VDDebugInfoDeinit(&g_debugInfo);

  DoSave(crash_report);

  // Tell the crashing process that it can exit. Be sure to write crashinfo.txt
  // first.
  fclose(stdout);

  // Now that we've done that, the process is gone. Don't use g_hParent.
  CloseHandle(SymbolLookup::g_hParent);
  SymbolLookup::g_hParent = nullptr;

  CrashDialog cd;
  cd.Run(IDD_DISASM_CRASH);
}

}  // namespace

void CrashHandler::CrashHandlerHandleArgs(int argc, char* argv[]) {
  if (argc == 2 && !strcmp(argv[1], CHILD_MAGIC_PARAMETER)) {
    ChildProcess();
    exit(0);
  }
}

/*
 * (c) 2003-2006 Glenn Maynard
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
