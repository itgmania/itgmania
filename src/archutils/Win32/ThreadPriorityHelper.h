/*++

A layer to isolate windows.h from other source files while retaining the ability to set thread priorities.

	One possible way to call it if you don't need to store the result is like so:

		#ifdef _WIN32
				bool SuccessStatus = BoostThreadPriorityToHighest();
				if (!SuccessStatus)
				{
					ASSERT_M(0, "Failed to set thread priority to highest.");
				}
		#endif

Please refer to the Windows SetThreadPriority function documentation for more information.
https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setthreadpriority

Please maintain this priority structure for the threads:

Highest or Time Critical
* Rendering thread
* Audio mixing thread
	- If this is not high enough priority, it can negatively affect the game clock

Above Normal or Highest
* Audio decoding thread
* Audio streaming thread
* Audio buffering thread

Normal
* Main game thread
	- Please keep this below the audio thread priority

Below Normal
* Worker threads

--*/

bool SetThreadPriorityLevel(int priorityLevel);

bool BoostThreadPriorityToBelowNormal();
bool BoostThreadPriorityToNormal();
bool BoostThreadPriorityToAboveNormal();
bool BoostThreadPriorityToHighest();
bool BoostThreadPriorityToTimeCritical();

/**
 * (c) sukibaby 2024
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
