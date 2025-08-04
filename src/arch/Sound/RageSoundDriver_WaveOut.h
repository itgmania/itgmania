#ifndef RAGE_SOUND_WAVEOUT_H
#define RAGE_SOUND_WAVEOUT_H

#include "RageSoundDriver.h"
#include "RageThreads.h"

#include <cstdint>

#include <windows.h>
#include <mmsystem.h>

class RageSoundDriver_WaveOut: public RageSoundDriver
{
public:
	// The size of wo_buffers_ is the -maximum- number of blocks we can have.
	// We have to pre-allocate this size here. With the default target latency
	// of 118 ms, 13 buffers is sufficient for both 44100 and 48000 Hz sample
	// rates, but at least 20 buffers are needed if higher sample rates (such
	// as 96000 Hz) are used, or the game will crash during initialization.
	static const int kMaximumNumBlocks = 32;

	RageSoundDriver_WaveOut();
	~RageSoundDriver_WaveOut();
	RString Init();
	int64_t GetPosition() const;
	float GetPlayLatency() const;
	int GetSampleRate() const { return wo_samplerate_; }
private:
	static int MixerThread_start( void *p );
	void MixerThread();
	RageThread MixingThread;
	bool GetData();
	void SetupDecodingThread();

	HWAVEOUT waveout_handle_;
	HANDLE soundevent_handle_;
	WAVEHDR wo_buffers_[kMaximumNumBlocks];
	int wo_samplerate_;
	bool wo_shutdown_;
	int wo_last_cursor_position_;
	bool wo_init_success_;
	int wo_frames_per_block_;
	int wo_num_blocks_;
	int wo_blocksize_;
};

#endif

/*
 * (c) 2002-2004 Glenn Maynard
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
