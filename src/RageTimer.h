/* RageTimer - Timer services. */

#ifndef RAGE_TIMER_H
#define RAGE_TIMER_H

#include <cstdint>
#include <chrono>
using namespace std::chrono;

class RageTimer
{
public:
	RageTimer( ) { Touch(); }
	RageTimer( int secs, int us );

	/* Time ago this RageTimer represents. */
	float Ago() const;
	void Touch();
	inline bool IsZero() const { return m_time_point == sm_time_point(); }
	inline void SetZero() { m_time_point = sm_time_point(); }

	/* Time between last call to GetDeltaTime() (Ago() + Touch()): */
	float GetDeltaTime();
	/* (alias) */
	float PeekDeltaTime() const { return Ago(); }
	std::int64_t NsecsAgo() const
	{
		const auto duration = duration_cast<nanoseconds>(sm_clock::now() - m_time_point);
		return static_cast<std::int64_t>(duration.count());
	}

	/* deprecated: */
	static float GetTimeSinceStart( bool bAccurate = true );	// seconds since the program was started
	static float GetTimeSinceStartFast() { return GetTimeSinceStart(false); }
	static std::uint64_t GetUsecsSinceStart();

	static RageTimer GetZeroTimer() { return RageTimer(sm_time_point()); }

	/* Get a timer representing half of the time ago as this one. */
	RageTimer Half() const;

	/* Add (or subtract) a duration from a timestamp.  The result is another timestamp. */
	RageTimer operator+( float tm ) const
	{
		return Sum(*this, tm);
	}

	RageTimer operator-( float tm ) const { return *this + -tm; }
	void operator+=( float tm ) { *this = *this + tm; }
	void operator-=( float tm ) { *this = *this + -tm; }

	/* Find the amount of time between two timestamps.  The result is a duration. */
	float operator-( const RageTimer &rhs ) const
	{
		return Difference(*this, rhs);
	}

	bool operator<( const RageTimer &rhs ) const
	{
		return m_time_point < rhs.m_time_point;
	}

	// If the high-resolution clock implementation provided is steady (i.e. monotonic)
	// use it. If it isn't monotonic, use the best monotonic clock we have.
	using sm_clock = std::conditional<
		std::chrono::high_resolution_clock::is_steady,
		std::chrono::high_resolution_clock,
		std::chrono::steady_clock>::type;
	using sm_time_point = std::chrono::time_point<sm_clock>;
	using sm_duration = sm_clock::duration;

private:
	sm_time_point m_time_point;

	RageTimer( sm_time_point point );

	using float_seconds = duration<float, std::ratio<1, 1>>;

	static constexpr std::uint64_t GetDurationAsMicros(const sm_duration duration)
	{
		const auto micros = duration_cast<microseconds>(duration);
		return static_cast<std::uint64_t>(micros.count());
	}

	static constexpr float SMDurationToFloat(const sm_duration duration)
	{
		return duration_cast<float_seconds>(duration).count();
	}

	static constexpr sm_duration FloatToSMDuration(const float sec)
	{
		return duration_cast<RageTimer::sm_duration>(float_seconds(sec));
	}

	static RageTimer Sum( const RageTimer &lhs, const float tm )
	{
		return RageTimer(lhs.m_time_point + FloatToSMDuration(tm));
	}

	static float Difference( const RageTimer &lhs, const RageTimer &rhs )
	{
		const auto difference = lhs.m_time_point - rhs.m_time_point;
		return SMDurationToFloat(difference);
	}


};

extern const RageTimer RageZeroTimer;

// For profiling how long some chunk of code takes. -Kyz
#define START_TIME(name) std::uint64_t name##_start_time= RageTimer::GetUsecsSinceStart();
#define START_TIME_CALL_COUNT(name) START_TIME(name); ++name##_call_count;
#define END_TIME(name) std::uint64_t name##_end_time= RageTimer::GetUsecsSinceStart();  LOG->Time(#name " time: %zu to %zu = %zu", name##_start_time, name##_end_time, name##_end_time - name##_start_time);
#define END_TIME_ADD_TO(name) std::uint64_t name##_end_time= RageTimer::GetUsecsSinceStart();  name##_total += name##_end_time - name##_start_time;
#define END_TIME_CALL_COUNT(name) END_TIME_ADD_TO(name); ++name##_end_count;

#define DECL_TOTAL_TIME(name) extern std::uint64_t name##_total;
#define DEF_TOTAL_TIME(name) std::uint64_t name##_total= 0;
#define PRINT_TOTAL_TIME(name) LOG->Time(#name " total time: %zu", name##_total);
#define DECL_TOT_CALL_PAIR(name) extern std::uint64_t name##_total; extern std::uint64_t name##_call_count;
#define DEF_TOT_CALL_PAIR(name) std::uint64_t name##_total= 0; std::uint64_t name##_call_count= 0;
#define PRINT_TOT_CALL_PAIR(name) LOG->Time(#name " calls: %zu, time: %zu, per: %f", name##_call_count, name##_total, static_cast<float>(name##_total) / name##_call_count);
#define DECL_TOT_CALL_END(name) DECL_TOT_CALL_PAIR(name); extern std::uint64_t name##_end_count;
#define DEF_TOT_CALL_END(name) DEF_TOT_CALL_PAIR(name); std::uint64_t name##_end_count= 0;
#define PRINT_TOT_CALL_END(name) LOG->Time(#name " calls: %zu, time: %zu, early end: %zu, per: %f", name##_call_count, name##_total, name##_end_count, static_cast<float>(name##_total) / (name##_call_count - name##_end_count));

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
