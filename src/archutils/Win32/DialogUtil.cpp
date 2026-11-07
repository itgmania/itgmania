#include "DialogUtil.h"

#include <cmath>
#include <memory>
#include <string>

#include "RageUtil.h"
#include "ThemeManager.h"
#include "archutils/Win32/ErrorStrings.h"
#include "global.h"

// Create*Font copied from MFC's CFont

// log_font->nHeight is interpreted as PointSize * 10
static HFONT CreatePointFontIndirect(const LOGFONT* lp_log_font) {
  HDC dc = ::GetDC(nullptr);

  // convert nPointSize to logical units based on pDC
  LOGFONT log_font = *lp_log_font;
  POINT pt;
  pt.y = ::GetDeviceCaps(dc, LOGPIXELSY) * log_font.lfHeight;
  pt.y /= 720;  // 72 points/inch * 10 decipoints/point
  pt.x = 0;
  ::DPtoLP(dc, &pt, 1);
  POINT pt_org = {0, 0};
  ::DPtoLP(dc, &pt_org, 1);
  log_font.lfHeight = -std::abs(pt.y - pt_org.y);

  ReleaseDC(nullptr, dc);

  return ::CreateFontIndirect(&log_font);
}

// point_size is actually scaled 10x
static HFONT CreatePointFont(int point_size, LPCTSTR face_name) {
  ASSERT(face_name != nullptr);

  LOGFONT log_font;
  memset(&log_font, 0, sizeof(LOGFONT));
  log_font.lfCharSet = DEFAULT_CHARSET;
  log_font.lfHeight = point_size;
  lstrcpyn(log_font.lfFaceName, face_name, strlen(log_font.lfFaceName));

  return ::CreatePointFontIndirect(&log_font);
}

struct FontDeleter {
  void operator()(HFONT font) const {
    if (font) {
      ::DeleteObject(font);
    }
  }
};

void DialogUtil::SetHeaderFont(HWND hdlg, int id) {
  ASSERT(hdlg != nullptr);

  HWND control = ::GetDlgItem(hdlg, id);
  ASSERT(control != nullptr);

  static std::unique_ptr<std::remove_pointer<HFONT>::type, FontDeleter> hfont(
      CreatePointFont(16 * 10, "Arial Black"));

  ::SendMessage(control, WM_SETFONT, (WPARAM)hfont.get(), TRUE);
}

void DialogUtil::LocalizeDialogAndContents(HWND hdlg) {
  ASSERT(THEME != nullptr);

  const int kLargeString = 256;
  char temp[kLargeString] = "";
  std::string group;

  {
    ::GetWindowText(hdlg, temp, ARRAYLEN(temp));
    std::string s = temp;
    group = "Dialog-" + s;
    s = THEME->GetString(group, s);
    ::SetWindowText(hdlg, ConvertUTF8ToACP(s).c_str());
  }

  for (HWND hwnd_child = ::GetTopWindow(hdlg); hwnd_child != nullptr;
       hwnd_child = ::GetNextWindow(hwnd_child, GW_HWNDNEXT)) {
    ::GetWindowText(hwnd_child, temp, ARRAYLEN(temp));
    std::string s = temp;
    if (s.empty()) {
      continue;
    }
    s = THEME->GetString(group, s);
    ::SetWindowText(hwnd_child, ConvertUTF8ToACP(s).c_str());
  }
}

/*
 * (c) 2002-2004 Chris Danford
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
