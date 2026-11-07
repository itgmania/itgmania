#include "WindowsDialogBox.h"

#include "global.h"

WindowsDialogBox::WindowsDialogBox() { wnd_ = nullptr; }

void WindowsDialogBox::Run(int dialog) {
  char full_app_path[MAX_PATH];
  GetModuleFileName(nullptr, full_app_path, MAX_PATH);
  HINSTANCE handle = LoadLibrary(full_app_path);

  DialogBoxParam(
      handle, MAKEINTRESOURCE(dialog), nullptr, DlgProc, (LPARAM)this);
}

INT_PTR APIENTRY
WindowsDialogBox::DlgProc(HWND dlg, UINT msg, WPARAM w_param, LPARAM l_param) {
  if (msg == WM_INITDIALOG) {
    SetProp(dlg, "WindowsDialogBox", (HANDLE)l_param);
  }

  WindowsDialogBox* this_ptr =
      (WindowsDialogBox*)GetProp(dlg, "WindowsDialogBox");
  if (this_ptr == nullptr) {
    return FALSE;
  }

  if (this_ptr->wnd_ == nullptr) {
    this_ptr->wnd_ = dlg;
  }

  return this_ptr->HandleMessage(msg, w_param, l_param);
}

/*
 * (c) 2006 Glenn Maynard
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
