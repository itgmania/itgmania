/* ScreenMiniMenu - Displays a simple menu over the top of another screen. */

#ifndef SCREEN_MINI_MENU_H
#define SCREEN_MINI_MENU_H

#include <string>
#include <vector>

#include "GameConstantsAndTypes.h"
#include "PlayerNumber.h"
#include "ScreenMessage.h"
#include "ScreenOptions.h"

typedef bool (*MenuRowUpdateEnabled)();

struct MenuRowDef {
  int row_code_;
  std::string name_;
  bool b_enabled_;
  MenuRowUpdateEnabled pfn_enabled_;  // if ! nullptr, used instead of bEnabled
  EditMode show_in_;
  int default_choice_;
  std::vector<std::string> choices_;
  bool theme_title_;
  bool theme_items_;

  MenuRowDef()
      : row_code_(0),
        name_(""),
        b_enabled_(false),
        pfn_enabled_(),
        show_in_(),
        default_choice_(0),
        choices_(),
        theme_title_(false),
        theme_items_(false) {}
  MenuRowDef(
      int r, std::string n, MenuRowUpdateEnabled pe, EditMode s, bool bTT,
      bool bTI, int d, const char* c0 = nullptr, const char* c1 = nullptr,
      const char* c2 = nullptr, const char* c3 = nullptr,
      const char* c4 = nullptr, const char* c5 = nullptr,
      const char* c6 = nullptr, const char* c7 = nullptr,
      const char* c8 = nullptr, const char* c9 = nullptr,
      const char* c10 = nullptr, const char* c11 = nullptr,
      const char* c12 = nullptr, const char* c13 = nullptr,
      const char* c14 = nullptr, const char* c15 = nullptr,
      const char* c16 = nullptr, const char* c17 = nullptr,
      const char* c18 = nullptr, const char* c19 = nullptr,
      const char* c20 = nullptr, const char* c21 = nullptr,
      const char* c22 = nullptr, const char* c23 = nullptr,
      const char* c24 = nullptr, const char* c25 = nullptr)
      : row_code_(r),
        name_(n),
        b_enabled_(true),
        pfn_enabled_(pe),
        show_in_(s),
        default_choice_(d),
        choices_(),
        theme_title_(bTT),
        theme_items_(bTI) {
#define PUSH(c) \
  if (c) choices_.push_back(c);
    PUSH(c0);
    PUSH(c1);
    PUSH(c2);
    PUSH(c3);
    PUSH(c4);
    PUSH(c5);
    PUSH(c6);
    PUSH(c7);
    PUSH(c8);
    PUSH(c9);
    PUSH(c10);
    PUSH(c11);
    PUSH(c12);
    PUSH(c13);
    PUSH(c14);
    PUSH(c15);
    PUSH(c16);
    PUSH(c17);
    PUSH(c18);
    PUSH(c19);
    PUSH(c20);
    PUSH(c21);
    PUSH(c22);
    PUSH(c22);
    PUSH(c23);
    PUSH(c23);
    PUSH(c24);
    PUSH(c25);
#undef PUSH
  }

  MenuRowDef(
      int r, std::string n, bool e, EditMode s, bool bTT, bool bTI, int d,
      std::vector<std::string> options)
      : row_code_(r),
        name_(n),
        b_enabled_(e),
        pfn_enabled_(nullptr),
        show_in_(s),
        default_choice_(d),
        choices_(),
        theme_title_(bTT),
        theme_items_(bTI) {
    for (std::string& str : options) {
      if (str != "") {
        choices_.push_back(str);
      }
    }
  }

  MenuRowDef(
      int r, std::string n, bool e, EditMode s, bool bTT, bool bTI, int d,
      const char* c0 = nullptr, const char* c1 = nullptr,
      const char* c2 = nullptr, const char* c3 = nullptr,
      const char* c4 = nullptr, const char* c5 = nullptr,
      const char* c6 = nullptr, const char* c7 = nullptr,
      const char* c8 = nullptr, const char* c9 = nullptr,
      const char* c10 = nullptr, const char* c11 = nullptr,
      const char* c12 = nullptr, const char* c13 = nullptr,
      const char* c14 = nullptr, const char* c15 = nullptr,
      const char* c16 = nullptr, const char* c17 = nullptr,
      const char* c18 = nullptr, const char* c19 = nullptr,
      const char* c20 = nullptr, const char* c21 = nullptr,
      const char* c22 = nullptr, const char* c23 = nullptr,
      const char* c24 = nullptr, const char* c25 = nullptr)
      : row_code_(r),
        name_(n),
        b_enabled_(e),
        pfn_enabled_(nullptr),
        show_in_(s),
        default_choice_(d),
        choices_(),
        theme_title_(bTT),
        theme_items_(bTI) {
#define PUSH(c) \
  if (c) choices_.push_back(c);
    PUSH(c0);
    PUSH(c1);
    PUSH(c2);
    PUSH(c3);
    PUSH(c4);
    PUSH(c5);
    PUSH(c6);
    PUSH(c7);
    PUSH(c8);
    PUSH(c9);
    PUSH(c10);
    PUSH(c11);
    PUSH(c12);
    PUSH(c13);
    PUSH(c14);
    PUSH(c15);
    PUSH(c16);
    PUSH(c17);
    PUSH(c18);
    PUSH(c19);
    PUSH(c20);
    PUSH(c21);
    PUSH(c22);
    PUSH(c22);
    PUSH(c23);
    PUSH(c23);
    PUSH(c24);
    PUSH(c25);
#undef PUSH
  }

  void SetOneUnthemedChoice(const std::string& sChoice) {
    choices_.resize(1);
    choices_[0] = "|" + sChoice;
  }

  bool SetDefaultChoiceIfPresent(std::string sChoice) {
    default_choice_ = 0;
    for (unsigned i = 0; i < choices_.size(); ++i) {
      if (choices_[i] == sChoice) {
        default_choice_ = i;
        return true;
      }
    }
    return false;
  }
};

struct MenuDef {
  std::string class_name_;
  std::vector<MenuRowDef> rows_;

  MenuDef(
      std::string c, MenuRowDef r0 = MenuRowDef(), MenuRowDef r1 = MenuRowDef(),
      MenuRowDef r2 = MenuRowDef(), MenuRowDef r3 = MenuRowDef(),
      MenuRowDef r4 = MenuRowDef(), MenuRowDef r5 = MenuRowDef(),
      MenuRowDef r6 = MenuRowDef(), MenuRowDef r7 = MenuRowDef(),
      MenuRowDef r8 = MenuRowDef(), MenuRowDef r9 = MenuRowDef(),
      MenuRowDef r10 = MenuRowDef(), MenuRowDef r11 = MenuRowDef(),
      MenuRowDef r12 = MenuRowDef(), MenuRowDef r13 = MenuRowDef(),
      MenuRowDef r14 = MenuRowDef(), MenuRowDef r15 = MenuRowDef(),
      MenuRowDef r16 = MenuRowDef(), MenuRowDef r17 = MenuRowDef(),
      MenuRowDef r18 = MenuRowDef(), MenuRowDef r19 = MenuRowDef(),
      MenuRowDef r20 = MenuRowDef(), MenuRowDef r21 = MenuRowDef(),
      MenuRowDef r22 = MenuRowDef(), MenuRowDef r23 = MenuRowDef(),
      MenuRowDef r24 = MenuRowDef(), MenuRowDef r25 = MenuRowDef(),
      MenuRowDef r26 = MenuRowDef(), MenuRowDef r27 = MenuRowDef(),
      MenuRowDef r28 = MenuRowDef(), MenuRowDef r29 = MenuRowDef())
      : class_name_(c), rows_() {
#define PUSH(r) \
  if (!r.name_.empty()) rows_.push_back(r);
    PUSH(r0);
    PUSH(r1);
    PUSH(r2);
    PUSH(r3);
    PUSH(r4);
    PUSH(r5);
    PUSH(r6);
    PUSH(r7);
    PUSH(r8);
    PUSH(r9);
    PUSH(r10);
    PUSH(r11);
    PUSH(r12);
    PUSH(r13);
    PUSH(r14);
    PUSH(r15);
    PUSH(r16);
    PUSH(r17);
    PUSH(r18);
    PUSH(r19);
    PUSH(r20);
    PUSH(r21);
    PUSH(r22);
    PUSH(r23);
    PUSH(r24);
    PUSH(r25);
    PUSH(r26);
    PUSH(r27);
    PUSH(r28);
    PUSH(r29);
#undef PUSH
  }
};

class ScreenMiniMenu : public ScreenOptions {
 public:
  static void MiniMenu(
      const MenuDef* pDef, ScreenMessage smSendOnOK,
      ScreenMessage smSendOnCancel = SM_None, float fX = 0, float fY = 0);

  void Init();
  void BeginScreen();
  void HandleScreenMessage(const ScreenMessage SM);

 protected:
  virtual void AfterChangeValueOrRow(PlayerNumber pn);
  virtual void ImportOptions(int iRow, const std::vector<PlayerNumber>& vpns);
  virtual void ExportOptions(int iRow, const std::vector<PlayerNumber>& vpns);

  virtual bool FocusedItemEndsScreen(PlayerNumber pn) const;

  void LoadMenu(const MenuDef* pDef);

  ScreenMessage send_on_ok_;
  ScreenMessage send_on_cancel_;

  std::vector<MenuRowDef> menu_rows_;

 public:
  ScreenMiniMenu() : send_on_ok_(), send_on_cancel_(), menu_rows_() {}

  static bool cancelled_;
  static int last_row_code_;
  static std::vector<int> last_answers_;
};

#endif

/*
 * (c) 2003-2004 Chris Danford
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
