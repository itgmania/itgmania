#include "global.h"
#include "RageSoundMixBuffer.h"
#include "RageUtil.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>

RageSoundMixBuffer::RageSoundMixBuffer()
{
	m_pMixbuf = static_cast<float*>(std::malloc(BUF_SIZE * sizeof(float)));
	if (m_pMixbuf == nullptr)
	{
		ASSERT_M(false, "Failed to allocate memory for the sound mixing buffer");
	}
	m_iBufSize = BUF_SIZE;
	std::memset(m_pMixbuf, 0, m_iBufSize * sizeof(float));
	m_iBufUsed = 0;
	m_iOffset = 0;
}


RageSoundMixBuffer::~RageSoundMixBuffer()
{
	std::free(m_pMixbuf);
}

/* write() will start mixing iOffset samples into the buffer.  Be careful; this is
 * measured in samples, not frames, so if the data is stereo, multiply by two. */
void RageSoundMixBuffer::SetWriteOffset( int iOffset )
{
	m_iOffset = iOffset;
}

void RageSoundMixBuffer::Extend(unsigned iSamples)
{
	const uint64_t realsize = static_cast<uint64_t>(iSamples) + static_cast<uint64_t>(m_iOffset);
	if( m_iBufSize < realsize )
	{
		m_pMixbuf = static_cast<float*>(std::realloc(m_pMixbuf, sizeof(float) * realsize));
		if (m_pMixbuf == nullptr)
		{
			ASSERT_M(false, "Failed to re-allocate memory for the sound mixing buffer.");
		}
		m_iBufSize = realsize;
	}

	if( m_iBufUsed < realsize )
	{
		std::memset(m_pMixbuf + m_iBufUsed, 0, (realsize - m_iBufUsed) * sizeof(float));
		m_iBufUsed = realsize;
	}
}

void RageSoundMixBuffer::write( const float *pBuf, unsigned iSize, int iSourceStride, int iDestStride )
{
	if( iSize == 0 )
		return;

	// iSize = 3, iDestStride = 2 uses 4 frames.  Don't allocate the stride of the last sample.
	Extend( iSize * iDestStride - (iDestStride-1) );

	// Scale volume and add.
	float *pDestBuf = m_pMixbuf+m_iOffset;

	while( iSize )
	{
		*pDestBuf += *pBuf;
		pBuf += iSourceStride;
		pDestBuf += iDestStride;
		--iSize;
	}
}

void RageSoundMixBuffer::read( int16_t *pBuf )
{
	for( unsigned iPos = 0; iPos < m_iBufUsed; ++iPos )
	{
		float iOut = m_pMixbuf[iPos];
		iOut = std::clamp( iOut, -1.0f, +1.0f );
		pBuf[iPos] = static_cast<int>((iOut * 32767) + 0.5);
	}
	m_iBufUsed = 0;
}

void RageSoundMixBuffer::read( float *pBuf )
{
	std::memcpy( pBuf, m_pMixbuf, m_iBufUsed * sizeof(float) );
	m_iBufUsed = 0;
}

void RageSoundMixBuffer::read_deinterlace( float **pBufs, int channels )
{
	for( unsigned i = 0; i < m_iBufUsed / channels; ++i )
		for( int ch = 0; ch < channels; ++ch )
			pBufs[ch][i] = m_pMixbuf[channels * i + ch];
	m_iBufUsed = 0;
}

/*
 * Copyright (c) 2002-2004 Glenn Maynard
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
