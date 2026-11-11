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

const float kX[NUM_SetTimeSelection] = {320, 320, 320, 320, 320, 320};

const float kY[NUM_SetTimeSelection] = {140, 180, 220, 260, 300, 340};

static float GetTitleX(SetTimeSelection s) { return kX[s] - 80; }
static float GetTitleY(SetTimeSelection s) { return kY[s]; }
static float GetValueX(SetTimeSelection s) { return kX[s] + 80; }
static float GetValueY(SetTimeSelection s) { return kY[s]; }

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

void ScreenSetTime::Update(float delta) {
  Screen::Update(delta);

  time_t i_now = time(nullptr);
  i_now += time_offset_;

  tm now;
  localtime_r(&i_now, &now);

  int i_pretty_hour = now.tm_hour % 12;
  if (i_pretty_hour == 0) {
    i_pretty_hour = 12;
  }
  std::string s_pretty_hour =
      ssprintf("%d %s", i_pretty_hour, now.tm_hour >= 12 ? "pm" : "am");

  text_value_[hour].SetText(s_pretty_hour);
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

void ScreenSetTime::ChangeValue(int direction) {
  time_t now = time(nullptr);
  time_t i_adjusted = now + time_offset_;

  tm adjusted;
  localtime_r(&i_adjusted, &adjusted);

  // tm now = GetLocalTime();
  switch (selection_) {
    case hour:
      adjusted.tm_hour += direction;
      break;
    case minute:
      adjusted.tm_min += direction;
      break;
    case second:
      adjusted.tm_sec += direction;
      break;
    case year:
      adjusted.tm_year += direction;
      break;
    case month:
      adjusted.tm_mon += direction;
      break;
    case day:
      adjusted.tm_mday += direction;
      break;
    default:
      FAIL_M(ssprintf("Invalid SetTimeSelection: %i", selection_));
  }

  /* Normalize: */
  i_adjusted = mktime(&adjusted);

  time_offset_ = i_adjusted - now;

  sound_change_value_.Play(true);
}

void ScreenSetTime::ChangeSelection(int direction) {
  // set new value of m_Selection
  SetTimeSelection old_selection = selection_;
  enum_add<SetTimeSelection>(selection_, direction);

  enum_clamp(
      selection_, SetTimeSelection(0),
      SetTimeSelection(NUM_SetTimeSelection - 1));
  if (direction != 0 && selection_ == old_selection) {
    return;  // can't move any more
  }

  text_value_[old_selection].StopEffect();
  text_value_[selection_].SetEffectDiffuseShift(
      1.f, RageColor(0.3f, 0.3f, 0.3f, 1), RageColor(1, 1, 1, 1));

  if (direction != 0) {
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
  bool holding_left_and_right =
      INPUTMAPPER->IsBeingPressed(GAME_BUTTON_RIGHT, input.pn_) &&
      INPUTMAPPER->IsBeingPressed(GAME_BUTTON_LEFT, input.pn_);

  if (holding_left_and_right) {
    ChangeSelection(-1);
  } else if (selection_ == NUM_SetTimeSelection - 1)  // last row
  {
    /* Save the new time. */
    time_t now = time(nullptr);
    time_t i_adjusted = now + time_offset_;

    tm adjusted;
    localtime_r(&i_adjusted, &adjusted);

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
