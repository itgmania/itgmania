#include "global.h"

#include "RageTimer.h"
#include "RageLog.h"
#include "RageUtil.h"

#include "arch/ArchHooks/ArchHooks.h"

#include <cmath>
#include <cstdint>

const RageTimer RageZeroTimer(0,0);

void RageTimer::Touch()
{
	std::uint64_t usecs = ArchHooks::GetMicrosecondsSinceStart( true );

	this->m_secs = std::uint64_t(usecs / 1000000);
	this->m_us = std::uint64_t(usecs % 1000000);
}

float RageTimer::Ago() const
{
	const RageTimer Now;
	return Now - *this;
}

float RageTimer::GetDeltaTime()
{
	const RageTimer Now;
	const float diff = Difference( Now, *this );
	*this = Now;
	return diff;
}

/* Get a timer representing half of the time ago as this one.  This is	
 * useful for averaging time.  For example,	
 *	
 * RageTimer tm;	
 * ... do stuff ...	
 * RageTimer AverageTime = tm.Half();	
 * printf( "Something happened approximately %f seconds ago.\n", tm.Ago() ); 
 * Note this has been reverted to the original SM3.95 function. */
RageTimer RageTimer::Half() const
{
	const float fProbableDelay = Ago() / 2;
	return *this + fProbableDelay;
}


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

RageTimer RageTimer::Sum(const RageTimer& lhs, float tm)
{
	/* Calculate the seconds and microseconds from the time:
	 * tm == 5.25  -> secs =  5, us = 5.25  - ( 5) = .25
	 * tm == -1.25 -> secs = -2, us = -1.25 - (-2) = .75 */
	int64_t seconds = std::floor(tm);
	int64_t us = int64_t((tm - seconds) * 1000000);

	// Prevent unnecessarily checking the time
	RageTimer ret(0, 0);

	// Calculate the sum of the seconds and microseconds
	ret.m_secs = seconds + lhs.m_secs;
	ret.m_us = us + lhs.m_us;

	// Adjust the seconds and microseconds if microseconds is greater than
	// or equal to 1000000 (usec <-> sec conversion factor)
	if (ret.m_us >= 1000000)
	{
		ret.m_us -= 1000000;
		++ret.m_secs;
	}

	return ret;
}

double RageTimer::Difference(const RageTimer& lhs, const RageTimer& rhs)
{
	// Calculate the difference in seconds and microseconds respectively
	int64_t secs = lhs.m_secs - rhs.m_secs;
	int64_t us = lhs.m_us - rhs.m_us;

	// Adjust seconds and microseconds if microseconds is negative
	if ( us < 0 )
	{
		us += 1000000;
		--secs;
	}

	// Return the difference as a double to preserve the fractional part
	return static_cast<double>(secs) + static_cast<double>(us) / 1000000.0;
}

/*
 * The following is not thread-safe or accurate, but Lua depends on
 * it being here, so this function should be left alone as it only exists
 * for legacy compatibility.
 * 
 * Use Wallclock.h instead if you need to interact with the system clock.
 */
static std::uint64_t g_iStartTime = ArchHooks::GetMicrosecondsSinceStart( true );

float RageTimer::GetTimeSinceStart( bool bAccurate )
{
	std::uint64_t usecs = ArchHooks::GetMicrosecondsSinceStart( true );
	usecs -= g_iStartTime;
	return usecs / 1000000.0;
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

