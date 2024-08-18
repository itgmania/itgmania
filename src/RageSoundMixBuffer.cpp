#include "global.h"
#include "RageSoundMixBuffer.h"
#include "RageUtil.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>

RageSoundMixBuffer::RageSoundMixBuffer()
{
	m_iBufSize = 0;
	m_iBufUsed = 0;
	m_pMixbuf = nullptr;
	m_iOffset = 0;
}

RageSoundMixBuffer::~RageSoundMixBuffer()
{
	std::free(m_pMixbuf);
}

void RageSoundMixBuffer::Extend(unsigned iSamples) noexcept
{
	static const std::int_fast64_t chunksize = 1024; // in samples
	const std::int_fast64_t realsize = static_cast<std::int_fast64_t>(iSamples) + m_iOffset;
	std::int_fast64_t newsize = ((realsize + chunksize - 1) / chunksize) * chunksize; // find the next multiple of 1024

	if( m_iBufSize < newsize )
	{
		m_pMixbuf = static_cast<float*>(std::realloc(m_pMixbuf, sizeof(float) * newsize));
		m_iBufSize = newsize;
	}

	if( m_iBufUsed < realsize )
	{
		std::memset(m_pMixbuf + m_iBufUsed, 0, (realsize - m_iBufUsed) * sizeof(float));
		m_iBufUsed = realsize;
	}
}

void RageSoundMixBuffer::write( const float *pBuf, unsigned iSize, int iSourceStride, int iDestStride ) noexcept
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

void RageSoundMixBuffer::read_deinterlace( float **pBufs, int channels ) noexcept
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
