/*++

Module:

   Wallclock.h
    
Abstract:

   Provides an alternate set of timestamping tools, designed with code profiling
   in mind.
    
Notes:

   Profiling the StepMania code base is very difficult due to its largely
   single-threaded structure, which makes it impossible for profiler tools to
   generate any useful information. This means one typically has to do manual
   profiling combined with squeezing in logging where possible, and inspecting
   the generated log file.
   
   This header includes some thread-safe timestamping functions to help with
   profiling and debugging. These functions are thread-safe and can be used
   to measure the elapsed time between multiple points in the code.
   
   Unlike RageTimer objects, which are meant to be used for time arithmetic,
   Wallclock timestamp functions feature a mutex for thread safety and work
   directly with values retrieved from the system's high resolution timer.

   Below, you will find more examples of how to use wallclock's timestamping
   features as they pertain to profiling, as well as several functions to get
   a timestamp from the OS's high-resolution timer.
   
   This is not intended for in-game use because adding thread safety generally
   only causes issues.
   
   To compile this with the game, you can add it to CMakeData-singletons.cmake
    
--*/

#ifndef Wallclock_H
#define Wallclock_H

#include <cstdint>
#include <mutex>
#include "arch/ArchHooks/ArchHooks.h"

static const std::int64_t WC_GAMESTART = ArchHooks::GetMicrosecondsSinceStart();

class Wallclock {
public:

/*

   The following functions are more or less equivalent to calling
   RageTimer::GetUsecsSinceStart, but here there is also a way to get
   the system's time value directly.
   
*/

    static inline std::int64_t GetSystemTime() noexcept {
        return ArchHooks::GetMicrosecondsSinceStart();
    }   

    static inline std::int64_t GetElapsedGameTime() noexcept {
        return ArchHooks::GetMicrosecondsSinceStart() - WC_GAMESTART;
    }

/*

   The following methods are used to interact with the timestamping system.

   The timestamps are stored in a vector, are organized by index, and are
   thread-safe. The timestamps here are based on microseconds.

   Since these were designed with profiling in mind, they will not throw
   exceptions. If you try to access an index that does not exist, you will
   return the default value of the return type.

   When profiling code, you may wish to verify the index number at a given
   point in a method. The function GetTimestampCount() exists to allow you to
   retrieve the index at that point in time. Once you know the index of every
   timer call you've placed, you can easily set up logging or arithmetic
   using these different timestamps.

   Example usage to prepare a function for profiling:

       #include "Wallclock.h"
       #include "RageLog.h"
       Wallclock tracker;

       tracker.Start();
       ...
       tracker.Start();

       std::size_t value = myTimer.GetTimestampCount();
       LOG->Trace("You have %zu timestamps", value);

   RageTimer objects are often used with a single .Touch() and .Ago() to
   calculate a time difference. These are safe to replace with a Wallclock
   instance. If you do replace a RageTimer object, you will likely also have
   to change any relevant variables from float to std::uint64_t, and any
   duration checks from seconds to microseconds.

       Wallclock timer; // presumably used to be a RageTimer
       std::uint64_t elapsedDuration; // presumably used to be a float
       timer.Start(); 
       elapsedDuration = timer.Stop(0);

*/

    inline void Start() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        timestamps.push_back(Wallclock::GetSystemTime());
    }

    inline std::uint64_t Stop(std::size_t index) const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        std::uint64_t now = static_cast<std::uint64_t(Wallclock::GetSystemTime());
        return now - timestamps[index];
    }

    inline std::uint64_t ReturnTimestamp(std::size_t index) const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return timestamps[index];
    }

    inline std::size_t GetTimestampCount() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return timestamps.size();
    }

    void ClearTimestamps() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        timestamps.clear();
    }

private:
    std::vector<std::uint64_t> timestamps;
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
