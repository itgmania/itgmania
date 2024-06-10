/*
This exists to prevent windows.h headers from being pulled into other source files,
so we can safely change the priority level of threads in a Win32 environment.

The reason we use numbers instead of the names of the priority levels is because
it avoids a clunky situation where we have to pass the thread priority level as a string.

You can call it like this:

#ifdef _WIN32
	ThreadPriorityManager::SetThreadPriorityForWin32(ThreadPriorityManager::GetPriorityLevel(THREAD_PRIORITY_HIGHEST));
#endif

 Options for the priority level:

THREAD_PRIORITY_TIME_CRITICAL	
THREAD_PRIORITY_HIGHEST			
THREAD_PRIORITY_ABOVE_NORMAL	
THREAD_PRIORITY_NORMAL			
THREAD_PRIORITY_BELOW_NORMAL	
THREAD_PRIORITY_LOWEST			
THREAD_PRIORITY_IDLE			

Please maintain this priority structure for the threads:

********* Highest or Time Critical *********
- Rendering thread
- Audio mixing thread

********* Above Normal or Highest *********
- Audio decoding thread
- Audio streaming thread

********* Normal *********
Main game thread (this should never run at a higher priority than the audio thread)

********* Below Normal *********
Worker threads

Sukibaby 2024
*/

#include "Threadgrabber.h"
#include <windows.h>
#define WIN32_LEAN_AND_MEAN

int GetPriorityLevel(int priorityLevelCode)
{
	return priorityLevelCode;
}

void SetThreadPriorityForWin32(int priorityLevel)
{
	HANDLE hThread = GetCurrentThread();
	SetThreadPriority(hThread, priorityLevel);
}
