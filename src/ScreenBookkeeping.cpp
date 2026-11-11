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

  for (int i = 0; i < kNumBookkeepingCols; i++) {
    text_data_[i].LoadFromFont(THEME->GetPathF(name_, "data"));
    text_data_[i].SetName("Data");
    LOAD_ALL_COMMANDS_AND_SET_XY(text_data_[i]);
    float x = SCALE(
        i, 0.f, kNumBookkeepingCols - 1, SCREEN_LEFT + 50, SCREEN_RIGHT - 160);
    text_data_[i].SetX(x);
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

void ScreenBookkeeping::Update(float delta) {
  ScreenWithMenuElements::Update(delta);
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
      Profile* profile = PROFILEMAN->GetMachineProfile();

      std::vector<Song*> songs;
      int count = 0;
      for (Song* song : SONGMAN->GetAllSongs()) {
        if (UNLOCKMAN->SongIsLocked(song) & ~LOCKED_DISABLED) {
          continue;
        }
        count += profile->GetSongNumTimesPlayed(song);
        songs.push_back(song);
      }
      text_title_.SetText(ssprintf(SONG_PLAYS.GetValue().c_str(), count));
      SongUtil::SortSongPointerArrayByNumPlays(songs, profile, true);

      const int song_per_col = 15;

      int song_index = 0;
      for (int i = 0; i < kNumBookkeepingCols; i++) {
        std::string s;
        for (int j = 0; j < song_per_col; j++) {
          if (song_index < (int)songs.size()) {
            Song* song = songs[song_index];
            count = profile->GetSongNumTimesPlayed(song);
            std::string title =
                ssprintf("%4d", count) + " " + song->GetDisplayFullTitle();
            if (title.length() > 22) {
              title = Left(title, 20) + "...";
            }
            s += title + "\n";
            song_index++;
          }
        }
        text_data_[i].SetText(s);
        text_data_[i].SetHorizAlign(kAlignLeft);
      }
    } break;
    case BookkeepingView_LastDays: {
      text_title_.SetText(ssprintf(LAST_DAYS.GetValue().c_str(), kNumLastDays));

      int coins[kNumLastDays];
      BOOKKEEPER->GetCoinsLastDays(coins);
      int total_last = 0;

      std::string title, data;
      for (int i = 0; i < kNumLastDays; i++) {
        title += LastDayToLocalizedString(i) + "\n";
        data += ssprintf("%d", coins[i]) + "\n";
        total_last += coins[i];
      }

      title += ALL_TIME.GetValue() + "\n";
      data += ssprintf("%i\n", total_last);

      text_data_[0].SetText("");
      text_data_[1].SetHorizAlign(kAlignLeft);
      text_data_[1].SetText(title);
      text_data_[2].SetText("");
      text_data_[3].SetHorizAlign(kAlignRight);
      text_data_[3].SetText(data);
    } break;
    case BookkeepingView_LastWeeks: {
      text_title_.SetText(
          ssprintf(LAST_WEEKS.GetValue().c_str(), kNumLastWeeks));

      int coins[kNumLastWeeks];
      BOOKKEEPER->GetCoinsLastWeeks(coins);

      std::string title, data;
      for (int col = 0; col < 4; col++) {
        std::string temp;
        for (int row = 0; row < 52 / 4; row++) {
          int week = row * 4 + col;
          temp += LastWeekToLocalizedString(week) +
                  ssprintf(": %d", coins[week]) + "\n";
        }

        text_data_[col].SetHorizAlign(kAlignLeft);
        text_data_[col].SetText(temp);
      }
    } break;
    case BookkeepingView_DayOfWeek: {
      text_title_.SetText(DAY_OF_WEEK);

      int coins[kDaysInWeek];
      BOOKKEEPER->GetCoinsByDayOfWeek(coins);

      std::string title, data;
      for (int i = 0; i < kDaysInWeek; i++) {
        title += DayOfWeekToString(i) + "\n";
        data += ssprintf("%d", coins[i]) + "\n";
      }

      text_data_[0].SetText("");
      text_data_[1].SetHorizAlign(kAlignLeft);
      text_data_[1].SetText(title);
      text_data_[2].SetText("");
      text_data_[3].SetHorizAlign(kAlignRight);
      text_data_[3].SetText(data);
    } break;
    case BookkeepingView_HourOfDay: {
      text_title_.SetText(HOUR_OF_DAY);

      int coins[kHoursInDay];
      BOOKKEEPER->GetCoinsByHour(coins);

      std::string title1, data1;
      for (int i = 0; i < kHoursInDay / 2; i++) {
        title1 += HourInDayToLocalizedString(i) + "\n";
        data1 += ssprintf("%d", coins[i]) + "\n";
      }

      std::string title2, data2;
      for (int i = (kHoursInDay / 2); i < kHoursInDay; i++) {
        title2 += HourInDayToLocalizedString(i) + "\n";
        data2 += ssprintf("%d", coins[i]) + "\n";
      }

      text_data_[0].SetHorizAlign(kAlignLeft);
      text_data_[0].SetText(title1);
      text_data_[1].SetHorizAlign(kAlignRight);
      text_data_[1].SetText(data1);
      text_data_[2].SetHorizAlign(kAlignLeft);
      text_data_[2].SetText(title2);
      text_data_[3].SetHorizAlign(kAlignRight);
      text_data_[3].SetText(data2);
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
