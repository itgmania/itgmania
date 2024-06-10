/* 2024 sukibaby */

class ThreadPriorityManager
{
public:
	enum PriorityLevel
	{
		TIME_CRITICAL = 7,
		HIGHEST = 6,
		ABOVE_NORMAL = 5,
		NORMAL = 4,
		BELOW_NORMAL = 3,
		LOWEST = 2,
		IDLE = 1
	};

	static void SetThreadPriorityForWin32(PriorityLevel priorityLevel)
	{
		HANDLE hThread = GetCurrentThread();
		SetThreadPriority(hThread, static_cast<int>(priorityLevel));
	}
};
