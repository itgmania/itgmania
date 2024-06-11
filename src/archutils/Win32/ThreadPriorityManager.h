/*++

Module name:

	ThreadPriorityManager.h

Abstract:

	This file contains the implementation for the ThreadPriorityManager class.
	This exists to prevent windows.h headers from being pulled into other source files,
	so we can safely change the priority level of threads in a Win32 environment.

	The reason we use numbers instead of the names of the priority levels is because
	it avoids a clunky situation where we have to pass the thread priority level as a string.

	You can call it like this:

	#ifdef _WIN32
		ThreadPriorityManager::SetThreadPriorityForWin32(ThreadPriorityManager::HIGHEST);
	#endif

	or you can directly pass it an integer:

	#ifdef _WIN32
		ThreadPriorityManager::SetThreadPriorityForWin32(2); // 2 corresponds to THREAD_PRIORITY_HIGHEST
	#endif

Environment:

	User mode

----------------------------------------------

 Options for the priority level correspond directly to the values in the Windows API:

THREAD_PRIORITY_IDLE: -15
THREAD_PRIORITY_LOWEST: -2
THREAD_PRIORITY_BELOW_NORMAL: -1
THREAD_PRIORITY_NORMAL: 0
THREAD_PRIORITY_ABOVE_NORMAL: 1
THREAD_PRIORITY_HIGHEST: 2
THREAD_PRIORITY_TIME_CRITICAL: 15

Please refer to the Windows API documentation for more information. You can pass a specific integer if desired.
https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setthreadpriority

----------------------------------------------

Please maintain this priority structure for the threads:

 *** Highest or Time Critical
   * Rendering thread
   * Audio mixing thread

 *** Above Normal or Highest
   * Audio decoding thread
   * Audio streaming thread

 *** Normal
   * Main game thread
	 - Please keep this below the audio thread priority

 *** Below Normal ***
   * Worker threads

--*/

#ifndef THREADGRABBER_H
#define THREADGRABBER_H

#include <windows.h>
#define WIN32_LEAN_AND_MEAN

typedef void* HANDLE;

class ThreadPriorityManager {
public:

	enum PriorityLevel
	{
		// some aliases for convenient use.
		TIME_CRITICAL = 15,
		HIGHEST = 2,
		ABOVE_NORMAL = 1,
		NORMAL = 0,
		BELOW_NORMAL = -1,
		LOWEST = -2,
		IDLE = -15
	};

	static void SetThreadPriorityForWin32(int priorityLevel);
};

inline void ThreadPriorityManager::SetThreadPriorityForWin32(int priorityLevel)
{
	extern HANDLE GetCurrentThread();
	extern BOOL SetThreadPriority(HANDLE hThread, int nPriority);

	HANDLE hThread = GetCurrentThread();
	SetThreadPriority(hThread, priorityLevel);
}

#endif // THREADGRABBER_H
