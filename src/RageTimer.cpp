/*++

Source file:

    RageTimer.cpp

Abstract:

    1) Provide the time since game start as seconds or microseconds,
    2) Provide methods to perform time calculations on RageTimer objects   

Notes:

    Floating-point time math is slower than integer time methods,
    and is prone to precision loss by having the fractional part
    lost during type conversion, for this reason usage of
    floating-point values for a time-since-start value should be
    avoided whenever possible.

    Use caution and do thorough testing if you change anything here.
    The engine is very sensitive to fluctuations or errors in time math.

    The most precise way to calculate time differences is to calculate
    the difference between two microsecond precision time since start values.
    
    RageTimer objects should be used for non-time critical functions.

    Fluctuations in time reported by the OS can manifest as a sudden
    shift in the game's sync (though this is more likely due to buffer
    underrun somewhere).
    
    Incorrect time math or floating-point precision loss will manifest
    as a consistent sync offset, as if the global offset value shifted.
    
Practical limits on data types for time storage:

    32-bit signed integer
     seconds = 68 years
     milliseconds = Almost 600 hours
     microseconds = Just under 36 minutes

    64-bit signed integer
     seconds = >1 million years
     milliseconds = >1 million years
     microseconds = Almost 300,000 years

    The maximum value a single-precision float can store
    with millisecond accuracy is 1e7f (10,000,000).
   
    The maximum value a double-precision float can store
    with millisecond accuracy is 1e15 (1,000,000,000,000,000).
   
    3 decimal places is chosen because 1 millisecond is 0.001 seconds.

--*/

#include "global.h"

#include "RageTimer.h"
#include "RageLog.h"
#include "RageUtil.h"

#include "arch/ArchHooks/ArchHooks.h"

#include <cmath>
#include <cstdint>

#define TIMESTAMP_RESOLUTION 1000000

const RageTimer RageZeroTimer(0,0);
static std::uint64_t g_iStartTime = ArchHooks::GetMicrosecondsSinceStart( true );

static std::uint64_t GetTime( bool /* bAccurate */ )
{
	return ArchHooks::GetMicrosecondsSinceStart( true );
}

/*++

Function Description:

   Get the time since the game was started, using the
   time in microseconds as reported by the OS, and
   then convert it to a floating point value.
   
Time resolutions:

   Seconds
   
--*/

double RageTimer::GetTimeSinceStart(bool bAccurate)
{
	std::uint64_t usecs = GetTime(bAccurate);
	usecs -= g_iStartTime;
	return usecs / 1000000.0;
}

/*++

Function Description:

   Get the time since the game was started, using the
   time in microseconds as reported by the OS.
   
Time resolutions:

   Microseconds 
   
--*/

std::uint64_t RageTimer::GetUsecsSinceStart()
{
	return GetTime(true) - g_iStartTime;
}

// RageTimer objects functions
//
// - IsZero           checks if m_sec and m_us are 0
// - SetZero          sets m_sec and m_us to 0
// - Touch            no return (void)
// - Ago              returns float
// - GetDeltaTime     returns float
// - PeekDeltaTime    alias for Ago
// - Half             returns RageTimer
// - Sum              calculate the seconds and microseconds from the time given
// - Difference       calculate the difference in seconds and microseconds respectively

/*++

Function Description:

   Split the current time as microseconds into two
   integers (one for the integer part, and one for
   the fractional part).
   
Return Type:

   None (sets values of m_secs and m_us)
   
--*/

void RageTimer::Touch()
{
	std::uint64_t usecs = GetTime( true );

	this->m_secs = std::uint64_t(usecs / TIMESTAMP_RESOLUTION);
	this->m_us = std::uint64_t(usecs % TIMESTAMP_RESOLUTION);
}

/*++

Function Description:

   Return the difference in time since a RageTimer
   object was initialized. It does not modify the
   state of the RageTimer.
   
Return Type:

   Single-precision floating point
   
--*/

float RageTimer::Ago() const
{
	const RageTimer Now;
	return Now - *this;
}

/*++

Function Description:

   Return the difference in time since a RageTimer
   object was last checked. It modifies the state
   of the RageTimer.
   
Return Type:

   Floating point
   
--*/

float RageTimer::GetDeltaTime()
{
	const RageTimer Now;
	const float diff = Difference( Now, *this );
	*this = Now;
	return diff;
}

/*++

Function Description:

   Return a point in time halfway between the current
   time and the time stored by the RageTimer.
   
Return Type:

   RageTimer
   
--*/

RageTimer RageTimer::Half() const
{
	const float fProbableDelay = Ago() / 2;
	return *this + fProbableDelay;
}

/*++

Routine Description:

   Operator overloads for RageTimer methods
   
--*/

RageTimer RageTimer::operator+(float tm) const
{
	return Sum(*this, tm);
}

float RageTimer::operator-(const RageTimer &rhs) const
{
	return Difference(*this, rhs);
}

bool RageTimer::operator<( const RageTimer &rhs ) const
{
	if( m_secs != rhs.m_secs ) return m_secs < rhs.m_secs;
	return m_us < rhs.m_us;

}

/*++

Function Description:

   Calculate the seconds and microseconds from the given
   time values, prevent unnecessarily checking the time,
   and Adjust the seconds and microseconds if microseconds
   is greater than or equal to 1,000,000.
   
Return Type:

   RageTimer
   
--*/

RageTimer RageTimer::Sum(const RageTimer& lhs, float tm)
{
	/* Calculate the seconds and microseconds from the time:
	 * tm == 5.25  -> secs =  5, us = 5.25  - ( 5) = .25
	 * tm == -1.25 -> secs = -2, us = -1.25 - (-2) = .75 */
	int64_t seconds = std::floor(tm);
	int64_t us = int64_t((tm - seconds) * TIMESTAMP_RESOLUTION);

	// Prevent unnecessarily checking the time
	RageTimer ret(0, 0);

	// Calculate the sum of the seconds and microseconds
	ret.m_secs = seconds + lhs.m_secs;
	ret.m_us = us + lhs.m_us;

	// Adjust the seconds and microseconds if microseconds is greater than or equal to TIMESTAMP_RESOLUTION
	if (ret.m_us >= TIMESTAMP_RESOLUTION)
	{
		ret.m_us -= TIMESTAMP_RESOLUTION;
		++ret.m_secs;
	}

	return ret;
}

/*++

Function Description:

   Calculate the difference in seconds and microseconds
   respectively, adjust for negative microseconds, and
   return the time difference.
   
Return Type:

   Floating point
   
--*/

double RageTimer::Difference(const RageTimer& lhs, const RageTimer& rhs)
{
	// Calculate the difference in seconds and microseconds respectively
	int64_t secs = lhs.m_secs - rhs.m_secs;
	int64_t us = lhs.m_us - rhs.m_us;

	// Adjust seconds and microseconds if microseconds is negative
	if ( us < 0 )
	{
		us += TIMESTAMP_RESOLUTION;
		--secs;
	}

	// Return the difference as a double to preserve the fractional part
	return static_cast<double>(secs) + static_cast<double>(us) / TIMESTAMP_RESOLUTION;
}

#include "LuaManager.h"
LuaFunction(GetTimeSinceStart, RageTimer::GetTimeSinceStartFast())

/*
 * Copyright (c) 2001-2003 Chris Danford, Glenn Maynard
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

