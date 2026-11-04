#include "ScreenPrompt.h"

#include <string>

#include "ActorUtil.h"
#include "BitmapText.h"
#include "EnumHelper.h"
#include "GameSoundManager.h"
#include "InputEventPlus.h"
#include "InputFilter.h"
#include "RageInputDevice.h"
#include "RageUtil.h"
#include "Screen.h"
#include "ScreenManager.h"
#include "ScreenMessage.h"
#include "ScreenWithMenuElements.h"
#include "ThemeManager.h"
#include "global.h"

PromptAnswer ScreenPrompt::last_answer_ = ANSWER_YES;
bool ScreenPrompt::cancelled_last_ = false;

#define ANSWER_TEXT(s) THEME->GetString(name_, s)

static const char* PromptAnswerNames[] = {
    "Yes",
    "No",
    "Cancel",
};
XToString(PromptAnswer);

// Settings:
namespace {
std::string g_sText;
PromptType g_PromptType;
PromptAnswer g_defaultAnswer;
void (*g_pOnYes)(void*);
void (*g_pOnNo)(void*);
void* g_pCallbackData;
};  // namespace

void ScreenPrompt::SetPromptSettings(
    const std::string& sText, PromptType type, PromptAnswer defaultAnswer,
    void (*OnYes)(void*), void (*OnNo)(void*), void* pCallbackData) {
  g_sText = sText;
  g_PromptType = type;
  g_defaultAnswer = defaultAnswer;
  g_pOnYes = OnYes;
  g_pOnNo = OnNo;
  g_pCallbackData = pCallbackData;
}

void ScreenPrompt::Prompt(
    ScreenMessage smSendOnPop, const std::string& sText, PromptType type,
    PromptAnswer defaultAnswer, void (*OnYes)(void*), void (*OnNo)(void*),
    void* pCallbackData) {
  SetPromptSettings(sText, type, defaultAnswer, OnYes, OnNo, pCallbackData);

  SCREENMAN->AddNewScreenToTop("ScreenPrompt", smSendOnPop);
}

REGISTER_SCREEN_CLASS(ScreenPrompt);

void ScreenPrompt::Init() {
  ScreenWithMenuElements::Init();

  text_question_.LoadFromFont(THEME->GetPathF(name_, "question"));
  text_question_.SetName("Question");
  LOAD_ALL_COMMANDS(text_question_);
  this->AddChild(&text_question_);

  spr_cursor_.Load(THEME->GetPathG(name_, "cursor"));
  spr_cursor_->SetName("Cursor");
  LOAD_ALL_COMMANDS(spr_cursor_);
  this->AddChild(spr_cursor_);

  for (int i = 0; i < NUM_PromptAnswer; i++) {
    text_answer_[i].LoadFromFont(THEME->GetPathF(name_, "answer"));
    // The name of the actor isn't set because it is not known at this point
    // how many answers there will be, and the name depends on the number of
    // answers as a clumsy way of letting the themer set different positions
    // for different answer groups.  The name is set in BeginScreen, and
    // then the commands are loaded. -Kyz (At least, that seems like the
    // explanation to me, reading the code years after the author left)
    this->AddChild(&text_answer_[i]);
  }

  snd_change_.Load(THEME->GetPathS(name_, "change"), true);
}

void ScreenPrompt::BeginScreen() {
  ScreenWithMenuElements::BeginScreen();

  answer_ = g_defaultAnswer;
  enum_clamp(answer_, PromptAnswer(0), PromptAnswer(g_PromptType));

  text_question_.SetText(g_sText);
  SET_XY(text_question_);
  text_question_.PlayCommand("On");

  spr_cursor_->PlayCommand("On");

  for (int i = 0; i <= g_PromptType; i++) {
    std::string sElem = ssprintf("Answer%dOf%d", i + 1, g_PromptType + 1);
    text_answer_[i].SetName(sElem);
    LOAD_ALL_COMMANDS(text_answer_[i]);
    // Side note:  Because LOAD_ALL_COMMANDS occurs here, InitCommand will
    // not be run for the actors.  People can just use OnCommand instead.
    std::string sAnswer = PromptAnswerToString((PromptAnswer)i);
    // FRAGILE
    if (g_PromptType == PROMPT_OK) {
      sAnswer = "OK";
    }

    text_answer_[i].SetText(ANSWER_TEXT(sAnswer));
    text_answer_[i].PlayCommand("On");
    SET_XY(text_answer_[i]);
  }

  for (int i = g_PromptType + 1; i < NUM_PromptAnswer; i++) {
    text_answer_[i].SetText("");
  }

  PositionCursor();
}

bool ScreenPrompt::Input(const InputEventPlus& input) {
  if (IsTransitioning()) {
    return false;
  }

  if (input.type_ == IET_RELEASE) {
    return false;
  }

  if (input.device_i_.device_ == DEVICE_KEYBOARD) {
    switch (input.device_i_.button_) {
      case KEY_LEFT:
        return this->MenuLeft(input);
      case KEY_RIGHT:
        return this->MenuRight(input);
      default:
        break;
    }
  }

  return ScreenWithMenuElements::Input(input);
}

bool ScreenPrompt::CanGoRight() {
  switch (g_PromptType) {
    case PROMPT_OK:
      return false;
    case PROMPT_YES_NO:
      return answer_ < ANSWER_NO;
    case PROMPT_YES_NO_CANCEL:
      return answer_ < ANSWER_CANCEL;
    default:
      FAIL_M(ssprintf("Invalid PromptType: %i", g_PromptType));
  }
}

void ScreenPrompt::Change(int dir) {
  text_answer_[answer_].StopEffect();
  answer_ = (PromptAnswer)(answer_ + dir);
  ASSERT(answer_ >= 0 && answer_ < NUM_PromptAnswer);

  PositionCursor();

  snd_change_.Play(true);
}

bool ScreenPrompt::MenuLeft(const InputEventPlus& input) {
  if (CanGoLeft()) {
    Change(-1);
    return true;
  }
  return false;
}

bool ScreenPrompt::MenuRight(const InputEventPlus& input) {
  if (CanGoRight()) {
    Change(+1);
    return true;
  }
  return false;
}

bool ScreenPrompt::MenuStart(const InputEventPlus& input) {
  if (out_.IsTransitioning() || cancel_.IsTransitioning()) {
    return false;
  }

  End(false);
  return true;
}

bool ScreenPrompt::MenuBack(const InputEventPlus& input) {
  if (out_.IsTransitioning() || cancel_.IsTransitioning()) {
    return false;
  }

  switch (g_PromptType) {
    case PROMPT_OK:
    case PROMPT_YES_NO:
      // don't allow cancel
      break;
    case PROMPT_YES_NO_CANCEL:
      End(true);
      return true;
  }
  return false;
}

void ScreenPrompt::End(bool bCancelled) {
  switch (answer_) {
    case ANSWER_YES:
      send_on_pop_ = SM_Success;
      break;
    case ANSWER_NO:
      send_on_pop_ = SM_Failure;
      break;
    default:
      break;
  }

  if (bCancelled) {
    Cancel(SM_GoToNextScreen);
  } else {
    SCREENMAN->PlayStartSound();
    StartTransitioningScreen(SM_GoToNextScreen);
  }

  switch (answer_) {
    case ANSWER_YES:
      if (g_pOnYes) {
        g_pOnYes(g_pCallbackData);
      }
      break;
    case ANSWER_NO:
      if (g_pOnNo) {
        g_pOnNo(g_pCallbackData);
      }
      break;
    default:
      break;
  }

  last_answer_ = bCancelled ? ANSWER_CANCEL : answer_;
  cancelled_last_ = bCancelled;
}

void ScreenPrompt::PositionCursor() {
  BitmapText& bt = text_answer_[answer_];
  spr_cursor_->SetXY(bt.GetX(), bt.GetY());
}

void ScreenPrompt::TweenOffScreen() {
  text_question_.PlayCommand("Off");
  spr_cursor_->PlayCommand("Off");
  for (int i = 0; i <= g_PromptType; i++) {
    text_answer_[i].PlayCommand("Off");
  }

  ScreenWithMenuElements::TweenOffScreen();
}

// lua start
/*
#include "LuaBinding.h"

 // use proper doxygen when lua is enabled.
 @brief Allow Lua to have access to the ScreenPrompt.
class LunaScreenPrompt: public Luna<ScreenPrompt>
{
public:

        LunaScreenPrompt()
        {

        }
};
LUA_REGISTER_DERIVED_CLASS( ScreenPrompt, ScreenWithMenuElements )
*/
// lua end

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
