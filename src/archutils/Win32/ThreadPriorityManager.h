#ifndef THREADGRABBER_H
#define THREADGRABBER_H

#include <windows.h>
#define WIN32_LEAN_AND_MEAN

class ThreadPriorityManager
{
public:
	enum PriorityLevel
	{
		// some aliases for convenient use.
		// see Threadgrabber.cpp for more information.
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

#endif // THREADGRABBER_H
