#include "global.h"
#include "RageSoundDriver_WaveOut.h"

#if defined(_MSC_VER)
#pragma comment(lib, "winmm.lib")
#endif

#include "RageTimer.h"
#include "RageLog.h"
#include "RageSound.h"
#include "RageUtil.h"
#include "RageSoundManager.h"
#include "PrefsManager.h"
#include "archutils/Win32/ErrorStrings.h"

#include <cstdint>
#include <vector>


REGISTER_SOUND_DRIVER_CLASS( WaveOut );

namespace {
	// We want to target a specific latency (118 ms) to ensure a consistent
	// experience when using either 44100 or 48000 Hz sample rate.
	// This value was chosen because it has the smallest difference in actual
	// latency (0.29 ms) between these two 44100 and 48000 Hz.
	// To achieve this, we use 512 frames per block and determine the number
	// of blocks and buffer size in frames based on the sample rate.
	constexpr int kTargetLatency = 118;
	constexpr int kChannels = 2;
	constexpr int kBlockSizeFrames = 512;
	constexpr int kBytesPerFrame = kChannels * 2;  // 16 bit

	inline int CalculateNumBlocks( int sampleRate )
	{
		return (sampleRate * kTargetLatency +
			(1000 * kBlockSizeFrames - 1)) /
			(1000 * kBlockSizeFrames);
	}
}  // namespace

static RString wo_ssprintf( MMRESULT err, const char *szFmt, ...)
{
	char szBuf[MAXERRORLENGTH];
	waveOutGetErrorText( err, szBuf, MAXERRORLENGTH );

	va_list va;
	va_start( va, szFmt );
	RString s = vssprintf( szFmt, va );
	va_end( va );

	return s += ssprintf( "(%s)", szBuf );
}

int RageSoundDriver_WaveOut::MixerThread_start( void *p )
{
	((RageSoundDriver_WaveOut *) p)->MixerThread();
	return 0;
}

void RageSoundDriver_WaveOut::MixerThread()
{
	if( !SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL) )
		LOG->Warn( werr_ssprintf(GetLastError(), "Failed to set sound thread priority") );

	while( !wo_shutdown_ )
	{
		while( GetData() )
			;

		WaitForSingleObject( soundevent_handle_, 10 );
	}

	waveOutReset( waveout_handle_ );
}

bool RageSoundDriver_WaveOut::GetData()
{
	/* Look for a free buffer. */
	int b;
	for( b = 0; b < wo_num_blocks_; ++b )
		if( wo_buffers_[b].dwFlags & WHDR_DONE )
			break;
	if( b == wo_num_blocks_ )
		return false;

	/* Call the callback. */
	this->Mix( (int16_t *) wo_buffers_[b].lpData, kBlockSizeFrames, wo_last_cursor_position_, GetPosition() );

	MMRESULT ret = waveOutWrite( waveout_handle_, &wo_buffers_[b], sizeof(wo_buffers_[b]) );
	if( ret != MMSYSERR_NOERROR )
	{
		Init();
		if (wo_init_success_ == false)
		{
			FAIL_M(wo_ssprintf(ret, "waveOutWrite failed"));
		}
	}

	/* Increment wo_last_cursor_position_. */
	wo_last_cursor_position_ += kBlockSizeFrames;

	return true;
}

void RageSoundDriver_WaveOut::SetupDecodingThread()
{
	if( !SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL) )
		LOG->Warn( werr_ssprintf(GetLastError(), "Failed to set sound thread priority") );
}

int64_t RageSoundDriver_WaveOut::GetPosition() const
{
	MMTIME tm;
	tm.wType = TIME_SAMPLES;
	MMRESULT ret = waveOutGetPosition( waveout_handle_, &tm, sizeof(tm) );
  	if( ret != MMSYSERR_NOERROR )
		FAIL_M( wo_ssprintf(ret, "waveOutGetPosition failed") );

	return tm.u.sample;
}

RageSoundDriver_WaveOut::RageSoundDriver_WaveOut()
	: MixingThread()
	, waveout_handle_(nullptr)
	, soundevent_handle_(CreateEvent(nullptr, false, true, nullptr))
	, wo_buffers_{}
	, wo_samplerate_(0)
	, wo_shutdown_(false)
	, wo_last_cursor_position_(0)
	, wo_init_success_(false)
	, wo_frames_per_block_(0)
	, wo_num_blocks_(1)
	, wo_blocksize_(0)
{
}

RString RageSoundDriver_WaveOut::Init()
{
	wo_init_success_ = false;
	wo_samplerate_ = PREFSMAN->m_iSoundPreferredSampleRate;
	if( wo_samplerate_ == 0 )
	{
		wo_samplerate_ = kFallbackSampleRate;
	}

	wo_num_blocks_ = CalculateNumBlocks( wo_samplerate_ );

	// Clamp wo_num_blocks_ to the range of kMaximumNumBlocks.
	if (wo_num_blocks_ < 1 || wo_num_blocks_ > kMaximumNumBlocks)
	{
		LOG->Warn("RageSoundDriver_WaveOut: wo_num_blocks_ out of range (%d); clamping", wo_num_blocks_);
		wo_num_blocks_ = std::clamp(wo_num_blocks_, 1, kMaximumNumBlocks);
	}

	wo_frames_per_block_ = kBlockSizeFrames * wo_num_blocks_;
	wo_blocksize_ = kBlockSizeFrames * kBytesPerFrame;

	WAVEFORMATEX fmt;
	fmt.wFormatTag = WAVE_FORMAT_PCM;
	fmt.nChannels = kChannels;
	fmt.cbSize = 0;
	fmt.nSamplesPerSec = wo_samplerate_;
	fmt.wBitsPerSample = 8 * (kBytesPerFrame / kChannels);
	fmt.nBlockAlign = fmt.nChannels * fmt.wBitsPerSample / 8;
	fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;

	std::vector<UINT> deviceIds;
	if (!PREFSMAN->m_iSoundDevice.Get().empty()) {
		std::vector<RString> portNames;
		split(PREFSMAN->m_iSoundDevice.Get(), ",", portNames, true);
		for (const RString& device : portNames) {
			int id = StringToInt(device, /*pos=*/0, /*base=*/10, /*exceptVal=*/-1);
			if (id != -1) {
				deviceIds.push_back(id);
			}
		}
	}
	deviceIds.push_back(WAVE_MAPPER);  // Fallback to WAVE_MAPPER

	MMRESULT ret = MMSYSERR_ERROR;
	for (UINT id : deviceIds) {
		ret = waveOutOpen( &waveout_handle_, id, &fmt, (DWORD_PTR) soundevent_handle_, NULL, CALLBACK_EVENT );
		if (ret == MMSYSERR_NOERROR) {
			break;
		}
	}
	if (ret != MMSYSERR_NOERROR) {
		return wo_ssprintf( ret, "waveOutOpen failed" );
	}


	ZERO( wo_buffers_ );
	for(int b = 0; b < wo_num_blocks_; ++b)
	{
		wo_buffers_[b].dwBufferLength = wo_blocksize_;
		wo_buffers_[b].lpData = new char[wo_blocksize_];
		ret = waveOutPrepareHeader( waveout_handle_, &wo_buffers_[b], sizeof(wo_buffers_[b]) );
		if( ret != MMSYSERR_NOERROR )
			return wo_ssprintf( ret, "waveOutPrepareHeader failed" );
		wo_buffers_[b].dwFlags |= WHDR_DONE;
	}

	LOG->Info( "WaveOut software mixing at %i hz", wo_samplerate_ );

	/* We have a very large writeahead; make sure we have a large enough decode
	 * buffer to recover cleanly from underruns. */
	SetDecodeBufferSize( wo_frames_per_block_ * 3/2 );
	StartDecodeThread();

	MixingThread.SetName( "Mixer thread" );
	MixingThread.Create( MixerThread_start, this );

	wo_init_success_ = true;
	return RString();
}

RageSoundDriver_WaveOut::~RageSoundDriver_WaveOut()
{
	/* Signal the mixing thread to quit. */
	if( MixingThread.IsCreated() )
	{
		wo_shutdown_ = true;
		SetEvent( soundevent_handle_ );
		LOG->Trace( "Shutting down mixer thread ..." );
		MixingThread.Wait();
		LOG->Trace( "Mixer thread shut down." );
	}

	if( waveout_handle_ != nullptr )
	{
		for( int b = 0; b < wo_num_blocks_ && wo_buffers_[b].lpData != nullptr; ++b )
		{
			waveOutUnprepareHeader( waveout_handle_, &wo_buffers_[b], sizeof(wo_buffers_[b]) );
			delete [] wo_buffers_[b].lpData;
		}

		waveOutClose( waveout_handle_ );
	}

	CloseHandle( soundevent_handle_ );
}

float RageSoundDriver_WaveOut::GetPlayLatency() const
{
	/* If we have a 1000-byte buffer, and we fill 100 bytes at a time, we
	 * almost always have between 900 and 1000 bytes filled; on average, 950. */
	return (wo_frames_per_block_ - kBlockSizeFrames/2) * (1.0f / wo_samplerate_);
}

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
