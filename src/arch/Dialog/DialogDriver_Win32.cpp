#include "DialogDriver_Win32.h"

#include <string>

#include "LocalizedString.h"
#include "ProductInfo.h"
#include "RageUtil.h"
#include "ThemeManager.h"
#include "archutils/Win32/SpecialDirs.h"
#include "archutils/win32/AppInstance.h"
#include "archutils/win32/DialogUtil.h"
#include "archutils/win32/ErrorStrings.h"
#include "archutils/win32/GraphicsWindow.h"
#include "archutils/win32/RestartProgram.h"
#include "archutils/win32/WindowsResources.h"
#include "global.h"

REGISTER_DIALOG_DRIVER_CLASS(Win32);

static bool g_bHush;
static std::string g_sMessage;
static bool g_bAllowHush;

static INT_PTR CALLBACK
OKWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  switch (msg) {
    case WM_INITDIALOG: {
      // Disable the parent window, like a modal MessageBox does.
      EnableWindow(GetParent(hwnd), FALSE);

      DialogUtil::LocalizeDialogAndContents(hwnd);

      // Hide or display "Don't show this message."
      g_bHush = false;
      HWND hush_button = GetDlgItem(hwnd, IDC_HUSH);
      int style = GetWindowLong(hush_button, GWL_STYLE);

      if (g_bAllowHush) {
        style |= WS_VISIBLE;
      } else {
        style &= ~WS_VISIBLE;
      }
      SetWindowLong(hush_button, GWL_STYLE, style);

      // Set static text.
      std::string message = g_sMessage;
      Replace(message, "\n", "\r\n");
      SetWindowText(GetDlgItem(hwnd, IDC_MESSAGE), message.c_str());

      // Focus is on any of the controls in the dialog by default.
      // I'm not sure why. Set focus to the button manually. -Chris
      SetFocus(GetDlgItem(hwnd, IDOK));
    } break;

    case WM_DESTROY:
      // Re-enable the parent window.
      EnableWindow(GetParent(hwnd), TRUE);
      break;

    case WM_COMMAND:
      switch (LOWORD(wparam)) {
        case IDOK:
          g_bHush = !!IsDlgButtonChecked(hwnd, IDC_HUSH);
          [[fallthrough]];
        case IDCANCEL:
          EndDialog(hwnd, 0);
          break;
      }
  }
  return FALSE;
}
static HWND GetHwnd() { return GraphicsWindow::GetHwnd(); }
static LocalizedString ERROR_WINDOW_TITLE("Dialog-Prompt", "Error");
static std::string GetWindowTitle() {
  std::string s = ERROR_WINDOW_TITLE.GetValue();
  return s;
}

void DialogDriver_Win32::OK(std::string message, std::string id) {
  g_bAllowHush = id != "";
  g_sMessage = message;
  AppInstance handle;
  DialogBox(handle.Get(), MAKEINTRESOURCE(IDD_OK), ::GetHwnd(), OKWndProc);
  if (g_bAllowHush && g_bHush) {
    Dialog::IgnoreMessage(id);
  }
}

static std::string g_sErrorString;

static INT_PTR CALLBACK
ErrorWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  switch (msg) {
    case WM_INITDIALOG: {
      DialogUtil::SetHeaderFont(hwnd, IDC_STATIC_HEADER_TEXT);

      // Set static text
      std::string message = g_sErrorString;
      Replace(message, "\n", "\r\n");
      SetWindowText(GetDlgItem(hwnd, IDC_EDIT_ERROR), message.c_str());
    } break;
    case WM_COMMAND:
      switch (LOWORD(wparam)) {
        case IDC_BUTTON_VIEW_LOG: {
          PROCESS_INFORMATION pi;
          STARTUPINFO si;
          ZeroMemory(&si, sizeof(si));

          std::string app_data_dir = SpecialDirs::GetAppDataDir();
          std::string command =
              "notepad \"" + app_data_dir + PRODUCT_ID + "/Logs/log.txt\"";
          CreateProcess( // TODO: resolve Warning C6335 "leaking process information"
					nullptr,		// pointer to name of executable module
					const_cast<char *>(command.c_str()),	// pointer to command line string
					nullptr,  // process security attributes
					nullptr,   // thread security attributes
					false,  // handle inheritance flag
					0, // creation flags
					nullptr,  // pointer to new environment block
					nullptr,   // pointer to current directory name
					&si,  // pointer to STARTUPINFO
					&pi  // pointer to PROCESS_INFORMATION
				);
        } break;
        case IDC_BUTTON_REPORT:
          // safe to remove this button?
          break;
        case IDC_BUTTON_RESTART:
          Win32RestartProgram();
        case IDOK:
          EndDialog(hwnd, 0);
          break;
      }
      break;
    case WM_CTLCOLORSTATIC: {
      HDC hdc = (HDC)wparam;
      HWND hwnd_static = (HWND)lparam;
      HBRUSH hbr = nullptr;

      // TODO:  Change any attributes of the DC here
      switch (GetDlgCtrlID(hwnd_static)) {
        case IDC_STATIC_HEADER_TEXT:
        case IDC_STATIC_ICON:
          hbr = (HBRUSH)::GetStockObject(WHITE_BRUSH);
          SetBkMode(hdc, OPAQUE);
          SetBkColor(hdc, RGB(255, 255, 255));
          break;
      }

      // TODO:  Return a different brush if the default is not desired
      return reinterpret_cast<INT_PTR>(hbr);
    }
  }
  return FALSE;
}

void DialogDriver_Win32::Error(std::string error, std::string id) {
  g_sErrorString = error;

  // throw up a pretty error dialog
  AppInstance handle;
  DialogBox(
      handle.Get(), MAKEINTRESOURCE(IDD_ERROR_DIALOG), nullptr, ErrorWndProc);
}

Dialog::Result DialogDriver_Win32::AbortRetryIgnore(
    std::string message, std::string id) {
  int ret = 0;
  ret = ::MessageBox(
      ::GetHwnd(), ConvertUTF8ToACP(message).c_str(),
      ConvertUTF8ToACP(::GetWindowTitle()).c_str(),
      MB_ABORTRETRYIGNORE | MB_DEFBUTTON3);
  switch (ret) {
    case IDABORT:
      return Dialog::abort;
    case IDRETRY:
      return Dialog::retry;
    case IDIGNORE:
      return Dialog::ignore;
    default:
      FAIL_M(ssprintf(
          "Unexpected response to Abort/Retry/Ignore dialog: %i", ret));
  }
}

Dialog::Result DialogDriver_Win32::AbortRetry(
    std::string message, std::string id) {
  int ret = 0;
  ret = ::MessageBox(
      ::GetHwnd(), ConvertUTF8ToACP(message).c_str(),
      ConvertUTF8ToACP(::GetWindowTitle()).c_str(), MB_RETRYCANCEL);
  switch (ret) {
    case IDRETRY:
      return Dialog::retry;
    case IDCANCEL:
      return Dialog::abort;
    default:
      FAIL_M(ssprintf("Unexpected response to Retry/Cancel dialog: %i", ret));
  }
}

/*
 * (c) 2003-2004 Glenn Maynard, Chris Danford
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
