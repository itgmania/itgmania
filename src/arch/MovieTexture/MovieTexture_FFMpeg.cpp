#include "global.h"
#include "MovieTexture_FFMpeg.h"

#include "RageDisplay.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "RageFile.h"
#include "RageSurface.h"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <cstdarg>

static void FixLilEndian()
{
	if constexpr ( !Endian::little ) {
		return;
	}

	static bool Initialized = false;
	if( Initialized )
		return;
	Initialized = true;

	for( int i = 0; i < AVPixelFormats[i].bpp; ++i )
	{
		AVPixelFormat_t &pf = AVPixelFormats[i];

		if( !pf.bByteSwapOnLittleEndian )
			continue;

		for( int mask = 0; mask < 4; ++mask)
		{
			int m = pf.masks[mask];
			switch( pf.bpp )
			{
				case 24: m = Swap24(m); break;
				case 32: m = Swap32(m); break;
				default:
					 FAIL_M(ssprintf("Unsupported BPP value: %i", pf.bpp));
			}
			pf.masks[mask] = m;
		}
	}
}

static int FindCompatibleAVFormat( bool bHighColor )
{
	for( int i = 0; AVPixelFormats[i].bpp; ++i )
	{
		AVPixelFormat_t &fmt = AVPixelFormats[i];
		if( fmt.YUV != PixelFormatYCbCr_Invalid )
		{
			EffectMode em = MovieTexture_Generic::GetEffectMode( fmt.YUV );
			if( !DISPLAY->IsEffectModeSupported(em) )
				continue;
		}
		else if( fmt.bHighColor != bHighColor )
		{
			continue;
		}

		RagePixelFormat pixfmt = DISPLAY->FindPixelFormat( fmt.bpp,
				fmt.masks[0],
				fmt.masks[1],
				fmt.masks[2],
				fmt.masks[3],
				true /* realtime */
				);

		if( pixfmt == RagePixelFormat_Invalid )
			continue;

		return i;
	}

	return -1;
}

RageSurface *RageMovieTextureDriver_FFMpeg::AVCodecCreateCompatibleSurface( int iTextureWidth, int iTextureHeight, bool bPreferHighColor, int &iAVTexfmt, MovieDecoderPixelFormatYCbCr &fmtout )
{
	FixLilEndian();

	int iAVTexfmtIndex = FindCompatibleAVFormat( bPreferHighColor );
	if( iAVTexfmtIndex == -1 )
		iAVTexfmtIndex = FindCompatibleAVFormat( !bPreferHighColor );

	if( iAVTexfmtIndex == -1 )
	{
		/* No dice.  Use the first avcodec format of the preferred bit depth,
		 * and let the display system convert. */
		for( iAVTexfmtIndex = 0; AVPixelFormats[iAVTexfmtIndex].bpp; ++iAVTexfmtIndex )
			if( AVPixelFormats[iAVTexfmtIndex].bHighColor == bPreferHighColor )
				break;
		ASSERT( AVPixelFormats[iAVTexfmtIndex].bpp != 0 );
	}

	const AVPixelFormat_t *pfd = &AVPixelFormats[iAVTexfmtIndex];
	iAVTexfmt = pfd->pf;
	fmtout = pfd->YUV;

	LOG->Trace( "Texture pixel format: %i %i (%ibpp, %08x %08x %08x %08x)", iAVTexfmt, fmtout,
		pfd->bpp, pfd->masks[0], pfd->masks[1], pfd->masks[2], pfd->masks[3] );

	if( pfd->YUV == PixelFormatYCbCr_YUYV422 )
		iTextureWidth /= 2;

	return CreateSurface( iTextureWidth, iTextureHeight, pfd->bpp,
		pfd->masks[0], pfd->masks[1], pfd->masks[2], pfd->masks[3] );
}

MovieDecoder_FFMpeg::MovieDecoder_FFMpeg()
{
	FixLilEndian();

	m_fctx = nullptr;
	m_pStream = nullptr;
	m_iCurrentPacketOffset = -1;
	m_Frame = avcodec::av_frame_alloc();

	Init();
}

MovieDecoder_FFMpeg::~MovieDecoder_FFMpeg()
{
	if (m_swsctx)
	{
		avcodec::sws_freeContext(m_swsctx);
		m_swsctx = nullptr;
	}
	if (m_avioContext != nullptr )
	{
		RageFile *file = (RageFile *)m_avioContext->opaque;
		file->Close();
		delete file;
		avcodec::av_free(m_avioContext);
	}
	if ( m_buffer != nullptr )
	{
		avcodec::av_free(m_buffer);
	}
	if ( m_pStreamCodec != nullptr)
	{
		avcodec::avcodec_free_context(&m_pStreamCodec);
	}
}

void MovieDecoder_FFMpeg::Init()
{
	m_iEOF = 0;
	m_fTimestamp = 0;
	m_iFrameNumber = 0;
	m_totalFrames = 0;
	m_fTimestampOffset = 0;
	m_swsctx = nullptr;
	m_avioContext = nullptr;
	m_buffer = nullptr;
}

float MovieDecoder_FFMpeg::GetTimestamp() const
{
	// Always display the first frame.
	if (m_iFrameNumber == 0) {
		return 0;
	}

	// In a logical situation, this means that display is outpacing decoding.
	if (m_iFrameNumber >= m_FrameBuffer.size()) {
		return 0;
	}
	return m_FrameBuffer[m_iFrameNumber].frameTimestamp;
}

bool MovieDecoder_FFMpeg::IsCurrentFrameReady() {
	// We're displaying faster than decoding. Do not even try to display the frame.
	if (m_iFrameNumber >= m_FrameBuffer.size()) {
		return false;
	}
	// If the whole movie is decoded, then the frame is definitely ready.
	if (m_iEOF) {
		return true;
	}

	std::lock_guard<std::mutex>(m_FrameBuffer[m_iFrameNumber].lock);
	if (m_FrameBuffer[m_iFrameNumber].skip) {
		LOG->Info("Frame %i not decoded, skipping...", m_iFrameNumber);
		return true;
	}
	if (!m_FrameBuffer[m_iFrameNumber].decoded) {
		LOG->Info("Frame %i not decoded and was not skipped, total frames: %i", m_iFrameNumber, m_totalFrames);
	}
	return m_FrameBuffer[m_iFrameNumber].decoded;
}

int MovieDecoder_FFMpeg::DecodeNextFrame()
{
	// Add in a new FrameBuffer entry, and lock it immediately.
	m_FrameBuffer.push_back(FrameHolder());
	std::lock_guard<std::mutex>(m_FrameBuffer.back().lock);
	int status = SendPacketToBuffer();
	if (status < 0) {
		return status;
	}
	status = DecodePacketInBuffer();
	if (firstFrame) {
		firstFrame = false;
	}
	return status;
}

int MovieDecoder_FFMpeg::DecodeMovie()
{
	using std::chrono::operator""ms;

	// The first frame expected to be decoded and drawn already,
	// that is handled by MovieTexture_Generic::Init().
	int frameNum = 0;
	while (!m_iEOF) {
		// This wake up time could be tied to the RageTimer, but as it doesn't
		// need to sync with other parts of ITGm, using chrono is fine.
		// The 1ms time here is arbitrary, and means that the game will decode
		// at a maximum speed of 1 frame per ms (or 1000 fps).
		auto wake_up = std::chrono::steady_clock::now() + 1ms;

		int status = DecodeNextFrame();

		// If cancelled (quitting a song, scrolling the banner), or fatal error,
		// stop decoding.
		if (status < 0) {
			return status;
		}

		frameNum++;

		// This means when opening the file, less frames were detected than
		// there actually are. Increment to keep up so we don't end the video
		// early during display.
		if (frameNum - 1 > m_totalFrames) {
			m_totalFrames++;
		}

		std::this_thread::sleep_until(wake_up);
	}
	return 0;
}

int MovieDecoder_FFMpeg::SendPacketToBuffer()
{
	if (cancel) {
		return -2;
	}
	if (m_iEOF > 0) {
		return 0;
	}

	while (true)
	{
		int ret = avcodec::av_read_frame(m_fctx, &m_FrameBuffer.back().packet);
		/* XXX: why is avformat returning AVERROR_NOMEM on EOF? */
		if (ret < 0)
		{
			/* EOF. */
			m_iEOF = 1;
			m_FrameBuffer.pop_back(); // Don't display an EoF frame.
			// If we had to approximate the number of frames, set the actual
			// total number of frames. This is benign even if we did have an
			// accurate frame count at the start.
			m_totalFrames = m_FrameBuffer.size();
			return 0;
		}

		if (m_FrameBuffer.back().packet.stream_index == m_pStream->index)
		{
			m_iCurrentPacketOffset = 0;
			return 1;
		}
		/* It's not for the video stream; ignore it. */
		avcodec::av_packet_unref(&m_FrameBuffer.back().packet);
	}
}

int MovieDecoder_FFMpeg::DecodePacketInBuffer() {
	if (cancel) {
		return -2;
	}
	if (m_iEOF == 0 && m_iCurrentPacketOffset == -1) {
		return 0; /* no packet */
	}

	while (m_iEOF == 0 && m_iCurrentPacketOffset <= m_FrameBuffer.back().packet.size)
	{
		/* If we have no data on the first frame, just return EOF; passing an empty packet
		 * to avcodec_decode_video in this case is crashing it.  However, passing an empty
		 * packet is normal with B-frames, to flush.  This may be unnecessary in newer
		 * versions of avcodec, but I'm waiting until a new stable release to upgrade. */
		if (m_FrameBuffer.back().packet.size == 0 && firstFrame) {
			return 0; /* eof */
		}

		/* Hack: we need to send size = 0 to flush frames at the end, but we have
		 * to give it a buffer to read from since it tries to read anyway. */
		m_FrameBuffer.back().packet.data = m_FrameBuffer.back().packet.size ? m_FrameBuffer.back().packet.data : nullptr;
		int len = m_FrameBuffer.back().packet.size;
		avcodec::avcodec_send_packet(m_pStreamCodec, &m_FrameBuffer.back().packet);
		int avcodec_return = avcodec::avcodec_receive_frame(m_pStreamCodec, &m_FrameBuffer.back().frame);

		if (len < 0)
		{
			LOG->Warn("avcodec_decode_video2 fatal error, packet size negative: %i", len);
			return -1;
		}

		m_iCurrentPacketOffset += len;

		if (avcodec_return != 0)
		{
			LOG->Warn(
				"Frame number %i not successfully decoded into buffer. avcodec_receive_frame status: %i",
				static_cast<int>(m_FrameBuffer.size() - 1),
				avcodec_return);
			continue;
		}

		if (m_FrameBuffer.back().frame.pkt_dts != AV_NOPTS_VALUE)
		{
			m_FrameBuffer.back().frameTimestamp = (float)(m_FrameBuffer.back().frame.pkt_dts * av_q2d(m_pStream->time_base));
		}
		else
		{
			/* If the timestamp is zero, this frame is to be played at the
			 * time of the last frame plus the length of the last frame. */
			if (!firstFrame) {
				m_FrameBuffer.back().frameTimestamp += m_FrameBuffer[m_FrameBuffer.size() - 2].frameDelay;
			}
			else {
				m_FrameBuffer.back().frameTimestamp = 0;
			}
		}

		// Length of this frame, only used as a fallback for getting the frame
		// timestamp above.
		m_FrameBuffer.back().frameDelay = (float)av_q2d(m_pStream->time_base);
		m_FrameBuffer.back().frameDelay += m_FrameBuffer.back().frame.repeat_pict * (m_FrameBuffer.back().frameDelay * 0.5f);
		m_FrameBuffer.back().decoded = true;

		return 1;
	}

	// This if statement means the packet did not decode correctly. This is not
	// necessarily fatal for video playback, but out of caution the frame should
	// be skipped.
	if (!m_FrameBuffer.back().decoded) {
		m_FrameBuffer.back().skip = true;
	}

	return 0; /* packet done */
}

bool MovieDecoder_FFMpeg::SkipNextFrame() {
	if (m_iFrameNumber > (m_totalFrames - 1)) {
		return true;
	}
	if (m_FrameBuffer[m_iFrameNumber].skip) {
		m_iFrameNumber++;
		return true;
	}
	return false;
}

bool MovieDecoder_FFMpeg::GetFrame(RageSurface* pSurface)
{
	avcodec::AVFrame pict;
	pict.data[0] = (unsigned char*)pSurface->pixels;
	pict.linesize[0] = pSurface->pitch;

	/* XXX 1: Do this in one of the Open() methods instead?
	 * XXX 2: The problem of doing this in Open() is that m_AVTexfmt is not
	 * already initialized with its correct value.
	 */
	if (m_swsctx == nullptr)
	{
		m_swsctx = avcodec::sws_getCachedContext(m_swsctx,
			GetWidth(), GetHeight(), m_pStreamCodec->pix_fmt,
			GetWidth(), GetHeight(), m_AVTexfmt,
			sws_flags, nullptr, nullptr, nullptr);
		if (m_swsctx == nullptr)
		{
			LOG->Warn("Cannot initialize sws conversion context for (%d,%d) %d->%d", GetWidth(), GetHeight(), m_pStreamCodec->pix_fmt, m_AVTexfmt);
			return false;
		}
	}

	avcodec::sws_scale(m_swsctx,
		m_FrameBuffer[m_iFrameNumber].frame.data, m_FrameBuffer[m_iFrameNumber].frame.linesize, 0, GetHeight(),
		pict.data, pict.linesize);

	// Don't advance the frame number past the (potential) end of the buffer.
	// This can happen if display is outpacing decoding, or if we're at the
	// end of file.
	if (m_iFrameNumber >= (m_totalFrames - 1)) {
		return m_iEOF;
	}
	m_iFrameNumber++;
	return false;
}

static RString averr_ssprintf( int err, const char *fmt, ... )
{
	ASSERT( err < 0 );

	va_list     va;
	va_start(va, fmt);
	RString s = vssprintf( fmt, va );
	va_end(va);

	std::size_t errbuf_size = 512;
	char* errbuf = new char[errbuf_size];
	avcodec::av_strerror(err, errbuf, errbuf_size);
	RString Error = ssprintf("%i: %s", err, errbuf);
	delete[] errbuf;

	return s + " (" + Error + ")";
}

static int AVIORageFile_ReadPacket( void *opaque, std::uint8_t *buf, int buf_size )
{
	RageFile *f = (RageFile *)opaque;
	int n = f->Read( buf, buf_size );
	if (n == 0)
		return AVERROR_EOF;
	return n;
}

static std::int64_t AVIORageFile_Seek( void *opaque, std::int64_t offset, int whence )
{
    RageFile *f = (RageFile *)opaque;
    if( whence == AVSEEK_SIZE )
		return f->GetFileSize();

	if( whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END )
	{
		LOG->Trace("Error: unsupported seek whence: %d", whence);
		return -1;
	}

	return f->Seek( (int) offset, whence );
}

RString MovieDecoder_FFMpeg::Open( RString sFile )
{
	m_fctx = avcodec::avformat_alloc_context();
	if( !m_fctx )
		return "AVCodec: Couldn't allocate context";

	RageFile *f = new RageFile;

	if( !f->Open(sFile, RageFile::READ) )
	{
		RString errorMessage = f->GetError();
		RString error = ssprintf("MovieDecoder_FFMpeg: Error opening \"%s\": %s", sFile.c_str(), errorMessage.c_str() );
		delete f;
		return error;
	}

	m_buffer = (unsigned char *)avcodec::av_malloc(STEPMANIA_FFMPEG_BUFFER_SIZE);
	m_avioContext = avcodec::avio_alloc_context(m_buffer, STEPMANIA_FFMPEG_BUFFER_SIZE, 0, f, AVIORageFile_ReadPacket, nullptr, AVIORageFile_Seek);
	m_fctx->pb = m_avioContext;
	int ret = avcodec::avformat_open_input( &m_fctx, sFile.c_str(), nullptr, nullptr );
	if( ret < 0 )
		return RString( averr_ssprintf(ret, "AVCodec: Couldn't open \"%s\"", sFile.c_str()) );

	ret = avcodec::avformat_find_stream_info( m_fctx, nullptr );
	if( ret < 0 )
		return RString( averr_ssprintf(ret, "AVCodec (%s): Couldn't find codec parameters", sFile.c_str()) );

	int stream_idx = avcodec::av_find_best_stream( m_fctx, avcodec::AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0 );
	if ( stream_idx < 0 ||
		static_cast<unsigned int>(stream_idx) >= m_fctx->nb_streams ||
		m_fctx->streams[stream_idx] == nullptr )
		return "Couldn't find any video streams";
	m_pStream = m_fctx->streams[stream_idx];
	m_pStreamCodec = avcodec::avcodec_alloc_context3(nullptr);
	if (avcodec::avcodec_parameters_to_context(m_pStreamCodec, m_pStream->codecpar) < 0)
		return ssprintf("Could not get context from parameters");

	if( m_pStreamCodec->codec_id == avcodec::AV_CODEC_ID_NONE )
		return ssprintf( "Unsupported codec %08x", m_pStreamCodec->codec_tag );

	RString sError = OpenCodec();
	if( !sError.empty() )
		return ssprintf( "AVCodec (%s): %s", sFile.c_str(), sError.c_str() );

	LOG->Trace("Bitrate: %i", static_cast<int>(m_pStreamCodec->bit_rate));
	LOG->Trace("Codec pixel format: %s", avcodec::av_get_pix_fmt_name(m_pStreamCodec->pix_fmt));
	m_totalFrames = m_pStream->nb_frames;
	if (m_totalFrames <= 0) {
		// Sometimes we might not get a correct frame count.
		// In that case, approximate and fix it later.
		m_totalFrames = m_fctx->duration // microseconds
			* (m_pStream->avg_frame_rate.num) / (m_pStream->avg_frame_rate.den) / (1000000);
		LOG->Trace("Number of frames provided is inaccurate, estimating.");
	}
	LOG->Trace("Number of frames detected: %i", m_totalFrames);

	return RString();
}

RString MovieDecoder_FFMpeg::OpenCodec()
{
	Init();

	ASSERT( m_pStream != nullptr );
	if( m_pStreamCodec->codec )
		avcodec::avcodec_close( m_pStreamCodec );

	const avcodec::AVCodec *pCodec = avcodec::avcodec_find_decoder( m_pStreamCodec->codec_id );
	if( pCodec == nullptr )
		return ssprintf( "Couldn't find decoder %i", m_pStreamCodec->codec_id );

	m_pStreamCodec->workaround_bugs   = 1;
	m_pStreamCodec->idct_algo         = FF_IDCT_AUTO;
	m_pStreamCodec->error_concealment = 3;

	LOG->Trace("Opening codec %s", pCodec->name );

	int ret = avcodec::avcodec_open2( m_pStreamCodec, pCodec, nullptr );
	if( ret < 0 )
		return RString( averr_ssprintf(ret, "Couldn't open codec \"%s\"", pCodec->name) );
	ASSERT( m_pStreamCodec->codec != nullptr );

	return RString();
}

void MovieDecoder_FFMpeg::Close()
{
	if( m_pStream && m_pStreamCodec->codec )
	{
		avcodec::avcodec_close( m_pStreamCodec );
		m_pStream = nullptr;
	}

	if( m_fctx )
	{
		avcodec::avformat_close_input( &m_fctx );
		m_fctx = nullptr;
	}

	Init();
}

void MovieDecoder_FFMpeg::Rewind()
{
	m_iFrameNumber = 0;
}

RageSurface *MovieDecoder_FFMpeg::CreateCompatibleSurface( int iTextureWidth, int iTextureHeight, bool bPreferHighColor, MovieDecoderPixelFormatYCbCr &fmtout )
{
	return RageMovieTextureDriver_FFMpeg::AVCodecCreateCompatibleSurface( iTextureWidth, iTextureHeight, bPreferHighColor, *ConvertValue<int>(&m_AVTexfmt), fmtout );
}

MovieTexture_FFMpeg::MovieTexture_FFMpeg( RageTextureID ID ):
	MovieTexture_Generic( ID, new MovieDecoder_FFMpeg )
{
}

RageMovieTexture *RageMovieTextureDriver_FFMpeg::Create( RageTextureID ID, RString &sError )
{
	MovieTexture_FFMpeg *pRet = new MovieTexture_FFMpeg( ID );
	sError = pRet->Init();
	if( !sError.empty() )
		SAFE_DELETE( pRet );
	return pRet;
}

REGISTER_MOVIE_TEXTURE_CLASS( FFMpeg );

/*
 * (c) 2003-2005 Glenn Maynard
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
