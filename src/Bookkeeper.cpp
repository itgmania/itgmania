#include "global.h"

#include "Bookkeeper.h"

#include <ctime>

#include "GameConstantsAndTypes.h"
#include "GameState.h"
#include "IniFile.h"
#include "RageFile.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "SongManager.h"
#include "SpecialFiles.h"
#include "XmlFile.h"
#include "XmlFileUtil.h"

// Global and accessible from anywhere in our program
Bookkeeper* BOOKKEEPER = nullptr;

static const RString COINS_DAT = "Save/Coins.xml";

Bookkeeper::Bookkeeper() {
  ClearAll();
  ReadFromDisk();
}

Bookkeeper::~Bookkeeper() { WriteToDisk(); }

#define WARN_AND_RETURN                                      \
  {                                                          \
    LOG->Warn("Error parsing at %s:%d", __FILE__, __LINE__); \
    return;                                                  \
  }

void Bookkeeper::ClearAll() { coins_for_hour_.clear(); }

bool Bookkeeper::Date::operator<(const Date& rhs) const {
  if (year != rhs.year) {
    return year < rhs.year;
  }
  if (day_of_year != rhs.day_of_year) {
    return day_of_year < rhs.day_of_year;
  }
  return hour < rhs.hour;
}

void Bookkeeper::Date::Set(time_t t) {
  tm local_time;
  localtime_r(&t, &local_time);

  Set(local_time);
}

void Bookkeeper::Date::Set(tm time) {
  hour = time.tm_hour;
  day_of_year = time.tm_yday;
  year = time.tm_year + 1900;
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
    if (!day->GetAttrValue("Hour", d.hour) ||
        !day->GetAttrValue("Day", d.day_of_year) ||
        !day->GetAttrValue("Year", d.year)) {
      LOG->Warn("Incomplete date field");
      continue;
    }

    int iCoins;
    day->GetTextValue(iCoins);

    coins_for_hour_[d] = iCoins;
  }
}

XNode* Bookkeeper::CreateNode() const {
  XNode* xml = new XNode("Bookkeeping");
  XNode* data = xml->AppendChild("Data");

  for (auto it = coins_for_hour_.begin(); it != coins_for_hour_.end(); ++it) {
    int coins = it->second;
    XNode* day = data->AppendChild("Coins", coins);

    const Date& date = it->first;
    day->AppendAttr("Hour", date.hour);
    day->AppendAttr("Day", date.day_of_year);
    day->AppendAttr("Year", date.year);
  }

  return xml;
}

void Bookkeeper::ReadFromDisk() {
  if (!IsAFile(COINS_DAT)) {
    return;
  }

  XNode xml;
  if (!XmlFileUtil::LoadFromFileShowErrors(xml, COINS_DAT)) {
    return;
  }

  int num_coins = 0;
  ReadCoinsFile(num_coins);

  if (num_coins < 0) {
    num_coins = 0;
  } else if (
      num_coins / PREFSMAN->m_iCoinsPerCredit > PREFSMAN->m_iMaxNumCredits) {
    num_coins = 0;
  }

  LOG->Trace("Number of Coins to Load on boot: %i", num_coins);
  GAMESTATE->m_iCoins.Set(num_coins);

  LoadFromNode(&xml);
}

void Bookkeeper::WriteToDisk() {
  // Write data. Use SLOW_FLUSH, to help ensure that we don't lose coin data.
  RageFile f;
  if (!f.Open(COINS_DAT, RageFile::WRITE | RageFile::SLOW_FLUSH)) {
    LOG->Warn(
        "Couldn't open file \"%s\" for writing: %s", COINS_DAT.c_str(),
        f.GetError().c_str());
    return;
  }

  std::unique_ptr<XNode> xml(CreateNode());
  XmlFileUtil::SaveToFile(xml.get(), f);
}

void Bookkeeper::CoinInserted() {
  Date date;
  date.Set(time(nullptr));

  ++coins_for_hour_[date];
}

void Bookkeeper::WriteCoinsFile(int coins) {
  IniFile ini;
  ini.SetValue("Bookkeeping", "Coins", coins);
  ini.WriteFile(SpecialFiles::COINS_INI);
}

void Bookkeeper::ReadCoinsFile(int& coins) {
  IniFile ini;
  ini.ReadFile(SpecialFiles::COINS_INI);
  ini.GetValue("Bookkeeping", "Coins", coins);
}

// Return the number of coins between [beginning, ending).
int Bookkeeper::GetNumCoinsInRange(
    std::map<Date, int>::const_iterator begin,
    std::map<Date, int>::const_iterator end) const {
  int iCoins = 0;

  while (begin != end) {
    iCoins += begin->second;
    ++begin;
  }

  return iCoins;
}

int Bookkeeper::GetNumCoins(Date beginning, Date ending) const {
  return GetNumCoinsInRange(
      coins_for_hour_.lower_bound(beginning),
      coins_for_hour_.lower_bound(ending));
}

int Bookkeeper::GetCoinsTotal() const {
  return GetNumCoinsInRange(coins_for_hour_.begin(), coins_for_hour_.end());
}

void Bookkeeper::GetCoinsLastDays(int coins[NUM_LAST_DAYS]) const {
  time_t old_time = time(nullptr);
  tm time;
  localtime_r(&old_time, &time);

  time.tm_hour = 0;

  for (int i = 0; i < NUM_LAST_DAYS; ++i) {
    tm end_time = AddDays(time, +1);
    coins[i] = GetNumCoins(time, end_time);
    time = GetYesterday(time);
  }
}

void Bookkeeper::GetCoinsLastWeeks(int coins[NUM_LAST_WEEKS]) const {
  time_t old_time = time(nullptr);
  tm time;
  localtime_r(&old_time, &time);

  time = GetNextSunday(time);
  time = GetYesterday(time);

  for (int w = 0; w < NUM_LAST_WEEKS; ++w) {
    tm start_time = AddDays(time, -DAYS_IN_WEEK);
    coins[w] = GetNumCoins(start_time, time);
    time = start_time;
  }
}

// 'day' is days since Jan 1. 'year' is e.g. 2005. Return the day of the week,
// where 0 is Sunday.
void Bookkeeper::GetCoinsByDayOfWeek(int coins[DAYS_IN_WEEK]) const {
  for (int i = 0; i < DAYS_IN_WEEK; ++i) {
    coins[i] = 0;
  }

  for (auto it = coins_for_hour_.begin(); it != coins_for_hour_.end(); ++it) {
    const Date& d = it->first;
    int day_of_week = GetDayInYearAndYear(d.day_of_year, d.year).tm_wday;
    coins[day_of_week] += it->second;
  }
}

void Bookkeeper::GetCoinsByHour(int coins[HOURS_IN_DAY]) const {
  memset(coins, 0, sizeof(int) * HOURS_IN_DAY);
  for (auto it = coins_for_hour_.begin(); it != coins_for_hour_.end(); ++it) {
    const Date& d = it->first;

    if (d.hour >= HOURS_IN_DAY) {
      LOG->Warn("Hour %i >= %i", d.hour, HOURS_IN_DAY);
      continue;
    }

    coins[d.hour] += it->second;
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
