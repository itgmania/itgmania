#include "global.h"

#include "RageTimer.h"
#include "RageLog.h"
#include "RageUtil.h"

#include "arch/ArchHooks/ArchHooks.h"

#include <cmath>
#include <cstdint>

constexpr std::int64_t USEC_TO_SEC_AS_INTEGER = 1000000;
constexpr float USEC_TO_SEC_AS_FLOAT = 1000000.0f;
constexpr double USEC_TO_SEC_AS_DOUBLE = 1000000.0;
const RageTimer	RageZeroTimer(0,0);
const static std::int64_t g_iStartTime = ArchHooks::GetMicrosecondsSinceStart( true );

static std::int64_t GetTime( bool /* bAccurate */ )
{
	return ArchHooks::GetMicrosecondsSinceStart( true );
}

double RageTimer::GetTimeSinceStart(bool bAccurate)
{
	std::int64_t usecs = GetTime(bAccurate);
	usecs -= g_iStartTime;
	return usecs / USEC_TO_SEC_AS_DOUBLE;
}

float RageTimer::GetTimeSinceStartFast(bool bAccurate)
{
	std::int64_t usecs = GetTime(bAccurate);
	usecs -= g_iStartTime;
	return usecs / USEC_TO_SEC_AS_FLOAT;
}

std::uint64_t RageTimer::DeltaSecondsAsUnsigned()
{
    std::uint64_t usecs = static_cast<std::uint64_t>(GetTime(true) - g_iStartTime);
    return usecs / USEC_TO_SEC_AS_INTEGER;
}

std::int64_t RageTimer::DeltaSecondsAsSigned()
{
    std::int64_t usecs = (GetTime(true) - g_iStartTime);
    return usecs / USEC_TO_SEC_AS_INTEGER;
}

std::uint64_t RageTimer::DeltaMicrosecondsAsUnsigned()
{
	return static_cast<uint64_t>(GetTime(true) - g_iStartTime);
}

std::int64_t RageTimer::DeltaMicrosecondsAsSigned()
{
	return GetTime(true) - g_iStartTime;
}

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

	this->m_secs = std::uint64_t(usecs / USEC_TO_SEC_AS_INTEGER);
	this->m_us = std::uint64_t(usecs % USEC_TO_SEC_AS_INTEGER);
}

/*++

Function Description:

   Return the difference in time since a RageTimer
   object was initialized. It does not modify the
   state of the RageTimer.
   
Return Type:

   Floating point
   
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
	std::int64_t seconds = std::floor(tm);
	std::int64_t us = static_cast<int64_t>((tm - seconds) * USEC_TO_SEC_AS_INTEGER);

	RageTimer ret(0, 0); // Prevent unnecessarily checking the time

	ret.m_secs = seconds + lhs.m_secs;
	ret.m_us = us + lhs.m_us;

	if (ret.m_us >= USEC_TO_SEC_AS_INTEGER)
	{
		ret.m_us -= USEC_TO_SEC_AS_INTEGER;
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
	std::int64_t secs = lhs.m_secs - rhs.m_secs;
	std::int64_t us = lhs.m_us - rhs.m_us;

	if ( us < 0 )
	{
		us += USEC_TO_SEC_AS_INTEGER;
		--secs;
	}

	return static_cast<double>(secs) + static_cast<double>(us) / USEC_TO_SEC_AS_INTEGER;
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

