/*
 * This can be used in two ways: as a timestamp or as a timer.
 *
 * As a timer,
 * RageTimer Timer;
 * for(;;) {
 *   printf( "Will be approximately: %f", Timer.PeekDeltaTime()) ;
 *   float fDeltaTime = Timer.GetDeltaTime();
 * }
 *
 * or as a timestamp:
 * void foo( RageTimer &timestamp ) {
 *     if( timestamp.IsZero() )
 *         printf( "The timestamp isn't set." );
 *     else
 *         printf( "The timestamp happened %f ago", timestamp.Ago() );
 *     timestamp.Touch();
 *     printf( "Near zero: %f", timestamp.Age() );
 * }
 */

#include "global.h"

#include "RageTimer.h"
#include "RageUtil.h"

#include <chrono>
using namespace std::chrono;


const RageTimer RageZeroTimer(0,0);
static auto g_iStartTime = steady_clock::now();


float RageTimer::GetTimeSinceStart()
{
	duration<float> t = steady_clock::now() - g_iStartTime;
	return t.count();
}

double RageTimer::GetTimeSinceStartDouble()
{
	duration<double> t = steady_clock::now() - g_iStartTime;
	return t.count();
}

std::uint64_t RageTimer::GetMicroseconds() const
{
	steady_clock::duration duration = timestamp.time_since_epoch();
	std::uint64_t usecs = duration_cast<microseconds>(duration).count();
	return usecs;
}

void RageTimer::Touch()
{
	timestamp = steady_clock::now();
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

/*
 * Get a timer representing half of the time ago as this one.  This is
 * useful for averaging time.  For example,
 *
 * RageTimer tm;
 * ... do stuff ...
 * RageTimer AverageTime = tm.Half();
 * printf( "Something happened approximately %f seconds ago.\n", tm.Ago() );
 */
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
	return timestamp < rhs.timestamp;
}

RageTimer RageTimer::Sum(const RageTimer &lhs, float tm)
{
	const steady_clock::time_point ret =
		lhs.timestamp +
		duration_cast<steady_clock::duration>(duration<float>(tm));
	return RageTimer(ret);
}

float RageTimer::Difference(const RageTimer &lhs, const RageTimer &rhs)
{
	const duration<float> diff = lhs.timestamp - rhs.timestamp;
	return diff.count();
}

#include "LuaManager.h"
LuaFunction(GetTimeSinceStart, RageTimer::GetTimeSinceStartDouble())

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

