#include "ThreadPriorityHelper.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

bool SetThreadPriorityLevel(int priorityLevel)
{
	return SetThreadPriority(GetCurrentThread(), priorityLevel) != 0;
}

bool BoostThreadPriorityToBelowNormal()
{
	return SetThreadPriorityLevel(THREAD_PRIORITY_BELOW_NORMAL);
}

bool BoostThreadPriorityToNormal()
{
	return SetThreadPriorityLevel(THREAD_PRIORITY_NORMAL);
}

bool BoostThreadPriorityToAboveNormal()
{
	return SetThreadPriorityLevel(THREAD_PRIORITY_ABOVE_NORMAL);
}

bool BoostThreadPriorityToHighest()
{
	return SetThreadPriorityLevel(THREAD_PRIORITY_HIGHEST);
}

bool BoostThreadPriorityToTimeCritical()
{
	return SetThreadPriorityLevel(THREAD_PRIORITY_TIME_CRITICAL);
}
