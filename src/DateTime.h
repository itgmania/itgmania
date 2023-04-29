#ifndef DATE_TIME_H
#define DATE_TIME_H

#include <ctime>

#include "EnumHelper.h"

int StringToDayInYear(RString sDayInYear);

// The number of days we check for previously.
const int NUM_LAST_DAYS = 7;
// The number of weeks we check for previously.
const int NUM_LAST_WEEKS = 52;
// The number of days that are in a year.
// This is set up to be a maximum for leap years.
const int DAYS_IN_YEAR = 366;
// The number of hours in a day.
const int HOURS_IN_DAY = 24;
// The number of days that are in a week.
const int DAYS_IN_WEEK = 7;
// Which month are we focusing on?
//
// NOTE(Wolfman2000):Is there any reason why the actual months aren't defined
// in here?
enum Month {
	// The number of months in the year.
  NUM_Month = 12,
	// There should be no month at this point.
  Month_Invalid
};

RString DayInYearToString(int day_in_year_index);
RString LastDayToString(int last_day_index);
RString LastDayToLocalizedString(int last_day_index);
RString DayOfWeekToString(int day_of_week_index);
RString DayOfWeekToLocalizedString(int day_of_week_index);
RString HourInDayToString(int hour_index);
RString HourInDayToLocalizedString(int hour_index);
const RString& MonthToString(Month month);
const RString& MonthToLocalizedString(Month month);
RString LastWeekToString(int last_week_index);
RString LastWeekToLocalizedString(int last_week_index);
LuaDeclareType(Month);

tm AddDays(tm start, int days_to_move);
tm GetYesterday(tm start);
int GetDayOfWeek(tm time);
tm GetNextSunday(tm start);

tm GetDayInYearAndYear(int day_in_year_index, int iYear);

// A standard way of determining the date and the time.
struct DateTime {
  // The number of seconds after the minute.
  // Valid values are [0, 59].
  int tm_sec;
  // The number of minutes after the hour.
  // Valid values are [0, 59].
  int tm_min;
  // The number of hours since midnight (or 0000 hours).
  // Valid values are [0, 23].
  int tm_hour;
  // The specified day of the current month.
  // Valid values are [1, 31].
  // NOTE(Wolfman2000): Is it possible to set an illegal date through here,
  // such as day 30 of February?
  int tm_mday;
  // The number of months since January.
  // Valid values are [0, 11].
  int tm_mon;
  // The number of years since the year 1900.
  int tm_year;

  // Set up a default date and time.
  DateTime();
  // Initialize the date and time.
  void Init();

  // Determine if this DateTime is less than some other time.
  bool operator<(const DateTime& other) const;
  // Determine if this DateTime is greater than some other time.
  bool operator>(const DateTime& other) const;
  // Determine if this DateTime is equal to some other time.
  bool operator==(const DateTime& other) const;
  // Determine if this DateTime is not equal to some other time.
  bool operator!=(const DateTime& other) const { return !operator==(other); }
  // Determine if this DateTime is less than or equal to some other time.
  bool operator<=(const DateTime& other) const { return !operator>(other); }

  // Determine if this DateTime is greater than or equal to some other time.
  bool operator>=(const DateTime& other) const { return !operator<(other); }

  // Retrieve the current date and time.
  static DateTime GetNowDateTime();
  // Retrieve the current date.
  static DateTime GetNowDate();

  // Remove the time portion from the date.
  void StripTime();

  // Retrieve a string representation of the current date and time.
  // This returns a common SQL/XML format: "YYYY-MM-DD HH:MM:SS".
  RString GetString() const;
  // Attempt to turn a string into a DateTime.
  bool FromString(const RString date_time);
};

#endif  // DATE_TIME_H

/**
 * @file
 * @author Chris Danford (c) 2001-2004
 * @section LICENSE
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
