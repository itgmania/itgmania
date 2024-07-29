/*++

Module name:

    Wallclock
    
Abstract:

    All functions directly related to the system clock are stored here. Methods
    are also provided to create and manage high-resolution timestamps.
    
Notes:

    The engine makes use of two clocks - one driven by the audio frame position,
    and one driven by the system's high resolution timer (a sub-1us resolution
    timer which is not affected by time & date settings).
    
    This header provides lightweight methods to interact with the system timer
    in any way needed. It is included everywhere the system timer is used.

    Wallclock's timestamping functions can be used to provide greater accuracy
    and lower overhead as compared to using RageTimer objects, so they are
    preferred for time-critical operations. However, to ensure calls to the
    system clock are reserved for important functions, RageTimer objects
    should be used to take measurements in non-critical situations.

    The accuracy of Wallclock is directly tied to the stability of the game
    sync. Maintaining precision here is crucial. Too much error in the retrieval
    of the system timestamp will manifest as a drastic shift in the game's sync,
    and will feel like the global offset suddenly changed. Incorrect math here
    will manifest as a _consistent_ sync offset in game.
    
    Resolution mismatches, unintended type conversions, or values truncated/
    rounded when they shouldn't be can cause errors when the time is calculated
    and manifest as a _sudden_ drift of sync.
    
    To minimize the potential for resolution errors:
     - Time should only be stored as seconds or microseconds.
     - Use of floating point values to store system time should be avoided
       whenever possible.
     - you should take care to use the constant that corresponds to your data
       type when calling WC_USECTOSEC. Various constexpr's are used instead of
       a single macro to ensure all values are type-checked. 
    
    The clock interface to Lua is handled via RageTimer in order to not break
    legacy code or Lua which expects GetTimeSinceStart() to be there.
    
Usage examples:

    - Get the current system time in microseconds:
    std::int64_t systemTime = Wallclock::GetSystemTime();

    - Get the current system time in seconds
    unsigned systemTimeInSeconds = Wallclock::GetSystemTimeInSeconds();

    - Get the time since game start in microseconds
    std::int64_t elapsedGameTime = Wallclock::GetElapsedGameTime();

    - Create multiple instances of Wallclock for timestamping purposes
    Wallclock tracker1;
    Wallclock tracker2;

    - Update the timestamp for each instance
    tracker1.UpdateTimestamp();
    tracker2.UpdateTimestamp();

    - Retrieve the elapsed time for tracker1
    std::uint64_t elapsedTime1 = tracker1.GetTimestampDuration();
    
    - Return the timestamp taken earlier for tracker2
    std::uint64_t timestampFromSpecificEvent = tracker2.ReturnTimestamp();
    
--*/

#ifndef Wallclock_H
#define Wallclock_H

#include <cstdint>
#include <mutex>
#include "arch/ArchHooks/ArchHooks.h"

static const std::int64_t WC_GAMESTART = 
    ArchHooks::GetMicrosecondsSinceStart(true);

static constexpr int WC_USECTOSEC_INT = 1000000;
static constexpr unsigned WC_USECTOSEC_UNSIGNED = 1000000U;
static constexpr float WC_USECTOSEC_FLOAT = 1000000.0f;
static constexpr double WC_USECTOSEC_DOUBLE = 1000000.0;
static constexpr std::int64_t WC_USECTOSEC_INT64 = 1000000LL;
static constexpr std::uint64_t WC_USECTOSEC_UINT64 = 1000000ULL;

class Wallclock {
public:

    //  System Time Retrieval
    static inline std::int64_t GetSystemTime() noexcept { // Unit: usecs
        return ArchHooks::GetMicrosecondsSinceStart(true);
    }   

    static inline std::int64_t GetElapsedGameTime() noexcept { // Unit: usecs
        return ArchHooks::GetMicrosecondsSinceStart(true) - WC_GAMESTART;
    }

    static inline unsigned GetSystemTimeInSeconds() noexcept // Unit: secs
    { 
        return static_cast<unsigned>(
            ArchHooks::GetMicrosecondsSinceStart(true) / WC_USECTOSEC_UNSIGNED);
    }

    static inline float FloatingPointTimeInSeconds() noexcept // Unit: secs
    {
        double determinator = static_cast<double>(
            ArchHooks::GetMicrosecondsSinceStart(true) / WC_USECTOSEC_DOUBLE
        );
        return static_cast<float>(determinator);
    }

    // Timestamp Management
    inline void UpdateTimestamp() noexcept { // Unit: usecs
        std::lock_guard<std::mutex> lock(mutex_);
        stamp = GetSystemTime();
    }

    inline uint64_t ReturnTimestamp() noexcept { // Unit: usecs
        std::lock_guard<std::mutex> lock(mutex_);
        return stamp;
    }

    inline std::int64_t GetTimestampDuration() const noexcept { // Unit: usecs
        std::lock_guard<std::mutex> lock(mutex_);
        std::int64_t now = GetSystemTime();
        return now - stamp;
    }

    inline double GetTimestampDurationSeconds() const noexcept { // Unit: secs
        std::lock_guard<std::mutex> lock(mutex_);
        std::uint64_t now = GetSystemTime();
        std::uint64_t elapsed = now - stamp;
        return static_cast<double>(elapsed) / WC_USECTOSEC_DOUBLE;
    }

private:
    std::uint64_t stamp;
    mutable std::mutex mutex_;
};

#endif // Wallclock_H

/*
 * (c) 2024 sukibaby
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
