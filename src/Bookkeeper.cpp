#include "Bookkeeper.h"

#include <cstring>
#include <ctime>
#include <map>
#include <memory>
#include <string>

#include "DateTime.h"
#include "GameState.h"
#include "IniFile.h"
#include "PrefsManager.h"
#include "RageFile.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "SpecialFiles.h"
#include "XmlFile.h"
#include "XmlFileUtil.h"

Bookkeeper* BOOKKEEPER =
    nullptr;  // global and accessible from anywhere in our program

static const std::string kCoinsDat = "Save/Coins.xml";

Bookkeeper::Bookkeeper() {
  // GameState resets the coin count, but once the Bookkeeper is initialized,
  // the coin count is overwritten by the ReadFromDisk function. Resetting the
  // CoinsFile before reloading allows the game to start without credits.
  if (PREFSMAN->reset_coins_at_startup_) {
    WriteCoinsFile(0);
  }

  ClearAll();
  ReadFromDisk();
}

Bookkeeper::~Bookkeeper() { WriteToDisk(); }

#define WARN_AND_RETURN                                      \
  {                                                          \
    LOG->Warn("Error parsing at %s:%d", __FILE__, __LINE__); \
    return;                                                  \
  }

void Bookkeeper::ClearAll() { map_coins_for_hour_.clear(); }

bool Bookkeeper::Date::operator<(const Date& rhs) const {
  if (year_ != rhs.year_) {
    return year_ < rhs.year_;
  }
  if (day_of_year_ != rhs.day_of_year_) {
    return day_of_year_ < rhs.day_of_year_;
  }
  return hour_ < rhs.hour_;
}

void Bookkeeper::Date::Set(time_t t) {
  tm ltime;
  localtime_r(&t, &ltime);

  Set(ltime);
}

void Bookkeeper::Date::Set(tm time) {
  hour_ = time.tm_hour;
  day_of_year_ = time.tm_yday;
  year_ = time.tm_year + 1900;
}

void Bookkeeper::LoadFromNode(const XNode* node) {
  if (node->GetName() != "Bookkeeping") {
    LOG->Warn(
        "Error loading bookkeeping: unexpected \"%s\"",
        node->GetName().c_str());
    return;
  }

  const XNode* data = node->GetChild("Data");
  if (data == nullptr) {
    LOG->Warn("Error loading bookkeeping: Data node missing");
    return;
  }

  FOREACH_CONST_Child(data, day) {
    Date d;
    if (!day->GetAttrValue("Hour", d.hour_) ||
        !day->GetAttrValue("Day", d.day_of_year_) ||
        !day->GetAttrValue("Year", d.year_)) {
      LOG->Warn("Incomplete date field");
      continue;
    }

    int coins;
    day->GetTextValue(coins);

    map_coins_for_hour_[d] = coins;
  }
}

XNode* Bookkeeper::CreateNode() const {
  XNode* xml = new XNode("Bookkeeping");

  {
    XNode* data = xml->AppendChild("Data");

    for (std::map<Date, int>::const_iterator it = map_coins_for_hour_.begin();
         it != map_coins_for_hour_.end(); ++it) {
      int coins = it->second;
      XNode* day = data->AppendChild("Coins", coins);

      const Date& d = it->first;
      day->AppendAttr("Hour", d.hour_);
      day->AppendAttr("Day", d.day_of_year_);
      day->AppendAttr("Year", d.year_);
    }
  }

  return xml;
}

void Bookkeeper::ReadFromDisk() {
  if (!IsAFile(kCoinsDat)) {
    return;
  }

  XNode xml;
  if (!XmlFileUtil::LoadFromFileShowErrors(xml, kCoinsDat)) {
    return;
  }

  int num_coins = 0;
  ReadCoinsFile(num_coins);

  if (num_coins < 0) {
    num_coins = 0;
  } else if (
      num_coins / PREFSMAN->coins_per_credit_ > PREFSMAN->max_num_credits_) {
    num_coins = 0;
  }

  LOG->Trace("Number of Coins to Load on boot: %i", num_coins);
  GAMESTATE->coins_.Set(num_coins);

  LoadFromNode(&xml);
}

void Bookkeeper::WriteToDisk() {
  // Write data. Use SLOW_FLUSH, to help ensure that we don't lose coin data.
  RageFile f;
  if (!f.Open(kCoinsDat, RageFile::WRITE | RageFile::SLOW_FLUSH)) {
    LOG->Warn(
        "Couldn't open file \"%s\" for writing: %s", kCoinsDat.c_str(),
        f.GetError().c_str());
    return;
  }

  std::unique_ptr<XNode> xml(CreateNode());
  XmlFileUtil::SaveToFile(xml.get(), f);
}

void Bookkeeper::CoinInserted() {
  Date d;
  d.Set(time(nullptr));

  ++map_coins_for_hour_[d];
}

void Bookkeeper::WriteCoinsFile(int coins) {
  IniFile ini;
  ini.SetValue("Bookkeeping", "Coins", coins);
  ini.WriteFile(SpecialFiles::kCoinsIni);
}

void Bookkeeper::ReadCoinsFile(int& coins) {
  IniFile ini;
  ini.ReadFile(SpecialFiles::kCoinsIni);
  ini.GetValue("Bookkeeping", "Coins", coins);
}

// Return the number of coins between [beginning,ending).
int Bookkeeper::GetNumCoinsInRange(
    std::map<Date, int>::const_iterator begin,
    std::map<Date, int>::const_iterator end) const {
  int coins = 0;

  while (begin != end) {
    coins += begin->second;
    ++begin;
  }

  return coins;
}

int Bookkeeper::GetNumCoins(Date beginning, Date ending) const {
  return GetNumCoinsInRange(
      map_coins_for_hour_.lower_bound(beginning),
      map_coins_for_hour_.lower_bound(ending));
}

int Bookkeeper::GetCoinsTotal() const {
  return GetNumCoinsInRange(
      map_coins_for_hour_.begin(), map_coins_for_hour_.end());
}

void Bookkeeper::GetCoinsLastDays(int coins[kNumLastDays]) const {
  time_t old_time = time(nullptr);
  tm time;
  localtime_r(&old_time, &time);

  time.tm_hour = 0;

  for (int i = 0; i < kNumLastDays; i++) {
    tm end_time = AddDays(time, +1);
    coins[i] = GetNumCoins(time, end_time);
    time = GetYesterday(time);
  }
}

void Bookkeeper::GetCoinsLastWeeks(int coins[kNumLastWeeks]) const {
  time_t old_time = time(nullptr);
  tm time;
  localtime_r(&old_time, &time);

  time = GetNextSunday(time);
  time = GetYesterday(time);

  for (int w = 0; w < kNumLastWeeks; w++) {
    tm start_time = AddDays(time, -kDaysInWeek);
    coins[w] = GetNumCoins(start_time, time);
    time = start_time;
  }
}

/* iDay is days since Jan 1. iYear is eg. 2005. Return the day of the week,
 * where 0 is Sunday. */
void Bookkeeper::GetCoinsByDayOfWeek(int coins[kDaysInWeek]) const {
  for (int i = 0; i < kDaysInWeek; i++) {
    coins[i] = 0;
  }

  for (std::map<Date, int>::const_iterator it = map_coins_for_hour_.begin();
       it != map_coins_for_hour_.end(); ++it) {
    const Date& d = it->first;
    int day_of_week = GetDayInYearAndYear(d.day_of_year_, d.year_).tm_wday;
    coins[day_of_week] += it->second;
  }
}

void Bookkeeper::GetCoinsByHour(int coins[kHoursInDay]) const {
  memset(coins, 0, sizeof(int) * kHoursInDay);
  for (std::map<Date, int>::const_iterator it = map_coins_for_hour_.begin();
       it != map_coins_for_hour_.end(); ++it) {
    const Date& d = it->first;

    if (d.hour_ >= kHoursInDay) {
      LOG->Warn("Hour %i >= %i", d.hour_, kHoursInDay);
      continue;
    }

    coins[d.hour_] += it->second;
  }
}

/*
 * (c) 2003-2005 Chris Danford, Glenn Maynard
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
