#include "ScreenTextEntry.h"

#include <cmath>
#include <string>

#include "ActorUtil.h"
#include "BitmapText.h"
#include "FontCharAliases.h"
#include "InputEventPlus.h"
#include "InputFilter.h"
#include "LocalizedString.h"
#include "LuaBinding.h"
#include "LuaManager.h"
#include "RageInput.h"
#include "RageInputDevice.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "RageUtil/ConvertValue.h"
#include "Screen.h"
#include "ScreenManager.h"
#include "ScreenMessage.h"
#include "ScreenPrompt.h"
#include "ScreenWithMenuElements.h"
#include "ThemeManager.h"
#include "arch/ArchHooks/ArchHooks.h"  // HOOKS->GetClipboard()

static const char* g_szKeys[NUM_KeyboardRow][KEYS_PER_ROW] = {
    {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M"},
    {"N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z"},
    {"a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m"},
    {"n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z"},
    {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "", "", ""},
    {"!", "@", "#", "$", "%", "^", "&", "(", ")", "[", "]", "{", "}"},
    {"+", "-", "=", "_", ",", ".", "'", "\"", ":", "", "", "", ""},
    {"", "", "Space", "", "", "Backspace", "", "", "Cancel", "", "", "Done",
     ""},
};

std::string ScreenTextEntry::last_answer_ = "";

// Settings:
namespace {
std::string g_sQuestion;
std::string g_sInitialAnswer;
int g_iMaxInputLength;
bool (*g_pValidate)(const std::string& sAnswer, std::string& sErrorOut);
void (*g_pOnOK)(const std::string& sAnswer);
void (*g_pOnCancel)();
bool g_bPassword;
bool (*g_pValidateAppend)(
    const std::string& sAnswerBeforeChar, std::string& sAppend);
std::string (*g_pFormatAnswerForDisplay)(const std::string& sAnswer);

// Lua bridge
LuaReference g_ValidateFunc;
LuaReference g_OnOKFunc;
LuaReference g_OnCancelFunc;
LuaReference g_ValidateAppendFunc;
LuaReference g_FormatAnswerForDisplayFunc;
};  // namespace

void ScreenTextEntry::SetTextEntrySettings(
    std::string sQuestion, std::string sInitialAnswer, int iMaxInputLength,
    bool (*Validate)(const std::string& sAnswer, std::string& sErrorOut),
    void (*OnOK)(const std::string& sAnswer), void (*OnCancel)(),
    bool bPassword,
    bool (*ValidateAppend)(
        const std::string& sAnswerBeforeChar, std::string& sAppend),
    std::string (*FormatAnswerForDisplay)(const std::string& sAnswer)) {
  g_sQuestion = sQuestion;
  g_sInitialAnswer = sInitialAnswer;
  g_iMaxInputLength = iMaxInputLength;
  g_pValidate = Validate;
  g_pOnOK = OnOK;
  g_pOnCancel = OnCancel;
  g_pValidateAppend = ValidateAppend;
  g_pFormatAnswerForDisplay = FormatAnswerForDisplay;
}

void ScreenTextEntry::TextEntry(
    ScreenMessage smSendOnPop, std::string sQuestion,
    std::string sInitialAnswer, int iMaxInputLength,
    bool (*Validate)(const std::string& sAnswer, std::string& sErrorOut),
    void (*OnOK)(const std::string& sAnswer), void (*OnCancel)(),
    bool bPassword,
    bool (*ValidateAppend)(
        const std::string& sAnswerBeforeChar, std::string& sAppend),
    std::string (*FormatAnswerForDisplay)(const std::string& sAnswer)) {
  g_sQuestion = sQuestion;
  g_sInitialAnswer = sInitialAnswer;
  g_iMaxInputLength = iMaxInputLength;
  g_pValidate = Validate;
  g_pOnOK = OnOK;
  g_pOnCancel = OnCancel;
  g_bPassword = bPassword;
  g_pValidateAppend = ValidateAppend;
  g_pFormatAnswerForDisplay = FormatAnswerForDisplay;

  SCREENMAN->AddNewScreenToTop("ScreenTextEntry", smSendOnPop);
}

static LocalizedString INVALID_FLOAT(
    "ScreenTextEntry", "\"%s\" is an invalid floating point value.");
bool ScreenTextEntry::FloatValidate(
    const std::string& sAnswer, std::string& sErrorOut) {
  float f;
  if (StringToFloat(sAnswer, f)) {
    return true;
  }
  sErrorOut = ssprintf(INVALID_FLOAT.GetValue().c_str(), sAnswer.c_str());
  return false;
}

static LocalizedString INVALID_INT(
    "ScreenTextEntry", "\"%s\" is an invalid integer value.");
bool ScreenTextEntry::IntValidate(
    const std::string& sAnswer, std::string& sErrorOut) {
  int f;
  if (sAnswer >> f) {
    return true;
  }
  sErrorOut = ssprintf(INVALID_INT.GetValue().c_str(), sAnswer.c_str());
  return false;
}

bool ScreenTextEntry::cancelled_last_ = false;

/* Handle UTF-8. Right now, we need to at least be able to backspace a whole
 * UTF-8 character. Better would be to operate in wchar_t.
 *
 * XXX: Don't allow internal-use codepoints (above 0xFFFF); those are subject to
 * change and shouldn't be written to disk. */
REGISTER_SCREEN_CLASS(ScreenTextEntry);
REGISTER_SCREEN_CLASS(ScreenTextEntryVisual);

void ScreenTextEntry::Init() {
  ScreenWithMenuElements::Init();

  text_question_.LoadFromFont(THEME->GetPathF(name_, "question"));
  text_question_.SetName("Question");
  LOAD_ALL_COMMANDS(text_question_);
  this->AddChild(&text_question_);

  text_answer_.LoadFromFont(THEME->GetPathF(name_, "answer"));
  text_answer_.SetName("Answer");
  LOAD_ALL_COMMANDS(text_answer_);
  this->AddChild(&text_answer_);

  show_answer_caret_ = false;
  // m_iCaretLocation = 0;

  snd_type_.Load(THEME->GetPathS(name_, "type"), true);
  snd_backspace_.Load(THEME->GetPathS(name_, "backspace"), true);
}

void ScreenTextEntry::BeginScreen() {
  answer_ = RStringToWstring(g_sInitialAnswer);

  ScreenWithMenuElements::BeginScreen();

  text_question_.SetText(g_sQuestion);
  SET_XY(text_question_);
  SET_XY(text_answer_);

  UpdateAnswerText();
}

static LocalizedString ANSWER_CARET("ScreenTextEntry", "AnswerCaret");
static LocalizedString ANSWER_BLANK("ScreenTextEntry", "AnswerBlank");
void ScreenTextEntry::UpdateAnswerText() {
  std::string s;
  if (g_bPassword) {
    s = std::string(answer_.size(), '*');
  } else {
    s = WStringToRString(answer_);
  }

  bool bAnswerFull = (int)s.length() >= g_iMaxInputLength;

  if (g_pFormatAnswerForDisplay) {
    s = g_pFormatAnswerForDisplay(s);
  }

  // Handle caret drawing
  // m_iCaretLocation = s.length()
  if (show_answer_caret_ && !bAnswerFull) {
    s += ANSWER_CARET;  // was '_'
  } else {
    s += ANSWER_BLANK;  // was "  "
  }

  FontCharAliases::ReplaceMarkers(s);
  text_answer_.SetText(s);
}

void ScreenTextEntry::Update(float fDelta) {
  ScreenWithMenuElements::Update(fDelta);

  if (timer_toggle_cursor_.Ago() > 0.25f) {
    timer_toggle_cursor_.Touch();
    show_answer_caret_ = !show_answer_caret_;
    UpdateAnswerText();
  }
}

bool ScreenTextEntry::Input(const InputEventPlus& input) {
  static bool bLCtrl = false, bRCtrl = false;
  if (IsTransitioning()) {
    return false;
  }

  // bLCtrl and bRCtl are whether their respective Ctrl keys are held
  // We update them here.
  if (input.device_i_ == DeviceInput(DEVICE_KEYBOARD, KEY_LCTRL)) {
    switch (input.type_) {
      case IET_FIRST_PRESS:
        bLCtrl = true;
        break;
      case IET_RELEASE:
        bLCtrl = false;
        break;
      default:
        break;
    }
  }

  if (input.device_i_ == DeviceInput(DEVICE_KEYBOARD, KEY_RCTRL)) {
    switch (input.type_) {
      case IET_FIRST_PRESS:
        bRCtrl = true;
        break;
      case IET_RELEASE:
        bRCtrl = false;
        break;
      default:
        break;
    }
  }

  bool bHandled = false;
  if (input.device_i_ == DeviceInput(DEVICE_KEYBOARD, KEY_BACK)) {
    switch (input.type_) {
      case IET_FIRST_PRESS:
      case IET_REPEAT:
        BackspaceInAnswer();
        bHandled = true;
      default:
        break;
    }
  } else if (input.type_ == IET_FIRST_PRESS) {
    wchar_t c = INPUTMAN->DeviceInputToChar(input.device_i_, true);
    // Detect Ctrl+V
    if ((c == L'v' || c == L'V') && (bLCtrl || bRCtrl)) {
      TryAppendToAnswer(HOOKS->GetClipboard());

      TextEnteredDirectly();  // XXX: This doesn't seem appropriate but there's
                              // no TextPasted()
      bHandled = true;
    } else if (c >= L' ') {
      // todo: handle caps lock -aj
      TryAppendToAnswer(WStringToRString(std::wstring() + c));

      TextEnteredDirectly();
      bHandled = true;
    }
  }

  return ScreenWithMenuElements::Input(input) || bHandled;
}

void ScreenTextEntry::TryAppendToAnswer(std::string s) {
  {
    std::wstring sNewAnswer = answer_ + RStringToWstring(s);
    if ((int)sNewAnswer.length() > g_iMaxInputLength) {
      SCREENMAN->PlayInvalidSound();
      return;
    }
  }

  if (g_pValidateAppend && !g_pValidateAppend(WStringToRString(answer_), s)) {
    SCREENMAN->PlayInvalidSound();
    return;
  }

  std::wstring sNewAnswer = answer_ + RStringToWstring(s);
  answer_ = sNewAnswer;
  snd_type_.Play(true);
  UpdateAnswerText();
}

void ScreenTextEntry::BackspaceInAnswer() {
  if (answer_.empty()) {
    SCREENMAN->PlayInvalidSound();
    return;
  }
  answer_.erase(answer_.end() - 1);
  snd_backspace_.Play(true);
  UpdateAnswerText();
}

bool ScreenTextEntry::MenuStart(const InputEventPlus& input) {
  // HACK: Only allow the screen to end on the Enter key.-aj
  if (input.device_i_ == DeviceInput(DEVICE_KEYBOARD, KEY_ENTER) &&
      input.type_ == IET_FIRST_PRESS) {
    End(false);
    return true;
  }
  return false;
}

void ScreenTextEntry::End(bool bCancelled) {
  if (bCancelled) {
    if (g_pOnCancel) {
      g_pOnCancel();
    }

    Cancel(SM_GoToNextScreen);
    // TweenOffScreen();
  } else {
    std::string sAnswer = WStringToRString(answer_);
    std::string sError;
    if (g_pValidate != nullptr) {
      bool bValidAnswer = g_pValidate(sAnswer, sError);
      if (!bValidAnswer) {
        ScreenPrompt::Prompt(SM_None, sError);
        return;  // don't end this screen.
      }
    }

    if (g_pOnOK) {
      std::string ret = WStringToRString(answer_);
      FontCharAliases::ReplaceMarkers(ret);
      g_pOnOK(ret);
    }

    StartTransitioningScreen(SM_GoToNextScreen);
    SCREENMAN->PlayStartSound();
  }

  cancelled_last_ = bCancelled;
  last_answer_ = bCancelled ? std::string("") : WStringToRString(answer_);
}

bool ScreenTextEntry::MenuBack(const InputEventPlus& input) {
  if (input.type_ != IET_FIRST_PRESS) {
    return false;
  }
  End(true);
  return true;
}

void ScreenTextEntry::TextEntrySettings::FromStack(lua_State* L) {
  if (lua_type(L, 1) != LUA_TTABLE) {
    LOG->Trace("not a table");
    return;
  }

  lua_pushvalue(L, 1);
  const int iTab = lua_gettop(L);

  // Get ScreenMessage
  lua_getfield(L, iTab, "SendOnPop");
  const char* pStr = lua_tostring(L, -1);
  if (pStr == nullptr) {
    send_on_pop_ = SM_None;
  } else {
    send_on_pop_ = ScreenMessageHelpers::ToScreenMessage(pStr);
  }
  lua_settop(L, iTab);

  // Get Question
  lua_getfield(L, iTab, "Question");
  pStr = lua_tostring(L, -1);
  if (pStr == nullptr) {
    LuaHelpers::ReportScriptError(
        "ScreenTextEntry \"Question\" entry is not a string.");
    pStr = "";
  }
  question_ = pStr;
  lua_settop(L, iTab);

  // Get Initial Answer
  lua_getfield(L, iTab, "InitialAnswer");
  pStr = lua_tostring(L, -1);
  if (pStr == nullptr) {
    pStr = "";
  }
  initial_answer_ = pStr;
  lua_settop(L, iTab);

  // Get Max Input Length
  lua_getfield(L, iTab, "MaxInputLength");
  max_input_length_ = lua_tointeger(L, -1);
  lua_settop(L, iTab);

  // Get Password
  lua_getfield(L, iTab, "Password");
  password_ = !!lua_toboolean(L, -1);
  lua_settop(L, iTab);

#define SET_FUNCTION_MEMBER(memname)                            \
  lua_getfield(L, iTab, #memname);                              \
  if (lua_isfunction(L, -1)) {                                  \
    memname.SetFromStack(L);                                    \
  } else if (!lua_isnil(L, -1)) {                               \
    LuaHelpers::ReportScriptError(                              \
        "ScreenTextEntry \"" #memname "\" is not a function."); \
  }                                                             \
  lua_settop(L, iTab);

  SET_FUNCTION_MEMBER(validate_);
  SET_FUNCTION_MEMBER(on_ok_);
  SET_FUNCTION_MEMBER(on_cancel_);
  SET_FUNCTION_MEMBER(validate_append_);
  SET_FUNCTION_MEMBER(format_answer_for_display_);
#undef SET_FUNCTION_MEMBER
}

// Lua bridges
static bool ValidateFromLua(
    const std::string& sAnswer, std::string& sErrorOut) {
  if (g_ValidateFunc.IsNil() || !g_ValidateFunc.IsSet()) {
    return true;
  }
  Lua* L = LUA->Get();

  g_ValidateFunc.PushSelf(L);

  // Argument 1 (answer):
  lua_pushstring(L, sAnswer.c_str());

  // Argument 2 (error out):
  lua_pushstring(L, sErrorOut.c_str());

  bool valid = false;

  std::string error = "Lua error in ScreenTextEntry Validate: ";
  if (LuaHelpers::RunScriptOnStack(L, error, 2, 2, true)) {
    if (!lua_isstring(L, -1) || !lua_isboolean(L, -2)) {
      LuaHelpers::ReportScriptError(
          "Lua error: ScreenTextEntry Validate did not return 'bool, string'.");
    } else {
      std::string ErrorFromLua;
      LuaHelpers::Pop(L, ErrorFromLua);
      if (!ErrorFromLua.empty()) {
        sErrorOut = ErrorFromLua;
      }
      LuaHelpers::Pop(L, valid);
    }
  }
  lua_settop(L, 0);
  LUA->Release(L);
  return valid;
}

static void OnOKFromLua(const std::string& sAnswer) {
  if (g_OnOKFunc.IsNil() || !g_OnOKFunc.IsSet()) {
    return;
  }
  Lua* L = LUA->Get();

  g_OnOKFunc.PushSelf(L);
  // Argument 1 (answer):
  lua_pushstring(L, sAnswer.c_str());
  std::string error = "Lua error in ScreenTextEntry OnOK: ";
  LuaHelpers::RunScriptOnStack(L, error, 1, 0, true);

  LUA->Release(L);
}

static void OnCancelFromLua() {
  if (g_OnCancelFunc.IsNil() || !g_OnCancelFunc.IsSet()) {
    return;
  }
  Lua* L = LUA->Get();

  g_OnCancelFunc.PushSelf(L);
  std::string error = "Lua error in ScreenTextEntry OnCancel: ";
  LuaHelpers::RunScriptOnStack(L, error, 0, 0, true);

  LUA->Release(L);
}

static bool ValidateAppendFromLua(
    const std::string& sAnswerBeforeChar, std::string& sAppend) {
  if (g_ValidateAppendFunc.IsNil() || !g_ValidateAppendFunc.IsSet()) {
    return true;
  }
  Lua* L = LUA->Get();

  g_ValidateAppendFunc.PushSelf(L);

  // Argument 1 (AnswerBeforeChar):
  lua_pushstring(L, sAnswerBeforeChar.c_str());

  // Argument 2 (Append):
  lua_pushstring(L, sAppend.c_str());

  bool append = false;

  std::string error = "Lua error in ScreenTextEntry ValidateAppend: ";
  if (LuaHelpers::RunScriptOnStack(L, error, 2, 1, true)) {
    if (!lua_isboolean(L, -1)) {
      LuaHelpers::ReportScriptError(
          "\"ValidateAppend\" did not return a boolean.");
    } else {
      LuaHelpers::Pop(L, append);
    }
  }
  lua_settop(L, 0);
  LUA->Release(L);
  return append;
}

static std::string FormatAnswerForDisplayFromLua(const std::string& sAnswer) {
  if (g_FormatAnswerForDisplayFunc.IsNil() ||
      !g_FormatAnswerForDisplayFunc.IsSet()) {
    return sAnswer;
  }
  Lua* L = LUA->Get();

  g_FormatAnswerForDisplayFunc.PushSelf(L);
  // Argument 1 (Answer):
  lua_pushstring(L, sAnswer.c_str());

  std::string answer;
  std::string error = "Lua error in ScreenTextEntry FormatAnswerForDisplay: ";
  if (LuaHelpers::RunScriptOnStack(L, error, 1, 1, true)) {
    if (!lua_isstring(L, -1)) {
      LuaHelpers::ReportScriptError(
          "\"FormatAnswerForDisplay\" did not return a string.");
    } else {
      LuaHelpers::Pop(L, answer);
    }
  }
  lua_settop(L, 0);
  LUA->Release(L);
  return answer;
}

void ScreenTextEntry::LoadFromTextEntrySettings(
    const TextEntrySettings& settings) {
  g_ValidateFunc = settings.validate_;
  g_OnOKFunc = settings.on_ok_;
  g_OnCancelFunc = settings.on_cancel_;
  g_ValidateAppendFunc = settings.validate_append_;
  g_FormatAnswerForDisplayFunc = settings.format_answer_for_display_;

  // set functions
  SetTextEntrySettings(
      settings.question_, settings.initial_answer_, settings.max_input_length_,
      ValidateFromLua,  // Validate
      OnOKFromLua,      // OnOK
      OnCancelFromLua,  // OnCancel
      settings.password_,
      ValidateAppendFromLua,         // ValidateAppend
      FormatAnswerForDisplayFromLua  // FormatAnswerForDisplay
  );

  // Hack: reload screen with new info
  BeginScreen();
}

/** @brief Allow Lua to have access to the ScreenTextEntry. */
class LunaScreenTextEntry : public Luna<ScreenTextEntry> {
 public:
  static int Load(T* p, lua_State* L) {
    ScreenTextEntry::TextEntrySettings settings;
    settings.FromStack(L);
    p->LoadFromTextEntrySettings(settings);
    return 0;
  }

  LunaScreenTextEntry() { ADD_METHOD(Load); }
};
LUA_REGISTER_DERIVED_CLASS(ScreenTextEntry, ScreenWithMenuElements)
// lua end

// begin ScreenTextEntryVisual
void ScreenTextEntryVisual::Init() {
  ROW_START_X.Load(name_, "RowStartX");
  ROW_START_Y.Load(name_, "RowStartY");
  ROW_END_X.Load(name_, "RowEndX");
  ROW_END_Y.Load(name_, "RowEndY");

  ScreenTextEntry::Init();

  spr_cursor_.Load(THEME->GetPathG(name_, "cursor"));
  spr_cursor_->SetName("Cursor");
  LOAD_ALL_COMMANDS(spr_cursor_);
  this->AddChild(spr_cursor_);

  // Init keyboard
  {
    BitmapText text;
    text.LoadFromFont(THEME->GetPathF(name_, "keyboard"));
    text.SetName("Keys");
    ActorUtil::LoadAllCommands(text, name_);
    text.PlayCommand("Init");

    FOREACH_KeyboardRow(r) {
      for (int x = 0; x < KEYS_PER_ROW; ++x) {
        BitmapText*& pbt = text_keys_[r][x];
        pbt = text.Copy();
        this->AddChild(pbt);

        std::string s = g_szKeys[r][x];
        if (!s.empty() && r == KEYBOARD_ROW_SPECIAL) {
          s = THEME->GetString(name_, s);
        }
        pbt->SetText(s);
      }
    }
  }

  snd_change_.Load(THEME->GetPathS(name_, "change"), true);
}

ScreenTextEntryVisual::~ScreenTextEntryVisual() {
  FOREACH_KeyboardRow(r) for (int x = 0; x < KEYS_PER_ROW; ++x)
      RageUtil::SafeDelete(text_keys_[r][x]);
}

void ScreenTextEntryVisual::BeginScreen() {
  ScreenTextEntry::BeginScreen();

  focus_x_ = 0;
  focus_y_ = (KeyboardRow)0;

  FOREACH_KeyboardRow(r) {
    for (int x = 0; x < KEYS_PER_ROW; ++x) {
      BitmapText& bt = *text_keys_[r][x];
      float fX =
          std::round(SCALE(x, 0, KEYS_PER_ROW - 1, ROW_START_X, ROW_END_X));
      float fY =
          std::round(SCALE(r, 0, NUM_KeyboardRow - 1, ROW_START_Y, ROW_END_Y));
      bt.SetXY(fX, fY);
    }
  }

  PositionCursor();
}

void ScreenTextEntryVisual::PositionCursor() {
  BitmapText& bt = *text_keys_[focus_y_][focus_x_];
  spr_cursor_->SetXY(bt.GetX(), bt.GetY());
  spr_cursor_->PlayCommand(
      focus_y_ == KEYBOARD_ROW_SPECIAL ? "SpecialKey" : "RegularKey");
}

void ScreenTextEntryVisual::TextEnteredDirectly() {
  // If the user enters text with a keyboard, jump to DONE, so enter ends the
  // screen.
  focus_y_ = KEYBOARD_ROW_SPECIAL;
  focus_x_ = DONE;

  PositionCursor();
}

void ScreenTextEntryVisual::MoveX(int iDir) {
  std::string sKey;
  do {
    focus_x_ += iDir;
    wrap(focus_x_, KEYS_PER_ROW);

    sKey = g_szKeys[focus_y_][focus_x_];
  } while (sKey == "");

  snd_change_.Play(true);
  PositionCursor();
}

void ScreenTextEntryVisual::MoveY(int iDir) {
  std::string sKey;
  do {
    focus_y_ = enum_add2(focus_y_, +iDir);
    wrap(*ConvertValue<int>(&focus_y_), NUM_KeyboardRow);

    // HACK: Round to nearest option so that we always stop
    // on KEYBOARD_ROW_SPECIAL.
    if (focus_y_ == KEYBOARD_ROW_SPECIAL) {
      for (int i = 0; true; i++) {
        sKey = g_szKeys[focus_y_][focus_x_];
        if (sKey != "") {
          break;
        }

        // UGLY: Probe one space to the left before looking to the right
        focus_x_ += (i == 0) ? -1 : +1;
        wrap(focus_x_, KEYS_PER_ROW);
      }
    }

    sKey = g_szKeys[focus_y_][focus_x_];
  } while (sKey == "");

  snd_change_.Play(true);
  PositionCursor();
}

bool ScreenTextEntryVisual::MenuLeft(const InputEventPlus& input) {
  if (input.type_ != IET_FIRST_PRESS) {
    return false;
  }
  MoveX(-1);
  return true;
}
bool ScreenTextEntryVisual::MenuRight(const InputEventPlus& input) {
  if (input.type_ != IET_FIRST_PRESS) {
    return false;
  }
  MoveX(+1);
  return true;
}
bool ScreenTextEntryVisual::MenuUp(const InputEventPlus& input) {
  if (input.type_ != IET_FIRST_PRESS) {
    return false;
  }
  MoveY(-1);
  return true;
}
bool ScreenTextEntryVisual::MenuDown(const InputEventPlus& input) {
  if (input.type_ != IET_FIRST_PRESS) {
    return false;
  }
  MoveY(+1);
  return true;
}

bool ScreenTextEntryVisual::MenuStart(const InputEventPlus& input) {
  if (input.type_ != IET_FIRST_PRESS) {
    return false;
  }
  if (focus_y_ == KEYBOARD_ROW_SPECIAL) {
    switch (focus_x_) {
      case SPACEBAR:
        TryAppendToAnswer(" ");
        break;
      case BACKSPACE:
        BackspaceInAnswer();
        break;
      case CANCEL:
        End(true);
        break;
      case DONE:
        End(false);
        break;
      default:
        return false;
    }
  } else {
    TryAppendToAnswer(g_szKeys[focus_y_][focus_x_]);
  }
  return true;
}

/*
 * (c) 2001-2004 Chris Danford
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
