/* RageTimer - Timer services. */

#ifndef RAGE_TIMER_H
#define RAGE_TIMER_H

#include <chrono>

class RageTimer
{
public:
	RageTimer() { Touch(); }
	RageTimer(std::chrono::steady_clock::time_point tm) : timestamp(tm) {};
	RageTimer(unsigned microseconds) : timestamp()
	{
		timestamp += std::chrono::microseconds(microseconds);
	}
	RageTimer(unsigned secs, unsigned microseconds) : timestamp()
	{
		auto seconds = std::chrono::seconds(secs);
		auto microsecs = std::chrono::microseconds(microseconds);
		timestamp += seconds + microsecs;
	}

	/* Time ago this RageTimer represents. */
	float Ago() const;
	void Touch();
	inline bool IsZero() const {
		return timestamp == std::chrono::steady_clock::time_point();
	}
	inline void SetZero() {
		timestamp = std::chrono::steady_clock::time_point();
	}

	/* Time between last call to GetDeltaTime() (Ago() + Touch()): */
	float GetDeltaTime();
	/* (alias) */
	float PeekDeltaTime() const { return Ago(); }

	// seconds since the program was started
	static float GetTimeSinceStart();
	// floats are generally used throughout the program for time tracking, but
	// we can expose a double version to Lua.
	static double GetTimeSinceStartDouble();

	std::uint64_t GetMicroseconds() const;

	/* Get a timer representing half of the time ago as this one. */
	RageTimer Half() const;

	/* Add (or subtract) a duration from a timestamp.  The result is another timestamp. */
	RageTimer operator+( float tm ) const;
	RageTimer operator-( float tm ) const { return *this + -tm; }
	void operator+=( float tm ) { *this = *this + tm; }
	void operator-=( float tm ) { *this = *this + -tm; }

	/* Find the amount of time between two timestamps.  The result is a duration. */
	float operator-( const RageTimer &rhs ) const;

	bool operator<( const RageTimer &rhs ) const;

	// NOTE: We must ensure that we're compiling with Visual Studio 2019 16.8 or
	// later for Windows builds. Otherwise, the steady_clock is not steady.
	// See: https://developercommunity.visualstudio.com/t/stl-steady-clock-not-steady/1105325
	std::chrono::steady_clock::time_point timestamp;

private:
	static RageTimer Sum( const RageTimer &lhs, float tm );
	static float Difference( const RageTimer &lhs, const RageTimer &rhs );
};

extern const RageTimer RageZeroTimer;

#endif

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

