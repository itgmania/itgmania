#include "ScreenSetTime.h"

#include <ctime>
#include <string>

#include "BitmapText.h"
#include "DateTime.h"
#include "EnumHelper.h"
#include "GameInput.h"
#include "InputEventPlus.h"
#include "InputFilter.h"
#include "InputMapper.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "Screen.h"
#include "ScreenManager.h"
#include "ScreenMessage.h"
#include "ScreenWithMenuElements.h"
#include "ThemeManager.h"
#include "Tween.h"
#include "arch/ArchHooks/ArchHooks.h"
#include "global.h"

static const char* SetTimeSelectionNames[] = {
    "Year", "Month", "Day", "Hour", "Minute", "Second",
};
XToString(SetTimeSelection);
/** @brief A foreach iterator through the time selections. */
#define FOREACH_SetTimeSelection(s) FOREACH_ENUM(SetTimeSelection, s)

const float g_X[NUM_SetTimeSelection] = {320, 320, 320, 320, 320, 320};

const float g_Y[NUM_SetTimeSelection] = {140, 180, 220, 260, 300, 340};

static float GetTitleX(SetTimeSelection s) { return g_X[s] - 80; }
static float GetTitleY(SetTimeSelection s) { return g_Y[s]; }
static float GetValueX(SetTimeSelection s) { return g_X[s] + 80; }
static float GetValueY(SetTimeSelection s) { return g_Y[s]; }

REGISTER_SCREEN_CLASS(ScreenSetTime);

void ScreenSetTime::Init() {
  ScreenWithMenuElements::Init();

  selection_ = hour;

  FOREACH_SetTimeSelection(s) {
    BitmapText& title = text_title_[s];
    BitmapText& value = text_value_[s];

    title.LoadFromFont(THEME->GetPathF("Common", "title"));
    title.SetDiffuse(RageColor(1, 1, 1, 1));
    title.SetText(SetTimeSelectionToString(s));
    title.SetXY(GetTitleX(s), GetTitleY(s));
    this->AddChild(&title);

    title.SetDiffuse(RageColor(1, 1, 1, 0));
    title.BeginTweening(0.3f, TWEEN_LINEAR);
    title.SetDiffuse(RageColor(1, 1, 1, 1));

    value.LoadFromFont(THEME->GetPathF("Common", "normal"));
    value.SetDiffuse(RageColor(1, 1, 1, 1));
    value.SetXY(GetValueX(s), GetValueY(s));
    this->AddChild(&value);

    value.SetDiffuse(RageColor(1, 1, 1, 0));
    value.BeginTweening(0.3f, TWEEN_LINEAR);
    value.SetDiffuse(RageColor(1, 1, 1, 1));
  }

  sound_change_selection_.Load(
      THEME->GetPathS("ScreenSetTime", "ChangeSelection"));
  sound_change_value_.Load(THEME->GetPathS("ScreenSetTime", "ChangeValue"));

  time_offset_ = 0;
  selection_ = (SetTimeSelection)0;
  ChangeSelection(0);
}

void ScreenSetTime::Update(float fDelta) {
  Screen::Update(fDelta);

  time_t iNow = time(nullptr);
  iNow += time_offset_;

  tm now;
  localtime_r(&iNow, &now);

  int iPrettyHour = now.tm_hour % 12;
  if (iPrettyHour == 0) {
    iPrettyHour = 12;
  }
  std::string sPrettyHour =
      ssprintf("%d %s", iPrettyHour, now.tm_hour >= 12 ? "pm" : "am");

  text_value_[hour].SetText(sPrettyHour);
  text_value_[minute].SetText(ssprintf("%02d", now.tm_min));
  text_value_[second].SetText(ssprintf("%02d", now.tm_sec));
  text_value_[year].SetText(ssprintf("%02d", now.tm_year + 1900));
  text_value_[month].SetText(MonthToString((Month)now.tm_mon));
  text_value_[day].SetText(ssprintf("%02d", now.tm_mday));
}

bool ScreenSetTime::Input(const InputEventPlus& input) {
  if (input.type_ != IET_FIRST_PRESS && input.type_ != IET_REPEAT) {
    return false;  // ignore
  }

  if (IsTransitioning()) {
    return false;
  }

  return Screen::Input(input);  // default handler
}

void ScreenSetTime::ChangeValue(int iDirection) {
  time_t iNow = time(nullptr);
  time_t iAdjusted = iNow + time_offset_;

  tm adjusted;
  localtime_r(&iAdjusted, &adjusted);

  // tm now = GetLocalTime();
  switch (selection_) {
    case hour:
      adjusted.tm_hour += iDirection;
      break;
    case minute:
      adjusted.tm_min += iDirection;
      break;
    case second:
      adjusted.tm_sec += iDirection;
      break;
    case year:
      adjusted.tm_year += iDirection;
      break;
    case month:
      adjusted.tm_mon += iDirection;
      break;
    case day:
      adjusted.tm_mday += iDirection;
      break;
    default:
      FAIL_M(ssprintf("Invalid SetTimeSelection: %i", selection_));
  }

  /* Normalize: */
  iAdjusted = mktime(&adjusted);

  time_offset_ = iAdjusted - iNow;

  sound_change_value_.Play(true);
}

void ScreenSetTime::ChangeSelection(int iDirection) {
  // set new value of m_Selection
  SetTimeSelection OldSelection = selection_;
  enum_add<SetTimeSelection>(selection_, iDirection);

  enum_clamp(
      selection_, SetTimeSelection(0),
      SetTimeSelection(NUM_SetTimeSelection - 1));
  if (iDirection != 0 && selection_ == OldSelection) {
    return;  // can't move any more
  }

  text_value_[OldSelection].StopEffect();
  text_value_[selection_].SetEffectDiffuseShift(
      1.f, RageColor(0.3f, 0.3f, 0.3f, 1), RageColor(1, 1, 1, 1));

  if (iDirection != 0) {
    sound_change_selection_.Play(true);
  }
}

bool ScreenSetTime::MenuUp(const InputEventPlus& input) {
  ChangeSelection(-1);
  return true;
}

bool ScreenSetTime::MenuDown(const InputEventPlus& input) {
  ChangeSelection(+1);
  return true;
}

bool ScreenSetTime::MenuLeft(const InputEventPlus& input) {
  ChangeValue(-1);
  return true;
}

bool ScreenSetTime::MenuRight(const InputEventPlus& input) {
  ChangeValue(+1);
  return true;
}

bool ScreenSetTime::MenuStart(const InputEventPlus& input) {
  bool bHoldingLeftAndRight =
      INPUTMAPPER->IsBeingPressed(GAME_BUTTON_RIGHT, input.pn_) &&
      INPUTMAPPER->IsBeingPressed(GAME_BUTTON_LEFT, input.pn_);

  if (bHoldingLeftAndRight) {
    ChangeSelection(-1);
  } else if (selection_ == NUM_SetTimeSelection - 1)  // last row
  {
    /* Save the new time. */
    time_t iNow = time(nullptr);
    time_t iAdjusted = iNow + time_offset_;

    tm adjusted;
    localtime_r(&iAdjusted, &adjusted);

    HOOKS->SetTime(adjusted);

    /* We're going to draw a little more while we transition out.  We've already
     * set the new time; don't over-adjust visually. */
    time_offset_ = 0;

    FOREACH_SetTimeSelection(s) {
      BitmapText& title = text_title_[s];
      title.BeginTweening(0.3f, TWEEN_LINEAR);
      title.SetDiffuse(RageColor(1, 1, 1, 0));

      BitmapText& value = text_value_[s];
      value.BeginTweening(0.3f, TWEEN_LINEAR);
      value.SetDiffuse(RageColor(1, 1, 1, 0));
    }

    SCREENMAN->PlayStartSound();
    StartTransitioningScreen(SM_GoToNextScreen);
  } else {
    ChangeSelection(+1);
  }
  return true;
}

bool ScreenSetTime::MenuSelect(const InputEventPlus& input) {
  ChangeSelection(-1);
  return true;
}

bool ScreenSetTime::MenuBack(const InputEventPlus& input) {
  StartTransitioningScreen(SM_GoToPrevScreen);
  return true;
}

/*
 * (c) 2004 Chris Danford
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
