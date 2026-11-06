#include "ScreenBookkeeping.h"

#include <string>
#include <vector>

#include "Actor.h"
#include "ActorUtil.h"
#include "Bookkeeper.h"
#include "DateTime.h"
#include "EnumHelper.h"
#include "InputEventPlus.h"
#include "InputFilter.h"
#include "LocalizedString.h"
#include "Profile.h"
#include "ProfileManager.h"
#include "RageUtil.h"
#include "Screen.h"
#include "ScreenDimensions.h"
#include "ScreenManager.h"
#include "ScreenMessage.h"
#include "ScreenWithMenuElements.h"
#include "Song.h"
#include "SongManager.h"
#include "SongUtil.h"
#include "StdString.h"
#include "ThemeManager.h"
#include "UnlockManager.h"
#include "global.h"

static const char* BookkeepingViewNames[] = {
    "SongPlays", "LastDays", "LastWeeks", "DayOfWeek", "HourOfDay",
};
XToString(BookkeepingView);

REGISTER_SCREEN_CLASS(ScreenBookkeeping);

void ScreenBookkeeping::Init() {
  ScreenWithMenuElements::Init();

  text_all_time_.LoadFromFont(THEME->GetPathF(name_, "AllTime"));
  text_all_time_.SetName("AllTime");
  LOAD_ALL_COMMANDS_AND_SET_XY(text_all_time_);
  this->AddChild(&text_all_time_);

  text_title_.LoadFromFont(THEME->GetPathF(name_, "title"));
  text_title_.SetName("Title");
  LOAD_ALL_COMMANDS_AND_SET_XY(text_title_);
  this->AddChild(&text_title_);

  for (int i = 0; i < NUM_BOOKKEEPING_COLS; i++) {
    text_data_[i].LoadFromFont(THEME->GetPathF(name_, "data"));
    text_data_[i].SetName("Data");
    LOAD_ALL_COMMANDS_AND_SET_XY(text_data_[i]);
    float fX = SCALE(
        i, 0.f, NUM_BOOKKEEPING_COLS - 1, SCREEN_LEFT + 50, SCREEN_RIGHT - 160);
    text_data_[i].SetX(fX);
    this->AddChild(&text_data_[i]);
  }

  FOREACH_ENUM(BookkeepingView, i) {
    if (THEME->GetMetricB(name_, "Show" + BookkeepingViewToString(i))) {
      bookkeeping_views_.push_back(i);
    }
  }

  view_index_ = 0;

  UpdateView();
}

void ScreenBookkeeping::Update(float fDelta) {
  ScreenWithMenuElements::Update(fDelta);
}

bool ScreenBookkeeping::Input(const InputEventPlus& input) {
  if (input.type_ != IET_FIRST_PRESS && input.type_ != IET_REPEAT) {
    return false;  // ignore
  }

  return Screen::Input(input);  // default handler
}

bool ScreenBookkeeping::MenuLeft(const InputEventPlus& input) {
  view_index_--;
  rage_clamp(view_index_, 0, bookkeeping_views_.size() - 1);

  UpdateView();
  return true;
}

bool ScreenBookkeeping::MenuRight(const InputEventPlus& input) {
  view_index_++;
  rage_clamp(view_index_, 0, bookkeeping_views_.size() - 1);

  UpdateView();
  return true;
}

bool ScreenBookkeeping::MenuStart(const InputEventPlus& input) {
  if (IsTransitioning()) {
    return false;
  }

  SCREENMAN->PlayStartSound();
  StartTransitioningScreen(SM_GoToNextScreen);
  return true;
}

bool ScreenBookkeeping::MenuBack(const InputEventPlus& input) {
  if (IsTransitioning()) {
    return false;
  }

  SCREENMAN->PlayStartSound();
  StartTransitioningScreen(SM_GoToPrevScreen);
  return true;
}

bool ScreenBookkeeping::MenuCoin(const InputEventPlus& input) {
  UpdateView();

  return Screen::MenuCoin(input);
}

static LocalizedString ALL_TIME("ScreenBookkeeping", "All-time Coin Total:");
static LocalizedString SONG_PLAYS("ScreenBookkeeping", "Total Song Plays: %d");
static LocalizedString LAST_DAYS(
    "ScreenBookkeeping", "Coin Data of Last %d Days");
static LocalizedString LAST_WEEKS(
    "ScreenBookkeeping", "Coin Data of Last %d Weeks");
static LocalizedString DAY_OF_WEEK(
    "ScreenBookkeeping", "Coin Data by Day of Week, All-Time");
static LocalizedString HOUR_OF_DAY(
    "ScreenBookkeeping", "Coin Data by Hour of Day, All-Time");
void ScreenBookkeeping::UpdateView() {
  BookkeepingView view = bookkeeping_views_[view_index_];

  {
    std::string s;
    s += ALL_TIME.GetValue();
    s += ssprintf(" %i\n", BOOKKEEPER->GetCoinsTotal());
    text_all_time_.SetText(s);
  }

  switch (view) {
    case BookkeepingView_SongPlays: {
      Profile* pProfile = PROFILEMAN->GetMachineProfile();

      std::vector<Song*> vpSongs;
      int iCount = 0;
      for (Song* pSong : SONGMAN->GetAllSongs()) {
        if (UNLOCKMAN->SongIsLocked(pSong) & ~LOCKED_DISABLED) {
          continue;
        }
        iCount += pProfile->GetSongNumTimesPlayed(pSong);
        vpSongs.push_back(pSong);
      }
      text_title_.SetText(ssprintf(SONG_PLAYS.GetValue().c_str(), iCount));
      SongUtil::SortSongPointerArrayByNumPlays(vpSongs, pProfile, true);

      const int iSongPerCol = 15;

      int iSongIndex = 0;
      for (int i = 0; i < NUM_BOOKKEEPING_COLS; i++) {
        std::string s;
        for (int j = 0; j < iSongPerCol; j++) {
          if (iSongIndex < (int)vpSongs.size()) {
            Song* pSong = vpSongs[iSongIndex];
            iCount = pProfile->GetSongNumTimesPlayed(pSong);
            std::string sTitle =
                ssprintf("%4d", iCount) + " " + pSong->GetDisplayFullTitle();
            if (sTitle.length() > 22) {
              sTitle = Left(sTitle, 20) + "...";
            }
            s += sTitle + "\n";
            iSongIndex++;
          }
        }
        text_data_[i].SetText(s);
        text_data_[i].SetHorizAlign(align_left);
      }
    } break;
    case BookkeepingView_LastDays: {
      text_title_.SetText(
          ssprintf(LAST_DAYS.GetValue().c_str(), NUM_LAST_DAYS));

      int coins[NUM_LAST_DAYS];
      BOOKKEEPER->GetCoinsLastDays(coins);
      int iTotalLast = 0;

      std::string sTitle, sData;
      for (int i = 0; i < NUM_LAST_DAYS; i++) {
        sTitle += LastDayToLocalizedString(i) + "\n";
        sData += ssprintf("%d", coins[i]) + "\n";
        iTotalLast += coins[i];
      }

      sTitle += ALL_TIME.GetValue() + "\n";
      sData += ssprintf("%i\n", iTotalLast);

      text_data_[0].SetText("");
      text_data_[1].SetHorizAlign(align_left);
      text_data_[1].SetText(sTitle);
      text_data_[2].SetText("");
      text_data_[3].SetHorizAlign(align_right);
      text_data_[3].SetText(sData);
    } break;
    case BookkeepingView_LastWeeks: {
      text_title_.SetText(
          ssprintf(LAST_WEEKS.GetValue().c_str(), NUM_LAST_WEEKS));

      int coins[NUM_LAST_WEEKS];
      BOOKKEEPER->GetCoinsLastWeeks(coins);

      std::string sTitle, sData;
      for (int col = 0; col < 4; col++) {
        std::string sTemp;
        for (int row = 0; row < 52 / 4; row++) {
          int week = row * 4 + col;
          sTemp += LastWeekToLocalizedString(week) +
                   ssprintf(": %d", coins[week]) + "\n";
        }

        text_data_[col].SetHorizAlign(align_left);
        text_data_[col].SetText(sTemp);
      }
    } break;
    case BookkeepingView_DayOfWeek: {
      text_title_.SetText(DAY_OF_WEEK);

      int coins[DAYS_IN_WEEK];
      BOOKKEEPER->GetCoinsByDayOfWeek(coins);

      std::string sTitle, sData;
      for (int i = 0; i < DAYS_IN_WEEK; i++) {
        sTitle += DayOfWeekToString(i) + "\n";
        sData += ssprintf("%d", coins[i]) + "\n";
      }

      text_data_[0].SetText("");
      text_data_[1].SetHorizAlign(align_left);
      text_data_[1].SetText(sTitle);
      text_data_[2].SetText("");
      text_data_[3].SetHorizAlign(align_right);
      text_data_[3].SetText(sData);
    } break;
    case BookkeepingView_HourOfDay: {
      text_title_.SetText(HOUR_OF_DAY);

      int coins[HOURS_IN_DAY];
      BOOKKEEPER->GetCoinsByHour(coins);

      std::string sTitle1, sData1;
      for (int i = 0; i < HOURS_IN_DAY / 2; i++) {
        sTitle1 += HourInDayToLocalizedString(i) + "\n";
        sData1 += ssprintf("%d", coins[i]) + "\n";
      }

      std::string sTitle2, sData2;
      for (int i = (HOURS_IN_DAY / 2); i < HOURS_IN_DAY; i++) {
        sTitle2 += HourInDayToLocalizedString(i) + "\n";
        sData2 += ssprintf("%d", coins[i]) + "\n";
      }

      text_data_[0].SetHorizAlign(align_left);
      text_data_[0].SetText(sTitle1);
      text_data_[1].SetHorizAlign(align_right);
      text_data_[1].SetText(sData1);
      text_data_[2].SetHorizAlign(align_left);
      text_data_[2].SetText(sTitle2);
      text_data_[3].SetHorizAlign(align_right);
      text_data_[3].SetText(sData2);
    } break;
    default:
      FAIL_M(ssprintf("Invalid BookkeepingView: %i", view));
  }
}

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
