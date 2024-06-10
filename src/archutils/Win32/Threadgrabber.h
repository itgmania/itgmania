/* 2024 sukibaby */

class ThreadPriorityManager
{
public:
	static int GetPriorityLevel(int priorityLevelCode)
	{
		// Return the priority level code as is, since it's already an integer
		return priorityLevelCode;
	}

	static void SetThreadPriorityForWin32(int priorityLevel)
	{
		HANDLE hThread = GetCurrentThread();
		SetThreadPriority(hThread, priorityLevel);
	}
};
