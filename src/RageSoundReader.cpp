#include "global.h"
#include "RageSoundReader.h"
#include "RageLog.h"
#include "RageUtil_AutoPtr.h"
#include <chrono>
#include <thread>

REGISTER_CLASS_TRAITS( RageSoundReader, pCopy->Copy() );

int RageSoundReader::RetriedRead( float *pBuffer, int iFrames, int *iSourceFrame, float *fRate )
{
	if( iFrames == 0 )
		return 0;

	if( fRate )
		*fRate = this->GetStreamToSourceRatio();
	if( iSourceFrame )
		*iSourceFrame = this->GetNextSourceFrame();

	int iGotFrames = this->Read( pBuffer, iFrames );

	if( iGotFrames == RageSoundReader::STREAM_LOOPED )
		iGotFrames = 0;

	if( iGotFrames != 0 )
		return iGotFrames;

	// If the read fails, retry 10 times with an increased delay each time.
	int iMaxTries = 10;
	int iDelayMS = 1; // 1 millisecond
	for(int iTries = 0; iTries < iMaxTries; ++iTries)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(iDelayMS));

		iGotFrames = this->Read( pBuffer, iFrames );

		if( iGotFrames == RageSoundReader::STREAM_LOOPED )
			iGotFrames = 0;

		if( iGotFrames != 0 )
			return iGotFrames;

		iDelayMS *= 2;  // Double the delay
	}

	LOG->Warn( "RageSoundReader: 10 attempts to read from the stream failed. Busy looping." );
	return 0;
}

/*
 * Copyright (c) 2006 Glenn Maynard
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
